mod actions;
mod animations;
mod apps;
mod automation;
mod audio;
mod bluetooth;
mod connect;
mod control;
mod displays;
mod dock;
mod emma;
mod files;
mod games;
mod greeter;
mod haptics;
mod installer;
mod keyboard;
mod touchkeys;
mod layer;
mod link;
mod lock;
mod media;
mod mobile;
mod mobilelock;
mod modes;
mod network;
mod nmwifi;
mod notifications;
mod pad;
mod phone;
mod polkit;
mod power;
mod printers;
mod remote;
mod rest;
mod schedule;
mod status;
mod store;
mod system_info;
mod tray;
mod tv;
mod vdf;
mod wallpaper;
mod wifi;
mod winmax;

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use gtk::prelude::MonitorExt;
use gtk_layer_shell::{Edge, KeyboardMode, Layer};
use layer::{Exclusive, LayerSpec};
use lumna_core::{Compositor, CompositorEvent, Monitor, Window, Workspace};
use modes::{Layout, Mode, Modes, Target};
use lumna_hypr::Hyprland;
use tauri::{AppHandle, Emitter, Manager, WebviewUrl, WebviewWindow, WebviewWindowBuilder};

type SharedCompositor = Arc<dyn Compositor>;

/// Terminal usado para apps de linha de comando (`Terminal=true` no `.desktop`).
const TERMINAL: &str = "foot";

#[derive(Default)]
struct AppsCache(Mutex<Vec<apps::App>>);

#[derive(Default)]
struct GamesCache(Mutex<Vec<games::Game>>);

#[derive(Default)]
struct VideosCache(Mutex<Vec<media::Video>>);

/// Posição lógica (x, y) do monitor GTK de cada janela por monitor (`topbar-0`, `mode-0`...),
/// para achar o mesmo monitor na lista do compositor.
#[derive(Default)]
struct MonitorPositions(Mutex<HashMap<String, (i32, i32)>>);

const TOPBAR: LayerSpec = LayerSpec {
    namespace: "lumna-topbar",
    layer: Layer::Top,
    anchors: &[Edge::Top, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::Auto,
    size: (-1, 36),
    visible: true,
};

const DOCK: LayerSpec = LayerSpec {
    namespace: "lumna-dock",
    layer: Layer::Top,
    anchors: &[Edge::Bottom],
    margins: &[(Edge::Bottom, 10)],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::Auto,
    size: (320, 72),
    // Quem mostra (e onde) é o `dock::update`, conforme as configurações.
    visible: false,
};

const LAUNCHER: LayerSpec = LayerSpec {
    namespace: "lumna-launcher",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::Exclusive,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: false,
};

/// Tela cheia de Gaming ou Mídia, atrás das janelas e abaixo da topbar.
const MODE_SCREEN: LayerSpec = LayerSpec {
    namespace: "lumna-mode",
    layer: Layer::Bottom,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    // Recebe o teclado quando clicada (setas e Enter navegam como o controle).
    keyboard: KeyboardMode::OnDemand,
    exclusive: Exclusive::None,
    size: (-1, -1),
    visible: false,
};

/// "Alternar modo": aparece no centro da tela a cada troca. Ignora o espaço da topbar e do dock,
/// senão o centro muda de lugar entre os modos (o dock só existe no Desktop).
const SWITCHER: LayerSpec = LayerSpec {
    namespace: "lumna-switcher",
    layer: Layer::Overlay,
    anchors: &[],
    margins: &[],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::Ignore,
    size: (600, 220),
    visible: false,
};

/// Fundo do Desktop (um por monitor): o papel de parede e o menu do botão direito na área vazia.
/// Fica na camada de fundo, embaixo das janelas; o swaybg continua por baixo como reserva.
/// Tela de descanso (uma por monitor): por cima de tudo, recebe o teclado para a primeira tecla só
/// acordar (não ir parar no app).
const REST: LayerSpec = LayerSpec {
    namespace: "lumna-rest",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::Exclusive,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: false,
};

const DESKTOP: LayerSpec = LayerSpec {
    namespace: "lumna-desktop",
    layer: Layer::Background,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: true,
};

/// Caminho do papel de parede em uso, liberado para o `<img>` da camada de fundo.
#[tauri::command]
fn desktop_wallpaper(app: AppHandle) -> Option<String> {
    let path = wallpaper::current();
    if !std::path::Path::new(&path).exists() {
        return None;
    }
    let _ = app.asset_protocol_scope().allow_file(&path);
    Some(path)
}

/// Painel da Emma: a tela toda (para fechar com um clique fora), com o teclado enquanto aberto.
const EMMA: LayerSpec = LayerSpec {
    namespace: "lumna-emma",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::Exclusive,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: false,
};

/// Menu do Wi-Fi: a tela toda (abaixo da topbar) para fechar com um clique fora do painel.
/// Recebe o teclado quando clicado, para digitar a senha.
const WIFI: LayerSpec = LayerSpec {
    namespace: "lumna-wifi",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::OnDemand,
    exclusive: Exclusive::None,
    size: (-1, -1),
    visible: false,
};

/// Pedido de senha de administrador (polkit): cobre a tela e prende o teclado até responder.
/// Menu de um ícone da bandeja: camada transparente na tela toda (clique fora fecha), o menu embaixo
/// do ícone.
const TRAY_MENU: LayerSpec = LayerSpec {
    namespace: "lumna-traymenu",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::OnDemand,
    exclusive: Exclusive::None,
    size: (-1, -1),
    visible: false,
};

/// Menu de energia: véu na tela toda com os botões no centro (Esc ou clique fora fecha).
const POWER: LayerSpec = LayerSpec {
    namespace: "lumna-power",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::Exclusive,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: false,
};

const AUTH: LayerSpec = LayerSpec {
    namespace: "lumna-auth",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::Exclusive,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: false,
};

/// Rodando no celular (`--mobile`): as janelas dos apps não têm tamanho mínimo de desktop.
static MOBILE: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

/// Tamanho mínimo das janelas do LUMNA. No celular a tela tem ~432 px lógicos de largura: um mínimo
/// de desktop deixa a janela maior que o espaço dela, e o toque cai fora do lugar.
/// Tamanho inicial das janelas do LUMNA. No celular elas já nascem do tamanho útil da tela (entre a
/// barra de status e a faixa da barrinha): se nascessem no tamanho de PC, o WebKit desenhava a
/// tela errada por um instante até o Hyprland maximizar (o "flash" ao abrir um app).
fn app_size(app: &AppHandle, desktop: (f64, f64), full: bool) -> (f64, f64) {
    if !MOBILE.load(std::sync::atomic::Ordering::Relaxed) {
        return desktop;
    }
    let monitor = app.state::<SharedCompositor>().monitors().ok().and_then(|m| m.into_iter().find(|m| m.focused));
    match monitor {
        Some(m) if full => (m.width as f64, m.height as f64),
        // 36 da barra de status, 26 da faixa da barrinha (o dock some com o app aberto)
        Some(m) => (m.width as f64, (m.height - 36 - 26).max(200) as f64),
        None => desktop,
    }
}

fn min_side(desktop: f64) -> f64 {
    if MOBILE.load(std::sync::atomic::Ordering::Relaxed) { 200.0 } else { desktop }
}

/// Celular (`lumna-shell --mobile`): barra de status no topo, por cima do notch.
const MOBILE_STATUS: LayerSpec = LayerSpec {
    namespace: "lumna-mobile-status",
    layer: Layer::Top,
    anchors: &[Edge::Top, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::Auto,
    size: (-1, 36),
    visible: true,
};

/// Celular: a tela Início (relógio, Emma, grade de apps), embaixo das janelas dos apps.
const MOBILE_HOME: LayerSpec = LayerSpec {
    namespace: "lumna-mobile-home",
    layer: Layer::Bottom,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::OnDemand,
    // a tela toda, por trás da barra de status e da navegação (o eclipse passa por baixo delas)
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: true,
};

/// Celular: a barra de navegação de baixo (Início, Apps, Arquivos, Ajustes, Emma), sempre visível.
const MOBILE_NAV: LayerSpec = LayerSpec {
    namespace: "lumna-mobile-nav",
    layer: Layer::Top,
    anchors: &[Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::None,
    // só a faixa da barrinha (26 px) é reservada, sempre: o dock (Início) desenha por cima de
    // uma tela sem apps, e os apps nunca são redimensionados quando ele aparece ou some
    exclusive: Exclusive::Fixed(26),
    size: (-1, 92),
    visible: true,
};

/// Celular: o painel da Emma pega o teclado só quando precisa. Com o teclado "exclusivo" (o do PC)
/// o Hyprland entrega a ela até os toques que caem no LUMNA Keyboard, e o véu fecha o painel.
/// Ela também respeita o espaço da barra de status, da navegação e do teclado (a folha encolhe
/// quando o teclado aparece, em vez de ficar por baixo dele).
const MOBILE_EMMA: LayerSpec = LayerSpec { keyboard: KeyboardMode::OnDemand, exclusive: Exclusive::None, ..EMMA };

/// Celular: as faixas finas nas bordas laterais, só com um app aberto. Deslizar delas na
/// diagonal (pra dentro e pra baixo) volta pra tela Início.
const MOBILE_EDGE_LEFT: LayerSpec = LayerSpec {
    namespace: "lumna-mobile-edge",
    layer: Layer::Top,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left],
    margins: &[],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::None,
    size: (14, -1),
    visible: false,
};
const MOBILE_EDGE_RIGHT: LayerSpec = LayerSpec { anchors: &[Edge::Top, Edge::Bottom, Edge::Right], ..MOBILE_EDGE_LEFT };

/// Celular: a central (notificações e atalhos), por cima de tudo menos do teclado. Abre deslizando
/// a barra de status pra baixo.
const MOBILE_CENTER: LayerSpec = LayerSpec {
    namespace: "lumna-mobile-center",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: false,
};

/// Celular: a tela da ligação (tocando, chamando, em conversa), por cima de tudo.
const MOBILE_CALL: LayerSpec = LayerSpec { namespace: "lumna-mobile-call", ..MOBILE_CENTER };

/// Celular: pedidos que chegam do PC pelo Connect (senha, confirmação da Emma). Pega o teclado
/// pra digitar a senha; fica abaixo do bloqueio (só responde quem desbloqueou o celular).
const MOBILE_ASK: LayerSpec = LayerSpec { namespace: "lumna-mobile-ask", keyboard: KeyboardMode::OnDemand, ..MOBILE_CENTER };

/// Celular: a tela de bloqueio. Pega o teclado quando o campo de senha pede (o LUMNA Keyboard
/// digita nela); fica abaixo da ligação, que aparece mesmo bloqueado.
const MOBILE_LOCK: LayerSpec = LayerSpec { namespace: "lumna-mobile-lock", keyboard: KeyboardMode::OnDemand, ..MOBILE_CENTER };

/// Celular: o menu de energia (segurar o botão power).
const MOBILE_POWER: LayerSpec = LayerSpec { namespace: "lumna-mobile-power", ..MOBILE_CENTER };

/// Celular: os apps recentes (o "RAM"). Abre deslizando a navegação pra cima.
const MOBILE_RECENTS: LayerSpec = LayerSpec { namespace: "lumna-mobile-recents", ..MOBILE_CENTER };

/// Celular: avisos no topo, na largura da tela, logo abaixo da barra de status.
const MOBILE_NOTIFICATIONS: LayerSpec = LayerSpec {
    namespace: "lumna-notifications",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Left, Edge::Right],
    margins: &[(Edge::Top, 40), (Edge::Left, 8), (Edge::Right, 8)],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::None,
    size: (-1, 10),
    visible: false,
};

/// Celular: o LUMNA Keyboard, no rodapé e por cima de tudo (até do painel da Emma). Nunca pega o
/// foco do teclado, para as teclas irem ao app em foco; reserva o espaço dele (os apps sobem).
const KEYBOARD: LayerSpec = LayerSpec {
    namespace: "lumna-keyboard",
    layer: Layer::Overlay,
    anchors: &[Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::Auto,
    size: (-1, 392),
    visible: false,
};

/// Tela de login (`lumna-shell --greeter`): a tela toda, com o teclado.
const GREETER: LayerSpec = LayerSpec {
    namespace: "lumna-greeter",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::Exclusive,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: true,
};

/// Barra de serviços da Mídia: por cima do streaming, na tela toda (clique fora volta ao vídeo),
/// com o teclado enquanto aberta.
const TVBAR: LayerSpec = LayerSpec {
    namespace: "lumna-tvbar",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Bottom, Edge::Left, Edge::Right],
    margins: &[],
    keyboard: KeyboardMode::Exclusive,
    exclusive: Exclusive::Ignore,
    size: (-1, -1),
    visible: false,
};

const NOTIFICATIONS: LayerSpec = LayerSpec {
    namespace: "lumna-notifications",
    layer: Layer::Overlay,
    anchors: &[Edge::Top, Edge::Right],
    margins: &[(Edge::Top, 8), (Edge::Right, 12)],
    keyboard: KeyboardMode::None,
    exclusive: Exclusive::None,
    size: (380, 10),
    visible: false,
};

// ---- compositor ----

#[tauri::command]
fn workspaces(compositor: tauri::State<'_, SharedCompositor>) -> Result<Vec<Workspace>, String> {
    compositor.workspaces()
}

#[tauri::command]
fn monitors(compositor: tauri::State<'_, SharedCompositor>) -> Result<Vec<Monitor>, String> {
    compositor.monitors()
}

#[tauri::command]
fn windows(compositor: tauri::State<'_, SharedCompositor>) -> Result<Vec<Window>, String> {
    compositor.windows()
}

/// Janela em foco (para a barra da janela na topbar).
#[tauri::command]
fn active_window(compositor: tauri::State<'_, SharedCompositor>) -> Result<Option<Window>, String> {
    compositor.active_window()
}

/// Minimizar, maximizar, fechar ou trazer para a frente uma janela (a em foco, se não disser qual).
/// O Hyprland não tem "minimizar": a janela vai para um workspace escondido e volta pelo dock.
#[tauri::command]
fn window_control(app: AppHandle, address: Option<String>, action: String) -> Result<(), String> {
    let compositor = app.state::<SharedCompositor>();
    let address = match address {
        Some(address) => address,
        None => compositor.active_window()?.map(|w| w.address).ok_or("nenhuma janela em foco")?,
    };
    match action.as_str() {
        "close" => compositor.close_window(&address),
        "minimize" => compositor.move_window(&address, &lumna_core::minimized_workspace(&address)),
        // Leva uns instantes (o dock se ajusta antes); fora da thread principal.
        "maximize" => {
            let handle = app.clone();
            std::thread::spawn(move || {
                if let Err(err) = winmax::toggle(&handle, &address) {
                    eprintln!("lumna-shell: maximizar: {err}");
                }
            });
            Ok(())
        }
        "fullscreen" => compositor.fullscreen_window(&address),
        "focus" => {
            // Minimizada: volta para a área de trabalho em uso no monitor em foco.
            let minimized = compositor.windows()?.iter().any(|w| w.address == address && w.minimized());
            if minimized {
                let target = compositor.monitors()?.into_iter().find(|m| m.focused).map(|m| m.active_workspace).unwrap_or(1);
                compositor.move_window(&address, &target.to_string())?;
            }
            compositor.focus_window(&address)
        }
        _ => Err(format!("ação de janela desconhecida: {action}")),
    }
}

#[tauri::command]
fn window_bars(compositor: tauri::State<'_, SharedCompositor>) -> bool {
    compositor.window_bars()
}

#[tauri::command]
fn focus_workspace(compositor: tauri::State<'_, SharedCompositor>, id: i64) -> Result<(), String> {
    compositor.focus_workspace(id)
}

// ---- apps ----

/// Relê os `.desktop` (apps instalados ou removidos aparecem sem reiniciar o shell).
#[tauri::command]
fn apps(cache: tauri::State<'_, AppsCache>) -> Vec<apps::App> {
    let list = apps::list();
    *cache.0.lock().unwrap() = list.clone();
    list
}

#[tauri::command]
fn launch_app(
    app: AppHandle,
    cache: tauri::State<'_, AppsCache>,
    compositor: tauri::State<'_, SharedCompositor>,
    id: String,
) -> Result<(), String> {
    let command = {
        let cache = cache.0.lock().unwrap();
        let entry = cache.iter().find(|a| a.id == id).ok_or_else(|| format!("app {id} não encontrado"))?;
        entry.command(TERMINAL)
    };
    compositor.exec(&command)?;
    layer::set_visible("launcher", false);
    let _ = app.emit_to("launcher", "lumna://launcher-closed", ());
    Ok(())
}

// ---- status ----

#[tauri::command]
fn status(app: AppHandle, state: tauri::State<'_, status::StatusState>) -> status::Status {
    status::refresh(&app);
    state.0.lock().unwrap().clone()
}

#[tauri::command]
fn toggle_mute(app: AppHandle) -> Result<(), String> {
    status::wpctl(&["set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"])?;
    status::refresh(&app);
    Ok(())
}

#[tauri::command]
fn change_volume(app: AppHandle, step: i32) -> Result<(), String> {
    let change = format!("{}%{}", step.abs(), if step >= 0 { "+" } else { "-" });
    status::wpctl(&["set-volume", "-l", "1", "@DEFAULT_AUDIO_SINK@", &change])?;
    status::refresh(&app);
    Ok(())
}

// ---- camadas ----

#[tauri::command]
fn layer_resize(window: WebviewWindow, width: i32, height: i32) {
    layer::resize(window.label(), width, height);
}

#[tauri::command]
fn layer_hide(app: AppHandle, window: WebviewWindow) {
    layer::set_visible(window.label(), false);
    if window.label() == "launcher" {
        let _ = app.emit_to("launcher", "lumna://launcher-closed", ());
    }
}

// ---- notificações ----

#[tauri::command]
fn notification_closed(id: u32, reason: u32) {
    notifications::closed(id, reason);
}

#[tauri::command]
fn notification_action(id: u32, key: String) {
    notifications::action(id, key);
    notifications::closed(id, notifications::CLOSED_DISMISSED);
}

/// "Não perturbe" vale enquanto o monitor em foco estiver em Gaming ou Mídia.
pub fn do_not_disturb(app: &AppHandle) -> bool {
    if mobile::quiet() {
        return true;
    }
    let focused = app.state::<SharedCompositor>().monitors().ok().and_then(|m| m.into_iter().find(|m| m.focused));
    focused.is_some_and(|m| app.state::<Modes>().mode_of(&m.name) != Mode::Desktop)
}

pub fn show_notifications(app: &AppHandle) {
    let _ = app.run_on_main_thread(|| layer::set_visible("notifications", true));
}

// ---- launcher ----

pub enum LauncherAction {
    Toggle,
    Open,
    Close,
}

/// Abre o launcher no monitor em foco, ou fecha. Pode ser chamado de qualquer thread.
pub fn launcher(app: &AppHandle, action: LauncherAction) -> Result<(), String> {
    let focused = app.state::<SharedCompositor>().monitors()?.into_iter().find(|m| m.focused);
    let handle = app.clone();
    app.run_on_main_thread(move || {
        let open = match action {
            LauncherAction::Toggle => !layer::is_visible("launcher"),
            LauncherAction::Open => true,
            LauncherAction::Close => false,
        };
        if open {
            if let Some(monitor) = &focused {
                layer::move_to_monitor_at("launcher", monitor.x, monitor.y);
            }
            layer::set_visible("launcher", true);
            let _ = handle.emit_to("launcher", "lumna://launcher-opened", ());
        } else {
            layer::set_visible("launcher", false);
            let _ = handle.emit_to("launcher", "lumna://launcher-closed", ());
        }
    })
    .map_err(|e| e.to_string())
}

/// Abre ou fecha o painel da Emma no monitor em foco (`Super + A`, botão do dock).
pub fn emma_panel(app: &AppHandle, action: LauncherAction) -> Result<(), String> {
    let focused = app.state::<SharedCompositor>().monitors()?.into_iter().find(|m| m.focused);
    let handle = app.clone();
    app.run_on_main_thread(move || {
        let open = match action {
            LauncherAction::Toggle => !layer::is_visible("emma"),
            LauncherAction::Open => true,
            LauncherAction::Close => false,
        };
        if open {
            if let Some(monitor) = &focused {
                layer::move_to_monitor_at("emma", monitor.x, monitor.y);
            }
            layer::set_visible("emma", true);
            let _ = handle.emit_to("emma", "lumna://emma-opened", ());
        } else {
            layer::set_visible("emma", false);
        }
    })
    .map_err(|e| e.to_string())
}

#[tauri::command]
fn toggle_emma(app: AppHandle) -> Result<(), String> {
    emma_panel(&app, LauncherAction::Toggle)
}

#[tauri::command]
fn toggle_launcher(app: AppHandle) -> Result<(), String> {
    launcher(&app, LauncherAction::Toggle)
}

/// Abre ou fecha o menu do Wi-Fi no monitor da topbar que foi clicada.
#[tauri::command]
fn toggle_wifi(app: AppHandle, window: WebviewWindow, positions: tauri::State<'_, MonitorPositions>) -> Result<(), String> {
    let at = positions.0.lock().unwrap().get(window.label()).copied();
    let handle = app.clone();
    app.run_on_main_thread(move || {
        if layer::is_visible("wifi") {
            layer::set_visible("wifi", false);
            return;
        }
        if let Some((x, y)) = at {
            layer::move_to_monitor_at("wifi", x, y);
        }
        layer::set_visible("wifi", true);
        let _ = handle.emit_to("wifi", "lumna://wifi-opened", ());
    })
    .map_err(|e| e.to_string())
}

// ---- configurações ----

/// Abre a janela de Configurações (uma janela comum, não uma camada) na página pedida.
pub fn open_settings(app: &AppHandle, page: Option<String>) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("settings") {
        let _ = window.unminimize();
        let _ = window.set_focus();
        if let Some(page) = page {
            let _ = window.emit_to("settings", "lumna://settings-page", page);
        }
        return Ok(());
    }
    let url = format!("index.html#{}", page.unwrap_or_default());
    WebviewWindowBuilder::new(app, "settings", WebviewUrl::App(url.into()))
        .title("Configurações")
        .inner_size(app_size(app, (1000.0, 680.0), false).0, app_size(app, (1000.0, 680.0), false).1)
        .background_color(tauri::window::Color(8, 6, 15, 255))
        .min_inner_size(min_side(760.0), min_side(520.0))
        .decorations(false)
        .transparent(true)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

/// Abre o Telefone (uma janela só), na aba pedida: teclado, recentes ou contatos.
pub fn open_phone(app: &AppHandle, tab: Option<String>) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("phone") {
        let _ = window.unminimize();
        let _ = window.set_focus();
        if let Some(tab) = tab {
            let _ = window.emit_to("phone", "lumna://phone-tab", tab);
        }
        return Ok(());
    }
    let url = format!("index.html#{}", tab.unwrap_or_default());
    WebviewWindowBuilder::new(app, "phone", WebviewUrl::App(url.into()))
        .title("Telefone")
        .inner_size(app_size(app, (420.0, 820.0), false).0, app_size(app, (420.0, 820.0), false).1)
        .background_color(tauri::window::Color(8, 6, 15, 255))
        .min_inner_size(min_side(360.0), min_side(560.0))
        .decorations(false)
        .transparent(true)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

/// Celular: o controle do modo Mídia do PC (tela cheia). `pc` = o id do PC no Connect.
pub fn open_remote(app: &AppHandle, pc: Option<String>) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("remote") {
        let _ = window.unminimize();
        let _ = window.set_focus();
        if let Some(pc) = pc {
            let _ = window.emit_to("remote", "lumna://remote-pc", pc);
        }
        return Ok(());
    }
    let url = format!("index.html#{}", pc.unwrap_or_default());
    WebviewWindowBuilder::new(app, "remote", WebviewUrl::App(url.into()))
        .title("LUMNA Controle")
        .inner_size(app_size(app, (420.0, 820.0), true).0, app_size(app, (420.0, 820.0), true).1)
        .background_color(tauri::window::Color(8, 6, 15, 255))
        .decorations(false)
        .fullscreen(true)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn remote_open(app: AppHandle, pc: Option<String>) -> Result<(), String> {
    open_remote(&app, pc)
}

/// Tocou num aviso do shell que abre algo (`Notification::open`).
#[tauri::command]
async fn notification_open(app: AppHandle, target: String) -> Result<(), String> {
    match target.split_once(':') {
        Some(("remote", pc)) => open_remote(&app, Some(pc.to_string()).filter(|p| !p.is_empty())),
        _ => Err(format!("não sei abrir {target}")),
    }
}

#[tauri::command]
async fn phone_open(app: AppHandle, tab: Option<String>) -> Result<(), String> {
    open_phone(&app, tab)
}

#[tauri::command]
async fn settings_open(app: AppHandle, page: Option<String>) -> Result<(), String> {
    open_settings(&app, page)
}

/// Mostra (com o pedido) ou esconde a tela de senha, no monitor em foco.
/// O painel da Emma estava aberto quando a tela de senha apareceu (volta quando ela fecha).
static EMMA_BEHIND_AUTH: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

pub fn auth_layer(app: &AppHandle, request: Option<polkit::Request>) {
    use std::sync::atomic::Ordering;
    let focused = app.state::<SharedCompositor>().monitors().ok().and_then(|m| m.into_iter().find(|m| m.focused));
    let handle = app.clone();
    let _ = app.run_on_main_thread(move || match request {
        Some(request) => {
            // O painel da Emma cobre a tela e prende o teclado: sai da frente enquanto a senha está
            // aberta (a Emma pediu a instalação e fica esperando).
            if layer::is_visible("emma") {
                layer::set_visible("emma", false);
                EMMA_BEHIND_AUTH.store(true, Ordering::SeqCst);
            }
            if !layer::is_visible("auth") {
                if let Some(monitor) = &focused {
                    layer::move_to_monitor_at("auth", monitor.x, monitor.y);
                }
                layer::set_visible("auth", true);
            }
            let _ = handle.emit_to("auth", "lumna://auth-request", request);
        }
        None => {
            layer::set_visible("auth", false);
            let _ = handle.emit_to("auth", "lumna://auth-done", ());
            if EMMA_BEHIND_AUTH.swap(false, Ordering::SeqCst) {
                layer::set_visible("emma", true);
                let _ = handle.emit_to("emma", "lumna://emma-returned", ());
            }
        }
    });
}

/// Abre a Loja (janela comum), na aba pedida.
pub fn open_store(app: &AppHandle, tab: Option<String>) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("store") {
        let _ = window.unminimize();
        let _ = window.set_focus();
        if let Some(tab) = tab {
            let _ = window.emit_to("store", "lumna://store-tab", tab);
        }
        return Ok(());
    }
    let url = format!("index.html#{}", tab.unwrap_or_default());
    WebviewWindowBuilder::new(app, "store", WebviewUrl::App(url.into()))
        .title("Loja")
        .inner_size(app_size(app, (1100.0, 720.0), false).0, app_size(app, (1100.0, 720.0), false).1)
        .background_color(tauri::window::Color(8, 6, 15, 255))
        .min_inner_size(min_side(800.0), min_side(540.0))
        .decorations(false)
        .transparent(true)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn store_open(app: AppHandle, tab: Option<String>) -> Result<(), String> {
    open_store(&app, tab)
}

/// Abre uma janela do Explorador (cada chamada é uma janela nova, como num desktop comum).
pub fn open_files(app: &AppHandle, path: Option<String>) -> Result<(), String> {
    static NEXT: std::sync::atomic::AtomicU32 = std::sync::atomic::AtomicU32::new(1);
    let label = format!("files-{}", NEXT.fetch_add(1, std::sync::atomic::Ordering::Relaxed));
    let path = path.map(|p| files::percent_decode(p.trim().trim_start_matches("file://"))).unwrap_or_default();
    // O caminho vai no fragmento da URL, codificado (pode ter espaço, acento, #).
    let fragment: String = path
        .bytes()
        .map(|b| if b.is_ascii_alphanumeric() || b"/-_.~".contains(&b) { (b as char).to_string() } else { format!("%{b:02X}") })
        .collect();
    WebviewWindowBuilder::new(app, &label, WebviewUrl::App(format!("index.html#{fragment}").into()))
        .title("Arquivos")
        .inner_size(app_size(app, (1100.0, 700.0), false).0, app_size(app, (1100.0, 700.0), false).1)
        .background_color(tauri::window::Color(8, 6, 15, 255))
        .min_inner_size(min_side(640.0), min_side(420.0))
        .decorations(false)
        .transparent(true)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn files_window(app: AppHandle, path: Option<String>) -> Result<(), String> {
    open_files(&app, path)
}

/// Abre o "Instalar LUMNA OS" (só existe no pendrive).
pub fn open_installer(app: &AppHandle) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("installer") {
        let _ = window.unminimize();
        let _ = window.set_focus();
        return Ok(());
    }
    WebviewWindowBuilder::new(app, "installer", WebviewUrl::App("index.html".into()))
        .title("Instalar LUMNA OS")
        .inner_size(app_size(app, (980.0, 660.0), false).0, app_size(app, (980.0, 660.0), false).1)
        .background_color(tauri::window::Color(8, 6, 15, 255))
        .min_inner_size(min_side(820.0), min_side(600.0))
        .decorations(false)
        .transparent(true)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn installer_open(app: AppHandle) -> Result<(), String> {
    open_installer(&app)
}

// ---- jogos e controle ----

/// Relê a biblioteca (jogos instalados ou removidos aparecem sem reiniciar o shell).
#[tauri::command]
fn games(app: AppHandle, cache: tauri::State<'_, GamesCache>) -> Vec<games::Game> {
    // As capas ficam fora da pasta do app: o webview só pode lê-las por estas pastas liberadas.
    let scope = app.asset_protocol_scope();
    for root in games::art_roots() {
        let _ = scope.allow_directory(&root, true);
    }
    let list = games::list();
    *cache.0.lock().unwrap() = list.clone();
    list
}

#[tauri::command]
fn launch_game(
    cache: tauri::State<'_, GamesCache>,
    compositor: tauri::State<'_, SharedCompositor>,
    id: String,
) -> Result<(), String> {
    let command = {
        let cache = cache.0.lock().unwrap();
        cache.iter().find(|g| g.id == id).map(|g| g.launch.clone()).ok_or_else(|| format!("jogo {id} não encontrado"))?
    };
    compositor.exec(&command)
}

#[derive(Clone, serde::Serialize)]
struct PadEvent {
    action: pad::Action,
    /// Monitor em foco: só a tela dele reage.
    monitor: Option<String>,
}

/// Uma ação do controle (ou do `lumna-ctl pad`). Com um app ou jogo em foco, o controle é dele:
/// só o botão central (troca de modo) continua com o LUMNA.
pub fn pad_action(app: &AppHandle, action: pad::Action) {
    // Barra de serviços da Mídia aberta: o controle é dela; o botão central volta ao vídeo.
    if let Some(monitor) = app.state::<tv::Tv>().bar() {
        if action == pad::Action::Guide {
            close_tvbar(app);
        } else {
            let _ = app.emit("lumna://tvbar-pad", PadEvent { action, monitor: Some(monitor) });
        }
        return;
    }
    let compositor = app.state::<SharedCompositor>();
    if action == pad::Action::Guide {
        // Na Mídia, com um serviço ou vídeo na frente, o botão central abre a barra de serviços.
        if media_menu(app).unwrap_or(false) {
            return;
        }
        if let Err(err) = switch_mode(app, Target::Next) {
            eprintln!("lumna-shell: troca de modo pelo controle: {err}");
        }
        return;
    }
    if matches!(compositor.active_window(), Ok(Some(_))) {
        return;
    }
    let monitor = compositor.monitors().ok().and_then(|m| m.into_iter().find(|m| m.focused)).map(|m| m.name);
    let _ = app.emit("lumna://pad", PadEvent { action, monitor });
}

// ---- mídia ----

#[tauri::command]
fn media_services() -> &'static [media::Service] {
    media::SERVICES
}

/// Vídeos locais. As miniaturas que faltam são geradas em segundo plano; quando ficam prontas,
/// sai o evento `lumna://videos-updated` e a tela pede a lista de novo.
#[tauri::command]
fn videos(app: AppHandle, cache: tauri::State<'_, VideosCache>) -> Vec<media::Video> {
    let _ = app.asset_protocol_scope().allow_directory(media::thumbs_dir(), false);
    let list = media::videos();
    *cache.0.lock().unwrap() = list.clone();
    if list.iter().any(|v| v.thumb.is_none()) {
        let missing = list.clone();
        std::thread::spawn(move || {
            if media::make_missing_thumbs(&missing) > 0 {
                let _ = app.emit("lumna://videos-updated", ());
            }
        });
    }
    list
}

#[tauri::command]
fn play_video(
    cache: tauri::State<'_, VideosCache>,
    compositor: tauri::State<'_, SharedCompositor>,
    id: String,
) -> Result<(), String> {
    let command = {
        let cache = cache.0.lock().unwrap();
        cache.iter().find(|v| v.id == id).map(media::video_command).ok_or_else(|| format!("vídeo {id} não encontrado"))?
    };
    compositor.exec(&command)?;
    media_hint();
    Ok(())
}

#[tauri::command]
fn remote_pairing(remote: tauri::State<'_, remote::Remote>) -> remote::Pairing {
    remote.start_pairing()
}

#[tauri::command]
fn remote_stop_pairing(remote: tauri::State<'_, remote::Remote>) {
    remote.stop_pairing();
}

#[tauri::command]
fn remote_forget(remote: tauri::State<'_, remote::Remote>) -> Result<(), String> {
    remote.forget_all()
}

/// Monitor em foco, se ele estiver no modo Mídia.
fn focused_media_monitor(app: &AppHandle) -> Result<Option<lumna_core::Monitor>, String> {
    let compositor = app.state::<SharedCompositor>();
    let Some(monitor) = compositor.monitors()?.into_iter().find(|m| m.focused) else { return Ok(None) };
    Ok((app.state::<Modes>().mode_of(&monitor.name) == Mode::Media).then_some(monitor))
}

/// Janelas na tela da Mídia que não são do Firefox da TV (o mpv dos vídeos locais).
fn media_others(compositor: &dyn Compositor, monitor: &str) -> Vec<lumna_core::Window> {
    let workspace = format!("media-{monitor}");
    compositor
        .windows()
        .unwrap_or_default()
        .into_iter()
        .filter(|w| w.workspace_name == workspace && w.class != tv::APP_ID)
        .collect()
}

fn monitor_or_focused(app: &AppHandle, monitor: Option<String>) -> Result<String, String> {
    match monitor {
        Some(name) => Ok(name),
        None => app
            .state::<SharedCompositor>()
            .monitors()?
            .into_iter()
            .find(|m| m.focused)
            .map(|m| m.name)
            .ok_or_else(|| "nenhum monitor em foco".into()),
    }
}

/// Barra de serviços da Mídia (`Super + M`, `Super + Esc`, botão central): só abre com um serviço
/// ou vídeo na frente; aberta, fecha. Devolve `false` fora da Mídia ou sem nada na frente.
pub fn media_menu(app: &AppHandle) -> Result<bool, String> {
    let tv = app.state::<tv::Tv>();
    if tv.bar().is_some() {
        close_tvbar(app);
        return Ok(true);
    }
    let Some(monitor) = focused_media_monitor(app)? else { return Ok(false) };
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let playing = tv.playing(compositor.as_ref(), &monitor.name);
    if playing.is_none() && media_others(compositor.as_ref(), &monitor.name).is_empty() {
        return Ok(false);
    }
    // O print da miniatura sai antes de a barra aparecer por cima.
    if playing.is_some() {
        tv.snapshot(&monitor.name);
    }
    tv.set_bar(Some(monitor.name.clone()));
    let _ = app.asset_protocol_scope().allow_directory(tv::thumbs_dir(), false);
    let state = tv.state(compositor.as_ref(), &monitor.name);
    let payload = serde_json::json!({ "monitor": monitor.name, "state": state });
    let handle = app.clone();
    let (x, y) = (monitor.x, monitor.y);
    app.run_on_main_thread(move || {
        layer::move_to_monitor_at("tvbar", x, y);
        layer::set_visible("tvbar", true);
        let _ = handle.emit("lumna://tvbar", payload);
    })
    .map_err(|e| e.to_string())?;
    Ok(true)
}

pub fn close_tvbar(app: &AppHandle) {
    app.state::<tv::Tv>().set_bar(None);
    let _ = app.run_on_main_thread(|| layer::set_visible("tvbar", false));
}

/// Avisa a tela da Mídia (Recentes e serviços vivos mudaram).
fn tv_changed(app: &AppHandle, monitor: &str) {
    let _ = app.emit("lumna://tv", monitor);
}

/// Volta para o Início da Mídia neste monitor: o serviço da frente vira Recente (e fica vivo,
/// estacionado) e o vídeo local fecha (o mpv guarda onde parou).
fn go_home(app: &AppHandle, monitor: &str) -> Result<(), String> {
    close_tvbar(app);
    let compositor = app.state::<SharedCompositor>().inner().clone();
    for window in media_others(compositor.as_ref(), monitor) {
        let _ = compositor.close_window(&window.address);
    }
    app.state::<tv::Tv>().home(compositor.as_ref(), monitor)?;
    tv_changed(app, monitor);
    Ok(())
}

/// Abre um serviço (ou retoma uma página) na Mídia. Sem monitor, no monitor em foco, que entra no
/// modo Mídia se ainda não estiver nele.
pub fn media_open(app: &AppHandle, monitor: Option<String>, service: &str, url: Option<&str>) -> Result<(), String> {
    let monitor = match monitor {
        Some(name) => name,
        None => {
            if focused_media_monitor(app)?.is_none() {
                switch_mode(app, Target::Set(Mode::Media))?;
            }
            monitor_or_focused(app, None)?
        }
    };
    close_tvbar(app);
    let compositor = app.state::<SharedCompositor>().inner().clone();
    for window in media_others(compositor.as_ref(), &monitor) {
        let _ = compositor.close_window(&window.address);
    }
    let result = app.state::<tv::Tv>().open(compositor.as_ref(), &monitor, service, url);
    tv_changed(app, &monitor);
    if result.is_ok() {
        media_hint();
    }
    result
}

/// Na Mídia: volta para o Início (botão "início" do celular, `lumna-ctl media home`). Fora da
/// Mídia não faz nada (devolve `false`).
pub fn media_home(app: &AppHandle) -> Result<bool, String> {
    let Some(monitor) = focused_media_monitor(app)? else { return Ok(false) };
    go_home(app, &monitor.name)?;
    Ok(true)
}

/// Aviso de como trocar de serviço, na primeira vez que algo abre em tela cheia na Mídia.
pub fn media_hint() {
    static SHOWN: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
    if SHOWN.swap(true, std::sync::atomic::Ordering::Relaxed) {
        return;
    }
    let _ = std::process::Command::new("notify-send")
        .args(["-a", "LUMNA", "-t", "6000", "Trocar de serviço ou voltar ao Início", "Super + M, ou o botão central do controle"])
        .spawn();
}

/// Botão "início" do celular: na Mídia, volta para o Início; fora dela, fecha o que estiver na
/// frente ou, se não houver nada, vai para a Mídia.
pub fn remote_home(app: &AppHandle) -> Result<(), String> {
    if media_home(app)? {
        return Ok(());
    }
    let compositor = app.state::<SharedCompositor>();
    if compositor.active_window()?.is_some() {
        compositor.close_active_window()
    } else {
        switch_mode(app, Target::Set(Mode::Media))
    }
}

// ---- Mídia: comandos da tela e da barra ----

#[tauri::command]
async fn tv_state(app: AppHandle, monitor: Option<String>) -> Result<tv::TvState, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let monitor = monitor_or_focused(&app, monitor)?;
        let _ = app.asset_protocol_scope().allow_directory(tv::thumbs_dir(), false);
        let compositor = app.state::<SharedCompositor>().inner().clone();
        Ok(app.state::<tv::Tv>().state(compositor.as_ref(), &monitor))
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn tv_open(app: AppHandle, monitor: Option<String>, service: String, url: Option<String>) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || media_open(&app, monitor, &service, url.as_deref()))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn tv_resume(app: AppHandle, monitor: Option<String>, id: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let recent = tv::recents().into_iter().find(|r| r.id == id).ok_or("essa página saiu dos recentes")?;
        media_open(&app, monitor, &recent.service, Some(&recent.url))
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn tv_home(app: AppHandle, monitor: Option<String>, focus: Option<String>) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let monitor = monitor_or_focused(&app, monitor)?;
        go_home(&app, &monitor)?;
        // "Vídeos locais" na barra: o Início abre já na fileira dos vídeos.
        if let Some(row) = focus {
            let _ = app.emit("lumna://media-focus", serde_json::json!({ "monitor": monitor, "row": row }));
        }
        Ok(())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn tv_forget(app: AppHandle, monitor: Option<String>, id: String) {
    app.state::<tv::Tv>().forget(&id);
    if let Ok(monitor) = monitor_or_focused(&app, monitor) {
        tv_changed(&app, &monitor);
    }
}

#[tauri::command]
fn tvbar_close(app: AppHandle) {
    close_tvbar(&app);
}

// ---- modos ----

/// Nome do monitor (no compositor) onde está a janela que chamou.
#[tauri::command]
fn monitor_name(
    window: WebviewWindow,
    positions: tauri::State<'_, MonitorPositions>,
    compositor: tauri::State<'_, SharedCompositor>,
) -> Result<Option<String>, String> {
    let Some(position) = positions.0.lock().unwrap().get(window.label()).copied() else { return Ok(None) };
    Ok(compositor.monitors()?.into_iter().find(|m| (m.x, m.y) == position).map(|m| m.name))
}

#[tauri::command]
fn modes(state: tauri::State<'_, Modes>) -> modes::Snapshot {
    state.snapshot(None)
}

#[tauri::command]
fn switch_to(app: AppHandle, mode: String) -> Result<(), String> {
    let mode = Mode::parse(&mode).ok_or_else(|| format!("modo desconhecido: {mode}"))?;
    switch_mode(&app, Target::Set(mode))
}

#[tauri::command]
fn change_layout(app: AppHandle, layout: Layout) -> Result<(), String> {
    set_layout(&app, layout)
}

#[tauri::command]
fn show_switcher(app: AppHandle) -> Result<(), String> {
    let focused = app.state::<SharedCompositor>().monitors()?.into_iter().find(|m| m.focused).map(|m| m.name);
    apply_modes(&app, focused, true)
}

pub fn switch_mode(app: &AppHandle, target: Target) -> Result<(), String> {
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let focused = app.state::<Modes>().switch(compositor.as_ref(), target)?;
    apply_modes(app, Some(focused), true)
}

pub fn set_layout(app: &AppHandle, layout: Layout) -> Result<(), String> {
    app.state::<Modes>().set_layout(layout)?;
    let focused = app.state::<SharedCompositor>().monitors()?.into_iter().find(|m| m.focused).map(|m| m.name);
    apply_modes(app, focused, true)
}

/// Avisa as janelas e mostra ou esconde as telas de modo, o dock e o seletor.
fn apply_modes(app: &AppHandle, focused: Option<String>, show_switcher: bool) -> Result<(), String> {
    let snapshot = app.state::<Modes>().snapshot(focused.clone());
    let monitors = app.state::<SharedCompositor>().monitors()?;
    let positions = app.state::<MonitorPositions>().0.lock().unwrap().clone();
    let mode_at = |label: &str| {
        positions
            .get(label)
            .and_then(|pos| monitors.iter().find(|m| (m.x, m.y) == *pos))
            .map(|m| snapshot.modes.get(&m.name).copied().unwrap_or_default())
            .unwrap_or_default()
    };
    let screens: Vec<(String, bool)> = positions
        .keys()
        .filter(|label| label.starts_with("mode-"))
        .map(|label| (label.clone(), mode_at(label) != Mode::Desktop))
        .collect();
    let switcher_at = focused.and_then(|name| monitors.iter().find(|m| m.name == name).map(|m| (m.x, m.y)));

    automation::modes_changed(app, &snapshot.modes);
    remote::modes_changed(&snapshot.modes);
    let docks = app.clone();
    std::thread::spawn(move || dock::update(&docks, false));
    let _ = app.emit("lumna://modes", snapshot);
    let handle = app.clone();
    app.run_on_main_thread(move || {
        for (label, visible) in &screens {
            layer::set_visible(label, *visible);
        }
        if show_switcher {
            if let Some((x, y)) = switcher_at {
                layer::move_to_monitor_at("switcher", x, y);
            }
            layer::set_visible("switcher", true);
            let _ = handle.emit_to("switcher", "lumna://switcher-shown", ());
        }
    })
    .map_err(|e| e.to_string())
}

/// Depois de uma troca de workspace por fora do LUMNA, o modo de cada monitor acompanha.
fn follow_workspaces(app: &AppHandle) {
    let Ok(monitors) = app.state::<SharedCompositor>().monitors() else { return };
    let modes = app.state::<Modes>();
    let mut changed = false;
    for monitor in &monitors {
        if !monitor.active_workspace_name.is_empty() {
            changed |= modes.follow_workspace(&monitor.name, &monitor.active_workspace_name);
        }
    }
    if changed {
        let _ = apply_modes(app, None, false);
    }
}

// ---- início ----

/// Repassa os eventos do compositor para todas as janelas. Se a conexão cair, tenta de novo.
fn forward_events(app: AppHandle, compositor: SharedCompositor) {
    std::thread::spawn(move || {
        let mut wait = Duration::from_millis(500);
        loop {
            let mut connected = false;
            let result = compositor.listen(&mut |event| {
                connected = true;
                if matches!(event, CompositorEvent::WorkspaceFocused { .. } | CompositorEvent::MonitorFocused { .. }) {
                    follow_workspaces(&app);
                }
                automation::compositor_event(&app, &event);
                // Janela maximizada, aberta, fechada ou trocada de workspace: o dock pode mudar.
                if !matches!(event, CompositorEvent::ActiveWindowChanged) {
                    dock::update(&app, false);
                }
                let _ = app.emit("lumna://compositor", event);
            });
            if connected {
                wait = Duration::from_millis(500);
            }
            if let Err(err) = result {
                eprintln!("lumna-shell: eventos do compositor: {err} (nova tentativa em {wait:?})");
            }
            std::thread::sleep(wait);
            wait = (wait * 2).min(Duration::from_secs(30));
        }
    });
}

/// Camadas que existem uma por monitor (`desktop-0`, `topbar-0`...).
const PER_MONITOR: [(&str, &LayerSpec); 5] =
    [("desktop-", &DESKTOP), ("topbar-", &TOPBAR), ("mode-", &MODE_SCREEN), ("dock-", &DOCK), ("rest-", &REST)];

/// Monitor ligado, desligado ou de volta (acordar da suspensão desliga e religa as saídas): cada
/// camada por monitor volta para o seu monitor, as que faltam são criadas, e modos e dock se refazem.
fn watch_monitors(app: &AppHandle) {
    use gtk::glib;
    use std::cell::RefCell;
    thread_local! {
        static PENDING: RefCell<Option<glib::SourceId>> = const { RefCell::new(None) };
    }
    let schedule = {
        let app = app.clone();
        move || {
            let app = app.clone();
            PENDING.with(|pending| {
                if let Some(id) = pending.borrow_mut().take() {
                    id.remove();
                }
                // As saídas chegam uma de cada vez e a geometria assenta depois: espera acalmar.
                let id = glib::timeout_add_local_once(Duration::from_millis(1500), move || {
                    PENDING.with(|pending| pending.borrow_mut().take());
                    relayout_monitors(&app);
                });
                *pending.borrow_mut() = Some(id);
            });
        }
    };
    let Some(display) = gtk::gdk::Display::default() else { return };
    let on_added = schedule.clone();
    display.connect_monitor_added(move |_, _| on_added());
    display.connect_monitor_removed(move |_, _| schedule());
}

fn relayout_monitors(app: &AppHandle) {
    let Some(display) = gtk::gdk::Display::default() else { return };
    let count = display.n_monitors();
    for index in 0..count {
        let Some(monitor) = display.monitor(index) else { continue };
        let geometry = monitor.geometry();
        for (prefix, spec) in PER_MONITOR {
            let label = format!("{prefix}{index}");
            if layer::exists(&label) {
                app.state::<MonitorPositions>().0.lock().unwrap().insert(label.clone(), (geometry.x(), geometry.y()));
                layer::set_monitor(&label, &monitor);
                // Fundo e topbar estão sempre lá; modos, dock e descanso se acertam logo abaixo.
                if prefix == "desktop-" || prefix == "topbar-" {
                    layer::set_visible(&label, true);
                }
            } else if let Err(err) = create_layer(app, &label, &monitor, spec) {
                eprintln!("lumna-shell: camada {label}: {err}");
            }
        }
    }
    // As camadas únicas voltam ao principal (as que abrem no monitor em foco se movem sozinhas ao abrir).
    if let Some(primary) = display.monitor(0) {
        for label in ["notifications", "launcher", "switcher", "wifi", "power", "traymenu", "auth", "emma"] {
            layer::set_monitor(label, &primary);
        }
    }
    // Monitores a menos: as camadas deles ficam escondidas até voltarem.
    let extra: Vec<String> = app
        .state::<MonitorPositions>()
        .0
        .lock()
        .unwrap()
        .keys()
        .filter(|label| {
            PER_MONITOR.iter().any(|(prefix, _)| {
                label.strip_prefix(prefix).and_then(|i| i.parse::<i32>().ok()).is_some_and(|i| i >= count)
            })
        })
        .cloned()
        .collect();
    for label in &extra {
        layer::set_visible(label, false);
    }
    let handle = app.clone();
    std::thread::spawn(move || {
        let _ = apply_modes(&handle, None, false);
        dock::update(&handle, true);
        let _ = handle.emit("lumna://monitors-relayout", ());
    });
}

fn create_layer(app: &AppHandle, label: &str, monitor: &gtk::gdk::Monitor, spec: &LayerSpec) -> tauri::Result<()> {
    let geometry = monitor.geometry();
    app.state::<MonitorPositions>().0.lock().unwrap().insert(label.to_string(), (geometry.x(), geometry.y()));
    let window = WebviewWindowBuilder::new(app, label, WebviewUrl::App("index.html".into()))
        .title(format!("LUMNA {label}"))
        .visible(false)
        .decorations(false)
        .transparent(true)
        .build()?;
    layer::make_layer(&window, monitor, spec)?;
    // O WebKit numa camada não avisa a página quando o mouse sai dela (`pointerleave` não chega).
    // O GTK avisa: a página recebe `lumna://pointer-left` (o dock usa para descer de volta).
    let target = window.clone();
    window.with_webview(move |webview| {
        use gtk::prelude::WidgetExt;
        webview.inner().connect_leave_notify_event(move |_, event| {
            // Só a saída de verdade: clicar também gera "saídas" de pega/solta (grab/ungrab).
            if event.mode() == gtk::gdk::CrossingMode::Normal && event.detail() != gtk::gdk::NotifyType::Inferior {
                let _ = target.emit_to(target.label(), "lumna://pointer-left", ());
            }
            gtk::glib::Propagation::Proceed
        });
    })
}

fn main() {
    let greeter_mode = std::env::args().any(|a| a == "--greeter");
    let mobile_mode = std::env::args().any(|a| a == "--mobile");
    MOBILE.store(mobile_mode, std::sync::atomic::Ordering::Relaxed);
    let compositor: SharedCompositor = match Hyprland::from_env() {
        Ok(hyprland) => Arc::new(hyprland),
        Err(err) => {
            eprintln!("lumna-shell: {err}");
            std::process::exit(1);
        }
    };

    tauri::Builder::default()
        .manage(compositor.clone())
        .manage(AppsCache::default())
        .manage(GamesCache::default())
        .manage(VideosCache::default())
        .manage(remote::Remote::default())
        .manage(status::StatusState::default())
        .manage(Modes::load())
        .manage(MonitorPositions::default())
        .manage(emma::Emma::default())
        .manage(automation::Runner::default())
        .manage(dock::Docks::default())
        .manage(tv::Tv::load())
        .manage(winmax::Maximized::default())
        .invoke_handler(tauri::generate_handler![
            workspaces,
            monitors,
            windows,
            active_window,
            window_control,
            focus_workspace,
            apps,
            launch_app,
            status,
            toggle_mute,
            change_volume,
            layer_resize,
            layer_hide,
            notification_closed,
            notification_action,
            toggle_launcher,
            toggle_emma,
            desktop_wallpaper,
            displays::displays,
            window_bars,
            keyboard::keyboard_state,
            keyboard::keyboard_current,
            keyboard::keyboard_next,
            power::power_state,
            tray::tray_items,
            tray::tray_activate,
            tray::tray_menu,
            tray::tray_menu_click,
            tray::tray_menu_open,
            rest::rest_off,
            wallpaper::wallpaper_motion_set,
            lock::idle_config,
            lock::idle_config_set,
            lock::lock_screen,
            power::power_set_profile,
            power::power_action,
            power::power_menu,
            keyboard::keyboard_set,
            keyboard::locale_set,
            displays::displays_arrange,
            dock::dock_state,
            dock::dock_config,
            dock::dock_config_set,
            dock::dock_config_reset,
            dock::dock_pin,
            dock::dock_monitors,
            wallpaper::wallpaper_list,
            wallpaper::wallpaper_set,
            wallpaper::wallpaper_set_lock,
            wallpaper::wallpaper_add,
            wallpaper::wallpaper_remove,
            wallpaper::wallpaper_gallery,
            wallpaper::wallpaper_import,
            monitor_name,
            modes,
            switch_to,
            change_layout,
            show_switcher,
            games,
            launch_game,
            media_services,
            videos,
            play_video,
            tv_state,
            tv_open,
            tv_resume,
            tv_home,
            tv_forget,
            tvbar_close,
            remote_pairing,
            remote_stop_pairing,
            remote_forget,
            toggle_wifi,
            settings_open,
            store_open,
            files_window,
            installer_open,
            installer::installer_disks,
            installer::installer_timezones,
            installer::installer_run,
            installer::installer_reboot,
            greeter::greeter_users,
            greeter::greeter_sessions,
            greeter::greeter_login,
            greeter::greeter_power,
            files::files_list,
            files::files_stamp,
            files::files_places,
            files::files_open,
            files::files_terminal,
            files::files_mkdir,
            files::files_rename,
            files::files_trash,
            files::files_paste,
            files::files_trash_list,
            files::files_trash_restore,
            files::files_trash_delete,
            files::files_mount,
            files::files_unmount,
            files::files_info,
            files::files_thumb,
            polkit::auth_respond,
            store::store_search,
            store::store_installed,
            store::store_updates,
            store::store_sources,
            store::store_install,
            store::store_remove,
            store::store_upgrade,
            store::store_set_multilib,
            store::store_set_flathub,
            store::store_set_aur,
            network::network_interfaces,
            bluetooth::bluetooth_state,
            bluetooth::bluetooth_power,
            bluetooth::bluetooth_scan,
            bluetooth::bluetooth_pair,
            bluetooth::bluetooth_connect,
            bluetooth::bluetooth_disconnect,
            bluetooth::bluetooth_forget,
            bluetooth::bluetooth_confirm,
            audio::audio_state,
            audio::audio_set_default,
            audio::audio_set_volume,
            audio::audio_toggle_mute,
            printers::printers,
            printers::printers_discover,
            printers::printer_add,
            printers::printer_remove,
            printers::printer_set_default,
            printers::printer_test_page,
            system_info::system_info,
            wifi::wifi_state,
            wifi::wifi_scan,
            wifi::wifi_connect,
            wifi::wifi_disconnect,
            wifi::wifi_forget,
            wifi::wifi_power,
            actions::actions_tool_defs,
            actions::actions_run_tool,
            actions::clipboard_read,
            actions::clipboard_write,
            emma::emma_status,
            emma::emma_login,
            touchkeys::keyboard_type,
            touchkeys::keyboard_key,
            touchkeys::keyboard_show,
            touchkeys::keyboard_toggle,
            touchkeys::keyboard_target,
            haptics::haptic,
            mobile::mobile_panel,
            mobile::recents,
            mobile::recents_snapshot,
            mobile::recents_kill,
            mobile::quick_state,
            mobile::quick_brightness,
            mobile::quick_flashlight,
            mobile::quick_mobile_data,
            mobile::quick_airplane,
            mobile::quick_quiet,
            mobile::mobile_edges,
            mobile::mobile_show,
            animations::animations_get,
            animations::animations_set,
            animations::animations_reset,
            haptics::haptics_get,
            haptics::haptics_set,
            mobile::battery_info,
            connect::connect_computers,
            connect::connect_discover,
            connect::connect_pair,
            connect::connect_forget,
            connect::connect_send,
            connect::connect_ask_close,
            connect::connect_set_features,
            connect::connect_set_address,
            connect::connect_send_files,
            connect::connect_send_clipboard,
            connect::connect_targets,
            connect::connect_share,
            connect::connect_media,
            link::connect_settings,
            link::connect_settings_set,
            link::connect_transfers,
            link::connect_recent_files,
            remote::remote_devices,
            remote::remote_device_set,
            remote::remote_device_forget,
            remote::remote_send_files,
            remote::remote_send_clipboard,
            schedule::schedule_list,
            schedule::schedule_add,
            schedule::schedule_remove,
            mobilelock::lock_state,
            mobilelock::lock_unlock,
            mobilelock::lock_now,
            mobilelock::lock_config_set,
            mobilelock::power_menu_close,
            mobile::profile_get,
            mobile::profile_set_name,
            mobile::profile_set_photo,
            wifi::wifi_add,
            wifi::wifi_details,
            mobile::mobile_data_info,
            mobile::mobile_data_set,
            phone::phone_calls,
            phone::phone_call,
            phone::phone_accept,
            phone::phone_hangup,
            phone::phone_dtmf,
            phone::phone_mute,
            phone::phone_log,
            phone::phone_log_clear,
            phone::contacts_list,
            phone::contacts_save,
            phone::contacts_delete,
            phone_open,
            remote_open,
            notification_open,
            emma::emma_logout,
            emma::emma_send,
            emma::emma_decide,
            emma::emma_new_conversation,
            emma::emma_history,
            emma::emma_permissions,
            emma::emma_set_permission,
            emma::emma_providers,
            emma::emma_set_provider,
            automation::automation_overview,
            automation::automation_set_enabled,
            automation::automation_run_macro,
        ])
        .setup(move |app| {
            let display = gtk::gdk::Display::default().expect("sem display GTK");
            let primary = display.monitor(0).expect("nenhum monitor");
            // Tela de login: só ela, sem topbar, dock nem serviços da sessão.
            if greeter_mode {
                create_layer(app.handle(), "greeter", &primary, &GREETER)?;
                return Ok(());
            }
            // Celular: uma tela só, com barra de status, Início e navegação; a Emma por cima.
            if mobile_mode {
                create_layer(app.handle(), "mobile-home", &primary, &MOBILE_HOME)?;
                create_layer(app.handle(), "mobile-status", &primary, &MOBILE_STATUS)?;
                create_layer(app.handle(), "mobile-nav", &primary, &MOBILE_NAV)?;
                create_layer(app.handle(), "mobile-edge-left", &primary, &MOBILE_EDGE_LEFT)?;
                create_layer(app.handle(), "mobile-edge-right", &primary, &MOBILE_EDGE_RIGHT)?;
                create_layer(app.handle(), "emma", &primary, &MOBILE_EMMA)?;
                create_layer(app.handle(), "notifications", &primary, &MOBILE_NOTIFICATIONS)?;
                create_layer(app.handle(), "mobile-center", &primary, &MOBILE_CENTER)?;
                create_layer(app.handle(), "mobile-recents", &primary, &MOBILE_RECENTS)?;
                create_layer(app.handle(), "mobile-ask", &primary, &MOBILE_ASK)?;
                create_layer(app.handle(), "mobile-lock", &primary, &MOBILE_LOCK)?;
                create_layer(app.handle(), "mobile-call", &primary, &MOBILE_CALL)?;
                create_layer(app.handle(), "mobile-power", &primary, &MOBILE_POWER)?;
                // por último: fica por cima da Emma (as duas são Overlay)
                create_layer(app.handle(), "keyboard", &primary, &KEYBOARD)?;
                forward_events(app.handle().clone(), compositor.clone());
                status::watch(app.handle().clone());
                wifi::watch(app.handle().clone());
                notifications::start(app.handle().clone());
                bluetooth::watch(app.handle().clone());
                control::listen(app.handle().clone());
                phone::watch(app.handle().clone());
                mobilelock::start(app.handle().clone());
                connect::start(app.handle().clone());
                mobile::track_data_usage();
                return Ok(());
            }
            for index in 0..display.n_monitors() {
                if let Some(monitor) = display.monitor(index) {
                    for (prefix, spec) in PER_MONITOR {
                        create_layer(app.handle(), &format!("{prefix}{index}"), &monitor, spec)?;
                    }
                }
            }
            watch_monitors(app.handle());
            create_layer(app.handle(), "notifications", &primary, &NOTIFICATIONS)?;
            create_layer(app.handle(), "launcher", &primary, &LAUNCHER)?;
            create_layer(app.handle(), "switcher", &primary, &SWITCHER)?;
            create_layer(app.handle(), "tvbar", &primary, &TVBAR)?;
            create_layer(app.handle(), "wifi", &primary, &WIFI)?;
            create_layer(app.handle(), "power", &primary, &POWER)?;
            create_layer(app.handle(), "traymenu", &primary, &TRAY_MENU)?;
            create_layer(app.handle(), "auth", &primary, &AUTH)?;
            create_layer(app.handle(), "emma", &primary, &EMMA)?;

            forward_events(app.handle().clone(), compositor.clone());
            status::watch(app.handle().clone());
            notifications::start(app.handle().clone());
            control::listen(app.handle().clone());
            pad::watch(app.handle().clone());
            remote::start(app.handle().clone());
            schedule::start(app.handle().clone());
            wifi::watch(app.handle().clone());
            bluetooth::watch(app.handle().clone());
            polkit::start(app.handle().clone());
            lock::start(app.handle().clone());
            tray::start(app.handle().clone());
            automation::start(app.handle().clone());
            let handle = app.handle().clone();
            std::thread::spawn(move || dock::update(&handle, true));
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("erro ao rodar o lumna-shell");
}
