//! Socket de controle: o `lumna-ctl` (chamado pelos atalhos do Hyprland) manda ordens por aqui.

use std::io::{Read, Write};
use std::os::unix::net::UnixListener;

use tauri::AppHandle;

use crate::modes::{Layout, Mode, Target};
use crate::LauncherAction;

/// Resposta com texto: `ok` na primeira linha e o resto para o `lumna-ctl` mostrar.
fn handle_with_output(app: &AppHandle, command: &str) -> Option<Result<String, String>> {
    if command == "actions" {
        let lines: Vec<String> = crate::actions::CATALOG.iter().map(|d| format!("{:<20} {}", d.name, d.label)).collect();
        return Some(Ok(lines.join("\n")));
    }
    // `action apps.open {"name":"firefox"}`: o JSON é opcional e vale o resto da linha.
    let rest = command.strip_prefix("action ")?;
    let (name, json) = rest.trim().split_once(' ').unwrap_or((rest.trim(), "{}"));
    let args: serde_json::Value = match serde_json::from_str(json) {
        Ok(v) => v,
        Err(e) => return Some(Err(format!("argumentos não são JSON: {e}"))),
    };
    let outcome = tauri::async_runtime::block_on(crate::actions::run(app.clone(), name, args));
    Some(if outcome.ok { Ok(outcome.content) } else { Err(outcome.summary) })
}

fn handle(app: &AppHandle, command: &str) -> Result<(), String> {
    // O caminho do Explorador pode ter espaços: vale o resto da linha.
    if command == "files" || command.starts_with("files ") {
        let path = command["files".len()..].trim();
        return crate::open_files(app, (!path.is_empty()).then(|| path.to_string()));
    }
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
        ["installer"] => crate::open_installer(app),
        ["power"] => crate::power::toggle_menu(app, None),
        // no celular, o bloqueio é o do LUMNA (o hyprlock esconderia o teclado da tela)
        ["lock", ..] | ["screen", _] | ["power-menu"] if crate::MOBILE.load(std::sync::atomic::Ordering::Relaxed) => {
            crate::mobilelock::control(app, &command.split_whitespace().collect::<Vec<_>>())
        }
        ["lock"] => crate::lock::lock_now(app),
        ["rest", "on"] => crate::rest::set(app, true),
        ["rest", "off"] => crate::rest::set(app, false),
        // Botões da barra de título (hyprbars): valem para a janela em foco.
        ["window", action @ ("minimize" | "maximize" | "close" | "fullscreen")] => {
            crate::window_control(app.clone(), None, action.to_string())
        }
        ["media", "home"] => crate::media_home(app).map(|_| ()),
        ["media", "menu"] => crate::media_menu(app).map(|_| ()),
        ["media", "open", service] => crate::media_open(app, None, service, None),
        ["emma"] | ["emma", "toggle"] => crate::emma_panel(app, LauncherAction::Toggle),
        ["emma", "open"] => crate::emma_panel(app, LauncherAction::Open),
        ["emma", "close"] => crate::emma_panel(app, LauncherAction::Close),
        ["store"] => crate::open_store(app, None),
        ["store", tab] => crate::open_store(app, Some(tab.to_string())),
        ["phone"] => crate::open_phone(app, None),
        ["remote"] => crate::open_remote(app, None),
        ["remote", pc] => crate::open_remote(app, Some(pc.to_string())),
        ["phone", tab] => crate::open_phone(app, Some(tab.to_string())),
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
            let command = command.trim();
            let reply = match handle_with_output(&app, command) {
                Some(Ok(text)) => format!("ok\n{text}"),
                Some(Err(err)) => err,
                None => match handle(&app, command) {
                    Ok(()) => "ok".to_string(),
                    Err(err) => err,
                },
            };
            let _ = stream.write_all(reply.as_bytes());
        }
    });
}
