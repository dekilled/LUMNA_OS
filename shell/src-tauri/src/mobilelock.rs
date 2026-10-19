//! Celular: tela de bloqueio, botão power e apagar a tela quando fica parado.
//!
//! O bloqueio é a camada `mobile-lock` (por cima dos apps, abaixo da ligação e do teclado). No PC
//! o bloqueio é o hyprlock, pelo protocolo de bloqueio do Wayland; aqui ele não serve, porque
//! esconde todas as outras camadas, inclusive o LUMNA Keyboard, e não teria como digitar a senha.
//! A senha é a do Linux, conferida pelo `unix_chkpwd` (o ajudante do próprio PAM). Pode ter também
//! um PIN de 4 a 8 números (só pro bloqueio): guardado como hash com sal em `mobile-pin` e só se
//! cria ou tira com a senha do Linux. Errou 5 vezes: espera 30 s (e o dobro a cada nova rodada).
//!
//! O botão power é lido direto do kernel (`/dev/input/eventN` do `pm8941_pwrkey`, liberado pela
//! regra udev): toque rápido apaga a tela e bloqueia (ou acende, se estava apagada); segurar abre
//! o menu de energia; tocando uma ligação, silencia. O logind ignora o botão
//! (`/etc/systemd/logind.conf.d/50-lumna-power.conf`), senão ele desligava o aparelho.
//!
//! Parado: o hypridle chama `lumna-ctl lock idle` depois do tempo escolhido nos Ajustes.

use std::io::{Read, Write};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::time::Duration;

use serde::{Deserialize, Serialize};
use serde_json::json;
use tauri::{AppHandle, Emitter, Manager};

use crate::{layer, SharedCompositor};

static LOCKED: AtomicBool = AtomicBool::new(false);
static SCREEN_ON: AtomicBool = AtomicBool::new(true);
/// Muda a cada toque no power: o "segurar" só vale se ninguém soltou nesse meio tempo.
static PRESS: AtomicU64 = AtomicU64::new(0);

const LONG_PRESS: Duration = Duration::from_millis(600);

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(default)]
pub struct LockConfig {
    /// Pedir a senha ao acender a tela.
    pub enabled: bool,
    /// Segundos parado até apagar a tela e bloquear (0 = nunca).
    pub screen_off_after: u32,
}

impl Default for LockConfig {
    fn default() -> Self {
        LockConfig { enabled: true, screen_off_after: 60 }
    }
}

fn config_dir() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna")
}

fn load() -> LockConfig {
    std::fs::read_to_string(config_dir().join("mobile-lock.json"))
        .ok()
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_default()
}

/// O hypridle do celular: só o "parado → apagar e bloquear".
pub fn idle_conf(config: &LockConfig) -> String {
    let mut text = String::from("# Gerado pelo LUMNA (Ajustes → Tela e aparência). Mudanças à mão são trocadas por lá.\n");
    if config.screen_off_after > 0 {
        text += &format!("\nlistener {{\n    timeout = {}\n    on-timeout = lumna-ctl lock idle\n}}\n", config.screen_off_after);
    }
    text
}

fn restart_idle(app: &AppHandle, config: &LockConfig) -> Result<(), String> {
    let dir = config_dir();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let path = dir.join("hypridle-mobile.conf");
    std::fs::write(&path, idle_conf(config)).map_err(|e| e.to_string())?;
    let user = std::env::var("USER").unwrap_or_default();
    let _ = Command::new("pkill").args(["-x", "-u", &user, "hypridle"]).status();
    app.state::<SharedCompositor>().exec(&format!("hypridle -c '{}'", path.display()))
}

// ---- tela ----

fn dpms(on: bool) {
    let action = if on { "on" } else { "off" };
    let new = Command::new("hyprctl").args(["dispatch", &format!("hl.dsp.dpms({{ action = \"{action}\" }})")]).output();
    let ok = new.is_ok_and(|o| String::from_utf8_lossy(&o.stdout).trim() == "ok");
    if !ok {
        let _ = Command::new("hyprctl").args(["dispatch", "dpms", action]).status();
    }
}

fn set_screen(app: &AppHandle, on: bool) {
    SCREEN_ON.store(on, Ordering::SeqCst);
    dpms(on);
    let _ = app.emit("lumna://screen", on);
}

// ---- bloqueio ----

/// Mostra a tela de bloqueio (e tira da frente o teclado e os painéis).
pub fn lock(app: &AppHandle) {
    if !load().enabled {
        return;
    }
    LOCKED.store(true, Ordering::SeqCst);
    let handle = app.clone();
    let _ = app.run_on_main_thread(move || {
        // a Emma também sai: a conversa de antes do bloqueio não fica à mostra
        for panel in ["mobile-center", "mobile-recents", "mobile-power", "emma"] {
            layer::set_visible(panel, false);
        }
        if layer::is_visible("keyboard") {
            layer::set_visible("keyboard", false);
            layer::set_visible("mobile-nav", true);
            let _ = handle.emit("lumna://keyboard", json!({ "shown": false, "lumna": false }));
        }
        layer::set_visible("mobile-lock", true);
        let _ = handle.emit("lumna://lock", true);
    });
}

fn unlock(app: &AppHandle) {
    LOCKED.store(false, Ordering::SeqCst);
    // o campo da senha sai de cena: as teclas não ficam presas nele
    crate::touchkeys::clear_target();
    let handle = app.clone();
    let _ = app.run_on_main_thread(move || {
        if layer::is_visible("keyboard") {
            layer::set_visible("keyboard", false);
            layer::set_visible("mobile-nav", true);
            let _ = handle.emit("lumna://keyboard", json!({ "shown": false, "lumna": false }));
        }
        layer::set_visible("mobile-lock", false);
        let _ = handle.emit("lumna://lock", false);
    });
}

/// Confere a senha do usuário pelo `unix_chkpwd` (ele só confere a de quem chama; espera a
/// senha terminada em zero na entrada e responde pelo código de saída).
fn password_ok(password: &str) -> bool {
    let user = std::env::var("USER").unwrap_or_default();
    let helper = ["/usr/bin/unix_chkpwd", "/usr/sbin/unix_chkpwd", "/sbin/unix_chkpwd"]
        .into_iter()
        .find(|p| std::path::Path::new(p).exists())
        .unwrap_or("unix_chkpwd");
    let Ok(mut child) = Command::new(helper).args([&user, "nullok"]).stdin(Stdio::piped()).stdout(Stdio::null()).stderr(Stdio::null()).spawn()
    else {
        return false;
    };
    if let Some(mut stdin) = child.stdin.take() {
        let _ = stdin.write_all(password.as_bytes());
        let _ = stdin.write_all(&[0]);
    }
    child.wait().is_ok_and(|s| s.success())
}

/// A tela de bloqueio está na frente (a Emma só faz o que é seguro).
pub fn is_locked() -> bool {
    LOCKED.load(Ordering::SeqCst)
}

// ---- PIN ----

/// O PIN guardado: `tamanho:sal:hash` (o tamanho deixa a tela conferir sozinha no último número).
fn pin_path() -> PathBuf {
    config_dir().join("mobile-pin")
}

const PIN_ROUNDS: u32 = 120_000;

fn pin_hash(salt: &str, pin: &str) -> String {
    use sha2::{Digest, Sha256};
    let mut h = Sha256::digest(format!("{salt}:{pin}").as_bytes());
    for _ in 0..PIN_ROUNDS {
        h = Sha256::digest(h);
    }
    h.iter().map(|b| format!("{b:02x}")).collect()
}

fn read_pin() -> Option<(usize, String, String)> {
    let text = std::fs::read_to_string(pin_path()).ok()?;
    let mut parts = text.trim().splitn(3, ':');
    let len = parts.next()?.parse().ok()?;
    Some((len, parts.next()?.to_string(), parts.next()?.to_string()))
}

fn pin_length() -> Option<usize> {
    read_pin().map(|(len, _, _)| len)
}

fn pin_ok(pin: &str) -> bool {
    read_pin().is_some_and(|(len, salt, hash)| pin.len() == len && pin_hash(&salt, pin) == hash)
}

fn valid_pin(pin: &str) -> bool {
    (4..=8).contains(&pin.len()) && pin.bytes().all(|b| b.is_ascii_digit())
}

fn write_pin(pin: &str) -> Result<(), String> {
    use std::os::unix::fs::OpenOptionsExt;
    let mut salt = [0u8; 16];
    std::fs::File::open("/dev/urandom").and_then(|mut f| f.read_exact(&mut salt)).map_err(|e| e.to_string())?;
    let salt: String = salt.iter().map(|b| format!("{b:02x}")).collect();
    std::fs::create_dir_all(config_dir()).map_err(|e| e.to_string())?;
    let mut file = std::fs::OpenOptions::new().write(true).create(true).truncate(true).mode(0o600).open(pin_path()).map_err(|e| e.to_string())?;
    file.write_all(format!("{}:{salt}:{}\n", pin.len(), pin_hash(&salt, pin)).as_bytes()).map_err(|e| e.to_string())
}

/// Tentativas erradas seguidas e até quando esperar (segundos Unix).
static FAILS: AtomicU64 = AtomicU64::new(0);
static WAIT_UNTIL: AtomicU64 = AtomicU64::new(0);

fn unix_now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_secs())
}

/// Segundos que ainda faltam pra poder tentar de novo.
fn wait_left() -> u64 {
    WAIT_UNTIL.load(Ordering::SeqCst).saturating_sub(unix_now())
}

/// Mais um erro: a cada 5, espera (30 s, 60 s, 120 s... até 10 min).
fn failed() -> String {
    let fails = FAILS.fetch_add(1, Ordering::SeqCst) + 1;
    if fails % 5 == 0 {
        let wait = (30u64 << ((fails / 5 - 1).min(5))).min(600);
        WAIT_UNTIL.store(unix_now() + wait, Ordering::SeqCst);
        return format!("Muitas tentativas. Espere {wait} s");
    }
    "Incorreto".into()
}

#[derive(Debug, Clone, Serialize)]
pub struct LockState {
    pub locked: bool,
    pub screen_on: bool,
    pub config: LockConfig,
    /// Tamanho do PIN (nenhum: só a senha).
    pub pin: Option<usize>,
    /// Segundos até poder tentar de novo (errou demais).
    pub wait: u64,
}

#[tauri::command]
pub fn lock_state() -> LockState {
    LockState {
        locked: LOCKED.load(Ordering::SeqCst),
        screen_on: SCREEN_ON.load(Ordering::SeqCst),
        config: load(),
        pin: pin_length(),
        wait: wait_left(),
    }
}

/// Desbloqueia com o PIN (só números, do tamanho dele) ou com a senha do Linux.
#[tauri::command]
pub async fn lock_unlock(app: AppHandle, password: String) -> Result<(), String> {
    let left = wait_left();
    if left > 0 {
        return Err(format!("Espere {left} s"));
    }
    let ok = tauri::async_runtime::spawn_blocking(move || {
        (pin_length() == Some(password.len()) && password.bytes().all(|b| b.is_ascii_digit()) && pin_ok(&password)) || password_ok(&password)
    })
    .await
    .unwrap_or(false);
    if !ok {
        return Err(failed());
    }
    FAILS.store(0, Ordering::SeqCst);
    unlock(&app);
    Ok(())
}

/// Cria ou troca o PIN (`pin` vazio: tira). Pede a senha do Linux.
#[tauri::command]
pub async fn lock_pin_set(password: String, pin: String) -> Result<Option<usize>, String> {
    if !pin.is_empty() && !valid_pin(&pin) {
        return Err("O PIN tem de 4 a 8 números".into());
    }
    tauri::async_runtime::spawn_blocking(move || {
        if !password_ok(&password) {
            return Err("Senha do Linux incorreta".to_string());
        }
        if pin.is_empty() {
            match std::fs::remove_file(pin_path()) {
                Err(e) if e.kind() != std::io::ErrorKind::NotFound => return Err(e.to_string()),
                _ => {}
            }
        } else {
            write_pin(&pin)?;
        }
        Ok(pin_length())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn lock_now(app: AppHandle) {
    lock(&app);
}

#[tauri::command]
pub fn lock_config_set(app: AppHandle, config: LockConfig) -> Result<LockConfig, String> {
    let config = LockConfig { screen_off_after: config.screen_off_after.min(3600), ..config };
    std::fs::create_dir_all(config_dir()).map_err(|e| e.to_string())?;
    std::fs::write(config_dir().join("mobile-lock.json"), serde_json::to_vec_pretty(&config).unwrap_or_default())
        .map_err(|e| e.to_string())?;
    restart_idle(&app, &config)?;
    if !config.enabled && LOCKED.load(Ordering::SeqCst) {
        unlock(&app);
    }
    Ok(config)
}

// ---- menu de energia (segurar o power) ----

pub fn power_menu(app: &AppHandle, show: bool) {
    let handle = app.clone();
    let _ = app.run_on_main_thread(move || {
        layer::set_visible("mobile-power", show);
        let _ = handle.emit_to("mobile-power", "lumna://mobile-panel", show);
    });
}

#[tauri::command]
pub fn power_menu_close(app: AppHandle) {
    power_menu(&app, false);
}

// ---- botão power ----

fn short_press(app: &AppHandle) {
    // tocando uma ligação: o power silencia
    if crate::phone::silence() {
        return;
    }
    if !SCREEN_ON.load(Ordering::SeqCst) {
        set_screen(app, true);
        return;
    }
    power_menu(app, false);
    lock(app);
    set_screen(app, false);
}

fn long_press(app: &AppHandle) {
    crate::haptics::buzz(40);
    if !SCREEN_ON.load(Ordering::SeqCst) {
        set_screen(app, true);
    }
    power_menu(app, true);
}

/// O dispositivo do botão power: o que tem KEY_POWER (116) entre as teclas, preferindo o do
/// PMIC (`pm8941_pwrkey`).
fn power_device() -> Option<PathBuf> {
    let mut found = Vec::new();
    for entry in std::fs::read_dir("/sys/class/input").ok()?.flatten() {
        let name = entry.file_name().to_string_lossy().to_string();
        if !name.starts_with("event") {
            continue;
        }
        let dev_name = std::fs::read_to_string(entry.path().join("device/name")).unwrap_or_default();
        let keys = std::fs::read_to_string(entry.path().join("device/capabilities/key")).unwrap_or_default();
        if has_key(&keys, 116) {
            found.push((dev_name.contains("pwrkey"), PathBuf::from(format!("/dev/input/{name}"))));
        }
    }
    found.sort_by_key(|(pwrkey, _)| !*pwrkey);
    found.into_iter().next().map(|(_, p)| p)
}

/// O bitmap de teclas do sysfs: palavras em hexa, da mais alta pra mais baixa.
fn has_key(bitmap: &str, code: usize) -> bool {
    let words: Vec<&str> = bitmap.split_whitespace().collect();
    let bits = usize::BITS as usize;
    let index = code / bits;
    let Some(word) = words.len().checked_sub(index + 1).and_then(|i| words.get(i)) else { return false };
    u64::from_str_radix(word, 16).is_ok_and(|w| w >> (code % bits) & 1 == 1)
}

fn watch_power(app: AppHandle) {
    std::thread::spawn(move || {
        let Some(path) = power_device() else {
            return eprintln!("lumna-shell: botão power: nenhum dispositivo com KEY_POWER");
        };
        let mut file = match std::fs::File::open(&path) {
            Ok(f) => f,
            Err(e) => return eprintln!("lumna-shell: botão power: {}: {e} (regra udev 70-lumna-vibra.rules)", path.display()),
        };
        let mut event = [0u8; 24];
        while file.read_exact(&mut event).is_ok() {
            let kind = u16::from_ne_bytes([event[16], event[17]]);
            let code = u16::from_ne_bytes([event[18], event[19]]);
            let value = i32::from_ne_bytes([event[20], event[21], event[22], event[23]]);
            if kind != 1 || code != 116 {
                continue;
            }
            match value {
                1 => {
                    let press = PRESS.fetch_add(1, Ordering::SeqCst) + 1;
                    let handle = app.clone();
                    std::thread::spawn(move || {
                        std::thread::sleep(LONG_PRESS);
                        // ainda segurando (ninguém soltou): menu; e o soltar não conta como toque
                        if PRESS.compare_exchange(press, u64::MAX, Ordering::SeqCst, Ordering::SeqCst).is_ok() {
                            long_press(&handle);
                        }
                    });
                }
                0 => {
                    if PRESS.load(Ordering::SeqCst) != u64::MAX {
                        PRESS.fetch_add(1, Ordering::SeqCst);
                        short_press(&app);
                    } else {
                        PRESS.store(0, Ordering::SeqCst);
                    }
                }
                _ => {}
            }
        }
    });
}

/// `lumna-ctl lock [idle]` e `lumna-ctl screen on|off`.
pub fn control(app: &AppHandle, args: &[&str]) -> Result<(), String> {
    match args {
        ["lock"] => lock(app),
        // parado: não apaga no meio de uma ligação
        ["lock", "idle"] => {
            if !crate::phone::in_call() {
                lock(app);
                set_screen(app, false);
            }
        }
        ["screen", "on"] => set_screen(app, true),
        ["screen", "off"] => set_screen(app, false),
        ["power-menu"] => power_menu(app, true),
        _ => return Err(format!("comando desconhecido: {args:?}")),
    }
    Ok(())
}

/// Início do shell no celular: bloqueado (se a pessoa quer), o power escutado e o hypridle no ar.
pub fn start(app: AppHandle) {
    let config = load();
    if let Err(e) = restart_idle(&app, &config) {
        eprintln!("lumna-shell: hypridle: {e}");
    }
    watch_power(app.clone());
    if config.enabled {
        lock(&app);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pin_guardado_com_sal() {
        let dir = std::env::temp_dir().join(format!("lumna-pin-{}", std::process::id()));
        std::env::set_var("XDG_CONFIG_HOME", &dir);
        assert!(valid_pin("1234") && valid_pin("12345678") && !valid_pin("123") && !valid_pin("12a4"));
        write_pin("2580").unwrap();
        assert_eq!(pin_length(), Some(4));
        assert!(pin_ok("2580") && !pin_ok("0852") && !pin_ok("25800"));
        let text = std::fs::read_to_string(pin_path()).unwrap();
        assert!(!text.contains("2580"));
        std::fs::remove_dir_all(&dir).ok();
    }

    #[test]
    fn tecla_power_no_bitmap() {
        // KEY_POWER = 116: bit 52 da segunda palavra de 64 bits
        assert!(has_key("10000000000000 0", 116));
        assert!(!has_key("0 0", 116));
        assert!(!has_key("", 116));
    }

    #[test]
    fn hypridle_so_com_tempo() {
        assert!(idle_conf(&LockConfig { enabled: true, screen_off_after: 30 }).contains("timeout = 30\n    on-timeout = lumna-ctl lock idle"));
        assert!(!idle_conf(&LockConfig { enabled: true, screen_off_after: 0 }).contains("listener"));
    }
}
