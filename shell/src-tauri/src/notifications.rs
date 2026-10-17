//! Servidor de notificações do desktop (`org.freedesktop.Notifications`, o que o `notify-send`,
//! o Firefox e os outros apps usam). Cada notificação vira um evento para a janela de avisos.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::OnceLock;

use serde::Serialize;
use tauri::{AppHandle, Emitter};
use zbus::zvariant::OwnedValue;

const PATH: &str = "/org/freedesktop/Notifications";
const INTERFACE: &str = "org.freedesktop.Notifications";

/// Motivos de fechamento definidos pela especificação (a interface manda 1, expirou, e 2, fechada).
pub const CLOSED_DISMISSED: u32 = 2;
pub const CLOSED_BY_CALL: u32 = 3;

#[derive(Debug, Clone, Serialize)]
pub struct Notification {
    pub id: u32,
    pub app_name: String,
    pub summary: String,
    pub body: String,
    /// Pares (chave, texto do botão). A chave `default` é o clique no aviso.
    pub actions: Vec<(String, String)>,
    /// Milissegundos; 0 = fica até ser fechada.
    pub timeout: u32,
    pub urgent: bool,
    /// Chegou no "não perturbe": vai pra central (celular) sem aparecer por cima.
    pub quiet: bool,
}

static CONNECTION: OnceLock<zbus::Connection> = OnceLock::new();

struct Server {
    app: AppHandle,
    next_id: AtomicU32,
}

#[zbus::interface(name = "org.freedesktop.Notifications")]
impl Server {
    fn get_capabilities(&self) -> Vec<&str> {
        vec!["body", "actions", "persistence"]
    }

    #[allow(clippy::too_many_arguments)]
    fn notify(
        &self,
        app_name: String,
        replaces_id: u32,
        _app_icon: String,
        summary: String,
        body: String,
        actions: Vec<String>,
        hints: HashMap<String, OwnedValue>,
        expire_timeout: i32,
    ) -> u32 {
        let id = if replaces_id > 0 { replaces_id } else { self.next_id.fetch_add(1, Ordering::Relaxed) };
        let urgent = hints.get("urgency").and_then(|v| u8::try_from(v).ok()) == Some(2);
        let timeout = match expire_timeout {
            0 => 0,
            t if t < 0 && urgent => 0,
            t if t < 0 => 5000,
            t => t as u32,
        };
        let notification = Notification {
            id,
            app_name,
            summary,
            body,
            actions: actions.chunks(2).filter(|c| c.len() == 2).map(|c| (c[0].clone(), c[1].clone())).collect(),
            timeout,
            urgent,
            // Gaming, Mídia e o não perturbe do celular: só avisos urgentes aparecem.
            quiet: !urgent && crate::do_not_disturb(&self.app),
        };
        let quiet = notification.quiet;
        // e pros celulares do Connect
        crate::remote::broadcast(serde_json::json!({
            "type": "notification",
            "app": notification.app_name,
            "summary": notification.summary,
            "body": notification.body,
            "urgent": notification.urgent,
        }));
        let _ = self.app.emit("lumna://notification", notification);
        if !quiet {
            crate::show_notifications(&self.app);
        }
        id
    }

    fn close_notification(&self, id: u32) {
        let _ = self.app.emit("lumna://notification-close", id);
        closed(id, CLOSED_BY_CALL);
    }

    fn get_server_information(&self) -> (&str, &str, &str, &str) {
        ("LUMNA", "LUMNA OS", env!("CARGO_PKG_VERSION"), "1.2")
    }
}

/// Registra o servidor no barramento da sessão. Se outro serviço de avisos já estiver rodando,
/// só registra o erro: o resto do shell continua funcionando.
pub fn start(app: AppHandle) {
    tauri::async_runtime::spawn(async move {
        let server = Server { app, next_id: AtomicU32::new(1) };
        let connection = zbus::connection::Builder::session()
            .and_then(|b| b.name(INTERFACE))
            .and_then(|b| b.serve_at(PATH, server));
        match connection {
            Ok(builder) => match builder.build().await {
                Ok(connection) => {
                    let _ = CONNECTION.set(connection);
                }
                Err(err) => eprintln!("lumna-shell: notificações: {err}"),
            },
            Err(err) => eprintln!("lumna-shell: notificações: {err}"),
        }
    });
}

fn emit_signal(member: &'static str, body: impl serde::Serialize + zbus::zvariant::DynamicType + Send + Sync + 'static) {
    let Some(connection) = CONNECTION.get().cloned() else { return };
    tauri::async_runtime::spawn(async move {
        if let Err(err) = connection.emit_signal(None::<&str>, PATH, INTERFACE, member, &body).await {
            eprintln!("lumna-shell: sinal {member}: {err}");
        }
    });
}

/// Notificação criada pelo próprio shell (avisos do PC pelo Connect), como se viesse de um app.
pub fn local(app: &AppHandle, app_name: &str, summary: &str, body: &str, urgent: bool) {
    static NEXT: AtomicU32 = AtomicU32::new(1_000_000);
    let notification = Notification {
        id: NEXT.fetch_add(1, Ordering::Relaxed),
        app_name: app_name.to_string(),
        summary: summary.to_string(),
        body: body.to_string(),
        actions: Vec::new(),
        timeout: if urgent { 0 } else { 6000 },
        urgent,
        quiet: !urgent && crate::do_not_disturb(app),
    };
    let quiet = notification.quiet;
    let _ = app.emit("lumna://notification", notification);
    if !quiet {
        crate::show_notifications(app);
    }
}

pub fn closed(id: u32, reason: u32) {
    emit_signal("NotificationClosed", (id, reason));
}

pub fn action(id: u32, key: String) {
    emit_signal("ActionInvoked", (id, key));
}
