//! Maximizar do LUMNA: a janela flutuante ocupa a área livre do monitor (menos topbar e dock),
//! mas continua uma janela comum.
//!
//! O "maximizado" do Hyprland é um estado de tela cheia: com ele, uma janela chamada por cima (o
//! Spotify pelo dock) aparecia na frente, mas os cliques iam para a maximizada embaixo.
//!
//! Maximizada, a janela fica sem borda e com cantos retos. Ela continua maximizada até a pessoa
//! arrastar ou redimensionar; quando o dock muda (esconde, aparece, expande), ela se reencaixa no
//! espaço que sobrou (antes, um dock que aparecia por um instante empurrava a janela e o LUMNA
//! deixava de achar que ela estava maximizada, então o dock ficava preso na tela).

use std::collections::HashMap;
use std::sync::Mutex;
use std::time::Duration;

use lumna_core::{Monitor, Window};
use tauri::{AppHandle, Manager};

use crate::{dock, SharedCompositor};

/// Altura da barra de título (hyprbars, `bar_height` no hyprland.lua), que fica por fora da área
/// da janela. A borda some enquanto maximizada.
const BAR: i32 = 38;
/// Classes sem barra de título (regra `lumna-own-bar` do hyprland.lua).
const NO_BAR: &[&str] = &["lumna-shell", "os.lumna.shell", "lumna-tv"];
/// Diferença tolerada na horizontal (arredondamentos): mais que isso foi a pessoa que mexeu.
const SLACK: i32 = 8;
/// Na vertical o dock aparecer ou sumir empurra a janela; isso não conta como a pessoa mexer.
const SLACK_DOCK: i32 = 200;

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

    /// Ainda no lugar em que o LUMNA pôs (o dock pode ter empurrado na vertical).
    fn near(&self, other: &Rect) -> bool {
        (self.x - other.x).abs() <= SLACK
            && (self.width - other.width).abs() <= SLACK
            && (self.y - other.y).abs() <= SLACK_DOCK
            && (self.height - other.height).abs() <= SLACK_DOCK
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

/// Área da janela maximizada: o monitor menos o espaço reservado, descontando a barra de título,
/// que fica por fora.
pub fn work_area(monitor: &Monitor, bar: bool) -> Rect {
    let [left, top, right, bottom] = monitor.reserved;
    let bar = if bar { BAR } else { 0 };
    Rect {
        x: monitor.x + left,
        y: monitor.y + top + bar,
        width: monitor.width - left - right,
        height: monitor.height - top - bottom - bar,
    }
}

/// Sem borda e com cantos retos enquanto maximizada; `unset` volta ao visual normal.
fn flat(compositor: &dyn lumna_core::Compositor, address: &str, on: bool) {
    let (border, rounding) = if on { ("0", "0") } else { ("unset", "unset") };
    let _ = compositor.set_window_prop(address, "border_size", border);
    let _ = compositor.set_window_prop(address, "rounding", rounding);
}

fn target_for(compositor: &dyn lumna_core::Compositor, window: &Window) -> Result<Rect, String> {
    let monitor = compositor
        .monitors()?
        .into_iter()
        .find(|m| m.id == window.monitor)
        .ok_or("o monitor da janela sumiu")?;
    let bar = compositor.window_bars() && !NO_BAR.contains(&window.class.as_str());
    Ok(work_area(&monitor, bar))
}

/// Esquece as que fecharam ou que a pessoa arrastou/redimensionou (essas voltam a ter borda).
pub fn prune(app: &AppHandle, windows: &[Window]) {
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let state = app.state::<Maximized>();
    let mut saved = state.0.lock().unwrap();
    let moved: Vec<String> = saved
        .iter()
        .filter(|(address, entry)| match windows.iter().find(|w| &w.address == *address) {
            None => true,
            Some(w) => entry.target.is_some_and(|t| !t.near(&Rect::of(w))),
        })
        .map(|(address, _)| address.clone())
        .collect();
    for address in moved {
        saved.remove(&address);
        if windows.iter().any(|w| w.address == address) {
            flat(compositor.as_ref(), &address, false);
        }
    }
}

/// O dock mudou: as maximizadas ocupam de novo exatamente o espaço que sobrou.
pub fn refit(app: &AppHandle) {
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let Ok(windows) = compositor.windows() else { return };
    let state = app.state::<Maximized>();
    let entries: Vec<(String, Rect)> =
        state.0.lock().unwrap().iter().filter_map(|(a, e)| e.target.map(|t| (a.clone(), t))).collect();
    for (address, old) in entries {
        let Some(window) = windows.iter().find(|w| w.address == address) else { continue };
        let Ok(target) = target_for(compositor.as_ref(), window) else { continue };
        if Rect::of(window) != target || target != old {
            if compositor.place_window(&address, target.x, target.y, target.width, target.height).is_ok() {
                if let Some(entry) = state.0.lock().unwrap().get_mut(&address) {
                    entry.target = Some(target);
                }
            }
        }
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
        flat(compositor.as_ref(), address, false);
        if let Some(r) = before {
            compositor.place_window(address, r.x, r.y, r.width, r.height)?;
        }
        dock::update(app, false);
        return Ok(());
    }

    // Primeiro o dock se ajusta (esconde, expande ou fica), depois a janela ocupa o que sobrou.
    state.0.lock().unwrap().insert(address.to_string(), Saved { before: Rect::of(&window), target: None });
    flat(compositor.as_ref(), address, true);
    dock::update(app, false);
    std::thread::sleep(Duration::from_millis(300));
    let target = target_for(compositor.as_ref(), &window)?;
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
            flat(compositor.as_ref(), address, false);
        }
    }
    drop(saved);
    dock::update(app, false);
    placed
}

/// Tamanho de quem abre pequeno demais: esta fração da área livre do monitor.
const OPEN_WIDTH: f64 = 0.65;
const OPEN_HEIGHT: f64 = 0.72;
/// Janelas do próprio LUMNA (já abrem no tamanho delas) e as de tela cheia.
const OWN_SIZE: &[&str] = &["lumna-shell", "os.lumna.shell", "lumna-tv"];

/// Onde a janela nova deve ficar: só a primeira janela de um app, no Desktop, se abriu menor que o
/// tamanho padrão. Diálogos e janelas extras do mesmo app (mesmo processo) ficam como vieram.
pub fn opening_rect(window: &Window, windows: &[Window], area: Rect) -> Option<Rect> {
    let desktop = window.workspace_name.parse::<i64>().is_ok();
    if !window.floating || window.fullscreen != 0 || !desktop || OWN_SIZE.contains(&window.class.as_str()) {
        return None;
    }
    let sibling = windows
        .iter()
        .any(|w| w.address != window.address && ((window.pid != 0 && w.pid == window.pid) || w.class == window.class));
    if sibling {
        return None;
    }
    let width = ((area.width as f64 * OPEN_WIDTH) as i32).max(window.width).min(area.width);
    let height = ((area.height as f64 * OPEN_HEIGHT) as i32).max(window.height).min(area.height);
    if width <= window.width + SLACK && height <= window.height + SLACK {
        return None;
    }
    Some(Rect { x: area.x + (area.width - width) / 2, y: area.y + (area.height - height) / 2, width, height })
}

/// Janela nova: se abriu minúscula, ganha um tamanho de janela de desktop, centralizada.
pub fn on_open(app: &AppHandle, address: &str) {
    // O evento traz o endereço sem o "0x" da lista de janelas.
    let address = format!("0x{}", address.trim_start_matches("0x"));
    let address = address.as_str();
    // O app costuma pedir o tamanho dele logo depois de aparecer: espera assentar.
    std::thread::sleep(Duration::from_millis(250));
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let Ok(windows) = compositor.windows() else { return };
    let Some(window) = windows.iter().find(|w| w.address == address) else { return };
    let Ok(area) = target_for(compositor.as_ref(), window) else { return };
    if let Some(r) = opening_rect(window, &windows, area) {
        let _ = compositor.place_window(address, r.x, r.y, r.width, r.height);
    }
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
        assert_eq!(area, Rect { x: 1920, y: 36 + 38, width: 2560, height: 1440 - 36 - 82 - 38 });
        assert_eq!(work_area(&monitor, false).y, 36);
    }

    fn window(address: &str, class: &str, pid: i64, width: i32, height: i32) -> Window {
        Window {
            address: address.into(),
            class: class.into(),
            title: String::new(),
            workspace: 1,
            workspace_name: "1".into(),
            monitor: 0,
            fullscreen: 0,
            x: 0,
            y: 0,
            width,
            height,
            floating: true,
            focus_history: 0,
            pid,
        }
    }

    #[test]
    fn tiny_first_windows_open_at_desktop_size() {
        let area = Rect { x: 0, y: 74, width: 1920, height: 924 };
        let spotify = window("0x1", "spotify", 10, 400, 300);
        let r = opening_rect(&spotify, &[spotify.clone()], area).unwrap();
        assert_eq!((r.width, r.height), (1248, 665));
        assert_eq!((r.x, r.y), ((1920 - 1248) / 2, 74 + (924 - 665) / 2));
        // Já grande: fica como está.
        let big = window("0x2", "gimp", 11, 1500, 800);
        assert!(opening_rect(&big, &[big.clone()], area).is_none());
        // Diálogo do mesmo app (mesmo processo): fica como veio.
        let dialog = window("0x3", "spotify-dialog", 10, 400, 300);
        assert!(opening_rect(&dialog, &[spotify.clone(), dialog.clone()], area).is_none());
        // Fora do Desktop (Gaming, Mídia) não mexe.
        let mut game = window("0x4", "game", 12, 400, 300);
        game.workspace_name = "gaming-DP-1".into();
        assert!(opening_rect(&game, &[game.clone()], area).is_none());
    }

    #[test]
    fn moved_windows_stop_counting_as_maximized() {
        let target = Rect { x: 0, y: 76, width: 1916, height: 960 };
        assert!(target.near(&Rect { x: 1, y: 75, width: 1916, height: 962 }));
        // O dock apareceu e empurrou a janela para cima: continua maximizada (e se reencaixa).
        assert!(target.near(&Rect { x: 0, y: -6, width: 1916, height: 960 }));
        // A pessoa arrastou para o lado ou encolheu: não está mais.
        assert!(!target.near(&Rect { x: 300, y: 200, width: 1916, height: 960 }));
        assert!(!target.near(&Rect { x: 0, y: 76, width: 1200, height: 960 }));
    }
}
