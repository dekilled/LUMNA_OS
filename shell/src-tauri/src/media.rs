//! Modo Mídia: serviços de streaming e os vídeos do próprio computador.
//!
//! - Streaming abre no Firefox em modo quiosque (tela cheia, sem barras), num perfil só da TV,
//!   com o DRM (Widevine) ligado. O WebKit do shell não tem Widevine (ver docs/VIABILIDADE.md, 3.1).
//! - Vídeos locais tocam no mpv, que guarda onde você parou; é daí que sai o "Continuar assistindo".

use std::path::{Path, PathBuf};
use std::process::Command;

use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct Service {
    pub id: &'static str,
    pub name: &'static str,
    #[serde(skip)]
    pub url: &'static str,
}

pub const SERVICES: &[Service] = &[
    Service { id: "netflix", name: "Netflix", url: "https://www.netflix.com/browse" },
    Service { id: "youtube", name: "YouTube", url: "https://www.youtube.com" },
    Service { id: "prime", name: "Prime Video", url: "https://www.primevideo.com" },
    Service { id: "disney", name: "Disney+", url: "https://www.disneyplus.com" },
    Service { id: "music", name: "Música", url: "https://music.youtube.com" },
];

#[derive(Debug, Clone, Serialize)]
pub struct Video {
    pub id: String,
    pub title: String,
    #[serde(skip)]
    pub path: PathBuf,
    /// Miniatura (caminho local), quando já foi gerada.
    pub thumb: Option<String>,
    /// 0 a 1: quanto já foi assistido (0 = não começou).
    pub progress: f32,
    /// Quando foi assistido por último (segundos desde 1970), para ordenar o "Continuar".
    pub watched_at: u64,
}

const VIDEO_EXTENSIONS: &[&str] = &["mp4", "mkv", "webm", "avi", "mov", "m4v", "ts"];

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default()
}

fn cache_dir() -> PathBuf {
    std::env::var_os("XDG_CACHE_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".cache")).join("lumna/thumbs")
}

fn state_home() -> PathBuf {
    std::env::var_os("XDG_STATE_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".local/state"))
}

/// Pasta de vídeos do usuário (`XDG_VIDEOS_DIR` do user-dirs, ou ~/Vídeos, ou ~/Videos).
pub fn videos_dir() -> PathBuf {
    let config = std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".config"));
    if let Ok(text) = std::fs::read_to_string(config.join("user-dirs.dirs")) {
        for line in text.lines() {
            if let Some(value) = line.strip_prefix("XDG_VIDEOS_DIR=") {
                let value = value.trim_matches('"').replace("$HOME", &home().to_string_lossy());
                return PathBuf::from(value);
            }
        }
    }
    ["Vídeos", "Videos"].iter().map(|d| home().join(d)).find(|p| p.is_dir()).unwrap_or_else(|| home().join("Vídeos"))
}

/// O mpv guarda a posição em `watch_later/<MD5 do caminho, em maiúsculas>`, numa linha `start=`.
fn watch_later(path: &Path) -> Option<(f64, u64)> {
    let name = format!("{:X}", md5::compute(path.to_string_lossy().as_bytes()));
    let file = [state_home().join("mpv/watch_later"), home().join(".config/mpv/watch_later")]
        .into_iter()
        .map(|dir| dir.join(&name))
        .find(|f| f.is_file())?;
    let text = std::fs::read_to_string(&file).ok()?;
    let start = text.lines().find_map(|l| l.strip_prefix("start="))?.trim().parse().ok()?;
    let when = std::fs::metadata(&file)
        .and_then(|m| m.modified())
        .ok()
        .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
        .map_or(0, |d| d.as_secs());
    Some((start, when))
}

fn cache_key(path: &Path) -> String {
    format!("{:x}", md5::compute(path.to_string_lossy().as_bytes()))
}

/// Duração em segundos, guardada junto da miniatura depois da primeira leitura.
fn duration(path: &Path) -> Option<f64> {
    let cached = cache_dir().join(format!("{}.duration", cache_key(path)));
    if let Some(d) = std::fs::read_to_string(&cached).ok().and_then(|t| t.trim().parse().ok()) {
        return Some(d);
    }
    let output = Command::new("ffprobe")
        .args(["-v", "error", "-show_entries", "format=duration", "-of", "default=nw=1:nk=1"])
        .arg(path)
        .output()
        .ok()?;
    let d: f64 = String::from_utf8_lossy(&output.stdout).trim().parse().ok()?;
    let _ = std::fs::create_dir_all(cache_dir());
    let _ = std::fs::write(cached, d.to_string());
    Some(d)
}

fn thumb_path(path: &Path) -> PathBuf {
    cache_dir().join(format!("{}.jpg", cache_key(path)))
}

/// Gera as miniaturas que faltam (um quadro aos 10% do vídeo). Devolve quantas foram criadas.
pub fn make_missing_thumbs(videos: &[Video]) -> usize {
    let _ = std::fs::create_dir_all(cache_dir());
    let mut made = 0;
    for video in videos.iter().filter(|v| v.thumb.is_none()) {
        let at = duration(&video.path).map_or(5.0, |d| (d * 0.1).min(120.0));
        let ok = Command::new("ffmpeg")
            .args(["-loglevel", "error", "-y", "-ss", &format!("{at:.1}"), "-i"])
            .arg(&video.path)
            .args(["-frames:v", "1", "-vf", "scale=640:-2"])
            .arg(thumb_path(&video.path))
            .status()
            .is_ok_and(|s| s.success());
        made += usize::from(ok);
    }
    made
}

/// Vídeos da pasta de vídeos (até 3 níveis), os assistidos por último primeiro.
pub fn videos() -> Vec<Video> {
    let mut found = Vec::new();
    let mut stack = vec![(videos_dir(), 0)];
    while let Some((dir, depth)) = stack.pop() {
        let Ok(entries) = std::fs::read_dir(&dir) else { continue };
        for entry in entries.flatten() {
            let path = entry.path();
            let hidden = entry.file_name().to_string_lossy().starts_with('.');
            if path.is_dir() && depth < 3 && !hidden {
                stack.push((path, depth + 1));
            } else if path
                .extension()
                .and_then(|e| e.to_str())
                .is_some_and(|e| VIDEO_EXTENSIONS.contains(&e.to_lowercase().as_str()))
            {
                found.push(path);
            }
        }
    }
    let mut videos: Vec<Video> = found
        .into_iter()
        .map(|path| {
            let (progress, watched_at) = match watch_later(&path) {
                Some((start, when)) => (duration(&path).map_or(0.0, |d| (start / d).clamp(0.0, 1.0) as f32), when),
                None => (0.0, 0),
            };
            let thumb = thumb_path(&path);
            Video {
                id: cache_key(&path),
                title: path.file_stem().unwrap_or_default().to_string_lossy().replace(['.', '_'], " "),
                thumb: thumb.is_file().then(|| thumb.to_string_lossy().into()),
                progress,
                watched_at,
                path,
            }
        })
        .collect();
    videos.sort_by(|a, b| b.watched_at.cmp(&a.watched_at).then_with(|| a.title.to_lowercase().cmp(&b.title.to_lowercase())));
    videos
}

fn quote(text: &str) -> String {
    format!("'{}'", text.replace('\'', r"'\''"))
}

pub fn video_command(video: &Video) -> String {
    format!("mpv --fs --force-window=yes --save-position-on-quit {}", quote(&video.path.to_string_lossy()))
}

/// Perfil do Firefox só para a TV, com DRM ligado e sem perguntas de "navegador padrão".
fn media_profile() -> Result<PathBuf, String> {
    let dir = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| home().join(".local/share"))
        .join("lumna/firefox-tv");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let prefs = [
        ("media.eme.enabled", "true"),
        ("media.gmp-widevinecdm.enabled", "true"),
        ("media.gmp-widevinecdm.visible", "true"),
        ("browser.shell.checkDefaultBrowser", "false"),
        ("browser.aboutwelcome.enabled", "false"),
        ("datareporting.policy.dataSubmissionPolicyBypassNotification", "true"),
        ("media.hardware-video-decoding.enabled", "true"),
    ];
    let user_js: String = prefs.iter().map(|(k, v)| format!("user_pref(\"{k}\", {v});\n")).collect();
    std::fs::write(dir.join("user.js"), user_js).map_err(|e| e.to_string())?;
    Ok(dir)
}

pub fn service_command(id: &str) -> Result<String, String> {
    let service = SERVICES.iter().find(|s| s.id == id).ok_or_else(|| format!("serviço {id} não existe"))?;
    let profile = media_profile()?;
    Ok(format!("firefox --kiosk --new-instance --profile {} {}", quote(&profile.to_string_lossy()), service.url))
}

pub fn thumbs_dir() -> PathBuf {
    cache_dir()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn quotes_paths_for_the_shell() {
        assert_eq!(quote("/home/a/Filme d'Água.mkv"), r"'/home/a/Filme d'\''Água.mkv'");
    }

    #[test]
    fn mpv_watch_later_uses_uppercase_md5() {
        // Mesmo nome que o mpv dá ao arquivo de posição de /tmp/a.mkv.
        assert_eq!(format!("{:X}", md5::compute(b"/tmp/a.mkv")).len(), 32);
        assert!(format!("{:X}", md5::compute(b"/tmp/a.mkv")).chars().all(|c| !c.is_ascii_lowercase()));
    }

    #[test]
    fn every_service_opens_in_kiosk_mode() {
        for service in SERVICES {
            let cmd = service_command(service.id).unwrap();
            assert!(cmd.starts_with("firefox --kiosk"), "{cmd}");
            assert!(cmd.ends_with(service.url));
        }
    }
}
