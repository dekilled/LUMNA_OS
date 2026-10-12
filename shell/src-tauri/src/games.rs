//! Biblioteca de jogos do modo Gaming: Steam, Heroic (Epic, GOG, Amazon) e Lutris.
//!
//! Só lê o que as lojas já guardam no disco; nada é baixado. As capas são caminhos locais
//! (servidos ao webview pelo protocolo `asset` do Tauri) ou, no Heroic, a URL da loja.

use std::path::{Path, PathBuf};

use serde::Serialize;
use serde_json::Value as Json;

use crate::vdf;

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Game {
    /// Único na biblioteca: `steam:730`, `epic:Fortnite`, `lutris:12`.
    pub id: String,
    /// `steam`, `epic`, `gog`, `amazon` ou `lutris`.
    pub source: &'static str,
    pub title: String,
    /// Capa em pé (600×900): caminho absoluto ou URL.
    pub cover: Option<String>,
    /// Arte larga para o fundo: caminho absoluto ou URL.
    pub hero: Option<String>,
    /// Segundos desde 1970; 0 se nunca jogado (ou a loja não informa).
    pub last_played: u64,
    #[serde(skip)]
    pub launch: String,
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default()
}

fn data_home() -> PathBuf {
    std::env::var_os("XDG_DATA_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".local/share"))
}

fn config_home() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".config"))
}

// ---- Steam ----

/// Ferramentas que a Steam instala como "apps" mas não são jogos.
fn is_steam_tool(appid: &str, name: &str) -> bool {
    const TOOL_IDS: &[&str] = &["228980", "1070560", "1391110", "1628350", "1493710", "1826330"];
    TOOL_IDS.contains(&appid)
        || ["Proton", "Steam Linux Runtime", "Steamworks Common"].iter().any(|p| name.starts_with(p))
}

fn steam_roots() -> Vec<PathBuf> {
    let mut roots = vec![
        data_home().join("Steam"),
        home().join(".steam/steam"),
        home().join(".var/app/com.valvesoftware.Steam/.local/share/Steam"),
    ];
    // ~/.steam/steam costuma ser um link para ~/.local/share/Steam: não ler duas vezes.
    roots.retain(|r| r.join("steamapps").is_dir());
    let mut unique: Vec<PathBuf> = Vec::new();
    for root in roots {
        let real = root.canonicalize().unwrap_or(root);
        if !unique.contains(&real) {
            unique.push(real);
        }
    }
    unique
}

/// Procura uma arte da Steam no cache de capas. A Steam já usou dois formatos:
/// `librarycache/<appid>_library_600x900.jpg` e `librarycache/<appid>/.../library_600x900.jpg`.
fn steam_art(root: &Path, appid: &str, name: &str) -> Option<String> {
    let cache = root.join("appcache/librarycache");
    for ext in ["jpg", "png"] {
        let legacy = cache.join(format!("{appid}_{name}.{ext}"));
        if legacy.is_file() {
            return Some(legacy.to_string_lossy().into());
        }
    }
    let dir = cache.join(appid);
    let mut stack = vec![(dir, 0)];
    while let Some((dir, depth)) = stack.pop() {
        let Ok(entries) = std::fs::read_dir(&dir) else { continue };
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_dir() && depth < 2 {
                stack.push((path, depth + 1));
            } else if path.file_stem().is_some_and(|s| s.to_string_lossy() == name) {
                return Some(path.to_string_lossy().into());
            }
        }
    }
    None
}

fn steam_games() -> Vec<Game> {
    let mut games = Vec::new();
    for root in steam_roots() {
        let mut libraries = vec![root.join("steamapps")];
        if let Ok(text) = std::fs::read_to_string(root.join("steamapps/libraryfolders.vdf")) {
            let doc = vdf::parse(&text);
            if let Some(folders) = doc.get("libraryfolders") {
                for (_, folder) in folders.entries() {
                    if let Some(path) = folder.text("path") {
                        let steamapps = PathBuf::from(path).join("steamapps");
                        if !libraries.contains(&steamapps) {
                            libraries.push(steamapps);
                        }
                    }
                }
            }
        }
        for library in libraries {
            let Ok(entries) = std::fs::read_dir(&library) else { continue };
            for entry in entries.flatten() {
                let file = entry.file_name().to_string_lossy().to_string();
                if !(file.starts_with("appmanifest_") && file.ends_with(".acf")) {
                    continue;
                }
                let Ok(text) = std::fs::read_to_string(entry.path()) else { continue };
                let doc = vdf::parse(&text);
                let Some(app) = doc.get("AppState") else { continue };
                let (Some(appid), Some(name)) = (app.text("appid"), app.text("name")) else { continue };
                // StateFlags 4 = totalmente instalado.
                let installed = app.text("StateFlags").and_then(|f| f.parse::<u32>().ok()).is_some_and(|f| f & 4 != 0);
                if !installed || is_steam_tool(appid, name) {
                    continue;
                }
                games.push(Game {
                    id: format!("steam:{appid}"),
                    source: "steam",
                    title: name.to_string(),
                    cover: steam_art(&root, appid, "library_600x900"),
                    hero: steam_art(&root, appid, "library_hero"),
                    last_played: app.text("LastPlayed").and_then(|t| t.parse().ok()).unwrap_or(0),
                    launch: format!("steam steam://rungameid/{appid}"),
                });
            }
        }
    }
    games
}

// ---- Heroic (Epic, GOG, Amazon) ----

/// A lista de jogos de cada loja que o Heroic guarda em cache, filtrada pelos instalados.
fn heroic_games() -> Vec<Game> {
    let base = [config_home().join("heroic"), home().join(".var/app/com.heroicgameslauncher.hgl/config/heroic")]
        .into_iter()
        .find(|p| p.is_dir());
    let Some(base) = base else { return Vec::new() };
    let stores: [(&str, &str, &str); 3] = [
        ("epic", "legendary", "store_cache/legendary_library.json"),
        ("gog", "gog", "store_cache/gog_library.json"),
        ("amazon", "nile", "store_cache/nile_library.json"),
    ];
    let mut games = Vec::new();
    for (source, runner, file) in stores {
        let Ok(text) = std::fs::read_to_string(base.join(file)) else { continue };
        let Ok(doc) = serde_json::from_str::<Json>(&text) else { continue };
        let list = doc.get("library").or_else(|| doc.get("games")).and_then(Json::as_array);
        for game in list.into_iter().flatten() {
            if game.get("is_installed").and_then(Json::as_bool) != Some(true) {
                continue;
            }
            let field = |key: &str| game.get(key).and_then(Json::as_str).filter(|s| !s.is_empty()).map(str::to_string);
            let (Some(app_name), Some(title)) = (field("app_name"), field("title")) else { continue };
            games.push(Game {
                id: format!("{source}:{app_name}"),
                source,
                title,
                cover: field("art_square").or_else(|| field("art_cover")),
                hero: field("art_background").or_else(|| field("art_cover")),
                last_played: 0,
                launch: format!("xdg-open heroic://launch/{runner}/{app_name}"),
            });
        }
    }
    games
}

// ---- Lutris ----

fn lutris_games() -> Vec<Game> {
    let db = data_home().join("lutris/pga.db");
    let Ok(conn) = rusqlite::Connection::open_with_flags(&db, rusqlite::OpenFlags::SQLITE_OPEN_READ_ONLY) else {
        return Vec::new();
    };
    let Ok(mut query) = conn.prepare("SELECT id, name, slug, COALESCE(lastplayed, 0) FROM games WHERE installed = 1")
    else {
        return Vec::new();
    };
    let covers = [data_home().join("lutris/coverart"), home().join(".cache/lutris/coverart")];
    let rows = query.query_map([], |row| {
        Ok((row.get::<_, i64>(0)?, row.get::<_, String>(1)?, row.get::<_, String>(2)?, row.get::<_, i64>(3)?))
    });
    let Ok(rows) = rows else { return Vec::new() };
    rows.flatten()
        .map(|(id, name, slug, lastplayed)| Game {
            id: format!("lutris:{id}"),
            source: "lutris",
            title: name,
            cover: covers
                .iter()
                .flat_map(|dir| ["jpg", "png"].map(|ext| dir.join(format!("{slug}.{ext}"))))
                .find(|p| p.is_file())
                .map(|p| p.to_string_lossy().into()),
            hero: None,
            last_played: lastplayed.max(0) as u64,
            launch: format!("lutris lutris:rungameid/{id}"),
        })
        .collect()
}

/// Todos os jogos instalados: os jogados por último primeiro, depois em ordem alfabética.
pub fn list() -> Vec<Game> {
    let mut games: Vec<Game> = steam_games().into_iter().chain(heroic_games()).chain(lutris_games()).collect();
    games.sort_by(|a, b| b.last_played.cmp(&a.last_played).then_with(|| a.title.to_lowercase().cmp(&b.title.to_lowercase())));
    games
}

/// Pastas onde podem estar as capas locais (liberadas para o webview pelo protocolo `asset`).
pub fn art_roots() -> Vec<PathBuf> {
    let mut roots: Vec<PathBuf> = steam_roots().into_iter().map(|r| r.join("appcache/librarycache")).collect();
    roots.push(data_home().join("lutris/coverart"));
    roots.push(home().join(".cache/lutris/coverart"));
    roots
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn steam_tools_are_not_games() {
        assert!(is_steam_tool("1493710", "Proton Experimental"));
        assert!(is_steam_tool("228980", "Steamworks Common Redistributables"));
        assert!(!is_steam_tool("730", "Counter-Strike 2"));
    }

    #[test]
    fn finds_steam_art_in_both_layouts() {
        let root = std::env::temp_dir().join(format!("lumna-art-{}", std::process::id()));
        let cache = root.join("appcache/librarycache");
        std::fs::create_dir_all(cache.join("620/abc123")).unwrap();
        std::fs::write(cache.join("400_library_600x900.jpg"), b"x").unwrap();
        std::fs::write(cache.join("620/abc123/library_600x900.jpg"), b"x").unwrap();
        assert!(steam_art(&root, "400", "library_600x900").unwrap().ends_with("400_library_600x900.jpg"));
        assert!(steam_art(&root, "620", "library_600x900").unwrap().ends_with("abc123/library_600x900.jpg"));
        assert_eq!(steam_art(&root, "999", "library_600x900"), None);
        std::fs::remove_dir_all(root).unwrap();
    }
}
