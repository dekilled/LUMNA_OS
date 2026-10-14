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
    pub active_workspace_name: String,
    /// Posição no layout (coordenadas lógicas), usada para achar o mesmo monitor no GTK.
    pub x: i32,
    pub y: i32,
}

/// Janela de um app, como o compositor a enxerga.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct Window {
    pub address: String,
    pub class: String,
    pub title: String,
    pub workspace: i64,
    /// Nome do workspace (`special:minimized` = minimizada pelo LUMNA).
    pub workspace_name: String,
    /// Id do monitor onde a janela está.
    pub monitor: i64,
}

/// Workspace escondido onde ficam as janelas minimizadas (o Hyprland não tem "minimizar").
pub const MINIMIZED: &str = "special:minimized";

impl Window {
    pub fn minimized(&self) -> bool {
        self.workspace_name == MINIMIZED
    }
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
    /// A janela em foco mudou (ou o título dela).
    ActiveWindowChanged,
}

pub type Result<T> = std::result::Result<T, String>;

/// Socket em que o shell recebe ordens do `lumna-ctl` (uma linha por conexão, resposta até o EOF).
pub fn control_socket_path() -> Option<std::path::PathBuf> {
    std::env::var_os("XDG_RUNTIME_DIR").map(|dir| std::path::PathBuf::from(dir).join("lumna").join("shell.sock"))
}

pub trait Compositor: Send + Sync {
    fn workspaces(&self) -> Result<Vec<Workspace>>;
    fn monitors(&self) -> Result<Vec<Monitor>>;
    fn windows(&self) -> Result<Vec<Window>>;
    /// Janela com o foco do teclado, se houver.
    fn active_window(&self) -> Result<Option<Window>>;
    fn focus_workspace(&self, id: i64) -> Result<()>;
    /// Vai para um workspace com nome (ex.: `gaming-DP-1`), criando-o no monitor em foco.
    fn focus_named_workspace(&self, name: &str) -> Result<()>;
    fn focus_monitor(&self, name: &str) -> Result<()>;
    /// Fecha a janela em foco (pede para o app fechar, como `Super + Q`).
    fn close_active_window(&self) -> Result<()>;
    /// Traz a janela para a frente (vai para o workspace dela).
    fn focus_window(&self, address: &str) -> Result<()>;
    /// Pede para a janela fechar.
    fn close_window(&self, address: &str) -> Result<()>;
    /// Leva a janela para um workspace (`3`, `name:gaming-DP-1`...).
    fn move_window(&self, address: &str, workspace: &str) -> Result<()>;
    /// Liga ou desliga a tela cheia da janela.
    fn fullscreen_window(&self, address: &str) -> Result<()>;
    /// Liga ou desliga o "maximizado" (ocupa a área toda, sem cobrir a topbar).
    fn maximize_window(&self, address: &str) -> Result<()>;
    /// Roda um comando como filho do compositor (as janelas abrem no workspace atual).
    fn exec(&self, command: &str) -> Result<()>;
    /// Encerra o compositor (a tela de login sai assim para o greetd abrir a sessão).
    fn exit(&self) -> Result<()>;
    /// Bloqueia a thread chamando `on_event` a cada evento até a conexão cair.
    fn listen(&self, on_event: &mut dyn FnMut(CompositorEvent)) -> Result<()>;
}
