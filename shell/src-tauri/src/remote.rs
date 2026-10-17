//! Controle pelo celular: servidor WebSocket só para a rede local, anunciado por mDNS
//! (`_lumna-remote._tcp`), para o app LUMNA Controle achar a TV sozinho.
//!
//! Pareamento: a tela de Mídia mostra um código de 6 dígitos (vale 5 minutos e uma vez só).
//! O celular manda o código e recebe um token, guardado nos dois lados; daí em diante ele entra
//! só com o token. "Esquecer celulares" apaga todos os tokens.
//!
//! Mensagens são JSON, uma por quadro de texto. Do celular:
//! `{"type":"pair","code":"482913","name":"Pixel"}`, `{"type":"hello","token":"..."}` e, depois de
//! entrar, `pad` (as ações do controle), `text`, `key`, `media`, `volume`, `mode` e `home`.
//!
//! Celular Connect (o LUMNA no celular): além disso, `info`, `power`, `emma` (conversar com a Emma
//! daqui), `emma-decide` (aprovar uma ação dela) e `auth-respond` (a senha de um pedido do polkit).
//! E o PC avisa os celulares conectados, sem eles pedirem: `notification`, `auth` / `auth-done`
//! (pedido de senha) e `emma-ask` (a Emma quer confirmar uma ação).
//!
//! E mais (o "KDE Connect" do LUMNA): `clipboard` (o que foi copiado num vai pro outro), `notify`
//! (avisos e ligações do celular aparecem aqui), arquivos (`file-begin` / `file-chunk` /
//! `file-end` do celular pra cá; `file-offer` daqui e o celular puxa com `file-pull`) e
//! `schedule-*` (agendamentos). Cada celular tem seus recursos (`link::Features`), ligados e
//! desligados em Configurações → Celular; o que estiver desligado nem sai nem entra.
//!
//! Rede: só a local, e a do Tailscale (100.64.0.0/10), que leva o Connect pra fora de casa.

use std::io::Read;
use std::net::IpAddr;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::{Duration, Instant};

use std::sync::atomic::{AtomicU64, Ordering};

use futures_util::{SinkExt, StreamExt};
use tokio::sync::mpsc;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager};
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::tungstenite::error::{Error, ProtocolError};
use tokio_tungstenite::tungstenite::Message;

use crate::link::Features;
use crate::modes::{Mode, Target};
use crate::pad::Action;

pub const PORT: u16 = 47800;
const SERVICE: &str = "_lumna-remote._tcp.local.";
const CODE_LIFETIME: Duration = Duration::from_secs(5 * 60);

#[derive(Serialize, Deserialize, Clone)]
struct Device {
    /// Pareamentos antigos não tinham: ganham um ao carregar.
    #[serde(default)]
    id: String,
    name: String,
    token: String,
    #[serde(default)]
    features: Features,
}

#[derive(Default)]
pub struct Remote {
    /// Código de pareamento em exibição e até quando ele vale.
    code: Mutex<Option<(String, Instant)>>,
}

#[derive(Serialize)]
pub struct Pairing {
    pub code: String,
    /// Endereços desta máquina na rede local, para digitar no celular se o mDNS falhar.
    pub addresses: Vec<String>,
    pub port: u16,
    pub expires_in: u64,
    pub devices: Vec<String>,
}

fn devices_path() -> Option<PathBuf> {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
    Some(base.join("lumna").join("remotes.json"))
}

fn load_devices() -> Vec<Device> {
    let mut devices: Vec<Device> = devices_path()
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_default();
    if devices.iter().any(|d| d.id.is_empty()) {
        for (i, d) in devices.iter_mut().enumerate().filter(|(_, d)| d.id.is_empty()) {
            d.id = format!("{}{i}", crate::link::new_id());
        }
        let _ = save_devices(&devices);
    }
    devices
}

fn device(id: &str) -> Option<Device> {
    load_devices().into_iter().find(|d| d.id == id)
}

fn save_devices(devices: &[Device]) -> Result<(), String> {
    use std::os::unix::fs::PermissionsExt;
    let path = devices_path().ok_or("sem pasta de configuração")?;
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(&path, serde_json::to_string_pretty(devices).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    // Os tokens dão controle da máquina: só o dono lê.
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o600)).map_err(|e| e.to_string())
}

fn random_bytes<const N: usize>() -> [u8; N] {
    let mut bytes = [0u8; N];
    std::fs::File::open("/dev/urandom").and_then(|mut f| f.read_exact(&mut bytes)).expect("sem /dev/urandom");
    bytes
}

fn new_token() -> String {
    random_bytes::<32>().iter().map(|b| format!("{b:02x}")).collect()
}

fn new_code() -> String {
    let n = u32::from_le_bytes(random_bytes::<4>()) % 1_000_000;
    format!("{n:06}")
}

/// Comparação em tempo constante (não vaza quantos dígitos acertou).
fn same(a: &str, b: &str) -> bool {
    a.len() == b.len() && a.bytes().zip(b.bytes()).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}

/// Só aceita conexões da rede local (e da própria máquina), e do Tailscale (a rede privada que
/// leva o Connect pra fora de casa: 100.64.0.0/10 e fd7a:115c:a1e0::/48).
fn is_local(ip: IpAddr) -> bool {
    match ip {
        IpAddr::V4(v4) => {
            let o = v4.octets();
            v4.is_loopback() || v4.is_private() || v4.is_link_local() || (o[0] == 100 && (o[1] & 0xc0) == 64)
        }
        IpAddr::V6(v6) => {
            v6.is_loopback() || (v6.segments()[0] & 0xfe00) == 0xfc00 || (v6.segments()[0] & 0xffc0) == 0xfe80
        }
    }
}

// ---- avisos pros celulares conectados ----

static NEXT_CLIENT: AtomicU64 = AtomicU64::new(1);

struct Client {
    id: u64,
    /// O `Device::id` do celular.
    device: String,
    tx: mpsc::UnboundedSender<String>,
}

static CLIENTS: Mutex<Vec<Client>> = Mutex::new(Vec::new());
/// Mandado pra uma conexão pra ela fechar (o Connect foi desligado ou o celular esquecido).
const CLOSE: &str = "\u{0}close";

/// Que recurso um aviso precisa ligado pra sair.
fn feature_for(kind: &str) -> fn(&Features) -> bool {
    match kind {
        "notification" => |f| f.notifications,
        "clipboard" => |f| f.clipboard,
        "auth" | "auth-done" | "emma-ask" => |f| f.control,
        _ => |_| true,
    }
}

/// Manda um aviso pra todos os celulares conectados que aceitam esse tipo de aviso.
pub fn broadcast(event: Value) {
    if !crate::link::enabled() {
        return;
    }
    let allowed = feature_for(event["type"].as_str().unwrap_or_default());
    let devices = load_devices();
    let text = event.to_string();
    CLIENTS.lock().unwrap().retain(|c| {
        let ok = devices.iter().find(|d| d.id == c.device).is_some_and(|d| allowed(&d.features));
        !ok || c.tx.send(text.clone()).is_ok()
    });
}

/// Manda um aviso só pra um celular. `false` se ele não está conectado.
fn send_to(device: &str, event: &Value) -> bool {
    let text = event.to_string();
    CLIENTS.lock().unwrap().iter().filter(|c| c.device == device).any(|c| c.tx.send(text.clone()).is_ok())
}

fn online(device: &str) -> bool {
    CLIENTS.lock().unwrap().iter().any(|c| c.device == device)
}

/// O Connect foi desligado (ou ligado) neste PC: desligado, derruba quem estiver conectado.
pub fn enabled_changed(enabled: bool) {
    if !enabled {
        for c in CLIENTS.lock().unwrap().drain(..) {
            let _ = c.tx.send(CLOSE.into());
        }
    }
}

fn devices_changed(app: &AppHandle) {
    let _ = app.emit("lumna://remote-devices", remote_devices());
}

fn local_addresses() -> Vec<String> {
    local_ip_address::list_afinet_netifas()
        .unwrap_or_default()
        .into_iter()
        .filter_map(|(_, ip)| match ip {
            IpAddr::V4(v4) if v4.is_private() => Some(v4.to_string()),
            _ => None,
        })
        .collect()
}

impl Remote {
    /// Gera um código novo (o anterior deixa de valer) para a tela de Mídia mostrar.
    pub fn start_pairing(&self) -> Pairing {
        let code = new_code();
        *self.code.lock().unwrap() = Some((code.clone(), Instant::now() + CODE_LIFETIME));
        Pairing {
            code,
            addresses: local_addresses(),
            port: PORT,
            expires_in: CODE_LIFETIME.as_secs(),
            devices: load_devices().into_iter().map(|d| d.name).collect(),
        }
    }

    pub fn stop_pairing(&self) {
        *self.code.lock().unwrap() = None;
    }

    pub fn forget_all(&self) -> Result<(), String> {
        save_devices(&[])?;
        enabled_changed(false);
        Ok(())
    }

    /// Confere o código; se estiver certo e no prazo, ele é gasto e o celular ganha um token.
    fn pair(&self, code: &str, name: &str) -> Result<(String, String), &'static str> {
        let mut current = self.code.lock().unwrap();
        match current.as_ref() {
            Some((expected, until)) if Instant::now() <= *until && same(expected, code) => {
                *current = None;
                let token = new_token();
                let id = crate::link::new_id();
                let mut devices = load_devices();
                devices.push(Device { id: id.clone(), name: name.chars().take(40).collect(), token: token.clone(), features: Features::default() });
                save_devices(&devices).map_err(|_| "não deu para salvar o pareamento")?;
                Ok((id, token))
            }
            Some(_) => Err("código inválido ou expirado"),
            None => Err("abra o pareamento na tela de Mídia da TV"),
        }
    }

    fn device_for(&self, token: &str) -> Option<Device> {
        load_devices().into_iter().find(|d| same(&d.token, token))
    }
}

fn hostname() -> String {
    std::fs::read_to_string("/etc/hostname").map(|h| h.trim().to_string()).unwrap_or_else(|_| "lumna".into())
}

/// Anuncia o serviço na rede para o app achar a TV sem digitar IP.
fn announce() -> Option<mdns_sd::ServiceDaemon> {
    let daemon = mdns_sd::ServiceDaemon::new().ok()?;
    let host = hostname();
    let info = mdns_sd::ServiceInfo::new(SERVICE, &format!("LUMNA {host}"), &format!("{host}.local."), "", PORT, &[("v", "1")][..])
        .ok()?
        .enable_addr_auto();
    daemon.register(info).ok()?;
    Some(daemon)
}

pub fn start(app: AppHandle) {
    tauri::async_runtime::spawn(async move {
        let listener = match TcpListener::bind(("0.0.0.0", PORT)).await {
            Ok(listener) => listener,
            Err(err) => {
                eprintln!("lumna-shell: controle remoto na porta {PORT}: {err}");
                return;
            }
        };
        // Mantém o anúncio vivo enquanto o servidor roda.
        let _mdns = announce();
        // o que for copiado aqui vai pros celulares
        crate::link::clipboard_watch(|text| broadcast(json!({ "type": "clipboard", "text": text })));
        loop {
            let Ok((stream, peer)) = listener.accept().await else { continue };
            if !is_local(peer.ip()) {
                continue;
            }
            let app = app.clone();
            tauri::async_runtime::spawn(async move {
                if let Err(err) = serve(app, stream).await {
                    eprintln!("lumna-shell: controle remoto {peer}: {err}");
                }
            });
        }
    });
}

async fn serve(app: AppHandle, stream: TcpStream) -> Result<(), String> {
    let mut ws = tokio_tungstenite::accept_async(stream).await.map_err(|e| e.to_string())?;
    // (id, nome) do celular, depois que ele se identifica
    let mut device: Option<(String, String)> = None;
    // avisos do PC pra este celular (entra na lista quando ele se identifica)
    let (tx, mut rx) = mpsc::unbounded_channel::<String>();
    let client = NEXT_CLIENT.fetch_add(1, Ordering::Relaxed);
    let result = loop {
        let message = tokio::select! {
            message = ws.next() => match message {
                Some(message) => message,
                None => break Ok(()),
            },
            Some(event) = rx.recv() => {
                if event == CLOSE {
                    let _ = ws.close(None).await;
                    break Ok(());
                }
                if let Err(e) = ws.send(Message::Text(event.into())).await {
                    break Err(e.to_string());
                }
                continue;
            }
        };
        let text = match message {
            Ok(Message::Text(text)) => text.to_string(),
            Ok(Message::Close(_)) => break Ok(()),
            Ok(_) => continue,
            // Celular que some sem se despedir (Wi-Fi caiu, app fechado): normal, não é erro.
            Err(Error::Protocol(ProtocolError::ResetWithoutClosingHandshake)) => break Ok(()),
            Err(err) => break Err(err.to_string()),
        };
        let Ok(msg) = serde_json::from_str::<Value>(&text) else { continue };
        let kind = msg["type"].as_str().unwrap_or_default();
        let field = |key: &str| msg[key].as_str().unwrap_or_default().to_string();
        let reply = match (kind, &device) {
            ("pair" | "hello", _) if !crate::link::enabled() => {
                json!({ "type": "error", "message": "o Connect está desligado neste PC" })
            }
            ("pair", _) => {
                let name = Some(field("name")).filter(|n| !n.is_empty()).unwrap_or_else(|| "Celular".into());
                match app.state::<Remote>().pair(&field("code"), &name) {
                    Ok((id, token)) => {
                        let _ = app.emit("lumna://remote-paired", &name);
                        device = Some((id.clone(), name));
                        CLIENTS.lock().unwrap().push(Client { id: client, device: id, tx: tx.clone() });
                        devices_changed(&app);
                        json!({ "type": "paired", "token": token, "name": format!("LUMNA {}", hostname()) })
                    }
                    Err(reason) => json!({ "type": "error", "message": reason }),
                }
            }
            ("hello", _) => match app.state::<Remote>().device_for(&field("token")) {
                Some(d) => {
                    if device.is_none() {
                        CLIENTS.lock().unwrap().push(Client { id: client, device: d.id.clone(), tx: tx.clone() });
                        devices_changed(&app);
                    }
                    device = Some((d.id, d.name));
                    json!({ "type": "ready", "name": format!("LUMNA {}", hostname()) })
                }
                None => json!({ "type": "error", "message": "celular não pareado", "unpaired": true }),
            },
            (_, None) => json!({ "type": "error", "message": "pareie o celular primeiro", "unpaired": true }),
            (_, Some((id, _))) => {
                let app = app.clone();
                let msg = msg.clone();
                let id = id.clone();
                // Os comandos falam com o compositor e com processos: fora do laço assíncrono.
                tokio::task::spawn_blocking(move || command(&app, &id, &msg))
                    .await
                    .unwrap_or_else(|e| Err(e.to_string()))
                    .map_or_else(|err| json!({ "type": "error", "message": err }), |extra| extra.unwrap_or(json!({ "type": "ok" })))
            }
        };
        if let Err(e) = ws.send(Message::Text(reply.to_string().into())).await {
            break Err(e.to_string());
        }
    };
    CLIENTS.lock().unwrap().retain(|c| c.id != client);
    if let Some((id, name)) = &device {
        if !online(id) {
            crate::link::recv_cancel_peer(&app, name);
            offers_cancel(&app, id);
        }
        devices_changed(&app);
    }
    result
}

fn run(program: &str, args: &[&str]) -> Result<(), String> {
    let status = std::process::Command::new(program).args(args).status().map_err(|e| format!("{program}: {e}"))?;
    status.success().then_some(()).ok_or_else(|| format!("{program} falhou"))
}

/// Executa um comando de um celular já pareado. Pode devolver uma resposta além do "ok".
fn command(app: &AppHandle, device_id: &str, msg: &Value) -> Result<Option<Value>, String> {
    let field = |key: &str| msg[key].as_str().unwrap_or_default();
    let device = device(device_id).ok_or("celular não pareado")?;
    let need = |on: bool, what: &str| if on { Ok(()) } else { Err(format!("{what} está desligado pra este celular no PC")) };
    let kind = msg["type"].as_str().unwrap_or_default();
    if matches!(kind, "power" | "emma" | "emma-decide" | "auth-respond") || kind.starts_with("schedule-") {
        need(device.features.control, "o controle de longe")?;
    }
    if kind.starts_with("file-") && kind != "file-pull" && kind != "file-reject" {
        need(device.features.files, "receber arquivos")?;
    }
    // o id da transferência vem do celular: separa por aparelho
    let transfer = || format!("{}:{}", device.id, field("id"));
    match kind {
        "pad" => {
            let action = Action::parse(field("action")).ok_or("ação desconhecida")?;
            crate::pad_action(app, action);
            Ok(None)
        }
        // Digita no campo em foco (busca do launcher, do YouTube...).
        "text" => {
            let text = field("value");
            if text.is_empty() || text.len() > 500 {
                return Err("texto vazio ou longo demais".into());
            }
            run("wtype", &["--", text]).map(|_| None)
        }
        "key" => {
            let key = match field("value") {
                "backspace" => "BackSpace",
                "enter" => "Return",
                "escape" => "Escape",
                "space" => "space",
                _ => return Err("tecla desconhecida".into()),
            };
            run("wtype", &["-k", key]).map(|_| None)
        }
        "media" => {
            let args: &[&str] = match field("action") {
                "play-pause" => &["play-pause"],
                "next" => &["next"],
                "previous" => &["previous"],
                "forward" => &["position", "10+"],
                "back" => &["position", "10-"],
                _ => return Err("comando de mídia desconhecido".into()),
            };
            run("playerctl", args).map(|_| None)
        }
        "volume" => {
            match field("action") {
                "up" => crate::status::wpctl(&["set-volume", "-l", "1", "@DEFAULT_AUDIO_SINK@", "5%+"])?,
                "down" => crate::status::wpctl(&["set-volume", "@DEFAULT_AUDIO_SINK@", "5%-"])?,
                "mute" => crate::status::wpctl(&["set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"])?,
                _ => return Err("comando de volume desconhecido".into()),
            }
            let status = crate::status::read();
            crate::status::refresh(app);
            Ok(Some(json!({ "type": "volume", "value": status.volume, "muted": status.muted })))
        }
        "mode" => {
            let mode = Mode::parse(field("value")).ok_or("modo desconhecido")?;
            crate::switch_mode(app, Target::Set(mode)).map(|_| None)
        }
        // Fecha o que estiver na frente (o streaming, o vídeo) e volta para a tela de Mídia.
        "home" => crate::remote_home(app).map(|_| None),
        // ---- Celular Connect ----
        "info" => Ok(Some(info())),
        "power" => {
            let action = field("action");
            if !matches!(action, "lock" | "suspend" | "reboot" | "poweroff") {
                return Err("ação de energia desconhecida".into());
            }
            tauri::async_runtime::block_on(crate::power::power_action(app.clone(), action.to_string())).map(|_| None)
        }
        "emma" => {
            let text = field("text");
            if text.trim().is_empty() || text.len() > 4000 {
                return Err("mensagem vazia ou longa demais".into());
            }
            let reply = crate::emma::ask(app, text)?;
            Ok(Some(json!({ "type": "emma-reply", "content": reply })))
        }
        "emma-decide" => {
            let allow = msg["allow"].as_bool().unwrap_or(false);
            tauri::async_runtime::block_on(crate::emma::emma_decide(app.clone(), field("call_id").to_string(), allow)).map(|_| None)
        }
        "auth-respond" => {
            crate::polkit::auth_respond(field("cookie").to_string(), Some(field("password").to_string()));
            Ok(None)
        }
        "clipboard" => {
            need(device.features.clipboard, "a área de transferência")?;
            crate::link::clipboard_set(field("text")).map(|_| None)
        }
        // avisos do celular (e ligações chegando)
        "notify" => {
            need(device.features.phone_notifications, "mostrar avisos do celular")?;
            let from = if field("app").is_empty() { device.name.clone() } else { format!("{} · {}", device.name, field("app")) };
            crate::notifications::local(app, &from, field("summary"), field("body"), msg["urgent"].as_bool().unwrap_or(false));
            Ok(None)
        }
        "file-begin" => {
            let size = msg["size"].as_u64().ok_or("tamanho faltando")?;
            crate::link::recv_begin(app, &device.name, &transfer(), field("name"), size).map(|_| None)
        }
        "file-chunk" => crate::link::recv_chunk(app, &transfer(), field("data")).map(|_| None),
        "file-end" => crate::link::recv_end(app, &transfer()).map(|_| None),
        "file-cancel" => {
            crate::link::recv_cancel(app, &transfer(), "o celular cancelou");
            Ok(None)
        }
        "file-pull" => offer_pull(app, &device.id, field("id"), msg["offset"].as_u64().unwrap_or(0)).map(Some),
        "file-reject" => {
            offer_fail(app, field("id"), field("reason"));
            Ok(None)
        }
        "schedule-list" => Ok(Some(json!({ "type": "schedule", "tasks": crate::schedule::list() }))),
        "schedule-add" => {
            let task: crate::schedule::Task = serde_json::from_value(msg["task"].clone()).map_err(|e| e.to_string())?;
            crate::schedule::add(app, task, &device.name)?;
            Ok(Some(json!({ "type": "schedule", "tasks": crate::schedule::list() })))
        }
        "schedule-remove" => {
            crate::schedule::remove(app, field("id"))?;
            Ok(Some(json!({ "type": "schedule", "tasks": crate::schedule::list() })))
        }
        // o que está ligado pra este celular aqui no PC
        "features" => Ok(Some(json!({ "type": "features", "features": device.features }))),
        _ => Err("comando desconhecido".into()),
    }
}

/// Como está o PC (pra página do aparelho no celular).
fn info() -> Value {
    let uptime = std::fs::read_to_string("/proc/uptime")
        .ok()
        .and_then(|t| t.split_whitespace().next()?.parse::<f64>().ok())
        .map_or(0, |s| s as u64);
    let load = std::fs::read_to_string("/proc/loadavg").ok().and_then(|t| t.split_whitespace().next().map(str::to_string));
    let (total, available) = {
        let text = std::fs::read_to_string("/proc/meminfo").unwrap_or_default();
        let get = |key: &str| {
            text.lines().find(|l| l.starts_with(key)).and_then(|l| l.split_whitespace().nth(1)?.parse::<u64>().ok()).unwrap_or(0)
        };
        (get("MemTotal:"), get("MemAvailable:"))
    };
    let status = crate::status::read();
    json!({
        "type": "info",
        "host": hostname(),
        "user": std::env::var("USER").unwrap_or_default(),
        "uptime": uptime,
        "load": load,
        "memory_total_kb": total,
        "memory_available_kb": available,
        "battery": status.battery,
        "charging": status.charging,
    })
}

// ---- arquivos do PC pro celular: o PC oferece, o celular puxa ----

struct Offer {
    device: String,
    path: PathBuf,
    transfer: crate::link::Transfer,
}

static OFFERS: Mutex<Option<std::collections::HashMap<String, Offer>>> = Mutex::new(None);

fn offer_pull(app: &AppHandle, device: &str, id: &str, offset: u64) -> Result<Value, String> {
    let mut offers = OFFERS.lock().unwrap();
    let map = offers.get_or_insert_with(Default::default);
    let offer = map.get_mut(id).filter(|o| o.device == device).ok_or("esse arquivo não está mais sendo oferecido")?;
    let (data, len, eof) = crate::link::read_chunk(&offer.path, offset)?;
    offer.transfer.done = offset + len as u64;
    if eof {
        offer.transfer.state = "done";
    }
    crate::link::progress(app, offer.transfer.clone());
    if eof {
        map.remove(id);
    }
    Ok(crate::link::chunk_reply(data, len, eof))
}

fn offer_fail(app: &AppHandle, id: &str, reason: &str) {
    if let Some(mut offer) = OFFERS.lock().unwrap().as_mut().and_then(|m| m.remove(id)) {
        offer.transfer.state = "error";
        offer.transfer.error = Some(if reason.is_empty() { "o celular recusou".into() } else { reason.to_string() });
        crate::link::progress(app, offer.transfer);
    }
}

fn offers_cancel(app: &AppHandle, device: &str) {
    let ids: Vec<String> = OFFERS
        .lock()
        .unwrap()
        .as_ref()
        .map(|m| m.iter().filter(|(_, o)| o.device == device).map(|(id, _)| id.clone()).collect())
        .unwrap_or_default();
    for id in ids {
        offer_fail(app, &id, "a conexão caiu");
    }
}

// ---- comandos da página Celular (no PC) ----

#[derive(Serialize, Clone)]
pub struct DeviceView {
    pub id: String,
    pub name: String,
    pub online: bool,
    pub features: Features,
}

#[tauri::command]
pub fn remote_devices() -> Vec<DeviceView> {
    load_devices()
        .into_iter()
        .map(|d| DeviceView { online: online(&d.id), id: d.id, name: d.name, features: d.features })
        .collect()
}

#[tauri::command]
pub fn remote_device_set(app: AppHandle, id: String, features: Features) -> Result<(), String> {
    let mut devices = load_devices();
    let d = devices.iter_mut().find(|d| d.id == id).ok_or("celular não pareado")?;
    d.features = features;
    save_devices(&devices)?;
    devices_changed(&app);
    Ok(())
}

#[tauri::command]
pub fn remote_device_forget(app: AppHandle, id: String) -> Result<(), String> {
    let mut devices = load_devices();
    devices.retain(|d| d.id != id);
    save_devices(&devices)?;
    for c in CLIENTS.lock().unwrap().iter().filter(|c| c.device == id) {
        let _ = c.tx.send(CLOSE.into());
    }
    devices_changed(&app);
    Ok(())
}

/// Oferece arquivos pra um celular; ele puxa (e mostra o progresso nos dois).
#[tauri::command]
pub fn remote_send_files(app: AppHandle, id: String, paths: Vec<String>) -> Result<(), String> {
    let d = device(&id).ok_or("celular não pareado")?;
    if !online(&id) {
        return Err(format!("{} não está conectado", d.name));
    }
    for (path, name, size) in crate::link::files_to_send(&paths)? {
        let offer_id = crate::link::new_id();
        let transfer = crate::link::Transfer {
            id: offer_id.clone(),
            name: name.clone(),
            peer: d.name.clone(),
            direction: "out",
            done: 0,
            size,
            state: "running",
            error: None,
            path: Some(path.to_string_lossy().into()),
        };
        crate::link::progress(&app, transfer.clone());
        OFFERS.lock().unwrap().get_or_insert_with(Default::default).insert(offer_id.clone(), Offer { device: id.clone(), path, transfer });
        if !send_to(&id, &json!({ "type": "file-offer", "id": offer_id, "name": name, "size": size })) {
            offer_fail(&app, &offer_id, "o celular desconectou");
        }
    }
    Ok(())
}

/// Manda o que está copiado agora pra um celular.
#[tauri::command]
pub fn remote_send_clipboard(id: String) -> Result<(), String> {
    let text = crate::link::clipboard_get().ok_or("não tem texto copiado")?;
    send_to(&id, &json!({ "type": "clipboard", "text": text })).then_some(()).ok_or_else(|| "o celular não está conectado".into())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_local_networks_are_accepted() {
        assert!(is_local("192.168.0.12".parse().unwrap()));
        assert!(is_local("10.1.2.3".parse().unwrap()));
        assert!(is_local("127.0.0.1".parse().unwrap()));
        assert!(is_local("fe80::1".parse().unwrap()));
        assert!(!is_local("8.8.8.8".parse().unwrap()));
        // Tailscale
        assert!(is_local("100.101.102.103".parse().unwrap()));
        assert!(!is_local("100.128.0.1".parse().unwrap()));
        assert!(!is_local("2001:db8::1".parse().unwrap()));
    }

    #[test]
    fn codes_have_six_digits_and_tokens_are_long() {
        for _ in 0..50 {
            let code = new_code();
            assert_eq!(code.len(), 6);
            assert!(code.chars().all(|c| c.is_ascii_digit()));
        }
        assert_eq!(new_token().len(), 64);
        assert_ne!(new_token(), new_token());
    }

    #[test]
    fn compares_secrets_exactly() {
        assert!(same("123456", "123456"));
        assert!(!same("123456", "123457"));
        assert!(!same("12345", "123456"));
    }
}
