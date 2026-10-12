mod apps;
mod control;
mod games;
mod layer;
mod media;
mod modes;
mod notifications;
mod pad;
mod remote;
mod status;
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
            wifi::wifi_state,
            wifi::wifi_scan,
            wifi::wifi_connect,
            wifi::wifi_disconnect,
            wifi::wifi_forget,
            wifi::wifi_power,
        ])
        .setup(move |app| {
            let display = gtk::gdk::Display::default().expect("sem display GTK");
            let primary = display.monitor(0).expect("nenhum monitor");
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

            forward_events(app.handle().clone(), compositor.clone());
            status::watch(app.handle().clone());
            notifications::start(app.handle().clone());
            control::listen(app.handle().clone());
            pad::watch(app.handle().clone());
            remote::start(app.handle().clone());
            wifi::watch(app.handle().clone());
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("erro ao rodar o lumna-shell");
}
