//! Hyprland pelo IPC de socket Unix, sem depender de `hyprctl` nem de crates de terceiros.
//!
//! - `.socket.sock`: um pedido por conexão (`j/workspaces`, `dispatch ...`), resposta até o EOF.
//! - `.socket2.sock`: fluxo de eventos, uma linha `EVENTO>>DADOS` por evento.

use std::io::{BufRead, BufReader, Read, Write};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;

use lumna_core::{Compositor, CompositorEvent, KeyboardInfo, Monitor, Result, Window, Workspace};
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

/// Versões antigas mandam `true`/`false`; as novas, o modo (1 = maximizada, 2 = tela cheia).
fn fullscreen(v: &Value) -> i64 {
    match v.get("fullscreen") {
        Some(Value::Bool(on)) => i64::from(*on) * 2,
        Some(other) => other.as_i64().unwrap_or_default(),
        None => 0,
    }
}

fn scale(m: &Value) -> f64 {
    m.get("scale").and_then(Value::as_f64).filter(|s| *s > 0.0).unwrap_or(1.0)
}

/// Largura e altura no layout: pixels do modo ÷ escala, trocadas se a tela estiver em pé.
fn logical_size(m: &Value) -> (i32, i32) {
    let s = scale(m);
    let (w, h) = ((int(m, "width") as f64 / s).round() as i32, (int(m, "height") as f64 / s).round() as i32);
    if int(m, "transform") % 2 == 1 { (h, w) } else { (w, h) }
}

fn monitor_label(m: &Value) -> String {
    let label = format!("{} {}", text(m, "make"), text(m, "model")).trim().to_string();
    if label.is_empty() { text(m, "name") } else { label }
}

/// `1.0` → "1", `1.25` → "1.25".
pub fn scale_text(scale: f64) -> String {
    let text = format!("{scale:.3}");
    text.trim_end_matches('0').trim_end_matches('.').to_string()
}

/// Layouts (`"br,us"` → `["br", "us"]`) e o índice do que está em uso.
fn keyboard_info(k: &Value) -> KeyboardInfo {
    let layouts: Vec<String> = text(k, "layout").split(',').map(|l| l.trim().to_string()).filter(|l| !l.is_empty()).collect();
    let active = k.get("active_layout_index").and_then(Value::as_u64).unwrap_or(0) as usize;
    KeyboardInfo { active: active.min(layouts.len().saturating_sub(1)), layouts }
}

/// O teclado em Lua (`hl.config` com `kb_layout`, `kb_variant` e `kb_options`). Os nomes do xkb só
/// têm letras, números e `_-(),:`.
pub fn keyboard_rule(layout: &str, variant: &str, options: &str) -> Result<String> {
    for value in [layout, variant, options] {
        if !value.chars().all(|c| c.is_ascii_alphanumeric() || "_-(),:".contains(c)) {
            return Err(format!("nome de teclado inválido: {value:?}"));
        }
    }
    Ok(format!(
        "hl.config({{ input = {{ kb_layout = \"{layout}\", kb_variant = \"{variant}\", kb_options = \"{options}\" }} }})"
    ))
}

/// A regra em Lua que põe um monitor no lugar (a mesma do `hyprland.lua`, com posição fixa).
pub fn monitor_rule(name: &str, x: i32, y: i32, scale: f64) -> Result<String> {
    Ok(format!(
        "hl.monitor({{ output = {}, mode = \"preferred\", position = \"{x}x{y}\", scale = \"{}\" }})",
        lua_string(name)?,
        scale_text(scale)
    ))
}

fn text(v: &Value, key: &str) -> String {
    v.get(key).and_then(Value::as_str).unwrap_or_default().to_string()
}

/// Texto como string longa do Lua (`[==[...]==]`), que não interpreta escapes.
fn lua_string(text: &str) -> Result<String> {
    if text.contains("]==]") || text.contains('\n') {
        return Err(format!("texto não pode ser passado ao Hyprland: {text:?}"));
    }
    Ok(format!("[==[{text}]==]"))
}

/// `0x55d3a0` (ou `55d3a0`, como vem nos eventos) → `address:0x55d3a0`.
fn window_selector(address: &str) -> Result<String> {
    let hex = address.trim().trim_start_matches("0x");
    if hex.is_empty() || !hex.chars().all(|c| c.is_ascii_hexdigit()) {
        return Err(format!("endereço de janela inválido: {address:?}"));
    }
    Ok(format!("address:0x{hex}"))
}

fn lua_exec(command: &str) -> Result<String> {
    Ok(format!("hl.dsp.exec_cmd({})", lua_string(command)?))
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
        "activewindowv2" | "windowtitlev2" => vec![CompositorEvent::ActiveWindowChanged],
        "fullscreen" => vec![CompositorEvent::FullscreenChanged],
        "activelayout" => {
            let name = data.split_once(',').map_or(data, |(_, layout)| layout).to_string();
            vec![CompositorEvent::KeyboardLayout { name }]
        }
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
                active_workspace_name: m.get("activeWorkspace").map(|w| text(w, "name")).unwrap_or_default(),
                special_workspace: m.get("specialWorkspace").map(|w| int(w, "id")).unwrap_or_default(),
                x: int(m, "x") as i32,
                y: int(m, "y") as i32,
                width: logical_size(m).0,
                height: logical_size(m).1,
                scale: scale(m),
                label: monitor_label(m),
            })
            .collect())
    }

    fn window_bars(&self) -> bool {
        self.request("plugin list").is_ok_and(|list| list.contains("hyprbars"))
    }

    fn keyboard(&self) -> Result<KeyboardInfo> {
        let devices = self.request_json("devices")?;
        let keyboards = devices.get("keyboards").and_then(Value::as_array).cloned().unwrap_or_default();
        let main = keyboards
            .iter()
            .find(|k| k.get("main").and_then(Value::as_bool).unwrap_or(false))
            .or_else(|| keyboards.first())
            .ok_or("nenhum teclado")?;
        Ok(keyboard_info(main))
    }

    fn next_keyboard_layout(&self) -> Result<()> {
        let reply = self.request("switchxkblayout all next")?;
        if reply.trim() == "ok" {
            Ok(())
        } else {
            Err(format!("não deu para trocar o teclado: {}", reply.trim()))
        }
    }

    fn set_keyboard(&self, layout: &str, variant: &str, options: &str) -> Result<()> {
        let reply = self.request(&format!("eval {}", keyboard_rule(layout, variant, options)?))?;
        if reply.trim() == "ok" {
            Ok(())
        } else {
            Err(format!("o Hyprland recusou o teclado: {}", reply.trim()))
        }
    }

    fn place_monitor(&self, name: &str, x: i32, y: i32, scale: f64) -> Result<()> {
        let rule = monitor_rule(name, x, y, scale)?;
        let reply = self.request(&format!("eval {rule}"))?;
        // Confere no próprio compositor: a resposta do `eval` não diz muito.
        let placed = |me: &Self| me.monitors().is_ok_and(|list| list.iter().any(|m| m.name == name && (m.x, m.y) == (x, y)));
        if placed(self) {
            return Ok(());
        }
        // Config antiga (sem Lua): a palavra-chave clássica.
        let legacy = self.request(&format!("keyword monitor {name},preferred,{x}x{y},{}", scale_text(scale)))?;
        if placed(self) {
            return Ok(());
        }
        Err(format!("o Hyprland não moveu {name}: {} / {}", reply.trim(), legacy.trim()))
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
                workspace_name: c.get("workspace").map(|w| text(w, "name")).unwrap_or_default(),
                monitor: int(c, "monitor"),
                fullscreen: fullscreen(c),
            })
            .collect())
    }

    fn focus_named_workspace(&self, name: &str) -> Result<()> {
        let selector = lua_string(&format!("name:{name}"))?;
        self.dispatch(&format!("hl.dsp.focus({{ workspace = {selector} }})"), &format!("workspace name:{name}"))
    }

    fn focus_monitor(&self, name: &str) -> Result<()> {
        let selector = lua_string(name)?;
        self.dispatch(&format!("hl.dsp.focus({{ monitor = {selector} }})"), &format!("focusmonitor {name}"))
    }

    fn active_window(&self) -> Result<Option<Window>> {
        let w = self.request_json("activewindow")?;
        let address = text(&w, "address");
        if address.is_empty() {
            return Ok(None);
        }
        Ok(Some(Window {
            address,
            class: text(&w, "class"),
            title: text(&w, "title"),
            workspace: w.get("workspace").map(|ws| int(ws, "id")).unwrap_or_default(),
            workspace_name: w.get("workspace").map(|ws| text(ws, "name")).unwrap_or_default(),
            monitor: int(&w, "monitor"),
            fullscreen: fullscreen(&w),
        }))
    }

    fn close_active_window(&self) -> Result<()> {
        self.dispatch("hl.dsp.window.close()", "killactive")
    }

    fn focus_window(&self, address: &str) -> Result<()> {
        let window = window_selector(address)?;
        self.dispatch(&format!("hl.dsp.focus({{ window = {} }})", lua_string(&window)?), &format!("focuswindow {window}"))
    }

    fn close_window(&self, address: &str) -> Result<()> {
        let window = window_selector(address)?;
        self.dispatch(&format!("hl.dsp.window.close({{ window = {} }})", lua_string(&window)?), &format!("closewindow {window}"))
    }

    fn move_window(&self, address: &str, workspace: &str) -> Result<()> {
        let window = window_selector(address)?;
        let target = match workspace.parse::<i64>() {
            Ok(id) => id.to_string(),
            Err(_) => lua_string(workspace)?,
        };
        // `follow = false`: a janela vai sozinha (sem isso o Hyprland abre o workspace de destino,
        // e "minimizar" mostrava o workspace escondido por cima, com a janela ainda na tela).
        self.dispatch(
            &format!("hl.dsp.window.move({{ workspace = {target}, window = {}, follow = false }})", lua_string(&window)?),
            &format!("movetoworkspacesilent {workspace},{window}"),
        )
    }

    fn fullscreen_window(&self, address: &str) -> Result<()> {
        self.focus_window(address)?;
        self.dispatch("hl.dsp.window.fullscreen()", "fullscreen 0")
    }

    fn maximize_window(&self, address: &str) -> Result<()> {
        self.focus_window(address)?;
        self.dispatch("hl.dsp.window.fullscreen({ mode = \"maximized\" })", "fullscreen 1")
    }

    fn exec(&self, command: &str) -> Result<()> {
        self.dispatch(&lua_exec(command)?, &format!("exec {command}"))
    }

    fn exit(&self) -> Result<()> {
        self.dispatch("hl.dsp.exit()", "exit")
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
    #[test]
    fn keyboard_layouts_and_event() {
        let k = serde_json::json!({ "layout": "br,us", "active_layout_index": 1, "main": true });
        assert_eq!(keyboard_info(&k), KeyboardInfo { layouts: vec!["br".into(), "us".into()], active: 1 });
        let single = serde_json::json!({ "layout": "br", "active_layout_index": "none" });
        assert_eq!(keyboard_info(&single).active, 0);
        assert_eq!(
            parse_event("activelayout>>at-translated-set-2-keyboard,English (US)"),
            vec![CompositorEvent::KeyboardLayout { name: "English (US)".into() }]
        );
    }

    #[test]
    fn keyboard_rule_quotes_only_xkb_names() {
        assert_eq!(
            keyboard_rule("br,us", "abnt2,intl", "grp:alt_shift_toggle").unwrap(),
            r#"hl.config({ input = { kb_layout = "br,us", kb_variant = "abnt2,intl", kb_options = "grp:alt_shift_toggle" } })"#
        );
        assert!(keyboard_rule("br\"})os.exit()--", "", "").is_err());
    }

    #[test]
    fn monitor_rule_and_logical_size() {
        assert_eq!(
            monitor_rule("HDMI-A-1", 2560, 180, 1.0).unwrap(),
            r#"hl.monitor({ output = [==[HDMI-A-1]==], mode = "preferred", position = "2560x180", scale = "1" })"#
        );
        assert!(monitor_rule("DP-1]==]", 0, 0, 1.0).is_err());
        assert_eq!(scale_text(1.25), "1.25");
        let m = serde_json::json!({ "width": 2560, "height": 1440, "scale": 1.25, "transform": 0 });
        assert_eq!(logical_size(&m), (2048, 1152));
        let rotated = serde_json::json!({ "width": 1920, "height": 1080, "scale": 1.0, "transform": 1 });
        assert_eq!(logical_size(&rotated), (1080, 1920));
        assert_eq!(monitor_label(&serde_json::json!({ "name": "DP-1", "make": "Samsung", "model": "Odyssey G5" })), "Samsung Odyssey G5");
        assert_eq!(monitor_label(&serde_json::json!({ "name": "DP-1", "make": "", "model": "" })), "DP-1");
    }

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
    fn window_addresses() {
        assert_eq!(window_selector("55d3a0").unwrap(), "address:0x55d3a0");
        assert_eq!(window_selector("0x55d3a0").unwrap(), "address:0x55d3a0");
        assert!(window_selector("x; exit").is_err());
    }

    #[test]
    fn ignores_unknown_and_v1_duplicates() {
        assert!(parse_event("workspace>>3").is_empty());
        assert!(parse_event("createworkspace>>3").is_empty());
        assert!(parse_event("lixo").is_empty());
    }
}
