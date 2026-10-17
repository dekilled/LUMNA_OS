//! Telefone: ligações pelo ModemManager (D-Bus), o histórico de chamadas e os contatos.
//!
//! O ModemManager avisa por sinais quando uma chamada aparece, muda de estado ou some; a cada
//! aviso o shell relê as chamadas e manda a lista pra tela (`lumna://calls`). Com alguma chamada
//! em andamento a camada `mobile-call` aparece por cima de tudo; tocando, o celular vibra.
//! Quando uma chamada termina, ela entra no histórico (`~/.local/share/lumna/phone/calls.json`).
//!
//! Áudio da ligação: o som vai do modem pro fone e do microfone pro modem dentro do DSP. Pra isso
//! acontecer, três coisas durante a chamada: o `lumna-voiced` (serviço do sistema) abre o PCM de
//! voz; o shell troca a placa de som pro perfil "Voice Call" do UCM (que liga o fone e o
//! microfone ao modem) e mantém a placa acordada com dois pw-loopback (o PipeWire suspende o
//! que está parado, e a rota da voz cai junto). Quando a chamada acaba, volta pro perfil de antes.

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
/// `None` = a leitura falhou (o modem não respondeu agora): nada muda. Tratar uma falha
/// passageira como "sem chamadas" derrubava o áudio no meio da ligação.
async fn read_calls(conn: &Connection) -> Option<Vec<Call>> {
    let modem = voice_modem(conn).await.ok()?;
    let paths: Vec<OwnedObjectPath> = call_mm(conn, &modem, VOICE, "ListCalls", &()).await.ok()?;
    // primeiro lê tudo do D-Bus; depois mexe no estado (o lock não atravessa um await)
    let mut read = Vec::new();
    for path in paths {
        let path = path.to_string();
        let props = call_props(conn, &path).await?;
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
    Some(calls)
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
    let Some(calls) = read_calls(conn).await else { return };
    let changed = *CALLS.lock().unwrap() != calls;
    if changed {
        for c in &calls {
            eprintln!("lumna-shell: telefone: {} {}", c.path.rsplit('/').next().unwrap_or(""), c.state);
        }
        *CALLS.lock().unwrap() = calls.clone();
        present(app, &calls);
        // Qualquer chamada que não seja "tocando" nem "encerrada" segura o áudio ligado,
        // inclusive estados de passagem (desconhecido, em espera).
        let talking = calls.iter().any(|c| !matches!(c.state.as_str(), "ended" | "incoming" | "waiting"));
        call_audio(talking);
    }
}

// ---- áudio da ligação ----

const VOICE_PROFILE: &str = "Voice Call";

struct CallAudio {
    on: bool,
    /// placa e perfil pra onde voltar depois da ligação
    card: Option<(String, String)>,
    loopbacks: Vec<std::process::Child>,
    /// dispositivo de dados móveis que teve a reconexão automática pausada
    data_device: Option<String>,
}

static CALL_AUDIO: Mutex<CallAudio> =
    Mutex::new(CallAudio { on: false, card: None, loopbacks: Vec::new(), data_device: None });

/// A placa que tem o perfil "Voice Call" (a do SDM845) e o perfil pra voltar depois: o que
/// estava em uso, ou (se já estava em "Voice Call") o de música (HiFi...).
fn voice_card() -> Option<(String, String)> {
    let out = std::process::Command::new("pactl").args(["-f", "json", "list", "cards"]).output().ok()?;
    let cards: serde_json::Value = serde_json::from_slice(&out.stdout).ok()?;
    cards.as_array()?.iter().find_map(|card| {
        let profiles = card.get("profiles")?.as_object()?;
        if !profiles.contains_key(VOICE_PROFILE) {
            return None;
        }
        let name = card.get("name")?.as_str()?.to_string();
        let active = card.get("active_profile").and_then(|p| p.as_str()).unwrap_or_default();
        let back = if !active.is_empty() && active != VOICE_PROFILE {
            active.to_string()
        } else {
            let mut names: Vec<&String> = profiles.keys().filter(|p| *p != VOICE_PROFILE && *p != "off").collect();
            names.sort_by_key(|p| !p.contains("HiFi"));
            names.first().map(|p| p.to_string()).unwrap_or_else(|| "off".into())
        };
        Some((name, back))
    })
}

fn pactl_profile(card: &str, profile: &str) {
    match std::process::Command::new("pactl").args(["set-card-profile", card, profile]).output() {
        Ok(out) if out.status.success() => {}
        Ok(out) => eprintln!(
            "lumna-shell: telefone: não consegui pôr a placa {card} no perfil {profile}: {}",
            String::from_utf8_lossy(&out.stderr).trim()
        ),
        Err(e) => eprintln!("lumna-shell: telefone: pactl: {e}"),
    }
}

/// O dispositivo de dados móveis do NetworkManager (o modem), se houver.
fn gsm_device() -> Option<String> {
    let out = std::process::Command::new("nmcli").args(["-t", "-f", "DEVICE,TYPE", "device"]).output().ok()?;
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .find_map(|l| l.split_once(':').filter(|(_, t)| *t == "gsm").map(|(d, _)| d.to_string()))
}

/// Pausa (ou devolve) a reconexão automática dos dados móveis. Numa ligação o 4G cai (a voz
/// desce pra rede antiga, CSFB) e o NetworkManager tenta reconectar os dados na hora; o
/// ModemManager travava nessa reconexão, reiniciava o modem e a ligação caía.
fn data_autoconnect(device: &str, on: bool) {
    let ok = std::process::Command::new("nmcli")
        .args(["device", "set", device, "autoconnect", if on { "yes" } else { "no" }])
        .status()
        .is_ok_and(|s| s.success());
    if !ok {
        eprintln!("lumna-shell: telefone: não consegui mudar a reconexão automática de {device}");
    }
}

/// Liga ou desliga o caminho de áudio da chamada (numa thread: pactl e pw-loopback demoram).
fn call_audio(on: bool) {
    std::thread::spawn(move || {
        if !on {
            // Desligar espera um pouco e confere de novo: um piscar de estado não derruba a voz.
            std::thread::sleep(Duration::from_secs(2));
            if CALLS.lock().unwrap().iter().any(|c| !matches!(c.state.as_str(), "ended" | "incoming" | "waiting")) {
                return;
            }
        }
        let mut state = CALL_AUDIO.lock().unwrap();
        if state.on == on {
            return;
        }
        state.on = on;
        if on {
            if let Some(device) = gsm_device() {
                data_autoconnect(&device, false);
                state.data_device = Some(device);
            }
            match voice_card() {
                Some((card, back)) => {
                    pactl_profile(&card, VOICE_PROFILE);
                    state.card = Some((card, back));
                }
                None => eprintln!("lumna-shell: telefone: nenhuma placa com o perfil \"{VOICE_PROFILE}\" (sh mobile/session/audio.sh)"),
            }
            // mantém a placa acordada durante a ligação (o mesmo contorno do postmarketOS)
            for props in ["--capture-props=media.class=Audio/Sink", "--playback-props=media.class=Audio/Source"] {
                match std::process::Command::new("pw-loopback").args(["-m", "[FL FR]", props]).spawn() {
                    Ok(child) => state.loopbacks.push(child),
                    Err(e) => eprintln!("lumna-shell: telefone: pw-loopback: {e}"),
                }
            }
        } else {
            for mut child in state.loopbacks.drain(..) {
                let _ = child.kill();
                let _ = child.wait();
            }
            if let Some((card, back)) = state.card.take() {
                pactl_profile(&card, &back);
            }
            let _ = voice_mic(true);
            // os dados voltam sozinhos (o NetworkManager reconecta com o autoconnect de volta)
            if let Some(device) = state.data_device.take() {
                data_autoconnect(&device, true);
            }
        }
    });
}

/// Número da placa que tem o PCM de voz (VoiceMMode1), de /proc/asound/pcm.
fn voice_card_index() -> Option<String> {
    let pcm = std::fs::read_to_string("/proc/asound/pcm").ok()?;
    let line = pcm.lines().find(|l| l.contains("VoiceMMode1"))?;
    let card = line.split('-').next()?.trim().trim_start_matches('0');
    Some(if card.is_empty() { "0".into() } else { card.to_string() })
}

/// Liga/desliga o microfone da ligação na rota do DSP (é ela que leva a voz pro modem; mutar a
/// fonte do PipeWire não adiantaria).
fn voice_mic(on: bool) -> Result<(), String> {
    let card = voice_card_index().ok_or("o PCM de voz não existe (driver q6voice)")?;
    let out = std::process::Command::new("amixer")
        .args(["-q", "-c", &card, "cset", "name=VoiceMMode1 Capture Mixer SLIMBUS_0_TX", if on { "1" } else { "0" }])
        .output()
        .map_err(|e| format!("amixer: {e}"))?;
    if out.status.success() {
        Ok(())
    } else {
        Err(String::from_utf8_lossy(&out.stderr).trim().to_string())
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
    // Sem ligação, a placa não pode ficar no perfil de ligação (o WirePlumber lembra o último
    // perfil e devolveria o "Voice Call" no boot, com o som indo pro fone em vez do alto-falante).
    if CALLS.lock().unwrap().is_empty() {
        std::thread::spawn(|| {
            if !CALL_AUDIO.lock().unwrap().on {
                if let Some((card, back)) = voice_card() {
                    pactl_profile(&card, &back);
                }
            }
        });
    }
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

/// Mudo: corta o microfone da ligação.
#[tauri::command]
pub async fn phone_mute(on: bool) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || voice_mic(!on)).await.map_err(|e| e.to_string())?
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
    fn placa_da_voz() {
        let line = " 00-06: VoiceMMode1 (*) :  : playback 1 : capture 1";
        let card = line.split('-').next().unwrap().trim().trim_start_matches('0');
        assert_eq!(if card.is_empty() { "0" } else { card }, "0");
    }

    #[test]
    fn estados_do_modemmanager() {
        assert_eq!(state_name(3), "incoming");
        assert_eq!(state_name(4), "active");
        assert_eq!(state_name(7), "ended");
    }
}
