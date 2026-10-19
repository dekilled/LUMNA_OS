//! Captura de tela, no PC e no celular: a tela congela (o `grim` tira a foto inteira do monitor em
//! foco) e aparece a camada `capture` por cima, com a foto e um retângulo pra escolher a área
//! (arrasta, estica pelas alças, ou "Tela toda"). Salvar recorta a foto congelada (o que estava na
//! tela no instante da captura, não o que veio depois) e guarda em Imagens/Capturas; dá pra só
//! copiar também. Abre por `lumna-ctl capture` (Print no PC), pelo atalho da central no celular e
//! pela Emma.

use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};

use crate::{layer, SharedCompositor};
use lumna_core::Monitor;

/// O monitor da captura em andamento (a gravação usa as medidas dele).
static MONITOR: Mutex<Option<Monitor>> = Mutex::new(None);

/// A gravação em andamento: o wf-recorder, o arquivo e quando começou.
struct Recording {
    child: Child,
    path: PathBuf,
    since: Instant,
}
static RECORDING: Mutex<Option<Recording>> = Mutex::new(None);

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
    // gravando: o mesmo atalho (Print, o da central) para a gravação
    if RECORDING.lock().unwrap().is_some() {
        return stop_recording(app).map(|_| ());
    }
    let focused = app.state::<SharedCompositor>().monitors()?.into_iter().find(|m| m.focused);
    *MONITOR.lock().unwrap() = focused.clone();
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

/// Vídeos/Gravações.
pub fn records_dir() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    let videos = Command::new("xdg-user-dir")
        .arg("VIDEOS")
        .output()
        .ok()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .filter(|p| !p.is_empty() && Path::new(p) != home)
        .map(PathBuf::from)
        .unwrap_or_else(|| home.join("Vídeos"));
    videos.join("Gravações")
}

/// A área (frações) em coordenadas do Hyprland, como o wf-recorder (e o grim) pedem: "x,y lxa".
pub fn region(m: &Monitor, x: f64, y: f64, w: f64, h: f64) -> String {
    let (mw, mh) = (f64::from(m.width), f64::from(m.height));
    let rx = m.x + (x.clamp(0.0, 1.0) * mw).round() as i32;
    let ry = m.y + (y.clamp(0.0, 1.0) * mh).round() as i32;
    let rw = ((w.clamp(0.0, 1.0) * mw).round() as i32).max(2);
    let rh = ((h.clamp(0.0, 1.0) * mh).round() as i32).max(2);
    format!("{rx},{ry} {rw}x{rh}")
}

#[derive(Debug, Clone, Serialize)]
struct RecState {
    recording: bool,
    /// Segundos desde que começou.
    elapsed: u64,
}

fn rec_state(app: &AppHandle) {
    let state = RECORDING.lock().unwrap().as_ref().map(|r| r.since.elapsed().as_secs());
    let _ = app.emit("lumna://recording", RecState { recording: state.is_some(), elapsed: state.unwrap_or(0) });
}

/// Começa a gravar a área escolhida (em frações do monitor da captura). A pílula "gravando" fica
/// no topo da tela, com o tempo e o parar.
#[tauri::command]
pub async fn capture_record(app: AppHandle, x: f64, y: f64, w: f64, h: f64) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        if RECORDING.lock().unwrap().is_some() {
            return Err("já está gravando".to_string());
        }
        let monitor = MONITOR.lock().unwrap().clone();
        let dir = records_dir();
        std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
        let stamp = Command::new("date").arg("+%Y-%m-%d %H-%M-%S").output().map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string()).unwrap_or_default();
        let path = dir.join(format!("Gravação {stamp}.mp4"));
        hide(&app);
        let _ = std::fs::remove_file(frozen());
        // a camada de recorte sai da frente antes de começar
        std::thread::sleep(Duration::from_millis(250));
        let mut cmd = Command::new("wf-recorder");
        let whole = x <= 0.0 && y <= 0.0 && w >= 1.0 && h >= 1.0;
        match (&monitor, whole) {
            (Some(m), true) => {
                cmd.args(["-o", &m.name]);
            }
            (Some(m), false) => {
                cmd.args(["-g", &region(m, x, y, w, h)]);
            }
            (None, _) => {}
        }
        // H.264 rápido (o celular codifica na CPU) e com largura/altura pares, que o H.264 exige
        cmd.args(["-c", "libx264", "-p", "preset=ultrafast", "-p", "crf=26", "-F", "scale=trunc(iw/2)*2:trunc(ih/2)*2", "-y", "-f"])
            .arg(&path)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null());
        let child = cmd.spawn().map_err(|e| format!("wf-recorder: {e} (instale o pacote wf-recorder)"))?;
        *RECORDING.lock().unwrap() = Some(Recording { child, path, since: Instant::now() });
        let handle = app.clone();
        let _ = app.run_on_main_thread(move || {
            // no celular a pílula ia parar no vídeo: lá o aviso é o ponto vermelho na barra de
            // status (pequeno, como no Android) e o parar fica nele e na central
            if !crate::MOBILE.load(std::sync::atomic::Ordering::Relaxed) {
                layer::set_visible("recording", true);
            }
            rec_state(&handle);
        });
        // o tempo na pílula, a cada segundo; e se o wf-recorder morrer sozinho, a pílula some
        let ticker = app.clone();
        std::thread::spawn(move || loop {
            std::thread::sleep(Duration::from_secs(1));
            let died = {
                let mut rec = RECORDING.lock().unwrap();
                match rec.as_mut() {
                    None => return,
                    Some(r) => r.child.try_wait().ok().flatten().is_some(),
                }
            };
            if died {
                let _ = stop_recording(&ticker);
                return;
            }
            rec_state(&ticker);
        });
        Ok(())
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Para a gravação (o wf-recorder fecha o MP4 com SIGINT) e avisa onde ficou.
pub fn stop_recording(app: &AppHandle) -> Result<String, String> {
    let Some(mut rec) = RECORDING.lock().unwrap().take() else { return Ok(String::new()) };
    // SAFETY: só manda o sinal pro processo que este shell abriu
    unsafe {
        libc::kill(rec.child.id() as i32, libc::SIGINT);
    }
    let waited = Instant::now();
    while rec.child.try_wait().ok().flatten().is_none() && waited.elapsed() < Duration::from_secs(8) {
        std::thread::sleep(Duration::from_millis(100));
    }
    let _ = rec.child.kill();
    let _ = rec.child.wait();
    let handle = app.clone();
    let _ = app.run_on_main_thread(move || {
        layer::set_visible("recording", false);
        rec_state(&handle);
    });
    let path = rec.path.to_string_lossy().into_owned();
    if rec.path.metadata().is_ok_and(|m| m.len() > 0) {
        let secs = rec.since.elapsed().as_secs();
        let body = format!("{} · {}:{:02}", rec.path.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(), secs / 60, secs % 60);
        crate::notifications::local_open(app, "Gravação", "Gravação salva", &body, false, Some(format!("gallery:{path}")));
        Ok(path)
    } else {
        let _ = std::fs::remove_file(&rec.path);
        crate::notifications::local(app, "Gravação", "A gravação não deu certo", "O wf-recorder não gravou nada (veja journalctl -t lumna-shell)", true);
        Err("a gravação não deu certo".into())
    }
}

#[tauri::command]
pub async fn capture_record_stop(app: AppHandle) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || stop_recording(&app)).await.map_err(|e| e.to_string())?
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
    fn region_in_layout_coordinates() {
        let m = Monitor { name: "DSI-1".into(), x: 0, y: 0, width: 432, height: 936, scale: 2.5, ..Default::default() };
        assert_eq!(region(&m, 0.25, 0.5, 0.5, 0.25), "108,468 216x234");
        let pc = Monitor { name: "HDMI".into(), x: 1920, y: 0, width: 1920, height: 1080, scale: 1.0, ..Default::default() };
        assert_eq!(region(&pc, 0.0, 0.0, 1.0, 1.0), "1920,0 1920x1080");
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
