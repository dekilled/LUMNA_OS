//! Telefone: ligações pelo ModemManager (D-Bus), o histórico de chamadas e os contatos.
//!
//! O ModemManager avisa por sinais quando uma chamada aparece, muda de estado ou some; a cada
//! aviso o shell relê as chamadas e manda a lista pra tela (`lumna://calls`). Com alguma chamada
//! em andamento a camada `mobile-call` aparece por cima de tudo; tocando, o celular vibra.
//! Quando uma chamada termina, ela entra no histórico (`~/.local/share/lumna/phone/calls.json`).
//!
//! O áudio da ligação (q6voiced + UCM do sdm845) ainda não existe: a chamada conecta, mas o som
//! não passa pelo alto-falante e pelo microfone.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use futures_util::StreamExt;
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter};
use zbus::zvariant::{OwnedObjectPath, OwnedValue, Value};
use zbus::Connection;

use crate::layer;

const MM: &str = "org.freedesktop.ModemManager1";
const MM_PATH: &str = "/org/freedesktop/ModemManager1";
const VOICE: &str = "org.freedesktop.ModemManager1.Modem.Voice";
const CALL: &str = "org.freedesktop.ModemManager1.Call";

fn now() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs())
}

fn data_dir() -> PathBuf {
    let base = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".local/share")))
        .unwrap_or_else(|| PathBuf::from("/tmp"));
    base.join("lumna/phone")
}

fn read_json<T: for<'d> Deserialize<'d> + Default>(name: &str) -> T {
    std::fs::read_to_string(data_dir().join(name)).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

fn write_json<T: Serialize>(name: &str, value: &T) -> Result<(), String> {
    let dir = data_dir();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let tmp = dir.join(format!("{name}.tmp"));
    std::fs::write(&tmp, serde_json::to_vec_pretty(value).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    std::fs::rename(tmp, dir.join(name)).map_err(|e| e.to_string())
}

// ---- chamadas ----

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Call {
    pub path: String,
    pub number: String,
    /// dialing, ringing-out, incoming, active, held, waiting, ended
    pub state: String,
    pub incoming: bool,
    /// Quando a conversa começou (segundos Unix), pra contar o tempo na tela.
    pub active_since: Option<u64>,
}

/// MMCallState → nome curto.
fn state_name(state: i32) -> &'static str {
    match state {
        1 => "dialing",
        2 => "ringing-out",
        3 => "incoming",
        4 => "active",
        5 => "held",
        6 => "waiting",
        7 => "ended",
        _ => "unknown",
    }
}

struct Tracked {
    number: String,
    incoming: bool,
    first_seen: u64,
    active_since: Option<u64>,
    logged: bool,
}

static TRACKED: Mutex<Option<HashMap<String, Tracked>>> = Mutex::new(None);
static RINGING: AtomicBool = AtomicBool::new(false);
static CALLS: Mutex<Vec<Call>> = Mutex::new(Vec::new());

async fn system() -> Result<Connection, String> {
    Connection::system().await.map_err(|e| format!("D-Bus do sistema: {e}"))
}

async fn call_mm<B, R>(conn: &Connection, path: &str, interface: &str, method: &str, body: &B) -> Result<R, String>
where
    B: serde::Serialize + zbus::zvariant::DynamicType,
    R: for<'d> zbus::zvariant::DynamicDeserialize<'d>,
{
    let reply = conn.call_method(Some(MM), path, Some(interface), method, body).await.map_err(|e| explain(&e))?;
    reply.body().deserialize::<R>().map_err(|e| e.to_string())
}

fn explain(e: &zbus::Error) -> String {
    let text = e.to_string();
    if text.contains("ServiceUnknown") || text.contains("was not provided") {
        "o ModemManager não está rodando (sh mobile/session/modem-wifi.sh)".into()
    } else {
        text
    }
}

/// O primeiro modem que sabe fazer ligação.
async fn voice_modem(conn: &Connection) -> Result<String, String> {
    let manager = zbus::fdo::ObjectManagerProxy::builder(conn)
        .destination(MM)
        .map_err(|e| e.to_string())?
        .path(MM_PATH)
        .map_err(|e| e.to_string())?
        .build()
        .await
        .map_err(|e| e.to_string())?;
    let objects = manager.get_managed_objects().await.map_err(|e| explain(&e.into()))?;
    let mut modems: Vec<String> = objects
        .into_iter()
        .filter(|(_, interfaces)| interfaces.keys().any(|i| i.as_str() == VOICE))
        .map(|(path, _)| path.to_string())
        .collect();
    modems.sort();
    modems.into_iter().next().ok_or_else(|| "nenhum modem com voz (o chip está ativo?)".into())
}

async fn call_props(conn: &Connection, path: &str) -> Option<HashMap<String, OwnedValue>> {
    call_mm(conn, path, "org.freedesktop.DBus.Properties", "GetAll", &(CALL,)).await.ok()
}

fn prop_i32(props: &HashMap<String, OwnedValue>, key: &str) -> i32 {
    props.get(key).and_then(|v| i32::try_from(v).ok()).unwrap_or(0)
}

fn prop_string(props: &HashMap<String, OwnedValue>, key: &str) -> String {
    props.get(key).and_then(|v| v.try_clone().ok()).and_then(|v| String::try_from(v).ok()).unwrap_or_default()
}

/// Lê as chamadas do modem, atualiza o que está sendo acompanhado e registra as que acabaram.
async fn read_calls(conn: &Connection) -> Vec<Call> {
    let Ok(modem) = voice_modem(conn).await else { return Vec::new() };
    let paths: Vec<OwnedObjectPath> = call_mm(conn, &modem, VOICE, "ListCalls", &()).await.unwrap_or_default();
    // primeiro lê tudo do D-Bus; depois mexe no estado (o lock não atravessa um await)
    let mut read = Vec::new();
    for path in paths {
        let path = path.to_string();
        let Some(props) = call_props(conn, &path).await else { continue };
        read.push((path, state_name(prop_i32(&props, "State")), prop_i32(&props, "Direction") == 1, prop_string(&props, "Number")));
    }
    let mut calls = Vec::new();
    let mut guard = TRACKED.lock().unwrap();
    let tracked = guard.get_or_insert_with(HashMap::new);
    let mut seen = Vec::new();
    for (path, state, incoming, number) in read {
        let entry = tracked.entry(path.clone()).or_insert_with(|| Tracked {
            number: number.clone(),
            incoming,
            first_seen: now(),
            active_since: None,
            logged: false,
        });
        if !number.is_empty() {
            entry.number = number.clone();
        }
        if state == "active" && entry.active_since.is_none() {
            entry.active_since = Some(now());
        }
        seen.push(path.clone());
        calls.push(Call { path, number: entry.number.clone(), state: state.into(), incoming, active_since: entry.active_since });
    }
    // O que acabou (ou sumiu do modem) vai pro histórico, uma vez só.
    let ended: Vec<String> = tracked
        .iter()
        .filter(|(path, t)| !t.logged && (!seen.contains(path) || calls.iter().any(|c| &c.path == *path && c.state == "ended")))
        .map(|(path, _)| path.clone())
        .collect();
    for path in &ended {
        if let Some(t) = tracked.get_mut(path) {
            t.logged = true;
            log_call(t);
        }
    }
    tracked.retain(|path, _| seen.contains(path));
    calls
}

fn log_call(t: &Tracked) {
    let mut log: Vec<LogEntry> = read_json("calls.json");
    let end = now();
    log.insert(
        0,
        LogEntry {
            number: t.number.clone(),
            incoming: t.incoming,
            missed: t.incoming && t.active_since.is_none(),
            at: t.first_seen,
            duration: t.active_since.map_or(0, |s| end.saturating_sub(s)),
        },
    );
    log.truncate(500);
    if let Err(e) = write_json("calls.json", &log) {
        eprintln!("lumna-shell: histórico de chamadas: {e}");
    }
}

/// Mostra a tela de chamada enquanto houver alguma; vibra enquanto uma estiver tocando.
fn present(app: &AppHandle, calls: &[Call]) {
    let live = calls.iter().any(|c| c.state != "ended");
    let ringing = calls.iter().any(|c| c.state == "incoming" || c.state == "waiting");
    let _ = app.emit("lumna://calls", calls);
    let handle = app.clone();
    if live {
        let _ = app.run_on_main_thread(move || {
            if !layer::is_visible("mobile-call") {
                layer::set_visible("mobile-call", true);
                let _ = handle.emit_to("mobile-call", "lumna://mobile-panel", true);
            }
        });
    } else {
        // deixa o "chamada encerrada" na tela por um instante
        std::thread::spawn(move || {
            std::thread::sleep(Duration::from_millis(1500));
            if CALLS.lock().unwrap().iter().all(|c| c.state == "ended") {
                let _ = handle.run_on_main_thread(|| layer::set_visible("mobile-call", false));
            }
        });
    }
    if ringing && !RINGING.swap(true, Ordering::SeqCst) {
        std::thread::spawn(|| {
            while RINGING.load(Ordering::SeqCst) {
                crate::haptics::buzz(700);
                std::thread::sleep(Duration::from_millis(1600));
            }
        });
    } else if !ringing {
        RINGING.store(false, Ordering::SeqCst);
    }
}

async fn refresh(app: &AppHandle, conn: &Connection) {
    let calls = read_calls(conn).await;
    let changed = *CALLS.lock().unwrap() != calls;
    if changed {
        *CALLS.lock().unwrap() = calls.clone();
        present(app, &calls);
    }
}

/// Acompanha o ModemManager: cada sinal dele relê as chamadas.
pub fn watch(app: AppHandle) {
    tauri::async_runtime::spawn(async move {
        let mut wait = Duration::from_secs(2);
        loop {
            if let Err(err) = follow(&app).await {
                eprintln!("lumna-shell: telefone: {err} (nova tentativa em {wait:?})");
            }
            tokio::time::sleep(wait).await;
            wait = (wait * 2).min(Duration::from_secs(60));
        }
    });
}

async fn follow(app: &AppHandle) -> Result<(), String> {
    let conn = system().await?;
    let rule = zbus::MatchRule::builder()
        .msg_type(zbus::message::Type::Signal)
        .sender(MM)
        .map_err(|e| e.to_string())?
        .build();
    let mut stream = zbus::MessageStream::for_match_rule(rule, &conn, Some(64)).await.map_err(|e| e.to_string())?;
    refresh(app, &conn).await;
    while stream.next().await.is_some() {
        // um monte de sinais chega junto (propriedades, estado): junta numa leitura só
        while let Ok(Some(_)) = tokio::time::timeout(Duration::from_millis(150), stream.next()).await {}
        refresh(app, &conn).await;
    }
    Err("conexão com o D-Bus caiu".into())
}

#[tauri::command]
pub fn phone_calls() -> Vec<Call> {
    CALLS.lock().unwrap().clone()
}

/// Liga pra um número (cria a chamada no modem e disca).
#[tauri::command]
pub async fn phone_call(number: String) -> Result<(), String> {
    let number: String = number.chars().filter(|c| c.is_ascii_digit() || matches!(c, '+' | '*' | '#')).collect();
    if number.is_empty() {
        return Err("digite um número".into());
    }
    let conn = system().await?;
    let modem = voice_modem(&conn).await?;
    let mut props: HashMap<&str, Value> = HashMap::new();
    props.insert("number", Value::from(number.as_str()));
    let path: OwnedObjectPath = call_mm(&conn, &modem, VOICE, "CreateCall", &(props,)).await?;
    call_mm::<_, ()>(&conn, path.as_str(), CALL, "Start", &()).await
}

#[tauri::command]
pub async fn phone_accept(path: String) -> Result<(), String> {
    RINGING.store(false, Ordering::SeqCst);
    call_mm::<_, ()>(&system().await?, &path, CALL, "Accept", &()).await
}

/// Desliga uma chamada (ou recusa, se estiver tocando). Sem `path`, desliga todas.
#[tauri::command]
pub async fn phone_hangup(path: Option<String>) -> Result<(), String> {
    RINGING.store(false, Ordering::SeqCst);
    let conn = system().await?;
    match path {
        Some(path) => call_mm::<_, ()>(&conn, &path, CALL, "Hangup", &()).await,
        None => call_mm::<_, ()>(&conn, &voice_modem(&conn).await?, VOICE, "HangupAll", &()).await,
    }
}

/// Tecla durante a ligação (menus de atendimento: "digite 1...").
#[tauri::command]
pub async fn phone_dtmf(path: String, tone: String) -> Result<(), String> {
    call_mm::<_, ()>(&system().await?, &path, CALL, "SendDtmf", &(tone.as_str(),)).await
}

/// Silencia o microfone (vale quando o áudio da ligação existir).
#[tauri::command]
pub fn phone_mute(on: bool) -> Result<(), String> {
    crate::status::wpctl(&["set-mute", "@DEFAULT_AUDIO_SOURCE@", if on { "1" } else { "0" }])
}

// ---- histórico ----

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct LogEntry {
    pub number: String,
    pub incoming: bool,
    pub missed: bool,
    pub at: u64,
    /// segundos de conversa
    pub duration: u64,
}

#[tauri::command]
pub fn phone_log() -> Vec<LogEntry> {
    read_json("calls.json")
}

#[tauri::command]
pub fn phone_log_clear() -> Result<(), String> {
    write_json::<Vec<LogEntry>>("calls.json", &Vec::new())
}

// ---- contatos ----

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct Phone {
    /// "Celular", "Trabalho", "Casa"...
    pub label: String,
    pub number: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
pub struct Contact {
    pub id: String,
    pub first: String,
    pub last: String,
    pub phones: Vec<Phone>,
    pub email: String,
    pub company: String,
    pub address: String,
    pub birthday: String,
    pub favorite: bool,
}

#[tauri::command]
pub fn contacts_list() -> Vec<Contact> {
    let mut list: Vec<Contact> = read_json("contacts.json");
    list.sort_by_key(|c| format!("{} {}", c.first, c.last).to_lowercase());
    list
}

/// Salva (cria, se vier sem id) e devolve o contato salvo.
#[tauri::command]
pub fn contacts_save(mut contact: Contact) -> Result<Contact, String> {
    contact.first = contact.first.trim().to_string();
    contact.last = contact.last.trim().to_string();
    contact.phones.retain(|p| p.number.chars().any(|c| c.is_ascii_digit()));
    if contact.first.is_empty() && contact.last.is_empty() && contact.phones.is_empty() {
        return Err("o contato precisa de um nome ou um telefone".into());
    }
    let mut list: Vec<Contact> = read_json("contacts.json");
    if contact.id.is_empty() {
        let nanos = SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_nanos());
        contact.id = format!("{nanos:x}");
        list.push(contact.clone());
    } else if let Some(old) = list.iter_mut().find(|c| c.id == contact.id) {
        *old = contact.clone();
    } else {
        list.push(contact.clone());
    }
    write_json("contacts.json", &list)?;
    Ok(contact)
}

#[tauri::command]
pub fn contacts_delete(id: String) -> Result<(), String> {
    let mut list: Vec<Contact> = read_json("contacts.json");
    list.retain(|c| c.id != id);
    write_json("contacts.json", &list)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn estados_do_modemmanager() {
        assert_eq!(state_name(3), "incoming");
        assert_eq!(state_name(4), "active");
        assert_eq!(state_name(7), "ended");
    }
}
