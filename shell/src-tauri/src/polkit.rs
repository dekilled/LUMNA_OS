//! Agente do polkit: quando algo pede permissão de administrador (instalar um programa, o
//! `pkexec`, o Flatpak...), quem pergunta a senha é a tela do LUMNA, não uma janela genérica.
//!
//! O polkit chama `BeginAuthentication`; o shell mostra a camada `auth`, recebe a senha e a
//! entrega ao `polkit-agent-helper-1` (pelo socket `/run/polkit/agent-helper.socket` no polkit
//! 126+, ou rodando o helper setuid nas versões antigas), que confere pelo PAM e avisa o polkit.

use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use serde::Serialize;
use tauri::AppHandle;
use tokio::sync::{oneshot, OnceCell};
use zbus::zvariant::{OwnedValue, Value};
use zbus::Connection;

const AGENT_PATH: &str = "/os/lumna/polkit/agent";
const HELPER_SOCKET: &str = "/run/polkit/agent-helper.socket";
const HELPER: &str = "/usr/lib/polkit-1/polkit-agent-helper-1";
const ATTEMPTS: u32 = 3;

/// Pedido de senha para a tela.
#[derive(Debug, Clone, Serialize)]
pub struct Request {
    pub cookie: String,
    /// O que está pedindo permissão, na língua do sistema (vem da política do polkit).
    pub message: String,
    pub action: String,
    /// Conta cuja senha é pedida.
    pub user: String,
    /// Mensagem da tentativa anterior ("Senha incorreta").
    pub error: Option<String>,
}

#[derive(Debug, zbus::DBusError)]
#[zbus(prefix = "org.freedesktop.PolicyKit1.Error")]
enum PkError {
    #[zbus(error)]
    ZBus(zbus::Error),
    Failed(String),
    Cancelled(String),
}

/// Resposta da tela: `Some(senha)` ou `None` (cancelou).
type Answer = oneshot::Sender<Option<String>>;

struct Agent {
    app: AppHandle,
    answers: Arc<Mutex<HashMap<String, Answer>>>,
}

/// Nome da conta pelo uid, lendo o /etc/passwd.
fn user_name(uid: u32) -> Option<String> {
    std::fs::read_to_string("/etc/passwd").ok()?.lines().find_map(|line| {
        let fields: Vec<&str> = line.split(':').collect();
        (fields.get(2)?.parse::<u32>().ok()? == uid).then(|| fields[0].to_string())
    })
}

fn own_uid() -> Option<u32> {
    // Dono do /proc/self: o uid real deste processo, sem precisar da libc.
    use std::os::unix::fs::MetadataExt;
    std::fs::metadata("/proc/self").ok().map(|m| m.uid())
}

/// Escolhe a conta: a própria pessoa, se o polkit aceitar; senão a primeira oferecida (root ou
/// outro administrador).
pub fn choose_user(uids: &[u32], own: Option<u32>) -> Option<u32> {
    own.filter(|uid| uids.contains(uid)).or_else(|| uids.first().copied())
}

/// Desfaz o escape do `g_strescape` nas linhas do helper (`\n`, `\t`, `\\`, `\"`).
pub fn unescape(line: &str) -> String {
    let mut out = String::with_capacity(line.len());
    let mut chars = line.chars();
    while let Some(c) = chars.next() {
        if c != '\\' {
            out.push(c);
            continue;
        }
        match chars.next() {
            Some('n') => out.push('\n'),
            Some('t') => out.push('\t'),
            Some(other) => out.push(other),
            None => {}
        }
    }
    out
}

/// Confere a senha pelo helper do polkit. `Ok(true)` autenticou; `Ok(false)` senha errada.
fn run_helper(user: &str, cookie: &str, password: &str) -> Result<bool, String> {
    use std::os::unix::net::UnixStream;
    let (reader, mut writer): (Box<dyn std::io::Read>, Box<dyn Write>) =
        if std::path::Path::new(HELPER_SOCKET).exists() {
            let stream = UnixStream::connect(HELPER_SOCKET).map_err(|e| format!("{HELPER_SOCKET}: {e}"))?;
            let mut writer = stream.try_clone().map_err(|e| e.to_string())?;
            // No modo por socket o helper lê primeiro a conta, depois o cookie.
            writeln!(writer, "{user}").map_err(|e| e.to_string())?;
            (Box::new(stream), Box::new(writer))
        } else {
            let mut child = std::process::Command::new(HELPER)
                .arg(user)
                .stdin(std::process::Stdio::piped())
                .stdout(std::process::Stdio::piped())
                .spawn()
                .map_err(|e| format!("{HELPER}: {e}"))?;
            let stdout = child.stdout.take().ok_or("helper sem saída")?;
            let stdin = child.stdin.take().ok_or("helper sem entrada")?;
            std::thread::spawn(move || child.wait());
            (Box::new(stdout), Box::new(stdin))
        };
    writeln!(writer, "{cookie}").map_err(|e| e.to_string())?;
    let mut answered = false;
    for line in BufReader::new(reader).lines() {
        let line = unescape(&line.map_err(|e| e.to_string())?);
        if line.starts_with("PAM_PROMPT_ECHO_OFF") || line.starts_with("PAM_PROMPT_ECHO_ON") {
            // Só sabemos responder a senha; um segundo pedido (código de dois fatores) falha.
            if answered {
                return Ok(false);
            }
            writeln!(writer, "{password}").map_err(|e| e.to_string())?;
            answered = true;
        } else if line.starts_with("SUCCESS") {
            return Ok(true);
        } else if line.starts_with("FAILURE") {
            return Ok(false);
        }
    }
    Ok(false)
}

#[zbus::interface(name = "org.freedesktop.PolicyKit1.AuthenticationAgent")]
impl Agent {
    async fn begin_authentication(
        &self,
        action_id: String,
        message: String,
        _icon_name: String,
        _details: HashMap<String, String>,
        cookie: String,
        identities: Vec<(String, HashMap<String, OwnedValue>)>,
    ) -> Result<(), PkError> {
        let uids: Vec<u32> = identities
            .iter()
            .filter(|(kind, _)| kind == "unix-user")
            .filter_map(|(_, details)| details.get("uid").and_then(|v| u32::try_from(v).ok()))
            .collect();
        let uid = choose_user(&uids, own_uid()).ok_or_else(|| PkError::Failed("nenhuma conta para autenticar".into()))?;
        let user = user_name(uid).ok_or_else(|| PkError::Failed(format!("conta {uid} desconhecida")))?;

        let mut error = None;
        let mut tries = 0;
        let result = loop {
            let (tx, rx) = oneshot::channel();
            self.answers.lock().unwrap().insert(cookie.clone(), tx);
            let request = Request { cookie: cookie.clone(), message: message.clone(), action: action_id.clone(), user: user.clone(), error };
            // o pedido também vai pros celulares do Connect (dá pra digitar a senha de lá)
            crate::remote::broadcast(serde_json::json!({
                "type": "auth",
                "cookie": request.cookie,
                "message": request.message,
                "user": request.user,
                "error": request.error,
            }));
            crate::auth_layer(&self.app, Some(request));
            let Ok(Some(password)) = rx.await else {
                break Err(PkError::Cancelled("cancelado".into()));
            };
            let (user_c, cookie_c) = (user.clone(), cookie.clone());
            let checked = tauri::async_runtime::spawn_blocking(move || run_helper(&user_c, &cookie_c, &password))
                .await
                .map_err(|e| e.to_string())
                .and_then(|r| r);
            match checked {
                Ok(true) => break Ok(()),
                Ok(false) => error = Some("Senha incorreta. Tente de novo.".to_string()),
                Err(err) => {
                    eprintln!("lumna-shell: polkit: {err}");
                    break Err(PkError::Failed(err));
                }
            }
            // Depois de algumas tentativas erradas o pedido é recusado.
            tries += 1;
            if tries >= ATTEMPTS {
                break Err(PkError::Failed("muitas tentativas".into()));
            }
        };
        self.answers.lock().unwrap().remove(&cookie);
        crate::remote::broadcast(serde_json::json!({ "type": "auth-done", "cookie": cookie }));
        crate::auth_layer(&self.app, None);
        result
    }

    fn cancel_authentication(&self, cookie: String) {
        if let Some(tx) = self.answers.lock().unwrap().remove(&cookie) {
            let _ = tx.send(None);
        }
    }
}

struct Polkit {
    answers: Arc<Mutex<HashMap<String, Answer>>>,
    /// O agente responde por esta conexão: ela vive enquanto o shell rodar.
    _connection: Connection,
}

static POLKIT: OnceCell<Polkit> = OnceCell::const_new();

/// Sessão do logind deste processo (o polkit só aceita agentes por sessão).
async fn session_id(connection: &Connection) -> Result<String, String> {
    if let Ok(id) = std::env::var("XDG_SESSION_ID") {
        if !id.is_empty() {
            return Ok(id);
        }
    }
    let reply = connection
        .call_method(
            Some("org.freedesktop.login1"),
            "/org/freedesktop/login1",
            Some("org.freedesktop.login1.Manager"),
            "GetSessionByPID",
            &(std::process::id()),
        )
        .await
        .map_err(|e| format!("logind: {e}"))?;
    let path: zbus::zvariant::OwnedObjectPath = reply.body().deserialize().map_err(|e| e.to_string())?;
    let reply = connection
        .call_method(
            Some("org.freedesktop.login1"),
            path.as_str(),
            Some("org.freedesktop.DBus.Properties"),
            "Get",
            &("org.freedesktop.login1.Session", "Id"),
        )
        .await
        .map_err(|e| format!("logind: {e}"))?;
    let value: OwnedValue = reply.body().deserialize().map_err(|e| e.to_string())?;
    String::try_from(value).map_err(|e| e.to_string())
}

async fn register(app: &AppHandle) -> Result<(), String> {
    let connection = Connection::system().await.map_err(|e| format!("D-Bus do sistema: {e}"))?;
    let answers = Arc::new(Mutex::new(HashMap::new()));
    connection
        .object_server()
        .at(AGENT_PATH, Agent { app: app.clone(), answers: answers.clone() })
        .await
        .map_err(|e| e.to_string())?;
    let session = session_id(&connection).await?;
    let subject: (&str, HashMap<&str, Value>) = ("unix-session", HashMap::from([("session-id", Value::from(session.as_str()))]));
    let locale = std::env::var("LANG").unwrap_or_else(|_| "pt_BR.UTF-8".into());
    connection
        .call_method(
            Some("org.freedesktop.PolicyKit1"),
            "/org/freedesktop/PolicyKit1/Authority",
            Some("org.freedesktop.PolicyKit1.Authority"),
            "RegisterAuthenticationAgent",
            &(subject, locale.as_str(), AGENT_PATH),
        )
        .await
        .map_err(|e| format!("polkit: {e}"))?;
    let _ = POLKIT.set(Polkit { answers, _connection: connection });
    Ok(())
}

/// Registra o agente; se o polkit ainda não estiver de pé, tenta de novo.
pub fn start(app: AppHandle) {
    tauri::async_runtime::spawn(async move {
        let mut wait = Duration::from_secs(2);
        for _ in 0..8 {
            match register(&app).await {
                Ok(()) => return,
                Err(err) => eprintln!("lumna-shell: agente do polkit: {err} (nova tentativa em {wait:?})"),
            }
            tokio::time::sleep(wait).await;
            wait = (wait * 2).min(Duration::from_secs(60));
        }
    });
}

/// Resposta da tela de senha (`None`: cancelou).
#[tauri::command]
pub fn auth_respond(cookie: String, password: Option<String>) {
    if let Some(polkit) = POLKIT.get() {
        if let Some(tx) = polkit.answers.lock().unwrap().remove(&cookie) {
            let _ = tx.send(password);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn helper_protocol_bits() {
        assert_eq!(unescape(r"Senha:\t"), "Senha:\t");
        assert_eq!(unescape(r#"a\\b \"c\""#), r#"a\b "c""#);
        assert_eq!(choose_user(&[0, 1000], Some(1000)), Some(1000));
        assert_eq!(choose_user(&[0], Some(1000)), Some(0));
        assert_eq!(choose_user(&[], Some(1000)), None);
    }
}
