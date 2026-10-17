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

const SERVICE: &str = "_lumna-remote._tcp.local.";
/// Avisos do PC (o resto é resposta a um pedido nosso).
const EVENTS: [&str; 4] = ["notification", "auth", "auth-done", "emma-ask"];

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Computer {
    pub id: String,
    pub name: String,
    pub host: String,
    pub port: u16,
    #[serde(skip_serializing)]
    pub token: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct ComputerState {
    pub id: String,
    pub name: String,
    pub host: String,
    pub port: u16,
    pub online: bool,
}

type Request = (Value, oneshot::Sender<Result<Value, String>>);

struct Link {
    tx: mpsc::UnboundedSender<Request>,
    online: bool,
}

static LINKS: Mutex<Option<HashMap<String, Link>>> = Mutex::new(None);

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
        .map(|c| json!({ "id": c.id, "name": c.name, "host": c.host, "port": c.port, "token": c.token }))
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
        })
        .collect()
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
    match event["type"].as_str().unwrap_or_default() {
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
            // esquecido nesse meio tempo: para
            if !load().iter().any(|c| c.id == computer.id) {
                return;
            }
            let result = session(&app, &computer, &mut rx).await;
            set_online(&app, &computer.id, false);
            if let Err(e) = result {
                eprintln!("lumna-shell: connect {}: {e}", computer.name);
            } else {
                wait = Duration::from_secs(3);
            }
            // pedidos que chegaram com o PC fora: falham na hora
            while let Ok((_, reply)) = rx.try_recv() {
                let _ = reply.send(Err("o PC está fora do ar".into()));
            }
            tokio::time::sleep(wait).await;
            wait = (wait * 2).min(Duration::from_secs(60));
        }
    });
}

/// No início do shell no celular: uma conexão pra cada PC pareado.
pub fn start(app: AppHandle) {
    for computer in load() {
        spawn_link(&app, computer);
    }
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
    };
    let mut list = load();
    list.retain(|c| !(c.host == computer.host && c.port == computer.port));
    list.push(computer.clone());
    save(&list)?;
    spawn_link(&app, computer.clone());
    Ok(ComputerState { id: computer.id, name: computer.name, host: computer.host, port: computer.port, online: false })
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

/// Manda um comando pro PC e espera a resposta (a Emma pode levar um tempo).
#[tauri::command]
pub async fn connect_send(id: String, message: Value) -> Result<Value, String> {
    let tx = LINKS
        .lock()
        .unwrap()
        .as_ref()
        .and_then(|l| l.get(&id))
        .map(|l| l.tx.clone())
        .ok_or("esse PC não está pareado")?;
    let (reply_tx, reply_rx) = oneshot::channel();
    tx.send((message, reply_tx)).map_err(|_| "a conexão com o PC caiu".to_string())?;
    tokio::time::timeout(Duration::from_secs(180), reply_rx)
        .await
        .map_err(|_| "o PC demorou demais pra responder".to_string())?
        .map_err(|_| "a conexão com o PC caiu".to_string())?
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
