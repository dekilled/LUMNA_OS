//! Papel de parede: um para todos os desktops (por enquanto). Imagens do sistema em
//! `/usr/share/lumna/wallpapers`, as da pessoa em `~/.local/share/lumna/wallpapers` (cópias, para não
//! sumirem se o original mudar de lugar) e a escolha em `~/.config/lumna/wallpaper.json`.
//!
//! "Adicionar imagens" abre o seletor de arquivos do sistema pelo portal (xdg-desktop-portal): ele é
//! uma janela comum, não um popup pendurado nas camadas do shell.

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use futures_util::StreamExt;
use serde::Serialize;
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager};
use zbus::zvariant::{OwnedValue, Value as ZValue};

/// O de sempre, que vem com o LUMNA.
pub const DEFAULT: &str = "/usr/share/lumna/wallpaper.png";
const SYSTEM_DIR: &str = "/usr/share/lumna/wallpapers";
const EXTENSIONS: &[&str] = &["png", "jpg", "jpeg", "webp"];

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

fn user_dir() -> PathBuf {
    std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| home().join(".local/share"))
        .join("lumna/wallpapers")
}

fn config_path() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| home().join(".config"))
        .join("lumna/wallpaper.json")
}

fn is_image(path: &Path) -> bool {
    path.extension().and_then(|e| e.to_str()).is_some_and(|e| EXTENSIONS.contains(&e.to_lowercase().as_str()))
}

/// Só imagens das duas pastas (ou o padrão) podem virar papel de parede. Compara o caminho real
/// (sem `..` nem links), para não escapar das pastas.
fn allowed(path: &Path) -> bool {
    let Ok(real) = path.canonicalize() else { return false };
    let inside = |dir: &Path| dir.canonicalize().is_ok_and(|d| real.starts_with(d));
    real == Path::new(DEFAULT) || (is_image(&real) && (inside(Path::new(SYSTEM_DIR)) || inside(&user_dir())))
}

/// `{"path": desktop, "lock": tela de bloqueio}` (sem `lock` = o mesmo do desktop).
fn read_config() -> Value {
    std::fs::read_to_string(config_path())
        .ok()
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .filter(Value::is_object)
        .unwrap_or_else(|| json!({}))
}

fn write_config(config: &Value) -> Result<(), String> {
    let path = config_path();
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(&path, config.to_string()).map_err(|e| e.to_string())
}

fn valid(key: &str) -> Option<String> {
    read_config()[key].as_str().map(str::to_string).filter(|p| Path::new(p).exists() && allowed(Path::new(p)))
}

/// O papel de parede em uso (o padrão se a escolha sumiu).
pub fn current() -> String {
    valid("path").unwrap_or_else(|| DEFAULT.to_string())
}

/// A imagem escolhida só para a tela de bloqueio, se houver.
pub fn lock_choice() -> Option<String> {
    valid("lock")
}

/// O fundo da tela de bloqueio: a escolhida para ela ou o papel de parede do desktop.
pub fn current_lock() -> String {
    lock_choice().unwrap_or_else(current)
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Wallpaper {
    pub path: String,
    pub name: String,
    /// Adicionada pela pessoa (pode tirar da lista).
    pub user: bool,
}

/// "noite-violeta.jpg" → "Noite violeta".
pub fn pretty_name(path: &Path) -> String {
    let stem = path.file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
    let words = stem.replace(['-', '_'], " ");
    let mut chars = words.trim().chars();
    match chars.next() {
        Some(first) => first.to_uppercase().collect::<String>() + chars.as_str(),
        None => "Imagem".into(),
    }
}

fn images_in(dir: &Path) -> Vec<PathBuf> {
    let mut list: Vec<PathBuf> = std::fs::read_dir(dir)
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.is_file() && is_image(p))
        .collect();
    list.sort();
    list
}

pub fn list() -> Vec<Wallpaper> {
    let mut out = Vec::new();
    if Path::new(DEFAULT).exists() {
        out.push(Wallpaper { path: DEFAULT.into(), name: "LUMNA".into(), user: false });
    }
    for path in images_in(Path::new(SYSTEM_DIR)) {
        out.push(Wallpaper { name: pretty_name(&path), path: path.to_string_lossy().to_string(), user: false });
    }
    for path in images_in(&user_dir()) {
        out.push(Wallpaper { name: pretty_name(&path), path: path.to_string_lossy().to_string(), user: true });
    }
    out
}

/// Libera as pastas de papel de parede para o `<img>` (protocolo asset).
fn allow_assets(app: &AppHandle) {
    let scope = app.asset_protocol_scope();
    let _ = scope.allow_file(DEFAULT);
    let _ = scope.allow_directory(SYSTEM_DIR, false);
    let _ = scope.allow_directory(user_dir(), false);
}

/// Nome livre na pasta da pessoa ("foto.jpg", "foto (2).jpg"...).
pub fn free_name(dir: &Path, name: &str) -> PathBuf {
    let candidate = dir.join(name);
    if !candidate.exists() {
        return candidate;
    }
    let path = Path::new(name);
    let stem = path.file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_else(|| "imagem".into());
    let ext = path.extension().map(|e| format!(".{}", e.to_string_lossy())).unwrap_or_default();
    (2..).map(|n| dir.join(format!("{stem} ({n}){ext}"))).find(|p| !p.exists()).expect("sempre acha um nome")
}

/// `file:///home/a/Imagens/praia%20azul.jpg` → caminho.
pub fn uri_to_path(uri: &str) -> Option<PathBuf> {
    let rest = uri.strip_prefix("file://")?;
    let bytes = rest.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            if let Ok(b) = u8::from_str_radix(&rest[i + 1..i + 3], 16) {
                out.push(b);
                i += 3;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    Some(PathBuf::from(String::from_utf8_lossy(&out).to_string()))
}

/// Seletor de arquivos do sistema (portal FileChooser): devolve as imagens escolhidas.
async fn choose_images() -> Result<Vec<PathBuf>, String> {
    let connection = zbus::Connection::session().await.map_err(|e| format!("D-Bus: {e}"))?;
    // O portal responde por um objeto Request cujo caminho dá para prever pelo token: escuta antes
    // de pedir, para não perder a resposta.
    let token = format!("lumna{}", std::process::id());
    let sender = connection.unique_name().map(|n| n.trim_start_matches(':').replace('.', "_")).unwrap_or_default();
    let request_path = format!("/org/freedesktop/portal/desktop/request/{sender}/{token}");
    let rule = zbus::MatchRule::builder()
        .msg_type(zbus::message::Type::Signal)
        .interface("org.freedesktop.portal.Request")
        .map_err(|e| e.to_string())?
        .member("Response")
        .map_err(|e| e.to_string())?
        .path(request_path.as_str())
        .map_err(|e| e.to_string())?
        .build();
    let mut responses = zbus::MessageStream::for_match_rule(rule, &connection, Some(4)).await.map_err(|e| e.to_string())?;

    let filter = ("Imagens", vec![(0u32, "*.png"), (0u32, "*.jpg"), (0u32, "*.jpeg"), (0u32, "*.webp")]);
    let mut options: HashMap<&str, ZValue> = HashMap::new();
    options.insert("handle_token", ZValue::from(token.as_str()));
    options.insert("multiple", ZValue::from(true));
    options.insert("modal", ZValue::from(true));
    options.insert("filters", ZValue::from(vec![filter.clone()]));
    options.insert("current_filter", ZValue::from(filter));
    connection
        .call_method(
            Some("org.freedesktop.portal.Desktop"),
            "/org/freedesktop/portal/desktop",
            Some("org.freedesktop.portal.FileChooser"),
            "OpenFile",
            &("", "Escolher papéis de parede", options),
        )
        .await
        .map_err(|e| format!("o seletor de arquivos não abriu: {e}"))?;

    let message = responses.next().await.ok_or("o seletor de arquivos fechou sem responder")?.map_err(|e| e.to_string())?;
    let (response, results): (u32, HashMap<String, OwnedValue>) = message.body().deserialize().map_err(|e| e.to_string())?;
    // 0 = escolheu; 1 = cancelou.
    if response != 0 {
        return Ok(Vec::new());
    }
    let uris: Vec<String> = results.get("uris").and_then(|v| Vec::<String>::try_from(v.clone()).ok()).unwrap_or_default();
    Ok(uris.iter().filter_map(|u| uri_to_path(u)).filter(|p| is_image(p)).collect())
}

// ---- comandos ----

#[tauri::command]
pub fn wallpaper_list(app: AppHandle) -> Value {
    allow_assets(&app);
    json!({ "current": current(), "lock": lock_choice(), "items": list() })
}

#[tauri::command]
pub fn wallpaper_set(app: AppHandle, path: String) -> Result<(), String> {
    let chosen = PathBuf::from(&path);
    if !allowed(&chosen) {
        return Err("Essa imagem não está na lista de papéis de parede.".into());
    }
    let mut config = read_config();
    config["path"] = json!(path);
    write_config(&config)?;
    allow_assets(&app);
    // Todos os desktops (e quem mais mostrar o papel de parede) trocam juntos.
    let _ = app.emit("lumna://wallpaper", &path);
    // A tela de bloqueio usa o mesmo, se não tiver um só dela.
    crate::lock::write_lock_config();
    Ok(())
}

/// Fundo só da tela de bloqueio; `None` volta a usar o do desktop.
#[tauri::command]
pub fn wallpaper_set_lock(path: Option<String>) -> Result<(), String> {
    let mut config = read_config();
    match path {
        Some(path) => {
            if !allowed(Path::new(&path)) {
                return Err("Essa imagem não está na lista de papéis de parede.".into());
            }
            config["lock"] = json!(path);
        }
        None => {
            if let Some(map) = config.as_object_mut() {
                map.remove("lock");
            }
        }
    }
    write_config(&config)?;
    crate::lock::write_lock_config();
    Ok(())
}

/// Abre o seletor, copia as imagens escolhidas para a pasta da pessoa e devolve os caminhos novos.
#[tauri::command]
pub async fn wallpaper_add(app: AppHandle) -> Result<Vec<String>, String> {
    let chosen = choose_images().await?;
    let dir = user_dir();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let mut added = Vec::new();
    for source in chosen {
        let name = source.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_else(|| "imagem.jpg".into());
        let target = free_name(&dir, &name);
        std::fs::copy(&source, &target).map_err(|e| format!("{}: {e}", source.display()))?;
        added.push(target.to_string_lossy().to_string());
    }
    allow_assets(&app);
    Ok(added)
}

/// Tira da lista uma imagem que a pessoa adicionou (a cópia; o original fica onde estava).
#[tauri::command]
pub fn wallpaper_remove(app: AppHandle, path: String) -> Result<(), String> {
    let target = PathBuf::from(&path);
    if !target.starts_with(user_dir()) || !is_image(&target) {
        return Err("Só as imagens que você adicionou podem sair da lista.".into());
    }
    let was_current = current() == path;
    let was_lock = lock_choice().as_deref() == Some(path.as_str());
    std::fs::remove_file(&target).map_err(|e| e.to_string())?;
    if was_lock {
        wallpaper_set_lock(None)?;
    }
    if was_current {
        // Volta ao padrão no desktop (a escolha da tela de bloqueio fica).
        let mut config = read_config();
        if let Some(map) = config.as_object_mut() {
            map.remove("path");
        }
        write_config(&config)?;
        let _ = app.emit("lumna://wallpaper", DEFAULT);
        crate::lock::write_lock_config();
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_and_uris() {
        assert_eq!(pretty_name(Path::new("/x/noite-violeta.jpg")), "Noite violeta");
        assert_eq!(pretty_name(Path::new("/x/praia_azul.png")), "Praia azul");
        assert_eq!(
            uri_to_path("file:///home/a/Imagens/praia%20azul.jpg"),
            Some(PathBuf::from("/home/a/Imagens/praia azul.jpg"))
        );
        assert_eq!(uri_to_path("https://x.com/a.jpg"), None);
        assert!(is_image(Path::new("a.JPG")) && !is_image(Path::new("a.gif")));
    }

    #[test]
    fn free_names() {
        let dir = std::env::temp_dir().join(format!("lumna-wall-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        assert_eq!(free_name(&dir, "foto.jpg"), dir.join("foto.jpg"));
        std::fs::write(dir.join("foto.jpg"), "").unwrap();
        assert_eq!(free_name(&dir, "foto.jpg"), dir.join("foto (2).jpg"));
        let _ = std::fs::remove_dir_all(dir);
    }

    #[test]
    fn only_listed_images_are_allowed() {
        assert!(!allowed(Path::new("/etc/shadow")));
        assert!(!allowed(Path::new("/usr/share/lumna/wallpapers/../../../etc/hostname")));
    }
}
