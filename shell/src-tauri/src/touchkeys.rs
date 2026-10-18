//! LUMNA Keyboard (teclado na tela; o keyboard.rs é o layout do teclado físico): o teclado virtual do celular (`lumna-shell --mobile`).
//!
//! A tela do teclado é uma camada do layer-shell que nunca pega o foco do teclado: tocar nela não
//! tira o foco do app. As teclas chegam ao app em foco pelo protocolo de teclado virtual do Wayland
//! (o `wtype`, o mesmo que o controle remoto usa), numa fila só, para sair na ordem em que foram
//! tocadas mesmo quando a pessoa digita rápido.

use std::process::Command;
use std::sync::mpsc::{channel, Sender};
use std::sync::{Mutex, OnceLock};

use serde_json::json;
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
                // a saída vai para o journal: journalctl -t lumna-shell | grep lumna-keyboard
                match cmd.output() {
                    Ok(out) if !out.status.success() => eprintln!(
                        "lumna-keyboard: wtype {:?} falhou ({}): {}",
                        cmd.get_args().collect::<Vec<_>>(),
                        out.status,
                        String::from_utf8_lossy(&out.stderr).trim()
                    ),
                    Ok(_) => {}
                    Err(e) => eprintln!("lumna-keyboard: wtype: {e}"),
                }
            }
        });
        Mutex::new(tx)
    })
}

/// Janela do LUMNA com um campo de texto em foco. As teclas vão direto para ela (pela página), sem
/// passar pelo teclado virtual do Wayland: o WebKit lê as teclas de edição (apagar, setas, Enter)
/// pela posição física da tecla, e o mapa de teclas que o wtype inventa não bate com ela.
static TARGET: Mutex<Option<String>> = Mutex::new(None);
/// O teclado aberto agora está por cima de um app do LUMNA (sem reservar espaço).
static OVERLAY: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

#[tauri::command]
pub fn keyboard_target(label: String, focused: bool) {
    let mut target = TARGET.lock().unwrap();
    if focused {
        *target = Some(label);
    } else if target.as_deref() == Some(label.as_str()) {
        *target = None;
    }
}

/// Entrega à janela do LUMNA em foco, se houver. `true` = entregue.
fn to_lumna(app: &AppHandle, payload: serde_json::Value) -> bool {
    let target = TARGET.lock().unwrap().clone();
    match target {
        Some(label) => app.emit_to(label.as_str(), "lumna://keyboard-input", payload).is_ok(),
        None => false,
    }
}

/// Nomes de tecla do xkb aceitos (o resto é recusado: a página não manda comando arbitrário).
const KEYS: &[&str] = &[
    "BackSpace", "Return", "Tab", "Escape", "Left", "Right", "Up", "Down", "Home", "End", "Delete", "space",
];
const MODS: &[&str] = &["ctrl", "alt", "shift", "logo"];

#[tauri::command]
pub fn keyboard_type(app: AppHandle, text: String) -> Result<(), String> {
    if text.is_empty() || text.len() > 4000 {
        return Err("texto vazio ou longo demais".into());
    }
    if to_lumna(&app, json!({ "kind": "text", "text": text })) {
        return Ok(());
    }
    queue().lock().unwrap().send(Job::Text(text)).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn keyboard_key(app: AppHandle, key: String, mods: Vec<String>) -> Result<(), String> {
    // letra com Ctrl/Alt (Ctrl+C no terminal): uma letra minúscula também vale
    let letter = key.len() == 1 && key.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit());
    if !KEYS.contains(&key.as_str()) && !letter {
        return Err(format!("tecla desconhecida: {key}"));
    }
    if let Some(m) = mods.iter().find(|m| !MODS.contains(&m.as_str())) {
        return Err(format!("modificador desconhecido: {m}"));
    }
    if to_lumna(&app, json!({ "kind": "key", "key": key, "mods": mods })) {
        return Ok(());
    }
    queue().lock().unwrap().send(Job::Key { key, mods }).map_err(|e| e.to_string())
}

/// Mostra ou esconde o teclado. Enquanto ele está aberto, a barra de navegação sai (o teclado
/// ocupa o rodapé, como no esboço) e volta quando ele fecha.
#[tauri::command]
pub fn keyboard_show(app: AppHandle, show: bool) -> Result<(), String> {
    use tauri::Manager;
    let handle = app.clone();
    // a janela na frente é do próprio LUMNA? (mesmo processo; vale também quando o teclado é aberto
    // pela barrinha, sem campo em foco)
    let ours = show
        && app
            .state::<crate::SharedCompositor>()
            .active_window()
            .ok()
            .flatten()
            .is_some_and(|w| w.pid == i64::from(std::process::id()));
    app.run_on_main_thread(move || {
        if !layer::exists("keyboard") {
            return;
        }
        if layer::is_visible("keyboard") == show {
            return;
        }
        // lumna = o teclado abriu num campo do LUMNA (senão é num app de fora: terminal, editor...)
        // ao fechar, vale o modo em que ele abriu
        let lumna = if show { TARGET.lock().unwrap().is_some() || ours } else { OVERLAY.load(std::sync::atomic::Ordering::Relaxed) };
        OVERLAY.store(show && lumna, std::sync::atomic::Ordering::Relaxed);
        // Nos campos do LUMNA o teclado fica por cima, sem reservar espaço: a janela não muda de
        // tamanho (cada redimensionamento faz o WebKit redesenhar = flick) e a própria página
        // encolhe por dentro (lumna://keyboard). Apps de fora não sabem fazer isso: pra eles o
        // teclado reserva o espaço, como antes.
        if show {
            layer::set_exclusive("keyboard", if lumna { layer::Exclusive::None } else { layer::Exclusive::Auto });
        }
        layer::set_visible("keyboard", show);
        if !lumna {
            layer::set_visible("mobile-nav", !show);
        }
        let _ = handle.emit("lumna://keyboard", json!({ "shown": show, "lumna": lumna, "height": 392 }));
    })
    .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn keyboard_toggle(app: AppHandle) -> Result<(), String> {
    let shown = layer::is_visible("keyboard");
    keyboard_show(app, !shown)
}
