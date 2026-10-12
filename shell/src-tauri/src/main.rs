mod apps;
mod control;
mod layer;
mod notifications;
mod status;

use std::sync::{Arc, Mutex};
use std::time::Duration;

use gtk_layer_shell::{Edge, KeyboardMode, Layer};
use layer::{Exclusive, LayerSpec};
use lumna_core::{Compositor, Monitor, Window, Workspace};
use lumna_hypr::Hyprland;
use tauri::{AppHandle, Emitter, Manager, WebviewUrl, WebviewWindow, WebviewWindowBuilder};

type SharedCompositor = Arc<dyn Compositor>;

/// Terminal usado para apps de linha de comando (`Terminal=true` no `.desktop`).
const TERMINAL: &str = "foot";

#[derive(Default)]
struct AppsCache(Mutex<Vec<apps::App>>);

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

// ---- início ----

/// Repassa os eventos do compositor para todas as janelas. Se a conexão cair, tenta de novo.
fn forward_events(app: AppHandle, compositor: SharedCompositor) {
    std::thread::spawn(move || {
        let mut wait = Duration::from_millis(500);
        loop {
            let mut connected = false;
            let result = compositor.listen(&mut |event| {
                connected = true;
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
        .manage(status::StatusState::default())
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
        ])
        .setup(move |app| {
            let display = gtk::gdk::Display::default().expect("sem display GTK");
            let primary = display.monitor(0).expect("nenhum monitor");
            for index in 0..display.n_monitors() {
                if let Some(monitor) = display.monitor(index) {
                    create_layer(app, &format!("topbar-{index}"), &monitor, &TOPBAR)?;
                }
            }
            create_layer(app, "dock", &primary, &DOCK)?;
            create_layer(app, "notifications", &primary, &NOTIFICATIONS)?;
            create_layer(app, "launcher", &primary, &LAUNCHER)?;

            forward_events(app.handle().clone(), compositor.clone());
            status::watch(app.handle().clone());
            notifications::start(app.handle().clone());
            control::listen(app.handle().clone());
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("erro ao rodar o lumna-shell");
}
