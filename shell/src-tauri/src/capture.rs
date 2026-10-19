//! Captura de tela, no PC e no celular: a tela congela (o `grim` tira a foto inteira do monitor em
//! foco) e aparece a camada `capture` por cima, com a foto e um retângulo pra escolher a área
//! (arrasta, estica pelas alças, ou "Tela toda"). Salvar recorta a foto congelada (o que estava na
//! tela no instante da captura, não o que veio depois) e guarda em Imagens/Capturas; dá pra só
//! copiar também. Abre por `lumna-ctl capture` (Print no PC), pelo atalho da central no celular e
//! pela Emma.

use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::Duration;

use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};

use crate::{layer, SharedCompositor};

/// A foto congelada da tela (no tmpfs da sessão).
fn frozen() -> PathBuf {
    std::env::var_os("XDG_RUNTIME_DIR").map(PathBuf::from).unwrap_or_else(std::env::temp_dir).join("lumna-captura.png")
}

/// Imagens/Capturas (a pasta de imagens do xdg-user-dirs).
pub fn shots_dir() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    let pictures = Command::new("xdg-user-dir")
        .arg("PICTURES")
        .output()
        .ok()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .filter(|p| !p.is_empty() && Path::new(p) != home)
        .map(PathBuf::from)
        .unwrap_or_else(|| home.join("Imagens"));
    pictures.join("Capturas")
}

#[derive(Debug, Clone, Serialize)]
pub struct Frozen {
    pub path: String,
    pub width: u32,
    pub height: u32,
}

fn png_size(path: &Path) -> Result<(u32, u32), String> {
    let file = std::fs::File::open(path).map_err(|e| e.to_string())?;
    let reader = png::Decoder::new(std::io::BufReader::new(file)).read_info().map_err(|e| e.to_string())?;
    let info = reader.info();
    Ok((info.width, info.height))
}

/// Congela a tela e mostra a camada de recorte.
pub fn start(app: &AppHandle) -> Result<(), String> {
    let focused = app.state::<SharedCompositor>().monitors()?.into_iter().find(|m| m.focused);
    // os painéis que estiverem na frente saem da foto (a central do celular, o launcher...)
    let _ = app.run_on_main_thread(|| {
        for panel in ["mobile-center", "mobile-recents", "emma", "launcher", "capture"] {
            if layer::exists(panel) {
                layer::set_visible(panel, false);
            }
        }
    });
    // o tempo da animação de fechar
    std::thread::sleep(Duration::from_millis(280));
    let path = frozen();
    let mut grim = Command::new("grim");
    if let Some(m) = &focused {
        grim.args(["-o", &m.name]);
    }
    let out = grim.arg(&path).output().map_err(|e| format!("grim: {e}"))?;
    if !out.status.success() {
        return Err(format!("grim: {}", String::from_utf8_lossy(&out.stderr).trim()));
    }
    let (width, height) = png_size(&path)?;
    let _ = app.asset_protocol_scope().allow_file(&path);
    let handle = app.clone();
    let frozen = Frozen { path: path.to_string_lossy().into_owned(), width, height };
    app.run_on_main_thread(move || {
        if let Some(m) = &focused {
            layer::move_to_monitor_at("capture", m.x, m.y);
        }
        layer::set_visible("capture", true);
        let _ = handle.emit_to("capture", "lumna://capture", frozen);
    })
    .map_err(|e| e.to_string())
}

/// Recorta a área (em frações da imagem, 0..1) de um PNG e grava em `out`.
pub fn crop(src: &Path, out: &Path, x: f64, y: f64, w: f64, h: f64) -> Result<(u32, u32), String> {
    let file = std::fs::File::open(src).map_err(|e| e.to_string())?;
    let mut decoder = png::Decoder::new(std::io::BufReader::new(file));
    decoder.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let mut reader = decoder.read_info().map_err(|e| e.to_string())?;
    let mut buf = vec![0; reader.output_buffer_size()];
    let frame = reader.next_frame(&mut buf).map_err(|e| e.to_string())?;
    let (color, _) = reader.output_color_type();
    let bpp = color.samples();
    let (iw, ih) = (frame.width, frame.height);
    // fração → pixel, sempre pelo menos 1 pixel e dentro da imagem
    let px = |v: f64, size: u32| ((v.clamp(0.0, 1.0) * f64::from(size)).round() as u32).min(size);
    let (x0, y0) = (px(x, iw).min(iw - 1), px(y, ih).min(ih - 1));
    let (x1, y1) = (px(x + w, iw).max(x0 + 1), px(y + h, ih).max(y0 + 1));
    let (cw, ch) = (x1 - x0, y1 - y0);
    let stride = frame.line_size;
    let mut pixels = Vec::with_capacity((cw * ch) as usize * bpp);
    for row in y0..y0 + ch {
        let start = row as usize * stride + x0 as usize * bpp;
        pixels.extend_from_slice(&buf[start..start + cw as usize * bpp]);
    }
    let file = std::fs::File::create(out).map_err(|e| e.to_string())?;
    let mut encoder = png::Encoder::new(BufWriter::new(file), cw, ch);
    encoder.set_color(color);
    encoder.set_depth(png::BitDepth::Eight);
    encoder.set_compression(png::Compression::Fast);
    let mut writer = encoder.write_header().map_err(|e| e.to_string())?;
    writer.write_image_data(&pixels).map_err(|e| e.to_string())?;
    Ok((cw, ch))
}

/// O nome da captura: "Captura 2026-10-19 14-03-22.png" (sem repetir).
fn new_name(dir: &Path) -> PathBuf {
    let stamp = Command::new("date").arg("+%Y-%m-%d %H-%M-%S").output().map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string()).unwrap_or_default();
    let mut path = dir.join(format!("Captura {stamp}.png"));
    let mut n = 2;
    while path.exists() {
        path = dir.join(format!("Captura {stamp} ({n}).png"));
        n += 1;
    }
    path
}

fn copy_image(path: &Path) -> Result<(), String> {
    let bytes = std::fs::read(path).map_err(|e| e.to_string())?;
    let mut child =
        Command::new("wl-copy").args(["--type", "image/png"]).stdin(Stdio::piped()).spawn().map_err(|e| format!("wl-copy: {e}"))?;
    child.stdin.take().ok_or("wl-copy sem entrada")?.write_all(&bytes).map_err(|e| e.to_string())?;
    child.wait().map_err(|e| e.to_string())?;
    Ok(())
}

fn hide(app: &AppHandle) {
    let _ = app.run_on_main_thread(|| layer::set_visible("capture", false));
}

// ---- comandos da camada ----

/// Começa uma captura (o atalho da central, o botão da Galeria).
#[tauri::command]
pub async fn capture_start(app: AppHandle) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || start(&app)).await.map_err(|e| e.to_string())?
}

/// Salva a área escolhida (frações da imagem). `copy`: também (ou só) pra área de transferência.
/// Devolve o caminho salvo (vazio quando só copiou).
#[tauri::command]
pub async fn capture_save(app: AppHandle, x: f64, y: f64, w: f64, h: f64, save: bool, copy: bool) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let src = frozen();
        let dir = shots_dir();
        std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
        let out = if save { new_name(&dir) } else { src.with_file_name("lumna-captura-recorte.png") };
        crop(&src, &out, x, y, w, h)?;
        hide(&app);
        if copy {
            copy_image(&out)?;
        }
        let shown = out.to_string_lossy().into_owned();
        if save {
            let name = out.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            let summary = if copy { "Captura salva e copiada" } else { "Captura salva" };
            crate::notifications::local_open(&app, "Captura", summary, &name, false, Some(format!("gallery:{shown}")));
        } else {
            crate::notifications::local(&app, "Captura", "Captura copiada", "Cole onde quiser", false);
        }
        let _ = std::fs::remove_file(&src);
        Ok(if save { shown } else { String::new() })
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn capture_cancel(app: AppHandle) {
    hide(&app);
    let _ = std::fs::remove_file(frozen());
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write_png(path: &Path, w: u32, h: u32) {
        let file = std::fs::File::create(path).unwrap();
        let mut enc = png::Encoder::new(BufWriter::new(file), w, h);
        enc.set_color(png::ColorType::Rgba);
        enc.set_depth(png::BitDepth::Eight);
        let mut writer = enc.write_header().unwrap();
        // cada pixel guarda a própria coluna e linha
        let data: Vec<u8> = (0..h).flat_map(|y| (0..w).flat_map(move |x| [x as u8, y as u8, 0, 255])).collect();
        writer.write_image_data(&data).unwrap();
    }

    #[test]
    fn crops_the_chosen_area() {
        let dir = std::env::temp_dir().join(format!("lumna-crop-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let (src, out) = (dir.join("a.png"), dir.join("b.png"));
        write_png(&src, 100, 200);
        assert_eq!(crop(&src, &out, 0.1, 0.25, 0.5, 0.5).unwrap(), (50, 100));
        let mut reader = png::Decoder::new(std::fs::File::open(&out).unwrap()).read_info().unwrap();
        let mut buf = vec![0; reader.output_buffer_size()];
        reader.next_frame(&mut buf).unwrap();
        // o primeiro pixel do recorte é o (10, 50) da original
        assert_eq!(&buf[..2], &[10, 50]);
        // a área toda, e uma área fora da imagem não quebra
        assert_eq!(crop(&src, &out, 0.0, 0.0, 1.0, 1.0).unwrap(), (100, 200));
        assert!(crop(&src, &out, 0.99, 0.99, 0.5, 0.5).is_ok());
        std::fs::remove_dir_all(&dir).ok();
    }
}
