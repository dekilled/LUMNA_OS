//! Bloqueio de tela e tempo parado.
//!
//! A tela de bloqueio é o hyprlock (usa o protocolo de bloqueio do Wayland: se ele cair, a tela
//! continua bloqueada), no visual do LUMNA: o papel de parede em uso com blur, relógio grande, data
//! e o campo de senha violeta. O tempo parado é o hypridle: bloquear, desligar a tela e suspender
//! depois de N minutos, e bloquear antes de suspender.
//!
//! As escolhas ficam em `~/.config/lumna/idle.json`; as configs prontas (`hyprlock.conf` e
//! `hypridle.conf`) em `~/.config/lumna/`, refeitas quando algo muda.

use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Manager};

use crate::SharedCompositor;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(default)]
pub struct IdleConfig {
    /// Minutos parado até bloquear (0 = nunca).
    pub lock_after: u32,
    /// Minutos parado até desligar a tela (0 = nunca).
    pub screen_off_after: u32,
    /// Minutos parado até suspender (0 = nunca).
    pub suspend_after: u32,
    /// Bloqueia antes de suspender (a tela volta pedindo a senha).
    pub lock_on_suspend: bool,
}

impl Default for IdleConfig {
    fn default() -> Self {
        IdleConfig { lock_after: 10, screen_off_after: 15, suspend_after: 0, lock_on_suspend: true }
    }
}

impl IdleConfig {
    fn clamped(mut self) -> Self {
        // Até 12 horas; mais que isso é "nunca" na prática.
        for value in [&mut self.lock_after, &mut self.screen_off_after, &mut self.suspend_after] {
            *value = (*value).min(720);
        }
        self
    }
}

fn config_dir() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna")
}

fn load() -> IdleConfig {
    std::fs::read_to_string(config_dir().join("idle.json"))
        .ok()
        .and_then(|t| serde_json::from_str::<IdleConfig>(&t).ok())
        .unwrap_or_default()
        .clamped()
}

fn lock_config_path() -> PathBuf {
    config_dir().join("hyprlock.conf")
}

/// Comando que abre a tela de bloqueio (uma só, mesmo chamado várias vezes).
fn lock_command() -> String {
    format!("pidof hyprlock || hyprlock -c '{}'", lock_config_path().display())
}

const SCREEN_ON: &str = r#"hyprctl dispatch 'hl.dsp.dpms({ action = "on" })'"#;
const SCREEN_OFF: &str = r#"hyprctl dispatch 'hl.dsp.dpms({ action = "off" })'"#;

/// O `hypridle.conf` para as escolhas.
pub fn idle_conf(config: &IdleConfig) -> String {
    let mut text = String::from("# Gerado pelo LUMNA (Configurações → Energia). Mudanças à mão são trocadas por lá.\n\n");
    text += "general {\n";
    text += &format!("    lock_cmd = {}\n", lock_command());
    if config.lock_on_suspend {
        text += "    before_sleep_cmd = loginctl lock-session\n";
    }
    text += &format!("    after_sleep_cmd = {SCREEN_ON}\n");
    text += "}\n";
    if config.lock_after > 0 {
        text += &format!("\nlistener {{\n    timeout = {}\n    on-timeout = loginctl lock-session\n}}\n", config.lock_after * 60);
    }
    if config.screen_off_after > 0 {
        text += &format!(
            "\nlistener {{\n    timeout = {}\n    on-timeout = {SCREEN_OFF}\n    on-resume = {SCREEN_ON}\n}}\n",
            config.screen_off_after * 60
        );
    }
    if config.suspend_after > 0 {
        text += &format!("\nlistener {{\n    timeout = {}\n    on-timeout = systemctl suspend\n}}\n", config.suspend_after * 60);
    }
    text
}

/// O `hyprlock.conf` no visual do LUMNA, com o papel de parede em uso. `layouts`: siglas dos
/// teclados (`br`, `us`); com dois ou mais, aparece qual está em uso embaixo da senha.
pub fn lock_conf(wallpaper: &str, layouts: &[String]) -> String {
    // O caminho vem das pastas de papel de parede; sem quebra de linha nem `#`, que o hyprlang lê como
    // comentário.
    let wallpaper = if wallpaper.contains(['\n', '#']) { crate::wallpaper::DEFAULT } else { wallpaper };
    let safe = |l: &String| !l.is_empty() && l.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-');
    let keyboard = if layouts.len() > 1 && layouts.iter().all(safe) {
        let codes = layouts.iter().map(|l| l.to_uppercase()).collect::<Vec<_>>().join(",");
        format!(
            "\n# Teclado em uso (Alt + Shift troca)\nlabel {{\n    monitor =\n    text = $LAYOUT[{codes}]\n    color = rgba(a59cc9ff)\n    font_size = 12\n    font_family = Inter SemiBold\n    position = 0, -78\n    halign = center\n    valign = center\n}}\n"
        )
    } else {
        String::new()
    };
    let body = format!(
        r###"# Gerado pelo LUMNA: tela de bloqueio. Refeita quando o papel de parede muda.

general {{
    hide_cursor = false
    ignore_empty_input = true
}}

animations {{
    enabled = true
    bezier = lumna, 0.22, 1, 0.36, 1
    animation = fadeIn, 1, 4, lumna
    animation = fadeOut, 1, 4, lumna
    animation = inputFieldDots, 1, 2, lumna
}}

background {{
    monitor =
    path = {wallpaper}
    color = rgba(120c24ff)
    blur_passes = 3
    blur_size = 8
    brightness = 0.55
    vibrancy = 0.2
}}

# Relógio
label {{
    monitor =
    text = $TIME
    color = rgba(ece8ffff)
    font_size = 104
    font_family = Inter SemiBold
    position = 0, 190
    halign = center
    valign = center
}}

# Data
label {{
    monitor =
    text = cmd[update:60000] date +"%A, %d de %B"
    color = rgba(a59cc9ff)
    font_size = 20
    font_family = Inter
    position = 0, 100
    halign = center
    valign = center
}}

input-field {{
    monitor =
    size = 340, 54
    outline_thickness = 2
    rounding = 16
    inner_color = rgba(100c1ee6)
    outer_color = rgba(a78bfaee) rgba(7c3aedee) 45deg
    check_color = rgba(c4b5fdee) rgba(8b5cf6ee) 45deg
    fail_color = rgba(ef4444ee) rgba(b91c1cee) 45deg
    font_color = rgb(ece8ff)
    font_family = Inter
    fade_on_empty = false
    placeholder_text = <span foreground="##a59cc9">Senha de $USER</span>
    fail_text = Senha incorreta
    check_text = Conferindo…
    dots_spacing = 0.3
    dots_center = true
    position = 0, -20
    halign = center
    valign = center
}}

label {{
    monitor =
    text = LUMNA
    color = rgba(a78bfa99)
    font_size = 13
    font_family = Inter SemiBold
    position = 0, 40
    halign = center
    valign = bottom
}}
"###
    );
    body + &keyboard
}

/// Refaz a tela de bloqueio (no início do shell e quando o papel de parede muda).
pub fn write_lock_config() {
    let dir = config_dir();
    let _ = std::fs::create_dir_all(&dir);
    let layouts: Vec<String> = crate::keyboard::current_layouts().into_iter().map(|l| l.layout).collect();
    if let Err(err) = std::fs::write(lock_config_path(), lock_conf(&crate::wallpaper::current_lock(), &layouts)) {
        eprintln!("lumna-shell: hyprlock.conf: {err}");
    }
}

/// Grava o `hypridle.conf` e (re)inicia o hypridle como filho do compositor.
fn restart_idle(app: &AppHandle, config: &IdleConfig) -> Result<(), String> {
    let dir = config_dir();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let path = dir.join("hypridle.conf");
    std::fs::write(&path, idle_conf(config)).map_err(|e| e.to_string())?;
    let _ = std::process::Command::new("pkill").args(["-x", "-u", &whoami(), "hypridle"]).status();
    app.state::<SharedCompositor>().exec(&format!("hypridle -c '{}'", path.display()))
}

fn whoami() -> String {
    std::env::var("USER").unwrap_or_else(|_| "nobody".into())
}

/// No início do shell: tela de bloqueio pronta e o hypridle rodando com as escolhas.
pub fn start(app: AppHandle) {
    write_lock_config();
    if let Err(err) = restart_idle(&app, &load()) {
        eprintln!("lumna-shell: hypridle: {err}");
    }
}

/// Bloqueia agora.
pub fn lock_now(app: &AppHandle) -> Result<(), String> {
    app.state::<SharedCompositor>().exec(&lock_command())
}

#[tauri::command]
pub fn idle_config() -> IdleConfig {
    load()
}

#[tauri::command]
pub fn idle_config_set(app: AppHandle, config: IdleConfig) -> Result<IdleConfig, String> {
    let config = config.clamped();
    let dir = config_dir();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let json = serde_json::to_string_pretty(&config).map_err(|e| e.to_string())?;
    std::fs::write(dir.join("idle.json"), json + "\n").map_err(|e| e.to_string())?;
    restart_idle(&app, &config)?;
    Ok(config)
}

#[tauri::command]
pub fn lock_screen(app: AppHandle) -> Result<(), String> {
    lock_now(&app)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn idle_conf_only_has_the_chosen_timers() {
        let text = idle_conf(&IdleConfig { lock_after: 10, screen_off_after: 0, suspend_after: 30, lock_on_suspend: true });
        assert!(text.contains("before_sleep_cmd = loginctl lock-session"));
        assert!(text.contains("timeout = 600\n    on-timeout = loginctl lock-session"));
        assert!(text.contains("timeout = 1800\n    on-timeout = systemctl suspend"));
        assert!(!text.contains("dpms({ action = \"off\" })"));
        let never = idle_conf(&IdleConfig { lock_after: 0, screen_off_after: 0, suspend_after: 0, lock_on_suspend: false });
        assert!(!never.contains("listener"));
        assert!(!never.contains("before_sleep_cmd"));
    }

    #[test]
    fn lock_conf_uses_the_wallpaper() {
        let text = lock_conf("/home/eric/.local/share/lumna/wallpapers/praia.jpg", &["br".into()]);
        assert!(text.contains("path = /home/eric/.local/share/lumna/wallpapers/praia.jpg\n"));
        assert!(text.contains("input-field {"));
        assert!(!text.contains("$LAYOUT"));
        let two = lock_conf("/x.png", &["br".into(), "us".into()]);
        assert!(two.contains("text = $LAYOUT[BR,US]"));
        // Caminho estranho: volta para o padrão.
        assert!(lock_conf("/tmp/a#b.png", &[]).contains(&format!("path = {}\n", crate::wallpaper::DEFAULT)));
    }

    #[test]
    fn clamps_minutes() {
        let c = IdleConfig { lock_after: 99999, ..IdleConfig::default() }.clamped();
        assert_eq!(c.lock_after, 720);
    }
}
