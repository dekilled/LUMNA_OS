//! Núcleo do LUMNA OS: o que o shell sabe sobre o compositor, sem saber qual compositor é.
//!
//! O shell só conversa com `dyn Compositor`. O Hyprland é a primeira implementação
//! (`lumna-hypr`); trocar de compositor no futuro é escrever outra.

use serde::Serialize;

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct Workspace {
    pub id: i64,
    pub name: String,
    /// Nome da saída de vídeo (ex.: `DP-1`) onde o workspace está.
    pub monitor: String,
    pub windows: u32,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct Monitor {
    pub id: i64,
    pub name: String,
    pub focused: bool,
    pub active_workspace: i64,
}

/// Eventos que o shell precisa acompanhar. Chegam em tempo real pelo compositor.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum CompositorEvent {
    /// O workspace em foco mudou.
    WorkspaceFocused { id: i64 },
    /// Workspaces foram criados, destruídos, movidos ou ganharam/perderam janelas.
    WorkspacesChanged,
    /// O monitor em foco mudou.
    MonitorFocused { name: String },
    /// Monitor ligado ou desligado.
    MonitorsChanged,
    WindowOpened { address: String, class: String, title: String },
    WindowClosed { address: String },
}

pub type Result<T> = std::result::Result<T, String>;

pub trait Compositor: Send + Sync {
    fn workspaces(&self) -> Result<Vec<Workspace>>;
    fn monitors(&self) -> Result<Vec<Monitor>>;
    fn focus_workspace(&self, id: i64) -> Result<()>;
    /// Bloqueia a thread chamando `on_event` a cada evento até a conexão cair.
    fn listen(&self, on_event: &mut dyn FnMut(CompositorEvent)) -> Result<()>;
}
