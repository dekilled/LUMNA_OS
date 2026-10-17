//! Tela de login (`lumna-shell --greeter`): conversa com o greetd pelo socket dele
//! ($GREETD_SOCK, mensagens JSON com o tamanho na frente) e, com a senha certa, pede a sessão
//! LUMNA e fecha o Hyprland da tela de login.

use std::io::{Read, Write};
use std::os::unix::net::UnixStream;

use serde::Serialize;
use serde_json::{json, Value};

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct User {
    pub username: String,
    /// Nome completo (campo GECOS); vazio se não houver.
    pub name: String,
}

/// Contas de pessoas no /etc/passwd: uid de 1000 a 59999 com shell de verdade.
pub fn parse_users(passwd: &str) -> Vec<User> {
    passwd
        .lines()
        .filter_map(|line| {
            let f: Vec<&str> = line.split(':').collect();
            let uid: u32 = f.get(2)?.parse().ok()?;
            let shell = f.get(6)?;
            if !(1000..60000).contains(&uid) || shell.ends_with("nologin") || shell.ends_with("false") {
                return None;
            }
            Some(User { username: f[0].to_string(), name: f.get(4)?.split(',').next().unwrap_or("").to_string() })
        })
        .collect()
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Session {
    pub id: String,
    pub name: String,
    /// Comando que o greetd roda como a pessoa (sem shell: separado por espaços).
    pub exec: Vec<String>,
}

/// Uma sessão Wayland (`/usr/share/wayland-sessions/*.desktop`).
pub fn parse_session(id: &str, text: &str) -> Option<Session> {
    let field = |key: &str| text.lines().find_map(|l| l.strip_prefix(key)?.strip_prefix('=')).map(str::trim);
    if field("Hidden") == Some("true") || field("NoDisplay") == Some("true") {
        return None;
    }
    let exec: Vec<String> = field("Exec")?.split_whitespace().filter(|a| !a.starts_with('%')).map(str::to_string).collect();
    if exec.is_empty() {
        return None;
    }
    Some(Session { id: id.to_string(), name: field("Name").unwrap_or(id).to_string(), exec })
}

/// Sessões instaladas, com a do LUMNA primeiro.
#[tauri::command]
pub fn greeter_sessions() -> Vec<Session> {
    let mut list: Vec<Session> = std::fs::read_dir("/usr/share/wayland-sessions")
        .map(|dir| {
            dir.flatten()
                .filter_map(|e| {
                    let path = e.path();
                    let id = path.file_stem()?.to_string_lossy().into_owned();
                    parse_session(&id, &std::fs::read_to_string(&path).ok()?)
                })
                .collect()
        })
        .unwrap_or_default();
    list.sort_by_key(|s| (s.id != "lumna", s.name.to_lowercase()));
    if list.is_empty() {
        list.push(Session { id: "lumna".into(), name: "LUMNA · Hyprland".into(), exec: vec!["lumna-session".into()] });
    }
    list
}

#[tauri::command]
pub fn greeter_users() -> Vec<User> {
    std::fs::read_to_string("/etc/passwd").map(|p| parse_users(&p)).unwrap_or_default()
}

fn send(stream: &mut UnixStream, message: &Value) -> Result<(), String> {
    let body = message.to_string();
    stream.write_all(&(body.len() as u32).to_ne_bytes()).map_err(|e| e.to_string())?;
    stream.write_all(body.as_bytes()).map_err(|e| e.to_string())
}

fn receive(stream: &mut UnixStream) -> Result<Value, String> {
    let mut len = [0u8; 4];
    stream.read_exact(&mut len).map_err(|e| format!("greetd: {e}"))?;
    let mut body = vec![0u8; u32::from_ne_bytes(len) as usize];
    stream.read_exact(&mut body).map_err(|e| format!("greetd: {e}"))?;
    serde_json::from_slice(&body).map_err(|e| e.to_string())
}

/// Erro de autenticação do greetd em português.
fn describe(reply: &Value) -> String {
    match reply["error_type"].as_str() {
        Some("auth_error") => "Senha incorreta.".into(),
        _ => reply["description"].as_str().unwrap_or("Não deu para entrar.").to_string(),
    }
}

/// Faz o login inteiro: cria a sessão, responde a senha e pede a sessão escolhida.
fn login(username: &str, password: &str, command: &[String]) -> Result<(), String> {
    let path = std::env::var("GREETD_SOCK").map_err(|_| "Sem conexão com o greetd (GREETD_SOCK).".to_string())?;
    let mut stream = UnixStream::connect(path).map_err(|e| format!("greetd: {e}"))?;
    send(&mut stream, &json!({ "type": "create_session", "username": username }))?;
    let mut answered = false;
    loop {
        let reply = receive(&mut stream)?;
        match reply["type"].as_str() {
            Some("auth_message") => {
                let kind = reply["auth_message_type"].as_str().unwrap_or("");
                let response = match kind {
                    // Só sabemos responder a senha, uma vez; um segundo pedido (código) cancela.
                    "secret" if !answered => {
                        answered = true;
                        json!(password)
                    }
                    "visible" | "secret" => {
                        let _ = send(&mut stream, &json!({ "type": "cancel_session" }));
                        return Err("Esta conta pede uma confirmação que a tela ainda não sabe fazer.".into());
                    }
                    // Avisos e erros do PAM: só confirmar.
                    _ => Value::Null,
                };
                send(&mut stream, &json!({ "type": "post_auth_message_response", "response": response }))?;
            }
            Some("success") => break,
            _ => {
                let _ = send(&mut stream, &json!({ "type": "cancel_session" }));
                let _ = receive(&mut stream);
                return Err(describe(&reply));
            }
        }
    }
    send(&mut stream, &json!({ "type": "start_session", "cmd": command, "env": [] }))?;
    let reply = receive(&mut stream)?;
    if reply["type"] == "success" {
        Ok(())
    } else {
        Err(describe(&reply))
    }
}

/// Entra; com sucesso, fecha o Hyprland da tela de login (o greetd abre a sessão em seguida).
#[tauri::command]
pub async fn greeter_login(
    compositor: tauri::State<'_, crate::SharedCompositor>,
    username: String,
    password: String,
    session: Option<String>,
) -> Result<(), String> {
    let compositor = compositor.inner().clone();
    // Só sessões instaladas: a tela não manda comando solto para o greetd.
    let command = greeter_sessions()
        .into_iter()
        .find(|s| Some(&s.id) == session.as_ref())
        .map(|s| s.exec)
        .unwrap_or_else(|| vec!["lumna-session".into()]);
    tauri::async_runtime::spawn_blocking(move || -> Result<(), String> {
        login(&username, &password, &command)?;
        // Avisa o lumna-greeter que o Hyprland vai fechar de propósito (não é para subir de novo).
        if let Some(done) = std::env::var_os("LUMNA_GREETER_DONE") {
            let _ = std::fs::write(done, b"");
        }
        // Um instante para a animação de saída da tela.
        std::thread::sleep(std::time::Duration::from_millis(350));
        let _ = compositor.exit();
        std::process::exit(0);
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Desligar ou reiniciar (o logind deixa quem está na frente do computador).
#[tauri::command]
pub fn greeter_power(action: String) -> Result<(), String> {
    let verb = match action.as_str() {
        "poweroff" => "poweroff",
        "reboot" => "reboot",
        "suspend" => "suspend",
        _ => return Err("ação desconhecida".into()),
    };
    std::process::Command::new("systemctl").arg(verb).status().map(|_| ()).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_sessions() {
        let lumna = "[Desktop Entry]\nName=LUMNA · Hyprland\nExec=lumna-session\nType=Application\n";
        assert_eq!(
            parse_session("lumna", lumna),
            Some(Session { id: "lumna".into(), name: "LUMNA · Hyprland".into(), exec: vec!["lumna-session".into()] })
        );
        assert_eq!(parse_session("x", "Name=X\nExec=start-hyprland -- %U\n").unwrap().exec, vec!["start-hyprland", "--"]);
        assert_eq!(parse_session("y", "Name=Y\nExec=y\nHidden=true\n"), None);
    }

    #[test]
    fn lists_people() {
        let passwd = "root:x:0:0::/root:/bin/bash\n\
greeter:x:964:964:greetd greeter user:/var/lib/greetd:/usr/bin/nologin\n\
luan:x:1000:1000:Luan Eric,,,:/home/luan:/bin/bash\n\
nobody:x:65534:65534:Kernel Overflow User:/:/usr/bin/nologin\n\
ana:x:1001:1001::/home/ana:/bin/zsh\n";
        assert_eq!(
            parse_users(passwd),
            vec![
                User { username: "luan".into(), name: "Luan Eric".into() },
                User { username: "ana".into(), name: String::new() },
            ]
        );
    }
}
