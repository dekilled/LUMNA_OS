//! Telas: em que ordem os monitores ficam lado a lado (da esquerda para a direita) e como se
//! alinham na altura. É o que decide por qual borda o mouse passa de uma tela para a outra.
//!
//! A escolha vale na hora (o compositor move os monitores) e fica em `~/.config/lumna/monitors.json`;
//! as regras prontas para o Hyprland vão para `~/.config/lumna/monitors.lua`, que o `hyprland.lua`
//! carrega no login.

use std::path::PathBuf;

use lumna_core::{CompositorEvent, Monitor};
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager};

use crate::{MonitorPositions, SharedCompositor};

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Align {
    Top,
    #[default]
    Center,
    Bottom,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct Arrangement {
    /// Nomes das saídas (`DP-1`, `HDMI-A-1`...) da esquerda para a direita.
    pub order: Vec<String>,
    pub align: Align,
}

/// Cópia da ordem para a tela de login (criado pelo tmpfiles do lumna-session, gravável por todos).
const LOGIN_SCREEN_FILE: &str = "/var/lib/lumna/monitors";

fn config_dir() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna")
}

fn load() -> Arrangement {
    std::fs::read_to_string(config_dir().join("monitors.json"))
        .ok()
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_default()
}

/// Posição de cada monitor: lado a lado na ordem pedida (os que não estão nela vêm depois, na
/// ordem atual da esquerda para a direita), encostados, alinhados na altura.
pub fn layout(monitors: &[Monitor], arrangement: &Arrangement) -> Vec<(String, i32, i32)> {
    let mut rest: Vec<&Monitor> = monitors.iter().collect();
    rest.sort_by_key(|m| (m.x, m.y));
    let mut ordered: Vec<&Monitor> = arrangement
        .order
        .iter()
        .filter_map(|name| monitors.iter().find(|m| &m.name == name))
        .collect();
    ordered.dedup_by_key(|m| m.name.clone());
    rest.retain(|m| !ordered.iter().any(|o| o.name == m.name));
    ordered.extend(rest);

    let tallest = ordered.iter().map(|m| m.height).max().unwrap_or(0);
    let mut x = 0;
    ordered
        .into_iter()
        .map(|m| {
            let y = match arrangement.align {
                Align::Top => 0,
                Align::Center => (tallest - m.height) / 2,
                Align::Bottom => tallest - m.height,
            };
            let placed = (m.name.clone(), x, y);
            x += m.width;
            placed
        })
        .collect()
}

fn lua_file(monitors: &[Monitor], placements: &[(String, i32, i32)]) -> Result<String, String> {
    let mut text = String::from(
        "-- Gerado pelo LUMNA (Configurações → Telas). Mudanças à mão são trocadas na próxima vez\n\
         -- que as telas forem arrumadas por lá.\n",
    );
    for (name, x, y) in placements {
        let scale = monitors.iter().find(|m| &m.name == name).map_or(1.0, |m| m.scale);
        text += &lumna_hypr::monitor_rule(name, *x, *y, scale)?;
        text.push('\n');
    }
    Ok(text)
}

#[derive(Debug, Clone, Serialize)]
pub struct Displays {
    /// Da esquerda para a direita.
    pub monitors: Vec<Monitor>,
    pub align: Align,
}

#[tauri::command]
pub fn displays(app: AppHandle) -> Result<Displays, String> {
    let mut monitors = app.state::<SharedCompositor>().monitors()?;
    monitors.sort_by_key(|m| (m.x, m.y));
    Ok(Displays { monitors, align: load().align })
}

#[tauri::command]
pub fn displays_arrange(app: AppHandle, order: Vec<String>, align: Align) -> Result<Displays, String> {
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let before = compositor.monitors()?;
    let arrangement = Arrangement { order, align };
    let placements = layout(&before, &arrangement);

    let dir = config_dir();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    std::fs::write(dir.join("monitors.lua"), lua_file(&before, &placements)?).map_err(|e| e.to_string())?;
    let json = serde_json::to_string_pretty(&arrangement).map_err(|e| e.to_string())?;
    std::fs::write(dir.join("monitors.json"), json + "\n").map_err(|e| e.to_string())?;

    // A tela de login (outro usuário, sem acesso à pasta da pessoa) lê a mesma ordem daqui.
    let shared: String = placements
        .iter()
        .map(|(name, x, y)| {
            let scale = before.iter().find(|m| &m.name == name).map_or(1.0, |m| m.scale);
            format!("{name} {x} {y} {}\n", lumna_hypr::scale_text(scale))
        })
        .collect();
    if let Err(err) = std::fs::write(LOGIN_SCREEN_FILE, shared) {
        eprintln!("lumna-shell: {LOGIN_SCREEN_FILE}: {err} (a tela de login fica com a ordem automática)");
    }

    let mut errors = Vec::new();
    for (name, x, y) in &placements {
        let scale = before.iter().find(|m| &m.name == name).map_or(1.0, |m| m.scale);
        if let Err(err) = compositor.place_monitor(name, *x, *y, scale) {
            errors.push(err);
        }
    }

    // As camadas por monitor são achadas pela posição: quem estava num lugar segue o seu monitor.
    let after = compositor.monitors()?;
    {
        let state = app.state::<MonitorPositions>();
        let mut positions = state.0.lock().unwrap();
        for pos in positions.values_mut() {
            let name = before.iter().find(|m| (m.x, m.y) == *pos).map(|m| m.name.clone());
            if let Some(now) = name.and_then(|n| after.iter().find(|m| m.name == n)) {
                *pos = (now.x, now.y);
            }
        }
    }
    crate::dock::update(&app, true);
    let _ = app.emit("lumna://compositor", CompositorEvent::MonitorsChanged);

    if !errors.is_empty() {
        return Err(format!("Gravado para o próximo login, mas não deu para mover agora: {}", errors.join("; ")));
    }
    displays(app)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn monitor(name: &str, x: i32, width: i32, height: i32) -> Monitor {
        Monitor {
            id: 0,
            name: name.into(),
            focused: false,
            active_workspace: 1,
            active_workspace_name: "1".into(),
            special_workspace: 0,
            x,
            y: 0,
            width,
            height,
            scale: 1.0,
            label: name.into(),
        }
    }

    #[test]
    fn side_by_side_in_the_chosen_order() {
        // O Odyssey (1440p) foi detectado à esquerda; a pessoa quer o AOC à esquerda.
        let monitors = [monitor("DP-1", 0, 2560, 1440), monitor("HDMI-A-1", 2560, 1920, 1080)];
        let arrangement = Arrangement { order: vec!["HDMI-A-1".into(), "DP-1".into()], align: Align::Center };
        assert_eq!(layout(&monitors, &arrangement), [("HDMI-A-1".into(), 0, 180), ("DP-1".into(), 1920, 0)]);
        let bottom = Arrangement { align: Align::Bottom, ..arrangement.clone() };
        assert_eq!(layout(&monitors, &bottom)[0], ("HDMI-A-1".into(), 0, 360));
        let top = Arrangement { align: Align::Top, ..arrangement };
        assert_eq!(layout(&monitors, &top)[1], ("DP-1".into(), 1920, 0));
    }

    #[test]
    fn unknown_names_are_ignored_and_new_monitors_go_last() {
        let monitors = [monitor("DP-1", 0, 1920, 1080), monitor("DP-2", 1920, 1920, 1080), monitor("DP-3", 3840, 1280, 1024)];
        let arrangement = Arrangement { order: vec!["DP-2".into(), "SUMIU-1".into(), "DP-2".into()], align: Align::Top };
        let placed = layout(&monitors, &arrangement);
        assert_eq!(placed, [("DP-2".into(), 0, 0), ("DP-1".into(), 1920, 0), ("DP-3".into(), 3840, 0)]);
    }

    #[test]
    fn lua_file_has_one_rule_per_monitor() {
        let monitors = [monitor("DP-1", 0, 1920, 1080)];
        let text = lua_file(&monitors, &[("DP-1".into(), 0, 0)]).unwrap();
        assert!(text.contains(r#"hl.monitor({ output = [==[DP-1]==], mode = "preferred", position = "0x0", scale = "1" })"#));
    }
}
