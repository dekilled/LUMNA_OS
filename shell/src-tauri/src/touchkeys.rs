//! LUMNA Keyboard (teclado na tela; o keyboard.rs é o layout do teclado físico): o teclado virtual do celular (`lumna-shell --mobile`).
//!
//! A tela do teclado é uma camada do layer-shell que nunca pega o foco do teclado: tocar nela não
//! tira o foco do app. As teclas chegam ao app em foco pelo protocolo de teclado virtual do Wayland
//! (o `wtype`, o mesmo que o controle remoto usa), numa fila só, para sair na ordem em que foram
//! tocadas mesmo quando a pessoa digita rápido.

use std::process::Command;
use std::sync::mpsc::{channel, Sender};
use std::sync::{Mutex, OnceLock};

use tauri::{AppHandle, Emitter};

use crate::layer;

enum Job {
    Text(String),
    Key { key: String, mods: Vec<String> },
}

static QUEUE: OnceLock<Mutex<Sender<Job>>> = OnceLock::new();

fn queue() -> &'static Mutex<Sender<Job>> {
    QUEUE.get_or_init(|| {
        let (tx, rx) = channel::<Job>();
        std::thread::spawn(move || {
            for job in rx {
                let mut cmd = Command::new("wtype");
                match &job {
                    Job::Text(text) => {
                        cmd.args(["--", text]);
                    }
                    // segura os modificadores (ctrl, alt), aperta a tecla e solta
                    Job::Key { key, mods } => {
                        for m in mods {
                            cmd.args(["-M", m]);
                        }
                        cmd.args(["-k", key]);
                        for m in mods.iter().rev() {
                            cmd.args(["-m", m]);
                        }
                    }
                }
                if let Err(e) = cmd.status() {
                    eprintln!("lumna-keyboard: wtype: {e}");
                }
            }
        });
        Mutex::new(tx)
    })
}

/// Nomes de tecla do xkb aceitos (o resto é recusado: a página não manda comando arbitrário).
const KEYS: &[&str] = &[
    "BackSpace", "Return", "Tab", "Escape", "Left", "Right", "Up", "Down", "Home", "End", "Delete", "space",
];
const MODS: &[&str] = &["ctrl", "alt", "shift", "logo"];

#[tauri::command]
pub fn keyboard_type(text: String) -> Result<(), String> {
    if text.is_empty() || text.len() > 4000 {
        return Err("texto vazio ou longo demais".into());
    }
    queue().lock().unwrap().send(Job::Text(text)).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn keyboard_key(key: String, mods: Vec<String>) -> Result<(), String> {
    // letra com Ctrl/Alt (Ctrl+C no terminal): uma letra minúscula também vale
    let letter = key.len() == 1 && key.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit());
    if !KEYS.contains(&key.as_str()) && !letter {
        return Err(format!("tecla desconhecida: {key}"));
    }
    if let Some(m) = mods.iter().find(|m| !MODS.contains(&m.as_str())) {
        return Err(format!("modificador desconhecido: {m}"));
    }
    queue().lock().unwrap().send(Job::Key { key, mods }).map_err(|e| e.to_string())
}

/// Mostra ou esconde o teclado. Enquanto ele está aberto, a barra de navegação sai (o teclado
/// ocupa o rodapé, como no esboço) e volta quando ele fecha.
#[tauri::command]
pub fn keyboard_show(app: AppHandle, show: bool) -> Result<(), String> {
    let handle = app.clone();
    app.run_on_main_thread(move || {
        if !layer::exists("keyboard") {
            return;
        }
        if layer::is_visible("keyboard") == show {
            return;
        }
        layer::set_visible("keyboard", show);
        layer::set_visible("mobile-nav", !show);
        let _ = handle.emit("lumna://keyboard", show);
    })
    .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn keyboard_toggle(app: AppHandle) -> Result<(), String> {
    let shown = layer::is_visible("keyboard");
    keyboard_show(app, !shown)
}
