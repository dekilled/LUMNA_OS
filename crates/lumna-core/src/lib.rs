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

#[derive(Debug, Clone, Default, Serialize, PartialEq)]
pub struct Monitor {
    pub id: i64,
    pub name: String,
    pub focused: bool,
    pub active_workspace: i64,
    pub active_workspace_name: String,
    /// Workspace especial aberto por cima (0 = nenhum).
    pub special_workspace: i64,
    /// Posição no layout (coordenadas lógicas), usada para achar o mesmo monitor no GTK.
    pub x: i32,
    pub y: i32,
    /// Tamanho no layout (já dividido pela escala e girado), que é o que conta para encostar uma
    /// tela na outra.
    pub width: i32,
    pub height: i32,
    pub scale: f64,
    /// Fabricante e modelo, para mostrar à pessoa (ex.: "Samsung Odyssey G5").
    pub label: String,
    /// Espaço reservado nas bordas (esquerda, cima, direita, baixo) por topbar e dock.
    pub reserved: [i32; 4],
}

/// Teclado principal: os layouts configurados (`br`, `us`...) e qual está em uso.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct KeyboardInfo {
    pub layouts: Vec<String>,
    pub active: usize,
}

/// Janela de um app, como o compositor a enxerga.
#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct Window {
    pub address: String,
    pub class: String,
    pub title: String,
    pub workspace: i64,
    /// Nome do workspace (`special:minimized-*` = minimizada pelo LUMNA).
    pub workspace_name: String,
    /// Id do monitor onde a janela está.
    pub monitor: i64,
    /// 0 = normal, 1 = maximizada, 2 = tela cheia.
    pub fullscreen: i64,
    /// Posição e tamanho no layout (sem a barra de título e a borda).
    pub x: i32,
    pub y: i32,
    pub width: i32,
    pub height: i32,
    pub floating: bool,
    /// Ordem do último foco (0 = a mais recente).
    pub focus_history: i64,
    /// Processo dono da janela (0 = desconhecido).
    pub pid: i64,
}

/// Workspace escondido onde ficam as janelas minimizadas (o Hyprland não tem "minimizar").
pub const MINIMIZED: &str = "special:minimized";

/// Cada janela minimizada vai pro seu próprio workspace escondido. Num só, o Hyprland dividia a
/// área entre elas (com dois apps, cada um ficava com metade) e a janela, ao voltar, era esticada
/// da metade pra tela toda: o "flick" ao trocar de app no celular.
pub fn minimized_workspace(address: &str) -> String {
    format!("{MINIMIZED}-{}", address.trim_start_matches("0x"))
}

impl Window {
    pub fn minimized(&self) -> bool {
        self.workspace_name == MINIMIZED || self.workspace_name.starts_with(&format!("{MINIMIZED}-"))
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
    /// Uma janela entrou ou saiu de maximizada / tela cheia.
    FullscreenChanged,
    /// O layout do teclado em uso mudou (nome longo do xkb, ex.: "English (US)").
    KeyboardLayout { name: String },
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
    /// Põe uma janela flutuante nesta posição e tamanho (coordenadas do layout).
    fn place_window(&self, address: &str, x: i32, y: i32, width: i32, height: i32) -> Result<()>;
    /// Roda um comando como filho do compositor (as janelas abrem no workspace atual).
    fn exec(&self, command: &str) -> Result<()>;
    /// Põe o monitor na posição (x, y) do layout, mantendo a resolução preferida e a escala.
    fn place_monitor(&self, name: &str, x: i32, y: i32, scale: f64) -> Result<()>;
    /// Layouts do teclado principal e o que está em uso.
    fn keyboard(&self) -> Result<KeyboardInfo> {
        Err("não suportado".into())
    }
    /// Passa para o próximo layout do teclado.
    fn next_keyboard_layout(&self) -> Result<()> {
        Err("não suportado".into())
    }
    /// Troca o teclado na hora (nomes do xkb, separados por vírgula quando há mais de um).
    fn set_keyboard(&self, layout: &str, variant: &str, options: &str) -> Result<()>;
    /// As janelas têm barra de título desenhada pelo compositor (no Hyprland, o plugin hyprbars)?
    /// Sem ela, a topbar mostra o título e os botões da janela em foco.
    fn window_bars(&self) -> bool {
        false
    }
    /// Encerra o compositor (a tela de login sai assim para o greetd abrir a sessão).
    fn exit(&self) -> Result<()>;
    /// Bloqueia a thread chamando `on_event` a cada evento até a conexão cair.
    fn listen(&self, on_event: &mut dyn FnMut(CompositorEvent)) -> Result<()>;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cada_janela_minimizada_no_seu_workspace() {
        assert_eq!(minimized_workspace("0x55aa01"), "special:minimized-55aa01");
        let mut w = Window {
            address: "0x55aa01".into(),
            class: String::new(),
            title: String::new(),
            workspace: -98,
            workspace_name: minimized_workspace("0x55aa01"),
            monitor: 0,
            fullscreen: 0,
            x: 0,
            y: 0,
            width: 0,
            height: 0,
            floating: false,
            focus_history: 0,
            pid: 0,
        };
        assert!(w.minimized());
        w.workspace_name = MINIMIZED.into();
        assert!(w.minimized());
        w.workspace_name = "special:media".into();
        assert!(!w.minimized());
    }
}
