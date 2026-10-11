mod layer;

use std::sync::Arc;
use std::time::Duration;

use lumna_core::{Compositor, Monitor, Workspace};
use lumna_hypr::Hyprland;
use tauri::{Emitter, WebviewUrl, WebviewWindowBuilder};

type SharedCompositor = Arc<dyn Compositor>;

#[tauri::command]
fn workspaces(compositor: tauri::State<'_, SharedCompositor>) -> Result<Vec<Workspace>, String> {
    compositor.workspaces()
}

#[tauri::command]
fn monitors(compositor: tauri::State<'_, SharedCompositor>) -> Result<Vec<Monitor>, String> {
    compositor.monitors()
}

#[tauri::command]
fn focus_workspace(compositor: tauri::State<'_, SharedCompositor>, id: i64) -> Result<(), String> {
    compositor.focus_workspace(id)
}

/// Repassa os eventos do compositor para todas as janelas. Se a conexão cair, tenta de novo.
fn forward_events(app: tauri::AppHandle, compositor: SharedCompositor) {
    std::thread::spawn(move || loop {
        let result = compositor.listen(&mut |event| {
            let _ = app.emit("lumna://compositor", event);
        });
        if let Err(err) = result {
            eprintln!("lumna-shell: eventos do compositor: {err}");
        }
        std::thread::sleep(Duration::from_secs(1));
    });
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
        .invoke_handler(tauri::generate_handler![workspaces, monitors, focus_workspace])
        .setup(move |app| {
            let display = gtk::gdk::Display::default().expect("sem display GTK");
            for index in 0..display.n_monitors() {
                let Some(monitor) = display.monitor(index) else { continue };
                let window = WebviewWindowBuilder::new(app, format!("topbar-{index}"), WebviewUrl::App("index.html".into()))
                    .title("LUMNA topbar")
                    .visible(false)
                    .decorations(false)
                    .transparent(true)
                    .build()?;
                layer::make_topbar(&window, &monitor)?;
            }
            forward_events(app.handle().clone(), compositor.clone());
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("erro ao rodar o lumna-shell");
}
