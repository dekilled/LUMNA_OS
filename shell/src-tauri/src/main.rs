mod actions;
mod apps;
mod automation;
mod audio;
mod bluetooth;
mod control;
mod emma;
mod files;
mod games;
mod greeter;
mod installer;
mod layer;
mod media;
mod modes;
mod network;
mod notifications;
mod pad;
mod polkit;
mod printers;
mod remote;
mod status;
mod store;
mod system_info;
mod vdf;
mod wifi;

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
    visible: true,
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
        .inner_size(1000.0, 680.0)
        .min_inner_size(760.0, 520.0)
        .decorations(false)
        .transparent(true)
        .build()
        .map(|_| ())
        .map_err(|e| e.to_string())
}

#[tauri::command]
async fn settings_open(app: AppHandle, page: Option<String>) -> Result<(), String> {
    open_settings(&app, page)
}

/// Mostra (com o pedido) ou esconde a tela de senha, no monitor em foco.
pub fn auth_layer(app: &AppHandle, request: Option<polkit::Request>) {
    let focused = app.state::<SharedCompositor>().monitors().ok().and_then(|m| m.into_iter().find(|m| m.focused));
    let handle = app.clone();
    let _ = app.run_on_main_thread(move || match request {
        Some(request) => {
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
        .inner_size(1100.0, 720.0)
        .min_inner_size(800.0, 540.0)
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
        .inner_size(1100.0, 700.0)
        .min_inner_size(640.0, 420.0)
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
        .inner_size(980.0, 660.0)
        .min_inner_size(820.0, 600.0)
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
    let compositor = app.state::<SharedCompositor>();
    if action == pad::Action::Guide {
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
    compositor.exec(&command)
}

#[tauri::command]
fn open_service(compositor: tauri::State<'_, SharedCompositor>, id: String) -> Result<(), String> {
    let command = media::service_command(&id)?;
    // Um serviço por vez: o Firefox da TV anterior fecha antes de abrir o próximo.
    let _ = std::process::Command::new("pkill").args(["-f", "lumna/firefox-tv"]).status();
    compositor.exec(&command)
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

/// Botão "início" do celular: fecha o que estiver na frente (streaming, vídeo) ou, se não houver
/// nada, vai para a tela de Mídia.
pub fn remote_home(app: &AppHandle) -> Result<(), String> {
    let compositor = app.state::<SharedCompositor>();
    if compositor.active_window()?.is_some() {
        compositor.close_active_window()
    } else {
        switch_mode(app, Target::Set(Mode::Media))
    }
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
    let dock_visible = mode_at("dock") == Mode::Desktop;
    let switcher_at = focused.and_then(|name| monitors.iter().find(|m| m.name == name).map(|m| (m.x, m.y)));

    automation::modes_changed(app, &snapshot.modes);
    let _ = app.emit("lumna://modes", snapshot);
    let handle = app.clone();
    app.run_on_main_thread(move || {
        for (label, visible) in &screens {
            layer::set_visible(label, *visible);
        }
        layer::set_visible("dock", dock_visible);
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

fn create_layer(app: &tauri::App, label: &str, monitor: &gtk::gdk::Monitor, spec: &LayerSpec) -> tauri::Result<()> {
    let geometry = monitor.geometry();
    app.state::<MonitorPositions>().0.lock().unwrap().insert(label.to_string(), (geometry.x(), geometry.y()));
    let window = WebviewWindowBuilder::new(app, label, WebviewUrl::App("index.html".into()))
        .title(format!("LUMNA {label}"))
        .visible(false)
        .decorations(false)
        .transparent(true)
        .build()?;
    layer::make_layer(&window, monitor, spec)
}

fn main() {
    let greeter_mode = std::env::args().any(|a| a == "--greeter");
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
        .invoke_handler(tauri::generate_handler![
            workspaces,
            monitors,
            windows,
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
            open_service,
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
                create_layer(app, "greeter", &primary, &GREETER)?;
                return Ok(());
            }
            for index in 0..display.n_monitors() {
                if let Some(monitor) = display.monitor(index) {
                    create_layer(app, &format!("topbar-{index}"), &monitor, &TOPBAR)?;
                    create_layer(app, &format!("mode-{index}"), &monitor, &MODE_SCREEN)?;
                }
            }
            create_layer(app, "dock", &primary, &DOCK)?;
            create_layer(app, "notifications", &primary, &NOTIFICATIONS)?;
            create_layer(app, "launcher", &primary, &LAUNCHER)?;
            create_layer(app, "switcher", &primary, &SWITCHER)?;
            create_layer(app, "wifi", &primary, &WIFI)?;
            create_layer(app, "auth", &primary, &AUTH)?;
            create_layer(app, "emma", &primary, &EMMA)?;

            forward_events(app.handle().clone(), compositor.clone());
            status::watch(app.handle().clone());
            notifications::start(app.handle().clone());
            control::listen(app.handle().clone());
            pad::watch(app.handle().clone());
            remote::start(app.handle().clone());
            wifi::watch(app.handle().clone());
            bluetooth::watch(app.handle().clone());
            polkit::start(app.handle().clone());
            automation::start(app.handle().clone());
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("erro ao rodar o lumna-shell");
}
