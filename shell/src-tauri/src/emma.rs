//! Ponte com a Emma (backend do NewHarnessV2, por padrão em 127.0.0.1:8001).
//!
//! O backend pensa e conversa; o shell executa. Cada mensagem vai com o catálogo de ações
//! (`actions::tool_defs`); quando o modelo chama uma, chega um `tool_client` pelo SSE, a ação roda
//! aqui e o resultado volta pelo `/tool-result`. Confirmações (`tool_confirm`) aparecem no painel e
//! voltam pelo `/tool-decision`.
//!
//! O HTTP vai pelo `curl` (como no AUR da Loja), sem cabeçalho Origin: o backend aceita chamadas
//! locais com token. O token e o corpo vão pela entrada padrão do curl, nunca pela linha de comando.
//!
//! Configuração em `~/.config/lumna/emma.json` (modo 600):
//! `{"url", "email", "refresh_token", "session_id", "permissions": {...}}`. Com o pacote lumna-emma,
//! a conta local fica em `~/.config/lumna/emma/backend.env` e o login é automático.

use std::io::{BufRead, BufReader, Write};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use serde::Serialize;
use serde_json::{json, Map, Value};
use tauri::{AppHandle, Emitter, Manager};

const DEFAULT_URL: &str = "http://127.0.0.1:8001";
/// O token de acesso do backend vale 60 min; renova antes.
const ACCESS_TTL: Duration = Duration::from_secs(50 * 60);

#[derive(Default)]
pub struct Emma(Mutex<Inner>);

#[derive(Default)]
struct Inner {
    access: Option<(String, Instant)>,
    /// Uma conversa por vez.
    busy: bool,
    /// A conversa que está respondendo agora (para as confirmações irem pra ela).
    current: Option<String>,
    /// Última vez que o assistente (popup) conversou: depois de um tempo parado, começa outra.
    assist_used: Option<Instant>,
}

/// De onde vem a mensagem: cada um tem a sua conversa no backend, e os eventos levam o nome
/// (o app e o popup ouvem o mesmo `lumna://emma` e cada um fica com os seus).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Channel {
    /// O app da Emma (e o painel do PC): a conversa longa, com histórico.
    App,
    /// O popup do assistente, por cima de qualquer tela.
    Assist,
    /// O popup com a tela bloqueada: conversa nova a cada vez, sem histórico, só ações seguras.
    Lock,
}

impl Channel {
    pub fn parse(name: Option<&str>) -> Channel {
        match name {
            Some("assist") => Channel::Assist,
            Some("lock") => Channel::Lock,
            _ => Channel::App,
        }
    }
    fn name(self) -> &'static str {
        match self {
            Channel::App => "app",
            Channel::Assist => "assist",
            Channel::Lock => "lock",
        }
    }
    /// Chave da conversa no `emma.json`.
    fn key(self) -> &'static str {
        match self {
            Channel::App => "session_id",
            Channel::Assist => "assist_session_id",
            Channel::Lock => "lock_session_id",
        }
    }
}

/// O popup volta pra uma conversa nova depois de 10 min sem uso (como o assistente do Android).
const ASSIST_IDLE: Duration = Duration::from_secs(10 * 60);

/// Ações que a Emma pode fazer com a tela bloqueada: nada que leia dados da pessoa ou mexa no
/// que está aberto (quem fala pode não ser o dono do celular).
pub const LOCK_SAFE: [&str; 6] = ["audio.volume", "audio.outputs", "audio.output", "media.control", "media.now_playing", "wifi.status"];

/// Diretrizes da conversa do popup (vão no `system_prompt` da sessão): resposta curta e os
/// botões de próximo passo, que o popup desenha (cartão principal, links e sugestões).
const ASSIST_RULES: &str = "MODO ASSISTENTE (popup do LUMNA, por cima do que o usuário está fazendo): responda curto e direto, \
em 1 a 3 frases, sem títulos nem listas longas. Se dá para resolver agora com uma ferramenta, use a ferramenta.\n\
Quando houver próximos passos úteis, termine a resposta com linhas neste formato exato, cada uma na sua linha:\n\
[acao: Adicionar layout]  -> o próximo passo principal (no máximo 1), curto como um botão\n\
[link: Ver layouts instalados]  -> outra ação útil (no máximo 2)\n\
[sugestao: Qual layout escolher?]  -> uma pergunta que o usuário pode querer fazer depois (no máximo 2)\n\
Cada item vira um botão: ao tocar, o texto dele chega para você como a próxima mensagem do usuário. \
Não use esse formato para mais nada e não ofereça botões quando a resposta já resolve tudo.";

const LOCK_RULES: &str = "A TELA DO CELULAR ESTÁ BLOQUEADA: quem fala pode não ser o dono do aparelho. Não revele dados pessoais \
(mensagens, contatos, arquivos, agenda, memórias, histórico de conversas) e não faça nada além das ferramentas disponíveis \
agora (volume, mídia, Wi-Fi). Para o resto, peça para desbloquear o celular.";

/// O que a pessoa está vendo, numa linha curta ("Ajustes • Teclado e entrada").
fn context_line(context: &str) -> String {
    context.split_whitespace().collect::<Vec<_>>().join(" ").chars().take(160).collect()
}

fn session_prompt(channel: Channel, context: &str) -> String {
    let mut prompt = ASSIST_RULES.to_string();
    if channel == Channel::Lock {
        prompt.push_str("\n\n");
        prompt.push_str(LOCK_RULES);
    }
    let context = context_line(context);
    if !context.is_empty() {
        prompt.push_str(&format!("\n\nO usuário está vendo agora: {context}. Use isso quando a pergunta depender da tela (\"isso\", \"aqui\")."));
    }
    prompt
}

#[derive(Debug, Clone, Serialize)]
pub struct EmmaStatus {
    pub url: String,
    pub email: String,
    /// Já tem como entrar (token salvo ou conta local do pacote lumna-emma).
    pub configured: bool,
    /// O backend respondeu.
    pub online: bool,
}

fn config_dir() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))
        .unwrap_or_else(|| PathBuf::from("/tmp"))
        .join("lumna")
}

fn config_path() -> PathBuf {
    config_dir().join("emma.json")
}

fn read_config() -> Map<String, Value> {
    std::fs::read_to_string(config_path())
        .ok()
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .and_then(|v| v.as_object().cloned())
        .unwrap_or_default()
}

/// Muda só as chaves pedidas (as permissões e o resto ficam como estão).
fn update_config(changes: &[(&str, Value)]) -> Result<(), String> {
    use std::os::unix::fs::OpenOptionsExt;
    let mut config = read_config();
    for (key, value) in changes {
        if value.is_null() {
            config.remove(*key);
        } else {
            config.insert(key.to_string(), value.clone());
        }
    }
    std::fs::create_dir_all(config_dir()).map_err(|e| e.to_string())?;
    let tmp = config_path().with_extension("json.tmp");
    let mut file = std::fs::OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(true)
        .mode(0o600)
        .open(&tmp)
        .map_err(|e| e.to_string())?;
    file.write_all(serde_json::to_string_pretty(&Value::Object(config)).unwrap_or_default().as_bytes())
        .map_err(|e| e.to_string())?;
    std::fs::rename(&tmp, config_path()).map_err(|e| e.to_string())
}

pub(crate) fn base_url() -> String {
    read_config().get("url").and_then(Value::as_str).unwrap_or(DEFAULT_URL).trim_end_matches('/').to_string()
}

/// Conta local criada pelo pacote lumna-emma (`ADMIN_EMAIL=...`, `ADMIN_PASSWORD=...`).
pub fn parse_env(text: &str) -> Option<(String, String)> {
    let mut email = None;
    let mut password = None;
    for line in text.lines() {
        let Some((key, value)) = line.trim().split_once('=') else { continue };
        let value = value.trim().trim_matches('"').trim_matches('\'').to_string();
        match key.trim() {
            "ADMIN_EMAIL" => email = Some(value),
            "ADMIN_PASSWORD" => password = Some(value),
            _ => {}
        }
    }
    Some((email?, password?)).filter(|(e, p)| !e.is_empty() && !p.is_empty())
}

fn local_account() -> Option<(String, String)> {
    parse_env(&std::fs::read_to_string(config_dir().join("emma/backend.env")).ok()?)
}

/// Texto como string entre aspas do arquivo de configuração do curl.
pub fn curl_quote(text: &str) -> String {
    let mut out = String::with_capacity(text.len() + 2);
    out.push('"');
    for c in text.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

/// Configuração do curl (lida pela entrada padrão com `-K -`).
pub fn curl_config(method: &str, url: &str, token: Option<&str>, body: Option<&Value>) -> String {
    let mut config = format!("url = {}\nrequest = {}\n", curl_quote(url), curl_quote(method));
    if let Some(token) = token {
        config.push_str(&format!("header = {}\n", curl_quote(&format!("Authorization: Bearer {token}"))));
    }
    if let Some(body) = body {
        config.push_str("header = \"Content-Type: application/json\"\n");
        config.push_str(&format!("data-binary = {}\n", curl_quote(&body.to_string())));
    }
    config
}

pub(crate) fn curl(config: &str, extra: &[&str]) -> Result<std::process::Child, String> {
    let mut child = Command::new("curl")
        .args(["-sS", "--max-time", "900", "-K", "-"])
        .args(extra)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| format!("curl: {e}"))?;
    child.stdin.take().ok_or("curl sem entrada")?.write_all(config.as_bytes()).map_err(|e| e.to_string())?;
    Ok(child)
}

/// Uma chamada comum: devolve (status HTTP, corpo).
fn request(method: &str, path: &str, token: Option<&str>, body: Option<&Value>) -> Result<(u16, Value), String> {
    let url = format!("{}{path}", base_url());
    let child = curl(&curl_config(method, &url, token, body), &["--connect-timeout", "4", "-w", "\n%{http_code}"])?;
    let out = child.wait_with_output().map_err(|e| e.to_string())?;
    if !out.status.success() {
        return Err(format!("a Emma não respondeu em {}", base_url()));
    }
    let text = String::from_utf8_lossy(&out.stdout).to_string();
    let (body, code) = text.rsplit_once('\n').unwrap_or(("", text.as_str()));
    let code: u16 = code.trim().parse().unwrap_or(0);
    Ok((code, serde_json::from_str(body).unwrap_or(Value::Null)))
}

pub(crate) fn detail(body: &Value) -> String {
    match &body["detail"] {
        Value::String(s) => s.clone(),
        Value::Null => String::new(),
        other => other.to_string(),
    }
}

impl Emma {
    /// Token de acesso válido: o guardado, renovado pelo refresh token, ou um login com a conta local.
    pub(crate) fn access_token(&self) -> Result<String, String> {
        if let Some((token, at)) = &self.0.lock().unwrap().access {
            if at.elapsed() < ACCESS_TTL {
                return Ok(token.clone());
            }
        }
        let config = read_config();
        if let Some(refresh) = config.get("refresh_token").and_then(Value::as_str) {
            let (code, body) = request("POST", "/api/auth/refresh", None, Some(&json!({ "refresh_token": refresh })))?;
            if code == 200 {
                if let Some(token) = body["access_token"].as_str() {
                    self.0.lock().unwrap().access = Some((token.to_string(), Instant::now()));
                    return Ok(token.to_string());
                }
            }
        }
        if let Some((email, password)) = local_account() {
            return self.login(&email, &password);
        }
        Err("Entre na sua conta da Emma (Painel → Emma).".into())
    }

    fn login(&self, email: &str, password: &str) -> Result<String, String> {
        let (code, body) = request("POST", "/api/auth/login", None, Some(&json!({ "email": email, "password": password, "remember": true })))?;
        if code != 200 {
            let why = detail(&body);
            return Err(if why.is_empty() { format!("A Emma recusou o login ({code}).") } else { why });
        }
        let access = body["access_token"].as_str().ok_or("resposta de login sem token")?.to_string();
        update_config(&[("email", json!(email)), ("refresh_token", body["refresh_token"].clone())])?;
        self.0.lock().unwrap().access = Some((access.clone(), Instant::now()));
        Ok(access)
    }

    fn session(&self, token: &str, channel: Channel) -> Result<String, String> {
        let stale = channel == Channel::Assist && {
            let mut inner = self.0.lock().unwrap();
            let stale = inner.assist_used.is_some_and(|at| at.elapsed() > ASSIST_IDLE);
            inner.assist_used = Some(Instant::now());
            stale
        };
        if let Some(sid) = read_config().get(channel.key()).and_then(Value::as_str).filter(|_| !stale) {
            let (code, _) = request("GET", &format!("/api/sessions/{sid}/messages"), Some(token), None)?;
            if code == 200 {
                return Ok(sid.to_string());
            }
        }
        // as do popup ficam fora da lista de conversas do NewHarnessV2 até alguém responder nelas
        let body = match channel {
            Channel::App => json!({ "title": "LUMNA" }),
            _ => json!({ "title": "LUMNA Assistente", "assist_log": true }),
        };
        let (code, body) = request("POST", "/api/sessions", Some(token), Some(&body))?;
        let sid = body["id"].as_str().filter(|_| code == 200).ok_or_else(|| format!("não deu para abrir a conversa ({code})"))?;
        update_config(&[(channel.key(), json!(sid))])?;
        Ok(sid.to_string())
    }
}

/// Uma linha do SSE (`data: {...}`) como evento.
pub fn parse_sse(line: &str) -> Option<Value> {
    let data = line.strip_prefix("data:")?.trim();
    serde_json::from_str(data).ok()
}

fn now_local() -> String {
    Command::new("date").arg("+%Y-%m-%d %H:%M (%A) %z").output().map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string()).unwrap_or_default()
}

fn emit(app: &AppHandle, channel: Channel, event: &Value) {
    let mut event = event.clone();
    if let Some(map) = event.as_object_mut() {
        map.insert("channel".into(), json!(channel.name()));
    }
    let _ = app.emit("lumna://emma", &event);
    // com a tela bloqueada ninguém confirma nada (nem pelo PC)
    if channel == Channel::Lock {
        return;
    }
    // a Emma quer confirmar uma ação: os celulares do Connect também podem responder
    if event["type"] == "tool_confirm" {
        crate::remote::broadcast(json!({
            "type": "emma-ask",
            "call_id": event["tool"]["id"],
            "label": event["label"],
            "reason": event["reason"],
        }));
    }
}

/// A tela do celular está bloqueada (no PC, nunca).
fn locked() -> bool {
    crate::mobilelock::is_locked()
}

/// O catálogo que vai pro backend: com a tela bloqueada, só as ações seguras.
fn tools_for(lock: bool) -> Vec<Value> {
    let tools = crate::actions::tool_defs();
    if !lock {
        return tools;
    }
    let safe: Vec<String> = LOCK_SAFE.iter().map(|n| crate::actions::tool_name(n)).collect();
    tools.into_iter().filter(|t| t["name"].as_str().is_some_and(|n| safe.iter().any(|s| s == n))).collect()
}

/// Conversa uma mensagem inteira: manda, repassa os eventos ao painel e executa as ações.
/// Devolve a resposta final da Emma.
fn converse(app: &AppHandle, text: &str, voice: bool, channel: Channel, context: &str) -> Result<String, String> {
    // tela bloqueada: vale o modo bloqueado, venha de onde vier
    let channel = if locked() && channel != Channel::App { Channel::Lock } else { channel };
    let emma = app.state::<Emma>();
    let token = emma.access_token()?;
    let sid = emma.session(&token, channel)?;
    emma.0.lock().unwrap().current = Some(sid.clone());
    if channel != Channel::App {
        // as regras do popup e a tela de agora (muda a cada mensagem)
        let prompt = json!({ "system_prompt": session_prompt(channel, context) });
        if let Err(err) = request("PATCH", &format!("/api/sessions/{sid}"), Some(&token), Some(&prompt)) {
            eprintln!("lumna-shell: emma prompt do assistente: {err}");
        }
    }
    let body = json!({
        "content": text,
        "mode": "chat",
        "voice": voice,
        "client_now": now_local(),
        "lumna_tools": tools_for(channel == Channel::Lock || locked()),
    });
    let url = format!("{}/api/sessions/{sid}/chat", base_url());
    let mut child = curl(&curl_config("POST", &url, Some(&token), Some(&body)), &["-N", "--fail-with-body"])?;
    let stdout = child.stdout.take().ok_or("curl sem saída")?;
    let mut got_any = false;
    let mut answer = String::new();
    // O que não é SSE (o corpo de um erro, com --fail-with-body).
    let mut other = String::new();
    for line in BufReader::new(stdout).lines().map_while(Result::ok) {
        let Some(event) = parse_sse(&line) else {
            if other.len() < 4000 {
                other.push_str(&line);
            }
            continue;
        };
        got_any = true;
        emit(app, channel, &event);
        // bloqueado: o que pede confirmação (as ferramentas do próprio backend) é negado na hora
        if event["type"] == "tool_confirm" && (channel == Channel::Lock || locked()) {
            let decision = json!({ "call_id": event["tool"]["id"], "allow": false });
            let _ = request("POST", &format!("/api/sessions/{sid}/tool-decision"), Some(&token), Some(&decision));
        }
        if event["type"] == "done" {
            answer = event["message"]["content"].as_str().unwrap_or_default().to_string();
        }
        if event["type"] == "tool_client" {
            let tool = &event["tool"];
            let call_id = tool["id"].as_str().unwrap_or_default().to_string();
            let name = tool["name"].as_str().unwrap_or_default().to_string();
            let args = tool["args"].clone();
            let safe = crate::actions::from_tool_name(&name).is_some_and(|n| LOCK_SAFE.contains(&n));
            let outcome = if locked() && !safe {
                crate::actions::Outcome::fail("o celular está bloqueado: desbloqueie para a Emma fazer isso".to_string())
            } else {
                tauri::async_runtime::block_on(crate::actions::actions_run_tool(app.clone(), name, if args.is_null() { json!({}) } else { args }))
            };
            emit(app, channel, &json!({ "type": "tool_ran", "id": call_id, "ok": outcome.ok, "summary": outcome.summary }));
            let content: String = outcome.content.chars().take(4000).collect();
            let summary: String = outcome.summary.chars().take(300).collect();
            let result = json!({ "call_id": call_id, "ok": outcome.ok, "summary": summary, "content": content });
            if let Err(err) = request("POST", &format!("/api/sessions/{sid}/tool-result"), Some(&token), Some(&result)) {
                eprintln!("lumna-shell: emma tool-result: {err}");
            }
        }
    }
    let out = child.wait_with_output().map_err(|e| e.to_string())?;
    if !out.status.success() && !got_any {
        // 401: o token caiu (senha trocada, servidor reiniciado); a próxima tenta de novo do zero.
        emma.0.lock().unwrap().access = None;
        let err = String::from_utf8_lossy(&out.stderr);
        let why = serde_json::from_str::<Value>(&other).map(|body| detail(&body)).unwrap_or_default();
        return Err(if err.contains("401") || err.contains("403") {
            "A Emma pediu login de novo (Painel → Emma).".into()
        } else if !why.is_empty() {
            why
        } else {
            format!("A Emma não respondeu em {}.", base_url())
        });
    }
    emma.0.lock().unwrap().current = None;
    Ok(answer)
}

/// Uma conversa por vez: marca a Emma como ocupada enquanto `f` roda.
fn exclusive<T>(app: &AppHandle, f: impl FnOnce() -> Result<T, String>) -> Result<T, String> {
    {
        let emma = app.state::<Emma>();
        let mut inner = emma.0.lock().unwrap();
        if inner.busy {
            return Err("A Emma ainda está respondendo.".into());
        }
        inner.busy = true;
    }
    let result = f();
    app.state::<Emma>().0.lock().unwrap().busy = false;
    result
}

/// Pede algo à Emma por fora do painel (passo "emma" de uma macro). Bloqueia até ela responder.
pub fn ask(app: &AppHandle, text: &str) -> Result<String, String> {
    exclusive(app, || converse(app, text, false, Channel::App, ""))
}

// ---- comandos do painel ----

#[tauri::command]
pub async fn emma_status() -> EmmaStatus {
    tauri::async_runtime::spawn_blocking(|| {
        let config = read_config();
        let online = request("GET", "/api/", None, None).is_ok_and(|(code, _)| code > 0 && code < 500);
        EmmaStatus {
            url: base_url(),
            email: config.get("email").and_then(Value::as_str).unwrap_or_default().to_string(),
            configured: config.contains_key("refresh_token") || local_account().is_some(),
            online,
        }
    })
    .await
    .unwrap_or(EmmaStatus { url: base_url(), email: String::new(), configured: false, online: false })
}

#[tauri::command]
pub async fn emma_login(app: AppHandle, url: String, email: String, password: String) -> Result<(), String> {
    let url = url.trim().trim_end_matches('/').to_string();
    if !(url.starts_with("http://") || url.starts_with("https://")) {
        return Err("O endereço começa com http:// ou https://".into());
    }
    tauri::async_runtime::spawn_blocking(move || {
        update_config(&[("url", json!(url)), ("session_id", Value::Null)])?;
        app.state::<Emma>().login(email.trim(), &password).map(|_| ())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn emma_logout(app: AppHandle) -> Result<(), String> {
    app.state::<Emma>().0.lock().unwrap().access = None;
    update_config(&[("refresh_token", Value::Null), ("session_id", Value::Null)])
}

/// Manda uma mensagem. A resposta chega aos poucos pelo evento `lumna://emma` (com o `channel`).
/// `channel`: "app" (padrão), "assist" (o popup); `context`: o que está na tela, pro popup.
#[tauri::command]
pub async fn emma_send(app: AppHandle, text: String, voice: bool, channel: Option<String>, context: Option<String>) -> Result<(), String> {
    let text = text.trim().to_string();
    if text.is_empty() {
        return Ok(());
    }
    let channel = Channel::parse(channel.as_deref());
    let context = context.unwrap_or_default();
    let worker = app.clone();
    let result =
        tauri::async_runtime::spawn_blocking(move || exclusive(&worker, || converse(&worker, &text, voice, channel, &context).map(|_| ())))
            .await
            .map_err(|e| e.to_string())
            .and_then(|r| r);
    if let Err(err) = &result {
        let channel = if locked() && channel != Channel::App { Channel::Lock } else { channel };
        emit(&app, channel, &json!({ "type": "error", "message": err }));
    }
    result
}

/// A pessoa respondeu a uma confirmação (`tool_confirm`).
#[tauri::command]
pub async fn emma_decide(app: AppHandle, call_id: String, allow: bool) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        if locked() && allow {
            return Err("desbloqueie o celular para permitir".into());
        }
        let emma = app.state::<Emma>();
        let token = emma.access_token()?;
        let current = emma.0.lock().unwrap().current.clone();
        let sid = current.or_else(|| read_config().get("session_id").and_then(Value::as_str).map(str::to_string)).ok_or("sem conversa aberta")?;
        let (code, body) = request("POST", &format!("/api/sessions/{sid}/tool-decision"), Some(&token), Some(&json!({ "call_id": call_id, "allow": allow })))?;
        if code == 200 {
            Ok(())
        } else {
            Err(detail(&body))
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Começa uma conversa nova (a próxima mensagem abre outra sessão no backend).
#[tauri::command]
pub fn emma_new_conversation(channel: Option<String>) -> Result<(), String> {
    update_config(&[(Channel::parse(channel.as_deref()).key(), Value::Null)])
}

/// Mensagens da conversa atual, para o painel abrir onde parou (bloqueado: nada).
#[tauri::command]
pub async fn emma_history(app: AppHandle, channel: Option<String>) -> Vec<Value> {
    let channel = Channel::parse(channel.as_deref());
    if channel == Channel::Lock || (locked() && channel == Channel::Assist) {
        return Vec::new();
    }
    tauri::async_runtime::spawn_blocking(move || {
        let sid = read_config().get(channel.key()).and_then(Value::as_str)?.to_string();
        let token = app.state::<Emma>().access_token().ok()?;
        let (code, body) = request("GET", &format!("/api/sessions/{sid}/messages"), Some(&token), None).ok()?;
        let messages = body.as_array().filter(|_| code == 200)?;
        Some(
            messages
                .iter()
                .rev()
                .take(40)
                .rev()
                .map(|m| json!({ "role": m["role"], "content": m["content"], "tools": m["tools"] }))
                .collect(),
        )
    })
    .await
    .ok()
    .flatten()
    .unwrap_or_default()
}

/// Provedores de IA: os modelos prontos do backend e o que está em uso para conversar.
#[tauri::command]
pub async fn emma_providers(app: AppHandle) -> Result<Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let token = app.state::<Emma>().access_token()?;
        let (_, presets) = request("GET", "/api/providers/presets", Some(&token), None)?;
        let (_, settings) = request("GET", "/api/settings", Some(&token), None)?;
        let (_, providers) = request("GET", "/api/providers", Some(&token), None)?;
        let chat = &settings["routing"]["chat"];
        let current = providers
            .as_array()
            .into_iter()
            .flatten()
            .find(|p| p["id"] == chat["provider_id"])
            .map(|p| json!({ "name": p["name"], "model": chat["model"], "base_url": p["base_url"] }));
        Ok(json!({ "presets": presets, "current": current }))
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Cadastra o provedor (com a chave) e passa a conversar por ele.
#[tauri::command]
pub async fn emma_set_provider(app: AppHandle, name: String, base_url: String, api_key: String, model: String) -> Result<(), String> {
    if model.trim().is_empty() {
        return Err("Escolha o modelo.".into());
    }
    tauri::async_runtime::spawn_blocking(move || {
        let token = app.state::<Emma>().access_token()?;
        let body = json!({ "name": name.trim(), "base_url": base_url.trim(), "api_key": api_key.trim(), "models": [model.trim()] });
        let (code, provider) = request("POST", "/api/providers", Some(&token), Some(&body))?;
        let id = provider["id"].as_str().filter(|_| code == 200).ok_or_else(|| {
            let why = detail(&provider);
            if why.is_empty() { format!("O servidor recusou o provedor ({code}).") } else { why }
        })?;
        let routing = json!({ "routing": { "chat": { "provider_id": id, "model": model.trim() } } });
        let (code, body) = request("PUT", "/api/settings", Some(&token), Some(&routing))?;
        if code == 200 {
            Ok(())
        } else {
            Err(detail(&body))
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Persona e voz (o Perfil do app da Emma): o `/api/settings` do backend, só com o que o app
/// mostra. As chaves nunca voltam (o backend manda só `has_key`).
#[tauri::command]
pub async fn emma_settings(app: AppHandle) -> Result<Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let token = app.state::<Emma>().access_token()?;
        let (code, settings) = request("GET", "/api/settings", Some(&token), None)?;
        if code != 200 {
            return Err(detail(&settings));
        }
        let (_, local) = request("GET", "/api/local-voice", Some(&token), None).unwrap_or((0, Value::Null));
        Ok(json!({
            "personality": settings["personality"],
            "system_prompt": settings["system_prompt"],
            "voice": settings["voice"],
            "local_voice": local,
        }))
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Salva a persona e/ou a voz. `voice` vai como está (o backend junta com o que já tinha); só
/// os campos conhecidos passam.
#[tauri::command]
pub async fn emma_save_settings(app: AppHandle, personality: Option<String>, system_prompt: Option<String>, voice: Option<Value>) -> Result<(), String> {
    const VOICE_KEYS: [&str; 7] = ["api_key", "tts_engine", "stt_engine", "whisper_model", "local_voice", "voice_id", "auto_read"];
    let mut body = Map::new();
    if let Some(p) = personality {
        body.insert("personality".into(), json!(p.chars().take(8000).collect::<String>()));
    }
    if let Some(p) = system_prompt {
        body.insert("system_prompt".into(), json!(p.chars().take(8000).collect::<String>()));
    }
    if let Some(Value::Object(v)) = voice {
        let v: Map<String, Value> = v.into_iter().filter(|(k, _)| VOICE_KEYS.contains(&k.as_str())).collect();
        body.insert("voice".into(), Value::Object(v));
    }
    tauri::async_runtime::spawn_blocking(move || {
        let token = app.state::<Emma>().access_token()?;
        let (code, reply) = request("PUT", "/api/settings", Some(&token), Some(&Value::Object(body)))?;
        if code == 200 {
            Ok(())
        } else {
            Err(detail(&reply))
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Instala a voz local do backend (só no PC): "stt" (Whisper) ou "tts" (Pocket TTS).
#[tauri::command]
pub async fn emma_install_voice(app: AppHandle, what: String) -> Result<(), String> {
    if !["stt", "tts"].contains(&what.as_str()) {
        return Err("escolha stt ou tts".into());
    }
    tauri::async_runtime::spawn_blocking(move || {
        let token = app.state::<Emma>().access_token()?;
        let (code, reply) = request("POST", "/api/local-voice/install", Some(&token), Some(&json!({ "what": what })))?;
        if code == 200 {
            Ok(())
        } else {
            Err(detail(&reply))
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Permissões das ações, para o Painel → Emma.
#[derive(Debug, Clone, Serialize)]
pub struct ActionPermission {
    pub name: &'static str,
    pub label: &'static str,
    pub description: &'static str,
    pub risk: crate::actions::Risk,
    pub permission: &'static str,
}

#[tauri::command]
pub fn emma_permissions() -> Vec<ActionPermission> {
    let overrides = crate::actions::overrides();
    crate::actions::CATALOG
        .iter()
        .map(|d| ActionPermission {
            name: d.name,
            label: d.label,
            description: d.description,
            risk: d.risk,
            permission: crate::actions::permission(d, &overrides),
        })
        .collect()
}

#[tauri::command]
pub fn emma_set_permission(name: String, permission: String) -> Result<(), String> {
    if crate::actions::find(&name).is_none() || !["auto", "ask", "off"].contains(&permission.as_str()) {
        return Err("permissão inválida".into());
    }
    let mut permissions = read_config().get("permissions").and_then(Value::as_object).cloned().unwrap_or_default();
    permissions.insert(name, json!(permission));
    update_config(&[("permissions", Value::Object(permissions))])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn curl_config_escapes() {
        let body = json!({ "content": "diz \"oi\"\\ agora" });
        let config = curl_config("POST", "http://127.0.0.1:8001/api/x", Some("abc"), Some(&body));
        assert!(config.contains("url = \"http://127.0.0.1:8001/api/x\"\n"));
        assert!(config.contains("header = \"Authorization: Bearer abc\"\n"));
        // O JSON ({"content":"diz \"oi\"\\ agora"}) com cada \ e " escapados de novo para o curl.
        assert!(config.contains(r#"data-binary = "{\"content\":\"diz \\\"oi\\\"\\\\ agora\"}""#), "{config}");
        assert_eq!(curl_quote("a\nb"), "\"a\\nb\"");
    }

    #[test]
    fn assist_prompt() {
        let p = session_prompt(Channel::Assist, "Ajustes •\n Teclado   e entrada");
        assert!(p.contains("[acao: "));
        assert!(p.contains("O usuário está vendo agora: Ajustes • Teclado e entrada."));
        assert!(!p.contains("BLOQUEADA"));
        assert!(session_prompt(Channel::Lock, "").contains("BLOQUEADA"));
        assert_eq!(Channel::parse(Some("assist")), Channel::Assist);
        assert_eq!(Channel::parse(None), Channel::App);
    }

    #[test]
    fn reads_sse_and_env() {
        assert_eq!(parse_sse("data: {\"type\":\"delta\",\"content\":\"Oi\"}").unwrap()["content"], "Oi");
        assert!(parse_sse(": aguardando").is_none());
        assert!(parse_sse("").is_none());
        let env = "# conta local\nADMIN_EMAIL=emma@lumna.local\nADMIN_PASSWORD='s3cr3t=x'\nOUTRA=1\n";
        assert_eq!(parse_env(env), Some(("emma@lumna.local".into(), "s3cr3t=x".into())));
        assert_eq!(parse_env("ADMIN_EMAIL=a@b"), None);
    }
}
