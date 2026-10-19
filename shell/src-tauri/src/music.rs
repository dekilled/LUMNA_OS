//! LUMNA Music (offline): as músicas da pasta Músicas, com as tags (título, artista, álbum, capa)
//! lidas pelo `lofty` e guardadas num índice (só relê o arquivo que mudou).
//!
//! Quem toca é um mpv sem janela (`--idle`), comandado pelo socket JSON dele: a música continua
//! com o app fechado. A fila e o "próxima/anterior" ficam aqui. O estado vai pra tela pelo evento
//! `lumna://music` (o app, o cartão da central e o do bloqueio) e pro resto do sistema pelo MPRIS
//! (`org.mpris.MediaPlayer2.lumna_music`): os botões do fone, o `playerctl`, a Emma, o Connect.

use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixStream;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, UNIX_EPOCH};

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager};

const EXTS: [&str; 9] = ["mp3", "flac", "ogg", "opus", "m4a", "aac", "wav", "wma", "alac"];
const DEPTH: usize = 6;
const MAX: usize = 20_000;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Track {
    pub path: String,
    pub title: String,
    pub artist: String,
    pub album: String,
    pub album_artist: String,
    /// Número da faixa no álbum (0 = sem).
    pub track: u32,
    pub disc: u32,
    pub year: u32,
    /// Segundos.
    pub duration: f64,
    /// A capa (no cache, ou a imagem da pasta).
    pub cover: Option<String>,
    /// Modificação do arquivo (o índice relê só o que mudou).
    pub mtime: u64,
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

fn cache_dir() -> PathBuf {
    std::env::var_os("XDG_CACHE_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".cache")).join("lumna").join("music")
}

/// As pastas de música: a do XDG (Músicas) e as de nome comum, as que existem.
pub fn roots() -> Vec<PathBuf> {
    let xdg = Command::new("xdg-user-dir")
        .arg("MUSIC")
        .output()
        .ok()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .filter(|p| !p.is_empty() && Path::new(p) != home())
        .map(PathBuf::from);
    let mut out: Vec<PathBuf> = Vec::new();
    for dir in xdg.into_iter().chain([home().join("Músicas"), home().join("Música"), home().join("Music")]) {
        if dir.is_dir() && !out.iter().any(|d| d == &dir) {
            out.push(dir);
        }
    }
    out
}

fn is_audio(path: &Path) -> bool {
    path.extension().and_then(|e| e.to_str()).is_some_and(|e| EXTS.contains(&e.to_ascii_lowercase().as_str()))
}

fn walk(dir: &Path, depth: usize, found: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        if found.len() >= MAX {
            return;
        }
        let path = entry.path();
        if entry.file_name().to_string_lossy().starts_with('.') {
            continue;
        }
        let Ok(kind) = entry.file_type() else { continue };
        if kind.is_dir() {
            if depth > 0 {
                walk(&path, depth - 1, found);
            }
        } else if is_audio(&path) {
            found.push(path);
        }
    }
}

fn mtime(meta: &std::fs::Metadata) -> u64 {
    meta.modified().ok().and_then(|t| t.duration_since(UNIX_EPOCH).ok()).map_or(0, |d| d.as_secs())
}

/// A imagem da pasta (cover.jpg, folder.png...), quando o arquivo não traz capa.
fn folder_cover(dir: &Path) -> Option<String> {
    let entries = std::fs::read_dir(dir).ok()?;
    let mut images: Vec<PathBuf> = entries
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().and_then(|e| e.to_str()).is_some_and(|e| matches!(e.to_ascii_lowercase().as_str(), "jpg" | "jpeg" | "png" | "webp")))
        .collect();
    images.sort_by_key(|p| {
        let name = p.file_stem().map(|s| s.to_string_lossy().to_lowercase()).unwrap_or_default();
        ["cover", "folder", "front", "capa", "album"].iter().position(|n| name.starts_with(n)).unwrap_or(9)
    });
    images.into_iter().next().map(|p| p.to_string_lossy().into_owned())
}

/// Nome do arquivo sem a extensão e sem o número da faixa ("03 - Yellow" → "Yellow").
fn title_from_file(path: &Path) -> String {
    let stem = path.file_stem().map(|s| s.to_string_lossy().into_owned()).unwrap_or_default();
    let trimmed = stem.trim_start_matches(|c: char| c.is_ascii_digit()).trim_start_matches([' ', '-', '.', '_']).trim();
    if trimmed.is_empty() { stem } else { trimmed.to_string() }
}

fn read_track(path: &Path, meta: &std::fs::Metadata) -> Track {
    use lofty::prelude::*;
    let mut track = Track {
        path: path.to_string_lossy().into_owned(),
        title: title_from_file(path),
        artist: String::new(),
        album: String::new(),
        album_artist: String::new(),
        track: 0,
        disc: 0,
        year: 0,
        duration: 0.0,
        cover: None,
        mtime: mtime(meta),
    };
    if let Ok(tagged) = lofty::probe::Probe::open(path).and_then(|p| p.read()) {
        track.duration = tagged.properties().duration().as_secs_f64();
        if let Some(tag) = tagged.primary_tag().or_else(|| tagged.first_tag()) {
            let text = |v: Option<std::borrow::Cow<'_, str>>| v.map(|s| s.trim().to_string()).filter(|s| !s.is_empty());
            if let Some(t) = text(tag.title()) {
                track.title = t;
            }
            track.artist = text(tag.artist()).unwrap_or_default();
            track.album = text(tag.album()).unwrap_or_default();
            track.album_artist = tag.get_string(&lofty::tag::ItemKey::AlbumArtist).map(|s| s.trim().to_string()).unwrap_or_default();
            track.track = tag.track().unwrap_or(0);
            track.disc = tag.disk().unwrap_or(0);
            track.year = tag.year().unwrap_or(0);
            if let Some(picture) = tag.pictures().first() {
                // uma capa por álbum (as faixas do mesmo álbum dividem o arquivo)
                let key = format!("{:x}", md5::compute(format!("{}|{}|{}", track.album_artist_or_artist(), track.album, if track.album.is_empty() { &track.path } else { "" })));
                let ext = match picture.mime_type().map(|m| m.as_str()) {
                    Some("image/png") => "png",
                    _ => "jpg",
                };
                let out = cache_dir().join("covers").join(format!("{key}.{ext}"));
                if out.exists() || (std::fs::create_dir_all(out.parent().unwrap_or(Path::new("/"))).is_ok() && std::fs::write(&out, picture.data()).is_ok()) {
                    track.cover = Some(out.to_string_lossy().into_owned());
                }
            }
        }
    }
    if track.cover.is_none() {
        track.cover = path.parent().and_then(folder_cover);
    }
    track
}

impl Track {
    fn album_artist_or_artist(&self) -> &str {
        if self.album_artist.is_empty() { &self.artist } else { &self.album_artist }
    }
}

fn index_path() -> PathBuf {
    cache_dir().join("index.json")
}

/// A biblioteca: lê as pastas e o índice; só abre (pras tags) o arquivo novo ou mudado.
pub fn library() -> Vec<Track> {
    let old: HashMap<String, Track> = std::fs::read(index_path())
        .ok()
        .and_then(|b| serde_json::from_slice::<Vec<Track>>(&b).ok())
        .unwrap_or_default()
        .into_iter()
        .map(|t| (t.path.clone(), t))
        .collect();
    let mut found = Vec::new();
    for root in roots() {
        walk(&root, DEPTH, &mut found);
    }
    let mut changed = old.len() != found.len();
    let mut tracks: Vec<Track> = found
        .iter()
        .filter_map(|path| {
            let meta = std::fs::metadata(path).ok()?;
            let key = path.to_string_lossy();
            match old.get(key.as_ref()) {
                Some(t) if t.mtime == mtime(&meta) && t.cover.as_ref().is_none_or(|c| Path::new(c).exists()) => Some(t.clone()),
                _ => {
                    changed = true;
                    Some(read_track(path, &meta))
                }
            }
        })
        .collect();
    tracks.sort_by(|a, b| {
        (a.album_artist_or_artist().to_lowercase(), a.album.to_lowercase(), a.disc, a.track, a.title.to_lowercase()).cmp(&(
            b.album_artist_or_artist().to_lowercase(),
            b.album.to_lowercase(),
            b.disc,
            b.track,
            b.title.to_lowercase(),
        ))
    });
    if changed {
        let _ = std::fs::create_dir_all(cache_dir());
        let _ = std::fs::write(index_path(), serde_json::to_vec(&tracks).unwrap_or_default());
    }
    tracks
}

// ---- o player ----

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "lowercase")]
pub enum Repeat {
    #[default]
    Off,
    All,
    One,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct MusicState {
    pub queue: Vec<Track>,
    /// A faixa atual na fila (None: parado, nada carregado).
    pub index: Option<usize>,
    pub playing: bool,
    /// Segundos.
    pub position: f64,
    pub duration: f64,
    pub shuffle: bool,
    pub repeat: Repeat,
    /// Volume do player, 0 a 100 (None: 100). O do sistema fica no mixer.
    pub volume: Option<u8>,
}

impl MusicState {
    pub fn current(&self) -> Option<&Track> {
        self.index.and_then(|i| self.queue.get(i))
    }
}

struct Engine {
    child: Option<Child>,
    socket: Option<UnixStream>,
    /// A ordem embaralhada (índices da fila), quando o aleatório está ligado.
    order: Vec<usize>,
}

#[derive(Default)]
pub struct Music {
    state: Mutex<MusicState>,
    engine: Mutex<Option<Engine>>,
    last_emit: Mutex<Option<Instant>>,
}

pub type SharedMusic = Arc<Music>;

fn socket_path() -> PathBuf {
    std::env::var_os("XDG_RUNTIME_DIR").map(PathBuf::from).unwrap_or_else(std::env::temp_dir).join("lumna-music.sock")
}

fn send(engine: &mut Engine, command: Value) -> Result<(), String> {
    let socket = engine.socket.as_mut().ok_or("o player não está rodando")?;
    let mut line = json!({ "command": command }).to_string();
    line.push('\n');
    socket.write_all(line.as_bytes()).map_err(|e| format!("player: {e}"))
}

/// Liga o mpv (uma vez) e o leitor dos eventos dele.
fn ensure_engine(app: &AppHandle) -> Result<(), String> {
    let music = app.state::<SharedMusic>();
    let mut guard = music.engine.lock().unwrap();
    if let Some(engine) = guard.as_mut() {
        if engine.child.as_mut().is_some_and(|c| matches!(c.try_wait(), Ok(None))) {
            return Ok(());
        }
    }
    let path = socket_path();
    // um mpv que ficou de um shell anterior (reiniciado) para de tocar sozinho por aí
    let _ = Command::new("pkill").args(["-f", &format!("input-ipc-server={}", path.display())]).status();
    let _ = std::fs::remove_file(&path);
    let volume = music.state.lock().unwrap().volume.unwrap_or(100);
    let child = Command::new("mpv")
        .args(["--idle=yes", "--no-video", "--no-terminal", "--audio-display=no", "--keep-open=no", "--gapless-audio=weak"])
        // o nome no mixer de áudio (senão aparece "mpv")
        .args(["--audio-client-name=LUMNA Music", &format!("--volume={volume}")])
        .arg(format!("--input-ipc-server={}", path.display()))
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map_err(|_| "instale o mpv pra tocar música (sudo pacman -S mpv)".to_string())?;
    let mut socket = None;
    for _ in 0..50 {
        if let Ok(s) = UnixStream::connect(&path) {
            socket = Some(s);
            break;
        }
        std::thread::sleep(Duration::from_millis(60));
    }
    let socket = socket.ok_or("o player não abriu")?;
    let reader = socket.try_clone().map_err(|e| e.to_string())?;
    let mut engine = Engine { child: Some(child), socket: Some(socket), order: Vec::new() };
    for (id, prop) in [(1, "time-pos"), (2, "duration"), (3, "pause")] {
        send(&mut engine, json!(["observe_property", id, prop]))?;
    }
    *guard = Some(engine);
    drop(guard);
    let handle = app.clone();
    std::thread::spawn(move || {
        for line in BufReader::new(reader).lines().map_while(Result::ok) {
            if let Ok(event) = serde_json::from_str::<Value>(&line) {
                on_event(&handle, &event);
            }
        }
    });
    Ok(())
}

fn on_event(app: &AppHandle, event: &Value) {
    let music = app.state::<SharedMusic>();
    match event["event"].as_str() {
        Some("property-change") => {
            let mut s = music.state.lock().unwrap();
            match event["name"].as_str() {
                Some("time-pos") => s.position = event["data"].as_f64().unwrap_or(s.position),
                Some("duration") => {
                    if let Some(d) = event["data"].as_f64() {
                        s.duration = d;
                    }
                }
                Some("pause") => {
                    let paused = event["data"].as_bool().unwrap_or(false);
                    if s.index.is_some() {
                        s.playing = !paused;
                    }
                    drop(s);
                    emit(app, true);
                    return;
                }
                _ => return,
            }
            drop(s);
            emit(app, false);
        }
        // só o fim de verdade ("eof"); trocar de faixa (loadfile) termina a anterior com "stop"
        Some("end-file") if event["reason"] == "eof" => {
            let _ = advance(app, 1, true);
        }
        _ => {}
    }
}

/// Manda o estado pra tela e pro MPRIS. `now`: já (mudou de faixa, pausou); senão no máximo 1x/s.
fn emit(app: &AppHandle, now: bool) {
    let music = app.state::<SharedMusic>();
    {
        let mut last = music.last_emit.lock().unwrap();
        if !now && last.is_some_and(|at| at.elapsed() < Duration::from_millis(900)) {
            return;
        }
        *last = Some(Instant::now());
    }
    let state = public_state(&music.state.lock().unwrap());
    let _ = app.emit("lumna://music", &state);
    if now {
        crate::mpris::changed(app);
    }
}

/// O que vai pra tela: a fila pode ser enorme, então só a vizinhança da faixa atual.
#[derive(Debug, Clone, Serialize)]
pub struct PublicState {
    pub current: Option<Track>,
    pub index: Option<usize>,
    pub length: usize,
    pub playing: bool,
    pub position: f64,
    pub duration: f64,
    pub shuffle: bool,
    pub repeat: Repeat,
    pub volume: u8,
    /// As próximas (até 50); a primeira é a posição `index + 1` da fila.
    pub next: Vec<Track>,
}

fn public_state(s: &MusicState) -> PublicState {
    let next = match s.index {
        Some(i) => s.queue.iter().skip(i + 1).take(50).cloned().collect(),
        None => Vec::new(),
    };
    PublicState {
        current: s.current().cloned(),
        index: s.index,
        length: s.queue.len(),
        playing: s.playing,
        position: s.position,
        duration: s.duration,
        shuffle: s.shuffle,
        repeat: s.repeat,
        volume: s.volume.unwrap_or(100),
        next,
    }
}

/// Toca a faixa `index` da fila.
fn load(app: &AppHandle, index: usize) -> Result<(), String> {
    ensure_engine(app)?;
    let music = app.state::<SharedMusic>();
    let path = {
        let mut s = music.state.lock().unwrap();
        let track = s.queue.get(index).ok_or("a fila acabou")?.clone();
        s.index = Some(index);
        s.position = 0.0;
        s.duration = track.duration;
        s.playing = true;
        track.path
    };
    {
        let mut guard = music.engine.lock().unwrap();
        let engine = guard.as_mut().ok_or("o player não está rodando")?;
        send(engine, json!(["loadfile", path, "replace"]))?;
        send(engine, json!(["set_property", "pause", false]))?;
    }
    emit(app, true);
    Ok(())
}

/// Próxima (`step` = 1) ou anterior (-1), seguindo o aleatório e a repetição. `auto`: a música acabou.
fn advance(app: &AppHandle, step: i64, auto: bool) -> Result<(), String> {
    let music = app.state::<SharedMusic>();
    let (len, index, repeat, shuffle) = {
        let s = music.state.lock().unwrap();
        (s.queue.len(), s.index, s.repeat, s.shuffle)
    };
    let Some(index) = index else { return Ok(()) };
    if len == 0 {
        return Ok(());
    }
    if auto && repeat == Repeat::One {
        return load(app, index);
    }
    // a ordem: a da fila, ou a embaralhada
    let order: Vec<usize> = if shuffle {
        let mut guard = music.engine.lock().unwrap();
        let engine = guard.as_mut();
        match engine {
            Some(e) if e.order.len() == len => e.order.clone(),
            Some(e) => {
                e.order = shuffled(len, index);
                e.order.clone()
            }
            None => (0..len).collect(),
        }
    } else {
        (0..len).collect()
    };
    let pos = order.iter().position(|&i| i == index).unwrap_or(0) as i64;
    let next = pos + step;
    let target = if next < 0 {
        if repeat == Repeat::All { Some(order[len - 1]) } else { Some(order[0]) }
    } else if next as usize >= len {
        if repeat == Repeat::All { Some(order[0]) } else { None }
    } else {
        Some(order[next as usize])
    };
    match target {
        Some(i) => load(app, i),
        None => {
            // acabou a fila: para no começo da última
            let mut s = music.state.lock().unwrap();
            s.playing = false;
            s.position = 0.0;
            drop(s);
            emit(app, true);
            Ok(())
        }
    }
}

/// Embaralha com a atual na frente (xorshift com a hora: não precisa de criptografia).
fn shuffled(len: usize, first: usize) -> Vec<usize> {
    let mut seed = std::time::SystemTime::now().duration_since(UNIX_EPOCH).map_or(7, |d| d.as_nanos() as u64) | 1;
    let mut rest: Vec<usize> = (0..len).filter(|&i| i != first).collect();
    for i in (1..rest.len()).rev() {
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;
        rest.swap(i, (seed % (i as u64 + 1)) as usize);
    }
    std::iter::once(first).chain(rest).collect()
}

fn set_pause(app: &AppHandle, pause: bool) -> Result<(), String> {
    let music = app.state::<SharedMusic>();
    if music.state.lock().unwrap().index.is_none() {
        return Ok(());
    }
    // depois do fim da fila (parado no começo da última): tocar recomeça ela
    let ended = { music.engine.lock().unwrap().as_mut().is_none_or(|e| e.child.as_mut().is_none_or(|c| !matches!(c.try_wait(), Ok(None)))) };
    let idle = {
        let s = music.state.lock().unwrap();
        !s.playing && s.position == 0.0
    };
    if !pause && (ended || idle) {
        let index = music.state.lock().unwrap().index.unwrap_or(0);
        return load(app, index);
    }
    let mut guard = music.engine.lock().unwrap();
    let engine = guard.as_mut().ok_or("o player não está rodando")?;
    send(engine, json!(["set_property", "pause", pause]))
}

// ---- o que os outros chamam (MPRIS, Emma, lumna-ctl) ----

pub fn control(app: &AppHandle, action: &str) -> Result<(), String> {
    let playing = app.state::<SharedMusic>().state.lock().unwrap().playing;
    match action {
        "play" => set_pause(app, false),
        "pause" => set_pause(app, true),
        "toggle" | "play-pause" => set_pause(app, playing),
        "next" => advance(app, 1, false),
        "previous" | "prev" => {
            // como nos players: no meio da música, "anterior" volta pro começo dela
            let position = app.state::<SharedMusic>().state.lock().unwrap().position;
            if position > 4.0 { seek(app, 0.0) } else { advance(app, -1, false) }
        }
        "stop" => {
            let music = app.state::<SharedMusic>();
            if let Some(engine) = music.engine.lock().unwrap().as_mut() {
                let _ = send(engine, json!(["stop"]));
            }
            let mut s = music.state.lock().unwrap();
            s.playing = false;
            s.position = 0.0;
            drop(s);
            emit(app, true);
            Ok(())
        }
        _ => Err(format!("ação desconhecida: {action}")),
    }
}

pub fn seek(app: &AppHandle, seconds: f64) -> Result<(), String> {
    let music = app.state::<SharedMusic>();
    {
        let mut guard = music.engine.lock().unwrap();
        let engine = guard.as_mut().ok_or("nada tocando")?;
        send(engine, json!(["seek", seconds.max(0.0), "absolute"]))?;
    }
    music.state.lock().unwrap().position = seconds.max(0.0);
    emit(app, true);
    Ok(())
}

pub fn state(app: &AppHandle) -> MusicState {
    app.state::<SharedMusic>().state.lock().unwrap().clone()
}

/// Toca uma lista (um álbum, um artista, tudo) a partir de `index`.
pub fn play_list(app: &AppHandle, tracks: Vec<Track>, index: usize) -> Result<(), String> {
    if tracks.is_empty() {
        return Err("nenhuma música".into());
    }
    let index = index.min(tracks.len() - 1);
    let music = app.state::<SharedMusic>();
    {
        let mut s = music.state.lock().unwrap();
        s.queue = tracks;
    }
    if let Some(engine) = music.engine.lock().unwrap().as_mut() {
        engine.order.clear();
    }
    load(app, index)
}

/// Pela Emma ("toca Coldplay", "toca o álbum Parachutes", "toca música"): artista, álbum ou
/// música que bate com o pedido; vazio toca tudo, embaralhado.
pub fn play_query(app: &AppHandle, query: &str) -> Result<String, String> {
    let tracks = library();
    if tracks.is_empty() {
        return Err("nenhuma música na pasta Músicas".into());
    }
    let q = crate::actions::normalize(query);
    let matches = |field: &str| !q.is_empty() && crate::actions::normalize(field).contains(&q);
    let pick: Vec<Track> = if q.is_empty() {
        tracks.clone()
    } else {
        let by_artist: Vec<Track> = tracks.iter().filter(|t| matches(&t.artist) || matches(&t.album_artist)).cloned().collect();
        let by_album: Vec<Track> = tracks.iter().filter(|t| matches(&t.album)).cloned().collect();
        let by_title: Vec<Track> = tracks.iter().filter(|t| matches(&t.title)).cloned().collect();
        if !by_title.is_empty() && by_title.len() < by_album.len().max(by_artist.len()).max(1) {
            by_title
        } else if !by_album.is_empty() && (by_artist.is_empty() || by_album.len() <= by_artist.len()) {
            by_album
        } else if !by_artist.is_empty() {
            by_artist
        } else {
            by_title
        }
    };
    if pick.is_empty() {
        return Err(format!("nenhuma música, álbum ou artista parecido com \"{query}\""));
    }
    let what = if q.is_empty() { "suas músicas".to_string() } else { format!("{} ({} músicas)", pick[0].title, pick.len()) };
    let shuffle = q.is_empty();
    app.state::<SharedMusic>().state.lock().unwrap().shuffle = shuffle;
    let start = if shuffle { shuffled(pick.len(), 0)[0] } else { 0 };
    play_list(app, pick, start)?;
    Ok(format!("Tocando {what}"))
}

// ---- comandos da tela ----

#[tauri::command]
pub async fn music_library(app: AppHandle) -> Vec<Track> {
    for root in roots() {
        let _ = app.asset_protocol_scope().allow_directory(&root, true);
    }
    let _ = app.asset_protocol_scope().allow_directory(cache_dir(), true);
    tauri::async_runtime::spawn_blocking(library).await.unwrap_or_default()
}

#[tauri::command]
pub async fn music_play(app: AppHandle, tracks: Vec<Track>, index: usize) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || play_list(&app, tracks, index)).await.map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn music_control(app: AppHandle, action: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || control(&app, &action)).await.map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn music_seek(app: AppHandle, position: f64) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || seek(&app, position)).await.map_err(|e| e.to_string())?
}

/// Aleatório e repetição.
#[tauri::command]
pub fn music_mode(app: AppHandle, shuffle: Option<bool>, repeat: Option<Repeat>) {
    let music = app.state::<SharedMusic>();
    {
        let mut s = music.state.lock().unwrap();
        if let Some(on) = shuffle {
            s.shuffle = on;
        }
        if let Some(r) = repeat {
            s.repeat = r;
        }
    }
    if let Some(engine) = music.engine.lock().unwrap().as_mut() {
        engine.order.clear();
    }
    emit(&app, true);
}

/// Toca uma faixa da fila (tocar na lista "a seguir").
#[tauri::command]
pub async fn music_jump(app: AppHandle, index: usize) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || load(&app, index)).await.map_err(|e| e.to_string())?
}

/// Volume do player (0 a 100).
#[tauri::command]
pub fn music_volume(app: AppHandle, volume: u8) -> Result<(), String> {
    let volume = volume.min(100);
    let music = app.state::<SharedMusic>();
    music.state.lock().unwrap().volume = Some(volume);
    if let Some(engine) = music.engine.lock().unwrap().as_mut() {
        send(engine, json!(["set_property", "volume", volume]))?;
    }
    emit(&app, true);
    Ok(())
}

// ---- a fila ----

/// Põe na fila: logo depois da atual (`next`) ou no fim. Sem nada tocando, começa a tocar.
#[tauri::command]
pub async fn music_queue_add(app: AppHandle, tracks: Vec<Track>, next: bool) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let music = app.state::<SharedMusic>();
        let idle = music.state.lock().unwrap().index.is_none();
        if idle {
            return play_list(&app, tracks, 0);
        }
        {
            let mut s = music.state.lock().unwrap();
            let at = if next { s.index.map_or(s.queue.len(), |i| i + 1) } else { s.queue.len() };
            let at = at.min(s.queue.len());
            s.queue.splice(at..at, tracks);
        }
        if let Some(engine) = music.engine.lock().unwrap().as_mut() {
            engine.order.clear();
        }
        emit(&app, true);
        Ok(())
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Muda uma música de lugar na fila (posições da fila inteira; a atual continua a mesma).
#[tauri::command]
pub fn music_queue_move(app: AppHandle, from: usize, to: usize) -> Result<(), String> {
    let music = app.state::<SharedMusic>();
    {
        let mut s = music.state.lock().unwrap();
        let len = s.queue.len();
        if from >= len || to >= len {
            return Err("fora da fila".into());
        }
        let track = s.queue.remove(from);
        s.queue.insert(to, track);
        if let Some(i) = s.index {
            s.index = Some(if i == from {
                to
            } else if from < i && to >= i {
                i - 1
            } else if from > i && to <= i {
                i + 1
            } else {
                i
            });
        }
    }
    if let Some(engine) = music.engine.lock().unwrap().as_mut() {
        engine.order.clear();
    }
    emit(&app, true);
    Ok(())
}

/// Tira uma música da fila (a que está tocando não sai).
#[tauri::command]
pub fn music_queue_remove(app: AppHandle, index: usize) -> Result<(), String> {
    let music = app.state::<SharedMusic>();
    {
        let mut s = music.state.lock().unwrap();
        if Some(index) == s.index {
            return Err("essa está tocando".into());
        }
        if index >= s.queue.len() {
            return Err("fora da fila".into());
        }
        s.queue.remove(index);
        if let Some(i) = s.index {
            if index < i {
                s.index = Some(i - 1);
            }
        }
    }
    if let Some(engine) = music.engine.lock().unwrap().as_mut() {
        engine.order.clear();
    }
    emit(&app, true);
    Ok(())
}

// ---- favoritas e playlists (~/.config/lumna/music.json) ----

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct Playlist {
    pub id: String,
    pub name: String,
    /// Ícone do LUMNA (o nome no Icon.vue).
    pub icon: String,
    /// Caminhos das músicas, na ordem.
    pub tracks: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
pub struct Lists {
    pub favorites: Vec<String>,
    pub playlists: Vec<Playlist>,
}

fn lists_path() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".config")).join("lumna").join("music.json")
}

fn read_lists() -> Lists {
    std::fs::read(lists_path()).ok().and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default()
}

fn write_lists(lists: &Lists) -> Result<(), String> {
    let path = lists_path();
    std::fs::create_dir_all(path.parent().unwrap_or(Path::new("/"))).map_err(|e| e.to_string())?;
    std::fs::write(&path, serde_json::to_vec_pretty(lists).unwrap_or_default()).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn music_lists() -> Lists {
    read_lists()
}

#[tauri::command]
pub fn music_favorite(path: String, on: bool) -> Result<Lists, String> {
    let mut lists = read_lists();
    lists.favorites.retain(|p| p != &path);
    if on {
        lists.favorites.insert(0, path);
    }
    write_lists(&lists)?;
    Ok(lists)
}

/// Cria (sem `id`) ou renomeia uma playlist.
#[tauri::command]
pub fn music_playlist_save(id: Option<String>, name: String, icon: Option<String>) -> Result<Lists, String> {
    let name: String = name.split_whitespace().collect::<Vec<_>>().join(" ").chars().take(60).collect();
    if name.is_empty() {
        return Err("dê um nome pra playlist".into());
    }
    let mut lists = read_lists();
    match id.and_then(|id| lists.playlists.iter_mut().find(|p| p.id == id)) {
        Some(p) => {
            p.name = name;
            if let Some(icon) = icon {
                p.icon = icon;
            }
        }
        None => {
            let id = format!("{:x}", md5::compute(format!("{name}{:?}", std::time::SystemTime::now())));
            lists.playlists.push(Playlist { id: id[..12].to_string(), name, icon: icon.unwrap_or_else(|| "music".into()), tracks: Vec::new() });
        }
    }
    write_lists(&lists)?;
    Ok(lists)
}

#[tauri::command]
pub fn music_playlist_delete(id: String) -> Result<Lists, String> {
    let mut lists = read_lists();
    lists.playlists.retain(|p| p.id != id);
    write_lists(&lists)?;
    Ok(lists)
}

/// Põe músicas numa playlist (as que já estão lá não repetem) ou tira (`remove`).
#[tauri::command]
pub fn music_playlist_tracks(id: String, paths: Vec<String>, remove: bool) -> Result<Lists, String> {
    let mut lists = read_lists();
    let p = lists.playlists.iter_mut().find(|p| p.id == id).ok_or("a playlist não existe mais")?;
    if remove {
        p.tracks.retain(|t| !paths.contains(t));
    } else {
        for path in paths {
            if !p.tracks.contains(&path) {
                p.tracks.push(path);
            }
        }
    }
    write_lists(&lists)?;
    Ok(lists)
}

// ---- letras e detalhes ----

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct LyricLine {
    /// Segundos (letra sincronizada, do .lrc).
    pub time: f64,
    pub text: String,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct Lyrics {
    /// Sincronizada (o .lrc do lado da música, ou a da tag no formato LRC).
    pub synced: Vec<LyricLine>,
    /// Sem tempo (a da tag).
    pub plain: String,
}

/// `[01:23.45]linha` (mais de um tempo na mesma linha vale pra todos).
pub fn parse_lrc(text: &str) -> Vec<LyricLine> {
    let mut lines = Vec::new();
    for raw in text.lines() {
        let mut rest = raw.trim();
        let mut times = Vec::new();
        while let Some(stripped) = rest.strip_prefix('[') {
            let Some(end) = stripped.find(']') else { break };
            let stamp = &stripped[..end];
            let Some((m, s)) = stamp.split_once(':') else { break };
            match (m.trim().parse::<f64>(), s.trim().parse::<f64>()) {
                (Ok(m), Ok(s)) => times.push(m * 60.0 + s),
                _ => break,
            }
            rest = stripped[end + 1..].trim_start();
        }
        for t in times {
            lines.push(LyricLine { time: t, text: rest.to_string() });
        }
    }
    lines.sort_by(|a, b| a.time.total_cmp(&b.time));
    lines
}

#[tauri::command]
pub async fn music_lyrics(path: String) -> Lyrics {
    tauri::async_runtime::spawn_blocking(move || {
        use lofty::prelude::*;
        let file = Path::new(&path);
        if let Some(text) = ["lrc", "LRC"].iter().find_map(|e| std::fs::read_to_string(file.with_extension(e)).ok()) {
            let synced = parse_lrc(&text);
            if !synced.is_empty() {
                return Lyrics { synced, plain: String::new() };
            }
        }
        let tag_text = lofty::probe::Probe::open(file)
            .and_then(|p| p.read())
            .ok()
            .and_then(|f| f.primary_tag().or_else(|| f.first_tag()).and_then(|t| t.get_string(&lofty::tag::ItemKey::Lyrics).map(str::to_string)))
            .unwrap_or_default();
        let synced = parse_lrc(&tag_text);
        if synced.is_empty() { Lyrics { synced, plain: tag_text.trim().to_string() } } else { Lyrics { synced, plain: String::new() } }
    })
    .await
    .unwrap_or_default()
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct Details {
    pub format: String,
    /// kbps
    pub bitrate: u32,
    /// Hz
    pub sample_rate: u32,
    pub channels: u8,
    pub bit_depth: u8,
    pub size: u64,
    pub genre: String,
    pub folder: String,
}

#[tauri::command]
pub async fn music_details(path: String) -> Details {
    tauri::async_runtime::spawn_blocking(move || {
        use lofty::prelude::*;
        let file = Path::new(&path);
        let mut d = Details {
            format: file.extension().map(|e| e.to_string_lossy().to_uppercase()).unwrap_or_default(),
            size: std::fs::metadata(file).map(|m| m.len()).unwrap_or(0),
            folder: file.parent().map(|p| p.to_string_lossy().into_owned()).unwrap_or_default(),
            ..Details::default()
        };
        if let Ok(f) = lofty::probe::Probe::open(file).and_then(|p| p.read()) {
            let props = f.properties();
            d.bitrate = props.audio_bitrate().unwrap_or(0);
            d.sample_rate = props.sample_rate().unwrap_or(0);
            d.channels = props.channels().unwrap_or(0);
            d.bit_depth = props.bit_depth().unwrap_or(0);
            if let Some(tag) = f.primary_tag().or_else(|| f.first_tag()) {
                d.genre = tag.genre().map(|g| g.to_string()).unwrap_or_default();
            }
        }
        d
    })
    .await
    .unwrap_or_default()
}

/// A pasta Músicas (pro "Adicionar músicas": abre ela no Arquivos).
#[tauri::command]
pub fn music_folder() -> String {
    roots().into_iter().next().unwrap_or_else(|| home().join("Músicas")).to_string_lossy().into_owned()
}

#[tauri::command]
pub fn music_state(app: AppHandle) -> PublicState {
    public_state(&app.state::<SharedMusic>().state.lock().unwrap())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Com arquivos de verdade: `HOME=<pasta com Música/> cargo test biblioteca -- --ignored --nocapture`
    #[test]
    #[ignore]
    fn biblioteca_de_verdade() {
        for t in library() {
            println!("{} | {} | {} | faixa {} | {:.1}s | capa {:?}", t.title, t.artist, t.album, t.track, t.duration, t.cover);
        }
    }

    #[test]
    fn letra_sincronizada() {
        let l = parse_lrc("[ar:Coldplay]\n[00:12.50]Look at the stars\n[01:02.00][00:30.00]Yellow\nsem tempo");
        assert_eq!(l, vec![
            LyricLine { time: 12.5, text: "Look at the stars".into() },
            LyricLine { time: 30.0, text: "Yellow".into() },
            LyricLine { time: 62.0, text: "Yellow".into() },
        ]);
    }

    #[test]
    fn titulo_pelo_nome_do_arquivo() {
        assert_eq!(title_from_file(Path::new("/m/03 - Yellow.mp3")), "Yellow");
        assert_eq!(title_from_file(Path::new("/m/Clocks.flac")), "Clocks");
        assert_eq!(title_from_file(Path::new("/m/1979.mp3")), "1979");
    }

    #[test]
    fn embaralha_com_a_atual_na_frente() {
        let order = shuffled(10, 4);
        assert_eq!(order[0], 4);
        let mut sorted = order.clone();
        sorted.sort();
        assert_eq!(sorted, (0..10).collect::<Vec<_>>());
    }

    #[test]
    fn le_a_pasta_e_o_indice() {
        let dir = std::env::temp_dir().join(format!("lumna-music-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("Coldplay/Parachutes")).unwrap();
        std::fs::write(dir.join("Coldplay/Parachutes/05 - Yellow.mp3"), b"nao e mp3 de verdade").unwrap();
        std::fs::write(dir.join("Coldplay/Parachutes/cover.jpg"), b"x").unwrap();
        std::fs::write(dir.join("notas.txt"), b"x").unwrap();
        let mut found = Vec::new();
        walk(&dir, DEPTH, &mut found);
        assert_eq!(found.len(), 1);
        let t = read_track(&found[0], &std::fs::metadata(&found[0]).unwrap());
        assert_eq!(t.title, "Yellow");
        assert!(t.cover.as_deref().is_some_and(|c| c.ends_with("cover.jpg")));
        std::fs::remove_dir_all(&dir).ok();
    }
}
