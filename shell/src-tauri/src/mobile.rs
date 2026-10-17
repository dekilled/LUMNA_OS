//! Celular: a central (notificações e atalhos) e os apps recentes (o "RAM").
//!
//! Os dois são camadas na tela toda, escondidas até um gesto abrir: deslizar a barra de status
//! pra baixo abre a central; deslizar a navegação pra cima abre os recentes.
//!
//! Atalhos que o PC não tem: brilho e lanterna (pelo logind, sem root), dados móveis e modo avião
//! (pelo NetworkManager, que o polkit libera pro grupo wheel), não perturbe e rotação (só diz se
//! existe sensor; girar a tela ainda não está feito).

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};

use serde::Serialize;
use serde_json::json;
use tauri::{AppHandle, Emitter, Manager};

use crate::{layer, SharedCompositor};

/// Não perturbe do celular: os avisos vão direto pra central, sem aparecer por cima.
static QUIET: AtomicBool = AtomicBool::new(false);

pub fn quiet() -> bool {
    QUIET.load(Ordering::Relaxed)
}

// ---- painéis ----

const PANELS: [&str; 2] = ["mobile-center", "mobile-recents"];

/// Abre, fecha (ou alterna, sem `show`) a central ou os recentes. Um fecha o outro, e o teclado
/// sai da frente. O painel recebe `lumna://mobile-panel` pra se atualizar ao abrir.
#[tauri::command]
pub fn mobile_panel(app: AppHandle, name: String, show: Option<bool>) -> Result<(), String> {
    if !PANELS.contains(&name.as_str()) {
        return Err(format!("painel desconhecido: {name}"));
    }
    let handle = app.clone();
    app.run_on_main_thread(move || {
        let show = show.unwrap_or(!layer::is_visible(&name));
        if show {
            for other in PANELS.iter().filter(|p| **p != name) {
                layer::set_visible(other, false);
            }
            if layer::is_visible("keyboard") {
                layer::set_visible("keyboard", false);
                layer::set_visible("mobile-nav", true);
                let _ = handle.emit("lumna://keyboard", json!({ "shown": false, "lumna": false }));
            }
        }
        layer::set_visible(&name, show);
        let _ = handle.emit_to(name.as_str(), "lumna://mobile-panel", show);
    })
    .map_err(|e| e.to_string())
}

// ---- apps recentes ----

#[derive(Debug, Clone, Serialize)]
pub struct Recent {
    pub address: String,
    pub class: String,
    pub title: String,
    pub pid: i64,
    /// Memória do app (o processo e os filhos), em KiB. `None` = janela do próprio LUMNA.
    pub memory_kb: Option<u64>,
    /// Última imagem da janela (jpg no cache), se já houve uma.
    pub shot: Option<String>,
    pub shot_at: u64,
    pub focus_history: i64,
}

#[derive(Debug, Clone, Serialize)]
pub struct Recents {
    pub apps: Vec<Recent>,
    pub total_kb: u64,
    pub available_kb: u64,
}

fn shots_dir() -> PathBuf {
    let cache = std::env::var_os("XDG_CACHE_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".cache")))
        .unwrap_or_else(|| PathBuf::from("/tmp"));
    cache.join("lumna/recents")
}

fn shot_path(address: &str) -> PathBuf {
    shots_dir().join(format!("{}.jpg", address.trim_start_matches("0x")))
}

/// Pai de cada processo (de `/proc/<pid>/stat`).
fn parents() -> HashMap<i64, i64> {
    let mut map = HashMap::new();
    let Ok(entries) = std::fs::read_dir("/proc") else { return map };
    for entry in entries.flatten() {
        let Ok(pid) = entry.file_name().to_string_lossy().parse::<i64>() else { continue };
        let Ok(stat) = std::fs::read_to_string(entry.path().join("stat")) else { continue };
        // "pid (nome com espaços) estado ppid ...": o nome pode ter parênteses, então parte do último
        if let Some(ppid) = stat.rsplit_once(')').and_then(|(_, rest)| rest.split_whitespace().nth(1)?.parse().ok()) {
            map.insert(pid, ppid);
        }
    }
    map
}

/// O processo e todos os descendentes.
fn tree(root: i64, parents: &HashMap<i64, i64>) -> Vec<i64> {
    let mut out = vec![root];
    let mut i = 0;
    while i < out.len() {
        let me = out[i];
        out.extend(parents.iter().filter(|(_, p)| **p == me).map(|(c, _)| *c));
        i += 1;
    }
    out
}

/// Memória proporcional (Pss: a parte compartilhada dividida entre quem usa), em KiB.
fn pss_kb(pid: i64) -> u64 {
    let text = std::fs::read_to_string(format!("/proc/{pid}/smaps_rollup"))
        .or_else(|_| std::fs::read_to_string(format!("/proc/{pid}/status")))
        .unwrap_or_default();
    let key = if text.contains("Pss:") { "Pss:" } else { "VmRSS:" };
    text.lines()
        .find(|l| l.starts_with(key))
        .and_then(|l| l.split_whitespace().nth(1)?.parse().ok())
        .unwrap_or(0)
}

fn meminfo() -> (u64, u64) {
    let text = std::fs::read_to_string("/proc/meminfo").unwrap_or_default();
    let get = |key: &str| {
        text.lines()
            .find(|l| l.starts_with(key))
            .and_then(|l| l.split_whitespace().nth(1)?.parse().ok())
            .unwrap_or(0)
    };
    (get("MemTotal:"), get("MemAvailable:"))
}

fn mtime(path: &Path) -> u64 {
    path.metadata()
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
        .map_or(0, |d| d.as_secs())
}

#[tauri::command]
pub fn recents(app: AppHandle, compositor: tauri::State<'_, SharedCompositor>) -> Result<Recents, String> {
    let own = i64::from(std::process::id());
    let parents = parents();
    let _ = app.asset_protocol_scope().allow_directory(shots_dir(), false);
    let mut apps: Vec<Recent> = compositor
        .windows()?
        .into_iter()
        .map(|w| {
            let shot = shot_path(&w.address);
            Recent {
                memory_kb: (w.pid > 0 && w.pid != own).then(|| tree(w.pid, &parents).into_iter().map(pss_kb).sum()),
                shot_at: mtime(&shot),
                shot: shot.exists().then(|| shot.to_string_lossy().into_owned()),
                address: w.address,
                class: w.class,
                title: w.title,
                pid: w.pid,
                focus_history: w.focus_history,
            }
        })
        .collect();
    apps.sort_by_key(|a| a.focus_history);
    let (total_kb, available_kb) = meminfo();
    Ok(Recents { apps, total_kb, available_kb })
}

/// Guarda a imagem das janelas que estão na tela agora (antes de sumirem pros recentes).
/// O Hyprland não deixa capturar janela escondida, então a foto é tirada enquanto ela aparece.
#[tauri::command]
pub async fn recents_snapshot(compositor: tauri::State<'_, SharedCompositor>) -> Result<(), String> {
    let monitors = compositor.monitors()?;
    let visible: Vec<_> = compositor
        .windows()?
        .into_iter()
        .filter(|w| !w.minimized() && monitors.iter().any(|m| m.active_workspace == w.workspace))
        .collect();
    std::fs::create_dir_all(shots_dir()).map_err(|e| e.to_string())?;
    for w in visible {
        let geometry = format!("{},{} {}x{}", w.x, w.y, w.width, w.height);
        let target = shot_path(&w.address);
        let tmp = target.with_extension("tmp");
        let ok = Command::new("grim")
            .args(["-g", &geometry, "-s", "1", "-t", "jpeg", "-q", "70"])
            .arg(&tmp)
            .status()
            .map_err(|e| format!("grim: {e}"))?
            .success();
        if ok {
            let _ = std::fs::rename(&tmp, &target);
        }
    }
    Ok(())
}

/// Forçar parada: mata o app e os processos filhos (nunca o próprio LUMNA).
#[tauri::command]
pub fn recents_kill(compositor: tauri::State<'_, SharedCompositor>, address: String) -> Result<(), String> {
    let window = compositor.windows()?.into_iter().find(|w| w.address == address).ok_or("a janela já fechou")?;
    if window.pid <= 1 || window.pid == i64::from(std::process::id()) {
        return Err("essa janela é do próprio LUMNA".into());
    }
    for pid in tree(window.pid, &parents()).into_iter().rev() {
        // SAFETY: kill só manda um sinal; pid vem do /proc.
        unsafe { libc::kill(pid as libc::pid_t, libc::SIGKILL) };
    }
    let _ = std::fs::remove_file(shot_path(&address));
    Ok(())
}

// ---- atalhos ----

#[derive(Debug, Clone, Serialize, Default)]
pub struct Quick {
    /// 0-100, `None` sem tela com brilho ajustável.
    pub brightness: Option<u8>,
    /// `None` = o aparelho não tem LED de lanterna que o kernel mostre.
    pub flashlight: Option<bool>,
    /// `None` = sem modem ou sem conexão de dados configurada.
    pub mobile_data: Option<bool>,
    /// "4G", "3G"...
    pub mobile_tech: Option<String>,
    pub airplane: bool,
    /// Há acelerômetro? (girar a tela ainda não está feito)
    pub rotation_sensor: bool,
    pub quiet: bool,
}

fn first_entry(dir: &str, pick: impl Fn(&str) -> bool) -> Option<PathBuf> {
    let mut found: Vec<PathBuf> = std::fs::read_dir(dir)
        .ok()?
        .flatten()
        .filter(|e| pick(&e.file_name().to_string_lossy()))
        .map(|e| e.path())
        .collect();
    found.sort();
    found.into_iter().next()
}

fn read_u32(path: &Path) -> Option<u32> {
    std::fs::read_to_string(path).ok()?.trim().parse().ok()
}

fn backlight() -> Option<PathBuf> {
    first_entry("/sys/class/backlight", |_| true)
}

fn torch() -> Option<PathBuf> {
    first_entry("/sys/class/leds", |n| {
        let n = n.to_lowercase();
        n.contains("flash") || n.contains("torch")
    })
}

fn nmcli(args: &[&str]) -> Result<String, String> {
    let out = Command::new("nmcli").args(args).output().map_err(|e| format!("nmcli: {e}"))?;
    if out.status.success() {
        Ok(String::from_utf8_lossy(&out.stdout).into_owned())
    } else {
        Err(String::from_utf8_lossy(&out.stderr).trim().to_string())
    }
}

/// Conexões de dados móveis (tipo gsm) e se alguma está ativa.
fn gsm_connections() -> Vec<(String, bool)> {
    nmcli(&["-t", "-f", "NAME,TYPE,ACTIVE", "connection", "show"])
        .unwrap_or_default()
        .lines()
        .filter_map(|l| {
            // NAME pode ter ":" escapado como "\:"; os dois últimos campos não têm
            let mut parts = l.rsplitn(3, ':');
            let active = parts.next()? == "yes";
            let kind = parts.next()?;
            let name = parts.next()?.replace("\\:", ":");
            (kind == "gsm").then_some((name, active))
        })
        .collect()
}

fn mobile_tech() -> Option<String> {
    let out = Command::new("mmcli").args(["-m", "any", "-K"]).output().ok()?;
    let text = String::from_utf8_lossy(&out.stdout);
    let tech = text
        .lines()
        .find(|l| l.starts_with("modem.generic.access-technologies.value[1]"))?
        .split(':')
        .nth(1)?
        .trim()
        .to_lowercase();
    Some(
        match tech.as_str() {
            "5gnr" => "5G",
            "lte" => "4G",
            "umts" | "hsdpa" | "hsupa" | "hspa" | "hspa-plus" | "evdo0" | "evdoa" | "evdob" => "3G",
            "gsm" | "gprs" | "edge" | "1xrtt" => "2G",
            _ => return None,
        }
        .to_string(),
    )
}

#[tauri::command]
pub fn quick_state() -> Quick {
    let brightness = backlight().and_then(|b| {
        let max = read_u32(&b.join("max_brightness"))?.max(1);
        Some((read_u32(&b.join("brightness"))? * 100 / max) as u8)
    });
    let flashlight = torch().and_then(|t| read_u32(&t.join("brightness"))).map(|v| v > 0);
    let gsm = gsm_connections();
    let radios = nmcli(&["-t", "-f", "WIFI,WWAN", "radio"]).unwrap_or_default();
    let airplane = radios.trim() == "disabled:disabled";
    let rotation_sensor = std::fs::read_dir("/sys/bus/iio/devices")
        .map(|d| d.flatten().any(|e| e.path().join("in_accel_x_raw").exists()))
        .unwrap_or(false);
    Quick {
        brightness,
        flashlight,
        mobile_data: (!gsm.is_empty()).then(|| gsm.iter().any(|(_, a)| *a)),
        mobile_tech: if gsm.is_empty() { None } else { mobile_tech() },
        airplane,
        rotation_sensor,
        quiet: quiet(),
    }
}

/// Muda o brilho de um dispositivo do sysfs pelo logind (a sessão ativa pode, sem root).
async fn logind_brightness(subsystem: &str, path: &Path, value: u32) -> Result<(), String> {
    let name = path.file_name().ok_or("dispositivo sem nome")?.to_string_lossy().into_owned();
    let conn = zbus::Connection::system().await.map_err(|e| e.to_string())?;
    conn.call_method(
        Some("org.freedesktop.login1"),
        "/org/freedesktop/login1/session/auto",
        Some("org.freedesktop.login1.Session"),
        "SetBrightness",
        &(subsystem, name.as_str(), value),
    )
    .await
    .map(|_| ())
    .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn quick_brightness(percent: u8) -> Result<(), String> {
    let b = backlight().ok_or("sem controle de brilho")?;
    let max = read_u32(&b.join("max_brightness")).unwrap_or(255).max(1);
    // nunca apaga a tela de vez: 2% no mínimo
    let value = (u32::from(percent.clamp(2, 100)) * max).div_ceil(100);
    logind_brightness("backlight", &b, value).await
}

#[tauri::command]
pub async fn quick_flashlight(on: bool) -> Result<(), String> {
    let t = torch().ok_or("o aparelho não mostrou o LED da lanterna")?;
    let max = read_u32(&t.join("max_brightness")).unwrap_or(1).max(1);
    logind_brightness("leds", &t, if on { max } else { 0 }).await
}

#[tauri::command]
pub async fn quick_mobile_data(on: bool) -> Result<(), String> {
    let gsm = gsm_connections();
    let (name, _) = gsm.first().ok_or("nenhuma conexão de dados móveis configurada")?;
    nmcli(&["connection", if on { "up" } else { "down" }, "id", name]).map(|_| ())
}

/// Modo avião: desliga Wi-Fi e modem (o NetworkManager põe o modem em baixo consumo).
/// O Bluetooth a central desliga pelo módulo dele.
#[tauri::command]
pub async fn quick_airplane(on: bool) -> Result<(), String> {
    nmcli(&["radio", "all", if on { "off" } else { "on" }]).map(|_| ())
}

#[tauri::command]
pub fn quick_quiet(on: bool) {
    QUIET.store(on, Ordering::Relaxed);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_arvore_inclui_netos() {
        let parents: HashMap<i64, i64> = [(10, 1), (11, 10), (12, 11), (20, 1)].into();
        let mut t = tree(10, &parents);
        t.sort();
        assert_eq!(t, vec![10, 11, 12]);
    }
}
