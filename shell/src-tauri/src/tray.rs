//! Bandeja do sistema (StatusNotifierItem): os ícones que apps como Spotify, Discord e Steam deixam
//! rodando em segundo plano, na topbar.
//!
//! O LUMNA é o `org.kde.StatusNotifierWatcher` da sessão: cada app registra o seu item, e o shell lê o
//! ícone, o título e o menu (`com.canonical.dbusmenu`) dele pelo D-Bus. Clique ativa o app; o botão
//! direito abre o menu do app numa camada própria (`traymenu`).

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};

use base64::Engine;
use futures_util::StreamExt;
use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};
use zbus::message::Header;
use zbus::object_server::SignalEmitter;
use zbus::zvariant::{OwnedValue, Value};

use crate::{layer, MonitorPositions};

const WATCHER: &str = "org.kde.StatusNotifierWatcher";
const WATCHER_PATH: &str = "/StatusNotifierWatcher";
const ITEM: &str = "org.kde.StatusNotifierItem";
const MENU: &str = "com.canonical.dbusmenu";

#[derive(Debug, Clone, Serialize)]
pub struct TrayItem {
    /// `nome.no.barramento/caminho/do/objeto`.
    pub key: String,
    pub title: String,
    /// Data URL do ícone, se deu para achar.
    pub icon: Option<String>,
    /// `Active`, `Passive` (o app pede para ficar escondido) ou `NeedsAttention`.
    pub status: String,
    /// O app tem menu (dbusmenu).
    pub menu: bool,
    /// O clique principal só abre o menu.
    pub item_is_menu: bool,
    #[serde(skip)]
    bus: String,
    #[serde(skip)]
    path: String,
    #[serde(skip)]
    owner: String,
    #[serde(skip)]
    menu_path: Option<String>,
}

static ITEMS: Mutex<Vec<TrayItem>> = Mutex::new(Vec::new());
static CONNECTION: OnceLock<zbus::Connection> = OnceLock::new();
static APP: OnceLock<AppHandle> = OnceLock::new();

/// `":1.42"` + `"/org/ayatana/NotificationItem/spotify"` ou `"org.kde.StatusNotifierItem-12-1"`.
pub fn split_service(service: &str, sender: &str) -> (String, String) {
    if service.starts_with('/') {
        (sender.to_string(), service.to_string())
    } else if let Some((bus, path)) = service.split_once('/') {
        (bus.to_string(), format!("/{path}"))
    } else {
        (service.to_string(), "/StatusNotifierItem".to_string())
    }
}

struct Watcher;

#[zbus::interface(name = "org.kde.StatusNotifierWatcher")]
impl Watcher {
    async fn register_status_notifier_item(
        &self,
        service: &str,
        #[zbus(header)] header: Header<'_>,
        #[zbus(signal_emitter)] emitter: SignalEmitter<'_>,
    ) {
        let sender = header.sender().map(|s| s.to_string()).unwrap_or_default();
        let (bus, path) = split_service(service, &sender);
        let key = format!("{bus}{path}");
        let _ = Watcher::status_notifier_item_registered(&emitter, &key).await;
        tauri::async_runtime::spawn(async move { refresh_item(bus, path, sender).await });
    }

    fn register_status_notifier_host(&self, _service: &str) {}

    #[zbus(property)]
    fn registered_status_notifier_items(&self) -> Vec<String> {
        ITEMS.lock().unwrap().iter().map(|i| i.key.clone()).collect()
    }

    #[zbus(property)]
    fn is_status_notifier_host_registered(&self) -> bool {
        true
    }

    #[zbus(property)]
    fn protocol_version(&self) -> i32 {
        0
    }

    #[zbus(signal)]
    async fn status_notifier_item_registered(emitter: &SignalEmitter<'_>, service: &str) -> zbus::Result<()>;

    #[zbus(signal)]
    async fn status_notifier_item_unregistered(emitter: &SignalEmitter<'_>, service: &str) -> zbus::Result<()>;

    #[zbus(signal)]
    async fn status_notifier_host_registered(emitter: &SignalEmitter<'_>) -> zbus::Result<()>;
}

fn changed() {
    if let Some(app) = APP.get() {
        let items: Vec<TrayItem> = ITEMS.lock().unwrap().clone();
        let _ = app.emit("lumna://tray", items);
    }
}

fn text(props: &HashMap<String, OwnedValue>, key: &str) -> String {
    props.get(key).and_then(|v| String::try_from(v.clone()).ok()).unwrap_or_default()
}

/// Lê (ou relê) as propriedades de um item e atualiza a lista.
async fn refresh_item(bus: String, path: String, sender: String) {
    let Some(conn) = CONNECTION.get().cloned() else { return };
    let Ok(props) = zbus::fdo::PropertiesProxy::builder(&conn)
        .destination(bus.as_str())
        .and_then(|b| b.path(path.as_str()))
        .map(|b| b.cache_properties(zbus::proxy::CacheProperties::No))
    else {
        return;
    };
    let Ok(props) = props.build().await else { return };
    let Ok(iface) = zbus::names::InterfaceName::try_from(ITEM) else { return };
    let Ok(all) = props.get_all(iface).await else { return };
    let all: HashMap<String, OwnedValue> = all.into_iter().map(|(k, v)| (k.to_string(), v)).collect();

    let owner = if bus.starts_with(':') { bus.clone() } else { owner_of(&conn, &bus).await.unwrap_or(sender) };
    let menu_path = all
        .get("Menu")
        .and_then(|v| zbus::zvariant::OwnedObjectPath::try_from(v.clone()).ok())
        .map(|p| p.to_string())
        .filter(|p| p != "/");
    let mut title = text(&all, "Title");
    if title.is_empty() {
        title = text(&all, "Id");
    }
    let icon = icon_for(&conn, &all, &owner).await;
    let item = TrayItem {
        key: format!("{bus}{path}"),
        title,
        icon,
        status: Some(text(&all, "Status")).filter(|s| !s.is_empty()).unwrap_or_else(|| "Active".into()),
        menu: menu_path.is_some(),
        item_is_menu: all.get("ItemIsMenu").and_then(|v| bool::try_from(v).ok()).unwrap_or(false),
        bus,
        path,
        owner,
        menu_path,
    };
    {
        let mut items = ITEMS.lock().unwrap();
        match items.iter_mut().find(|i| i.key == item.key) {
            Some(existing) => *existing = item,
            None => items.push(item),
        }
    }
    changed();
}

async fn owner_of(conn: &zbus::Connection, name: &str) -> Option<String> {
    let dbus = zbus::fdo::DBusProxy::new(conn).await.ok()?;
    let name = zbus::names::BusName::try_from(name).ok()?;
    dbus.get_name_owner(name).await.ok().map(|o| o.to_string())
}

async fn pid_of(conn: &zbus::Connection, owner: &str) -> Option<u32> {
    let dbus = zbus::fdo::DBusProxy::new(conn).await.ok()?;
    let name = zbus::names::BusName::try_from(owner).ok()?;
    dbus.get_connection_unix_process_id(name).await.ok()
}

// ---- ícones ----

/// O ícone do item: a imagem que ele manda (IconPixmap) ou o nome procurado nos temas.
async fn icon_for(conn: &zbus::Connection, props: &HashMap<String, OwnedValue>, owner: &str) -> Option<String> {
    let name = text(props, "IconName");
    let theme_path = text(props, "IconThemePath");
    if !name.is_empty() {
        // Apps do Flatpak mandam caminhos de dentro do sandbox: dá para chegar neles por /proc/PID/root.
        let root = pid_of(conn, owner).await.map(|pid| PathBuf::from(format!("/proc/{pid}/root")));
        if let Some(found) = find_icon(&name, &theme_path, root.as_deref()) {
            return file_data_url(&found);
        }
    }
    props.get("IconPixmap").and_then(pixmap_data_url)
}

fn file_data_url(path: &Path) -> Option<String> {
    let bytes = std::fs::read(path).ok()?;
    let mime = if path.extension().is_some_and(|e| e == "svg") { "image/svg+xml" } else { "image/png" };
    Some(format!("data:{mime};base64,{}", base64::engine::general_purpose::STANDARD.encode(bytes)))
}

/// Procura `nome.png`/`nome.svg` na pasta do próprio app, nos temas e em /usr/share/pixmaps.
pub fn find_icon(name: &str, theme_path: &str, root: Option<&Path>) -> Option<PathBuf> {
    if name.starts_with('/') {
        let direct = PathBuf::from(name);
        if direct.is_file() {
            return Some(direct);
        }
        return root.map(|r| r.join(name.trim_start_matches('/'))).filter(|p| p.is_file());
    }
    let mut bases: Vec<PathBuf> = Vec::new();
    if !theme_path.is_empty() {
        bases.push(PathBuf::from(theme_path));
        if let Some(root) = root {
            bases.push(root.join(theme_path.trim_start_matches('/')));
        }
    }
    let home = std::env::var_os("HOME").map(PathBuf::from);
    if let Some(home) = &home {
        bases.push(home.join(".local/share/icons"));
        bases.push(home.join(".local/share/flatpak/exports/share/icons"));
    }
    bases.push("/var/lib/flatpak/exports/share/icons".into());
    bases.push("/usr/share/icons".into());
    bases.push("/usr/share/pixmaps".into());

    const THEMES: &[&str] = &["", "hicolor", "Adwaita", "breeze", "Papirus"];
    const SIZES: &[&str] = &["", "scalable", "256x256", "128x128", "96x96", "64x64", "48x48", "32x32", "24x24", "22x22", "16x16"];
    const KINDS: &[&str] = &["", "apps", "status", "panel", "devices"];
    for base in &bases {
        for theme in THEMES {
            for size in SIZES {
                for kind in KINDS {
                    for ext in ["svg", "png"] {
                        let candidate = base.join(theme).join(size).join(kind).join(format!("{name}.{ext}"));
                        if candidate.is_file() {
                            return Some(candidate);
                        }
                    }
                }
            }
        }
    }
    None
}

/// IconPixmap: `a(iiay)`, ARGB em ordem de rede. Usa a maior até 64 px e vira PNG.
fn pixmap_data_url(value: &OwnedValue) -> Option<String> {
    let list = <Vec<(i32, i32, Vec<u8>)>>::try_from(value.clone()).ok()?;
    let (w, h, data) = list
        .into_iter()
        .filter(|(w, h, d)| *w > 0 && *h > 0 && d.len() == (*w as usize) * (*h as usize) * 4)
        .min_by_key(|(w, _, _)| (64 - *w).abs())?;
    let png = argb_to_png(w as u32, h as u32, &data)?;
    Some(format!("data:image/png;base64,{}", base64::engine::general_purpose::STANDARD.encode(png)))
}

pub fn argb_to_png(width: u32, height: u32, argb: &[u8]) -> Option<Vec<u8>> {
    let rgba: Vec<u8> = argb.chunks_exact(4).flat_map(|p| [p[1], p[2], p[3], p[0]]).collect();
    let mut out = Vec::new();
    {
        let mut encoder = png::Encoder::new(&mut out, width, height);
        encoder.set_color(png::ColorType::Rgba);
        encoder.set_depth(png::BitDepth::Eight);
        let mut writer = encoder.write_header().ok()?;
        writer.write_image_data(&rgba).ok()?;
    }
    Some(out)
}

// ---- menu (dbusmenu) ----

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct MenuNode {
    pub id: i32,
    pub label: String,
    pub enabled: bool,
    pub separator: bool,
    /// `checkmark` ou `radio`, com `checked`.
    pub toggle: Option<String>,
    pub checked: bool,
    pub children: Vec<MenuNode>,
}

fn unwrap_variant<'a>(value: &'a Value<'a>) -> &'a Value<'a> {
    match value {
        Value::Value(inner) => unwrap_variant(inner),
        other => other,
    }
}

/// Um nó do `GetLayout`: `(i id, a{sv} propriedades, av filhos)` (os filhos vêm em variantes).
pub fn parse_node(value: &Value<'_>) -> Option<MenuNode> {
    let Value::Structure(s) = unwrap_variant(value) else { return None };
    let fields = s.fields();
    let id = match fields.first()? {
        Value::I32(id) => *id,
        _ => return None,
    };
    let mut props: HashMap<String, Value<'_>> = HashMap::new();
    if let Some(Value::Dict(dict)) = fields.get(1) {
        for (k, v) in dict.iter() {
            if let Value::Str(key) = k {
                props.insert(key.to_string(), unwrap_variant(v).clone());
            }
        }
    }
    let children: Vec<Value<'_>> = match fields.get(2) {
        Some(Value::Array(items)) => items.iter().cloned().collect(),
        _ => Vec::new(),
    };
    node_from(id, props, &children)
}

/// A raiz do `GetLayout` chega como estrutura (não variante): as partes já separadas.
fn node_from(id: i32, props: HashMap<String, Value<'_>>, children: &[Value<'_>]) -> Option<MenuNode> {
    let str_prop = |k: &str| match props.get(k) {
        Some(Value::Str(s)) => Some(s.to_string()),
        _ => None,
    };
    let bool_prop = |k: &str, default: bool| match props.get(k) {
        Some(Value::Bool(b)) => *b,
        _ => default,
    };
    if !bool_prop("visible", true) {
        return None;
    }
    let children = children.iter().filter_map(parse_node).collect();
    // "_Abrir" → "Abrir" (o sublinhado marca o atalho de teclado); "__" é um "_" de verdade.
    let label = str_prop("label").unwrap_or_default().replace("__", "\u{0}").replace('_', "").replace('\u{0}', "_");
    Some(MenuNode {
        id,
        label,
        enabled: bool_prop("enabled", true),
        separator: str_prop("type").as_deref() == Some("separator"),
        toggle: str_prop("toggle-type").filter(|t| !t.is_empty()),
        checked: matches!(props.get("toggle-state"), Some(Value::I32(1))),
        children,
    })
}

fn find_item(key: &str) -> Result<TrayItem, String> {
    ITEMS.lock().unwrap().iter().find(|i| i.key == key).cloned().ok_or_else(|| "esse item da bandeja sumiu".into())
}

async fn call(item: &TrayItem, path: &str, iface: &str, method: &str, body: &(impl serde::Serialize + zbus::zvariant::DynamicType)) -> Result<zbus::Message, String> {
    let conn = CONNECTION.get().ok_or("bandeja ainda não iniciou")?;
    conn.call_method(Some(item.bus.as_str()), path, Some(iface), method, body).await.map_err(|e| e.to_string())
}

#[tauri::command]
pub fn tray_items() -> Vec<TrayItem> {
    ITEMS.lock().unwrap().clone()
}

/// Clique principal (`secondary`: botão do meio).
#[tauri::command]
pub async fn tray_activate(key: String, x: i32, y: i32, secondary: bool) -> Result<(), String> {
    let item = find_item(&key)?;
    let method = if secondary { "SecondaryActivate" } else { "Activate" };
    call(&item, &item.path, ITEM, method, &(x, y)).await.map(|_| ())
}

/// O menu do app, pronto para desenhar.
#[tauri::command]
pub async fn tray_menu(key: String) -> Result<Vec<MenuNode>, String> {
    let item = find_item(&key)?;
    let menu = item.menu_path.clone().ok_or("esse app não tem menu")?;
    let _ = call(&item, &menu, MENU, "AboutToShow", &(0i32,)).await;
    let reply = call(&item, &menu, MENU, "GetLayout", &(0i32, -1i32, Vec::<String>::new())).await?;
    let body = reply.body();
    let (_revision, (id, props, children)): (u32, (i32, HashMap<String, OwnedValue>, Vec<OwnedValue>)) =
        body.deserialize().map_err(|e| e.to_string())?;
    let props: HashMap<String, Value<'_>> = props.iter().map(|(k, v)| (k.clone(), unwrap_variant(v).clone())).collect();
    let children: Vec<Value<'_>> = children.iter().map(|v| Value::from(v.clone())).collect();
    Ok(node_from(id, props, &children).map(|n| n.children).unwrap_or_default())
}

#[tauri::command]
pub async fn tray_menu_click(app: AppHandle, key: String, id: i32) -> Result<(), String> {
    let _ = app.run_on_main_thread(|| layer::set_visible("traymenu", false));
    let item = find_item(&key)?;
    let menu = item.menu_path.clone().ok_or("esse app não tem menu")?;
    let stamp = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs() as u32).unwrap_or(0);
    call(&item, &menu, MENU, "Event", &(id, "clicked", Value::from(0i32), stamp)).await.map(|_| ())
}

/// Abre o menu do item embaixo do ícone (`x` na topbar que pediu).
#[tauri::command]
pub async fn tray_menu_open(app: AppHandle, window: tauri::WebviewWindow, key: String, x: i32) -> Result<(), String> {
    let item = find_item(&key)?;
    if item.menu_path.is_none() {
        // Sem dbusmenu: o app desenha o próprio menu.
        return call(&item, &item.path, ITEM, "ContextMenu", &(x, 36i32)).await.map(|_| ());
    }
    let nodes = tray_menu(key.clone()).await.inspect_err(|e| eprintln!("lumna-shell: menu da bandeja ({key}): {e}"))?;
    let at = app.state::<MonitorPositions>().0.lock().unwrap().get(window.label()).copied();
    let handle = app.clone();
    app.run_on_main_thread(move || {
        if let Some((mx, my)) = at {
            layer::move_to_monitor_at("traymenu", mx, my);
        }
        layer::set_visible("traymenu", true);
        let _ = handle.emit_to("traymenu", "lumna://tray-menu", serde_json::json!({ "key": key, "title": item.title, "x": x, "nodes": nodes }));
    })
    .map_err(|e| e.to_string())
}

// ---- início ----

pub fn start(app: AppHandle) {
    let _ = APP.set(app);
    tauri::async_runtime::spawn(async move {
        let built = zbus::connection::Builder::session()
            .and_then(|b| b.name(WATCHER))
            .and_then(|b| b.serve_at(WATCHER_PATH, Watcher));
        let conn = match built {
            Ok(builder) => match builder.build().await {
                Ok(conn) => conn,
                Err(err) => return eprintln!("lumna-shell: bandeja: {err}"),
            },
            Err(err) => return eprintln!("lumna-shell: bandeja: {err}"),
        };
        let _ = CONNECTION.set(conn.clone());
        if let Ok(iface) = conn.object_server().interface::<_, Watcher>(WATCHER_PATH).await {
            let _ = Watcher::status_notifier_host_registered(iface.signal_emitter()).await;
        }
        watch_signals(conn).await;
    });
}

/// Itens que mudam (ícone, título, status) e apps que fecham.
async fn watch_signals(conn: zbus::Connection) {
    let Ok(mut stream) = zbus::MessageStream::for_match_rule(
        zbus::MatchRule::builder().msg_type(zbus::message::Type::Signal).build(),
        &conn,
        Some(256),
    )
    .await
    else {
        return;
    };
    while let Some(Ok(message)) = stream.next().await {
        let header = message.header();
        let (Some(iface), Some(member)) = (header.interface(), header.member()) else { continue };
        let sender = header.sender().map(|s| s.to_string()).unwrap_or_default();
        match (iface.as_str(), member.as_str()) {
            (ITEM, _) => {
                let targets: Vec<(String, String)> = ITEMS
                    .lock()
                    .unwrap()
                    .iter()
                    .filter(|i| i.owner == sender || i.bus == sender)
                    .map(|i| (i.bus.clone(), i.path.clone()))
                    .collect();
                for (bus, path) in targets {
                    let sender = sender.clone();
                    tauri::async_runtime::spawn(async move { refresh_item(bus, path, sender).await });
                }
            }
            ("org.freedesktop.DBus", "NameOwnerChanged") => {
                let Ok((name, _old, new)) = message.body().deserialize::<(String, String, String)>() else { continue };
                if new.is_empty() {
                    let removed: Vec<String> = {
                        let mut items = ITEMS.lock().unwrap();
                        let gone: Vec<String> = items.iter().filter(|i| i.owner == name || i.bus == name).map(|i| i.key.clone()).collect();
                        items.retain(|i| i.owner != name && i.bus != name);
                        gone
                    };
                    if !removed.is_empty() {
                        if let Ok(iface) = conn.object_server().interface::<_, Watcher>(WATCHER_PATH).await {
                            for key in &removed {
                                let _ = Watcher::status_notifier_item_unregistered(iface.signal_emitter(), key).await;
                            }
                        }
                        changed();
                    }
                }
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use zbus::zvariant::{Array, Dict, Signature, StructureBuilder};

    #[test]
    fn services() {
        assert_eq!(
            split_service("/org/ayatana/NotificationItem/spotify", ":1.42"),
            (":1.42".to_string(), "/org/ayatana/NotificationItem/spotify".to_string())
        );
        assert_eq!(
            split_service("org.kde.StatusNotifierItem-12-1", ":1.9"),
            ("org.kde.StatusNotifierItem-12-1".to_string(), "/StatusNotifierItem".to_string())
        );
        assert_eq!(split_service(":1.7/tray", ":1.7"), (":1.7".to_string(), "/tray".to_string()));
    }

    #[test]
    fn pixmap_becomes_png() {
        // 1×1, vermelho opaco em ARGB.
        let png = argb_to_png(1, 1, &[255, 255, 0, 0]).unwrap();
        assert_eq!(&png[1..4], b"PNG");
    }

    fn node(id: i32, props: Vec<(&str, Value<'static>)>, children: Vec<Value<'static>>) -> Value<'static> {
        let mut dict = Dict::new(&Signature::Str, &Signature::Variant);
        for (k, v) in props {
            dict.append(Value::from(k.to_string()), Value::Value(Box::new(v))).unwrap();
        }
        let mut array = Array::new(&Signature::Variant);
        for c in children {
            array.append(Value::Value(Box::new(c))).unwrap();
        }
        Value::Structure(StructureBuilder::new().add_field(id).append_field(Value::Dict(dict)).append_field(Value::Array(array)).build().unwrap())
    }

    #[test]
    fn menu_layout() {
        let root = node(
            0,
            vec![],
            vec![
                node(1, vec![("label", Value::from("_Mostrar Spotify"))], vec![]),
                node(2, vec![("type", Value::from("separator"))], vec![]),
                node(3, vec![("label", Value::from("Escondido")), ("visible", Value::from(false))], vec![]),
                node(4, vec![("label", Value::from("Repetir")), ("toggle-type", Value::from("checkmark")), ("toggle-state", Value::from(1i32))], vec![]),
                node(5, vec![("label", Value::from("Sair")), ("enabled", Value::from(false))], vec![]),
            ],
        );
        let menu = parse_node(&root).unwrap().children;
        assert_eq!(menu.len(), 4);
        assert_eq!(menu[0].label, "Mostrar Spotify");
        assert!(menu[1].separator);
        assert!(menu[2].checked && menu[2].toggle.as_deref() == Some("checkmark"));
        assert!(!menu[3].enabled);
    }

    #[test]
    fn icon_by_absolute_path_and_missing() {
        let dir = std::env::temp_dir().join(format!("lumna-tray-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("hicolor/48x48/apps")).unwrap();
        std::fs::write(dir.join("hicolor/48x48/apps/spotify-tray.png"), b"png").unwrap();
        assert_eq!(find_icon("spotify-tray", dir.to_str().unwrap(), None), Some(dir.join("hicolor/48x48/apps/spotify-tray.png")));
        assert_eq!(find_icon("nao-existe-mesmo-xyz", "", None), None);
        let _ = std::fs::remove_dir_all(dir);
    }
}
