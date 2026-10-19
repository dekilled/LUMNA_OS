//! Voz da Emma: ouvir (fala → texto) e falar (texto → fala), pelos motores do backend do
//! NewHarnessV2 (`/api/stt` e `/api/tts`: Fish Audio, ou Whisper e Pocket TTS locais no PC).
//!
//! Ouvir: o microfone é gravado aqui (`parec` ou `arecord`, PCM 16 kHz mono) e o fim da fala é
//! detectado pelo volume (1,3 s de silêncio depois de começar a falar). O WAV vai pro backend,
//! que devolve o texto. O estado sai em `lumna://emma-voice`:
//! `{state: "listening", level}` → `"transcribing"` → `"heard", text` (ou `"idle"`, `"error"`).
//!
//! Falar: cada frase entra numa fila; uma thread busca o áudio da próxima enquanto a outra toca a
//! atual (`pw-play` ou `paplay`), então a Emma começa a falar antes da resposta terminar.
//! Estados: `"speaking"` e, com a fila vazia, `"spoken"`.

use std::io::Read;
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::{mpsc, Mutex};
use std::time::Duration;

use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager};

use crate::emma::{self, Emma};

const RATE: u32 = 16_000;
/// 50 ms de áudio (16 kHz, 16 bits, mono).
const CHUNK: usize = 1_600;
/// Silêncio depois da fala que encerra a gravação.
const END_SILENCE: Duration = Duration::from_millis(1_300);
/// Ninguém falou nesse tempo: desiste.
const NO_SPEECH: Duration = Duration::from_secs(7);
const MAX_LISTEN: Duration = Duration::from_secs(25);

static LISTENING: AtomicBool = AtomicBool::new(false);
/// A pessoa tocou no microfone de novo: termina agora e transcreve o que já tem.
static STOP: AtomicBool = AtomicBool::new(false);
/// Fechou o popup: termina sem transcrever.
static CANCEL: AtomicBool = AtomicBool::new(false);

fn emit(app: &AppHandle, event: Value) {
    let _ = app.emit("lumna://emma-voice", event);
}

fn runtime_dir() -> PathBuf {
    std::env::var_os("XDG_RUNTIME_DIR").map(PathBuf::from).unwrap_or_else(std::env::temp_dir)
}

/// O primeiro programa da lista que existe no PATH.
fn find_bin(names: &[&'static str]) -> Option<&'static str> {
    let path = std::env::var_os("PATH")?;
    names.iter().copied().find(|name| std::env::split_paths(&path).any(|dir| dir.join(name).is_file()))
}

/// Os gravadores, na ordem: o do PipeWire pelo protocolo do Pulse, depois o ALSA.
const RECORDERS: [(&str, &[&str]); 2] = [
    ("parec", &["--raw", "--format=s16le", "--rate=16000", "--channels=1", "--latency-msec=50"]),
    ("arecord", &["-q", "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1"]),
];

/// Abre o microfone: o primeiro gravador que entrega áudio (o primeiro pedaço já vem junto).
fn recorder(first: &mut [u8]) -> Result<(Child, std::process::ChildStdout), String> {
    let mut tried = Vec::new();
    for (bin, args) in RECORDERS {
        if find_bin(&[bin]).is_none() {
            continue;
        }
        let Ok(mut child) = Command::new(bin).args(args).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::null()).spawn() else {
            continue;
        };
        let Some(mut stdout) = child.stdout.take() else { continue };
        if stdout.read_exact(first).is_ok() {
            return Ok((child, stdout));
        }
        let _ = child.kill();
        let _ = child.wait();
        tried.push(bin);
    }
    Err(if tried.is_empty() {
        "sem gravador de áudio (instale libpulse ou alsa-utils)".into()
    } else {
        format!("o microfone não abriu ({}); o PipeWire está rodando?", tried.join(", "))
    })
}

/// Volume (RMS) de um pedaço de PCM 16 bits.
fn rms(pcm: &[u8]) -> f64 {
    let samples = pcm.len() / 2;
    if samples == 0 {
        return 0.0;
    }
    let sum: f64 = pcm.chunks_exact(2).map(|b| f64::from(i16::from_le_bytes([b[0], b[1]])).powi(2)).sum();
    (sum / samples as f64).sqrt()
}

/// Onde a fala começa e termina, pelo volume. O ruído do ambiente é medido no começo (o menor
/// volume dos primeiros 400 ms); fala é o que passa bem dele.
#[derive(Default)]
struct Detector {
    floor: Option<f64>,
    loud: u32,
    started: bool,
    quiet: Duration,
    elapsed: Duration,
}

#[derive(Debug, PartialEq)]
enum Heard {
    Waiting,
    Speaking,
    Done,
    Nothing,
}

impl Detector {
    fn push(&mut self, level: f64, step: Duration) -> Heard {
        self.elapsed += step;
        if self.elapsed <= Duration::from_millis(400) {
            self.floor = Some(self.floor.map_or(level, |f| f.min(level)));
        }
        let threshold = (self.floor.unwrap_or(level) * 2.5).max(350.0);
        if !self.started {
            self.loud = if level > threshold { self.loud + 1 } else { 0 };
            // 150 ms seguidos acima do ruído: começou a falar
            if self.loud >= 3 {
                self.started = true;
            } else if self.elapsed >= NO_SPEECH {
                return Heard::Nothing;
            }
            return if self.started { Heard::Speaking } else { Heard::Waiting };
        }
        self.quiet = if level < threshold * 0.8 { self.quiet + step } else { Duration::ZERO };
        if self.quiet >= END_SILENCE || self.elapsed >= MAX_LISTEN {
            Heard::Done
        } else {
            Heard::Speaking
        }
    }
}

/// PCM 16 bits mono → WAV.
fn wav(pcm: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(pcm.len() + 44);
    let data = pcm.len() as u32;
    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&(36 + data).to_le_bytes());
    out.extend_from_slice(b"WAVEfmt ");
    out.extend_from_slice(&16u32.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes()); // PCM
    out.extend_from_slice(&1u16.to_le_bytes()); // mono
    out.extend_from_slice(&RATE.to_le_bytes());
    out.extend_from_slice(&(RATE * 2).to_le_bytes());
    out.extend_from_slice(&2u16.to_le_bytes());
    out.extend_from_slice(&16u16.to_le_bytes());
    out.extend_from_slice(b"data");
    out.extend_from_slice(&data.to_le_bytes());
    out.extend_from_slice(pcm);
    out
}

/// Uma chamada ao backend com o token, pelo curl: devolve (status, corpo).
fn call(app: &AppHandle, path: &str, body: Option<&Value>, extra: &[&str]) -> Result<(u16, Vec<u8>), String> {
    let first = call_once(app, path, body, extra)?;
    if first.0 != 401 {
        return Ok(first);
    }
    // o token venceu antes da hora (o relógio do celular pula de 1970 pra data certa no boot)
    app.state::<Emma>().forget();
    call_once(app, path, body, extra)
}

fn call_once(app: &AppHandle, path: &str, body: Option<&Value>, extra: &[&str]) -> Result<(u16, Vec<u8>), String> {
    let token = app.state::<Emma>().access_token()?;
    let url = format!("{}{path}", emma::base_url());
    let config = emma::curl_config("POST", &url, Some(&token), body);
    // uma frase ou uma fala nunca demoram isso; sem limite, um servidor engasgado prendia a fila
    let mut args = vec!["--connect-timeout", "4", "--max-time", "60", "-w", "\n%{http_code}"];
    args.extend_from_slice(extra);
    let out = emma::curl(&config, &args)?.wait_with_output().map_err(|e| e.to_string())?;
    if !out.status.success() {
        return Err(format!("a Emma não respondeu em {}", emma::base_url()));
    }
    let text = out.stdout;
    let cut = text.iter().rposition(|&b| b == b'\n').unwrap_or(0);
    let code = String::from_utf8_lossy(&text[(cut + 1).min(text.len())..]).trim().parse().unwrap_or(0);
    Ok((code, text[..cut].to_vec()))
}

fn why(code: u16, body: &[u8], what: &str) -> String {
    let detail = serde_json::from_slice::<Value>(body).map(|b| emma::detail(&b)).unwrap_or_default();
    if detail.is_empty() {
        format!("{what} falhou ({code}). Confira a voz no app da Emma → Perfil.")
    } else {
        detail
    }
}

fn transcribe(app: &AppHandle, pcm: &[u8]) -> Result<String, String> {
    let path = runtime_dir().join("lumna-emma-fala.wav");
    std::fs::write(&path, wav(pcm)).map_err(|e| e.to_string())?;
    let form = format!("audio=@{};type=audio/wav;filename=fala.wav", path.display());
    let result = call(app, "/api/stt", None, &["-F", &form]);
    let _ = std::fs::remove_file(&path);
    let (code, body) = result?;
    if code != 200 {
        return Err(why(code, &body, "A transcrição"));
    }
    let reply: Value = serde_json::from_slice(&body).unwrap_or(Value::Null);
    Ok(reply["text"].as_str().unwrap_or_default().trim().to_string())
}

fn listen(app: &AppHandle) -> Result<Option<String>, String> {
    let mut chunk = vec![0u8; CHUNK];
    let (mut child, mut stdout) = recorder(&mut chunk)?;
    let mut pcm = Vec::new();
    let mut detector = Detector::default();
    let mut outcome = Heard::Waiting;
    let mut tick = 0u32;
    // o primeiro pedaço já veio com o gravador
    let mut have = true;
    loop {
        if CANCEL.load(Ordering::SeqCst) {
            outcome = Heard::Nothing;
            break;
        }
        if STOP.load(Ordering::SeqCst) {
            outcome = if detector.started { Heard::Done } else { Heard::Nothing };
            break;
        }
        if !have && stdout.read_exact(&mut chunk).is_err() {
            break;
        }
        have = false;
        pcm.extend_from_slice(&chunk);
        let level = rms(&chunk);
        outcome = detector.push(level, Duration::from_millis(50));
        tick += 1;
        if tick % 2 == 0 {
            // 0..1, em escala de volume que dá pra ver (fala normal fica no meio)
            emit(app, json!({ "state": "listening", "level": (level / 6000.0).min(1.0), "speech": detector.started }));
        }
        if matches!(outcome, Heard::Done | Heard::Nothing) {
            break;
        }
    }
    let _ = child.kill();
    let _ = child.wait();
    if outcome == Heard::Nothing || pcm.is_empty() {
        return Ok(None);
    }
    emit(app, json!({ "state": "transcribing" }));
    transcribe(app, &pcm).map(Some)
}

/// Começa a ouvir. O resultado chega pelo `lumna://emma-voice`.
#[tauri::command]
pub fn emma_listen(app: AppHandle) -> Result<(), String> {
    if LISTENING.swap(true, Ordering::SeqCst) {
        return Ok(());
    }
    // a Emma para de falar quando a pessoa vai falar
    stop_speaking();
    STOP.store(false, Ordering::SeqCst);
    CANCEL.store(false, Ordering::SeqCst);
    emit(&app, json!({ "state": "listening", "level": 0.0, "speech": false }));
    std::thread::spawn(move || {
        let result = listen(&app);
        LISTENING.store(false, Ordering::SeqCst);
        match result {
            Ok(Some(text)) if !text.is_empty() => emit(&app, json!({ "state": "heard", "text": text })),
            Ok(_) => emit(&app, json!({ "state": "idle" })),
            Err(message) => emit(&app, json!({ "state": "error", "message": message })),
        }
    });
    Ok(())
}

/// Para de ouvir: `cancel` descarta; senão transcreve o que já gravou.
#[tauri::command]
pub fn emma_listen_stop(cancel: bool) {
    if cancel {
        CANCEL.store(true, Ordering::SeqCst);
    } else {
        STOP.store(true, Ordering::SeqCst);
    }
}

// ---- falar ----

/// Muda a cada "pare de falar": o que estava na fila de antes é descartado.
static GENERATION: AtomicU64 = AtomicU64::new(0);
/// Frases na fila ou tocando (zero = terminou de falar).
static PENDING: AtomicUsize = AtomicUsize::new(0);
static PLAYER: Mutex<Option<Child>> = Mutex::new(None);
static QUEUE: Mutex<Option<mpsc::Sender<(u64, String)>>> = Mutex::new(None);
/// Erro já avisado nesta fala (um aviso só, não um por frase).
static WARNED: AtomicU64 = AtomicU64::new(u64::MAX);

fn current(generation: u64) -> bool {
    GENERATION.load(Ordering::SeqCst) == generation
}

fn stop_speaking() {
    GENERATION.fetch_add(1, Ordering::SeqCst);
    if let Some(mut child) = PLAYER.lock().unwrap().take() {
        let _ = child.kill();
        let _ = child.wait();
    }
}

fn finished(app: &AppHandle) {
    if PENDING.fetch_sub(1, Ordering::SeqCst) == 1 {
        emit(app, json!({ "state": "spoken" }));
    }
}

fn synthesize(app: &AppHandle, text: &str, index: u64) -> Result<PathBuf, String> {
    let path = runtime_dir().join(format!("lumna-emma-voz-{}.audio", index % 4));
    let out = path.to_string_lossy().to_string();
    let (code, body) = call(app, "/api/tts", Some(&json!({ "text": text })), &["-o", &out])?;
    if code != 200 {
        let body = std::fs::read(&path).unwrap_or(body);
        let _ = std::fs::remove_file(&path);
        return Err(why(code, &body, "A voz"));
    }
    Ok(path)
}

/// Quanto o áudio dura, no máximo: o WAV diz no cabeçalho; o MP3 é estimado pelo tamanho na
/// menor taxa que o Fish usa (32 kbit/s), então sobra tempo.
fn max_duration(path: &PathBuf) -> Duration {
    let Ok(bytes) = std::fs::read(path) else { return Duration::from_secs(5) };
    let secs = if bytes.starts_with(b"RIFF") && bytes.len() > 44 {
        let rate = u32::from_le_bytes([bytes[28], bytes[29], bytes[30], bytes[31]]).max(1);
        bytes.len() as f64 / f64::from(rate)
    } else {
        bytes.len() as f64 / 4_000.0
    };
    Duration::from_secs_f64(secs + 3.0)
}

fn play(path: &PathBuf, generation: u64) {
    let Some(bin) = find_bin(&["pw-play", "paplay"]) else { return };
    let limit = max_duration(path);
    // "Emma" no mixer de áudio (o pw-play lê PIPEWIRE_PROPS; o paplay, PULSE_PROP)
    let Ok(child) = Command::new(bin)
        .arg(path)
        .env("PIPEWIRE_PROPS", "{ application.name = \"Emma\" media.name = \"Emma\" }")
        .env("PULSE_PROP", "application.name=Emma")
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
    else {
        return;
    };
    *PLAYER.lock().unwrap() = Some(child);
    let started = std::time::Instant::now();
    loop {
        std::thread::sleep(Duration::from_millis(40));
        let mut player = PLAYER.lock().unwrap();
        let Some(child) = player.as_mut() else { return };
        // parado pela pessoa, ou o tocador não saiu sozinho no fim do áudio (o resto da fila esperava)
        if !current(generation) || started.elapsed() > limit {
            if current(generation) {
                eprintln!("lumna-shell: emma voz: {bin} não terminou; parado");
            }
            let _ = child.kill();
        }
        if child.try_wait().map_or(true, |s| s.is_some()) {
            player.take();
            return;
        }
    }
}

/// As duas threads da fala: uma busca o áudio de cada frase, a outra toca (na ordem).
fn speaker(app: AppHandle) -> mpsc::Sender<(u64, String)> {
    let (tx, rx) = mpsc::channel::<(u64, String)>();
    let (ready_tx, ready_rx) = mpsc::sync_channel::<(u64, Option<PathBuf>)>(1);
    let fetcher = app.clone();
    std::thread::spawn(move || {
        let mut index = 0u64;
        for (generation, text) in rx {
            index += 1;
            let path = if current(generation) {
                match synthesize(&fetcher, &text, index) {
                    Ok(path) => Some(path),
                    Err(message) => {
                        if WARNED.swap(generation, Ordering::SeqCst) != generation {
                            emit(&fetcher, json!({ "state": "error", "message": message }));
                        }
                        None
                    }
                }
            } else {
                None
            };
            if ready_tx.send((generation, path)).is_err() {
                return;
            }
        }
    });
    std::thread::spawn(move || {
        for (generation, path) in ready_rx {
            if let Some(path) = path {
                if current(generation) {
                    emit(&app, json!({ "state": "speaking" }));
                    play(&path, generation);
                }
                let _ = std::fs::remove_file(&path);
            }
            finished(&app);
        }
    });
    tx
}

/// Fala uma frase (entra na fila, depois das que já estão lá).
#[tauri::command]
pub fn emma_speak(app: AppHandle, text: String) -> Result<(), String> {
    let text = text.trim().to_string();
    if text.is_empty() {
        return Ok(());
    }
    if find_bin(&["pw-play", "paplay"]).is_none() {
        return Err("sem tocador de áudio (pw-play ou paplay)".into());
    }
    let mut queue = QUEUE.lock().unwrap();
    let tx = queue.get_or_insert_with(|| speaker(app.clone()));
    PENDING.fetch_add(1, Ordering::SeqCst);
    if tx.send((GENERATION.load(Ordering::SeqCst), text)).is_err() {
        PENDING.fetch_sub(1, Ordering::SeqCst);
        *queue = None;
        return Err("a fala parou; tente de novo".into());
    }
    Ok(())
}

/// Para de falar na hora (e esquece o que estava na fila).
#[tauri::command]
pub fn emma_speak_stop() {
    stop_speaking();
}

/// Para tudo o que é voz: a fala e o microfone (sem transcrever).
pub fn stop_all() {
    stop_speaking();
    if LISTENING.load(Ordering::SeqCst) {
        CANCEL.store(true, Ordering::SeqCst);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn feed(d: &mut Detector, level: f64, ms: u64) -> Heard {
        let mut last = Heard::Waiting;
        for _ in 0..ms / 50 {
            last = d.push(level, Duration::from_millis(50));
        }
        last
    }

    #[test]
    fn detects_speech_and_silence() {
        let mut d = Detector::default();
        assert_eq!(feed(&mut d, 100.0, 500), Heard::Waiting);
        assert_eq!(feed(&mut d, 3000.0, 1000), Heard::Speaking);
        assert_eq!(feed(&mut d, 120.0, 1000), Heard::Speaking);
        assert_eq!(feed(&mut d, 120.0, 400), Heard::Done);
    }

    #[test]
    fn gives_up_without_speech() {
        let mut d = Detector::default();
        assert_eq!(feed(&mut d, 100.0, 8000), Heard::Nothing);
    }

    #[test]
    fn wav_header() {
        let w = wav(&[0u8; 3200]);
        assert_eq!(&w[..4], b"RIFF");
        assert_eq!(&w[8..16], b"WAVEfmt ");
        assert_eq!(u32::from_le_bytes([w[24], w[25], w[26], w[27]]), 16_000);
        assert_eq!(u32::from_le_bytes([w[40], w[41], w[42], w[43]]), 3200);
        assert_eq!(w.len(), 3244);
        assert!(rms(&[0, 0, 0, 0]) == 0.0);
    }
}
