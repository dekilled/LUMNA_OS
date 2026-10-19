//! Camada de automação: macros (sequências de ações) e gatilhos que rodam macros sozinhos.
//!
//! - Macro: `~/.config/lumna/macros/<nome>.json`, `{name, description, steps}`. Um passo é uma ação
//!   do catálogo (`{"action": "audio.volume", "args": {...}}`), um pedido à Emma
//!   (`{"emma": "deixa as luzes em 20%"}`) ou uma pausa (`{"wait": 2}`).
//! - Automação: `~/.config/lumna/automations.json`, uma lista de `{id, name, enabled, when, macro}`.
//!   `when` é o gatilho: horário, login, modo, app aberto/fechado, Wi-Fi ou Bluetooth conectado.
//! - Histórico: `~/.local/state/lumna/automations.log` (uma linha JSON por execução).
//!
//! A Emma cria e roda macros e automações pelas ações `macro.*` e `automation.*`; a pessoa vê,
//! liga/desliga e roda pelo painel da Emma. Ver docs/EMMA.md.

use std::collections::{HashMap, HashSet};
use std::io::Write;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::Duration;

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Emitter, Manager};

use crate::actions::{self, normalize, Origin, Outcome};
use crate::modes::Mode;

const MAX_STEPS: usize = 30;
const MAX_WAIT: f64 = 300.0;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum Step {
    Action {
        action: String,
        #[serde(default)]
        args: Value,
    },
    Emma {
        emma: String,
    },
    Wait {
        wait: f64,
    },
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Macro {
    pub name: String,
    #[serde(default)]
    pub description: String,
    pub steps: Vec<Step>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum Trigger {
    /// `at` = "08:00"; `days` = 1 (segunda) a 7 (domingo), vazio = todo dia.
    Time {
        at: String,
        #[serde(default)]
        days: Vec<u8>,
    },
    /// Ao entrar na sessão.
    Login,
    /// Uma tela entrou no modo (`desktop`, `gaming`, `media`).
    Mode { mode: String },
    AppOpened { app: String },
    AppClosed { app: String },
    /// Conectou nessa rede Wi-Fi.
    Wifi { network: String },
    /// Esse aparelho Bluetooth conectou.
    Bluetooth { device: String },
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Automation {
    pub id: String,
    pub name: String,
    #[serde(default = "yes")]
    pub enabled: bool,
    pub when: Trigger,
    /// Nome da macro (o arquivo, sem `.json`).
    #[serde(rename = "macro")]
    pub macro_name: String,
}

fn yes() -> bool {
    true
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LogEntry {
    pub at: String,
    /// "Hora do filme (Modo Mídia)" ou "Emma", "Painel", "lumna-ctl".
    pub by: String,
    #[serde(rename = "macro")]
    pub macro_name: String,
    pub ok: bool,
    pub summary: String,
}

// ---- arquivos ----

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

fn config_dir() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home().join(".config")).join("lumna")
}

fn macros_dir() -> PathBuf {
    config_dir().join("macros")
}

fn automations_path() -> PathBuf {
    config_dir().join("automations.json")
}

fn log_path() -> PathBuf {
    std::env::var_os("XDG_STATE_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| home().join(".local/state"))
        .join("lumna/automations.log")
}

/// "Hora do Filme!" → "hora-do-filme".
pub fn slug(name: &str) -> String {
    normalize(name).split(' ').filter(|w| !w.is_empty()).collect::<Vec<_>>().join("-")
}

pub fn list_macros() -> Vec<Macro> {
    let mut list: Vec<Macro> = std::fs::read_dir(macros_dir())
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.path().extension().is_some_and(|x| x == "json"))
        .filter_map(|e| serde_json::from_str(&std::fs::read_to_string(e.path()).ok()?).ok())
        .collect();
    list.sort_by_key(|m| normalize(&m.name));
    list
}

pub fn find_macro(name: &str) -> Option<Macro> {
    let list = list_macros();
    list.iter().find(|m| slug(&m.name) == slug(name)).cloned().or_else(|| actions::best(&list, name, |m| vec![m.name.clone()]).cloned())
}

/// Confere uma macro antes de salvar: passos conhecidos, sem macro dentro de macro, pausas curtas.
pub fn validate(m: &Macro) -> Result<(), String> {
    if slug(&m.name).is_empty() {
        return Err("a macro precisa de um nome".into());
    }
    if m.steps.is_empty() || m.steps.len() > MAX_STEPS {
        return Err(format!("uma macro tem de 1 a {MAX_STEPS} passos"));
    }
    for (i, step) in m.steps.iter().enumerate() {
        let n = i + 1;
        match step {
            Step::Action { action, args } => {
                if action.starts_with("macro.") || action.starts_with("automation.") {
                    return Err(format!("passo {n}: macro não roda outra macro nem mexe em automações"));
                }
                if actions::find(action).is_none() {
                    return Err(format!("passo {n}: ação desconhecida \"{action}\" (veja lumna-ctl actions)"));
                }
                if !(args.is_null() || args.is_object()) {
                    return Err(format!("passo {n}: args precisa ser um objeto JSON"));
                }
            }
            Step::Emma { emma } if emma.trim().is_empty() => return Err(format!("passo {n}: pedido à Emma vazio")),
            Step::Wait { wait } if !(0.0..=MAX_WAIT).contains(wait) => {
                return Err(format!("passo {n}: pausa de 0 a {MAX_WAIT} segundos"))
            }
            _ => {}
        }
    }
    Ok(())
}

fn write_private(path: &std::path::Path, text: &str) -> Result<(), String> {
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    let tmp = path.with_extension("tmp");
    std::fs::write(&tmp, text).map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, path).map_err(|e| e.to_string())
}

pub fn save_macro(m: &Macro) -> Result<(), String> {
    validate(m)?;
    let path = macros_dir().join(format!("{}.json", slug(&m.name)));
    write_private(&path, &serde_json::to_string_pretty(m).map_err(|e| e.to_string())?)
}

pub fn delete_macro(name: &str) -> Result<String, String> {
    let m = find_macro(name).ok_or_else(|| format!("nenhuma macro \"{name}\""))?;
    std::fs::remove_file(macros_dir().join(format!("{}.json", slug(&m.name)))).map_err(|e| e.to_string())?;
    Ok(m.name)
}

pub fn list_automations() -> Vec<Automation> {
    std::fs::read_to_string(automations_path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

fn write_automations(list: &[Automation]) -> Result<(), String> {
    write_private(&automations_path(), &serde_json::to_string_pretty(list).map_err(|e| e.to_string())?)
}

/// "08:00" → (8, 0).
fn parse_hhmm(text: &str) -> Option<(u8, u8)> {
    let (h, m) = text.trim().split_once(':')?;
    let (h, m) = (h.parse::<u8>().ok()?, m.parse::<u8>().ok()?);
    (h < 24 && m < 60).then_some((h, m))
}

pub fn validate_trigger(t: &Trigger) -> Result<(), String> {
    match t {
        Trigger::Time { at, days } => {
            parse_hhmm(at).ok_or("horário no formato HH:MM")?;
            if days.iter().any(|d| !(1..=7).contains(d)) {
                return Err("dias de 1 (segunda) a 7 (domingo)".into());
            }
        }
        Trigger::Mode { mode } if Mode::parse(mode).is_none() => return Err("modo: desktop, gaming ou media".into()),
        Trigger::AppOpened { app } | Trigger::AppClosed { app } if app.trim().is_empty() => return Err("diga o programa".into()),
        Trigger::Wifi { network } if network.trim().is_empty() => return Err("diga a rede".into()),
        Trigger::Bluetooth { device } if device.trim().is_empty() => return Err("diga o aparelho".into()),
        _ => {}
    }
    Ok(())
}

/// Cria ou troca (pelo nome) uma automação.
pub fn save_automation(name: &str, when: Trigger, macro_name: &str, enabled: bool) -> Result<Automation, String> {
    validate_trigger(&when)?;
    let target = find_macro(macro_name).ok_or_else(|| format!("nenhuma macro \"{macro_name}\" (crie com macro.save)"))?;
    let mut list = list_automations();
    let id = slug(name);
    if id.is_empty() {
        return Err("a automação precisa de um nome".into());
    }
    let automation = Automation { id: id.clone(), name: name.trim().to_string(), enabled, when, macro_name: slug(&target.name) };
    match list.iter_mut().find(|a| a.id == id) {
        Some(existing) => *existing = automation.clone(),
        None => list.push(automation.clone()),
    }
    write_automations(&list)?;
    Ok(automation)
}

pub fn set_enabled(id: &str, enabled: bool) -> Result<(), String> {
    let mut list = list_automations();
    let a = list.iter_mut().find(|a| a.id == id).ok_or("automação não existe")?;
    a.enabled = enabled;
    write_automations(&list)
}

pub fn delete_automation(name: &str) -> Result<String, String> {
    let mut list = list_automations();
    let before = list.len();
    let id = slug(name);
    let removed = list.iter().find(|a| a.id == id).map(|a| a.name.clone());
    list.retain(|a| a.id != id);
    if list.len() == before {
        return Err(format!("nenhuma automação \"{name}\""));
    }
    write_automations(&list)?;
    Ok(removed.unwrap_or_default())
}

/// Como a pessoa lê o gatilho ("Dias úteis às 08:00").
pub fn describe(t: &Trigger) -> String {
    const DAYS: [&str; 7] = ["seg", "ter", "qua", "qui", "sex", "sáb", "dom"];
    match t {
        Trigger::Time { at, days } => {
            let mut sorted = days.clone();
            sorted.sort();
            sorted.dedup();
            let when = match sorted.as_slice() {
                [] | [1, 2, 3, 4, 5, 6, 7] => "Todo dia".to_string(),
                [1, 2, 3, 4, 5] => "Dias úteis".to_string(),
                [6, 7] => "Fins de semana".to_string(),
                d => d.iter().map(|d| DAYS[(*d as usize).clamp(1, 7) - 1]).collect::<Vec<_>>().join(", "),
            };
            format!("{when} às {at}")
        }
        Trigger::Login => "Ao entrar".into(),
        Trigger::Mode { mode } => format!("Ao entrar no modo {mode}"),
        Trigger::AppOpened { app } => format!("Quando abrir {app}"),
        Trigger::AppClosed { app } => format!("Quando fechar {app}"),
        Trigger::Wifi { network } => format!("Ao conectar no Wi-Fi {network}"),
        Trigger::Bluetooth { device } => format!("Quando {device} conectar"),
    }
}

fn now_text() -> String {
    std::process::Command::new("date")
        .arg("+%Y-%m-%d %H:%M:%S")
        .output()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .unwrap_or_default()
}

fn log(entry: &LogEntry) {
    let path = log_path();
    if let Some(dir) = path.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    // Guarda só as últimas ~500 execuções.
    if std::fs::metadata(&path).is_ok_and(|m| m.len() > 200_000) {
        if let Ok(text) = std::fs::read_to_string(&path) {
            let lines: Vec<&str> = text.lines().collect();
            let _ = std::fs::write(&path, lines[lines.len().saturating_sub(250)..].join("\n") + "\n");
        }
    }
    if let Ok(mut file) = std::fs::OpenOptions::new().create(true).append(true).open(&path) {
        let _ = writeln!(file, "{}", serde_json::to_string(entry).unwrap_or_default());
    }
}

pub fn history(limit: usize) -> Vec<LogEntry> {
    let text = std::fs::read_to_string(log_path()).unwrap_or_default();
    let mut entries: Vec<LogEntry> = text.lines().filter_map(|l| serde_json::from_str(l).ok()).collect();
    entries.reverse();
    entries.truncate(limit);
    entries
}

// ---- execução ----

/// Roda a macro passo a passo, parando no primeiro que falhar.
///
/// Pedida pela própria Emma, os passos "emma" voltam para ela como tarefas (ela já está na
/// conversa); pedida por gatilho, painel ou `lumna-ctl`, eles viram uma mensagem para a Emma.
pub async fn run_macro(app: AppHandle, name: &str, origin: Origin, by: &str) -> Outcome {
    let Some(m) = find_macro(name) else { return Outcome::fail(format!("nenhuma macro \"{name}\"")) };
    if let Err(err) = validate(&m) {
        return Outcome::fail(format!("macro {}: {err}", m.name));
    }
    let overrides = actions::overrides();
    let mut done = Vec::new();
    let mut for_emma = Vec::new();
    let mut failed = None;
    for (i, step) in m.steps.iter().enumerate() {
        match step {
            Step::Action { action, args } => {
                let def = actions::find(action).expect("validada");
                if actions::permission(def, &overrides) == "off" {
                    failed = Some(format!("passo {}: \"{}\" está desligada no painel da Emma", i + 1, def.label));
                    break;
                }
                let args = if args.is_null() { json!({}) } else { args.clone() };
                let outcome = Box::pin(actions::run_from(app.clone(), action, args, Origin::Automation)).await;
                if !outcome.ok {
                    failed = Some(format!("passo {} ({}): {}", i + 1, def.label, outcome.summary));
                    break;
                }
                done.push(outcome.summary);
            }
            Step::Emma { emma } if origin == Origin::Emma => for_emma.push(emma.clone()),
            Step::Emma { emma } => {
                let (handle, text) = (app.clone(), emma.clone());
                let result = tauri::async_runtime::spawn_blocking(move || crate::emma::ask(&handle, &text)).await;
                match result.map_err(|e| e.to_string()).and_then(|r| r) {
                    Ok(answer) => done.push(format!("Emma: {}", answer.chars().take(120).collect::<String>())),
                    Err(err) => {
                        failed = Some(format!("passo {} (Emma): {err}", i + 1));
                        break;
                    }
                }
            }
            Step::Wait { wait } => tokio::time::sleep(Duration::from_secs_f64(*wait)).await,
        }
    }
    let outcome = match failed {
        Some(err) => Outcome { ok: false, summary: format!("{}: {err}", m.name), content: format!("Erro na macro {}: {err}. Feito antes: {}", m.name, done.join("; ")) },
        None if !for_emma.is_empty() => Outcome {
            ok: true,
            summary: format!("{}: {} passos", m.name, done.len()),
            content: format!(
                "Macro {} rodou: {}.\nFalta você fazer agora, nesta conversa: {}",
                m.name,
                if done.is_empty() { "nada no computador".into() } else { done.join("; ") },
                for_emma.join("; ")
            ),
        },
        None => Outcome { ok: true, summary: format!("{} feita", m.name), content: format!("Macro {} rodou: {}", m.name, done.join("; ")) },
    };
    log(&LogEntry { at: now_text(), by: by.to_string(), macro_name: m.name.clone(), ok: outcome.ok, summary: outcome.summary.clone() });
    let _ = app.emit("lumna://automation", ());
    outcome
}

/// Roda a macro de uma automação em segundo plano. Falha vira aviso na tela.
fn fire(app: &AppHandle, automation: &Automation) {
    let (app, automation) = (app.clone(), automation.clone());
    tauri::async_runtime::spawn(async move {
        let by = format!("{} ({})", automation.name, describe(&automation.when));
        let outcome = run_macro(app, &automation.macro_name, Origin::Automation, &by).await;
        if !outcome.ok {
            let _ = std::process::Command::new("notify-send").args(["-a", "LUMNA", &format!("Automação \"{}\" falhou", automation.name), &outcome.summary]).status();
        }
    });
}

fn fire_where(app: &AppHandle, matches: impl Fn(&Trigger) -> bool) {
    for automation in list_automations().iter().filter(|a| a.enabled && matches(&a.when)) {
        fire(app, automation);
    }
}

// ---- gatilhos ----

#[derive(Default)]
pub struct Runner(Mutex<RunnerState>);

#[derive(Default)]
struct RunnerState {
    /// Último minuto conferido ("2026-10-13 08:00"), para cada horário disparar uma vez.
    minute: String,
    /// Classe de cada janela aberta, para saber quem fechou.
    windows: HashMap<String, String>,
    modes: HashMap<String, Mode>,
    wifi: Option<String>,
    bluetooth: HashSet<String>,
    /// Wi-Fi e Bluetooth já foram lidos uma vez (o estado do login não dispara).
    primed: bool,
}

/// O nome do gatilho bate com a janela? ("spotify" com a classe "Spotify", "navegador" com o Firefox.)
fn app_matches(wanted: &str, class: &str, apps: &[crate::apps::App]) -> bool {
    let (w, c) = (normalize(wanted), normalize(class));
    if w.is_empty() || c.is_empty() {
        return false;
    }
    if c == w || c.contains(&w) {
        return true;
    }
    actions::best(apps, wanted, |a| vec![a.name.clone(), a.id.clone(), a.keywords.clone()]).is_some_and(|a| {
        [a.wm_class.clone().unwrap_or_default(), a.id.clone(), a.name.clone()].iter().any(|k| !k.is_empty() && normalize(k) == c)
    })
}

/// Janelas abrindo e fechando (chamado pelos eventos do compositor).
pub fn compositor_event(app: &AppHandle, event: &lumna_core::CompositorEvent) {
    use lumna_core::CompositorEvent::*;
    let (opened, class) = match event {
        WindowOpened { address, class, .. } => {
            app.state::<Runner>().0.lock().unwrap().windows.insert(address.trim_start_matches("0x").to_string(), class.clone());
            (true, class.clone())
        }
        WindowClosed { address } => match app.state::<Runner>().0.lock().unwrap().windows.remove(address.trim_start_matches("0x")) {
            Some(class) => (false, class),
            None => return,
        },
        _ => return,
    };
    let automations = list_automations();
    let wanted = automations.iter().any(|a| a.enabled && matches!((&a.when, opened), (Trigger::AppOpened { .. }, true) | (Trigger::AppClosed { .. }, false)));
    if !wanted {
        return;
    }
    let apps = crate::apps::list_plain();
    fire_where(app, |t| match t {
        Trigger::AppOpened { app: name } if opened => app_matches(name, &class, &apps),
        Trigger::AppClosed { app: name } if !opened => app_matches(name, &class, &apps),
        _ => false,
    });
}

/// Modos mudaram (chamado depois de cada troca): dispara o modo em que uma tela entrou.
pub fn modes_changed(app: &AppHandle, modes: &HashMap<String, Mode>) {
    let entered: HashSet<Mode> = {
        let runner = app.state::<Runner>();
        let mut state = runner.0.lock().unwrap();
        // Tela ainda não vista estava no Desktop (o modo de toda tela ao entrar).
        let entered = modes.iter().filter(|(monitor, mode)| state.modes.get(*monitor).copied().unwrap_or_default() != **mode).map(|(_, m)| *m).collect();
        state.modes = modes.clone();
        entered
    };
    if entered.is_empty() {
        return;
    }
    fire_where(app, |t| matches!(t, Trigger::Mode { mode } if Mode::parse(mode).is_some_and(|m| entered.contains(&m))));
}

/// "2026-10-13 08:00 2" (data, hora e dia da semana 1-7).
fn clock() -> Option<(String, u8, u8, u8)> {
    let out = std::process::Command::new("date").arg("+%Y-%m-%d %H:%M %u").output().ok()?;
    let text = String::from_utf8_lossy(&out.stdout).trim().to_string();
    let mut parts = text.split(' ');
    let (date, time, weekday) = (parts.next()?, parts.next()?, parts.next()?.parse().ok()?);
    let (h, m) = parse_hhmm(time)?;
    Some((format!("{date} {time}"), h, m, weekday))
}

pub fn time_matches(t: &Trigger, hour: u8, minute: u8, weekday: u8) -> bool {
    matches!(t, Trigger::Time { at, days } if parse_hhmm(at) == Some((hour, minute)) && (days.is_empty() || days.contains(&weekday)))
}

fn tick(app: &AppHandle) {
    let automations: Vec<Automation> = list_automations().into_iter().filter(|a| a.enabled).collect();
    if let Some((stamp, hour, minute, weekday)) = clock() {
        let new_minute = {
            let runner = app.state::<Runner>();
            let mut state = runner.0.lock().unwrap();
            let new = state.minute != stamp;
            state.minute = stamp;
            new
        };
        if new_minute {
            for a in automations.iter().filter(|a| time_matches(&a.when, hour, minute, weekday)) {
                fire(app, a);
            }
        }
    }
    let wants_wifi = automations.iter().any(|a| matches!(a.when, Trigger::Wifi { .. }));
    let wants_bluetooth = automations.iter().any(|a| matches!(a.when, Trigger::Bluetooth { .. }));
    let wifi = if wants_wifi { tauri::async_runtime::block_on(crate::wifi::wifi_state()).connected } else { None };
    let bluetooth: HashSet<String> = if wants_bluetooth {
        tauri::async_runtime::block_on(crate::bluetooth::bluetooth_state()).devices.into_iter().filter(|d| d.connected).map(|d| d.name).collect()
    } else {
        HashSet::new()
    };
    let (new_wifi, new_devices) = {
        let runner = app.state::<Runner>();
        let mut state = runner.0.lock().unwrap();
        let primed = state.primed;
        let new_wifi = wifi.clone().filter(|n| primed && state.wifi.as_ref() != Some(n));
        let new_devices: Vec<String> = bluetooth.iter().filter(|d| primed && !state.bluetooth.contains(*d)).cloned().collect();
        state.wifi = wifi;
        state.bluetooth = bluetooth;
        state.primed = true;
        (new_wifi, new_devices)
    };
    if let Some(network) = new_wifi {
        fire_where(app, |t| matches!(t, Trigger::Wifi { network: n } if normalize(n) == normalize(&network)));
    }
    for device in new_devices {
        fire_where(app, |t| matches!(t, Trigger::Bluetooth { device: d } if normalize(&device).contains(&normalize(d))));
    }
}

/// Começa a vigiar: login (uma vez, depois que o shell assentou), relógio, Wi-Fi e Bluetooth.
pub fn start(app: AppHandle) {
    std::thread::spawn(move || {
        // Janelas que já estavam abertas (para o "quando fechar") e os modos de agora.
        if let Ok(windows) = app.state::<crate::SharedCompositor>().windows() {
            let runner = app.state::<Runner>();
            let mut state = runner.0.lock().unwrap();
            for w in windows {
                state.windows.insert(w.address.trim_start_matches("0x").to_string(), w.class);
            }
        }
        app.state::<Runner>().0.lock().unwrap().modes = app.state::<crate::modes::Modes>().snapshot(None).modes;
        std::thread::sleep(Duration::from_secs(8));
        fire_where(&app, |t| matches!(t, Trigger::Login));
        loop {
            tick(&app);
            std::thread::sleep(Duration::from_secs(15));
        }
    });
}

// ---- ações (chamadas pelo actions.rs) ----

fn args_str<'a>(args: &'a Value, key: &str) -> &'a str {
    args.get(key).and_then(Value::as_str).map(str::trim).unwrap_or_default()
}

pub fn action_macro_list() -> Outcome {
    let list = list_macros();
    if list.is_empty() {
        return Outcome::data("Nenhuma macro", "Nenhuma macro salva ainda (crie com macro.save).");
    }
    let lines: Vec<String> = list.iter().map(|m| format!("{} — {} ({} passos)", m.name, m.description, m.steps.len())).collect();
    Outcome::data(format!("{} macros", list.len()), lines.join("\n"))
}

pub fn action_macro_save(args: &Value) -> Outcome {
    let m = Macro {
        name: args_str(args, "name").to_string(),
        description: args_str(args, "description").to_string(),
        steps: match serde_json::from_value(args.get("steps").cloned().unwrap_or(Value::Null)) {
            Ok(steps) => steps,
            Err(e) => return Outcome::fail(format!("passos inválidos: {e}")),
        },
    };
    match save_macro(&m) {
        Ok(()) => Outcome::done(format!("Macro \"{}\" salva ({} passos)", m.name, m.steps.len())),
        Err(err) => Outcome::fail(err),
    }
}

pub fn action_macro_delete(args: &Value) -> Outcome {
    match delete_macro(args_str(args, "name")) {
        Ok(name) => Outcome::done(format!("Macro \"{name}\" apagada")),
        Err(err) => Outcome::fail(err),
    }
}

pub fn action_automation_list() -> Outcome {
    let list = list_automations();
    if list.is_empty() {
        return Outcome::data("Nenhuma automação", "Nenhuma automação ainda (crie com automation.save).");
    }
    let lines: Vec<String> = list
        .iter()
        .map(|a| format!("{} — {} → macro {}{}", a.name, describe(&a.when), a.macro_name, if a.enabled { "" } else { " (desligada)" }))
        .collect();
    Outcome::data(format!("{} automações", list.len()), lines.join("\n"))
}

pub fn action_automation_save(args: &Value) -> Outcome {
    let when: Trigger = match serde_json::from_value(args.get("when").cloned().unwrap_or(Value::Null)) {
        Ok(t) => t,
        Err(e) => return Outcome::fail(format!("gatilho inválido: {e}")),
    };
    let enabled = args.get("enabled").and_then(Value::as_bool).unwrap_or(true);
    match save_automation(args_str(args, "name"), when, args_str(args, "macro"), enabled) {
        Ok(a) => Outcome::done(format!("Automação \"{}\": {} → {}", a.name, describe(&a.when), a.macro_name)),
        Err(err) => Outcome::fail(err),
    }
}

pub fn action_automation_delete(args: &Value) -> Outcome {
    match delete_automation(args_str(args, "name")) {
        Ok(name) => Outcome::done(format!("Automação \"{name}\" apagada")),
        Err(err) => Outcome::fail(err),
    }
}

// ---- comandos do painel ----

#[derive(Serialize)]
pub struct Overview {
    macros: Vec<Macro>,
    automations: Vec<AutomationView>,
    history: Vec<LogEntry>,
}

#[derive(Serialize)]
struct AutomationView {
    #[serde(flatten)]
    automation: Automation,
    description: String,
}

#[tauri::command]
pub fn automation_overview() -> Overview {
    Overview {
        macros: list_macros(),
        automations: list_automations().into_iter().map(|a| AutomationView { description: describe(&a.when), automation: a }).collect(),
        history: history(20),
    }
}

#[tauri::command]
pub fn automation_set_enabled(id: String, enabled: bool) -> Result<(), String> {
    set_enabled(&id, enabled)
}

#[tauri::command]
pub async fn automation_run_macro(app: AppHandle, name: String) -> Outcome {
    run_macro(app, &name, Origin::User, "Painel").await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_macros_and_triggers() {
        let m: Macro = serde_json::from_str(
            r#"{"name":"Hora do filme","steps":[
                {"action":"mode.switch","args":{"mode":"media"}},
                {"emma":"luzes em 20%"},
                {"wait":2},
                {"action":"media.open_service","args":{"id":"netflix"}}]}"#,
        )
        .unwrap();
        assert_eq!(m.steps.len(), 4);
        assert!(matches!(&m.steps[1], Step::Emma { emma } if emma == "luzes em 20%"));
        assert!(matches!(m.steps[2], Step::Wait { wait } if wait == 2.0));
        assert!(validate(&m).is_ok());
        assert_eq!(slug(&m.name), "hora-do-filme");

        let a: Automation = serde_json::from_str(
            r#"{"id":"cinema","name":"Cinema","when":{"type":"mode","mode":"media"},"macro":"hora-do-filme"}"#,
        )
        .unwrap();
        assert!(a.enabled);
        assert_eq!(describe(&a.when), "Ao entrar no modo media");
        let t: Trigger = serde_json::from_str(r#"{"type":"time","at":"08:00","days":[1,2,3,4,5]}"#).unwrap();
        assert_eq!(describe(&t), "Dias úteis às 08:00");
        assert!(time_matches(&t, 8, 0, 3));
        assert!(!time_matches(&t, 8, 0, 6));
        assert!(!time_matches(&t, 8, 1, 3));
        assert!(validate_trigger(&Trigger::Time { at: "25:00".into(), days: vec![] }).is_err());
        assert!(validate_trigger(&Trigger::Mode { mode: "festa".into() }).is_err());
    }

    #[test]
    fn rejects_bad_macros() {
        let step = |action: &str| Step::Action { action: action.into(), args: json!({}) };
        let m = |steps: Vec<Step>| Macro { name: "x".into(), description: String::new(), steps };
        assert!(validate(&m(vec![])).is_err());
        assert!(validate(&m(vec![step("macro.run")])).is_err());
        assert!(validate(&m(vec![step("automation.save")])).is_err());
        assert!(validate(&m(vec![step("apps.voar")])).is_err());
        assert!(validate(&m(vec![Step::Wait { wait: 1000.0 }])).is_err());
        assert!(validate(&m(vec![step("apps.open"), Step::Wait { wait: 1.0 }])).is_ok());
        assert!(validate(&Macro { name: "!!".into(), description: String::new(), steps: vec![step("apps.open")] }).is_err());
    }

    #[test]
    fn matches_apps_to_windows() {
        let app = |name: &str, id: &str, class: Option<&str>, keywords: &str| crate::apps::App {
            id: id.into(),
            name: name.into(),
            comment: String::new(),
            icon: None,
            wm_class: class.map(str::to_string),
            exec: id.into(),
            keywords: keywords.into(),
            terminal: false,
            category: String::new(),
        };
        let apps = vec![app("Firefox", "firefox", None, "Navegador web"), app("Steam", "steam", Some("steam"), "Jogos")];
        assert!(app_matches("spotify", "Spotify", &apps));
        assert!(app_matches("navegador", "firefox", &apps));
        assert!(app_matches("Steam", "steam", &apps));
        assert!(!app_matches("steam", "firefox", &apps));
    }
}
