//! Tela de descanso: depois de um tempo parado, o fundo animado do LUMNA em tela cheia com um relógio
//! grande, em todos os monitores. Qualquer movimento ou tecla sai dela.
//!
//! Quem conta o tempo é o hypridle (`lumna-ctl rest on` / `lumna-ctl rest off`, ver `lock.rs`).
//! Não é proteção: o bloqueio (hyprlock) entra por cima no tempo dele.

use tauri::{AppHandle, Emitter, Manager};

use crate::{layer, MonitorPositions};

pub fn set(app: &AppHandle, on: bool) -> Result<(), String> {
    let labels: Vec<String> =
        app.state::<MonitorPositions>().0.lock().unwrap().keys().filter(|l| l.starts_with("rest-")).cloned().collect();
    let handle = app.clone();
    app.run_on_main_thread(move || {
        for label in &labels {
            layer::set_visible(label, on);
        }
        let _ = handle.emit("lumna://rest", on);
    })
    .map_err(|e| e.to_string())
}

/// A página do descanso viu o mouse ou o teclado.
#[tauri::command]
pub fn rest_off(app: AppHandle) -> Result<(), String> {
    set(&app, false)
}
