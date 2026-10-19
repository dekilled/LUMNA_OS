//! Celular: o cabo USB num computador. Ao conectar (o computador "configurou" o celular), pergunta
//! se libera os arquivos; liberado, liga o MTP (`lumna-usb-arquivos.service`, ver
//! mobile/session/usb) e o computador vê o armazenamento no gerenciador de arquivos. Ao tirar o
//! cabo, desliga (como no Android: cada conexão pergunta de novo). Num carregador de tomada nada
//! acontece: ele não "configura" o celular. Com a tela bloqueada, a pergunta espera desbloquear.

use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

use serde::Serialize;
use tauri::{AppHandle, Emitter};

const SERVICE: &str = "lumna-usb-arquivos.service";

static FILES: AtomicBool = AtomicBool::new(false);
static CONNECTED: AtomicBool = AtomicBool::new(false);

/// O estado do controlador USB (`configured`: ligado num computador que reconheceu o celular).
fn udc_state() -> String {
    let Ok(dir) = std::fs::read_dir("/sys/class/udc") else { return String::new() };
    dir.flatten()
        .filter_map(|e| std::fs::read_to_string(e.path().join("state")).ok())
        .map(|s| s.trim().to_string())
        .next()
        .unwrap_or_default()
}

/// Num computador: reconhecido (ou com o computador dormindo, que suspende a USB).
fn in_computer(state: &str) -> bool {
    matches!(state, "configured" | "suspended")
}

fn service_active() -> bool {
    Command::new("systemctl").args(["is-active", "--quiet", SERVICE]).status().is_ok_and(|s| s.success())
}

fn systemctl(verb: &str) -> Result<(), String> {
    let out = Command::new("systemctl").args([verb, SERVICE]).stdin(Stdio::null()).output().map_err(|e| e.to_string())?;
    if out.status.success() {
        Ok(())
    } else {
        let why = String::from_utf8_lossy(&out.stderr).trim().to_string();
        Err(if why.is_empty() { format!("systemctl {verb} falhou") } else { why })
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct UsbState {
    /// O cabo está num computador.
    pub connected: bool,
    /// Os arquivos estão liberados (MTP ligado).
    pub files: bool,
}

fn state() -> UsbState {
    UsbState { connected: CONNECTED.load(Ordering::SeqCst), files: FILES.load(Ordering::SeqCst) }
}

fn emit(app: &AppHandle) {
    let _ = app.emit("lumna://usb", state());
}

/// Liga ou desliga os arquivos pelo cabo.
fn set_files(app: &AppHandle, on: bool) -> Result<(), String> {
    systemctl(if on { "start" } else { "stop" })?;
    FILES.store(on && service_active(), Ordering::SeqCst);
    emit(app);
    Ok(())
}

fn ask(app: &AppHandle) {
    let handle = app.clone();
    crate::notifications::local_ask(
        app,
        "USB",
        "Cabo USB conectado",
        "Deixar este computador ver os arquivos do celular?",
        &[("arquivos", "Transferir arquivos"), ("carregar", "Só carregar")],
        move |key| {
            if key != "arquivos" {
                return;
            }
            let app = handle.clone();
            std::thread::spawn(move || match set_files(&app, true) {
                Ok(()) => crate::notifications::local(
                    &app,
                    "USB",
                    "Arquivos liberados",
                    "Abra o celular no gerenciador de arquivos do computador (OnePlus 6T).",
                    false,
                ),
                Err(err) => crate::notifications::local(&app, "USB", "Não deu pra liberar os arquivos", &err, true),
            });
        },
    );
}

/// Acompanha o cabo (de segundo em segundo: o estado do controlador não avisa por evento).
pub fn watch(app: AppHandle) {
    std::thread::spawn(move || {
        FILES.store(service_active(), Ordering::SeqCst);
        let mut asked = false;
        let mut away_since: Option<Instant> = None;
        loop {
            let here = in_computer(&udc_state());
            if here {
                away_since = None;
                if !CONNECTED.swap(true, Ordering::SeqCst) {
                    emit(&app);
                }
                // pergunta uma vez por conexão, e só desbloqueado
                if !asked && !FILES.load(Ordering::SeqCst) && !crate::mobilelock::is_locked() {
                    asked = true;
                    ask(&app);
                }
            } else {
                // ligar o MTP solta e prende a USB de novo: um instante fora não é "tirou o cabo"
                let since = *away_since.get_or_insert_with(Instant::now);
                if since.elapsed() > Duration::from_secs(4) && CONNECTED.swap(false, Ordering::SeqCst) {
                    asked = false;
                    if FILES.load(Ordering::SeqCst) {
                        let _ = set_files(&app, false);
                    }
                    emit(&app);
                }
            }
            std::thread::sleep(Duration::from_secs(1));
        }
    });
}

#[tauri::command]
pub fn usb_state() -> UsbState {
    state()
}

#[tauri::command]
pub async fn usb_files(app: AppHandle, on: bool) -> Result<(), String> {
    if on && crate::mobilelock::is_locked() {
        return Err("desbloqueie o celular primeiro".into());
    }
    tauri::async_runtime::spawn_blocking(move || set_files(&app, on)).await.map_err(|e| e.to_string())?
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn so_num_computador() {
        assert!(in_computer("configured") && in_computer("suspended"));
        assert!(!in_computer("not attached") && !in_computer("powered") && !in_computer(""));
    }
}

