//! O que o PC e o celular dividem no Connect (os dois rodam este mesmo shell): os recursos
//! ligados pra cada aparelho, a pasta de recebidos, a área de transferência e os arquivos.
//!
//! Arquivos vão em pedaços de 192 KiB (base64 no JSON), cada um com resposta: quem manda só
//! lê o próximo pedaço quando o outro lado gravou o anterior, então um arquivo grande não enche
//! a memória. Do celular pro PC: `file-begin` / `file-chunk` / `file-end`. Do PC pro celular o PC
//! oferece (`file-offer`) e o celular puxa (`file-pull`), porque é o celular quem faz os pedidos.

use std::collections::HashMap;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use base64::Engine;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter};

pub const CHUNK: usize = 192 * 1024;
/// Texto copiado maior que isso não vai pro outro aparelho.
const CLIPBOARD_MAX: usize = 256 * 1024;

fn yes() -> bool {
    true
}

/// O que um aparelho pareado pode fazer. Cada lado guarda os seus (o PC pra cada celular, o
/// celular pra cada PC), e cada lado só manda e só aceita o que está ligado do seu lado.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq)]
pub struct Features {
    /// Avisos do PC no celular.
    #[serde(default = "yes")]
    pub notifications: bool,
    /// Avisos do celular (e ligações) no PC.
    #[serde(default = "yes")]
    pub phone_notifications: bool,
    #[serde(default = "yes")]
    pub clipboard: bool,
    /// Receber arquivos.
    #[serde(default = "yes")]
    pub files: bool,
    /// Comandos de longe: energia, Emma, senhas, agendamentos.
    #[serde(default = "yes")]
    pub control: bool,
}

impl Default for Features {
    fn default() -> Self {
        Features { notifications: true, phone_notifications: true, clipboard: true, files: true, control: true }
    }
}

// ---- ajustes gerais ----

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Settings {
    #[serde(default = "yes")]
    pub enabled: bool,
    /// Vazio = a pasta de Downloads.
    #[serde(default)]
    pub receive_dir: String,
    /// Celular: avisar quando um PC entra no modo Mídia (tocar abre o controle).
    #[serde(default = "yes")]
    pub media_notify: bool,
}

impl Default for Settings {
    fn default() -> Self {
        Settings { enabled: true, receive_dir: String::new(), media_notify: true }
    }
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

fn settings_path() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".config")).join("lumna/connect-settings.json")
}

pub fn settings() -> Settings {
    std::fs::read_to_string(settings_path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

pub fn enabled() -> bool {
    settings().enabled
}

/// A pasta de Downloads do usuário (a do `user-dirs.dirs`, senão ~/Downloads).
fn downloads() -> PathBuf {
    let home = home();
    let dirs = std::fs::read_to_string(home.join(".config/user-dirs.dirs")).unwrap_or_default();
    dirs.lines()
        .find_map(|l| l.strip_prefix("XDG_DOWNLOAD_DIR="))
        .map(|v| PathBuf::from(v.trim_matches('"').replace("$HOME", &home.to_string_lossy())))
        .unwrap_or_else(|| home.join("Downloads"))
}

pub fn receive_dir() -> PathBuf {
    let s = settings();
    if s.receive_dir.trim().is_empty() {
        downloads()
    } else {
        PathBuf::from(s.receive_dir.replace('~', &home().to_string_lossy()))
    }
}

#[derive(Serialize)]
pub struct SettingsView {
    pub enabled: bool,
    pub media_notify: bool,
    pub receive_dir: String,
    pub default_dir: String,
}

#[tauri::command]
pub fn connect_settings() -> SettingsView {
    let s = settings();
    SettingsView { enabled: s.enabled, media_notify: s.media_notify, receive_dir: receive_dir().to_string_lossy().into(), default_dir: downloads().to_string_lossy().into() }
}

/// Liga/desliga o Connect neste aparelho e troca a pasta de recebidos.
#[tauri::command]
pub fn connect_settings_set(
    app: AppHandle,
    enabled: Option<bool>,
    receive_dir: Option<String>,
    media_notify: Option<bool>,
) -> Result<SettingsView, String> {
    let mut s = settings();
    if let Some(m) = media_notify {
        s.media_notify = m;
    }
    if let Some(dir) = receive_dir {
        let dir = dir.trim().to_string();
        if !dir.is_empty() {
            std::fs::create_dir_all(PathBuf::from(dir.replace('~', &home().to_string_lossy()))).map_err(|e| format!("{dir}: {e}"))?;
        }
        s.receive_dir = dir;
    }
    let toggled = enabled.is_some_and(|e| e != s.enabled);
    if let Some(e) = enabled {
        s.enabled = e;
    }
    let path = settings_path();
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(&path, serde_json::to_string_pretty(&s).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    if toggled {
        crate::remote::enabled_changed(s.enabled);
        crate::connect::enabled_changed(&app);
    }
    Ok(connect_settings())
}

// ---- progresso das transferências ----

#[derive(Debug, Clone, Serialize)]
pub struct Transfer {
    pub id: String,
    pub name: String,
    /// Com quem (o nome do outro aparelho).
    pub peer: String,
    /// "in" (recebendo) ou "out" (mandando).
    pub direction: &'static str,
    pub done: u64,
    pub size: u64,
    /// "running", "done" ou "error".
    pub state: &'static str,
    pub error: Option<String>,
    /// Onde ficou (recebidos).
    pub path: Option<String>,
}

static TRANSFERS: Mutex<Vec<Transfer>> = Mutex::new(Vec::new());
static LAST_EMIT: Mutex<Option<Instant>> = Mutex::new(None);

/// Atualiza uma transferência e avisa a interface (no máximo ~6 vezes por segundo no meio dela).
pub fn progress(app: &AppHandle, t: Transfer) {
    let finished = t.state != "running";
    {
        let mut list = TRANSFERS.lock().unwrap();
        list.retain(|x| x.id != t.id);
        list.insert(0, t);
        list.truncate(30);
    }
    let mut last = LAST_EMIT.lock().unwrap();
    if finished || last.is_none_or(|l| l.elapsed() > Duration::from_millis(150)) {
        *last = Some(Instant::now());
        let _ = app.emit("lumna://connect-transfers", TRANSFERS.lock().unwrap().clone());
    }
}

#[tauri::command]
pub fn connect_transfers() -> Vec<Transfer> {
    TRANSFERS.lock().unwrap().clone()
}

pub fn new_id() -> String {
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_nanos());
    format!("{nanos:x}")
}

// ---- recebendo ----

struct Incoming {
    file: std::fs::File,
    part: PathBuf,
    target: PathBuf,
    transfer: Transfer,
}

static INCOMING: Mutex<Option<HashMap<String, Incoming>>> = Mutex::new(None);

/// Só o nome do arquivo (nada de `../` nem caminho), sem começar com ponto.
fn safe_name(name: &str) -> String {
    let base = name.rsplit(['/', '\\']).next().unwrap_or_default();
    let clean: String = base.chars().filter(|c| !c.is_control()).collect();
    let clean = clean.trim().trim_start_matches('.').to_string();
    if clean.is_empty() {
        "arquivo".into()
    } else {
        clean.chars().take(200).collect()
    }
}

/// "foto.jpg" que já existe vira "foto (2).jpg".
fn free_path(dir: &Path, name: &str) -> PathBuf {
    let taken = |p: &PathBuf| p.exists() || PathBuf::from(format!("{}.part", p.display())).exists();
    let first = dir.join(name);
    if !taken(&first) {
        return first;
    }
    let (stem, ext) = match name.rfind('.') {
        Some(i) if i > 0 => (&name[..i], &name[i..]),
        _ => (name, ""),
    };
    (2..)
        .map(|n| dir.join(format!("{stem} ({n}){ext}")))
        .find(|p| !taken(p))
        .unwrap()
}

pub fn recv_begin(app: &AppHandle, peer: &str, id: &str, name: &str, size: u64) -> Result<(), String> {
    let dir = receive_dir();
    std::fs::create_dir_all(&dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    let name = safe_name(name);
    let target = free_path(&dir, &name);
    let part = PathBuf::from(format!("{}.part", target.display()));
    let file = std::fs::File::create(&part).map_err(|e| format!("{}: {e}", part.display()))?;
    let transfer = Transfer {
        id: id.to_string(),
        name,
        peer: peer.to_string(),
        direction: "in",
        done: 0,
        size,
        state: "running",
        error: None,
        path: None,
    };
    progress(app, transfer.clone());
    INCOMING.lock().unwrap().get_or_insert_with(HashMap::new).insert(id.to_string(), Incoming { file, part, target, transfer });
    Ok(())
}

pub fn recv_chunk(app: &AppHandle, id: &str, data: &str) -> Result<(), String> {
    let bytes = base64::engine::general_purpose::STANDARD.decode(data).map_err(|e| e.to_string())?;
    let mut map = INCOMING.lock().unwrap();
    let inc = map.as_mut().and_then(|m| m.get_mut(id)).ok_or("transferência desconhecida")?;
    if inc.transfer.done + bytes.len() as u64 > inc.transfer.size {
        return Err("o arquivo veio maior que o anunciado".into());
    }
    inc.file.write_all(&bytes).map_err(|e| e.to_string())?;
    inc.transfer.done += bytes.len() as u64;
    progress(app, inc.transfer.clone());
    Ok(())
}

pub fn recv_end(app: &AppHandle, id: &str) -> Result<PathBuf, String> {
    let inc = INCOMING.lock().unwrap().as_mut().and_then(|m| m.remove(id)).ok_or("transferência desconhecida")?;
    let mut t = inc.transfer;
    if t.done != t.size {
        let _ = std::fs::remove_file(&inc.part);
        t.state = "error";
        t.error = Some("o arquivo chegou incompleto".into());
        progress(app, t);
        return Err("o arquivo chegou incompleto".into());
    }
    drop(inc.file);
    std::fs::rename(&inc.part, &inc.target).map_err(|e| e.to_string())?;
    t.state = "done";
    t.path = Some(inc.target.to_string_lossy().into());
    progress(app, t.clone());
    let folder = inc.target.parent().map(|p| p.to_string_lossy().replace(&*home().to_string_lossy(), "~")).unwrap_or_default();
    crate::notifications::local(app, &t.peer, &format!("Recebido: {}", t.name), &format!("Salvo em {folder}"), false);
    Ok(inc.target)
}

/// Cancela (a conexão caiu ou o outro lado desistiu): apaga o pedaço gravado.
pub fn recv_cancel(app: &AppHandle, id: &str, reason: &str) {
    if let Some(inc) = INCOMING.lock().unwrap().as_mut().and_then(|m| m.remove(id)) {
        let _ = std::fs::remove_file(&inc.part);
        let mut t = inc.transfer;
        t.state = "error";
        t.error = Some(reason.to_string());
        progress(app, t);
    }
}

/// Cancela tudo que estava vindo de um aparelho (a conexão com ele caiu).
pub fn recv_cancel_peer(app: &AppHandle, peer: &str) {
    let ids: Vec<String> = INCOMING
        .lock()
        .unwrap()
        .as_ref()
        .map(|m| m.iter().filter(|(_, i)| i.transfer.peer == peer).map(|(id, _)| id.clone()).collect())
        .unwrap_or_default();
    for id in ids {
        recv_cancel(app, &id, "a conexão caiu");
    }
}

// ---- mandando ----

/// Um pedaço do arquivo, em base64, e se ele foi o último.
pub fn read_chunk(path: &Path, offset: u64) -> Result<(String, usize, bool), String> {
    let mut file = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let size = file.metadata().map_err(|e| e.to_string())?.len();
    file.seek(SeekFrom::Start(offset)).map_err(|e| e.to_string())?;
    let mut buf = vec![0u8; CHUNK];
    let mut n = 0;
    while n < CHUNK {
        match file.read(&mut buf[n..]).map_err(|e| e.to_string())? {
            0 => break,
            k => n += k,
        }
    }
    buf.truncate(n);
    Ok((base64::engine::general_purpose::STANDARD.encode(&buf), n, offset + n as u64 >= size))
}

/// Confere os caminhos antes de mandar: só arquivos comuns (pasta não vai).
pub fn files_to_send(paths: &[String]) -> Result<Vec<(PathBuf, String, u64)>, String> {
    let mut out = Vec::new();
    for p in paths {
        let path = PathBuf::from(p);
        let meta = std::fs::metadata(&path).map_err(|e| format!("{p}: {e}"))?;
        if !meta.is_file() {
            return Err(format!("{}: pastas ainda não vão pelo Connect", path.file_name().map_or(p.clone(), |n| n.to_string_lossy().into())));
        }
        let name = path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_else(|| "arquivo".into());
        out.push((path, name, meta.len()));
    }
    if out.is_empty() {
        return Err("nenhum arquivo escolhido".into());
    }
    Ok(out)
}

/// Arquivos recentes (Downloads, câmera, imagens, documentos), pro celular escolher o que mandar.
#[derive(Serialize)]
pub struct RecentFile {
    pub path: String,
    pub name: String,
    pub size: u64,
    pub modified: u64,
}

#[tauri::command]
pub async fn connect_recent_files() -> Vec<RecentFile> {
    tauri::async_runtime::spawn_blocking(|| {
        let home = home();
        let dirs = [
            downloads(),
            home.join("DCIM/Camera"),
            home.join("DCIM"),
            home.join("Imagens"),
            home.join("Pictures"),
            home.join("Imagens/Capturas de tela"),
            home.join("Documentos"),
            home.join("Documents"),
            home.join("Músicas"),
            home.join("Vídeos"),
            receive_dir(),
        ];
        let mut seen = std::collections::HashSet::new();
        let mut files: Vec<RecentFile> = Vec::new();
        for dir in dirs {
            let Ok(entries) = std::fs::read_dir(&dir) else { continue };
            for e in entries.flatten() {
                let path = e.path();
                let Ok(meta) = e.metadata() else { continue };
                let name = e.file_name().to_string_lossy().to_string();
                if !meta.is_file() || name.starts_with('.') || name.ends_with(".part") || !seen.insert(path.clone()) {
                    continue;
                }
                let modified = meta.modified().ok().and_then(|m| m.duration_since(std::time::UNIX_EPOCH).ok()).map_or(0, |d| d.as_secs());
                files.push(RecentFile { path: path.to_string_lossy().into(), name, size: meta.len(), modified });
            }
        }
        files.sort_by_key(|f| std::cmp::Reverse(f.modified));
        files.truncate(80);
        files
    })
    .await
    .unwrap_or_default()
}

// ---- área de transferência ----

/// O último texto que veio do outro aparelho (pra não devolver ele de volta).
static LAST_CLIP: Mutex<Option<String>> = Mutex::new(None);

pub fn clipboard_set(text: &str) -> Result<(), String> {
    if text.is_empty() || text.len() > CLIPBOARD_MAX {
        return Err("texto vazio ou grande demais".into());
    }
    *LAST_CLIP.lock().unwrap() = Some(text.to_string());
    let mut child = Command::new("wl-copy").stdin(Stdio::piped()).spawn().map_err(|e| format!("wl-copy: {e}"))?;
    child.stdin.take().ok_or("wl-copy sem entrada")?.write_all(text.as_bytes()).map_err(|e| e.to_string())?;
    child.wait().map_err(|e| e.to_string())?;
    Ok(())
}

pub fn clipboard_get() -> Option<String> {
    let out = Command::new("wl-paste").args(["-n", "-t", "text"]).stdin(Stdio::null()).output().ok()?;
    let text = String::from_utf8(out.stdout).ok()?;
    (!text.is_empty() && text.len() <= CLIPBOARD_MAX).then_some(text)
}

/// Fica de olho no que for copiado aqui e chama `on_copy` com o texto (o que veio do outro
/// aparelho não volta). O `wl-paste --watch` roda o comando a cada cópia; o texto vem numa
/// linha em base64.
pub fn clipboard_watch(on_copy: impl Fn(String) + Send + 'static) {
    std::thread::spawn(move || loop {
        let child = Command::new("wl-paste")
            .args(["-t", "text", "--watch", "sh", "-c", "base64 -w0; echo"])
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn();
        let Ok(mut child) = child else {
            std::thread::sleep(Duration::from_secs(30));
            continue;
        };
        let started = Instant::now();
        if let Some(out) = child.stdout.take() {
            use std::io::BufRead;
            let mut first = true;
            for line in std::io::BufReader::new(out).lines() {
                let Ok(line) = line else { break };
                // a primeira linha é o que já estava copiado ao abrir: não é cópia nova
                if std::mem::take(&mut first) && started.elapsed() < Duration::from_secs(2) {
                    continue;
                }
                let Ok(bytes) = base64::engine::general_purpose::STANDARD.decode(line.trim()) else { continue };
                let Ok(text) = String::from_utf8(bytes) else { continue };
                if text.trim().is_empty() || text.len() > CLIPBOARD_MAX {
                    continue;
                }
                if LAST_CLIP.lock().unwrap().as_deref() == Some(text.as_str()) {
                    continue;
                }
                if enabled() {
                    on_copy(text);
                }
            }
        }
        let _ = child.wait();
        std::thread::sleep(Duration::from_secs(5));
    });
}

/// Resposta padrão de um pedido de arquivo.
pub fn chunk_reply(data: String, len: usize, eof: bool) -> Value {
    json!({ "type": "file-data", "data": data, "len": len, "eof": eof })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn nomes_seguros() {
        assert_eq!(safe_name("../../etc/passwd"), "passwd");
        assert_eq!(safe_name("C:\\fotos\\a.jpg"), "a.jpg");
        assert_eq!(safe_name(".bashrc"), "bashrc");
        assert_eq!(safe_name(""), "arquivo");
        assert_eq!(safe_name("foto 1.jpg"), "foto 1.jpg");
    }

    #[test]
    fn nome_livre() {
        let dir = std::env::temp_dir().join(format!("lumna-link-{}", new_id()));
        std::fs::create_dir_all(&dir).unwrap();
        assert_eq!(free_path(&dir, "a.txt"), dir.join("a.txt"));
        std::fs::write(dir.join("a.txt"), "x").unwrap();
        assert_eq!(free_path(&dir, "a.txt"), dir.join("a (2).txt"));
        std::fs::write(dir.join("LEIAME"), "x").unwrap();
        assert_eq!(free_path(&dir, "LEIAME"), dir.join("LEIAME (2)"));
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn pedacos() {
        let dir = std::env::temp_dir().join(format!("lumna-chunk-{}", new_id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("f.bin");
        let data: Vec<u8> = (0..(CHUNK + 1000)).map(|i| (i % 251) as u8).collect();
        std::fs::write(&path, &data).unwrap();
        let (a, n, eof) = read_chunk(&path, 0).unwrap();
        assert_eq!((n, eof), (CHUNK, false));
        let (b, m, eof) = read_chunk(&path, CHUNK as u64).unwrap();
        assert_eq!((m, eof), (1000, true));
        let mut back = base64::engine::general_purpose::STANDARD.decode(a).unwrap();
        back.extend(base64::engine::general_purpose::STANDARD.decode(b).unwrap());
        assert_eq!(back, data);
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn recursos_antigos_ligados() {
        let f: Features = serde_json::from_str("{}").unwrap();
        assert_eq!(f, Features::default());
        let f: Features = serde_json::from_str(r#"{"clipboard":false}"#).unwrap();
        assert!(!f.clipboard && f.files);
    }
}
