//! Hyprland pelo IPC de socket Unix, sem depender de `hyprctl` nem de crates de terceiros.
//!
//! - `.socket.sock`: um pedido por conexão (`j/workspaces`, `dispatch ...`), resposta até o EOF.
//! - `.socket2.sock`: fluxo de eventos, uma linha `EVENTO>>DADOS` por evento.

use std::io::{BufRead, BufReader, Read, Write};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;

use lumna_core::{Compositor, CompositorEvent, Monitor, Result, Window, Workspace};
use serde_json::Value;

pub struct Hyprland {
    dir: PathBuf,
}

impl Hyprland {
    /// Acha a instância do Hyprland desta sessão pelas variáveis que ele mesmo exporta.
    pub fn from_env() -> Result<Self> {
        let signature = std::env::var("HYPRLAND_INSTANCE_SIGNATURE")
            .map_err(|_| "HYPRLAND_INSTANCE_SIGNATURE não definida: o shell precisa rodar dentro do Hyprland".to_string())?;
        let runtime = std::env::var("XDG_RUNTIME_DIR").map_err(|_| "XDG_RUNTIME_DIR não definida".to_string())?;
        Ok(Self { dir: PathBuf::from(runtime).join("hypr").join(signature) })
    }

    fn request(&self, command: &str) -> Result<String> {
        let path = self.dir.join(".socket.sock");
        let mut stream = UnixStream::connect(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        stream.write_all(command.as_bytes()).map_err(|e| e.to_string())?;
        let mut reply = String::new();
        stream.read_to_string(&mut reply).map_err(|e| e.to_string())?;
        Ok(reply)
    }

    fn request_json(&self, what: &str) -> Result<Value> {
        let reply = self.request(&format!("j/{what}"))?;
        serde_json::from_str(&reply).map_err(|e| format!("resposta inválida de `{what}`: {e}: {reply}"))
    }

    /// Executa um dispatcher. Hyprland 0.56+ com config em Lua recebe uma expressão `hl.dsp.*`;
    /// versões e configs antigas recebem a forma clássica. Tenta a nova e cai para a antiga.
    fn dispatch(&self, lua: &str, legacy: &str) -> Result<()> {
        let reply = self.request(&format!("dispatch {lua}"))?;
        if reply.trim() == "ok" {
            return Ok(());
        }
        let fallback = self.request(&format!("dispatch {legacy}"))?;
        if fallback.trim() == "ok" {
            Ok(())
        } else {
            Err(format!("dispatch recusado: {} / {}", reply.trim(), fallback.trim()))
        }
    }
}

fn int(v: &Value, key: &str) -> i64 {
    v.get(key).and_then(Value::as_i64).unwrap_or_default()
}

fn text(v: &Value, key: &str) -> String {
    v.get(key).and_then(Value::as_str).unwrap_or_default().to_string()
}

/// Monta `hl.dsp.exec_cmd(...)` com o comando numa string longa do Lua, que não interpreta escapes.
fn lua_exec(command: &str) -> Result<String> {
    if command.contains("]==]") || command.contains('\n') {
        return Err(format!("comando não pode ser passado ao Hyprland: {command:?}"));
    }
    Ok(format!("hl.dsp.exec_cmd([==[{command}]==])"))
}

/// Converte uma linha do socket de eventos. Devolve vazio para eventos que o shell ignora.
pub fn parse_event(line: &str) -> Vec<CompositorEvent> {
    let Some((name, data)) = line.split_once(">>") else { return Vec::new() };
    match name {
        "workspacev2" => data
            .split_once(',')
            .and_then(|(id, _)| id.parse().ok())
            .map(|id| vec![CompositorEvent::WorkspaceFocused { id }])
            .unwrap_or_default(),
        "focusedmon" => {
            let name = data.split_once(',').map_or(data, |(n, _)| n).to_string();
            vec![CompositorEvent::MonitorFocused { name }, CompositorEvent::WorkspacesChanged]
        }
        "createworkspacev2" | "destroyworkspacev2" | "moveworkspacev2" | "renameworkspace" | "movewindowv2" => {
            vec![CompositorEvent::WorkspacesChanged]
        }
        "openwindow" => {
            // ENDEREÇO,WORKSPACE,CLASSE,TÍTULO (o título pode ter vírgulas)
            let mut parts = data.splitn(4, ',');
            let address = parts.next().unwrap_or_default().to_string();
            let _workspace = parts.next();
            let class = parts.next().unwrap_or_default().to_string();
            let title = parts.next().unwrap_or_default().to_string();
            vec![CompositorEvent::WindowOpened { address, class, title }, CompositorEvent::WorkspacesChanged]
        }
        "closewindow" => vec![
            CompositorEvent::WindowClosed { address: data.to_string() },
            CompositorEvent::WorkspacesChanged,
        ],
        "monitoradded" | "monitorremoved" => vec![CompositorEvent::MonitorsChanged, CompositorEvent::WorkspacesChanged],
        _ => Vec::new(),
    }
}

impl Compositor for Hyprland {
    fn workspaces(&self) -> Result<Vec<Workspace>> {
        let list = self.request_json("workspaces")?;
        let mut out: Vec<Workspace> = list
            .as_array()
            .into_iter()
            .flatten()
            .map(|w| Workspace {
                id: int(w, "id"),
                name: text(w, "name"),
                monitor: text(w, "monitor"),
                windows: int(w, "windows").max(0) as u32,
            })
            .collect();
        out.sort_by_key(|w| w.id);
        Ok(out)
    }

    fn monitors(&self) -> Result<Vec<Monitor>> {
        let list = self.request_json("monitors")?;
        Ok(list
            .as_array()
            .into_iter()
            .flatten()
            .map(|m| Monitor {
                id: int(m, "id"),
                name: text(m, "name"),
                focused: m.get("focused").and_then(Value::as_bool).unwrap_or(false),
                active_workspace: m.get("activeWorkspace").map(|w| int(w, "id")).unwrap_or_default(),
                x: int(m, "x") as i32,
                y: int(m, "y") as i32,
            })
            .collect())
    }

    fn windows(&self) -> Result<Vec<Window>> {
        let list = self.request_json("clients")?;
        Ok(list
            .as_array()
            .into_iter()
            .flatten()
            .filter(|c| c.get("mapped").and_then(Value::as_bool).unwrap_or(true))
            .map(|c| Window {
                address: text(c, "address"),
                class: text(c, "class"),
                title: text(c, "title"),
                workspace: c.get("workspace").map(|w| int(w, "id")).unwrap_or_default(),
            })
            .collect())
    }

    fn exec(&self, command: &str) -> Result<()> {
        self.dispatch(&lua_exec(command)?, &format!("exec {command}"))
    }

    fn focus_workspace(&self, id: i64) -> Result<()> {
        self.dispatch(&format!("hl.dsp.focus({{ workspace = {id} }})"), &format!("workspace {id}"))
    }

    fn listen(&self, on_event: &mut dyn FnMut(CompositorEvent)) -> Result<()> {
        let path = self.dir.join(".socket2.sock");
        let stream = UnixStream::connect(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        for line in BufReader::new(stream).lines() {
            let line = line.map_err(|e| e.to_string())?;
            for event in parse_event(&line) {
                on_event(event);
            }
        }
        Err("o Hyprland fechou o socket de eventos".into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn workspace_focus() {
        assert_eq!(parse_event("workspacev2>>3,3"), vec![CompositorEvent::WorkspaceFocused { id: 3 }]);
        assert_eq!(
            parse_event("workspacev2>>-98,special:media"),
            vec![CompositorEvent::WorkspaceFocused { id: -98 }]
        );
    }

    #[test]
    fn window_title_keeps_commas() {
        let events = parse_event("openwindow>>55d3a0,1,foot,oi, tudo bem?");
        assert_eq!(
            events[0],
            CompositorEvent::WindowOpened { address: "55d3a0".into(), class: "foot".into(), title: "oi, tudo bem?".into() }
        );
    }

    #[test]
    fn exec_is_a_lua_long_string() {
        assert_eq!(lua_exec("foot -e 'a \"b\"'").unwrap(), "hl.dsp.exec_cmd([==[foot -e 'a \"b\"']==])");
        assert!(lua_exec("x ]==] y").is_err());
    }

    #[test]
    fn ignores_unknown_and_v1_duplicates() {
        assert!(parse_event("workspace>>3").is_empty());
        assert!(parse_event("createworkspace>>3").is_empty());
        assert!(parse_event("lixo").is_empty());
    }
}
