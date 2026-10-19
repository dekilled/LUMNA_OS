//! Qobuz no LUMNA Music (com a assinatura da pessoa). O login é o do próprio web player
//! (play.qobuz.com) numa janela do LUMNA: a pessoa entra como sempre e um script pega o token da
//! sessão (do `localStorage` ou do cabeçalho que o player manda) e entrega navegando pra um
//! endereço que não existe (`https://lumna.invalid/...`), que o LUMNA intercepta e cancela. A
//! página do Qobuz não ganha acesso a nenhum comando do LUMNA. Senha nenhuma passa por aqui.
//!
//! Com o token, a API do web player (www.qobuz.com/api.json/0.2) dá as playlists e as favoritas.
//! Ela não é pública: o app id vem do próprio web player (lido do bundle.js dele).

use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager, WebviewUrl, WebviewWindowBuilder};

use crate::music::Track;

const API: &str = "https://www.qobuz.com/api.json/0.2";
const PLAYER: &str = "https://play.qobuz.com";
/// O endereço de mentira por onde o script da janela de login fala com o LUMNA.
const BRIDGE: &str = "lumna.invalid";

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(default)]
struct Account {
    app_id: String,
    token: String,
    user_id: String,
    name: String,
}

fn account_path() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna").join("qobuz.json")
}

fn read_account() -> Option<Account> {
    let a: Account = serde_json::from_slice(&std::fs::read(account_path()).ok()?).ok()?;
    (!a.token.is_empty() && !a.app_id.is_empty()).then_some(a)
}

/// O token é a chave da conta: o arquivo fica só pra pessoa (0600).
fn write_account(a: &Account) -> Result<(), String> {
    use std::io::Write;
    use std::os::unix::fs::OpenOptionsExt;
    let path = account_path();
    std::fs::create_dir_all(path.parent().unwrap_or(std::path::Path::new("/"))).map_err(|e| e.to_string())?;
    let mut f = std::fs::OpenOptions::new().write(true).create(true).truncate(true).mode(0o600).open(&path).map_err(|e| e.to_string())?;
    f.write_all(&serde_json::to_vec_pretty(a).unwrap_or_default()).map_err(|e| e.to_string())
}

// ---- a API ----

/// GET na API; devolve o JSON (erro com a mensagem do Qobuz).
fn api(account: &Account, endpoint: &str, params: &[(&str, String)]) -> Result<Value, String> {
    let q = crate::emma::curl_quote;
    let mut config = format!("url = {}\nget\n", q(&format!("{API}/{endpoint}")));
    for (k, v) in params {
        config.push_str(&format!("data-urlencode = {}\n", q(&format!("{k}={v}"))));
    }
    config.push_str(&format!("header = {}\n", q(&format!("X-App-Id: {}", account.app_id))));
    if !account.token.is_empty() {
        config.push_str(&format!("header = {}\n", q(&format!("X-User-Auth-Token: {}", account.token))));
    }
    let child = crate::emma::curl(&config, &["--max-time", "25", "-w", "\n%{http_code}"])?;
    let out = child.wait_with_output().map_err(|e| e.to_string())?;
    let text = String::from_utf8_lossy(&out.stdout);
    let (body, code) = text.rsplit_once('\n').unwrap_or((&text, "0"));
    let code: u16 = code.trim().parse().unwrap_or(0);
    let value: Value = serde_json::from_str(body).unwrap_or(Value::Null);
    match code {
        200 => Ok(value),
        0 => Err("sem conexão com o Qobuz".into()),
        401 => Err("o login do Qobuz venceu: conecte de novo".into()),
        _ => Err(value["message"].as_str().map(str::to_string).unwrap_or_else(|| format!("o Qobuz respondeu {code}"))),
    }
}

/// O app id do web player, lido do bundle.js da página de login.
fn player_app_id() -> Result<String, String> {
    let get = |url: &str| -> Result<String, String> {
        let out = std::process::Command::new("curl").args(["-sSL", "--max-time", "60", url]).output().map_err(|e| e.to_string())?;
        Ok(String::from_utf8_lossy(&out.stdout).into_owned())
    };
    let page = get(&format!("{PLAYER}/login"))?;
    let bundle = page
        .split('"')
        .find(|s| s.starts_with("/resources/") && s.ends_with("/bundle.js"))
        .ok_or("não achei o player do Qobuz (a página mudou?)")?;
    let js = get(&format!("{PLAYER}{bundle}"))?;
    let at = js.find("production:{api:{appId:\"").ok_or("não achei o app id do player do Qobuz")?;
    let rest = &js[at + "production:{api:{appId:\"".len()..];
    let id: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
    if id.is_empty() { Err("app id do Qobuz vazio".into()) } else { Ok(id) }
}

// ---- o que vai pra tela ----

#[derive(Debug, Clone, Serialize)]
pub struct QobuzStatus {
    pub connected: bool,
    pub name: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct QobuzPlaylist {
    pub id: String,
    pub name: String,
    pub description: String,
    pub count: u64,
    /// Segundos.
    pub duration: u64,
    pub cover: Option<String>,
    pub owner: String,
}

fn text(v: &Value) -> String {
    v.as_str().map(str::to_string).or_else(|| v.as_u64().map(|n| n.to_string())).unwrap_or_default()
}

/// Uma faixa do Qobuz no formato das do LUMNA (`qobuz:<id>` no lugar do caminho).
pub fn to_track(t: &Value) -> Option<Track> {
    let id = text(&t["id"]);
    if id.is_empty() {
        return None;
    }
    let album = &t["album"];
    let mut title = text(&t["title"]);
    let version = text(&t["version"]);
    if !version.is_empty() {
        title = format!("{title} ({version})");
    }
    let album_artist = text(&album["artist"]["name"]);
    let artist = Some(text(&t["performer"]["name"])).filter(|s| !s.is_empty()).unwrap_or_else(|| album_artist.clone());
    let image = &album["image"];
    let cover = ["large", "small", "thumbnail"].iter().map(|k| text(&image[*k])).find(|s| !s.is_empty());
    Some(Track {
        path: format!("qobuz:{id}"),
        title,
        artist,
        album: text(&album["title"]),
        album_artist,
        track: t["track_number"].as_u64().unwrap_or(0) as u32,
        disc: t["media_number"].as_u64().unwrap_or(0) as u32,
        year: text(&album["release_date_original"]).get(..4).and_then(|y| y.parse().ok()).unwrap_or(0),
        duration: t["duration"].as_f64().unwrap_or(0.0),
        cover,
        mtime: 0,
    })
}

fn to_playlist(p: &Value) -> QobuzPlaylist {
    let cover = ["images300", "image_rectangle", "images150", "images"]
        .iter()
        .find_map(|k| p[*k].as_array().and_then(|a| a.first()).and_then(Value::as_str).map(str::to_string));
    QobuzPlaylist {
        id: text(&p["id"]),
        name: text(&p["name"]),
        description: text(&p["description"]),
        count: p["tracks_count"].as_u64().unwrap_or(0),
        duration: p["duration"].as_u64().unwrap_or(0),
        cover,
        owner: text(&p["owner"]["name"]),
    }
}

fn need_account() -> Result<Account, String> {
    read_account().ok_or_else(|| "conecte a sua conta do Qobuz primeiro".to_string())
}

// ---- comandos ----

#[tauri::command]
pub fn qobuz_status() -> QobuzStatus {
    match read_account() {
        Some(a) => QobuzStatus { connected: true, name: a.name },
        None => QobuzStatus { connected: false, name: String::new() },
    }
}

#[tauri::command]
pub fn qobuz_logout(app: AppHandle) -> Result<(), String> {
    match std::fs::remove_file(account_path()) {
        Err(e) if e.kind() != std::io::ErrorKind::NotFound => return Err(e.to_string()),
        _ => {}
    }
    let _ = app.emit("lumna://qobuz", json!({ "connected": false, "name": "" }));
    Ok(())
}

#[tauri::command]
pub async fn qobuz_playlists() -> Result<Vec<QobuzPlaylist>, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let a = need_account()?;
        let body = api(&a, "playlist/getUserPlaylists", &[("limit", "500".into())])?;
        Ok(body["playlists"]["items"].as_array().map(|l| l.iter().map(to_playlist).collect()).unwrap_or_default())
    })
    .await
    .map_err(|e| e.to_string())?
}

/// As músicas de uma playlist (de 500 em 500, até 5000).
#[tauri::command]
pub async fn qobuz_playlist_tracks(id: String) -> Result<Vec<Track>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let a = need_account()?;
        let mut tracks = Vec::new();
        let mut offset = 0;
        loop {
            let body = api(&a, "playlist/get", &[("playlist_id", id.clone()), ("extra", "tracks".into()), ("limit", "500".into()), ("offset", offset.to_string())])?;
            let items = body["tracks"]["items"].as_array().cloned().unwrap_or_default();
            let total = body["tracks"]["total"].as_u64().unwrap_or(0) as usize;
            tracks.extend(items.iter().filter_map(to_track));
            offset += items.len();
            if items.is_empty() || offset >= total || offset >= 5000 {
                break;
            }
        }
        Ok(tracks)
    })
    .await
    .map_err(|e| e.to_string())?
}

/// As músicas favoritas da conta.
#[tauri::command]
pub async fn qobuz_favorites() -> Result<Vec<Track>, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let a = need_account()?;
        let body = api(&a, "favorite/getUserFavorites", &[("type", "tracks".into()), ("limit", "500".into())])?;
        Ok(body["tracks"]["items"].as_array().map(|l| l.iter().filter_map(to_track).collect()).unwrap_or_default())
    })
    .await
    .map_err(|e| e.to_string())?
}

// ---- a janela de login ----

/// O que roda na página do Qobuz: acha o token (no `localStorage` ou no cabeçalho que o player
/// manda) e entrega pelo endereço de mentira. Ao tocar num campo, pede o teclado do LUMNA.
const LOGIN_SCRIPT: &str = r#"(() => {
  if (location.hostname === "lumna.invalid" || window.__lumnaQobuz) return;
  window.__lumnaQobuz = true;
  let appId = "", sent = false;
  const send = (token, user) => {
    if (sent || !token) return;
    sent = true;
    location.href = "https://lumna.invalid/qobuz?token=" + encodeURIComponent(token) + "&app=" + encodeURIComponent(appId) + "&user=" + encodeURIComponent(user || "");
  };
  const look = (name, value) => {
    if (/^x-app-id$/i.test(name)) appId = String(value);
    else if (/^x-user-auth-token$/i.test(name) && value) send(String(value), "");
  };
  const setHeader = XMLHttpRequest.prototype.setRequestHeader;
  XMLHttpRequest.prototype.setRequestHeader = function (name, value) {
    try { look(name, value); } catch (e) {}
    return setHeader.apply(this, arguments);
  };
  const realFetch = window.fetch;
  window.fetch = function (input, init) {
    try { new Headers((init && init.headers) || (input && input.headers) || {}).forEach((v, k) => look(k, v)); } catch (e) {}
    return realFetch.apply(this, arguments);
  };
  setInterval(() => {
    try {
      const u = JSON.parse(localStorage.getItem("localuser") || "null");
      if (!u) return;
      const token = u.user_auth_token || u.token || (u.credential && u.credential.user_auth_token);
      send(token, u.id || (u.user && u.user.id) || "");
    } catch (e) {}
  }, 700);
  document.addEventListener("focusin", (e) => {
    const t = e.target;
    if (t && t.matches && t.matches("input:not([type=checkbox]):not([type=radio]), textarea")) location.href = "https://lumna.invalid/teclado";
  }, true);
})();"#;

/// O token chegou da janela: confere na API, guarda e avisa a tela.
fn finish_login(app: &AppHandle, token: String, app_id: String, user_id: String) {
    let result = (|| -> Result<Account, String> {
        let app_id = if app_id.chars().all(|c| c.is_ascii_digit()) && !app_id.is_empty() { app_id } else { player_app_id()? };
        let mut account = Account { app_id, token, user_id, name: String::new() };
        // um pedido de verdade com o token: confere que ele vale e traz o nome
        let body = api(&account, "playlist/getUserPlaylists", &[("limit", "1".into())])?;
        account.name = [&body["user"]["login"], &body["user"]["display_name"], &body["user"]["name"]]
            .iter()
            .map(|v| text(v))
            .find(|s| !s.is_empty())
            .unwrap_or_else(|| "sua conta".into());
        if account.user_id.is_empty() {
            account.user_id = text(&body["user"]["id"]);
        }
        write_account(&account)?;
        Ok(account)
    })();
    match result {
        Ok(a) => {
            let _ = app.emit("lumna://qobuz", json!({ "connected": true, "name": a.name }));
        }
        Err(err) => {
            let _ = app.emit("lumna://qobuz", json!({ "connected": false, "name": "", "error": err }));
        }
    }
    if let Some(w) = app.get_webview_window("qobuz-login") {
        let _ = w.close();
    }
}

fn query(url: &tauri::Url, key: &str) -> String {
    url.query_pairs().find(|(k, _)| k == key).map(|(_, v)| v.into_owned()).unwrap_or_default()
}

#[tauri::command]
pub async fn qobuz_login(app: AppHandle) -> Result<(), String> {
    if let Some(w) = app.get_webview_window("qobuz-login") {
        let _ = w.set_focus();
        return Ok(());
    }
    let handle = app.clone();
    let url = tauri::Url::parse(&format!("{PLAYER}/login")).map_err(|e| e.to_string())?;
    WebviewWindowBuilder::new(&app, "qobuz-login", WebviewUrl::External(url))
        .title("Entrar no Qobuz")
        .inner_size(460.0, 760.0)
        // o web player recusa navegador que ele não conhece
        .user_agent("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36")
        .initialization_script(LOGIN_SCRIPT)
        .on_navigation(move |url| {
            if url.host_str() != Some(BRIDGE) {
                return true;
            }
            match url.path() {
                "/qobuz" => {
                    let (token, app_id, user) = (query(url, "token"), query(url, "app"), query(url, "user"));
                    let h = handle.clone();
                    std::thread::spawn(move || finish_login(&h, token, app_id, user));
                }
                "/teclado" => {
                    // as teclas vêm pra esta janela, não pro último campo do LUMNA
                    crate::touchkeys::clear_target();
                    let _ = crate::touchkeys::keyboard_show(handle.clone(), true);
                }
                _ => {}
            }
            false
        })
        .build()
        .map_err(|e| e.to_string())?;
    // O web player recusa tela com menos de 1024 px de largura (o celular tem uns 432): com zoom
    // pra trás a página enxerga a janela mais larga, como num PC. Recalcula se a janela mudar.
    if let Some(window) = app.get_webview_window("qobuz-login") {
        fit_width(&window);
        let w = window.clone();
        window.on_window_event(move |e| {
            if matches!(e, tauri::WindowEvent::Resized(_) | tauri::WindowEvent::ScaleFactorChanged { .. }) {
                fit_width(&w);
            }
        });
    }
    Ok(())
}

/// A largura que o web player quer ver (com folga sobre os 1024).
const PLAYER_WIDTH: f64 = 1100.0;

/// O zoom que faz a janela parecer ter `PLAYER_WIDTH` de largura (1 se já tem).
fn zoom_for(logical_width: f64) -> f64 {
    if logical_width <= 0.0 { 1.0 } else { (logical_width / PLAYER_WIDTH).clamp(0.3, 1.0) }
}

fn fit_width(window: &tauri::WebviewWindow) {
    let (Ok(size), Ok(scale)) = (window.inner_size(), window.scale_factor()) else { return };
    let _ = window.set_zoom(zoom_for(size.width as f64 / scale));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn faixa_do_qobuz() {
        let t = to_track(&json!({
            "id": 12345, "title": "Yellow", "version": "Remastered", "duration": 266, "track_number": 5, "media_number": 1,
            "performer": { "name": "Coldplay" },
            "album": { "title": "Parachutes", "artist": { "name": "Coldplay" }, "release_date_original": "2000-07-10",
                       "image": { "small": "https://s/x.jpg", "large": "https://l/x.jpg" } }
        }))
        .unwrap();
        assert_eq!(t.path, "qobuz:12345");
        assert_eq!(t.title, "Yellow (Remastered)");
        assert_eq!((t.artist.as_str(), t.album.as_str(), t.track, t.year), ("Coldplay", "Parachutes", 5, 2000));
        assert_eq!(t.cover.as_deref(), Some("https://l/x.jpg"));
        assert!(to_track(&json!({ "title": "sem id" })).is_none());
    }

    #[test]
    fn zoom_da_janela_de_login() {
        assert_eq!(zoom_for(1400.0), 1.0);
        assert!((zoom_for(432.0) - 432.0 / 1100.0).abs() < 1e-9);
        assert_eq!(zoom_for(200.0), 0.3);
        assert_eq!(zoom_for(0.0), 1.0);
    }

    #[test]
    fn playlist_do_qobuz() {
        let p = to_playlist(&json!({ "id": 99, "name": "Viagens", "tracks_count": 42, "duration": 9000, "images300": ["https://c/1.jpg"], "owner": { "name": "eric" } }));
        assert_eq!((p.id.as_str(), p.name.as_str(), p.count, p.cover.as_deref()), ("99", "Viagens", 42, Some("https://c/1.jpg")));
    }

    /// Com internet: `cargo test app_id_do_player -- --ignored`
    #[test]
    #[ignore]
    fn app_id_do_player() {
        let id = player_app_id().unwrap();
        println!("app id: {id}");
        assert!(id.len() >= 6);
    }

    /// Com internet: a API responde com o app id (sem login: busca no catálogo)
    #[test]
    #[ignore]
    fn api_sem_login() {
        let a = Account { app_id: player_app_id().unwrap(), ..Account::default() };
        match api(&a, "track/search", &[("query", "coldplay yellow".into()), ("limit", "2".into())]) {
            Ok(body) => {
                let t: Vec<Track> = body["tracks"]["items"].as_array().unwrap().iter().filter_map(to_track).collect();
                println!("{t:#?}");
            }
            Err(e) => println!("erro (esperado se a busca exigir login): {e}"),
        }
    }

    #[test]
    fn script_do_login_nao_vaza_pra_outro_lugar() {
        // o script só fala pelo endereço de mentira, que o LUMNA cancela
        assert!(LOGIN_SCRIPT.contains("https://lumna.invalid/qobuz?token="));
        assert!(!LOGIN_SCRIPT.contains("__TAURI"));
    }
}
