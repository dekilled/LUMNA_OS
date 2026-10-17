//! Streaming da Mídia: o Firefox da TV, com uma janela por serviço em cada monitor.
//!
//! - Um Firefox só (perfil e app_id `lumna-tv`, separado do Firefox do dia a dia), em modo
//!   quiosque, controlado pelo WebDriver BiDi numa porta local: o LUMNA abre as janelas, navega,
//!   lê o endereço da página e pausa o vídeo. Como é um navegador só, o login vale em todas as telas.
//! - Cada monitor tem até 2 serviços vivos: o da frente e um estacionado (`special:lumna-tv`, fora
//!   da tela e pausado). Voltar a ele é instantâneo; abrir um terceiro fecha o usado há mais tempo.
//! - Ao sair de um serviço (Início ou troca de serviço) a página vira um "Recente": endereço,
//!   título e um print da tela como miniatura, para retomar depois.

use std::collections::{HashMap, HashSet};
use std::net::{SocketAddr, TcpStream};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use lumna_core::{Compositor, Window};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tungstenite::{Message, WebSocket};

use crate::media::{self, SERVICES};

/// app_id das janelas do Firefox da TV (`firefox --name`).
pub const APP_ID: &str = "lumna-tv";
/// Workspace escondido onde ficam os serviços estacionados.
pub const PARKING: &str = "special:lumna-tv";
const PORT: u16 = 9227;
const LIVE_PER_MONITOR: usize = 2;
const RECENTS_MAX: usize = 12;
/// Print tirado ao abrir a barra de serviços: vale como miniatura por este tempo.
const SHOT_FRESH: Duration = Duration::from_secs(120);

fn now() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs())
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_default()
}

fn data_dir() -> PathBuf {
    std::env::var_os("XDG_DATA_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".local/share")).join("lumna")
}

fn runtime_dir() -> PathBuf {
    std::env::var_os("XDG_RUNTIME_DIR").map(PathBuf::from).unwrap_or_else(std::env::temp_dir).join("lumna")
}

/// Pasta das miniaturas dos Recentes (o webview só lê pastas liberadas).
pub fn thumbs_dir() -> PathBuf {
    media::thumbs_dir().join("tv")
}

fn media_workspace(monitor: &str) -> String {
    format!("media-{monitor}")
}

// ---- WebDriver BiDi ----

struct Bidi {
    ws: WebSocket<TcpStream>,
    next: u64,
}

impl Bidi {
    fn connect() -> Result<Bidi, String> {
        let addr = SocketAddr::from(([127, 0, 0, 1], PORT));
        let stream = TcpStream::connect_timeout(&addr, Duration::from_millis(400)).map_err(|e| e.to_string())?;
        stream.set_read_timeout(Some(Duration::from_secs(20))).map_err(|e| e.to_string())?;
        let (ws, _) = tungstenite::client(format!("ws://127.0.0.1:{PORT}/session"), stream).map_err(|e| e.to_string())?;
        let mut bidi = Bidi { ws, next: 0 };
        bidi.call("session.new", json!({ "capabilities": {} }))?;
        Ok(bidi)
    }

    fn call(&mut self, method: &str, params: Value) -> Result<Value, String> {
        self.next += 1;
        let id = self.next;
        let request = json!({ "id": id, "method": method, "params": params }).to_string();
        self.ws.send(Message::text(request)).map_err(|e| format!("{method}: {e}"))?;
        loop {
            let message = self.ws.read().map_err(|e| format!("{method}: {e}"))?;
            let Message::Text(text) = message else { continue };
            let reply: Value = serde_json::from_str(text.as_str()).map_err(|e| e.to_string())?;
            if reply["id"].as_u64() != Some(id) {
                continue; // eventos
            }
            if reply["type"] == "error" {
                return Err(format!("{method}: {} {}", reply["error"].as_str().unwrap_or(""), reply["message"].as_str().unwrap_or("")));
            }
            return Ok(reply["result"].clone());
        }
    }

    fn url(&mut self, context: &str) -> Result<String, String> {
        let tree = self.call("browsingContext.getTree", json!({ "root": context, "maxDepth": 0 }))?;
        Ok(tree["contexts"][0]["url"].as_str().unwrap_or_default().to_string())
    }

    fn navigate(&mut self, context: &str, url: &str) -> Result<(), String> {
        self.call("browsingContext.navigate", json!({ "context": context, "url": url, "wait": "none" })).map(|_| ())
    }

    /// Espera a página ter conteúdo (`interactive` ou `complete`), no máximo `limit`. Vale para a
    /// cortina da Mídia não sair em cima de uma página branca.
    fn wait_ready(&mut self, context: &str, limit: Duration) {
        let deadline = Instant::now() + limit;
        // Logo depois do navigate, o documento ainda é o about:blank anterior (já "complete").
        std::thread::sleep(Duration::from_millis(300));
        while Instant::now() < deadline {
            let state = self
                .call(
                    "script.evaluate",
                    json!({ "expression": "location.protocol + ' ' + document.readyState", "target": { "context": context }, "awaitPromise": false }),
                )
                .ok()
                .and_then(|r| r["result"]["value"].as_str().map(String::from))
                .unwrap_or_default();
            if state.starts_with("http") && !state.ends_with("loading") {
                // Mais um instante para a primeira pintura.
                std::thread::sleep(Duration::from_millis(400));
                return;
            }
            std::thread::sleep(Duration::from_millis(200));
        }
    }

    fn eval(&mut self, context: &str, expression: &str) -> Result<(), String> {
        self.call(
            "script.evaluate",
            json!({ "expression": expression, "target": { "context": context }, "awaitPromise": false }),
        )
        .map(|_| ())
    }
}

/// Pausa os vídeos tocando (e marca quais eram, para voltar a tocar ao retomar).
const PAUSE: &str = "document.querySelectorAll('video').forEach(v => { if (!v.paused) { v.pause(); v.dataset.lumnaPaused = '1'; } })";
const PLAY: &str =
    "document.querySelectorAll('video[data-lumna-paused]').forEach(v => { delete v.dataset.lumnaPaused; v.play().catch(() => {}); })";

/// Preferências do perfil: DRM (Widevine) ligado, sem telas de primeiro uso, sem restaurar sessão
/// e sem as preferências de "navegador de teste" que o controle remoto ligaria.
fn profile() -> Result<PathBuf, String> {
    let dir = data_dir().join("tv-profile");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let prefs = [
        ("media.eme.enabled", "true"),
        ("media.gmp-widevinecdm.enabled", "true"),
        ("media.gmp-widevinecdm.visible", "true"),
        ("media.hardware-video-decoding.enabled", "true"),
        ("media.autoplay.default", "0"),
        ("browser.shell.checkDefaultBrowser", "false"),
        ("browser.aboutwelcome.enabled", "false"),
        ("browser.startup.page", "0"),
        ("browser.startup.homepage_override.mstone", "\"ignore\""),
        ("startup.homepage_welcome_url", "\"\""),
        ("datareporting.policy.dataSubmissionPolicyBypassNotification", "true"),
        ("datareporting.policy.firstRunURL", "\"\""),
        ("browser.sessionstore.resume_from_crash", "false"),
        ("browser.tabs.warnOnClose", "false"),
        ("browser.warnOnQuit", "false"),
        ("remote.prefs.recommended", "false"),
    ];
    let user_js: String = prefs.iter().map(|(k, v)| format!("user_pref(\"{k}\", {v});\n")).collect();
    std::fs::write(dir.join("user.js"), user_js).map_err(|e| e.to_string())?;
    Ok(dir)
}

fn launch_command(profile: &Path) -> String {
    format!(
        "firefox --name {APP_ID} --kiosk --no-remote --profile '{}' --remote-debugging-port {PORT} about:blank",
        profile.to_string_lossy().replace('\'', r"'\''")
    )
}

// ---- recentes ----

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Recent {
    pub id: String,
    pub service: String,
    pub title: String,
    pub url: String,
    pub thumb: Option<String>,
    /// Quando saiu da página (segundos desde 1970).
    pub at: u64,
}

fn recents_path() -> PathBuf {
    data_dir().join("media-recents.json")
}

pub fn recents() -> Vec<Recent> {
    std::fs::read_to_string(recents_path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

/// Põe a página no topo dos Recentes (a mesma página não se repete) e apaga as miniaturas que saem.
fn remember(list: &mut Vec<Recent>, recent: Recent) {
    for old in list.iter().filter(|r| r.url == recent.url && r.thumb != recent.thumb) {
        if let Some(thumb) = &old.thumb {
            let _ = std::fs::remove_file(thumb);
        }
    }
    list.retain(|r| r.url != recent.url);
    list.insert(0, recent);
    for old in list.drain(RECENTS_MAX.min(list.len())..) {
        if let Some(thumb) = old.thumb {
            let _ = std::fs::remove_file(thumb);
        }
    }
}

fn save_recents(list: &[Recent]) {
    let _ = std::fs::create_dir_all(data_dir());
    if let Ok(text) = serde_json::to_string_pretty(list) {
        let _ = std::fs::write(recents_path(), text);
    }
}

/// "Paisagens do mundo - YouTube — Mozilla Firefox" → "Paisagens do mundo".
pub fn clean_title(title: &str, service: &str) -> String {
    let mut text = title.trim();
    for suffix in [" — Mozilla Firefox", " - Mozilla Firefox", "Mozilla Firefox"] {
        text = text.strip_suffix(suffix).unwrap_or(text).trim();
    }
    if let Some(name) = SERVICES.iter().find(|s| s.id == service).map(|s| s.name) {
        for sep in [" - ", " | ", " – ", " — ", ": "] {
            let suffix = format!("{sep}{name}");
            if text.len() > suffix.len() && text.to_lowercase().ends_with(&suffix.to_lowercase()) {
                text = text[..text.len() - suffix.len()].trim();
            }
            let prefix = format!("{name}{}", sep.trim_end());
            if text.len() > prefix.len() + 1 && text.to_lowercase().starts_with(&prefix.to_lowercase()) {
                text = text[prefix.len()..].trim();
            }
        }
        if text.is_empty() {
            return name.to_string();
        }
    }
    text.to_string()
}

// ---- estado ----

#[derive(Debug, Clone, Serialize, Deserialize)]
struct Live {
    monitor: String,
    service: String,
    context: String,
    address: String,
    /// Última vez na frente (para fechar o mais antigo).
    used: u64,
}

#[derive(Default)]
struct Inner {
    bidi: Option<Bidi>,
    live: Vec<Live>,
    /// Print de cada monitor tirado ao abrir a barra de serviços.
    shots: HashMap<String, (PathBuf, Instant)>,
}

/// O que a tela da Mídia e a barra mostram.
#[derive(Debug, Clone, Serialize)]
pub struct TvState {
    /// Serviço na frente neste monitor.
    pub playing: Option<String>,
    /// Serviços vivos (na frente ou estacionados) neste monitor.
    pub live: Vec<String>,
    pub recents: Vec<Recent>,
}

#[derive(Default)]
pub struct Tv {
    inner: Mutex<Inner>,
    /// Monitor em que a barra de serviços está aberta.
    bar: Mutex<Option<String>>,
}

fn live_path() -> PathBuf {
    runtime_dir().join("tv.json")
}

fn tv_windows(compositor: &dyn Compositor) -> Result<Vec<Window>, String> {
    Ok(compositor.windows()?.into_iter().filter(|w| w.class == APP_ID).collect())
}

impl Tv {
    /// Retoma os serviços vivos de antes (o shell pode reiniciar com o Firefox aberto).
    pub fn load() -> Tv {
        let live = std::fs::read_to_string(live_path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default();
        Tv { inner: Mutex::new(Inner { live, ..Inner::default() }), bar: Mutex::new(None) }
    }

    pub fn bar(&self) -> Option<String> {
        self.bar.lock().unwrap().clone()
    }

    pub fn set_bar(&self, monitor: Option<String>) {
        *self.bar.lock().unwrap() = monitor;
    }

    /// Serviço na frente do monitor (a janela dele está no workspace da Mídia).
    pub fn playing(&self, compositor: &dyn Compositor, monitor: &str) -> Option<String> {
        let windows = tv_windows(compositor).ok()?;
        let inner = self.inner.lock().unwrap();
        front(&inner.live, &windows, monitor).map(|l| l.service.clone())
    }

    pub fn state(&self, compositor: &dyn Compositor, monitor: &str) -> TvState {
        let windows = tv_windows(compositor).unwrap_or_default();
        let mut inner = self.inner.lock().unwrap();
        prune(&mut inner.live, &windows);
        TvState {
            playing: front(&inner.live, &windows, monitor).map(|l| l.service.clone()),
            live: inner.live.iter().filter(|l| l.monitor == monitor).map(|l| l.service.clone()).collect(),
            recents: recents(),
        }
    }

    /// Print do monitor agora (antes de a barra aparecer por cima), para a miniatura do Recente.
    pub fn snapshot(&self, monitor: &str) {
        let path = runtime_dir().join(format!("tv-shot-{monitor}.jpg"));
        if grab(monitor, &path) {
            self.inner.lock().unwrap().shots.insert(monitor.to_string(), (path, Instant::now()));
        }
    }

    /// Abre o serviço neste monitor: volta para a janela viva dele ou abre uma nova. Com `url`,
    /// vai para essa página (retomar um Recente).
    pub fn open(&self, compositor: &dyn Compositor, monitor: &str, service: &str, url: Option<&str>) -> Result<(), String> {
        let def = SERVICES.iter().find(|s| s.id == service).ok_or_else(|| format!("serviço {service} não existe"))?;
        let mut inner = self.inner.lock().unwrap();
        let windows = tv_windows(compositor)?;
        prune(&mut inner.live, &windows);
        // O que estava na frente sai (vira Recente e fica estacionado).
        let leaving: Vec<Live> =
            front(&inner.live, &windows, monitor).filter(|l| l.service != service).cloned().into_iter().collect();
        for live in &leaving {
            leave(&mut inner, compositor, live, &windows);
        }
        let workspace = format!("name:{}", media_workspace(monitor));

        if let Some(index) = inner.live.iter().position(|l| l.monitor == monitor && l.service == service) {
            let live = inner.live[index].clone();
            if let Some(url) = url {
                let current = with_bidi(&mut inner, compositor, |b| b.url(&live.context)).unwrap_or_default();
                if current != url {
                    with_bidi(&mut inner, compositor, |b| b.navigate(&live.context, url))?;
                    if let Some(bidi) = inner.bidi.as_mut() {
                        bidi.wait_ready(&live.context, Duration::from_secs(8));
                    }
                }
            }
            bring(compositor, &live.address, &workspace)?;
            let _ = with_bidi(&mut inner, compositor, |b| b.eval(&live.context, PLAY));
            inner.live[index].used = now();
        } else {
            let (context, address) = new_window(&mut inner, compositor)?;
            with_bidi(&mut inner, compositor, |b| b.navigate(&context, url.unwrap_or(def.url)))?;
            bring(compositor, &address, &workspace)?;
            if let Some(bidi) = inner.bidi.as_mut() {
                bidi.wait_ready(&context, Duration::from_secs(8));
            }
            inner.live.push(Live { monitor: monitor.into(), service: service.into(), context, address, used: now() });
            // No máximo 2 por monitor: fecha o usado há mais tempo.
            while inner.live.iter().filter(|l| l.monitor == monitor).count() > LIVE_PER_MONITOR {
                let Some(oldest) = inner
                    .live
                    .iter()
                    .enumerate()
                    .filter(|(_, l)| l.monitor == monitor && l.service != service)
                    .min_by_key(|(_, l)| l.used)
                    .map(|(i, _)| i)
                else {
                    break;
                };
                let old = inner.live.remove(oldest);
                if with_bidi(&mut inner, compositor, |b| b.call("browsingContext.close", json!({ "context": old.context })))
                    .is_err()
                {
                    let _ = compositor.close_window(&old.address);
                }
            }
        }
        save_live(&inner.live);
        Ok(())
    }

    /// Volta para o Início: o serviço da frente vira Recente e fica estacionado. Devolve se havia
    /// um serviço na frente.
    pub fn home(&self, compositor: &dyn Compositor, monitor: &str) -> Result<bool, String> {
        let mut inner = self.inner.lock().unwrap();
        let windows = tv_windows(compositor)?;
        prune(&mut inner.live, &windows);
        let Some(live) = front(&inner.live, &windows, monitor).cloned() else { return Ok(false) };
        leave(&mut inner, compositor, &live, &windows);
        save_live(&inner.live);
        Ok(true)
    }

    /// Tira um Recente da lista.
    pub fn forget(&self, id: &str) {
        let mut list = recents();
        if let Some(thumb) = list.iter().find(|r| r.id == id).and_then(|r| r.thumb.clone()) {
            let _ = std::fs::remove_file(thumb);
        }
        list.retain(|r| r.id != id);
        save_recents(&list);
    }
}

fn save_live(live: &[Live]) {
    let _ = std::fs::create_dir_all(runtime_dir());
    if let Ok(text) = serde_json::to_string(live) {
        let _ = std::fs::write(live_path(), text);
    }
}

/// Esquece as janelas que não existem mais (fechadas pelo usuário, Firefox fechado).
fn prune(live: &mut Vec<Live>, windows: &[Window]) {
    live.retain(|l| windows.iter().any(|w| w.address == l.address));
}

fn front<'a>(live: &'a [Live], windows: &[Window], monitor: &str) -> Option<&'a Live> {
    let workspace = media_workspace(monitor);
    live.iter()
        .filter(|l| l.monitor == monitor)
        .filter(|l| windows.iter().any(|w| w.address == l.address && w.workspace_name == workspace))
        .max_by_key(|l| l.used)
}

/// Chama o Firefox da TV; se a conexão caiu (ou o Firefox fechou), conecta de novo uma vez.
fn with_bidi<T>(inner: &mut Inner, compositor: &dyn Compositor, f: impl Fn(&mut Bidi) -> Result<T, String>) -> Result<T, String> {
    if let Some(bidi) = inner.bidi.as_mut() {
        match f(bidi) {
            Ok(value) => return Ok(value),
            Err(err) => eprintln!("lumna-shell: Firefox da TV: {err} (reconectando)"),
        }
    }
    inner.bidi = None;
    ensure(inner, compositor)?;
    f(inner.bidi.as_mut().expect("conectado"))
}

/// Conecta no Firefox da TV, abrindo ele se não estiver aberto. Devolve se acabou de abrir.
fn ensure(inner: &mut Inner, compositor: &dyn Compositor) -> Result<bool, String> {
    if inner.bidi.is_some() {
        return Ok(false);
    }
    if let Ok(bidi) = Bidi::connect() {
        inner.bidi = Some(bidi);
        return Ok(false);
    }
    compositor.exec(&launch_command(&profile()?))?;
    let deadline = Instant::now() + Duration::from_secs(25);
    loop {
        std::thread::sleep(Duration::from_millis(300));
        match Bidi::connect() {
            Ok(bidi) => {
                inner.bidi = Some(bidi);
                return Ok(true);
            }
            Err(err) if Instant::now() > deadline => return Err(format!("o Firefox da TV não respondeu: {err}")),
            Err(_) => {}
        }
    }
}

/// Uma janela nova do Firefox da TV: o contexto (BiDi) e o endereço dela no compositor.
fn new_window(inner: &mut Inner, compositor: &dyn Compositor) -> Result<(String, String), String> {
    let before: HashSet<String> = tv_windows(compositor)?.into_iter().map(|w| w.address).collect();
    let fresh = ensure(inner, compositor)?;
    let bidi = inner.bidi.as_mut().expect("conectado");
    let known: HashSet<&str> = inner.live.iter().map(|l| l.context.as_str()).collect();
    // Recém-aberto, o Firefox já vem com uma janela: usa ela.
    let context = if fresh {
        let tree = bidi.call("browsingContext.getTree", json!({ "maxDepth": 0 }))?;
        tree["contexts"]
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(|c| c["context"].as_str())
            .find(|c| !known.contains(c))
            .map(String::from)
    } else {
        None
    };
    let context = match context {
        Some(context) => context,
        None => bidi.call("browsingContext.create", json!({ "type": "window" }))?["context"]
            .as_str()
            .ok_or("o Firefox não criou a janela")?
            .to_string(),
    };
    let deadline = Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(window) = tv_windows(compositor)?.into_iter().find(|w| !before.contains(&w.address)) {
            return Ok((context, window.address));
        }
        if Instant::now() > deadline {
            return Err("a janela do Firefox da TV não apareceu".into());
        }
        std::thread::sleep(Duration::from_millis(120));
    }
}

/// Traz a janela para a tela da Mídia, em foco e em tela cheia.
fn bring(compositor: &dyn Compositor, address: &str, workspace: &str) -> Result<(), String> {
    compositor.move_window(address, workspace)?;
    compositor.focus_window(address)?;
    // O quiosque pede tela cheia sozinho logo que a janela abre; só completa se não vier.
    let deadline = Instant::now() + Duration::from_millis(1500);
    loop {
        let full = compositor.windows()?.into_iter().find(|w| w.address == address).is_none_or(|w| w.fullscreen != 0);
        if full {
            return Ok(());
        }
        if Instant::now() > deadline {
            return compositor.fullscreen_window(address);
        }
        std::thread::sleep(Duration::from_millis(100));
    }
}

/// Sai do serviço: guarda a página nos Recentes, pausa e estaciona a janela.
fn leave(inner: &mut Inner, compositor: &dyn Compositor, live: &Live, windows: &[Window]) {
    let url = with_bidi(inner, compositor, |b| b.url(&live.context)).unwrap_or_default();
    if url.starts_with("http") {
        let at = now();
        let id = format!("{:x}", md5::compute(url.as_bytes()));
        let title = windows.iter().find(|w| w.address == live.address).map(|w| w.title.as_str()).unwrap_or_default();
        let thumb = thumbs_dir().join(format!("{id}-{at}.jpg"));
        let _ = std::fs::create_dir_all(thumbs_dir());
        let shot = inner.shots.remove(&live.monitor).filter(|(_, when)| when.elapsed() < SHOT_FRESH);
        let saved = match shot {
            Some((path, _)) => std::fs::rename(&path, &thumb).or_else(|_| std::fs::copy(&path, &thumb).map(|_| ())).is_ok(),
            None => grab(&live.monitor, &thumb),
        };
        let mut list = recents();
        remember(
            &mut list,
            Recent {
                id,
                service: live.service.clone(),
                title: clean_title(title, &live.service),
                url,
                thumb: saved.then(|| thumb.to_string_lossy().into()),
                at,
            },
        );
        save_recents(&list);
    }
    let _ = with_bidi(inner, compositor, |b| b.eval(&live.context, PAUSE));
    if let Err(err) = compositor.move_window(&live.address, PARKING) {
        eprintln!("lumna-shell: estacionar {}: {err}", live.service);
    }
}

/// Print de um monitor, pequeno (miniatura).
fn grab(monitor: &str, path: &Path) -> bool {
    std::process::Command::new("grim")
        .args(["-o", monitor, "-s", "0.35", "-t", "jpeg", "-q", "80"])
        .arg(path)
        .status()
        .is_ok_and(|s| s.success())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn recent(url: &str, thumb: Option<&str>) -> Recent {
        Recent { id: url.into(), service: "youtube".into(), title: url.into(), url: url.into(), thumb: thumb.map(String::from), at: 0 }
    }

    #[test]
    fn titles_lose_the_browser_and_service_names() {
        assert_eq!(clean_title("Paisagens do mundo - YouTube — Mozilla Firefox", "youtube"), "Paisagens do mundo");
        assert_eq!(clean_title("Netflix — Mozilla Firefox", "netflix"), "Netflix");
        assert_eq!(clean_title("Mozilla Firefox", "disney"), "Disney+");
        assert_eq!(clean_title("Série | Prime Video", "prime"), "Série");
        assert_eq!(clean_title("Prime Video: Watch movies — Mozilla Firefox", "prime"), "Watch movies");
    }

    #[test]
    fn recents_keep_one_entry_per_page_newest_first() {
        let mut list = vec![recent("a", None), recent("b", None)];
        remember(&mut list, recent("b", None));
        assert_eq!(list.iter().map(|r| r.url.as_str()).collect::<Vec<_>>(), ["b", "a"]);
        for i in 0..20 {
            remember(&mut list, recent(&format!("u{i}"), None));
        }
        assert_eq!(list.len(), RECENTS_MAX);
        assert_eq!(list[0].url, "u19");
    }

    #[test]
    fn the_front_service_is_the_one_on_the_media_workspace() {
        let window = |address: &str, ws: &str| Window {
            address: address.into(),
            class: APP_ID.into(),
            title: String::new(),
            workspace: 0,
            workspace_name: ws.into(),
            monitor: 0,
            fullscreen: 2,
            x: 0,
            y: 0,
            width: 1920,
            height: 1080,
            floating: true,
            focus_history: 0,
            pid: 0,
        };
        let live = vec![
            Live { monitor: "DP-1".into(), service: "youtube".into(), context: "a".into(), address: "0x1".into(), used: 5 },
            Live { monitor: "DP-1".into(), service: "netflix".into(), context: "b".into(), address: "0x2".into(), used: 9 },
            Live { monitor: "HDMI-A-1".into(), service: "disney".into(), context: "c".into(), address: "0x3".into(), used: 1 },
        ];
        let windows = [window("0x1", "media-DP-1"), window("0x2", PARKING), window("0x3", "media-HDMI-A-1")];
        assert_eq!(front(&live, &windows, "DP-1").map(|l| l.service.as_str()), Some("youtube"));
        assert_eq!(front(&live, &windows, "HDMI-A-1").map(|l| l.service.as_str()), Some("disney"));
        let mut pruned = live.clone();
        prune(&mut pruned, &windows[..2]);
        assert_eq!(pruned.len(), 2);
    }

    #[test]
    fn firefox_runs_apart_with_remote_control() {
        let cmd = launch_command(Path::new("/home/a/.local/share/lumna/tv-profile"));
        assert!(cmd.starts_with("firefox --name lumna-tv --kiosk --no-remote"), "{cmd}");
        assert!(cmd.contains("--remote-debugging-port 9227"));
    }
}
