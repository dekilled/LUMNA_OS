//! Os três modos do LUMNA (Desktop, Gaming, Mídia) e como eles se espalham pelos monitores.
//!
//! Cada monitor tem um modo. Desktop usa os workspaces numerados de sempre; Gaming e Mídia usam
//! um workspace com nome próprio por monitor (`gaming-DP-1`, `media-HDMI-A-1`), então as janelas
//! de cada modo ficam onde estavam quando se volta a ele. Trocar de modo nunca fecha nada.
//!
//! Com o layout "um modo por tela", `Super + Tab` troca só o monitor em foco; com "estender modo",
//! todos os monitores trocam juntos.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Mutex;

use lumna_core::Compositor;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "lowercase")]
pub enum Mode {
    #[default]
    Desktop,
    Gaming,
    Media,
}

impl Mode {
    pub const ALL: [Mode; 3] = [Mode::Desktop, Mode::Gaming, Mode::Media];

    pub fn parse(text: &str) -> Option<Mode> {
        match text {
            "desktop" => Some(Mode::Desktop),
            "gaming" => Some(Mode::Gaming),
            "media" | "midia" | "mídia" => Some(Mode::Media),
            _ => None,
        }
    }

    fn step(self, delta: isize) -> Mode {
        let index = Mode::ALL.iter().position(|m| *m == self).unwrap_or(0) as isize;
        Mode::ALL[(index + delta).rem_euclid(Mode::ALL.len() as isize) as usize]
    }

    /// Prefixo do workspace com nome deste modo; o Desktop usa os numerados.
    fn prefix(self) -> Option<&'static str> {
        match self {
            Mode::Desktop => None,
            Mode::Gaming => Some("gaming-"),
            Mode::Media => Some("media-"),
        }
    }

    /// De qual modo é um workspace, pelo nome.
    pub fn of_workspace(name: &str) -> Mode {
        Mode::ALL
            .into_iter()
            .find(|m| m.prefix().is_some_and(|p| name.starts_with(p)))
            .unwrap_or(Mode::Desktop)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
#[serde(rename_all = "kebab-case")]
pub enum Layout {
    /// Um modo por tela: só o monitor em foco troca.
    #[default]
    PerScreen,
    /// Estender modo: todos os monitores trocam juntos.
    Extend,
}

pub enum Target {
    Next,
    Prev,
    Set(Mode),
}

#[derive(Debug, Clone, Serialize)]
pub struct Snapshot {
    /// Modo de cada monitor, pelo nome do monitor no compositor.
    pub modes: HashMap<String, Mode>,
    pub layout: Layout,
    /// Monitor que acabou de trocar (para o aviso de troca aparecer nele).
    pub focused: Option<String>,
}

#[derive(Default, Serialize, Deserialize)]
struct Settings {
    layout: Layout,
}

fn settings_path() -> Option<PathBuf> {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
    Some(base.join("lumna").join("settings.json"))
}

#[derive(Default)]
struct State {
    modes: HashMap<String, Mode>,
    /// Último workspace numerado de cada monitor, para voltar a ele no Desktop.
    desktop_workspace: HashMap<String, i64>,
    layout: Layout,
}

#[derive(Default)]
pub struct Modes(Mutex<State>);

impl Modes {
    pub fn load() -> Modes {
        let layout = settings_path()
            .and_then(|p| std::fs::read_to_string(p).ok())
            .and_then(|text| serde_json::from_str::<Settings>(&text).ok())
            .unwrap_or_default()
            .layout;
        Modes(Mutex::new(State { layout, ..State::default() }))
    }

    pub fn snapshot(&self, focused: Option<String>) -> Snapshot {
        let state = self.0.lock().unwrap();
        Snapshot { modes: state.modes.clone(), layout: state.layout, focused }
    }

    pub fn mode_of(&self, monitor: &str) -> Mode {
        self.0.lock().unwrap().modes.get(monitor).copied().unwrap_or_default()
    }

    pub fn set_layout(&self, layout: Layout) -> Result<(), String> {
        self.0.lock().unwrap().layout = layout;
        let path = settings_path().ok_or("sem pasta de configuração")?;
        if let Some(dir) = path.parent() {
            std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
        }
        let text = serde_json::to_string_pretty(&Settings { layout }).map_err(|e| e.to_string())?;
        std::fs::write(&path, text).map_err(|e| format!("{}: {e}", path.display()))
    }

    /// Troca o modo e leva os monitores aos workspaces certos. Devolve o monitor em foco.
    pub fn switch(&self, compositor: &dyn Compositor, target: Target) -> Result<String, String> {
        let monitors = compositor.monitors()?;
        let workspaces = compositor.workspaces()?;
        let focused = monitors.iter().find(|m| m.focused).or(monitors.first()).ok_or("nenhum monitor")?.clone();

        let mut state = self.0.lock().unwrap();
        let current = state.modes.get(&focused.name).copied().unwrap_or_default();
        let next = match target {
            Target::Next => current.step(1),
            Target::Prev => current.step(-1),
            Target::Set(mode) => mode,
        };
        let targets: Vec<_> = match state.layout {
            Layout::PerScreen => vec![focused.clone()],
            // O monitor em foco por último, para o foco terminar onde estava.
            Layout::Extend => {
                let mut all: Vec<_> = monitors.iter().filter(|m| m.name != focused.name).cloned().collect();
                all.push(focused.clone());
                all
            }
        };

        for monitor in &targets {
            let from = state.modes.get(&monitor.name).copied().unwrap_or_default();
            if from == Mode::Desktop && monitor.active_workspace > 0 {
                state.desktop_workspace.insert(monitor.name.clone(), monitor.active_workspace);
            }
            if targets.len() > 1 {
                compositor.focus_monitor(&monitor.name)?;
            }
            match next.prefix() {
                Some(prefix) => compositor.focus_named_workspace(&format!("{prefix}{}", monitor.name))?,
                None => {
                    // Volta ao último workspace numerado deste monitor; se ele não existe mais
                    // (ficou vazio e o Hyprland apagou), usa um livre que não esteja em outro monitor.
                    let remembered = state.desktop_workspace.get(&monitor.name).copied();
                    let taken_elsewhere =
                        |id: i64| workspaces.iter().any(|w| w.id == id && w.monitor != monitor.name);
                    let id = remembered
                        .filter(|id| !taken_elsewhere(*id))
                        .or_else(|| (1..=9).find(|id| !taken_elsewhere(*id)))
                        .unwrap_or(1);
                    compositor.focus_workspace(id)?;
                }
            }
            state.modes.insert(monitor.name.clone(), next);
        }
        Ok(focused.name)
    }

    /// O usuário foi para um workspace por fora (atalho `Super + 1..9`, clique na topbar): o modo
    /// daquele monitor passa a ser o do workspace. Devolve `true` se algo mudou.
    pub fn follow_workspace(&self, monitor: &str, workspace_name: &str) -> bool {
        let mode = Mode::of_workspace(workspace_name);
        let mut state = self.0.lock().unwrap();
        let previous = state.modes.insert(monitor.to_string(), mode).unwrap_or_default();
        previous != mode
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cycles_through_the_three_modes() {
        assert_eq!(Mode::Desktop.step(1), Mode::Gaming);
        assert_eq!(Mode::Media.step(1), Mode::Desktop);
        assert_eq!(Mode::Desktop.step(-1), Mode::Media);
    }

    #[test]
    fn workspace_names_tell_the_mode() {
        assert_eq!(Mode::of_workspace("gaming-DP-1"), Mode::Gaming);
        assert_eq!(Mode::of_workspace("media-HDMI-A-1"), Mode::Media);
        assert_eq!(Mode::of_workspace("3"), Mode::Desktop);
    }

    #[test]
    fn layout_is_saved_in_kebab_case() {
        let text = serde_json::to_string(&Settings { layout: Layout::PerScreen }).unwrap();
        assert_eq!(text, r#"{"layout":"per-screen"}"#);
    }
}
