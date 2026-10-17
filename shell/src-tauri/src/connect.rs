//! Celular Connect: o celular conectado ao LUMNA do PC (o servidor de `remote.rs`).
//!
//! Acha os PCs na rede pelo mDNS (`_lumna-remote._tcp`), pareia com o código de 6 dígitos que o
//! PC mostra e guarda o token em `~/.config/lumna/connect.json` (só o dono lê). Cada PC pareado
//! tem uma conexão viva, que volta sozinha se cair.
//!
//! O PC manda avisos sem o celular pedir: `notification` vira uma notificação aqui; `auth`
//! (pedido de senha do polkit) e `emma-ask` (a Emma quer confirmar uma ação) abrem a camada
//! `mobile-ask`, e a resposta volta pro PC. Os comandos do celular (`info`, `power`, `emma`...)
//! saem por `connect_send` e a resposta é a próxima mensagem que não for aviso.
//!
//! E o resto do Connect: o que for copiado aqui vai pro PC e vice-versa (`clipboard`), os avisos
//! e as ligações daqui aparecem no PC (`notify`), arquivos vão e vêm (`link.rs`). Cada PC tem os
//! seus recursos ligados/desligados (`link::Features`), em Ajustes → Celular Connect → o PC.
//!
//! Fora de casa: com o Tailscale ligado nos dois, é só adicionar o PC pelo IP do Tailscale.

use std::collections::{HashMap, VecDeque};
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::Duration;

use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter};
use tokio::sync::{mpsc, oneshot};
use tokio_tungstenite::tungstenite::Message;

use crate::layer;
use crate::link::{self, Features};

const SERVICE: &str = "_lumna-remote._tcp.local.";
/// Avisos do PC (o resto é resposta a um pedido nosso).
const EVENTS: [&str; 7] = ["notification", "auth", "auth-done", "emma-ask", "clipboard", "file-offer", "mode"];

/// Quais monitores de cada PC estão no modo Mídia, e o aviso aberto por isso.
static MEDIA: Mutex<Option<HashMap<String, (Vec<String>, Option<u32>)>>> = Mutex::new(None);

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Computer {
    pub id: String,
    pub name: String,
    pub host: String,
    pub port: u16,
    #[serde(skip_serializing)]
    pub token: String,
    #[serde(default)]
    pub features: Features,
}

#[derive(Debug, Clone, Serialize)]
pub struct ComputerState {
    pub id: String,
    pub name: String,
    pub host: String,
    pub port: u16,
    pub online: bool,
    pub features: Features,
}

type Request = (Value, oneshot::Sender<Result<Value, String>>);

struct Link {
    tx: mpsc::UnboundedSender<Request>,
    online: bool,
}

static LINKS: Mutex<Option<HashMap<String, Link>>> = Mutex::new(None);
/// Acorda as conexões (o Connect foi ligado/desligado ou um PC mudou de endereço).
static KICK: tokio::sync::Notify = tokio::sync::Notify::const_new();

fn config_path() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna/connect.json")
}

fn load() -> Vec<Computer> {
    std::fs::read_to_string(config_path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

fn save(list: &[Computer]) -> Result<(), String> {
    use std::os::unix::fs::PermissionsExt;
    let path = config_path();
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    // o token dá controle do PC: só o dono lê (e o serde não esconde ele no arquivo)
    let full: Vec<Value> = list
        .iter()
        .map(|c| json!({ "id": c.id, "name": c.name, "host": c.host, "port": c.port, "token": c.token, "features": c.features }))
        .collect();
    std::fs::write(&path, serde_json::to_string_pretty(&full).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).map_err(|e| e.to_string())
}

fn url(host: &str, port: u16) -> String {
    if host.contains(':') {
        format!("ws://[{host}]:{port}")
    } else {
        format!("ws://{host}:{port}")
    }
}

fn phone_name() -> String {
    let host = std::fs::read_to_string("/etc/hostname").map(|h| h.trim().to_string()).unwrap_or_else(|_| "lumna".into());
    format!("{host} (celular)")
}

fn states() -> Vec<ComputerState> {
    let links = LINKS.lock().unwrap();
    load()
        .into_iter()
        .map(|c| ComputerState {
            online: links.as_ref().and_then(|l| l.get(&c.id)).is_some_and(|l| l.online),
            id: c.id,
            name: c.name,
            host: c.host,
            port: c.port,
            features: c.features,
        })
        .collect()
}

fn is_online(id: &str) -> bool {
    LINKS.lock().unwrap().as_ref().and_then(|l| l.get(id)).is_some_and(|l| l.online)
}

/// Um pedido pro PC (pelo laço da conexão dele), esperando a resposta.
async fn request(id: &str, message: Value, wait: Duration) -> Result<Value, String> {
    let tx = LINKS
        .lock()
        .unwrap()
        .as_ref()
        .and_then(|l| l.get(id))
        .map(|l| l.tx.clone())
        .ok_or("esse PC não está pareado")?;
    let (reply_tx, reply_rx) = oneshot::channel();
    tx.send((message, reply_tx)).map_err(|_| "a conexão com o PC caiu".to_string())?;
    tokio::time::timeout(wait, reply_rx)
        .await
        .map_err(|_| "o PC demorou demais pra responder".to_string())?
        .map_err(|_| "a conexão com o PC caiu".to_string())?
}

/// Manda pra todo PC conectado que tem o recurso ligado, sem esperar resposta.
fn to_all(message: Value, wanted: fn(&Features) -> bool) {
    if !link::enabled() {
        return;
    }
    for c in load().into_iter().filter(|c| wanted(&c.features) && is_online(&c.id)) {
        let message = message.clone();
        tauri::async_runtime::spawn(async move {
            let _ = request(&c.id, message, Duration::from_secs(20)).await;
        });
    }
}

/// Um aviso daqui (ou uma ligação chegando) vai pros PCs que querem ver.
pub fn forward_notification(app_name: &str, summary: &str, body: &str, urgent: bool) {
    to_all(json!({ "type": "notify", "app": app_name, "summary": summary, "body": body, "urgent": urgent }), |f| f.phone_notifications);
}

fn set_online(app: &AppHandle, id: &str, online: bool) {
    if let Some(link) = LINKS.lock().unwrap().as_mut().and_then(|l| l.get_mut(id)) {
        link.online = online;
    }
    let _ = app.emit("lumna://connect", states());
}

// ---- avisos do PC ----

fn show_ask(app: &AppHandle, ask: Value) {
    crate::haptics::buzz(60);
    let handle = app.clone();
    let _ = app.run_on_main_thread(move || {
        layer::set_visible("mobile-ask", true);
        let _ = handle.emit_to("mobile-ask", "lumna://connect-ask", ask);
    });
}

fn on_event(app: &AppHandle, computer: &Computer, event: &Value) {
    let text = |k: &str| event[k].as_str().unwrap_or_default().to_string();
    // os recursos podem ter mudado com a conexão aberta: lê de novo
    let features = load().into_iter().find(|c| c.id == computer.id).map_or(computer.features, |c| c.features);
    match event["type"].as_str().unwrap_or_default() {
        "notification" if !features.notifications => {}
        "auth" | "emma-ask" if !features.control => {}
        "clipboard" if !features.clipboard => {}
        "file-offer" if !features.files => {
            let id = computer.id.clone();
            let offer = text("id");
            tauri::async_runtime::spawn(async move {
                let _ = request(&id, json!({ "type": "file-reject", "id": offer, "reason": "o celular não aceita arquivos" }), Duration::from_secs(20)).await;
            });
        }
        "clipboard" => {
            if let Err(e) = link::clipboard_set(&text("text")) {
                eprintln!("lumna-shell: connect: área de transferência: {e}");
            }
        }
        "file-offer" => {
            let app = app.clone();
            let computer = computer.clone();
            let (offer, name, size) = (text("id"), text("name"), event["size"].as_u64().unwrap_or(0));
            tauri::async_runtime::spawn(async move { pull(&app, &computer, &offer, &name, size).await });
        }
        "mode" => {
            let media: Vec<String> = event["media"].as_array().map(|a| a.iter().filter_map(|v| v.as_str().map(str::to_string)).collect()).unwrap_or_default();
            let mut all = MEDIA.lock().unwrap();
            let (before, notice) = all.get_or_insert_with(HashMap::new).entry(computer.id.clone()).or_default();
            if media.is_empty() {
                if let Some(id) = notice.take() {
                    crate::notifications::local_close(app, id);
                }
            } else if before.is_empty() && notice.is_none() && link::settings().media_notify {
                *notice = Some(crate::notifications::local_open(
                    app,
                    &computer.name,
                    "Modo Mídia ligado",
                    "Toque pra abrir o controle",
                    false,
                    Some(format!("remote:{}", computer.id)),
                ));
            }
            *before = media.clone();
            drop(all);
            let _ = app.emit("lumna://connect-mode", json!({ "computer": computer.id, "media": media }));
        }
        "notification" => {
            let from = if text("app").is_empty() { computer.name.clone() } else { format!("{} · {}", computer.name, text("app")) };
            crate::notifications::local(app, &from, &text("summary"), &text("body"), event["urgent"].as_bool().unwrap_or(false));
        }
        "auth" => show_ask(
            app,
            json!({
                "kind": "auth", "computer": computer.id, "computer_name": computer.name,
                "cookie": event["cookie"], "message": event["message"], "user": event["user"], "error": event["error"],
            }),
        ),
        "auth-done" => {
            let _ = app.emit_to("mobile-ask", "lumna://connect-ask-done", json!({ "computer": computer.id, "cookie": event["cookie"] }));
        }
        "emma-ask" => show_ask(
            app,
            json!({
                "kind": "emma", "computer": computer.id, "computer_name": computer.name,
                "call_id": event["call_id"], "label": event["label"], "reason": event["reason"],
            }),
        ),
        _ => {}
    }
}

// ---- arquivos ----

/// Puxa um arquivo que o PC ofereceu, pedaço por pedaço.
async fn pull(app: &AppHandle, computer: &Computer, offer: &str, name: &str, size: u64) {
    let tid = format!("pc:{}:{offer}", computer.id);
    if let Err(e) = link::recv_begin(app, &computer.name, &tid, name, size) {
        let _ = request(&computer.id, json!({ "type": "file-reject", "id": offer, "reason": e }), Duration::from_secs(20)).await;
        return;
    }
    let mut offset = 0u64;
    loop {
        let reply = match request(&computer.id, json!({ "type": "file-pull", "id": offer, "offset": offset }), Duration::from_secs(60)).await {
            Ok(r) => r,
            Err(e) => return link::recv_cancel(app, &tid, &e),
        };
        if let Err(e) = link::recv_chunk(app, &tid, reply["data"].as_str().unwrap_or_default()) {
            link::recv_cancel(app, &tid, &e);
            let _ = request(&computer.id, json!({ "type": "file-reject", "id": offer, "reason": e }), Duration::from_secs(20)).await;
            return;
        }
        offset += reply["len"].as_u64().unwrap_or(0);
        if reply["eof"].as_bool().unwrap_or(true) {
            break;
        }
    }
    let _ = link::recv_end(app, &tid);
}

/// Manda arquivos daqui pro PC, um de cada vez, pedaço por pedaço.
async fn push(app: AppHandle, computer: Computer, files: Vec<(PathBuf, String, u64)>) {
    for (path, name, size) in files {
        let tid = link::new_id();
        let mut t = link::Transfer {
            id: tid.clone(),
            name: name.clone(),
            peer: computer.name.clone(),
            direction: "out",
            done: 0,
            size,
            state: "running",
            error: None,
            path: Some(path.to_string_lossy().into()),
        };
        link::progress(&app, t.clone());
        let result: Result<(), String> = async {
            request(&computer.id, json!({ "type": "file-begin", "id": tid, "name": name, "size": size }), Duration::from_secs(30)).await?;
            loop {
                let (p, offset) = (path.clone(), t.done);
                let (data, len, eof) = tauri::async_runtime::spawn_blocking(move || link::read_chunk(&p, offset)).await.map_err(|e| e.to_string())??;
                request(&computer.id, json!({ "type": "file-chunk", "id": tid, "data": data }), Duration::from_secs(60)).await?;
                t.done += len as u64;
                link::progress(&app, t.clone());
                if eof {
                    break;
                }
            }
            request(&computer.id, json!({ "type": "file-end", "id": tid }), Duration::from_secs(30)).await.map(|_| ())
        }
        .await;
        match result {
            Ok(()) => t.state = "done",
            Err(e) => {
                let _ = request(&computer.id, json!({ "type": "file-cancel", "id": tid }), Duration::from_secs(10)).await;
                t.state = "error";
                t.error = Some(e);
            }
        }
        link::progress(&app, t);
    }
}

// ---- a conexão com cada PC ----

async fn session(app: &AppHandle, computer: &Computer, rx: &mut mpsc::UnboundedReceiver<Request>) -> Result<(), String> {
    let (mut ws, _) = tokio::time::timeout(Duration::from_secs(8), tokio_tungstenite::connect_async(url(&computer.host, computer.port)))
        .await
        .map_err(|_| "o PC não respondeu".to_string())?
        .map_err(|e| e.to_string())?;
    ws.send(Message::Text(json!({ "type": "hello", "token": computer.token }).to_string().into())).await.map_err(|e| e.to_string())?;
    let mut pending: VecDeque<oneshot::Sender<Result<Value, String>>> = VecDeque::new();
    let mut ready = false;
    let mut ping = tokio::time::interval(Duration::from_secs(25));
    loop {
        tokio::select! {
            message = ws.next() => {
                let text = match message {
                    Some(Ok(Message::Text(t))) => t.to_string(),
                    Some(Ok(Message::Close(_))) | None => return Err("o PC fechou a conexão".into()),
                    Some(Ok(_)) => continue,
                    Some(Err(e)) => return Err(e.to_string()),
                };
                let Ok(msg) = serde_json::from_str::<Value>(&text) else { continue };
                let kind = msg["type"].as_str().unwrap_or_default();
                if !ready {
                    if kind == "ready" {
                        ready = true;
                        set_online(app, &computer.id, true);
                        continue;
                    }
                    if msg["unpaired"].as_bool() == Some(true) {
                        return Err("o PC esqueceu este celular: pareie de novo".into());
                    }
                    continue;
                }
                if EVENTS.contains(&kind) {
                    on_event(app, computer, &msg);
                } else if let Some(reply) = pending.pop_front() {
                    let _ = reply.send(if kind == "error" {
                        Err(msg["message"].as_str().unwrap_or("erro no PC").to_string())
                    } else {
                        Ok(msg)
                    });
                }
            }
            Some((msg, reply)) = rx.recv() => {
                if !ready {
                    let _ = reply.send(Err("o PC ainda não respondeu".into()));
                    continue;
                }
                if let Err(e) = ws.send(Message::Text(msg.to_string().into())).await {
                    let _ = reply.send(Err(e.to_string()));
                    return Err(e.to_string());
                }
                pending.push_back(reply);
            }
            _ = ping.tick() => {
                if ws.send(Message::Ping(Vec::new().into())).await.is_err() {
                    return Err("conexão caiu".into());
                }
            }
            _ = KICK.notified() => {
                let current = load().into_iter().find(|c| c.id == computer.id);
                if !link::enabled() || current.is_none_or(|c| c.host != computer.host || c.port != computer.port) {
                    let _ = ws.close(None).await;
                    return Ok(());
                }
            }
        }
    }
}

fn spawn_link(app: &AppHandle, computer: Computer) {
    let (tx, mut rx) = mpsc::unbounded_channel::<Request>();
    LINKS.lock().unwrap().get_or_insert_with(HashMap::new).insert(computer.id.clone(), Link { tx, online: false });
    let app = app.clone();
    tauri::async_runtime::spawn(async move {
        let mut wait = Duration::from_secs(3);
        loop {
            // esquecido nesse meio tempo: para (e pega o endereço novo, se mudou)
            let Some(computer) = load().into_iter().find(|c| c.id == computer.id) else { return };
            if !link::enabled() {
                while let Ok((_, reply)) = rx.try_recv() {
                    let _ = reply.send(Err("o Celular Connect está desligado".into()));
                }
                tokio::select! {
                    _ = tokio::time::sleep(Duration::from_secs(60)) => {}
                    _ = KICK.notified() => {}
                }
                continue;
            }
            let result = session(&app, &computer, &mut rx).await;
            set_online(&app, &computer.id, false);
            link::recv_cancel_peer(&app, &computer.name);
            // PC fora do ar: o aviso do Mídia some
            if let Some((_, Some(id))) = MEDIA.lock().unwrap().as_mut().and_then(|m| m.remove(&computer.id)) {
                crate::notifications::local_close(&app, id);
            }
            let _ = app.emit("lumna://connect-mode", json!({ "computer": computer.id, "media": [] }));
            if let Err(e) = result {
                eprintln!("lumna-shell: connect {}: {e}", computer.name);
            } else {
                wait = Duration::from_secs(3);
            }
            // pedidos que chegaram com o PC fora: falham na hora
            while let Ok((_, reply)) = rx.try_recv() {
                let _ = reply.send(Err("o PC está fora do ar".into()));
            }
            tokio::select! {
                _ = tokio::time::sleep(wait) => {}
                _ = KICK.notified() => wait = Duration::from_secs(1),
            }
            wait = (wait * 2).min(Duration::from_secs(60));
        }
    });
}

/// No início do shell no celular: uma conexão pra cada PC pareado.
pub fn start(app: AppHandle) {
    for computer in load() {
        spawn_link(&app, computer);
    }
    // o que for copiado aqui vai pros PCs
    link::clipboard_watch(|text| to_all(json!({ "type": "clipboard", "text": text }), |f| f.clipboard));
}

/// O Connect foi ligado ou desligado neste celular.
pub fn enabled_changed(app: &AppHandle) {
    KICK.notify_waiters();
    let _ = app.emit("lumna://connect", states());
}

// ---- comandos da interface ----

#[tauri::command]
pub fn connect_computers() -> Vec<ComputerState> {
    states()
}

#[derive(Debug, Clone, Serialize)]
pub struct Found {
    pub name: String,
    pub host: String,
    pub port: u16,
}

/// Procura PCs com o LUMNA na rede por alguns segundos.
#[tauri::command]
pub async fn connect_discover() -> Result<Vec<Found>, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let daemon = mdns_sd::ServiceDaemon::new().map_err(|e| e.to_string())?;
        let receiver = daemon.browse(SERVICE).map_err(|e| e.to_string())?;
        let mut found: Vec<Found> = Vec::new();
        let until = std::time::Instant::now() + Duration::from_secs(4);
        while let Some(left) = until.checked_duration_since(std::time::Instant::now()) {
            let Ok(event) = receiver.recv_timeout(left) else { break };
            if let mdns_sd::ServiceEvent::ServiceResolved(info) = event {
                let name = info.get_fullname().split("._lumna").next().unwrap_or("LUMNA").to_string();
                let mut addresses: Vec<String> = info.get_addresses().iter().map(|a| a.to_string()).collect();
                // IPv4 primeiro (mais simples de ver e digitar)
                addresses.sort_by_key(|a| a.contains(':'));
                if let Some(host) = addresses.into_iter().next() {
                    if !found.iter().any(|f| f.host == host) {
                        found.push(Found { name, host, port: info.get_port() });
                    }
                }
            }
        }
        let _ = daemon.shutdown();
        Ok(found)
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Pareia com o código que o PC mostra (Configurações → Celular, ou a tela de Mídia).
#[tauri::command]
pub async fn connect_pair(app: AppHandle, host: String, port: Option<u16>, code: String) -> Result<ComputerState, String> {
    let host = host.trim().to_string();
    let port = port.unwrap_or(crate::remote::PORT);
    let (mut ws, _) = tokio::time::timeout(Duration::from_secs(8), tokio_tungstenite::connect_async(url(&host, port)))
        .await
        .map_err(|_| format!("{host} não respondeu (o LUMNA está aberto no PC?)"))?
        .map_err(|e| format!("{host}: {e}"))?;
    ws.send(Message::Text(json!({ "type": "pair", "code": code.trim(), "name": phone_name() }).to_string().into()))
        .await
        .map_err(|e| e.to_string())?;
    let reply = loop {
        match tokio::time::timeout(Duration::from_secs(8), ws.next()).await {
            Ok(Some(Ok(Message::Text(t)))) => break serde_json::from_str::<Value>(&t).map_err(|e| e.to_string())?,
            Ok(Some(Ok(_))) => continue,
            _ => return Err("o PC não respondeu ao código".into()),
        }
    };
    let _ = ws.close(None).await;
    if reply["type"] != "paired" {
        return Err(reply["message"].as_str().unwrap_or("código recusado").to_string());
    }
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos());
    let computer = Computer {
        id: format!("{nanos:x}"),
        name: reply["name"].as_str().unwrap_or("LUMNA").to_string(),
        host,
        port,
        token: reply["token"].as_str().unwrap_or_default().to_string(),
        features: Features::default(),
    };
    let mut list = load();
    list.retain(|c| !(c.host == computer.host && c.port == computer.port));
    list.push(computer.clone());
    save(&list)?;
    spawn_link(&app, computer.clone());
    Ok(ComputerState { id: computer.id, name: computer.name, host: computer.host, port: computer.port, online: false, features: computer.features })
}

#[tauri::command]
pub fn connect_forget(app: AppHandle, id: String) -> Result<(), String> {
    let mut list = load();
    list.retain(|c| c.id != id);
    save(&list)?;
    LINKS.lock().unwrap().get_or_insert_with(HashMap::new).remove(&id);
    let _ = app.emit("lumna://connect", states());
    Ok(())
}

/// Os monitores de cada PC no modo Mídia (pro controle e pros Ajustes).
#[tauri::command]
pub fn connect_media() -> HashMap<String, Vec<String>> {
    MEDIA.lock().unwrap().as_ref().map(|m| m.iter().map(|(k, (v, _))| (k.clone(), v.clone())).collect()).unwrap_or_default()
}

/// Manda um comando pro PC e espera a resposta (a Emma pode levar um tempo).
#[tauri::command]
pub async fn connect_send(id: String, message: Value) -> Result<Value, String> {
    request(&id, message, Duration::from_secs(180)).await
}

#[tauri::command]
pub fn connect_set_features(app: AppHandle, id: String, features: Features) -> Result<(), String> {
    let mut list = load();
    list.iter_mut().find(|c| c.id == id).ok_or("esse PC não está pareado")?.features = features;
    save(&list)?;
    let _ = app.emit("lumna://connect", states());
    Ok(())
}

/// O PC mudou de IP (ou vai pelo Tailscale agora): troca o endereço sem parear de novo.
#[tauri::command]
pub fn connect_set_address(app: AppHandle, id: String, host: String, port: Option<u16>) -> Result<(), String> {
    let host = host.trim().to_string();
    if host.is_empty() {
        return Err("digite o endereço".into());
    }
    let mut list = load();
    let c = list.iter_mut().find(|c| c.id == id).ok_or("esse PC não está pareado")?;
    c.host = host;
    c.port = port.unwrap_or(crate::remote::PORT);
    save(&list)?;
    KICK.notify_waiters();
    let _ = app.emit("lumna://connect", states());
    Ok(())
}

#[tauri::command]
pub fn connect_send_files(app: AppHandle, id: String, paths: Vec<String>) -> Result<(), String> {
    let computer = load().into_iter().find(|c| c.id == id).ok_or("esse PC não está pareado")?;
    if !is_online(&id) {
        return Err(format!("{} está fora do ar", computer.name));
    }
    let files = link::files_to_send(&paths)?;
    tauri::async_runtime::spawn(push(app, computer, files));
    Ok(())
}

#[tauri::command]
pub async fn connect_send_clipboard(id: String) -> Result<(), String> {
    let text = tauri::async_runtime::spawn_blocking(link::clipboard_get).await.map_err(|e| e.to_string())?.ok_or("não tem texto copiado")?;
    request(&id, json!({ "type": "clipboard", "text": text }), Duration::from_secs(20)).await.map(|_| ())
}

// ---- pra quem mandar (o "Enviar para…" do Arquivos, no PC e no celular) ----

#[derive(Serialize)]
pub struct Target {
    pub id: String,
    pub name: String,
    /// "pc" (este é o celular) ou "phone" (este é o PC).
    pub kind: &'static str,
}

#[tauri::command]
pub fn connect_targets() -> Vec<Target> {
    if !link::enabled() {
        return Vec::new();
    }
    let mut out: Vec<Target> = states().into_iter().filter(|c| c.online).map(|c| Target { id: c.id, name: c.name, kind: "pc" }).collect();
    out.extend(crate::remote::remote_devices().into_iter().filter(|d| d.online).map(|d| Target { id: d.id, name: d.name, kind: "phone" }));
    out
}

#[tauri::command]
pub fn connect_share(app: AppHandle, kind: String, id: String, paths: Vec<String>) -> Result<(), String> {
    match kind.as_str() {
        "pc" => connect_send_files(app, id, paths),
        _ => crate::remote::remote_send_files(app, id, paths),
    }
}

#[tauri::command]
pub fn connect_ask_close(app: AppHandle) {
    let _ = app.run_on_main_thread(|| layer::set_visible("mobile-ask", false));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn enderecos() {
        assert_eq!(url("192.168.0.10", 47800), "ws://192.168.0.10:47800");
        assert_eq!(url("fe80::1", 47800), "ws://[fe80::1]:47800");
    }
}
