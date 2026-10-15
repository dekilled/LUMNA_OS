//! Energia: sair da sessão, suspender, hibernar, reiniciar e desligar (pelo logind), e o perfil de
//! energia (power-profiles-daemon: economia, equilibrado, desempenho).
//!
//! O menu de energia é uma camada própria (`power`), aberta pelo botão da topbar, por
//! `Super + Shift + E` (`lumna-ctl power`) ou pela página Energia das Configurações.

use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};
use zbus::zvariant::{OwnedValue, Value};

use crate::{layer, MonitorPositions, SharedCompositor};

const LOGIND: &str = "org.freedesktop.login1";
const LOGIND_PATH: &str = "/org/freedesktop/login1";
const LOGIND_MANAGER: &str = "org.freedesktop.login1.Manager";
/// O power-profiles-daemon atende pelos dois nomes (o antigo ainda existe em versões velhas).
const PROFILES: [(&str, &str); 2] = [
    ("org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles"),
    ("net.hadess.PowerProfiles", "/net/hadess/PowerProfiles"),
];

#[derive(Debug, Clone, Serialize, Default)]
pub struct PowerState {
    pub can_suspend: bool,
    pub can_hibernate: bool,
    pub can_reboot: bool,
    pub can_power_off: bool,
    /// Perfil em uso (`power-saver`, `balanced`, `performance`), se o serviço estiver instalado.
    pub profile: Option<String>,
    pub profiles: Vec<String>,
}

/// `yes` e `challenge` (pede senha) contam como "pode"; `no` e `na` (sem suporte), não.
fn allowed(answer: &str) -> bool {
    matches!(answer, "yes" | "challenge")
}

async fn logind_can(conn: &zbus::Connection, method: &str) -> bool {
    let reply = conn.call_method(Some(LOGIND), LOGIND_PATH, Some(LOGIND_MANAGER), method, &()).await;
    reply.ok().and_then(|r| r.body().deserialize::<String>().ok()).is_some_and(|a| allowed(&a))
}

async fn profiles(conn: &zbus::Connection) -> Option<(String, Vec<String>, &'static str, &'static str)> {
    for (name, path) in PROFILES {
        let Ok(props) = zbus::fdo::PropertiesProxy::builder(conn).destination(name).ok()?.path(path).ok()?.build().await else {
            continue;
        };
        let Ok(iface) = zbus::names::InterfaceName::try_from(name) else { continue };
        let Ok(active) = props.get(iface.clone(), "ActiveProfile").await else { continue };
        let active = String::try_from(active).unwrap_or_default();
        let list = props
            .get(iface, "Profiles")
            .await
            .ok()
            .and_then(|v| Vec::<std::collections::HashMap<String, OwnedValue>>::try_from(v).ok())
            .map(|items| {
                items.iter().filter_map(|p| p.get("Profile").and_then(|v| String::try_from(v.clone()).ok())).collect()
            })
            .unwrap_or_default();
        return Some((active, list, name, path));
    }
    None
}

#[tauri::command]
pub async fn power_state() -> PowerState {
    let Ok(conn) = zbus::Connection::system().await else { return PowerState::default() };
    let (profile, profiles) = match profiles(&conn).await {
        Some((active, list, ..)) => (Some(active), list),
        None => (None, Vec::new()),
    };
    PowerState {
        can_suspend: logind_can(&conn, "CanSuspend").await,
        can_hibernate: logind_can(&conn, "CanHibernate").await,
        can_reboot: logind_can(&conn, "CanReboot").await,
        can_power_off: logind_can(&conn, "CanPowerOff").await,
        profile,
        profiles,
    }
}

#[tauri::command]
pub async fn power_set_profile(profile: String) -> Result<(), String> {
    let conn = zbus::Connection::system().await.map_err(|e| e.to_string())?;
    let (_, list, name, path) = profiles(&conn).await.ok_or("o perfil de energia não está disponível")?;
    if !list.contains(&profile) {
        return Err(format!("perfil desconhecido: {profile}"));
    }
    let props = zbus::fdo::PropertiesProxy::builder(&conn)
        .destination(name)
        .map_err(|e| e.to_string())?
        .path(path)
        .map_err(|e| e.to_string())?
        .build()
        .await
        .map_err(|e| e.to_string())?;
    let iface = zbus::names::InterfaceName::try_from(name).map_err(|e| e.to_string())?;
    props.set(iface, "ActiveProfile", Value::from(profile)).await.map_err(|e| e.to_string())
}

/// `lock`, `logout`, `suspend`, `hibernate`, `reboot` ou `poweroff`.
#[tauri::command]
pub async fn power_action(app: AppHandle, action: String) -> Result<(), String> {
    let method = match action.as_str() {
        "lock" => {
            hide_menu(&app);
            return crate::lock::lock_now(&app);
        }
        "logout" => {
            hide_menu(&app);
            return app.state::<SharedCompositor>().exit();
        }
        "suspend" => "Suspend",
        "hibernate" => "Hibernate",
        "reboot" => "Reboot",
        "poweroff" => "PowerOff",
        _ => return Err(format!("ação de energia desconhecida: {action}")),
    };
    hide_menu(&app);
    let conn = zbus::Connection::system().await.map_err(|e| e.to_string())?;
    // `true`: se precisar de senha (outra pessoa logada, por exemplo), o polkit pergunta.
    conn.call_method(Some(LOGIND), LOGIND_PATH, Some(LOGIND_MANAGER), method, &(true,))
        .await
        .map(|_| ())
        .map_err(|e| format!("não deu para {}: {e}", label(&action)))
}

fn label(action: &str) -> &'static str {
    match action {
        "suspend" => "suspender",
        "hibernate" => "hibernar",
        "reboot" => "reiniciar",
        "poweroff" => "desligar",
        _ => "sair",
    }
}

fn hide_menu(app: &AppHandle) {
    let _ = app.run_on_main_thread(|| layer::set_visible("power", false));
}

/// Abre o menu de energia no monitor em foco (ou no da janela que pediu), ou fecha se já estiver aberto.
pub fn toggle_menu(app: &AppHandle, at: Option<(i32, i32)>) -> Result<(), String> {
    let at = at.or_else(|| {
        let monitors = app.state::<SharedCompositor>().monitors().ok()?;
        monitors.into_iter().find(|m| m.focused).map(|m| (m.x, m.y))
    });
    let handle = app.clone();
    app.run_on_main_thread(move || {
        if layer::is_visible("power") {
            layer::set_visible("power", false);
            return;
        }
        if let Some((x, y)) = at {
            layer::move_to_monitor_at("power", x, y);
        }
        layer::set_visible("power", true);
        let _ = handle.emit_to("power", "lumna://power-opened", ());
    })
    .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn power_menu(app: AppHandle, window: tauri::WebviewWindow, positions: tauri::State<'_, MonitorPositions>) -> Result<(), String> {
    let at = positions.0.lock().unwrap().get(window.label()).copied();
    toggle_menu(&app, at)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn logind_answers() {
        assert!(allowed("yes"));
        assert!(allowed("challenge"));
        assert!(!allowed("no"));
        assert!(!allowed("na"));
        assert_eq!(label("poweroff"), "desligar");
    }
}
