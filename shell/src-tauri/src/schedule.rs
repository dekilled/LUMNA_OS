//! Agendamentos do Connect: tarefas que o PC faz numa hora marcada, criadas no próprio PC
//! (Configurações → Celular) ou de longe pelo celular. Desligar, reiniciar, suspender, bloquear,
//! um lembrete (aparece no PC e nos celulares) ou um pedido pra Emma, que avisa quando terminar.
//!
//! Ficam em `~/.config/lumna/schedule.json`; o shell confere a cada 15 s. Tarefa que venceu com
//! o PC desligado roda assim que ele voltar, se não passou de 1 hora (senão só é descartada,
//! ou empurrada pro próximo dia, se repete).

use std::path::PathBuf;
use std::sync::Mutex;
use std::time::Duration;

use serde::{Deserialize, Serialize};
use serde_json::json;
use tauri::{AppHandle, Emitter};

const DAY: u64 = 86_400;
const LATE: u64 = 3_600;
const ACTIONS: [&str; 6] = ["poweroff", "reboot", "suspend", "lock", "remind", "emma"];

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Task {
    #[serde(default)]
    pub id: String,
    /// Segundos Unix.
    pub at: u64,
    pub action: String,
    /// O lembrete, ou o pedido pra Emma.
    #[serde(default)]
    pub text: String,
    /// Repete todo dia nesse horário.
    #[serde(default)]
    pub daily: bool,
    /// Quem criou ("Este PC" ou o nome do celular).
    #[serde(default)]
    pub by: String,
}

static LOCK: Mutex<()> = Mutex::new(());

fn path() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna/schedule.json")
}

fn load() -> Vec<Task> {
    std::fs::read_to_string(path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

fn save(list: &[Task]) -> Result<(), String> {
    let path = path();
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(&path, serde_json::to_string_pretty(list).map_err(|e| e.to_string())?).map_err(|e| e.to_string())
}

fn now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_or(0, |d| d.as_secs())
}

pub fn list() -> Vec<Task> {
    let mut l = load();
    l.sort_by_key(|t| t.at);
    l
}

pub fn add(app: &AppHandle, mut task: Task, by: &str) -> Result<Task, String> {
    if !ACTIONS.contains(&task.action.as_str()) {
        return Err("ação desconhecida".into());
    }
    if matches!(task.action.as_str(), "remind" | "emma") && task.text.trim().is_empty() {
        return Err(if task.action == "emma" { "diga o que a Emma deve fazer".into() } else { "escreva o lembrete".into() });
    }
    if task.text.len() > 2000 {
        return Err("texto longo demais".into());
    }
    let now = now();
    if task.at + 60 < now {
        if !task.daily {
            return Err("esse horário já passou".into());
        }
        while task.at <= now {
            task.at += DAY;
        }
    }
    task.id = crate::link::new_id();
    task.by = by.to_string();
    let _guard = LOCK.lock().unwrap();
    let mut l = load();
    l.push(task.clone());
    save(&l)?;
    let _ = app.emit("lumna://schedule", list_unlocked(l));
    Ok(task)
}

fn list_unlocked(mut l: Vec<Task>) -> Vec<Task> {
    l.sort_by_key(|t| t.at);
    l
}

pub fn remove(app: &AppHandle, id: &str) -> Result<(), String> {
    let _guard = LOCK.lock().unwrap();
    let mut l = load();
    l.retain(|t| t.id != id);
    save(&l)?;
    let _ = app.emit("lumna://schedule", list_unlocked(l));
    Ok(())
}

/// Tira da lista o que venceu (as diárias vão pro próximo dia) e devolve o que deve rodar agora.
fn take_due(list: &mut Vec<Task>, now: u64) -> Vec<Task> {
    let mut due = Vec::new();
    list.retain_mut(|t| {
        if t.at > now {
            return true;
        }
        if now - t.at <= LATE {
            due.push(t.clone());
        }
        if t.daily {
            while t.at <= now {
                t.at += DAY;
            }
            true
        } else {
            false
        }
    });
    due
}

fn tell(app: &AppHandle, summary: &str, body: &str, urgent: bool) {
    crate::notifications::local(app, "Agendamento", summary, body, urgent);
    crate::remote::broadcast(json!({ "type": "notification", "app": "Agendamento", "summary": summary, "body": body, "urgent": urgent }));
}

fn run(app: &AppHandle, task: &Task) {
    match task.action.as_str() {
        "remind" => tell(app, "Lembrete", &task.text, true),
        "emma" => {
            tell(app, "A Emma começou uma tarefa agendada", &task.text, false);
            let result = crate::emma::ask(app, &task.text);
            let (summary, body) = match result {
                Ok(reply) => ("A Emma terminou", reply),
                Err(e) => ("A Emma não conseguiu", e),
            };
            let body: String = body.chars().take(400).collect();
            tell(app, summary, &body, true);
        }
        action => {
            let label = match action {
                "poweroff" => "Desligando o PC",
                "reboot" => "Reiniciando o PC",
                "suspend" => "Suspendendo o PC",
                _ => "Bloqueando o PC",
            };
            tell(app, label, "Como estava agendado.", false);
            // dá tempo do aviso chegar nos celulares antes de a rede cair
            std::thread::sleep(Duration::from_secs(3));
            if let Err(e) = tauri::async_runtime::block_on(crate::power::power_action(app.clone(), action.to_string())) {
                tell(app, "O agendamento falhou", &e, true);
            }
        }
    }
}

pub fn start(app: AppHandle) {
    std::thread::spawn(move || loop {
        let due = {
            let _guard = LOCK.lock().unwrap();
            let mut l = load();
            let due = take_due(&mut l, now());
            if !due.is_empty() {
                if let Err(e) = save(&l) {
                    eprintln!("lumna-shell: agendamentos: {e}");
                }
                let _ = app.emit("lumna://schedule", list_unlocked(l));
            }
            due
        };
        for task in due {
            let app = app.clone();
            std::thread::spawn(move || run(&app, &task));
        }
        std::thread::sleep(Duration::from_secs(15));
    });
}

// ---- comandos da interface (no PC) ----

#[tauri::command]
pub fn schedule_list() -> Vec<Task> {
    list()
}

#[tauri::command]
pub fn schedule_add(app: AppHandle, task: Task) -> Result<Task, String> {
    add(&app, task, "Este PC")
}

#[tauri::command]
pub fn schedule_remove(app: AppHandle, id: String) -> Result<(), String> {
    remove(&app, &id)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn task(at: u64, daily: bool) -> Task {
        Task { id: at.to_string(), at, action: "remind".into(), text: "x".into(), daily, by: String::new() }
    }

    #[test]
    fn vence_na_hora() {
        let mut l = vec![task(100, false), task(5000, false)];
        let due = take_due(&mut l, 200);
        assert_eq!(due.len(), 1);
        assert_eq!(l, vec![task(5000, false)]);
    }

    #[test]
    fn diaria_vai_pro_dia_seguinte() {
        let mut l = vec![task(1000, true)];
        let due = take_due(&mut l, 1010);
        assert_eq!(due.len(), 1);
        assert_eq!(l[0].at, 1000 + DAY);
    }

    #[test]
    fn atrasada_demais_nao_roda() {
        let mut l = vec![task(1000, false), task(1000, true)];
        let due = take_due(&mut l, 1000 + LATE + DAY * 2 + 5);
        assert!(due.is_empty());
        assert_eq!(l.len(), 1);
        assert!(l[0].at > 1000 + LATE + DAY * 2 + 5);
    }
}
