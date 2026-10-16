//! Maximizar do LUMNA: a janela flutuante ocupa a área livre do monitor (menos topbar e dock),
//! mas continua uma janela comum.
//!
//! O "maximizado" do Hyprland é um estado de tela cheia: com ele, uma janela chamada por cima (o
//! Spotify pelo dock) aparecia na frente, mas os cliques iam para a maximizada embaixo.

use std::collections::HashMap;
use std::sync::Mutex;
use std::time::Duration;

use lumna_core::{Monitor, Window};
use tauri::{AppHandle, Manager};

use crate::{dock, SharedCompositor};

/// Altura da barra de título (hyprbars, `bar_height` no hyprland.lua) e borda das janelas
/// (`border_size`). As duas ficam por fora da área da janela.
const BAR: i32 = 38;
const BORDER: i32 = 2;
/// Classes sem barra de título (regra `lumna-own-bar` do hyprland.lua).
const NO_BAR: &[&str] = &["lumna-shell", "os.lumna.shell", "lumna-tv"];
/// Diferença tolerada para a janela ainda contar como maximizada (arredondamentos).
const SLACK: i32 = 4;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
}

impl Rect {
    fn of(window: &Window) -> Rect {
        Rect { x: window.x, y: window.y, width: window.width, height: window.height }
    }

    fn near(&self, other: &Rect) -> bool {
        (self.x - other.x).abs() <= SLACK
            && (self.y - other.y).abs() <= SLACK
            && (self.width - other.width).abs() <= SLACK
            && (self.height - other.height).abs() <= SLACK
    }
}

struct Saved {
    /// Onde estava antes (para restaurar).
    before: Rect,
    /// Onde foi posta; `None` enquanto o dock se ajusta.
    target: Option<Rect>,
}

#[derive(Default)]
pub struct Maximized(Mutex<HashMap<String, Saved>>);

impl Maximized {
    /// Maximizada pelo LUMNA e ainda no lugar (arrastada ou redimensionada, deixa de contar).
    pub fn is_maximized(&self, window: &Window) -> bool {
        match self.0.lock().unwrap().get(&window.address) {
            Some(Saved { target: Some(target), .. }) => target.near(&Rect::of(window)),
            Some(Saved { target: None, .. }) => true,
            None => false,
        }
    }
}

/// Área da janela maximizada: o monitor menos o espaço reservado, descontando a barra de título e
/// a borda, que ficam por fora.
pub fn work_area(monitor: &Monitor, bar: bool) -> Rect {
    let [left, top, right, bottom] = monitor.reserved;
    let bar = if bar { BAR } else { 0 };
    Rect {
        x: monitor.x + left + BORDER,
        y: monitor.y + top + BORDER + bar,
        width: monitor.width - left - right - 2 * BORDER,
        height: monitor.height - top - bottom - 2 * BORDER - bar,
    }
}

/// Maximiza ou restaura. Janelas lado a lado (não flutuantes) usam o maximizar do Hyprland.
pub fn toggle(app: &AppHandle, address: &str) -> Result<(), String> {
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let windows = compositor.windows()?;
    let window = windows.iter().find(|w| w.address == address).ok_or("a janela não existe mais")?.clone();
    let state = app.state::<Maximized>();
    state.0.lock().unwrap().retain(|a, _| windows.iter().any(|w| &w.address == a));
    if !window.floating || window.fullscreen == 1 {
        return compositor.maximize_window(address);
    }

    if state.is_maximized(&window) {
        let before = state.0.lock().unwrap().remove(address).map(|s| s.before);
        if let Some(r) = before {
            compositor.place_window(address, r.x, r.y, r.width, r.height)?;
        }
        dock::update(app, false);
        return Ok(());
    }

    // Primeiro o dock se ajusta (esconde, expande ou fica), depois a janela ocupa o que sobrou.
    state.0.lock().unwrap().insert(address.to_string(), Saved { before: Rect::of(&window), target: None });
    dock::update(app, false);
    std::thread::sleep(Duration::from_millis(300));
    let monitor = compositor
        .monitors()?
        .into_iter()
        .find(|m| m.id == window.monitor)
        .ok_or("o monitor da janela sumiu")?;
    let bar = compositor.window_bars() && !NO_BAR.contains(&window.class.as_str());
    let target = work_area(&monitor, bar);
    let placed = compositor.place_window(address, target.x, target.y, target.width, target.height);
    let mut saved = state.0.lock().unwrap();
    match placed {
        Ok(()) => {
            if let Some(entry) = saved.get_mut(address) {
                entry.target = Some(target);
            }
        }
        Err(_) => {
            saved.remove(address);
        }
    }
    drop(saved);
    dock::update(app, false);
    placed
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn work_area_leaves_room_for_topbar_dock_and_title_bar() {
        let monitor = Monitor {
            id: 0,
            name: "DP-1".into(),
            focused: true,
            active_workspace: 1,
            active_workspace_name: "1".into(),
            special_workspace: 0,
            x: 1920,
            y: 0,
            width: 2560,
            height: 1440,
            scale: 1.0,
            label: String::new(),
            reserved: [0, 36, 0, 82],
        };
        let area = work_area(&monitor, true);
        assert_eq!(area, Rect { x: 1922, y: 36 + 2 + 38, width: 2556, height: 1440 - 36 - 82 - 4 - 38 });
        assert_eq!(work_area(&monitor, false).y, 38);
    }

    #[test]
    fn moved_windows_stop_counting_as_maximized() {
        let target = Rect { x: 0, y: 76, width: 1916, height: 960 };
        assert!(target.near(&Rect { x: 1, y: 75, width: 1916, height: 962 }));
        assert!(!target.near(&Rect { x: 300, y: 200, width: 1916, height: 960 }));
    }
}
