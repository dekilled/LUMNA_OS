//! Explorador de arquivos do LUMNA ("Arquivos"): listar pastas, abrir, criar, renomear, copiar e
//! mover, lixeira (padrão freedesktop, a mesma dos outros programas), discos e pendrives (udisks)
//! e miniaturas (ffmpeg, guardadas em ~/.cache/lumna/thumbs).

use std::collections::HashMap;
use std::os::unix::fs::{MetadataExt, PermissionsExt};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant, UNIX_EPOCH};

use serde::Serialize;
use serde_json::Value;
use tauri::{AppHandle, Emitter, Manager};

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Entry {
    pub name: String,
    pub path: String,
    pub dir: bool,
    pub link: bool,
    /// Bytes (0 nas pastas).
    pub size: u64,
    /// Segundos desde 1970.
    pub modified: i64,
    /// `folder`, `image`, `video`, `audio`, `document`, `sheet`, `slides`, `pdf`, `archive`,
    /// `code`, `text`, `app`, `disk`, `file`.
    pub kind: &'static str,
    pub hidden: bool,
}

#[derive(Debug, Clone, Serialize)]
pub struct Listing {
    pub path: String,
    pub parent: Option<String>,
    pub writable: bool,
    pub entries: Vec<Entry>,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Place {
    pub name: String,
    pub path: String,
    pub icon: &'static str,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Drive {
    pub device: String,
    pub label: String,
    pub size: u64,
    pub mountpoint: Option<String>,
    /// Pendrive, cartão, HD externo.
    pub removable: bool,
    pub fstype: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct Places {
    pub home: String,
    pub places: Vec<Place>,
    pub trash: usize,
    pub drives: Vec<Drive>,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct TrashItem {
    /// Nome dentro da lixeira (identifica o item).
    pub id: String,
    pub name: String,
    pub original: String,
    /// `2026-10-12T18:30:00`.
    pub deleted: String,
    pub dir: bool,
    pub kind: &'static str,
}

#[derive(Debug, Clone, Serialize)]
pub struct Info {
    pub path: String,
    pub size: u64,
    /// Arquivos dentro (pastas).
    pub items: u64,
    pub modified: i64,
    pub accessed: i64,
    /// `rwxr-xr-x`.
    pub mode: String,
    pub owner: String,
}

// ---- utilidades ----

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

/// `~` e `~/x` viram a pasta pessoal.
pub fn expand(path: &str) -> PathBuf {
    if path == "~" || path.is_empty() {
        home()
    } else if let Some(rest) = path.strip_prefix("~/") {
        home().join(rest)
    } else {
        PathBuf::from(path)
    }
}

fn own_uid() -> u32 {
    std::fs::metadata("/proc/self").map(|m| m.uid()).unwrap_or(u32::MAX)
}

fn seconds(time: std::io::Result<std::time::SystemTime>) -> i64 {
    time.ok().and_then(|t| t.duration_since(UNIX_EPOCH).ok()).map_or(0, |d| d.as_secs() as i64)
}

/// Tipo do arquivo pela extensão (o suficiente para o ícone e as miniaturas).
pub fn kind_of(name: &str, dir: bool) -> &'static str {
    if dir {
        return "folder";
    }
    let ext = name.rsplit_once('.').map(|(_, e)| e.to_ascii_lowercase()).unwrap_or_default();
    match ext.as_str() {
        "png" | "jpg" | "jpeg" | "webp" | "gif" | "bmp" | "svg" | "avif" | "heic" | "tif" | "tiff" | "ico" => "image",
        "mp4" | "mkv" | "webm" | "avi" | "mov" | "m4v" | "wmv" | "flv" => "video",
        "mp3" | "flac" | "ogg" | "opus" | "wav" | "m4a" | "aac" | "wma" => "audio",
        "pdf" => "pdf",
        "doc" | "docx" | "odt" | "rtf" => "document",
        "xls" | "xlsx" | "ods" | "csv" => "sheet",
        "ppt" | "pptx" | "odp" => "slides",
        "zip" | "rar" | "7z" | "tar" | "gz" | "xz" | "zst" | "bz2" | "tgz" => "archive",
        "rs" | "js" | "ts" | "vue" | "py" | "c" | "h" | "cpp" | "java" | "go" | "sh" | "lua" | "json" | "toml" | "yaml"
        | "yml" | "html" | "css" | "kt" => "code",
        "txt" | "md" | "log" | "ini" | "conf" => "text",
        "desktop" | "appimage" | "exe" | "msi" => "app",
        "iso" | "img" => "disk",
        _ => "file",
    }
}

/// Nome de arquivo aceitável (sem barra, sem `.`/`..`, sem quebra de linha).
pub fn valid_name(name: &str) -> bool {
    let name = name.trim();
    !name.is_empty() && name != "." && name != ".." && !name.contains('/') && !name.contains('\0') && !name.contains('\n')
}

/// Primeiro nome livre na pasta: "foto.png" → "foto (2).png", "foto (3).png"...
/// Com `copy`, a primeira tentativa é "foto (cópia).png".
pub fn free_name(dir: &Path, name: &str, copy: bool) -> PathBuf {
    let first = dir.join(name);
    if !copy && !first.exists() {
        return first;
    }
    // Pastas e arquivos sem extensão (ou ocultos, ".bashrc") não têm o que separar.
    let (stem, ext) = match name.rsplit_once('.') {
        Some((stem, ext)) if !stem.is_empty() && !ext.contains(' ') => (stem.to_string(), format!(".{ext}")),
        _ => (name.to_string(), String::new()),
    };
    if copy {
        let candidate = dir.join(format!("{stem} (cópia){ext}"));
        if !candidate.exists() {
            return candidate;
        }
    }
    (2..)
        .map(|n| dir.join(format!("{stem} ({n}){ext}")))
        .find(|p| !p.exists())
        .expect("sempre acha um nome livre")
}

fn run(program: &str, args: &[&str]) -> Result<String, String> {
    let output = Command::new(program)
        .args(args)
        .env("LC_ALL", "C")
        .stdin(Stdio::null())
        .output()
        .map_err(|e| format!("{program}: {e}"))?;
    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).into_owned())
    } else {
        let err = String::from_utf8_lossy(&output.stderr).trim().to_string();
        Err(if err.is_empty() { format!("{program} falhou") } else { err })
    }
}

/// Erros do sistema em português, para a tela.
fn explain(error: std::io::Error) -> String {
    use std::io::ErrorKind::*;
    match error.kind() {
        PermissionDenied => "Sem permissão para isso.".into(),
        NotFound => "O arquivo não existe mais.".into(),
        AlreadyExists => "Já existe um item com esse nome.".into(),
        StorageFull => "O disco está cheio.".into(),
        ReadOnlyFilesystem => "Esse disco é só de leitura.".into(),
        DirectoryNotEmpty => "A pasta não está vazia.".into(),
        _ => error.to_string(),
    }
}

// ---- listar ----

fn entry(path: &Path) -> Option<Entry> {
    let name = path.file_name()?.to_string_lossy().into_owned();
    let link = std::fs::symlink_metadata(path).ok()?;
    // Atalho quebrado: aparece como arquivo comum.
    let meta = std::fs::metadata(path).unwrap_or(link.clone());
    let dir = meta.is_dir();
    Some(Entry {
        kind: kind_of(&name, dir),
        hidden: name.starts_with('.'),
        size: if dir { 0 } else { meta.len() },
        modified: seconds(meta.modified()),
        link: link.file_type().is_symlink(),
        path: path.to_string_lossy().into_owned(),
        name,
        dir,
    })
}

#[tauri::command]
pub async fn files_list(path: String, hidden: bool) -> Result<Listing, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let dir = expand(&path);
        let read = std::fs::read_dir(&dir).map_err(explain)?;
        let entries: Vec<Entry> = read
            .flatten()
            .filter_map(|e| entry(&e.path()))
            .filter(|e| hidden || !e.hidden)
            .collect();
        let meta = std::fs::metadata(&dir).map_err(explain)?;
        let mode = meta.permissions().mode();
        let writable = (meta.uid() == own_uid() && mode & 0o200 != 0) || mode & 0o002 != 0 || own_uid() == 0;
        Ok(Listing {
            path: dir.to_string_lossy().into_owned(),
            parent: dir.parent().map(|p| p.to_string_lossy().into_owned()),
            writable,
            entries,
        })
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Muda quando algo é criado, apagado ou renomeado na pasta: a tela relê só quando precisa.
#[tauri::command]
pub fn files_stamp(path: String) -> i64 {
    std::fs::metadata(expand(&path))
        .ok()
        .and_then(|m| m.modified().ok())
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map_or(0, |d| d.as_nanos() as i64)
}

// ---- lugares ----

/// Pastas do usuário (`~/.config/user-dirs.dirs`): `XDG_DOWNLOAD_DIR="$HOME/Downloads"`.
pub fn parse_user_dirs(text: &str, home: &Path) -> HashMap<String, PathBuf> {
    text.lines()
        .filter_map(|line| {
            let (key, value) = line.trim().split_once('=')?;
            let key = key.strip_prefix("XDG_")?.strip_suffix("_DIR")?;
            let value = value.trim().trim_matches('"');
            let path = match value.strip_prefix("$HOME") {
                Some(rest) => home.join(rest.trim_start_matches('/')),
                None => PathBuf::from(value),
            };
            Some((key.to_string(), path))
        })
        .collect()
}

/// `lsblk -J -b -o PATH,LABEL,SIZE,MOUNTPOINTS,RM,HOTPLUG,TYPE,FSTYPE`: partições com sistema de
/// arquivos que não são do próprio sistema (raiz, boot, swap, o pendrive do live).
pub fn parse_lsblk(json: &str) -> Vec<Drive> {
    let Ok(value) = serde_json::from_str::<Value>(json) else { return Vec::new() };
    let mut drives = Vec::new();
    fn walk(devices: &[Value], parent_removable: bool, out: &mut Vec<Drive>) {
        for d in devices {
            let removable = parent_removable || d["rm"].as_bool() == Some(true) || d["hotplug"].as_bool() == Some(true);
            let fstype = d["fstype"].as_str().unwrap_or_default();
            let kind = d["type"].as_str().unwrap_or_default();
            let mounts: Vec<&str> = d["mountpoints"].as_array().map(|m| m.iter().filter_map(Value::as_str).collect()).unwrap_or_default();
            let system = mounts.iter().any(|m| {
                *m == "/" || m.starts_with("/boot") || *m == "/efi" || *m == "/home" || m.starts_with("/run/archiso") || *m == "[SWAP]"
            });
            let skip_fs = matches!(fstype, "" | "swap" | "iso9660" | "squashfs" | "crypto_LUKS" | "LVM2_member" | "linux_raid_member");
            if (kind == "part" || kind == "disk" || kind == "crypt") && !system && !skip_fs {
                out.push(Drive {
                    device: d["path"].as_str().unwrap_or_default().to_string(),
                    label: d["label"].as_str().unwrap_or_default().to_string(),
                    size: d["size"].as_u64().unwrap_or(0),
                    mountpoint: mounts.first().map(|m| m.to_string()),
                    removable,
                    fstype: fstype.to_string(),
                });
            }
            if let Some(children) = d["children"].as_array() {
                walk(children, removable, out);
            }
        }
    }
    if let Some(devices) = value["blockdevices"].as_array() {
        walk(devices, false, &mut drives);
    }
    drives
}

fn trash_dir() -> PathBuf {
    std::env::var_os("XDG_DATA_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".local/share")).join("Trash")
}

#[tauri::command]
pub async fn files_places() -> Places {
    tauri::async_runtime::spawn_blocking(|| {
        let home = home();
        let config = std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config"));
        let dirs = std::fs::read_to_string(config.join("user-dirs.dirs")).map(|t| parse_user_dirs(&t, &home)).unwrap_or_default();
        // Sem o user-dirs.dirs, procura os nomes em português e em inglês.
        let pick = |key: &str, names: &[&str]| {
            dirs.get(key).cloned().filter(|p| p.is_dir() && *p != home).or_else(|| names.iter().map(|n| home.join(n)).find(|p| p.is_dir()))
        };
        let wanted: [(&str, &str, &[&str], &'static str); 5] = [
            ("DOCUMENTS", "Documentos", &["Documentos", "Documents"], "document"),
            ("DOWNLOAD", "Downloads", &["Downloads", "Transferências"], "download"),
            ("PICTURES", "Imagens", &["Imagens", "Pictures"], "image"),
            ("MUSIC", "Músicas", &["Músicas", "Music"], "music"),
            ("VIDEOS", "Vídeos", &["Vídeos", "Videos"], "video"),
        ];
        let mut places = vec![Place { name: "Início".into(), path: home.to_string_lossy().into_owned(), icon: "home" }];
        for (key, name, fallback, icon) in wanted {
            if let Some(path) = pick(key, fallback) {
                places.push(Place { name: name.into(), path: path.to_string_lossy().into_owned(), icon });
            }
        }
        let trash = std::fs::read_dir(trash_dir().join("files")).map(|d| d.count()).unwrap_or(0);
        let drives = run("lsblk", &["-J", "-b", "-o", "PATH,LABEL,SIZE,MOUNTPOINTS,RM,HOTPLUG,TYPE,FSTYPE"])
            .map(|o| parse_lsblk(&o))
            .unwrap_or_default();
        Places { home: home.to_string_lossy().into_owned(), places, trash, drives }
    })
    .await
    .unwrap_or(Places { home: home().to_string_lossy().into_owned(), places: Vec::new(), trash: 0, drives: Vec::new() })
}

// ---- operações ----

/// Abre com o programa padrão do tipo de arquivo.
#[tauri::command]
pub fn files_open(path: String) -> Result<(), String> {
    let path = expand(&path);
    let try_open = |program: &str, args: &[&str]| {
        Command::new(program).args(args).arg(&path).stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null()).spawn().is_ok()
    };
    if try_open("gio", &["open"]) || try_open("xdg-open", &[]) {
        Ok(())
    } else {
        Err("Não há programa para abrir esse arquivo.".into())
    }
}

/// Abre o terminal na pasta.
#[tauri::command]
pub fn files_terminal(path: String) -> Result<(), String> {
    Command::new("foot")
        .arg(format!("--working-directory={}", expand(&path).display()))
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("terminal: {e}"))
}

#[tauri::command]
pub fn files_mkdir(parent: String, name: String) -> Result<String, String> {
    if !valid_name(&name) {
        return Err("Nome inválido.".into());
    }
    let path = expand(&parent).join(name.trim());
    std::fs::create_dir(&path).map_err(explain)?;
    Ok(path.to_string_lossy().into_owned())
}

#[tauri::command]
pub fn files_rename(path: String, name: String) -> Result<String, String> {
    if !valid_name(&name) {
        return Err("Nome inválido: não pode ter / nem ficar vazio.".into());
    }
    let from = expand(&path);
    let to = from.parent().ok_or("Não dá para renomear isso.")?.join(name.trim());
    if to == from {
        return Ok(path);
    }
    if to.exists() {
        return Err("Já existe um item com esse nome.".into());
    }
    std::fs::rename(&from, &to).map_err(explain)?;
    Ok(to.to_string_lossy().into_owned())
}

/// Manda para a lixeira (dá para desfazer pela Lixeira).
#[tauri::command]
pub async fn files_trash(paths: Vec<String>) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let mut args = vec!["trash", "--"];
        args.extend(paths.iter().map(String::as_str));
        run("gio", &args).map(|_| ()).map_err(|e| format!("Não deu para mandar para a lixeira: {e}"))
    })
    .await
    .map_err(|e| e.to_string())?
}

#[derive(Clone, Serialize)]
struct Progress {
    done: u64,
    total: u64,
    current: String,
}

fn total_size(path: &Path) -> u64 {
    let Ok(meta) = std::fs::symlink_metadata(path) else { return 0 };
    if meta.is_dir() {
        std::fs::read_dir(path).map(|d| d.flatten().map(|e| total_size(&e.path())).sum()).unwrap_or(0)
    } else {
        meta.len()
    }
}

struct Copier<'a> {
    app: &'a AppHandle,
    done: u64,
    total: u64,
    last: Instant,
}

impl Copier<'_> {
    fn report(&mut self, current: &Path, force: bool) {
        if force || self.last.elapsed() > Duration::from_millis(150) {
            self.last = Instant::now();
            let current = current.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            let _ = self.app.emit("lumna://files-progress", Progress { done: self.done, total: self.total, current });
        }
    }

    fn copy(&mut self, from: &Path, to: &Path) -> std::io::Result<()> {
        let meta = std::fs::symlink_metadata(from)?;
        if meta.file_type().is_symlink() {
            std::os::unix::fs::symlink(std::fs::read_link(from)?, to)?;
        } else if meta.is_dir() {
            std::fs::create_dir(to)?;
            for entry in std::fs::read_dir(from)? {
                let entry = entry?;
                self.copy(&entry.path(), &to.join(entry.file_name()))?;
            }
            std::fs::set_permissions(to, meta.permissions())?;
        } else {
            // Copia em blocos para mostrar o andamento de arquivos grandes.
            use std::io::{Read, Write};
            let mut src = std::fs::File::open(from)?;
            let mut dst = std::fs::File::create(to)?;
            let mut buf = vec![0u8; 1 << 20];
            loop {
                let n = src.read(&mut buf)?;
                if n == 0 {
                    break;
                }
                dst.write_all(&buf[..n])?;
                self.done += n as u64;
                self.report(from, false);
            }
            std::fs::set_permissions(to, meta.permissions())?;
        }
        Ok(())
    }
}

/// Cola (copia ou move) os itens na pasta. Devolve os caminhos novos.
#[tauri::command]
pub async fn files_paste(app: AppHandle, sources: Vec<String>, dest: String, cut: bool) -> Result<Vec<String>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let dest = expand(&dest);
        let sources: Vec<PathBuf> = sources.iter().map(|s| expand(s)).collect();
        for src in &sources {
            if dest.starts_with(src) {
                return Err("Não dá para colocar uma pasta dentro dela mesma.".into());
            }
        }
        let mut copier = Copier { app: &app, done: 0, total: 0, last: Instant::now() };
        let mut to_copy = Vec::new();
        let mut created = Vec::new();
        for src in &sources {
            let name = src.file_name().ok_or("Item inválido.")?.to_string_lossy().into_owned();
            let same_dir = src.parent() == Some(dest.as_path());
            if cut && same_dir {
                created.push(src.clone());
                continue;
            }
            let target = free_name(&dest, &name, same_dir);
            // Mover dentro do mesmo disco é só renomear (instantâneo).
            if cut && std::fs::rename(src, &target).is_ok() {
                created.push(target);
                continue;
            }
            to_copy.push((src.clone(), target));
        }
        copier.total = to_copy.iter().map(|(src, _)| total_size(src)).sum();
        for (src, target) in to_copy {
            copier.copy(&src, &target).map_err(explain)?;
            if cut {
                let removed = if src.is_dir() && !src.is_symlink() { std::fs::remove_dir_all(&src) } else { std::fs::remove_file(&src) };
                removed.map_err(explain)?;
            }
            created.push(target);
        }
        copier.report(&dest, true);
        Ok(created.iter().map(|p| p.to_string_lossy().into_owned()).collect())
    })
    .await
    .map_err(|e| e.to_string())?
}

// ---- lixeira ----

/// Caminho de `Path=` do .trashinfo, que vem com %XX.
pub fn percent_decode(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            let hex = std::str::from_utf8(&bytes[i + 1..i + 3]).ok().and_then(|h| u8::from_str_radix(h, 16).ok());
            if let Some(b) = hex {
                out.push(b);
                i += 3;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

pub fn parse_trashinfo(text: &str) -> Option<(String, String)> {
    let path = text.lines().find_map(|l| l.strip_prefix("Path="))?;
    let date = text.lines().find_map(|l| l.strip_prefix("DeletionDate=")).unwrap_or_default();
    Some((percent_decode(path.trim()), date.trim().to_string()))
}

#[tauri::command]
pub fn files_trash_list() -> Vec<TrashItem> {
    let trash = trash_dir();
    let Ok(dir) = std::fs::read_dir(trash.join("files")) else { return Vec::new() };
    let mut items: Vec<TrashItem> = dir
        .flatten()
        .map(|e| {
            let id = e.file_name().to_string_lossy().into_owned();
            let info = std::fs::read_to_string(trash.join("info").join(format!("{id}.trashinfo"))).ok();
            let (original, deleted) = info.as_deref().and_then(parse_trashinfo).unwrap_or_default();
            let name = Path::new(&original).file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_else(|| id.clone());
            let dir = e.path().is_dir();
            TrashItem { kind: kind_of(&name, dir), id, name, original, deleted, dir }
        })
        .collect();
    items.sort_by(|a, b| b.deleted.cmp(&a.deleted));
    items
}

/// Volta os itens para onde estavam (com outro nome se o lugar já estiver ocupado).
#[tauri::command]
pub fn files_trash_restore(ids: Vec<String>) -> Result<(), String> {
    let trash = trash_dir();
    for id in ids {
        if !valid_name(&id) {
            continue;
        }
        let info_path = trash.join("info").join(format!("{id}.trashinfo"));
        let info = std::fs::read_to_string(&info_path).map_err(explain)?;
        let (original, _) = parse_trashinfo(&info).ok_or("Não sei de onde esse item veio.")?;
        let original = PathBuf::from(original);
        let parent = original.parent().ok_or("Lugar de origem inválido.")?;
        std::fs::create_dir_all(parent).map_err(explain)?;
        let name = original.file_name().ok_or("Nome inválido.")?.to_string_lossy().into_owned();
        let target = free_name(parent, &name, false);
        std::fs::rename(trash.join("files").join(&id), &target).map_err(explain)?;
        let _ = std::fs::remove_file(info_path);
    }
    Ok(())
}

/// Apaga de vez (só itens que já estão na lixeira; `None` esvazia tudo).
#[tauri::command]
pub async fn files_trash_delete(ids: Option<Vec<String>>) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let trash = trash_dir();
        let ids: Vec<String> = match ids {
            Some(ids) => ids,
            None => std::fs::read_dir(trash.join("files"))
                .map(|d| d.flatten().map(|e| e.file_name().to_string_lossy().into_owned()).collect())
                .unwrap_or_default(),
        };
        for id in ids.iter().filter(|id| valid_name(id)) {
            let path = trash.join("files").join(id);
            let removed = if path.is_dir() && !path.is_symlink() { std::fs::remove_dir_all(&path) } else { std::fs::remove_file(&path) };
            removed.map_err(explain)?;
            let _ = std::fs::remove_file(trash.join("info").join(format!("{id}.trashinfo")));
        }
        Ok(())
    })
    .await
    .map_err(|e| e.to_string())?
}

// ---- discos ----

/// Monta pelo udisks (sem senha para pendrives; discos internos pedem pela tela do LUMNA).
#[tauri::command]
pub async fn files_mount(device: String) -> Result<String, String> {
    if !device.starts_with("/dev/") || device.contains("..") {
        return Err("Dispositivo inválido.".into());
    }
    tauri::async_runtime::spawn_blocking(move || {
        let out = run("udisksctl", &["mount", "-b", &device]).map_err(|e| format!("Não deu para abrir o disco: {e}"))?;
        // "Mounted /dev/sdb1 at /run/media/lumna/PENDRIVE"
        out.split(" at ").nth(1).map(|p| p.trim().trim_end_matches('.').to_string()).ok_or_else(|| "O disco foi montado, mas não sei onde.".into())
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Desmonta para poder tirar com segurança.
#[tauri::command]
pub async fn files_unmount(device: String) -> Result<(), String> {
    if !device.starts_with("/dev/") || device.contains("..") {
        return Err("Dispositivo inválido.".into());
    }
    tauri::async_runtime::spawn_blocking(move || {
        run("udisksctl", &["unmount", "-b", &device]).map(|_| ()).map_err(|e| {
            if e.contains("target is busy") {
                "O disco está em uso. Feche os arquivos abertos dele e tente de novo.".into()
            } else {
                format!("Não deu para ejetar: {e}")
            }
        })
    })
    .await
    .map_err(|e| e.to_string())?
}

// ---- detalhes e miniaturas ----

fn mode_string(mode: u32) -> String {
    let bits = ['r', 'w', 'x'];
    (0..9).map(|i| if mode & (1 << (8 - i)) != 0 { bits[i % 3] } else { '-' }).collect()
}

fn owner_name(uid: u32) -> String {
    std::fs::read_to_string("/etc/passwd")
        .ok()
        .and_then(|p| {
            p.lines().find_map(|l| {
                let f: Vec<&str> = l.split(':').collect();
                (f.get(2)?.parse::<u32>().ok()? == uid).then(|| f[0].to_string())
            })
        })
        .unwrap_or_else(|| uid.to_string())
}

/// Tamanho de pasta: soma até um limite de itens, para não travar em pastas gigantes.
fn dir_size(path: &Path, budget: &mut u64) -> (u64, u64) {
    let mut size = 0;
    let mut items = 0;
    let Ok(dir) = std::fs::read_dir(path) else { return (0, 0) };
    for e in dir.flatten() {
        if *budget == 0 {
            break;
        }
        *budget -= 1;
        let Ok(meta) = e.path().symlink_metadata() else { continue };
        items += 1;
        if meta.is_dir() {
            let (s, i) = dir_size(&e.path(), budget);
            size += s;
            items += i;
        } else {
            size += meta.len();
        }
    }
    (size, items)
}

#[tauri::command]
pub async fn files_info(path: String) -> Result<Info, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let p = expand(&path);
        let meta = std::fs::metadata(&p).map_err(explain)?;
        let (size, items) = if meta.is_dir() { dir_size(&p, &mut 200_000) } else { (meta.len(), 0) };
        Ok(Info {
            path: p.to_string_lossy().into_owned(),
            size,
            items,
            modified: seconds(meta.modified()),
            accessed: seconds(meta.accessed()),
            mode: mode_string(meta.permissions().mode()),
            owner: owner_name(meta.uid()),
        })
    })
    .await
    .map_err(|e| e.to_string())?
}

fn thumbs_dir() -> PathBuf {
    crate::media::thumbs_dir().join("files")
}

static THUMB_SLOTS: tokio::sync::Semaphore = tokio::sync::Semaphore::const_new(3);

/// Miniatura de imagem ou vídeo (caminho de um arquivo que a tela abre pelo protocolo asset).
/// Imagens pequenas vão direto; o resto passa pelo ffmpeg uma vez e fica guardado.
#[tauri::command]
pub async fn files_thumb(app: AppHandle, path: String) -> Option<String> {
    let thumb = make_thumb(expand(&path)).await?;
    // A tela só lê pelo protocolo asset o que for liberado, arquivo por arquivo.
    let _ = app.asset_protocol_scope().allow_file(&thumb);
    Some(thumb)
}

async fn make_thumb(p: PathBuf) -> Option<String> {
    let name = p.file_name()?.to_string_lossy().into_owned();
    let kind = kind_of(&name, false);
    let meta = std::fs::metadata(&p).ok()?;
    let ext = name.rsplit_once('.').map(|(_, e)| e.to_ascii_lowercase()).unwrap_or_default();
    if kind == "image" && (ext == "svg" || (meta.len() < 400_000 && matches!(ext.as_str(), "png" | "jpg" | "jpeg" | "webp" | "gif"))) {
        return Some(p.to_string_lossy().into_owned());
    }
    if kind != "image" && kind != "video" {
        return None;
    }
    let key = format!("{:x}", md5::compute(format!("{}:{}:{}", p.display(), meta.len(), seconds(meta.modified()))));
    let out = thumbs_dir().join(format!("{key}.jpg"));
    if out.exists() {
        return Some(out.to_string_lossy().into_owned());
    }
    let _slot = THUMB_SLOTS.acquire().await.ok()?;
    tauri::async_runtime::spawn_blocking(move || {
        std::fs::create_dir_all(thumbs_dir()).ok()?;
        let make = |seek: Option<&str>| {
            let mut cmd = Command::new("ffmpeg");
            cmd.args(["-y", "-loglevel", "error"]);
            if let Some(seek) = seek {
                cmd.args(["-ss", seek]);
            }
            cmd.arg("-i").arg(&p).args(["-vf", "scale=320:-2", "-frames:v", "1", "-q:v", "4"]).arg(&out);
            cmd.stdin(Stdio::null()).stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok_and(|s| s.success()) && out.exists()
        };
        // Vídeo: um quadro a 3 s (o primeiro costuma ser preto); se o vídeo for curto, o começo.
        let ok = if kind == "video" { make(Some("3")) || make(None) } else { make(None) };
        ok.then(|| out.to_string_lossy().into_owned())
    })
    .await
    .ok()
    .flatten()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_and_kinds() {
        assert_eq!(kind_of("Foto.JPG", false), "image");
        assert_eq!(kind_of("filme.mkv", false), "video");
        assert_eq!(kind_of("x", true), "folder");
        assert_eq!(kind_of("README", false), "file");
        assert!(valid_name("Nova pasta"));
        assert!(!valid_name("a/b"));
        assert!(!valid_name(".."));
        assert!(!valid_name("  "));
        assert_eq!(mode_string(0o754), "rwxr-xr--");
    }

    #[test]
    fn free_names() {
        let dir = std::env::temp_dir().join(format!("lumna-files-test-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(dir.join("foto.png"), b"").unwrap();
        assert_eq!(free_name(&dir, "nova.png", false), dir.join("nova.png"));
        assert_eq!(free_name(&dir, "foto.png", false), dir.join("foto (2).png"));
        assert_eq!(free_name(&dir, "foto.png", true), dir.join("foto (cópia).png"));
        std::fs::write(dir.join("foto (cópia).png"), b"").unwrap();
        assert_eq!(free_name(&dir, "foto.png", true), dir.join("foto (2).png"));
        assert_eq!(free_name(&dir, ".bashrc", true), dir.join(".bashrc (cópia)"));
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn reads_system_files() {
        let home = Path::new("/home/lumna");
        let dirs = parse_user_dirs("# x\nXDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\nXDG_MUSIC_DIR=\"/mnt/musica\"\n", home);
        assert_eq!(dirs["DOWNLOAD"], home.join("Downloads"));
        assert_eq!(dirs["MUSIC"], PathBuf::from("/mnt/musica"));

        let info = "[Trash Info]\nPath=/home/lumna/Documentos/rel%C3%B3rio%20final.pdf\nDeletionDate=2026-10-12T18:30:00\n";
        assert_eq!(parse_trashinfo(info), Some(("/home/lumna/Documentos/relório final.pdf".into(), "2026-10-12T18:30:00".into())));
        assert_eq!(percent_decode("a%2"), "a%2");

        let lsblk = r#"{"blockdevices":[
          {"path":"/dev/nvme0n1","label":null,"size":512110190592,"mountpoints":[null],"rm":false,"hotplug":false,"type":"disk","fstype":null,"children":[
            {"path":"/dev/nvme0n1p1","label":null,"size":536870912,"mountpoints":["/boot"],"rm":false,"hotplug":false,"type":"part","fstype":"vfat"},
            {"path":"/dev/nvme0n1p2","label":null,"size":500000000000,"mountpoints":["/"],"rm":false,"hotplug":false,"type":"part","fstype":"btrfs"},
            {"path":"/dev/nvme0n1p3","label":"Windows","size":10000000000,"mountpoints":[null],"rm":false,"hotplug":false,"type":"part","fstype":"ntfs"}]},
          {"path":"/dev/sdb","label":null,"size":32000000000,"mountpoints":[null],"rm":true,"hotplug":true,"type":"disk","fstype":null,"children":[
            {"path":"/dev/sdb1","label":"PENDRIVE","size":32000000000,"mountpoints":["/run/media/lumna/PENDRIVE"],"rm":true,"hotplug":true,"type":"part","fstype":"exfat"}]},
          {"path":"/dev/sdc","label":"ARCH_202610","size":1200000000,"mountpoints":["/run/archiso/bootmnt"],"rm":true,"hotplug":true,"type":"disk","fstype":"iso9660"}]}"#;
        let drives = parse_lsblk(lsblk);
        assert_eq!(drives.len(), 2);
        assert_eq!((drives[0].label.as_str(), drives[0].removable, drives[0].mountpoint.as_deref()), ("Windows", false, None));
        assert_eq!((drives[1].label.as_str(), drives[1].removable), ("PENDRIVE", true));
        assert_eq!(drives[1].mountpoint.as_deref(), Some("/run/media/lumna/PENDRIVE"));
    }
}
