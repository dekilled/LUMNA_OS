//! Apps instalados, lidos dos arquivos `.desktop` (padrão freedesktop).

use std::collections::HashMap;
use std::path::{Path, PathBuf};

use base64::Engine;
use gtk::prelude::*;
use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct App {
    /// Nome do arquivo sem `.desktop` (ex.: `firefox`, `org.xfce.mousepad`).
    pub id: String,
    pub name: String,
    pub comment: String,
    /// Ícone pronto para o `<img>` (data URL), quando o tema tem um.
    pub icon: Option<String>,
    /// Classe da janela, para saber se o app está aberto.
    pub wm_class: Option<String>,
    #[serde(skip)]
    pub exec: String,
    /// Nome genérico e palavras-chave ("Navegador", "web;internet"), para achar o app por descrição.
    #[serde(skip)]
    pub keywords: String,
    #[serde(skip)]
    pub terminal: bool,
}

impl App {
    /// Linha de comando sem os códigos de campo (`%f`, `%U`...), que só fazem sentido com arquivos.
    pub fn command(&self, terminal: &str) -> String {
        let mut parts = Vec::new();
        let mut chars = self.exec.chars().peekable();
        let mut word = String::new();
        while let Some(c) = chars.next() {
            if c == '%' {
                if chars.next() == Some('%') {
                    word.push('%');
                }
            } else if c == ' ' {
                if !word.is_empty() {
                    parts.push(std::mem::take(&mut word));
                }
            } else {
                word.push(c);
            }
        }
        if !word.is_empty() {
            parts.push(word);
        }
        let command = parts.join(" ");
        if self.terminal {
            format!("{terminal} -e {command}")
        } else {
            command
        }
    }
}

/// Utilitários que vêm junto com outros pacotes e só poluem o launcher.
const HIDDEN: &[&str] = &[
    "avahi-discover",
    "bssh",
    "bvnc",
    "qv4l2",
    "qvidcap",
    "footclient",
    "foot-server",
    "xfce4-about",
    "thunar-settings",
    "thunar-bulk-rename",
    "lstopo",
];

fn data_dirs() -> Vec<PathBuf> {
    let home = std::env::var_os("XDG_DATA_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".local/share")));
    let system = std::env::var("XDG_DATA_DIRS").unwrap_or_else(|_| "/usr/local/share:/usr/share".into());
    let mut dirs: Vec<PathBuf> = home.into_iter().chain(system.split(':').filter(|d| !d.is_empty()).map(PathBuf::from)).collect();
    // Apps do Flatpak, mesmo numa sessão aberta antes do XDG_DATA_DIRS certo (o da pessoa primeiro).
    let flatpak = [
        std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".local/share/flatpak/exports/share")),
        Some(PathBuf::from("/var/lib/flatpak/exports/share")),
    ];
    for dir in flatpak.into_iter().flatten() {
        if !dirs.contains(&dir) {
            dirs.push(dir);
        }
    }
    dirs
}

/// Lê a seção `[Desktop Entry]`, preferindo os textos em português.
fn parse(path: &Path) -> Option<HashMap<String, String>> {
    let text = std::fs::read_to_string(path).ok()?;
    let mut in_entry = false;
    let mut fields = HashMap::new();
    for line in text.lines() {
        let line = line.trim();
        if line.starts_with('[') {
            in_entry = line == "[Desktop Entry]";
            continue;
        }
        if !in_entry || line.starts_with('#') {
            continue;
        }
        if let Some((key, value)) = line.split_once('=') {
            fields.insert(key.trim().to_string(), value.trim().to_string());
        }
    }
    Some(fields)
}

fn localized(fields: &HashMap<String, String>, key: &str) -> String {
    ["pt_BR", "pt"]
        .iter()
        .find_map(|lang| fields.get(&format!("{key}[{lang}]")))
        .or_else(|| fields.get(key))
        .cloned()
        .unwrap_or_default()
}

/// Resolve o ícone pelo tema GTK e devolve como data URL. Só na thread principal.
fn icon_data_url(icon: &str) -> Option<String> {
    let path = if icon.starts_with('/') {
        PathBuf::from(icon)
    } else {
        let theme = gtk::IconTheme::default()?;
        // Ícones dos apps do Flatpak (sessões abertas antes do XDG_DATA_DIRS com as pastas dele).
        static FLATPAK_ICONS: std::sync::Once = std::sync::Once::new();
        FLATPAK_ICONS.call_once(|| {
            for dir in data_dirs().iter().filter(|d| d.to_string_lossy().contains("flatpak")) {
                theme.append_search_path(dir.join("icons"));
            }
        });
        let info = theme.lookup_icon(icon, 64, gtk::IconLookupFlags::FORCE_SIZE)?;
        info.filename()?
    };
    let bytes = std::fs::read(&path).ok()?;
    let mime = match path.extension().and_then(|e| e.to_str()) {
        Some("svg") => "image/svg+xml",
        Some("png") => "image/png",
        Some("xpm") => return None,
        _ => "image/png",
    };
    Some(format!("data:{mime};base64,{}", base64::engine::general_purpose::STANDARD.encode(bytes)))
}

/// Todos os apps visíveis, em ordem alfabética. Só na thread principal (resolve ícones pelo GTK).
pub fn list() -> Vec<App> {
    scan(true)
}

/// Os mesmos apps, sem ícones: roda em qualquer thread (ações da Emma, `lumna-ctl action`).
pub fn list_plain() -> Vec<App> {
    scan(false)
}

fn scan(icons: bool) -> Vec<App> {
    // `None` marca um app escondido: um arquivo escondido do usuário também esconde o do sistema.
    let mut apps: HashMap<String, Option<App>> = HashMap::new();
    for dir in data_dirs() {
        let Ok(entries) = std::fs::read_dir(dir.join("applications")) else { continue };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) != Some("desktop") {
                continue;
            }
            let id = path.file_stem().unwrap_or_default().to_string_lossy().to_string();
            // O primeiro diretório (o do usuário) ganha dos do sistema.
            if apps.contains_key(&id) {
                continue;
            }
            let Some(fields) = parse(&path) else { continue };
            let hidden = HIDDEN.contains(&id.as_str())
                || ["NoDisplay", "Hidden"].iter().any(|k| fields.get(*k).is_some_and(|v| v == "true"));
            let is_app = fields.get("Type").is_some_and(|t| t == "Application");
            let Some(exec) = fields.get("Exec").cloned() else { continue };
            if hidden || !is_app {
                apps.insert(id, None);
                continue;
            }
            apps.insert(
                id.clone(),
                Some(App {
                    id,
                    name: localized(&fields, "Name"),
                    comment: localized(&fields, "Comment"),
                    icon: if icons { fields.get("Icon").and_then(|i| icon_data_url(i)) } else { None },
                    wm_class: fields.get("StartupWMClass").cloned(),
                    exec,
                    keywords: format!("{} {}", localized(&fields, "GenericName"), localized(&fields, "Keywords").replace(';', " ")),
                    terminal: fields.get("Terminal").is_some_and(|t| t == "true"),
                }),
            );
        }
    }
    let mut list: Vec<App> = apps.into_values().flatten().collect();
    list.sort_by_key(|a| a.name.to_lowercase());
    list
}

#[cfg(test)]
mod tests {
    use super::*;

    fn app(exec: &str, terminal: bool) -> App {
        App {
            id: "x".into(),
            name: "X".into(),
            comment: String::new(),
            icon: None,
            wm_class: None,
            exec: exec.into(),
            keywords: String::new(),
            terminal,
        }
    }

    #[test]
    fn strips_field_codes() {
        assert_eq!(app("firefox %u", false).command("foot"), "firefox");
        assert_eq!(app("mousepad %F", false).command("foot"), "mousepad");
        assert_eq!(app("echo 100%%", false).command("foot"), "echo 100%");
    }

    #[test]
    fn terminal_apps_open_in_the_terminal() {
        assert_eq!(app("htop", true).command("foot"), "foot -e htop");
    }
}
