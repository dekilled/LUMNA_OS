//! Galeria: as fotos da pessoa (Imagens, com as Capturas dentro, e a DCIM da câmera), da mais nova
//! pra mais velha, com o álbum de cada uma (a pasta onde está). A tela lê os arquivos pelo
//! protocolo asset; as pastas são liberadas aqui, só elas.

use std::path::{Path, PathBuf};
use std::process::Command;

use serde::Serialize;
use tauri::{AppHandle, Manager};

const EXTS: [&str; 6] = ["jpg", "jpeg", "png", "webp", "gif", "bmp"];
const VIDEO_EXTS: [&str; 4] = ["mp4", "webm", "mkv", "mov"];
/// Até onde descer nas pastas (Imagens/Viagem/2026 ainda entra).
const DEPTH: usize = 3;
const MAX: usize = 5000;

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Photo {
    pub path: String,
    pub name: String,
    /// "Capturas", "Câmera", "Imagens" ou o nome da subpasta.
    pub album: String,
    /// Segundos Unix (modificação).
    pub time: i64,
    pub size: u64,
    /// Vídeo (as gravações de tela, os da câmera).
    pub video: bool,
}

fn is_video(path: &Path) -> bool {
    path.extension().and_then(|e| e.to_str()).is_some_and(|e| VIDEO_EXTS.contains(&e.to_ascii_lowercase().as_str()))
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

fn pictures() -> PathBuf {
    Command::new("xdg-user-dir")
        .arg("PICTURES")
        .output()
        .ok()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .filter(|p| !p.is_empty() && Path::new(p) != home())
        .map(PathBuf::from)
        .unwrap_or_else(|| home().join("Imagens"))
}

/// As pastas da galeria, com o nome do álbum de quem está direto nelas.
pub fn roots() -> Vec<(PathBuf, &'static str)> {
    let mut out = vec![(pictures(), "Imagens"), (crate::capture::records_dir(), "Gravações")];
    for dcim in [home().join("DCIM"), pictures().join("DCIM")] {
        if dcim.is_dir() && !out.iter().any(|(p, _)| *p == dcim) {
            out.push((dcim, "Câmera"));
        }
    }
    out
}

fn album_of(root_album: &str, root: &Path, file: &Path) -> String {
    let parent = file.parent().unwrap_or(root);
    if parent == root {
        return root_album.to_string();
    }
    let name = parent.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    // a pasta da câmera do Android (DCIM/Camera) é a Câmera
    if root_album == "Câmera" && name.eq_ignore_ascii_case("camera") {
        return "Câmera".into();
    }
    name
}

fn walk(dir: &Path, depth: usize, found: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        let path = entry.path();
        let name = entry.file_name();
        if name.to_string_lossy().starts_with('.') {
            continue;
        }
        let Ok(kind) = entry.file_type() else { continue };
        if kind.is_dir() {
            if depth > 0 {
                walk(&path, depth - 1, found);
            }
        } else if is_video(&path) || path.extension().and_then(|e| e.to_str()).is_some_and(|e| EXTS.contains(&e.to_ascii_lowercase().as_str())) {
            found.push(path);
        }
        if found.len() >= MAX * 2 {
            return;
        }
    }
}

pub fn list() -> Vec<Photo> {
    let mut photos = Vec::new();
    for (root, album) in roots() {
        let mut found = Vec::new();
        walk(&root, DEPTH, &mut found);
        for path in found {
            let Ok(meta) = std::fs::metadata(&path) else { continue };
            let time = meta.modified().ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map_or(0, |d| d.as_secs() as i64);
            photos.push(Photo {
                album: album_of(album, &root, &path),
                name: path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(),
                path: path.to_string_lossy().into_owned(),
                time,
                size: meta.len(),
                video: is_video(&path),
            });
        }
    }
    photos.sort_by(|a, b| b.time.cmp(&a.time).then_with(|| a.name.cmp(&b.name)));
    photos.dedup_by(|a, b| a.path == b.path);
    photos.truncate(MAX);
    photos
}

/// Toca um vídeo no mpv, na tela toda (o player do WebKit não dá conta: fica preto). Sem mpv,
/// abre no programa padrão. No celular o mpv vai sozinho pra frente assim que a janela dele
/// aparece (a Galeria só sai de cena depois, sem passar pela tela de apps) e, quando ele fecha, a
/// Galeria volta.
#[tauri::command]
pub fn gallery_play(compositor: tauri::State<'_, crate::SharedCompositor>, path: String) -> Result<(), String> {
    if !Path::new(&path).is_file() {
        return Err("o vídeo não existe mais".into());
    }
    let mpv = Command::new("mpv")
        // os controles do mpv (play/pausa, a barra do tempo) sempre à vista e grandes pro dedo
        .args(["--fs", "--force-window=yes", "--keep-open=no", "--hwdec=auto-safe", "--osd-level=1"])
        .arg("--script-opts=osc-visibility=always,osc-scalefullscreen=2,osc-layout=bottombar")
        .arg(&path)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn();
    let Ok(mut child) = mpv else {
        return Command::new("xdg-open").arg(&path).spawn().map(|_| ()).map_err(|_| "instale o mpv pra tocar vídeos (sudo pacman -S mpv)".into());
    };
    let compositor = compositor.inner().clone();
    let mobile = crate::MOBILE.load(std::sync::atomic::Ordering::Relaxed);
    let gallery = if mobile { compositor.active_window().ok().flatten().map(|w| w.address) } else { None };
    std::thread::spawn(move || {
        if mobile {
            let pid = child.id() as i64;
            for _ in 0..100 {
                if let Some(w) = compositor.windows().ok().and_then(|ws| ws.into_iter().find(|w| w.pid == pid)) {
                    let _ = crate::mobile::show_app(compositor.as_ref(), Some(&w.address));
                    break;
                }
                if matches!(child.try_wait(), Ok(Some(_))) {
                    break;
                }
                std::thread::sleep(std::time::Duration::from_millis(50));
            }
        }
        let _ = child.wait();
        if let Some(gallery) = gallery {
            let _ = crate::mobile::show_app(compositor.as_ref(), Some(&gallery));
        }
    });
    Ok(())
}

#[tauri::command]
pub async fn gallery_list(app: AppHandle) -> Vec<Photo> {
    for (root, _) in roots() {
        let _ = app.asset_protocol_scope().allow_directory(&root, true);
    }
    tauri::async_runtime::spawn_blocking(list).await.unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn albums() {
        let root = Path::new("/h/Imagens");
        assert_eq!(album_of("Imagens", root, Path::new("/h/Imagens/a.png")), "Imagens");
        assert_eq!(album_of("Imagens", root, Path::new("/h/Imagens/Capturas/a.png")), "Capturas");
        assert_eq!(album_of("Câmera", Path::new("/h/DCIM"), Path::new("/h/DCIM/Camera/a.jpg")), "Câmera");
    }

    #[test]
    fn walks_only_images() {
        let dir = std::env::temp_dir().join(format!("lumna-gal-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("Capturas")).unwrap();
        std::fs::create_dir_all(dir.join(".escondida")).unwrap();
        for f in ["a.PNG", "b.txt", "Capturas/c.jpg", ".escondida/d.png", "e.mp4"] {
            std::fs::write(dir.join(f), b"x").unwrap();
        }
        let mut found = Vec::new();
        walk(&dir, DEPTH, &mut found);
        let mut names: Vec<_> = found.iter().map(|p| p.file_name().unwrap().to_string_lossy().into_owned()).collect();
        names.sort();
        assert_eq!(names, ["a.PNG", "c.jpg", "e.mp4"]);
        std::fs::remove_dir_all(&dir).ok();
    }
}
