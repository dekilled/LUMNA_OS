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

use std::io::Read;
use std::net::IpAddr;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::{Duration, Instant};

use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager};
use tokio::net::{TcpListener, TcpStream};
use tokio_tungstenite::tungstenite::error::{Error, ProtocolError};
use tokio_tungstenite::tungstenite::Message;

use crate::modes::{Mode, Target};
use crate::pad::Action;

pub const PORT: u16 = 47800;
const SERVICE: &str = "_lumna-remote._tcp.local.";
const CODE_LIFETIME: Duration = Duration::from_secs(5 * 60);

#[derive(Serialize, Deserialize, Clone)]
struct Device {
    name: String,
    token: String,
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
    devices_path()
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_default()
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

/// Só aceita conexões da rede local (e da própria máquina).
fn is_local(ip: IpAddr) -> bool {
    match ip {
        IpAddr::V4(v4) => v4.is_loopback() || v4.is_private() || v4.is_link_local(),
        IpAddr::V6(v6) => {
            v6.is_loopback() || (v6.segments()[0] & 0xfe00) == 0xfc00 || (v6.segments()[0] & 0xffc0) == 0xfe80
        }
    }
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
        save_devices(&[])
    }

    /// Confere o código; se estiver certo e no prazo, ele é gasto e o celular ganha um token.
    fn pair(&self, code: &str, name: &str) -> Result<String, &'static str> {
        let mut current = self.code.lock().unwrap();
        match current.as_ref() {
            Some((expected, until)) if Instant::now() <= *until && same(expected, code) => {
                *current = None;
                let token = new_token();
                let mut devices = load_devices();
                devices.push(Device { name: name.chars().take(40).collect(), token: token.clone() });
                save_devices(&devices).map_err(|_| "não deu para salvar o pareamento")?;
                Ok(token)
            }
            Some(_) => Err("código inválido ou expirado"),
            None => Err("abra o pareamento na tela de Mídia da TV"),
        }
    }

    fn device_for(&self, token: &str) -> Option<String> {
        load_devices().into_iter().find(|d| same(&d.token, token)).map(|d| d.name)
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
    let mut device: Option<String> = None;
    while let Some(message) = ws.next().await {
        let text = match message {
            Ok(Message::Text(text)) => text.to_string(),
            Ok(Message::Close(_)) => break,
            Ok(_) => continue,
            // Celular que some sem se despedir (Wi-Fi caiu, app fechado): normal, não é erro.
            Err(Error::Protocol(ProtocolError::ResetWithoutClosingHandshake)) => break,
            Err(err) => return Err(err.to_string()),
        };
        let Ok(msg) = serde_json::from_str::<Value>(&text) else { continue };
        let kind = msg["type"].as_str().unwrap_or_default();
        let field = |key: &str| msg[key].as_str().unwrap_or_default().to_string();
        let reply = match (kind, &device) {
            ("pair", _) => {
                let name = Some(field("name")).filter(|n| !n.is_empty()).unwrap_or_else(|| "Celular".into());
                match app.state::<Remote>().pair(&field("code"), &name) {
                    Ok(token) => {
                        let _ = app.emit("lumna://remote-paired", &name);
                        device = Some(name);
                        json!({ "type": "paired", "token": token, "name": format!("LUMNA {}", hostname()) })
                    }
                    Err(reason) => json!({ "type": "error", "message": reason }),
                }
            }
            ("hello", _) => match app.state::<Remote>().device_for(&field("token")) {
                Some(name) => {
                    device = Some(name);
                    json!({ "type": "ready", "name": format!("LUMNA {}", hostname()) })
                }
                None => json!({ "type": "error", "message": "celular não pareado", "unpaired": true }),
            },
            (_, None) => json!({ "type": "error", "message": "pareie o celular primeiro", "unpaired": true }),
            (_, Some(_)) => {
                let app = app.clone();
                let msg = msg.clone();
                // Os comandos falam com o compositor e com processos: fora do laço assíncrono.
                tokio::task::spawn_blocking(move || command(&app, &msg))
                    .await
                    .unwrap_or_else(|e| Err(e.to_string()))
                    .map_or_else(|err| json!({ "type": "error", "message": err }), |extra| extra.unwrap_or(json!({ "type": "ok" })))
            }
        };
        ws.send(Message::Text(reply.to_string().into())).await.map_err(|e| e.to_string())?;
    }
    Ok(())
}

fn run(program: &str, args: &[&str]) -> Result<(), String> {
    let status = std::process::Command::new(program).args(args).status().map_err(|e| format!("{program}: {e}"))?;
    status.success().then_some(()).ok_or_else(|| format!("{program} falhou"))
}

/// Executa um comando de um celular já pareado. Pode devolver uma resposta além do "ok".
fn command(app: &AppHandle, msg: &Value) -> Result<Option<Value>, String> {
    let field = |key: &str| msg[key].as_str().unwrap_or_default();
    match msg["type"].as_str().unwrap_or_default() {
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
        _ => Err("comando desconhecido".into()),
    }
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
