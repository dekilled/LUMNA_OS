//! Celular: a central (notificações e atalhos) e os apps recentes (o "RAM").
//!
//! Os dois são camadas na tela toda, escondidas até um gesto abrir: deslizar a barra de status
//! pra baixo abre a central; deslizar a navegação pra cima abre os recentes.
//!
//! Atalhos que o PC não tem: brilho e lanterna (pelo logind, sem root), dados móveis e modo avião
//! (pelo NetworkManager, que o polkit libera pro grupo wheel), não perturbe e rotação (só diz se
//! existe sensor; girar a tela ainda não está feito).
//!
//! Os comandos que leem o /proc ou chamam nmcli/mmcli são assíncronos: comando síncrono do Tauri
//! roda na thread da interface, e cada consulta congelava as animações de todas as camadas.

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
/// Celular: um app por vez, com animação. Mover uma janela do esconderijo (minimizada) pra tela
/// não tem animação no Hyprland; trocar de área de trabalho tem (a "Minimizar e áreas" do modo
/// desenvolvedor). Então: o app vai, sem ninguém ver, pra uma área vazia, e o LUMNA troca pra ela;
/// pra Início, troca pra uma área vazia. Só depois o que ficou pra trás é guardado, cada janela no
/// seu esconderijo (nunca redimensiona). `address` vazio = Início.
#[tauri::command]
pub async fn mobile_show(compositor: tauri::State<'_, SharedCompositor>, address: Option<String>) -> Result<(), String> {
    let compositor = compositor.inner().clone();
    tauri::async_runtime::spawn_blocking(move || show_app(compositor.as_ref(), address.as_deref()))
        .await
        .map_err(|e| e.to_string())?
}

fn show_app(compositor: &dyn lumna_core::Compositor, address: Option<&str>) -> Result<(), String> {
    let monitors = compositor.monitors()?;
    let current = monitors.iter().find(|m| m.focused).or(monitors.first()).ok_or("nenhum monitor")?.active_workspace;
    let windows = compositor.windows()?;
    let on_screen = |ws: i64| windows.iter().any(|w| !w.minimized() && w.workspace == ws);
    // uma área sem janela nenhuma (nem a atual)
    let empty = (1..=9).find(|id| *id != current && !windows.iter().any(|w| w.workspace == *id)).unwrap_or(9);
    let shown = match address {
        None => {
            if !on_screen(current) {
                None
            } else {
                compositor.focus_workspace(empty)?;
                None
            }
        }
        Some(address) => {
            let w = windows.iter().find(|w| w.address == address).ok_or("a janela já fechou")?;
            let alone = !w.minimized() && w.workspace == current && windows.iter().filter(|o| !o.minimized() && o.workspace == current).count() == 1;
            if !alone {
                compositor.move_window(address, &empty.to_string())?;
                compositor.focus_workspace(empty)?;
            }
            compositor.focus_window(address)?;
            Some(address.to_string())
        }
    };
    // o resto vai pro esconderijo, depois que a animação da troca terminou (no meio dela, o app
    // que está saindo sumiria de repente)
    std::thread::sleep(std::time::Duration::from_millis(450));
    for w in compositor.windows()? {
        if !w.minimized() && Some(&w.address) != shown.as_ref() {
            compositor.move_window(&w.address, &lumna_core::minimized_workspace(&w.address))?;
        }
    }
    Ok(())
}

/// Mostra as faixas das bordas (o gesto de voltar pra Início) só enquanto um app está aberto.
#[tauri::command]
pub fn mobile_edges(app: AppHandle, show: bool) {
    let _ = app.run_on_main_thread(move || {
        layer::set_visible("mobile-edge-left", show);
        layer::set_visible("mobile-edge-right", show);
    });
}

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
pub async fn recents(app: AppHandle, compositor: tauri::State<'_, SharedCompositor>) -> Result<Recents, String> {
    let compositor = compositor.inner().clone();
    let _ = app.asset_protocol_scope().allow_directory(shots_dir(), false);
    tauri::async_runtime::spawn_blocking(move || recents_now(&compositor)).await.map_err(|e| e.to_string())?
}

fn recents_now(compositor: &SharedCompositor) -> Result<Recents, String> {
    let own = i64::from(std::process::id());
    let parents = parents();
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
pub async fn recents_kill(compositor: tauri::State<'_, SharedCompositor>, address: String) -> Result<(), String> {
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
pub async fn quick_state() -> Quick {
    tauri::async_runtime::spawn_blocking(quick_now).await.unwrap_or_default()
}

fn quick_now() -> Quick {
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

// ---- dados móveis (página dos Ajustes) ----

/// Saída `mmcli -m any -K` como chave → valor.
fn mmcli_keys() -> HashMap<String, String> {
    let Ok(out) = Command::new("mmcli").args(["-m", "any", "-K"]).output() else { return HashMap::new() };
    String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter_map(|l| l.split_once(':'))
        .map(|(k, v)| (k.trim().to_string(), v.trim().to_string()))
        .filter(|(_, v)| v != "--")
        .collect()
}

#[derive(Debug, Clone, Serialize)]
pub struct ModeOption {
    /// "2g|3g|4g:4g" (permitidos:preferido)
    pub id: String,
    pub label: String,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct DataConnection {
    pub name: String,
    pub apn: String,
    pub roaming: bool,
    pub active: bool,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct MobileData {
    pub modem: bool,
    pub operator: String,
    pub state: String,
    pub tech: Option<String>,
    pub signal: Option<u8>,
    pub number: String,
    pub connection: Option<DataConnection>,
    pub modes: Vec<ModeOption>,
    pub mode: String,
    /// bytes usados no mês (contados pelo LUMNA desde que ele começou a contar)
    pub usage: u64,
    pub month: String,
}

/// "allowed: 2g, 3g, 4g; preferred: 4g" → "2g|3g|4g:4g"
fn mode_id(text: &str) -> Option<String> {
    let allowed = text.split(';').next()?.trim().strip_prefix("allowed:")?.trim().replace(", ", "|");
    let preferred = text.split(';').nth(1).and_then(|p| p.trim().strip_prefix("preferred:")).map(str::trim).unwrap_or("none");
    Some(format!("{allowed}:{preferred}"))
}

fn mode_label(id: &str) -> String {
    let (allowed, preferred) = id.split_once(':').unwrap_or((id, "none"));
    let mut gens: Vec<String> = allowed.split('|').map(|g| g.to_uppercase()).collect();
    gens.sort_by(|a, b| b.cmp(a));
    match (gens.len(), preferred) {
        (1, _) => format!("Só {}", gens[0]),
        (_, "none") => format!("{} automático", gens.join(" / ")),
        // preferir a geração mais nova é o "automático" dos celulares
        (_, p) if p.to_uppercase() == gens[0] => format!("{} automático", gens.join(" / ")),
        (_, p) => format!("{} (prefere {})", gens.join(" / "), p.to_uppercase()),
    }
}

#[tauri::command]
pub async fn mobile_data_info() -> MobileData {
    tauri::async_runtime::spawn_blocking(|| {
        let keys = mmcli_keys();
        let gsm = gsm_connections();
        let connection = gsm.first().map(|(name, active)| {
            let fields = nmcli(&["-g", "gsm.apn,gsm.home-only", "connection", "show", name]).unwrap_or_default();
            let mut lines = fields.lines();
            DataConnection {
                name: name.clone(),
                apn: lines.next().unwrap_or_default().to_string(),
                roaming: lines.next().unwrap_or_default() != "yes",
                active: *active,
            }
        });
        let mut modes: Vec<ModeOption> = keys
            .iter()
            .filter(|(k, _)| k.starts_with("modem.generic.supported-modes.value"))
            .filter_map(|(_, v)| mode_id(v))
            .map(|id| ModeOption { label: mode_label(&id), id })
            .collect();
        modes.sort_by(|a, b| b.id.len().cmp(&a.id.len()).then(b.id.cmp(&a.id)));
        modes.dedup_by(|a, b| a.id == b.id);
        let (usage, month) = {
            let _guard = USAGE_LOCK.lock();
            usage_now()
        };
        MobileData {
            modem: !keys.is_empty(),
            operator: keys.get("modem.3gpp.operator-name").cloned().unwrap_or_default(),
            state: keys.get("modem.generic.state").cloned().unwrap_or_default(),
            tech: mobile_tech(),
            signal: keys.get("modem.generic.signal-quality.value").and_then(|v| v.parse().ok()),
            number: keys.get("modem.generic.own-numbers.value[1]").cloned().unwrap_or_default(),
            connection,
            mode: keys.get("modem.generic.current-modes").and_then(|m| mode_id(m)).unwrap_or_default(),
            modes,
            usage,
            month,
        }
    })
    .await
    .unwrap_or_default()
}

/// Muda um ajuste: `apn`, `roaming` ("on"/"off") ou `modes` ("2g|3g|4g:4g").
#[tauri::command]
pub async fn mobile_data_set(key: String, value: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let gsm = gsm_connections();
        let conn = || gsm.first().cloned().ok_or_else(|| "nenhuma conexão de dados móveis configurada".to_string());
        match key.as_str() {
            "apn" => {
                let (name, active) = conn()?;
                nmcli(&["connection", "modify", "id", &name, "gsm.apn", value.trim()])?;
                if active {
                    nmcli(&["connection", "up", "id", &name])?;
                }
                Ok(())
            }
            "roaming" => {
                let (name, _) = conn()?;
                nmcli(&["connection", "modify", "id", &name, "gsm.home-only", if value == "on" { "no" } else { "yes" }])
                    .map(|_| ())
            }
            "modes" => {
                let (allowed, preferred) = value.split_once(':').unwrap_or((&value, "none"));
                let mut args = vec!["-m".to_string(), "any".into(), format!("--set-allowed-modes={allowed}")];
                if preferred != "none" {
                    args.push(format!("--set-preferred-mode={preferred}"));
                }
                let out = Command::new("mmcli").args(&args).output().map_err(|e| format!("mmcli: {e}"))?;
                if out.status.success() {
                    Ok(())
                } else {
                    Err(String::from_utf8_lossy(&out.stderr).trim().to_string())
                }
            }
            _ => Err(format!("ajuste desconhecido: {key}")),
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

// ---- uso de dados ----
// O kernel conta os bytes da interface de dados (rmnet...), mas zera a cada boot ou reconexão.
// A cada minuto o LUMNA soma o que andou desde a última leitura num total do mês.

#[derive(Debug, Clone, Serialize, serde::Deserialize, Default)]
struct Usage {
    month: String,
    bytes: u64,
    iface: String,
    boot: String,
    counter: u64,
}

fn usage_file() -> PathBuf {
    let base = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".local/share")))
        .unwrap_or_else(|| PathBuf::from("/tmp"));
    base.join("lumna/phone/data-usage.json")
}

fn this_month() -> String {
    Command::new("date").arg("+%Y-%m").output().map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string()).unwrap_or_default()
}

/// Interface IP da conexão de dados ativa.
fn data_iface() -> Option<String> {
    let devices = nmcli(&["-t", "-f", "DEVICE,TYPE,STATE", "device"]).ok()?;
    let device = devices.lines().find_map(|l| {
        let mut parts = l.split(':');
        let (dev, kind, state) = (parts.next()?, parts.next()?, parts.next()?);
        (kind == "gsm" && state == "connected").then(|| dev.to_string())
    })?;
    let iface = nmcli(&["-g", "GENERAL.IP-IFACE", "device", "show", &device]).ok()?.trim().to_string();
    (!iface.is_empty()).then_some(iface)
}

/// Soma o que andou desde a última leitura e devolve (bytes do mês, mês).
fn usage_now() -> (u64, String) {
    let path = usage_file();
    let mut usage: Usage = std::fs::read_to_string(&path).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default();
    let month = this_month();
    if usage.month != month {
        usage = Usage { month: month.clone(), ..Default::default() };
    }
    if let Some(iface) = data_iface() {
        let stat = |name: &str| read_u32_64(&PathBuf::from(format!("/sys/class/net/{iface}/statistics/{name}")));
        let counter = stat("rx_bytes") + stat("tx_bytes");
        let boot = std::fs::read_to_string("/proc/sys/kernel/random/boot_id").unwrap_or_default().trim().to_string();
        let same = usage.iface == iface && usage.boot == boot && counter >= usage.counter;
        usage.bytes += if same { counter - usage.counter } else { counter };
        usage.iface = iface;
        usage.boot = boot;
        usage.counter = counter;
    } else {
        // sem conexão: a próxima conta começa do zero da interface nova
        usage.iface.clear();
    }
    if let Some(dir) = path.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    let _ = std::fs::write(&path, serde_json::to_vec(&usage).unwrap_or_default());
    (usage.bytes, usage.month)
}

fn read_u32_64(path: &Path) -> u64 {
    std::fs::read_to_string(path).ok().and_then(|t| t.trim().parse().ok()).unwrap_or(0)
}

static USAGE_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

/// Conta o uso de dados a cada minuto, em segundo plano.
pub fn track_data_usage() {
    std::thread::spawn(|| loop {
        {
            let _guard = USAGE_LOCK.lock();
            usage_now();
        }
        std::thread::sleep(std::time::Duration::from_secs(60));
    });
}

// ---- energia (bateria) ----

#[derive(Debug, Clone, Serialize, Default)]
pub struct Battery {
    pub percent: Option<u8>,
    /// Charging, Discharging, Full, Not charging
    pub status: String,
    /// capacidade atual em relação à de fábrica (%)
    pub health: Option<u8>,
    pub temperature: Option<f32>,
    pub voltage: Option<f32>,
    /// potência em watts (positiva carregando)
    pub power: Option<f32>,
    pub cycles: Option<u32>,
    pub technology: String,
}

fn read_i64(dir: &Path, name: &str) -> Option<i64> {
    std::fs::read_to_string(dir.join(name)).ok()?.trim().parse().ok()
}

#[tauri::command]
pub fn battery_info() -> Battery {
    let Some(dir) = first_entry("/sys/class/power_supply", |n| {
        std::fs::read_to_string(format!("/sys/class/power_supply/{n}/type")).is_ok_and(|t| t.trim() == "Battery")
    }) else {
        return Battery::default();
    };
    let text = |name: &str| std::fs::read_to_string(dir.join(name)).map(|t| t.trim().to_string()).unwrap_or_default();
    let full = read_i64(&dir, "charge_full").or_else(|| read_i64(&dir, "energy_full"));
    let design = read_i64(&dir, "charge_full_design").or_else(|| read_i64(&dir, "energy_full_design"));
    let status = text("status");
    let current = read_i64(&dir, "current_now");
    let volts = read_i64(&dir, "voltage_now").map(|v| v as f32 / 1_000_000.0);
    Battery {
        percent: read_i64(&dir, "capacity").map(|c| c.clamp(0, 100) as u8),
        health: full.zip(design).filter(|(_, d)| *d > 0).map(|(f, d)| ((f * 100 / d).clamp(0, 100)) as u8),
        temperature: read_i64(&dir, "temp").map(|t| t as f32 / 10.0),
        voltage: volts,
        power: current.zip(volts).map(|(c, v)| {
            let watts = (c as f32 / 1_000_000.0 * v).abs();
            if status == "Charging" { watts } else { -watts }
        }),
        cycles: read_i64(&dir, "cycle_count").filter(|c| *c > 0).map(|c| c as u32),
        technology: text("technology"),
        status,
    }
}

// ---- perfil ----
// O nome que o LUMNA mostra fica em ~/.config/lumna/profile.json; a foto é o ~/.face (o lugar
// que as telas de login do Linux já procuram).

#[derive(Debug, Clone, Serialize, Default)]
pub struct Profile {
    pub name: String,
    pub user: String,
    pub host: String,
    pub photo: Option<String>,
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

fn profile_file() -> PathBuf {
    home().join(".config/lumna/profile.json")
}

#[tauri::command]
pub fn profile_get(app: AppHandle) -> Profile {
    let user = std::env::var("USER").unwrap_or_default();
    let saved: serde_json::Value =
        std::fs::read_to_string(profile_file()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default();
    // sem nome salvo: o nome completo do /etc/passwd (campo GECOS), senão o usuário
    let gecos = std::fs::read_to_string("/etc/passwd").ok().and_then(|p| {
        p.lines()
            .find(|l| l.starts_with(&format!("{user}:")))
            .and_then(|l| l.split(':').nth(4))
            .map(|g| g.split(',').next().unwrap_or("").to_string())
    });
    let face = home().join(".face");
    let photo = face.exists().then(|| {
        let _ = app.asset_protocol_scope().allow_file(&face);
        face.to_string_lossy().to_string()
    });
    Profile {
        name: saved["name"].as_str().map(str::to_string).or(gecos).filter(|n| !n.is_empty()).unwrap_or_else(|| user.clone()),
        host: std::fs::read_to_string("/etc/hostname").map(|h| h.trim().to_string()).unwrap_or_default(),
        user,
        photo,
    }
}

#[tauri::command]
pub fn profile_set_name(name: String) -> Result<(), String> {
    let file = profile_file();
    if let Some(dir) = file.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(file, serde_json::to_vec(&json!({ "name": name.trim() })).unwrap_or_default()).map_err(|e| e.to_string())
}

/// Foto do perfil: copia a imagem escolhida (da pasta pessoal) pro ~/.face.
#[tauri::command]
pub fn profile_set_photo(path: String) -> Result<(), String> {
    let real = PathBuf::from(&path).canonicalize().map_err(|e| e.to_string())?;
    if !real.starts_with(home()) {
        return Err("Só dá pra usar imagens da sua pasta pessoal.".into());
    }
    std::fs::copy(real, home().join(".face")).map(|_| ()).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn modos_do_modem() {
        assert_eq!(mode_id("allowed: 2g, 3g, 4g; preferred: 4g").as_deref(), Some("2g|3g|4g:4g"));
        assert_eq!(mode_id("allowed: 4g; preferred: none").as_deref(), Some("4g:none"));
        assert_eq!(mode_label("2g|3g|4g:4g"), "4G / 3G / 2G automático");
        assert_eq!(mode_label("2g|3g|4g:3g"), "4G / 3G / 2G (prefere 3G)");
        assert_eq!(mode_label("4g:none"), "Só 4G");
    }

    #[test]
    fn a_arvore_inclui_netos() {
        let parents: HashMap<i64, i64> = [(10, 1), (11, 10), (12, 11), (20, 1)].into();
        let mut t = tree(10, &parents);
        t.sort();
        assert_eq!(t, vec![10, 11, 12]);
    }
}
