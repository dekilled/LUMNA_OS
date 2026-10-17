//! Wi-Fi pelo iwd (o mesmo serviço que o Arch usa no pendrive), conversando com ele pelo D-Bus.
//!
//! O iwd guarda as redes conhecidas e pede a senha a um "agente" quando conecta numa rede nova:
//! o shell se registra como agente e entrega a senha que a pessoa digitou no menu.
//! Quem está no grupo `wheel` pode falar com o iwd (política do próprio pacote).

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use futures_util::StreamExt;
use serde::Serialize;
use tauri::{AppHandle, Emitter};
use tokio::sync::OnceCell;
use zbus::fdo::ObjectManagerProxy;
use zbus::zvariant::{ObjectPath, OwnedObjectPath, OwnedValue, Value};
use zbus::Connection;

const IWD: &str = "net.connman.iwd";
const DEVICE: &str = "net.connman.iwd.Device";
const STATION: &str = "net.connman.iwd.Station";
const NETWORK: &str = "net.connman.iwd.Network";
const KNOWN: &str = "net.connman.iwd.KnownNetwork";
const AGENT_PATH: &str = "/os/lumna/wifi/agent";

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Network {
    /// Caminho do objeto no iwd; é o que identifica a rede nas outras chamadas.
    pub path: String,
    pub name: String,
    /// `open`, `psk` (senha) ou `8021x` (empresa, com usuário).
    pub security: String,
    /// 0 a 4 tracinhos.
    pub signal: u8,
    pub known: bool,
    pub connected: bool,
}

#[derive(Debug, Clone, Serialize, Default, PartialEq)]
pub struct WifiState {
    /// O iwd está rodando e achou uma placa Wi-Fi.
    pub available: bool,
    pub powered: bool,
    pub scanning: bool,
    /// `connected`, `connecting`, `disconnected`... (o estado do iwd).
    pub state: String,
    /// Nome da rede conectada.
    pub connected: Option<String>,
    pub networks: Vec<Network>,
}

/// Sinal do iwd (dBm × 100) em tracinhos, nas faixas que os celulares costumam usar.
pub fn bars(strength: i16) -> u8 {
    match strength {
        s if s >= -6000 => 4,
        s if s >= -6700 => 3,
        s if s >= -7400 => 2,
        s if s >= -8100 => 1,
        _ => 0,
    }
}

/// Mensagem para a pessoa a partir do erro D-Bus do iwd.
pub fn explain(error: &zbus::Error) -> String {
    let name = match error {
        zbus::Error::MethodError(name, _, _) => name.as_str().to_string(),
        zbus::Error::FDO(fdo) => return format!("Wi-Fi: {fdo}"),
        other => return format!("Wi-Fi: {other}"),
    };
    match name.trim_start_matches("net.connman.iwd.") {
        "Failed" => "Não conectou. Confira a senha.",
        "InvalidFormat" => "Senha inválida: precisa ter de 8 a 63 caracteres.",
        "Aborted" | "Agent.Error.Canceled" => "Conexão cancelada.",
        "InProgress" | "Busy" => "Já está conectando, espere um pouco.",
        "NotFound" | "NotAvailable" => "A rede saiu do alcance.",
        "NotConnected" => "Não está conectado.",
        "NotSupported" => "Este tipo de rede ainda não é suportado.",
        "Timeout" => "A rede demorou a responder. Tente de novo.",
        _ => return format!("Wi-Fi: {name}"),
    }
    .to_string()
}

// ---- agente de senha ----

#[derive(Debug, zbus::DBusError)]
#[zbus(prefix = "net.connman.iwd.Agent.Error")]
enum AgentError {
    #[zbus(error)]
    ZBus(zbus::Error),
    Canceled(String),
}

/// Responde ao iwd com a senha que o menu guardou para a rede, uma vez só.
struct Agent {
    passwords: Arc<Mutex<HashMap<String, String>>>,
}

#[zbus::interface(name = "net.connman.iwd.Agent")]
impl Agent {
    fn release(&self) {}

    fn request_passphrase(&self, network: OwnedObjectPath) -> Result<String, AgentError> {
        self.passwords
            .lock()
            .unwrap()
            .remove(network.as_str())
            .ok_or_else(|| AgentError::Canceled("sem senha".into()))
    }

    fn request_private_key_passphrase(&self, _network: OwnedObjectPath) -> Result<String, AgentError> {
        Err(AgentError::Canceled("não suportado".into()))
    }

    fn request_user_name_and_password(&self, _network: OwnedObjectPath) -> Result<(String, String), AgentError> {
        Err(AgentError::Canceled("não suportado".into()))
    }

    fn request_user_password(&self, _network: OwnedObjectPath, _user: String) -> Result<String, AgentError> {
        Err(AgentError::Canceled("não suportado".into()))
    }

    fn cancel(&self, _reason: String) {}
}

// ---- conexão com o iwd ----

struct Wifi {
    connection: Connection,
    passwords: Arc<Mutex<HashMap<String, String>>>,
}

static WIFI: OnceCell<Wifi> = OnceCell::const_new();

async fn wifi() -> Result<&'static Wifi, String> {
    WIFI.get_or_try_init(|| async {
        let connection = Connection::system().await.map_err(|e| format!("D-Bus do sistema: {e}"))?;
        let passwords = Arc::new(Mutex::new(HashMap::new()));
        connection
            .object_server()
            .at(AGENT_PATH, Agent { passwords: passwords.clone() })
            .await
            .map_err(|e| e.to_string())?;
        Ok(Wifi { connection, passwords })
    })
    .await
}

type Properties = HashMap<String, OwnedValue>;
type Objects = HashMap<String, HashMap<String, Properties>>;

impl Wifi {
    async fn call<B, R>(&self, path: &str, interface: &str, method: &str, body: &B) -> zbus::Result<R>
    where
        B: serde::Serialize + zbus::zvariant::DynamicType,
        R: for<'d> zbus::zvariant::DynamicDeserialize<'d>,
    {
        let reply = self.connection.call_method(Some(IWD), path, Some(interface), method, body).await?;
        reply.body().deserialize::<R>()
    }

    async fn objects(&self) -> zbus::Result<Objects> {
        let manager = ObjectManagerProxy::builder(&self.connection).destination(IWD)?.path("/")?.build().await?;
        Ok(manager
            .get_managed_objects()
            .await?
            .into_iter()
            .map(|(path, interfaces)| {
                let interfaces = interfaces.into_iter().map(|(name, props)| (name.to_string(), props)).collect();
                (path.to_string(), interfaces)
            })
            .collect())
    }

    /// A primeira placa Wi-Fi (notebooks têm uma; o resto é raro o bastante para ignorar).
    fn device(objects: &Objects) -> Option<(&String, &HashMap<String, Properties>)> {
        let mut devices: Vec<_> = objects.iter().filter(|(_, i)| i.contains_key(DEVICE)).collect();
        devices.sort_by(|a, b| a.0.cmp(b.0));
        devices.into_iter().next()
    }

    async fn state(&self) -> WifiState {
        let Ok(objects) = self.objects().await else { return WifiState::default() };
        let Some((path, interfaces)) = Self::device(&objects) else { return WifiState::default() };
        let mut state = WifiState {
            available: true,
            powered: interfaces.get(DEVICE).and_then(|p| get_bool(p, "Powered")).unwrap_or(false),
            state: "disconnected".into(),
            ..Default::default()
        };
        // Sem a interface Station a placa está desligada (ou em outro modo, como ponto de acesso).
        let Some(station) = interfaces.get(STATION) else { return state };
        state.scanning = get_bool(station, "Scanning").unwrap_or(false);
        state.state = get_string(station, "State").unwrap_or_default();

        let ordered: Vec<(OwnedObjectPath, i16)> =
            self.call(path, STATION, "GetOrderedNetworks", &()).await.unwrap_or_default();
        let mut seen = std::collections::HashSet::new();
        for (network_path, strength) in ordered {
            let Some(props) = objects.get(network_path.as_str()).and_then(|i| i.get(NETWORK)) else { continue };
            let name = get_string(props, "Name").unwrap_or_default();
            // A mesma rede pode aparecer duas vezes (2,4 e 5 GHz chegam juntas); fica a mais forte.
            if name.is_empty() || !seen.insert(name.clone()) {
                continue;
            }
            let connected = get_bool(props, "Connected").unwrap_or(false);
            if connected {
                state.connected = Some(name.clone());
            }
            state.networks.push(Network {
                path: network_path.to_string(),
                name,
                security: get_string(props, "Type").unwrap_or_else(|| "open".into()),
                signal: bars(strength),
                known: props.contains_key("KnownNetwork"),
                connected,
            });
        }
        // A conectada sempre no topo.
        state.networks.sort_by_key(|n| !n.connected);
        state
    }

    async fn station(&self) -> Result<String, String> {
        let objects = self.objects().await.map_err(|e| explain(&e))?;
        let (path, interfaces) = Self::device(&objects).ok_or("Nenhuma placa Wi-Fi encontrada.")?;
        if !interfaces.contains_key(STATION) {
            return Err("O Wi-Fi está desligado.".into());
        }
        Ok(path.clone())
    }

    async fn register_agent(&self) {
        let path = ObjectPath::from_static_str_unchecked(AGENT_PATH);
        // Se o iwd reiniciou, ele esqueceu o agente; registrar de novo a cada conexão resolve.
        // "AlreadyExists" é o caso comum e não é erro.
        let _: zbus::Result<()> = self.call("/net/connman/iwd", "net.connman.iwd.AgentManager", "RegisterAgent", &(path,)).await;
    }
}

fn get_bool(props: &Properties, key: &str) -> Option<bool> {
    props.get(key).and_then(|v| bool::try_from(v).ok())
}

fn get_string(props: &Properties, key: &str) -> Option<String> {
    props.get(key).and_then(|v| v.try_clone().ok()).and_then(|v| String::try_from(v).ok())
}

// ---- comandos da interface ----
// Sem o iwd e com o NetworkManager (o celular), tudo vai pro `nmwifi`.

async fn has_owner(conn: &Connection, name: &str) -> bool {
    let Ok(dbus) = zbus::fdo::DBusProxy::new(conn).await else { return false };
    let Ok(name) = zbus::names::BusName::try_from(name) else { return false };
    dbus.name_has_owner(name).await.unwrap_or(false)
}

pub async fn use_nm() -> bool {
    let Ok(conn) = Connection::system().await else { return false };
    !has_owner(&conn, IWD).await && has_owner(&conn, "org.freedesktop.NetworkManager").await
}

async fn nm<T: Send + 'static>(f: impl FnOnce() -> T + Send + 'static) -> T {
    tauri::async_runtime::spawn_blocking(f).await.expect("thread do nmcli")
}

#[tauri::command]
pub async fn wifi_state() -> WifiState {
    if use_nm().await {
        return nm(crate::nmwifi::state).await;
    }
    match wifi().await {
        Ok(wifi) => wifi.state().await,
        Err(_) => WifiState::default(),
    }
}

/// Procura redes. Responde na hora; a lista nova chega pelo evento `lumna://wifi`.
#[tauri::command]
pub async fn wifi_scan() -> Result<(), String> {
    if use_nm().await {
        return nm(crate::nmwifi::scan).await;
    }
    let wifi = wifi().await?;
    let station = wifi.station().await?;
    // "Busy": já está procurando; dá no mesmo.
    let _: zbus::Result<()> = wifi.call(&station, STATION, "Scan", &()).await;
    Ok(())
}

/// Conecta numa rede. A senha só é usada se o iwd pedir (rede nova com senha).
#[tauri::command]
pub async fn wifi_connect(path: String, passphrase: Option<String>) -> Result<(), String> {
    if path.starts_with(crate::nmwifi::PREFIX) {
        return nm(move || crate::nmwifi::connect(&path, passphrase)).await;
    }
    let wifi = wifi().await?;
    wifi.register_agent().await;
    if let Some(passphrase) = passphrase {
        wifi.passwords.lock().unwrap().insert(path.clone(), passphrase);
    }
    let result: zbus::Result<()> = wifi.call(&path, NETWORK, "Connect", &()).await;
    wifi.passwords.lock().unwrap().remove(&path);
    result.map_err(|e| explain(&e))
}

#[tauri::command]
pub async fn wifi_disconnect() -> Result<(), String> {
    if use_nm().await {
        return nm(crate::nmwifi::disconnect).await;
    }
    let wifi = wifi().await?;
    let station = wifi.station().await?;
    wifi.call::<_, ()>(&station, STATION, "Disconnect", &()).await.map_err(|e| explain(&e))
}

/// Esquece a senha salva da rede (ela também desconecta, se for a atual).
#[tauri::command]
pub async fn wifi_forget(path: String) -> Result<(), String> {
    if path.starts_with(crate::nmwifi::PREFIX) {
        return nm(move || crate::nmwifi::forget(&path)).await;
    }
    let wifi = wifi().await?;
    let objects = wifi.objects().await.map_err(|e| explain(&e))?;
    let known = objects
        .get(&path)
        .and_then(|i| i.get(NETWORK))
        .and_then(|p| p.get("KnownNetwork"))
        .and_then(|v| v.try_clone().ok())
        .and_then(|v| OwnedObjectPath::try_from(v).ok())
        .ok_or("Essa rede não está salva.")?;
    wifi.call::<_, ()>(known.as_str(), KNOWN, "Forget", &()).await.map_err(|e| explain(&e))
}

#[tauri::command]
pub async fn wifi_power(on: bool) -> Result<(), String> {
    if use_nm().await {
        return nm(move || crate::nmwifi::power(on)).await;
    }
    let wifi = wifi().await?;
    let objects = wifi.objects().await.map_err(|e| explain(&e))?;
    let (path, _) = Wifi::device(&objects).ok_or("Nenhuma placa Wi-Fi encontrada.")?;
    wifi.call::<_, ()>(path, "org.freedesktop.DBus.Properties", "Set", &(DEVICE, "Powered", Value::from(on)))
        .await
        .map_err(|e| explain(&e))
}

/// Rede escondida (ou digitada à mão), no NetworkManager.
#[tauri::command]
pub async fn wifi_add(ssid: String, password: String, hidden: bool) -> Result<(), String> {
    if !use_nm().await {
        return Err("Adicionar rede à mão ainda não funciona com o iwd.".into());
    }
    nm(move || crate::nmwifi::add(&ssid, &password, hidden)).await
}

/// Propriedades da rede conectada (sinal, frequência, IP...).
#[tauri::command]
pub async fn wifi_details() -> Option<crate::nmwifi::Details> {
    nm(crate::nmwifi::details).await
}

/// Manda o estado novo para as janelas sempre que o iwd avisa alguma mudança.
/// Com o NetworkManager, confere a cada poucos segundos.
pub fn watch(app: AppHandle) {
    tauri::async_runtime::spawn(async move {
        if use_nm().await {
            let mut last = WifiState::default();
            loop {
                let state = nm(crate::nmwifi::state).await;
                if state != last {
                    let _ = app.emit("lumna://wifi", &state);
                    last = state;
                }
                tokio::time::sleep(Duration::from_secs(4)).await;
            }
        }
        let mut wait = Duration::from_secs(2);
        loop {
            if let Err(err) = follow(&app).await {
                eprintln!("lumna-shell: wi-fi: {err} (nova tentativa em {wait:?})");
            }
            tokio::time::sleep(wait).await;
            wait = (wait * 2).min(Duration::from_secs(60));
        }
    });
}

async fn follow(app: &AppHandle) -> Result<(), String> {
    let wifi = wifi().await?;
    let rule = zbus::MatchRule::builder()
        .msg_type(zbus::message::Type::Signal)
        .sender(IWD)
        .map_err(|e| e.to_string())?
        .build();
    let mut stream = zbus::MessageStream::for_match_rule(rule, &wifi.connection, Some(64))
        .await
        .map_err(|e| e.to_string())?;
    let mut last = wifi.state().await;
    let _ = app.emit("lumna://wifi", &last);
    while stream.next().await.is_some() {
        // Uma busca gera dezenas de sinais seguidos: junta tudo num estado só.
        while let Ok(Some(_)) = tokio::time::timeout(Duration::from_millis(250), stream.next()).await {}
        let state = wifi.state().await;
        if state != last {
            let _ = app.emit("lumna://wifi", &state);
            last = state;
        }
    }
    Err("conexão com o D-Bus caiu".into())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signal_bars() {
        assert_eq!(bars(-4500), 4);
        assert_eq!(bars(-6500), 3);
        assert_eq!(bars(-7000), 2);
        assert_eq!(bars(-8000), 1);
        assert_eq!(bars(-9000), 0);
    }
}
