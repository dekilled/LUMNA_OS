//! Dock: um por monitor (`dock-0`, `dock-1`...), com as escolhas da pessoa em `~/.config/lumna/dock.json`.
//!
//! O que fica no dock (apps, separadores, "Todos os apps", Emma), o tamanho, o espaçamento, a
//! opacidade do fundo, a posição, em quais monitores aparece e o que acontece quando uma janela é
//! maximizada: continuar ali, se esconder na borda (volta passando o mouse) ou virar uma barra de
//! largura inteira com os ícones no centro, como a barra de tarefas do Windows.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Mutex;

use gtk_layer_shell::Edge;
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager, WebviewWindow};

use crate::layer::{self, Exclusive};
use crate::modes::{Mode, Modes};
use crate::{MonitorPositions, SharedCompositor};

/// Separador entre grupos de ícones.
pub const SEPARATOR: &str = "|";
pub const LAUNCHER: &str = "lumna:launcher";
pub const EMMA: &str = "lumna:emma";

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Behavior {
    /// Sempre visível, reservando o espaço dele.
    Always,
    /// Some quando uma janela é maximizada; volta passando o mouse na borda.
    #[default]
    Smart,
    /// Com janela maximizada, vira uma barra de largura inteira (estilo Windows).
    Expand,
}

#[derive(Debug, Clone, Copy, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Position {
    #[default]
    Bottom,
    Left,
    Right,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(default)]
pub struct Config {
    /// Ids de `.desktop`, `|` (separador), `lumna:launcher` e `lumna:emma`, na ordem do dock.
    pub pinned: Vec<String>,
    /// Lado dos ícones, em pixels.
    pub size: u32,
    /// Espaço entre os ícones, em pixels.
    pub spacing: u32,
    /// Opacidade do fundo, de 0 (transparente) a 1.
    pub opacity: f64,
    pub behavior: Behavior,
    pub position: Position,
    /// `primary`, `all` ou o nome de uma saída (ex.: `DP-1`).
    pub monitors: String,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            pinned: ["lumna-installer", "lumna-files", "firefox", "org.xfce.mousepad", "foot", SEPARATOR, LAUNCHER, EMMA]
                .map(String::from)
                .to_vec(),
            size: 44,
            spacing: 6,
            opacity: 0.55,
            behavior: Behavior::default(),
            position: Position::default(),
            monitors: "primary".into(),
        }
    }
}

impl Config {
    /// Valores fora da faixa (arquivo editado à mão) voltam para dentro dela.
    fn clamped(mut self) -> Self {
        self.size = self.size.clamp(28, 72);
        self.spacing = self.spacing.min(24);
        self.opacity = if self.opacity.is_finite() { self.opacity.clamp(0.0, 1.0) } else { 0.55 };
        self.pinned.retain(|id| !id.trim().is_empty() && !id.contains('/'));
        // Dois separadores seguidos não separam nada.
        self.pinned.dedup_by(|a, b| a == SEPARATOR && b == SEPARATOR);
        if self.monitors.trim().is_empty() {
            self.monitors = "primary".into();
        }
        self
    }
}

fn config_path() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna/dock.json")
}

fn load() -> Config {
    std::fs::read_to_string(config_path())
        .ok()
        .and_then(|t| serde_json::from_str::<Config>(&t).ok())
        .unwrap_or_default()
        .clamped()
}

fn save(config: &Config) -> Result<(), String> {
    let path = config_path();
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    let text = serde_json::to_string_pretty(config).map_err(|e| e.to_string())?;
    std::fs::write(&path, text + "\n").map_err(|e| format!("{}: {e}", path.display()))
}

/// Como o dock de um monitor está agora.
#[derive(Debug, Clone, Copy, Default, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Look {
    /// Flutuando na borda, do tamanho dos ícones.
    #[default]
    Normal,
    /// Escondido: só uma faixa fina na borda, que o traz de volta com o mouse.
    Hidden,
    /// Barra de largura (ou altura) inteira, ícones no centro.
    Expanded,
}

#[derive(Debug, Clone, Serialize)]
pub struct DockState {
    pub look: Look,
    pub config: Config,
}

/// Configuração em uso e o que já foi aplicado em cada camada (visível, aparência).
pub struct Docks {
    config: Mutex<Config>,
    applied: Mutex<HashMap<String, (bool, Look)>>,
}

impl Default for Docks {
    fn default() -> Self {
        Docks { config: Mutex::new(load()), applied: Mutex::new(HashMap::new()) }
    }
}

/// O dock aparece neste monitor? `index` é a posição no GTK (0 = principal).
fn shows_on(monitors: &str, index: usize, name: &str, names: &[String]) -> bool {
    match monitors {
        "all" => true,
        "primary" => index == 0,
        // Um monitor pelo nome; se ele não estiver ligado, o dock vai para o principal.
        wanted if names.iter().any(|n| n == wanted) => name == wanted,
        _ => index == 0,
    }
}

fn look_for(behavior: Behavior, maximized: bool) -> Look {
    match (behavior, maximized) {
        (Behavior::Smart, true) => Look::Hidden,
        (Behavior::Expand, true) => Look::Expanded,
        _ => Look::Normal,
    }
}

/// Bordas, margem e reserva de espaço de cada aparência.
fn placement(position: Position, look: Look) -> (Vec<Edge>, i32, Exclusive) {
    let (edge, across) = match position {
        Position::Bottom => (Edge::Bottom, [Edge::Left, Edge::Right]),
        Position::Left => (Edge::Left, [Edge::Top, Edge::Bottom]),
        Position::Right => (Edge::Right, [Edge::Top, Edge::Bottom]),
    };
    match look {
        Look::Normal => (vec![edge], 10, Exclusive::Auto),
        // A faixa escondida cobre a borda toda, sem reservar espaço: a janela maximizada vai até o fim.
        Look::Hidden => (vec![edge, across[0], across[1]], 0, Exclusive::None),
        Look::Expanded => (vec![edge, across[0], across[1]], 0, Exclusive::Auto),
    }
}

/// Recalcula todos os docks (depois de eventos de janela, troca de modo ou de configuração) e
/// reposiciona só os que mudaram. `force` reaplica todos (a configuração mudou).
pub fn update(app: &AppHandle, force: bool) {
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let (Ok(monitors), Ok(windows)) = (compositor.monitors(), compositor.windows()) else { return };
    let modes = app.state::<Modes>().snapshot(None).modes;
    let positions = app.state::<MonitorPositions>().0.lock().unwrap().clone();
    let docks = app.state::<Docks>();
    let config = docks.config.lock().unwrap().clone();
    let names: Vec<String> = monitors.iter().map(|m| m.name.clone()).collect();

    let mut changes = Vec::new();
    {
        let mut applied = docks.applied.lock().unwrap();
        for (label, pos) in positions.iter().filter(|(label, _)| label.starts_with("dock-")) {
            let index: usize = label.trim_start_matches("dock-").parse().unwrap_or(0);
            let Some(monitor) = monitors.iter().find(|m| (m.x, m.y) == *pos) else { continue };
            let desktop = modes.get(&monitor.name).copied().unwrap_or_default() == Mode::Desktop;
            let visible = desktop && shows_on(&config.monitors, index, &monitor.name, &names);
            let maximized = windows
                .iter()
                .any(|w| w.fullscreen > 0 && !w.minimized() && w.workspace == monitor.active_workspace);
            let state = (visible, look_for(config.behavior, maximized));
            if force || applied.get(label) != Some(&state) {
                applied.insert(label.clone(), state);
                changes.push((label.clone(), state));
            }
        }
    }
    if changes.is_empty() {
        return;
    }
    let handle = app.clone();
    let _ = app.run_on_main_thread(move || {
        for (label, (visible, look)) in changes {
            let (anchors, margin, exclusive) = placement(config.position, look);
            layer::place(&label, &anchors, margin, exclusive);
            layer::set_visible(&label, visible);
            let _ = handle.emit_to(label.as_str(), "lumna://dock", DockState { look, config: config.clone() });
        }
    });
}

/// Estado do dock desta janela, para o primeiro desenho (depois chega pelo evento `lumna://dock`).
#[tauri::command]
pub fn dock_state(window: WebviewWindow, docks: tauri::State<Docks>) -> DockState {
    let look = docks.applied.lock().unwrap().get(window.label()).map(|(_, look)| *look).unwrap_or_default();
    DockState { look, config: docks.config.lock().unwrap().clone() }
}

#[tauri::command]
pub fn dock_config(docks: tauri::State<Docks>) -> Config {
    docks.config.lock().unwrap().clone()
}

fn replace(app: &AppHandle, config: Config) -> Result<Config, String> {
    let config = config.clamped();
    save(&config)?;
    *app.state::<Docks>().config.lock().unwrap() = config.clone();
    let _ = app.emit("lumna://dock-config", &config);
    let handle = app.clone();
    std::thread::spawn(move || update(&handle, true));
    Ok(config)
}

#[tauri::command]
pub fn dock_config_set(app: AppHandle, config: Config) -> Result<Config, String> {
    replace(&app, config)
}

/// Volta tudo ao padrão do LUMNA.
#[tauri::command]
pub fn dock_config_reset(app: AppHandle) -> Result<Config, String> {
    replace(&app, Config::default())
}

/// "Fixar no dock" / "Desafixar" do menu do botão direito. Um app novo entra depois do último app
/// fixo (antes do separador e dos botões do LUMNA).
#[tauri::command]
pub fn dock_pin(app: AppHandle, id: String, pinned: bool) -> Result<Config, String> {
    let mut config = app.state::<Docks>().config.lock().unwrap().clone();
    config.pinned = with_pin(config.pinned, &id, pinned);
    replace(&app, config)
}

fn with_pin(mut list: Vec<String>, id: &str, pinned: bool) -> Vec<String> {
    list.retain(|item| item != id);
    if pinned {
        let at = list.iter().rposition(|item| item != SEPARATOR && !item.starts_with("lumna:")).map_or(0, |i| i + 1);
        list.insert(at, id.to_string());
    }
    list
}

/// Nomes das saídas ligadas, para escolher em qual monitor o dock aparece.
#[tauri::command]
pub fn dock_monitors(app: AppHandle) -> Vec<String> {
    app.state::<SharedCompositor>().monitors().map(|list| list.into_iter().map(|m| m.name).collect()).unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_fills_missing_fields_and_clamps() {
        let config: Config = serde_json::from_str(r#"{"size": 500, "opacity": 3, "pinned": ["firefox","|","|","foot",""]}"#).unwrap();
        let config = config.clamped();
        assert_eq!(config.size, 72);
        assert_eq!(config.opacity, 1.0);
        assert_eq!(config.pinned, ["firefox", "|", "foot"]);
        assert_eq!(config.behavior, Behavior::Smart);
        assert_eq!(config.monitors, "primary");
    }

    #[test]
    fn behaviors_with_and_without_maximized_window() {
        assert_eq!(look_for(Behavior::Always, true), Look::Normal);
        assert_eq!(look_for(Behavior::Smart, false), Look::Normal);
        assert_eq!(look_for(Behavior::Smart, true), Look::Hidden);
        assert_eq!(look_for(Behavior::Expand, true), Look::Expanded);
    }

    #[test]
    fn placement_spans_the_edge_when_hidden_or_expanded() {
        let (anchors, margin, _) = placement(Position::Bottom, Look::Normal);
        assert_eq!((anchors, margin), (vec![Edge::Bottom], 10));
        let (anchors, _, exclusive) = placement(Position::Left, Look::Hidden);
        assert_eq!(anchors, vec![Edge::Left, Edge::Top, Edge::Bottom]);
        assert!(matches!(exclusive, Exclusive::None));
        assert!(matches!(placement(Position::Right, Look::Expanded).2, Exclusive::Auto));
    }

    #[test]
    fn monitor_choice() {
        let names = vec!["DP-1".to_string(), "HDMI-A-1".to_string()];
        assert!(shows_on("primary", 0, "DP-1", &names));
        assert!(!shows_on("primary", 1, "HDMI-A-1", &names));
        assert!(shows_on("all", 1, "HDMI-A-1", &names));
        assert!(shows_on("HDMI-A-1", 1, "HDMI-A-1", &names));
        assert!(!shows_on("HDMI-A-1", 0, "DP-1", &names));
        // Monitor escolhido desligado: fica no principal.
        assert!(shows_on("DP-9", 0, "DP-1", &names));
    }

    #[test]
    fn pin_goes_after_last_app() {
        let list = Config::default().pinned;
        let pinned = with_pin(list.clone(), "gimp", true);
        let at = pinned.iter().position(|i| i == "gimp").unwrap();
        assert_eq!(pinned[at - 1], "foot");
        assert_eq!(pinned[at + 1], SEPARATOR);
        let unpinned = with_pin(pinned, "gimp", false);
        assert_eq!(unpinned, list);
        assert_eq!(with_pin(vec![LAUNCHER.into()], "foot", true), ["foot", LAUNCHER]);
    }
}
