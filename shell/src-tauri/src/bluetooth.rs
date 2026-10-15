//! Bluetooth pelo BlueZ (D-Bus `org.bluez`): liga e desliga, procura aparelhos, pareia, conecta.
//!
//! O shell é o "agente" do BlueZ: quando um pareamento pede para conferir um código, a pergunta
//! vai para a tela (evento `lumna://bt-confirm`) e a resposta volta por `bluetooth_confirm`.
//! Fora de um pareamento começado pelo LUMNA, os pedidos de outros aparelhos são recusados.

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, LazyLock, Mutex};
use std::time::Duration;

use futures_util::StreamExt;
use serde::Serialize;
use tauri::{AppHandle, Emitter};
use tokio::sync::{oneshot, OnceCell};
use zbus::fdo::ObjectManagerProxy;
use zbus::zvariant::{ObjectPath, OwnedObjectPath, OwnedValue, Value};
use zbus::Connection;

const BLUEZ: &str = "org.bluez";
const ADAPTER: &str = "org.bluez.Adapter1";
const DEVICE: &str = "org.bluez.Device1";
const BATTERY: &str = "org.bluez.Battery1";
const AGENT_PATH: &str = "/os/lumna/bluetooth/agent";

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Device {
    pub path: String,
    pub address: String,
    pub name: String,
    /// Tipo pelo BlueZ: `audio-headset`, `audio-headphones`, `input-gaming`, `input-keyboard`,
    /// `input-mouse`, `phone`, `computer`... (vazio quando o aparelho não diz).
    pub icon: String,
    pub paired: bool,
    pub connected: bool,
    pub battery: Option<u8>,
}

#[derive(Debug, Clone, Serialize, Default, PartialEq)]
pub struct BluetoothState {
    /// O bluetoothd está rodando e há um adaptador.
    pub available: bool,
    pub powered: bool,
    pub discovering: bool,
    /// Pareados primeiro; depois os achados na busca (só os que têm nome).
    pub devices: Vec<Device>,
}

/// Pergunta para a tela: confere se o código no aparelho é este.
#[derive(Debug, Clone, Serialize)]
struct Confirm {
    device: String,
    name: String,
    /// 6 dígitos.
    code: String,
    /// `confirm`: a pessoa confirma que é o mesmo código; `type`: digita o código no aparelho (teclado).
    kind: &'static str,
}

pub fn explain(error: &zbus::Error) -> String {
    let (name, detail) = match error {
        zbus::Error::MethodError(name, detail, _) => (name.as_str().to_string(), detail.clone().unwrap_or_default()),
        other => return format!("Bluetooth: {other}"),
    };
    match name.trim_start_matches("org.bluez.Error.") {
        "AuthenticationFailed" | "AuthenticationRejected" => "O pareamento foi recusado. Tente de novo.",
        "AuthenticationCanceled" => "Pareamento cancelado.",
        "AuthenticationTimeout" => "O aparelho não respondeu a tempo. Coloque-o em modo de pareamento e tente de novo.",
        "ConnectionAttemptFailed" | "Failed" | "PageTimeout" => {
            "Não conectou. O aparelho está ligado, por perto e em modo de pareamento?"
        }
        "InProgress" => "Já está conectando, espere um pouco.",
        "NotReady" => "O Bluetooth está desligado.",
        "DoesNotExist" => "O aparelho sumiu da lista. Procure de novo.",
        "NotSupported" => "O aparelho não suporta isso.",
        _ if !detail.is_empty() => return format!("Bluetooth: {detail}"),
        _ => return format!("Bluetooth: {name}"),
    }
    .to_string()
}

// ---- agente ----

#[derive(Debug, zbus::DBusError)]
#[zbus(prefix = "org.bluez.Error")]
enum AgentError {
    #[zbus(error)]
    ZBus(zbus::Error),
    Rejected(String),
}

fn rejected() -> AgentError {
    AgentError::Rejected("recusado pelo LUMNA".into())
}

struct Agent {
    app: AppHandle,
    /// Um pareamento começado pelo LUMNA está em andamento.
    pairing: Arc<AtomicBool>,
    answers: Arc<Mutex<HashMap<String, oneshot::Sender<bool>>>>,
}

impl Agent {
    fn guard(&self) -> Result<(), AgentError> {
        if self.pairing.load(Ordering::SeqCst) {
            Ok(())
        } else {
            Err(rejected())
        }
    }

    fn show(&self, device: &OwnedObjectPath, code: u32, kind: &'static str) {
        let name = DEVICE_NAMES.lock().unwrap().get(device.as_str()).cloned().unwrap_or_default();
        let _ = self.app.emit(
            "lumna://bt-confirm",
            Confirm { device: device.to_string(), name, code: format!("{code:06}"), kind },
        );
    }
}

/// Nome de cada aparelho pelo caminho, para as perguntas do agente.
static DEVICE_NAMES: LazyLock<Mutex<HashMap<String, String>>> = LazyLock::new(Default::default);

#[zbus::interface(name = "org.bluez.Agent1")]
impl Agent {
    fn release(&self) {}

    /// Aparelhos antigos pedem um PIN; o padrão de fábrica da maioria é 0000.
    fn request_pin_code(&self, _device: OwnedObjectPath) -> Result<String, AgentError> {
        self.guard()?;
        Ok("0000".into())
    }

    fn display_pin_code(&self, device: OwnedObjectPath, pincode: String) -> Result<(), AgentError> {
        self.guard()?;
        self.show(&device, pincode.parse().unwrap_or(0), "type");
        Ok(())
    }

    fn request_passkey(&self, _device: OwnedObjectPath) -> Result<u32, AgentError> {
        Err(rejected())
    }

    fn display_passkey(&self, device: OwnedObjectPath, passkey: u32, _entered: u16) -> Result<(), AgentError> {
        self.guard()?;
        self.show(&device, passkey, "type");
        Ok(())
    }

    async fn request_confirmation(&self, device: OwnedObjectPath, passkey: u32) -> Result<(), AgentError> {
        self.guard()?;
        let (tx, rx) = oneshot::channel();
        self.answers.lock().unwrap().insert(device.to_string(), tx);
        self.show(&device, passkey, "confirm");
        match tokio::time::timeout(Duration::from_secs(60), rx).await {
            Ok(Ok(true)) => Ok(()),
            _ => {
                self.answers.lock().unwrap().remove(device.as_str());
                let _ = self.app.emit("lumna://bt-confirm-done", device.to_string());
                Err(rejected())
            }
        }
    }

    fn request_authorization(&self, _device: OwnedObjectPath) -> Result<(), AgentError> {
        self.guard()
    }

    fn authorize_service(&self, _device: OwnedObjectPath, _uuid: String) -> Result<(), AgentError> {
        self.guard()
    }

    fn cancel(&self) {
        self.answers.lock().unwrap().clear();
        let _ = self.app.emit("lumna://bt-confirm-done", "");
    }
}

// ---- conexão ----

struct Bluetooth {
    connection: Connection,
    pairing: Arc<AtomicBool>,
    answers: Arc<Mutex<HashMap<String, oneshot::Sender<bool>>>>,
}

static BLUETOOTH: OnceCell<Bluetooth> = OnceCell::const_new();
static APP: std::sync::OnceLock<AppHandle> = std::sync::OnceLock::new();

async fn bluetooth() -> Result<&'static Bluetooth, String> {
    BLUETOOTH
        .get_or_try_init(|| async {
            let app = APP.get().ok_or("Bluetooth ainda não iniciou")?.clone();
            let connection = Connection::system().await.map_err(|e| format!("D-Bus do sistema: {e}"))?;
            let pairing = Arc::new(AtomicBool::new(false));
            let answers = Arc::new(Mutex::new(HashMap::new()));
            connection
                .object_server()
                .at(AGENT_PATH, Agent { app, pairing: pairing.clone(), answers: answers.clone() })
                .await
                .map_err(|e| e.to_string())?;
            Ok(Bluetooth { connection, pairing, answers })
        })
        .await
}

type Properties = HashMap<String, OwnedValue>;
type Objects = HashMap<String, HashMap<String, Properties>>;

fn get_bool(props: &Properties, key: &str) -> Option<bool> {
    props.get(key).and_then(|v| bool::try_from(v).ok())
}

fn get_string(props: &Properties, key: &str) -> Option<String> {
    props.get(key).and_then(|v| v.try_clone().ok()).and_then(|v| String::try_from(v).ok())
}

impl Bluetooth {
    async fn call<B, R>(&self, path: &str, interface: &str, method: &str, body: &B) -> zbus::Result<R>
    where
        B: serde::Serialize + zbus::zvariant::DynamicType,
        R: for<'d> zbus::zvariant::DynamicDeserialize<'d>,
    {
        let reply = self.connection.call_method(Some(BLUEZ), path, Some(interface), method, body).await?;
        reply.body().deserialize::<R>()
    }

    async fn set(&self, path: &str, interface: &str, key: &str, value: bool) -> zbus::Result<()> {
        self.call(path, "org.freedesktop.DBus.Properties", "Set", &(interface, key, Value::from(value))).await
    }

    async fn objects(&self) -> zbus::Result<Objects> {
        let manager = ObjectManagerProxy::builder(&self.connection).destination(BLUEZ)?.path("/")?.build().await?;
        Ok(manager
            .get_managed_objects()
            .await?
            .into_iter()
            .map(|(path, interfaces)| {
                (path.to_string(), interfaces.into_iter().map(|(name, props)| (name.to_string(), props)).collect())
            })
            .collect())
    }

    fn adapter(objects: &Objects) -> Option<(&String, &Properties)> {
        let mut adapters: Vec<_> =
            objects.iter().filter_map(|(path, i)| i.get(ADAPTER).map(|props| (path, props))).collect();
        adapters.sort_by(|a, b| a.0.cmp(b.0));
        adapters.into_iter().next()
    }

    async fn state(&self) -> BluetoothState {
        let Ok(objects) = self.objects().await else { return BluetoothState::default() };
        let Some((adapter, props)) = Self::adapter(&objects) else { return BluetoothState::default() };
        let mut state = BluetoothState {
            available: true,
            powered: get_bool(props, "Powered").unwrap_or(false),
            discovering: get_bool(props, "Discovering").unwrap_or(false),
            devices: Vec::new(),
        };
        let mut names = HashMap::new();
        for (path, interfaces) in &objects {
            let Some(device) = interfaces.get(DEVICE) else { continue };
            if !path.starts_with(adapter.as_str()) {
                continue;
            }
            let paired = get_bool(device, "Paired").unwrap_or(false);
            let name = get_string(device, "Name");
            // Fora dos pareados, só aparecem os que têm nome e foram vistos nesta busca (têm sinal).
            if !paired && (name.is_none() || !device.contains_key("RSSI")) {
                continue;
            }
            let address = get_string(device, "Address").unwrap_or_default();
            let name = get_string(device, "Alias").or(name).unwrap_or_else(|| address.clone());
            names.insert(path.clone(), name.clone());
            state.devices.push(Device {
                path: path.clone(),
                address,
                name,
                icon: get_string(device, "Icon").unwrap_or_default(),
                paired,
                connected: get_bool(device, "Connected").unwrap_or(false),
                battery: interfaces.get(BATTERY).and_then(|b| b.get("Percentage")).and_then(|v| u8::try_from(v).ok()),
            });
        }
        *DEVICE_NAMES.lock().unwrap() = names;
        state.devices.sort_by(|a, b| (!a.paired, !a.connected, a.name.to_lowercase()).cmp(&(!b.paired, !b.connected, b.name.to_lowercase())));
        state
    }

    async fn adapter_path(&self) -> Result<String, String> {
        let objects = self.objects().await.map_err(|e| explain(&e))?;
        Self::adapter(&objects).map(|(path, _)| path.clone()).ok_or_else(|| "Nenhum adaptador Bluetooth encontrado.".into())
    }

    async fn register_agent(&self) {
        let path = ObjectPath::from_static_str_unchecked(AGENT_PATH);
        // "AlreadyExists" quando já está registrado; tudo bem.
        let _: zbus::Result<()> =
            self.call("/org/bluez", "org.bluez.AgentManager1", "RegisterAgent", &(&path, "KeyboardDisplay")).await;
        let _: zbus::Result<()> = self.call("/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent", &(&path,)).await;
    }
}

// ---- comandos ----

#[tauri::command]
pub async fn bluetooth_state() -> BluetoothState {
    match bluetooth().await {
        Ok(bt) => bt.state().await,
        Err(_) => BluetoothState::default(),
    }
}

#[tauri::command]
pub async fn bluetooth_power(on: bool) -> Result<(), String> {
    let bt = bluetooth().await?;
    let adapter = bt.adapter_path().await?;
    bt.set(&adapter, ADAPTER, "Powered", on).await.map_err(|e| explain(&e))
}

/// Liga ou desliga a busca. O BlueZ para a busca sozinho se o shell sair.
#[tauri::command]
pub async fn bluetooth_scan(on: bool) -> Result<(), String> {
    let bt = bluetooth().await?;
    let adapter = bt.adapter_path().await?;
    let method = if on { "StartDiscovery" } else { "StopDiscovery" };
    match bt.call::<_, ()>(&adapter, ADAPTER, method, &()).await {
        // Já estava (ou não estava) procurando: dá no mesmo.
        Err(zbus::Error::MethodError(name, _, _)) if name.as_str().ends_with("InProgress") || name.as_str().ends_with("Failed") => Ok(()),
        other => other.map_err(|e| explain(&e)),
    }
}

/// Pareia, marca como confiável (reconecta sozinho depois) e conecta.
#[tauri::command]
pub async fn bluetooth_pair(path: String) -> Result<(), String> {
    let bt = bluetooth().await?;
    bt.register_agent().await;
    bt.pairing.store(true, Ordering::SeqCst);
    let paired = bt.call::<_, ()>(&path, DEVICE, "Pair", &()).await;
    bt.pairing.store(false, Ordering::SeqCst);
    match paired {
        Err(zbus::Error::MethodError(name, _, _)) if name.as_str().ends_with("AlreadyExists") => {}
        other => other.map_err(|e| explain(&e))?,
    }
    let _ = bt.set(&path, DEVICE, "Trusted", true).await;
    bt.call::<_, ()>(&path, DEVICE, "Connect", &()).await.map_err(|e| explain(&e))
}

#[tauri::command]
pub async fn bluetooth_connect(path: String) -> Result<(), String> {
    let bt = bluetooth().await?;
    bt.call::<_, ()>(&path, DEVICE, "Connect", &()).await.map_err(|e| explain(&e))
}

#[tauri::command]
pub async fn bluetooth_disconnect(path: String) -> Result<(), String> {
    let bt = bluetooth().await?;
    bt.call::<_, ()>(&path, DEVICE, "Disconnect", &()).await.map_err(|e| explain(&e))
}

/// Esquece o aparelho (desfaz o pareamento).
#[tauri::command]
pub async fn bluetooth_forget(path: String) -> Result<(), String> {
    let bt = bluetooth().await?;
    let adapter = bt.adapter_path().await?;
    let device = OwnedObjectPath::try_from(path).map_err(|e| e.to_string())?;
    bt.call::<_, ()>(&adapter, ADAPTER, "RemoveDevice", &(device,)).await.map_err(|e| explain(&e))
}

/// Resposta da tela à pergunta "o código é este?".
#[tauri::command]
pub async fn bluetooth_confirm(device: String, accept: bool) -> Result<(), String> {
    let bt = bluetooth().await?;
    if let Some(tx) = bt.answers.lock().unwrap().remove(&device) {
        let _ = tx.send(accept);
    }
    Ok(())
}

/// Registra o agente e manda o estado novo às janelas a cada mudança no BlueZ.
pub fn watch(app: AppHandle) {
    let _ = APP.set(app.clone());
    tauri::async_runtime::spawn(async move {
        let mut wait = Duration::from_secs(2);
        loop {
            if let Err(err) = follow(&app).await {
                eprintln!("lumna-shell: bluetooth: {err} (nova tentativa em {wait:?})");
            }
            tokio::time::sleep(wait).await;
            wait = (wait * 2).min(Duration::from_secs(60));
        }
    });
}

async fn follow(app: &AppHandle) -> Result<(), String> {
    let bt = bluetooth().await?;
    let rule = zbus::MatchRule::builder()
        .msg_type(zbus::message::Type::Signal)
        .sender(BLUEZ)
        .map_err(|e| e.to_string())?
        .build();
    let mut stream =
        zbus::MessageStream::for_match_rule(rule, &bt.connection, Some(64)).await.map_err(|e| e.to_string())?;
    bt.register_agent().await;
    let mut last = bt.state().await;
    let _ = app.emit("lumna://bluetooth", &last);
    while stream.next().await.is_some() {
        // A busca manda sinais o tempo todo (força do sinal): junta tudo num estado só.
        while let Ok(Some(_)) = tokio::time::timeout(Duration::from_millis(300), stream.next()).await {}
        let state = bt.state().await;
        if state != last {
            // O bluetoothd reiniciou (adaptador voltou): o agente precisa ser registrado de novo.
            if state.available && !last.available {
                bt.register_agent().await;
            }
            let _ = app.emit("lumna://bluetooth", &state);
            last = state;
        }
    }
    Err("conexão com o D-Bus caiu".into())
}
