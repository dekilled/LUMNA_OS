//! Rede, volume e bateria para a topbar. Lidos do sysfs e do `wpctl` (PipeWire) a cada 2 s;
//! um evento só sai quando algo muda.

use std::process::Command;
use std::sync::Mutex;
use std::time::Duration;

use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};

#[derive(Debug, Clone, Default, Serialize, PartialEq)]
pub struct Status {
    /// `wifi`, `ethernet` ou `none`.
    pub network: &'static str,
    /// 0 a 100; `None` sem saída de áudio.
    pub volume: Option<u8>,
    pub muted: bool,
    /// 0 a 100; `None` sem bateria (PC de mesa).
    pub battery: Option<u8>,
    pub charging: bool,
}

#[derive(Default)]
pub struct StatusState(pub Mutex<Status>);

fn network() -> &'static str {
    let Ok(entries) = std::fs::read_dir("/sys/class/net") else { return "none" };
    let mut best = "none";
    for entry in entries.flatten() {
        let path = entry.path();
        if entry.file_name() == "lo" || !path.join("device").exists() {
            continue;
        }
        let up = std::fs::read_to_string(path.join("operstate")).is_ok_and(|s| s.trim() == "up");
        if !up {
            continue;
        }
        if path.join("wireless").exists() || path.join("phy80211").exists() {
            best = "wifi";
        } else {
            return "ethernet";
        }
    }
    best
}

/// `Volume: 0.40 [MUTED]` → (40, true)
pub fn parse_wpctl(output: &str) -> Option<(u8, bool)> {
    let value: f32 = output.split_whitespace().nth(1)?.parse().ok()?;
    Some(((value * 100.0).round().clamp(0.0, 150.0) as u8, output.contains("[MUTED]")))
}

fn volume() -> (Option<u8>, bool) {
    Command::new("wpctl")
        .args(["get-volume", "@DEFAULT_AUDIO_SINK@"])
        .output()
        .ok()
        .and_then(|o| parse_wpctl(&String::from_utf8_lossy(&o.stdout)))
        .map_or((None, false), |(v, m)| (Some(v), m))
}

fn battery() -> (Option<u8>, bool) {
    let Ok(entries) = std::fs::read_dir("/sys/class/power_supply") else { return (None, false) };
    for entry in entries.flatten() {
        let path = entry.path();
        let read = |name: &str| std::fs::read_to_string(path.join(name)).map(|s| s.trim().to_string());
        if read("type").is_ok_and(|t| t == "Battery") && read("scope").map_or(true, |s| s != "Device") {
            let capacity = read("capacity").ok().and_then(|c| c.parse().ok());
            let charging = read("status").is_ok_and(|s| s == "Charging" || s == "Full");
            return (capacity, charging);
        }
    }
    (None, false)
}

pub fn read() -> Status {
    let (volume, muted) = volume();
    let (battery, charging) = battery();
    Status { network: network(), volume, muted, battery, charging }
}

pub fn refresh(app: &AppHandle) {
    let status = read();
    let state = app.state::<StatusState>();
    let mut current = state.0.lock().unwrap();
    if *current != status {
        *current = status.clone();
        let _ = app.emit("lumna://status", status);
    }
}

pub fn watch(app: AppHandle) {
    std::thread::spawn(move || loop {
        refresh(&app);
        std::thread::sleep(Duration::from_secs(2));
    });
}

pub fn wpctl(args: &[&str]) -> Result<(), String> {
    let status = Command::new("wpctl").args(args).status().map_err(|e| format!("wpctl: {e}"))?;
    status.success().then_some(()).ok_or_else(|| format!("wpctl {args:?} falhou"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_wpctl() {
        assert_eq!(parse_wpctl("Volume: 0.40\n"), Some((40, false)));
        assert_eq!(parse_wpctl("Volume: 0.55 [MUTED]\n"), Some((55, true)));
        assert_eq!(parse_wpctl(""), None);
    }
}
