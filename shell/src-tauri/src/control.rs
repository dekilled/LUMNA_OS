//! Socket de controle: o `lumna-ctl` (chamado pelos atalhos do Hyprland) manda ordens por aqui.

use std::io::{Read, Write};
use std::os::unix::net::UnixListener;

use tauri::AppHandle;

use crate::modes::{Layout, Mode, Target};
use crate::LauncherAction;

fn handle(app: &AppHandle, command: &str) -> Result<(), String> {
    match command.split_whitespace().collect::<Vec<_>>().as_slice() {
        ["launcher", "toggle"] => crate::launcher(app, LauncherAction::Toggle),
        ["launcher", "open"] => crate::launcher(app, LauncherAction::Open),
        ["launcher", "close"] => crate::launcher(app, LauncherAction::Close),
        ["mode", "next"] => crate::switch_mode(app, Target::Next),
        ["mode", "prev"] => crate::switch_mode(app, Target::Prev),
        ["mode", "layout", "per-screen"] => crate::set_layout(app, Layout::PerScreen),
        ["mode", "layout", "extend"] => crate::set_layout(app, Layout::Extend),
        ["mode", name] => {
            let mode = Mode::parse(name).ok_or_else(|| format!("modo desconhecido: {name}"))?;
            crate::switch_mode(app, Target::Set(mode))
        }
        ["pad", name] => {
            let action = crate::pad::Action::parse(name).ok_or_else(|| format!("ação desconhecida: {name}"))?;
            crate::pad_action(app, action);
            Ok(())
        }
        ["settings"] => crate::open_settings(app, None),
        ["settings", page] => crate::open_settings(app, Some(page.to_string())),
        _ => Err(format!("comando desconhecido: {command:?}")),
    }
}

pub fn listen(app: AppHandle) {
    let Some(path) = lumna_core::control_socket_path() else {
        eprintln!("lumna-shell: sem XDG_RUNTIME_DIR, o lumna-ctl não vai funcionar");
        return;
    };
    if let Some(dir) = path.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    // Um socket velho (de um shell que caiu) impede o bind.
    let _ = std::fs::remove_file(&path);
    let listener = match UnixListener::bind(&path) {
        Ok(listener) => listener,
        Err(err) => {
            eprintln!("lumna-shell: {}: {err}", path.display());
            return;
        }
    };
    std::thread::spawn(move || {
        for stream in listener.incoming().flatten() {
            let mut stream = stream;
            let mut command = String::new();
            if stream.read_to_string(&mut command).is_err() {
                continue;
            }
            let reply = match handle(&app, command.trim()) {
                Ok(()) => "ok".to_string(),
                Err(err) => err,
            };
            let _ = stream.write_all(reply.as_bytes());
        }
    });
}
