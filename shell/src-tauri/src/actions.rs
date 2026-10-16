//! Ações do LUMNA: o que a Emma (e o `lumna-ctl action`, as macros e os gatilhos) pode fazer no
//! computador. Cada ação tem nome (`apps.open`), descrição para o modelo, parâmetros em JSON Schema,
//! risco e permissão. Ver docs/EMMA.md.
//!
//! Para a Emma, cada ação vira uma ferramenta `lumna_<grupo>_<ação>` (`lumna_apps_open`): o shell
//! manda o catálogo a cada mensagem, o backend oferece ao modelo e devolve as chamadas para o shell
//! executar aqui.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use serde::Serialize;
use serde_json::{json, Value};
use tauri::{AppHandle, Manager};

use crate::modes::{Mode, Modes, Target};
use crate::{apps, games, media, SharedCompositor, TERMINAL};

#[derive(Debug, Clone, Copy, Serialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum Risk {
    /// Só lê (listar apps, o que está tocando...).
    Read,
    /// Muda algo, mas se desfaz fácil (abrir app, volume, modo).
    Act,
    /// Difícil de desfazer: desligar, instalar, rodar comando. Sempre pergunta.
    Danger,
}

pub struct Def {
    pub name: &'static str,
    pub label: &'static str,
    pub description: &'static str,
    /// JSON Schema dos argumentos (só as propriedades; vira `{"type":"object",...}`).
    pub params: &'static str,
    pub required: &'static [&'static str],
    pub risk: Risk,
    /// `auto`, `ask` ou `off` (a pessoa muda no Painel → Emma).
    pub default: &'static str,
}

const NONE: &str = "{}";

pub const CATALOG: &[Def] = &[
    // ---- apps ----
    Def {
        name: "apps.list",
        label: "Listar programas",
        description: "Lista os programas instalados no computador (inclusive Flatpak e AUR). Filtre com query.",
        params: r#"{"query":{"type":"string","description":"Parte do nome ou do tipo (ex.: navegador, editor)"}}"#,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "apps.open",
        label: "Abrir programa",
        description: "Abre um programa instalado pelo nome aproximado (\"firefox\", \"spotify\", \"editor de texto\").",
        params: r#"{"name":{"type":"string","description":"Nome do programa"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "apps.close",
        label: "Fechar programa",
        description: "Fecha as janelas de um programa aberto (pede para ele fechar, como Super+Q).",
        params: r#"{"name":{"type":"string","description":"Nome do programa ou título da janela"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "apps.focus",
        label: "Mostrar programa",
        description: "Traz para a frente a janela de um programa que já está aberto.",
        params: r#"{"name":{"type":"string","description":"Nome do programa ou título da janela"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    // ---- janelas e modos ----
    Def {
        name: "windows.list",
        label: "Listar janelas",
        description: "Lista as janelas abertas (programa, título e área de trabalho).",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "windows.move",
        label: "Mover janela",
        description: "Leva a janela de um programa para outra área de trabalho (1 a 9).",
        params: r#"{"name":{"type":"string","description":"Programa ou título da janela"},"workspace":{"type":"integer","minimum":1,"maximum":9}}"#,
        required: &["name", "workspace"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "windows.fullscreen",
        label: "Tela cheia",
        description: "Liga ou desliga a tela cheia da janela de um programa.",
        params: r#"{"name":{"type":"string","description":"Programa ou título da janela"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "mode.current",
        label: "Modo atual",
        description: "Diz em qual modo o LUMNA está (desktop, gaming ou media) em cada tela.",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "mode.switch",
        label: "Trocar de modo",
        description: "Troca o modo do LUMNA: desktop (trabalho), gaming (jogos, controle) ou media (TV, filmes).",
        params: r#"{"mode":{"type":"string","enum":["desktop","gaming","media"]}}"#,
        required: &["mode"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "panel.open",
        label: "Abrir tela do LUMNA",
        description: "Abre uma tela do sistema: configuracoes (página: rede, bluetooth, som, impressoras, sistema), loja (aba: descobrir, instalados, atualizacoes), arquivos ou inicio (menu de apps).",
        params: r#"{"panel":{"type":"string","enum":["configuracoes","loja","arquivos","inicio"]},"page":{"type":"string","description":"Página ou aba (opcional)"}}"#,
        required: &["panel"],
        risk: Risk::Act,
        default: "auto",
    },
    // ---- som e mídia ----
    Def {
        name: "audio.volume",
        label: "Volume",
        description: "Ajusta o volume da saída de som: percent (0-100), change (+/- pontos) ou mute (true/false).",
        params: r#"{"percent":{"type":"integer","minimum":0,"maximum":100},"change":{"type":"integer","minimum":-100,"maximum":100},"mute":{"type":"boolean"}}"#,
        required: &[],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "audio.outputs",
        label: "Saídas de som",
        description: "Lista as saídas de som (caixas, fone, HDMI) e qual está em uso.",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "audio.output",
        label: "Trocar saída de som",
        description: "Muda a saída de som pelo nome aproximado (\"fone\", \"HDMI\", \"caixa\").",
        params: r#"{"name":{"type":"string"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "media.control",
        label: "Controlar mídia",
        description: "Controla o que está tocando (Spotify, navegador, mpv...): play_pause, next, previous ou stop.",
        params: r#"{"action":{"type":"string","enum":["play_pause","next","previous","stop"]}}"#,
        required: &["action"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "media.now_playing",
        label: "O que está tocando",
        description: "Diz o que está tocando agora (título, artista, programa).",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "media.open_service",
        label: "Abrir streaming",
        description: "Abre um serviço de streaming em tela cheia, no modo Mídia da tela em foco: youtube, netflix, prime, disney, paramount ou music.",
        params: r#"{"id":{"type":"string","enum":["youtube","netflix","prime","disney","paramount","music"]}}"#,
        required: &["id"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "media.play_video",
        label: "Tocar vídeo",
        description: "Toca um vídeo da pasta Vídeos pelo nome aproximado (continua de onde parou).",
        params: r#"{"name":{"type":"string"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "games.list",
        label: "Listar jogos",
        description: "Lista os jogos instalados (Steam, Epic, GOG, Lutris...).",
        params: r#"{"query":{"type":"string"}}"#,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "games.launch",
        label: "Abrir jogo",
        description: "Abre um jogo instalado pelo nome aproximado.",
        params: r#"{"name":{"type":"string"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    // ---- arquivos ----
    Def {
        name: "files.open",
        label: "Abrir arquivo ou pasta",
        description: "Abre uma pasta no Explorador ou um arquivo no programa padrão. Caminho absoluto, com ~, ou nome de pasta do usuário (Downloads, Documentos...).",
        params: r#"{"path":{"type":"string"}}"#,
        required: &["path"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "files.search",
        label: "Procurar arquivos",
        description: "Procura arquivos e pastas pelo nome dentro da pasta pessoal.",
        params: r#"{"name":{"type":"string"},"limit":{"type":"integer","minimum":1,"maximum":50}}"#,
        required: &["name"],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "files.recent",
        label: "Arquivos recentes",
        description: "Lista os arquivos usados por último.",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    // ---- sistema ----
    Def {
        name: "wifi.status",
        label: "Wi-Fi",
        description: "Diz se o Wi-Fi está ligado, a rede conectada e as redes por perto.",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "wifi.connect",
        label: "Conectar Wi-Fi",
        description: "Conecta numa rede Wi-Fi já salva (ou aberta) pelo nome. Redes novas com senha: abra o menu Wi-Fi.",
        params: r#"{"name":{"type":"string"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "notify",
        label: "Aviso",
        description: "Mostra um aviso na tela.",
        params: r#"{"title":{"type":"string"},"body":{"type":"string"}}"#,
        required: &["title"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "clipboard.get",
        label: "Ler área de transferência",
        description: "Lê o texto copiado (Ctrl+C) agora.",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "ask",
    },
    Def {
        name: "clipboard.set",
        label: "Copiar texto",
        description: "Coloca um texto na área de transferência, pronto para colar.",
        params: r#"{"text":{"type":"string"}}"#,
        required: &["text"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "screenshot",
        label: "Captura de tela",
        description: "Tira uma foto da tela e salva em Imagens/Capturas.",
        params: NONE,
        required: &[],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "power",
        label: "Energia",
        description: "Suspende, desliga, reinicia ou sai da sessão: suspend, shutdown, restart, logout.",
        params: r#"{"action":{"type":"string","enum":["suspend","shutdown","restart","logout"]}}"#,
        required: &["action"],
        risk: Risk::Danger,
        default: "ask",
    },
    // ---- loja ----
    Def {
        name: "store.search",
        label: "Procurar na Loja",
        description: "Procura programas para instalar (repositórios do Arch, Flathub e AUR).",
        params: r#"{"query":{"type":"string"}}"#,
        required: &["query"],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "store.install",
        label: "Instalar programa",
        description: "Instala um programa achado no store.search (source e name exatamente como vieram). Pede a senha de administrador antes. Prefira arch ou flathub; o AUR é código da comunidade, sem revisão.",
        params: r#"{"source":{"type":"string","enum":["arch","flathub","aur"]},"name":{"type":"string"},"repo":{"type":"string"}}"#,
        required: &["source", "name"],
        risk: Risk::Danger,
        default: "ask",
    },
    // ---- serviços ----
    Def {
        name: "service.list",
        label: "Serviços",
        description: "Lista os serviços do usuário (systemctl --user) rodando agora.",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "service.control",
        label: "Controlar serviço",
        description: "status, start, stop, restart, enable ou disable de um serviço do usuário (systemctl --user).",
        params: r#"{"name":{"type":"string"},"action":{"type":"string","enum":["status","start","stop","restart","enable","disable"]}}"#,
        required: &["name", "action"],
        risk: Risk::Act,
        default: "ask",
    },
    // ---- comando ----
    Def {
        name: "shell.run",
        label: "Rodar comando",
        description: "Roda um comando no terminal (sh -c) como o usuário e devolve a saída. Só para o que nenhuma outra ação faz.",
        params: r#"{"command":{"type":"string"}}"#,
        required: &["command"],
        risk: Risk::Danger,
        default: "off",
    },
    // ---- macros e automações (automation.rs) ----
    Def {
        name: "macro.list",
        label: "Listar macros",
        description: "Lista as macros salvas (sequências de ações com um nome, como \"hora do filme\").",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "macro.run",
        label: "Rodar macro",
        description: "Roda uma macro salva pelo nome. Passos \"emma\" voltam para você fazer em seguida.",
        params: r#"{"name":{"type":"string"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "auto",
    },
    Def {
        name: "macro.save",
        label: "Salvar macro",
        description: "Cria ou troca uma macro. steps é uma lista de passos: {\"action\": nome de uma ação lumna (com ponto, ex. \"audio.volume\"), \"args\": {...}}, {\"emma\": pedido em texto para você (luzes, lembretes)} ou {\"wait\": segundos}.",
        params: r#"{"name":{"type":"string"},"description":{"type":"string"},"steps":{"type":"array","items":{"type":"object"}}}"#,
        required: &["name", "steps"],
        risk: Risk::Act,
        default: "ask",
    },
    Def {
        name: "macro.delete",
        label: "Apagar macro",
        description: "Apaga uma macro salva.",
        params: r#"{"name":{"type":"string"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "ask",
    },
    Def {
        name: "automation.list",
        label: "Listar automações",
        description: "Lista as automações (gatilho → macro), ligadas e desligadas.",
        params: NONE,
        required: &[],
        risk: Risk::Read,
        default: "auto",
    },
    Def {
        name: "automation.save",
        label: "Salvar automação",
        description: "Cria ou troca uma automação que roda uma macro sozinha. when: {\"type\":\"time\",\"at\":\"08:00\",\"days\":[1..7, 1=segunda]}, {\"type\":\"login\"}, {\"type\":\"mode\",\"mode\":\"gaming\"}, {\"type\":\"app_opened\"|\"app_closed\",\"app\":\"steam\"}, {\"type\":\"wifi\",\"network\":\"Casa\"} ou {\"type\":\"bluetooth\",\"device\":\"Fone\"}. Crie a macro antes.",
        params: r#"{"name":{"type":"string"},"when":{"type":"object"},"macro":{"type":"string"},"enabled":{"type":"boolean"}}"#,
        required: &["name", "when", "macro"],
        risk: Risk::Act,
        default: "ask",
    },
    Def {
        name: "automation.delete",
        label: "Apagar automação",
        description: "Apaga uma automação.",
        params: r#"{"name":{"type":"string"}}"#,
        required: &["name"],
        risk: Risk::Act,
        default: "ask",
    },
];

pub fn find(name: &str) -> Option<&'static Def> {
    CATALOG.iter().find(|d| d.name == name)
}

/// `apps.open` → `lumna_apps_open`.
pub fn tool_name(name: &str) -> String {
    format!("lumna_{}", name.replace('.', "_"))
}

/// `lumna_apps_open` → `apps.open`.
pub fn from_tool_name(tool: &str) -> Option<&'static str> {
    CATALOG.iter().map(|d| d.name).find(|n| tool_name(n) == tool)
}

// ---- permissões ----

fn config_dir() -> PathBuf {
    std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| home().join(".config"))
        .join("lumna")
}

fn home() -> PathBuf {
    std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"))
}

/// Permissões que a pessoa mudou (`~/.config/lumna/emma.json`, chave `permissions`).
pub fn overrides() -> HashMap<String, String> {
    std::fs::read_to_string(config_dir().join("emma.json"))
        .ok()
        .and_then(|t| serde_json::from_str::<Value>(&t).ok())
        .and_then(|v| serde_json::from_value(v["permissions"].clone()).ok())
        .unwrap_or_default()
}

/// Permissão valendo: a da pessoa, mas o que é perigoso nunca roda sem perguntar.
pub fn permission(def: &Def, overrides: &HashMap<String, String>) -> &'static str {
    let chosen = match overrides.get(def.name).map(String::as_str) {
        Some("auto") => "auto",
        Some("ask") => "ask",
        Some("off") => "off",
        _ => def.default,
    };
    if def.risk == Risk::Danger && chosen == "auto" {
        "ask"
    } else {
        chosen
    }
}

/// O catálogo no formato que o backend da Emma recebe (`lumna_tools` do POST /chat).
pub fn tool_defs() -> Vec<Value> {
    let overrides = overrides();
    CATALOG
        .iter()
        .map(|d| {
            let properties: Value = serde_json::from_str(d.params).unwrap_or_else(|_| json!({}));
            json!({
                "name": tool_name(d.name),
                "label": d.label,
                "description": d.description,
                "params": {"type": "object", "properties": properties, "required": d.required},
                "risk": if d.risk == Risk::Read { "read" } else { "act" },
                "default": permission(d, &overrides),
            })
        })
        .collect()
}

// ---- execução ----

/// Resultado no formato do `/tool-result` do backend.
#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Outcome {
    pub ok: bool,
    /// Uma frase para a tela ("Firefox aberto").
    pub summary: String,
    /// O que o modelo lê.
    pub content: String,
}

impl Outcome {
    pub fn done(summary: impl Into<String>) -> Outcome {
        let summary = summary.into();
        Outcome { ok: true, content: summary.clone(), summary }
    }

    pub fn data(summary: impl Into<String>, content: impl Into<String>) -> Outcome {
        Outcome { ok: true, summary: summary.into(), content: content.into() }
    }

    pub fn fail(message: impl Into<String>) -> Outcome {
        let message = message.into();
        Outcome { ok: false, summary: message.clone(), content: format!("Erro: {message}") }
    }
}

fn arg<'a>(args: &'a Value, key: &str) -> &'a str {
    args.get(key).and_then(Value::as_str).map(str::trim).unwrap_or_default()
}

/// Roda uma ação pelo nome (`apps.open`). Quem chama já decidiu a permissão.
/// Quem pediu a ação: muda como as macros tratam os passos "emma".
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Origin {
    /// A pessoa (lumna-ctl, painel).
    User,
    /// A Emma, no meio de uma conversa.
    Emma,
    /// Um passo de macro ou um gatilho.
    Automation,
}

/// Pede a senha de administrador (política os.lumna.confirm, toda vez) antes de a Emma instalar.
async fn confirm_install(package: &str, source: &str) -> Result<(), String> {
    let from = match source {
        "flathub" => "Flathub",
        "aur" => "AUR",
        _ => "Arch",
    };
    let what = format!("{package} ({from})");
    let status = tauri::async_runtime::spawn_blocking(move || {
        Command::new("pkexec").arg("/usr/lib/lumna/lumna-confirm").arg("install").arg(what).status()
    })
    .await
    .map_err(|e| e.to_string())?
    .map_err(|e| format!("pkexec: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err("Instalação cancelada: a senha não foi confirmada.".into())
    }
}

pub async fn run(app: AppHandle, name: &str, args: Value) -> Outcome {
    run_from(app, name, args, Origin::User).await
}

pub async fn run_from(app: AppHandle, name: &str, args: Value, origin: Origin) -> Outcome {
    let Some(def) = find(name) else { return Outcome::fail(format!("ação desconhecida: {name}")) };
    for key in def.required {
        if args.get(*key).is_none_or(|v| v.is_null() || v.as_str().is_some_and(|s| s.trim().is_empty())) {
            return Outcome::fail(format!("falta o parâmetro {key}"));
        }
    }
    match name {
        "wifi.status" => wifi_status().await,
        "wifi.connect" => wifi_connect(arg(&args, "name")).await,
        "store.search" => store_search(arg(&args, "query")).await,
        "macro.run" => {
            let by = match origin {
                Origin::Emma => "Emma",
                Origin::User => "lumna-ctl",
                Origin::Automation => "Automação",
            };
            Box::pin(crate::automation::run_macro(app, arg(&args, "name"), origin, by)).await
        }
        "store.install" => {
            let (source, package, repo) = (arg(&args, "source").to_string(), arg(&args, "name").to_string(), arg(&args, "repo").to_string());
            // Pela Emma (ou por uma macro), a senha vem antes de tudo, de qualquer fonte (no AUR, antes
            // até de baixar e montar o pacote).
            if origin != Origin::User {
                if let Err(err) = confirm_install(&package, &source).await {
                    return Outcome::fail(err);
                }
            }
            match crate::store::store_install(app, source, package.clone(), repo).await {
                Ok(()) => Outcome::done(format!("{package} instalado")),
                Err(err) => Outcome::fail(err),
            }
        }
        _ => {
            let name = name.to_string();
            tauri::async_runtime::spawn_blocking(move || run_blocking(&app, &name, &args))
                .await
                .unwrap_or_else(|e| Outcome::fail(e.to_string()))
        }
    }
}

fn run_blocking(app: &AppHandle, name: &str, args: &Value) -> Outcome {
    let compositor = app.state::<SharedCompositor>().inner().clone();
    let result: Result<Outcome, String> = match name {
        "apps.list" => Ok(apps_list(arg(args, "query"))),
        "apps.open" => apps_open(&compositor, arg(args, "name")),
        "apps.close" => with_window(&compositor, arg(args, "name"), |w| compositor.close_window(&w.address))
            .map(|w| Outcome::done(format!("{} fechado", w.class))),
        "apps.focus" => with_window(&compositor, arg(args, "name"), |w| compositor.focus_window(&w.address))
            .map(|w| Outcome::done(format!("{} na frente", w.class))),
        "windows.list" => windows_list(&compositor),
        "windows.move" => {
            let workspace = args.get("workspace").and_then(Value::as_i64).unwrap_or(0);
            if !(1..=9).contains(&workspace) {
                Err("área de trabalho vai de 1 a 9".into())
            } else {
                with_window(&compositor, arg(args, "name"), |w| compositor.move_window(&w.address, &workspace.to_string()))
                    .map(|w| Outcome::done(format!("{} na área {workspace}", w.class)))
            }
        }
        "windows.fullscreen" => with_window(&compositor, arg(args, "name"), |w| compositor.fullscreen_window(&w.address))
            .map(|w| Outcome::done(format!("Tela cheia: {}", w.class))),
        "mode.current" => Ok(mode_current(app, &compositor)),
        "mode.switch" => match Mode::parse(arg(args, "mode")) {
            Some(mode) => crate::switch_mode(app, Target::Set(mode)).map(|()| Outcome::done(format!("Modo {}", arg(args, "mode")))),
            None => Err("modo desconhecido (desktop, gaming ou media)".into()),
        },
        "panel.open" => panel_open(app, arg(args, "panel"), arg(args, "page")),
        "audio.volume" => audio_volume(args),
        "audio.outputs" => Ok(audio_outputs()),
        "audio.output" => audio_output(arg(args, "name")),
        "media.control" => media_control(arg(args, "action")),
        "media.now_playing" => Ok(now_playing()),
        "media.open_service" => crate::media_open(app, None, arg(args, "id"), None)
            .map(|()| Outcome::done(format!("{} aberto", arg(args, "id")))),
        "media.play_video" => {
            let videos = media::videos();
            match best(&videos, arg(args, "name"), |v| vec![v.title.clone()]) {
                Some(video) => compositor.exec(&media::video_command(video)).map(|()| Outcome::done(format!("Tocando {}", video.title))),
                None => Err(format!("nenhum vídeo parecido com \"{}\"", arg(args, "name"))),
            }
        }
        "games.list" => Ok(games_list(arg(args, "query"))),
        "games.launch" => {
            let games = games::list();
            match best(&games, arg(args, "name"), |g| vec![g.title.clone()]) {
                Some(game) => compositor.exec(&game.launch).map(|()| Outcome::done(format!("Abrindo {}", game.title))),
                None => Err(format!("nenhum jogo parecido com \"{}\"", arg(args, "name"))),
            }
        }
        "files.open" => files_open(app, &compositor, arg(args, "path")),
        "files.search" => {
            let limit = args.get("limit").and_then(Value::as_u64).unwrap_or(20).clamp(1, 50) as usize;
            Ok(files_search(&home(), arg(args, "name"), limit))
        }
        "files.recent" => Ok(files_recent()),
        "notify" => run_cmd(Command::new("notify-send").args(["-a", "Emma", arg(args, "title"), arg(args, "body")]))
            .map(|_| Outcome::done("Aviso mostrado")),
        "clipboard.get" => run_cmd(Command::new("wl-paste").args(["-n", "-t", "text"])).map(|text| {
            let text: String = text.chars().take(4000).collect();
            Outcome::data("Área de transferência lida", if text.is_empty() { "(vazia)".into() } else { text })
        }),
        "clipboard.set" => clipboard_set(arg(args, "text")),
        "screenshot" => screenshot(),
        "power" => power(&compositor, arg(args, "action")),
        "service.list" => run_cmd(Command::new("systemctl").args([
            "--user", "list-units", "--type=service", "--state=running", "--no-legend", "--plain", "--no-pager",
        ]))
        .map(|out| Outcome::data("Serviços rodando", services_text(&out))),
        "service.control" => service_control(arg(args, "name"), arg(args, "action")),
        "shell.run" => shell_run(arg(args, "command")),
        "macro.list" => Ok(crate::automation::action_macro_list()),
        "macro.save" => Ok(crate::automation::action_macro_save(args)),
        "macro.delete" => Ok(crate::automation::action_macro_delete(args)),
        "automation.list" => Ok(crate::automation::action_automation_list()),
        "automation.save" => Ok(crate::automation::action_automation_save(args)),
        "automation.delete" => Ok(crate::automation::action_automation_delete(args)),
        _ => Err(format!("ação desconhecida: {name}")),
    };
    result.unwrap_or_else(Outcome::fail)
}

// ---- busca aproximada ----

/// Minúsculas, sem acento, só letras, números e espaço ("Navegador Web" → "navegador web").
pub fn normalize(text: &str) -> String {
    let mut out = String::new();
    for c in text.to_lowercase().chars() {
        let c = match c {
            'á' | 'à' | 'â' | 'ã' | 'ä' => 'a',
            'é' | 'è' | 'ê' | 'ë' => 'e',
            'í' | 'ì' | 'î' | 'ï' => 'i',
            'ó' | 'ò' | 'ô' | 'õ' | 'ö' => 'o',
            'ú' | 'ù' | 'û' | 'ü' => 'u',
            'ç' => 'c',
            c if c.is_alphanumeric() => c,
            _ => ' ',
        };
        out.push(c);
    }
    out.split_whitespace().collect::<Vec<_>>().join(" ")
}

/// Nota de 0 a 100 de quão bem `query` descreve `text`.
fn score(text: &str, query: &str) -> u32 {
    let (text, query) = (normalize(text), normalize(query));
    if query.is_empty() || text.is_empty() {
        return 0;
    }
    if text == query {
        return 100;
    }
    if text.starts_with(&query) {
        return 85;
    }
    if text.split(' ').any(|w| w.starts_with(&query)) {
        return 70;
    }
    if text.contains(&query) {
        return 55;
    }
    // Todas as palavras do pedido aparecem ("editor texto" acha "Editor de texto").
    let words: Vec<&str> = query.split(' ').filter(|w| w.len() > 2).collect();
    if !words.is_empty() && words.iter().all(|w| text.contains(w)) {
        return 45;
    }
    0
}

/// O item que melhor bate com o pedido (no empate, o de nome mais curto).
pub fn best<'a, T>(items: &'a [T], query: &str, keys: impl Fn(&T) -> Vec<String>) -> Option<&'a T> {
    items
        .iter()
        .filter_map(|item| {
            let keys = keys(item);
            let (s, len) = keys
                .iter()
                .enumerate()
                // O nome vale mais que as palavras-chave.
                .map(|(i, k)| (score(k, query).saturating_sub(if i == 0 { 0 } else { 15 }), keys[0].len()))
                .max()?;
            (s > 0).then_some((s, len, item))
        })
        .max_by(|a, b| a.0.cmp(&b.0).then(b.1.cmp(&a.1)))
        .map(|(_, _, item)| item)
}

fn app_keys(a: &apps::App) -> Vec<String> {
    vec![a.name.clone(), a.id.clone(), a.wm_class.clone().unwrap_or_default(), a.keywords.clone(), a.comment.clone()]
}

// ---- apps e janelas ----

fn apps_list(query: &str) -> Outcome {
    let list = apps::list_plain();
    let shown: Vec<String> = list
        .iter()
        .filter(|a| query.is_empty() || app_keys(a).iter().any(|k| score(k, query) > 0))
        .take(120)
        .map(|a| if a.comment.is_empty() { a.name.clone() } else { format!("{} — {}", a.name, a.comment) })
        .collect();
    if shown.is_empty() {
        return Outcome::data("Nenhum programa", format!("Nenhum programa instalado parecido com \"{query}\"."));
    }
    Outcome::data(format!("{} programas", shown.len()), shown.join("\n"))
}

fn apps_open(compositor: &SharedCompositor, name: &str) -> Result<Outcome, String> {
    let list = apps::list_plain();
    let app = best(&list, name, app_keys).ok_or_else(|| {
        format!("nenhum programa instalado parecido com \"{name}\" (dá para procurar na Loja com store.search)")
    })?;
    compositor.exec(&app.command(TERMINAL))?;
    Ok(Outcome::done(format!("{} aberto", app.name)))
}

/// A janela que bate com o nome (do programa ou do título), já com a ação feita.
fn with_window(
    compositor: &SharedCompositor,
    name: &str,
    action: impl Fn(&lumna_core::Window) -> Result<(), String>,
) -> Result<lumna_core::Window, String> {
    let windows = compositor.windows()?;
    // "fecha o navegador": o nome do app (do .desktop) leva até a classe da janela.
    let list = apps::list_plain();
    let app = best(&list, name, app_keys);
    let window = best(&windows, name, |w| vec![w.class.clone(), w.title.clone()])
        .or_else(|| {
            let app = app?;
            let classes = [app.wm_class.clone().unwrap_or_default(), app.id.clone(), app.name.clone()];
            windows.iter().find(|w| classes.iter().any(|c| !c.is_empty() && normalize(c) == normalize(&w.class)))
        })
        .ok_or_else(|| format!("nenhuma janela aberta de \"{name}\""))?;
    action(window)?;
    Ok(window.clone())
}

fn windows_list(compositor: &SharedCompositor) -> Result<Outcome, String> {
    let windows = compositor.windows()?;
    if windows.is_empty() {
        return Ok(Outcome::data("Nenhuma janela", "Nenhuma janela aberta."));
    }
    let lines: Vec<String> = windows.iter().map(|w| format!("{} — {} (área {})", w.class, w.title, w.workspace)).collect();
    Ok(Outcome::data(format!("{} janelas", windows.len()), lines.join("\n")))
}

fn mode_current(app: &AppHandle, compositor: &SharedCompositor) -> Outcome {
    let modes = app.state::<Modes>();
    let monitors = compositor.monitors().unwrap_or_default();
    let lines: Vec<String> = monitors
        .iter()
        .map(|m| format!("{}{}: {}", m.name, if m.focused { " (em foco)" } else { "" }, mode_id(modes.mode_of(&m.name))))
        .collect();
    Outcome::data("Modo atual", lines.join("\n"))
}

fn mode_id(mode: Mode) -> &'static str {
    match mode {
        Mode::Desktop => "desktop",
        Mode::Gaming => "gaming",
        Mode::Media => "media",
    }
}

fn panel_open(app: &AppHandle, panel: &str, page: &str) -> Result<Outcome, String> {
    let page = (!page.is_empty()).then(|| page.to_string());
    match panel {
        "configuracoes" => crate::open_settings(app, page),
        "loja" => crate::open_store(app, page),
        "arquivos" => crate::open_files(app, page),
        "inicio" => crate::launcher(app, crate::LauncherAction::Open),
        _ => return Err(format!("tela desconhecida: {panel}")),
    }?;
    Ok(Outcome::done(format!("{panel} aberto")))
}

// ---- som e mídia ----

fn audio_volume(args: &Value) -> Result<Outcome, String> {
    let sink = "@DEFAULT_AUDIO_SINK@";
    if let Some(mute) = args.get("mute").and_then(Value::as_bool) {
        crate::status::wpctl(&["set-mute", sink, if mute { "1" } else { "0" }])?;
        if args.get("percent").is_none() && args.get("change").is_none() {
            return Ok(Outcome::done(if mute { "Som mudo" } else { "Som ligado" }));
        }
    }
    if let Some(percent) = args.get("percent").and_then(Value::as_i64) {
        let percent = percent.clamp(0, 100);
        crate::status::wpctl(&["set-volume", sink, &format!("{percent}%")])?;
        crate::status::wpctl(&["set-mute", sink, "0"])?;
        return Ok(Outcome::done(format!("Volume em {percent}%")));
    }
    if let Some(change) = args.get("change").and_then(Value::as_i64) {
        let step = format!("{}%{}", change.unsigned_abs().min(100), if change >= 0 { "+" } else { "-" });
        crate::status::wpctl(&["set-volume", "-l", "1", sink, &step])?;
        return Ok(match crate::status::read().volume {
            Some(volume) => Outcome::done(format!("Volume em {volume}%")),
            None => Outcome::done("Volume ajustado"),
        });
    }
    Err("diga percent, change ou mute".into())
}

fn audio_outputs() -> Outcome {
    let state = crate::audio::audio_state();
    if state.outputs.is_empty() {
        return Outcome::data("Sem saídas", "Nenhuma saída de som encontrada.");
    }
    let lines: Vec<String> = state
        .outputs
        .iter()
        .map(|o| format!("{}{} — volume {}%{}", o.name, if o.default { " (em uso)" } else { "" }, o.volume, if o.muted { ", mudo" } else { "" }))
        .collect();
    Outcome::data(format!("{} saídas", lines.len()), lines.join("\n"))
}

fn audio_output(name: &str) -> Result<Outcome, String> {
    let state = crate::audio::audio_state();
    let output = best(&state.outputs, name, |o| vec![o.name.clone()]).ok_or_else(|| format!("nenhuma saída parecida com \"{name}\""))?;
    crate::audio::audio_set_default(output.id)?;
    Ok(Outcome::done(format!("Som saindo em {}", output.name)))
}

fn media_control(action: &str) -> Result<Outcome, String> {
    let (command, done) = match action {
        "play_pause" => ("play-pause", "Play/pausa"),
        "next" => ("next", "Próxima"),
        "previous" => ("previous", "Anterior"),
        "stop" => ("stop", "Parado"),
        _ => return Err(format!("ação de mídia desconhecida: {action}")),
    };
    run_cmd(Command::new("playerctl").arg(command)).map_err(|_| "nada tocando agora".to_string())?;
    Ok(Outcome::done(done))
}

fn now_playing() -> Outcome {
    match run_cmd(Command::new("playerctl").args(["metadata", "--format", "{{status}}|{{playerName}}|{{artist}}|{{title}}"])) {
        Ok(line) => {
            let parts: Vec<&str> = line.trim().splitn(4, '|').collect();
            let [status, player, artist, title] = parts[..] else { return Outcome::data("Nada tocando", "Nada tocando agora.") };
            let status = if status == "Playing" { "tocando" } else { "pausado" };
            let who = if artist.is_empty() { String::new() } else { format!(" — {artist}") };
            Outcome::data(format!("{title}{who}"), format!("{title}{who} ({player}, {status})"))
        }
        Err(_) => Outcome::data("Nada tocando", "Nada tocando agora."),
    }
}

fn games_list(query: &str) -> Outcome {
    let games = games::list();
    let shown: Vec<String> = games
        .iter()
        .filter(|g| query.is_empty() || score(&g.title, query) > 0)
        .take(100)
        .map(|g| format!("{} ({})", g.title, g.source))
        .collect();
    if shown.is_empty() {
        return Outcome::data("Nenhum jogo", "Nenhum jogo encontrado.");
    }
    Outcome::data(format!("{} jogos", shown.len()), shown.join("\n"))
}

// ---- arquivos ----

/// Pastas do usuário pelo nome do dia a dia ("downloads", "documentos").
fn user_dir(name: &str) -> Option<PathBuf> {
    let key = match normalize(name).as_str() {
        "downloads" | "download" | "baixados" => "DOWNLOAD",
        "documentos" | "documents" => "DOCUMENTS",
        "imagens" | "fotos" | "pictures" => "PICTURES",
        "musicas" | "musica" | "music" => "MUSIC",
        "videos" | "video" => "VIDEOS",
        "area de trabalho" | "desktop" => "DESKTOP",
        "pasta pessoal" | "home" | "inicio" => return Some(home()),
        _ => return None,
    };
    let configured = Command::new("xdg-user-dir").arg(key).output().ok().map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string());
    configured.filter(|p| !p.is_empty() && p != &home().to_string_lossy()).map(PathBuf::from).or_else(|| {
        let fallback = match key {
            "DOWNLOAD" => "Downloads",
            "DOCUMENTS" => "Documentos",
            "PICTURES" => "Imagens",
            "MUSIC" => "Músicas",
            "VIDEOS" => "Vídeos",
            _ => "Área de trabalho",
        };
        Some(home().join(fallback))
    })
}

pub fn expand(path: &str) -> PathBuf {
    if let Some(dir) = user_dir(path) {
        return dir;
    }
    if path == "~" {
        return home();
    }
    if let Some(rest) = path.strip_prefix("~/") {
        return home().join(rest);
    }
    PathBuf::from(path)
}

fn quote(text: &str) -> String {
    format!("'{}'", text.replace('\'', r"'\''"))
}

fn files_open(app: &AppHandle, compositor: &SharedCompositor, path: &str) -> Result<Outcome, String> {
    let path = expand(path);
    if !path.is_absolute() {
        return Err("use um caminho completo (ou procure com files.search)".into());
    }
    if path.is_dir() {
        crate::open_files(app, Some(path.to_string_lossy().to_string()))?;
    } else if path.exists() {
        compositor.exec(&format!("xdg-open {}", quote(&path.to_string_lossy())))?;
    } else {
        return Err(format!("{} não existe", path.display()));
    }
    Ok(Outcome::done(format!("{} aberto", path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_else(|| "/".into()))))
}

/// Procura pelo nome (sem pastas escondidas e sem descer demais), parando cedo para não travar.
pub fn files_search(root: &Path, name: &str, limit: usize) -> Outcome {
    let query = normalize(name);
    let started = Instant::now();
    let mut found = Vec::new();
    let mut stack = vec![(root.to_path_buf(), 0)];
    let mut seen = 0;
    while let Some((dir, depth)) = stack.pop() {
        let Ok(entries) = std::fs::read_dir(&dir) else { continue };
        for entry in entries.flatten() {
            seen += 1;
            let file_name = entry.file_name().to_string_lossy().to_string();
            if file_name.starts_with('.') || file_name == "node_modules" || file_name == "target" {
                continue;
            }
            let path = entry.path();
            if normalize(&file_name).contains(&query) {
                found.push(path.clone());
            }
            if depth < 8 && entry.file_type().is_ok_and(|t| t.is_dir()) {
                stack.push((path, depth + 1));
            }
        }
        if found.len() >= limit || seen > 200_000 || started.elapsed() > Duration::from_secs(4) {
            break;
        }
    }
    found.truncate(limit);
    if found.is_empty() {
        return Outcome::data("Nada encontrado", format!("Nenhum arquivo com \"{name}\" no nome."));
    }
    let lines: Vec<String> = found.iter().map(|p| p.to_string_lossy().to_string()).collect();
    Outcome::data(format!("{} encontrados", lines.len()), lines.join("\n"))
}

/// `~/.local/share/recently-used.xbel`: `<bookmark href="file:///..." modified="2026-10-12T...">`.
pub fn parse_recent(xbel: &str, limit: usize) -> Vec<String> {
    let mut items: Vec<(String, String)> = xbel
        .split("<bookmark ")
        .skip(1)
        .filter_map(|b| {
            let attr = |key: &str| {
                let start = b.find(&format!("{key}=\""))? + key.len() + 2;
                Some(b[start..].split('"').next()?.to_string())
            };
            let href = attr("href")?;
            let path = href.strip_prefix("file://")?;
            Some((attr("modified").or_else(|| attr("visited")).unwrap_or_default(), url_decode(path)))
        })
        .collect();
    items.sort_by(|a, b| b.0.cmp(&a.0));
    items.into_iter().map(|(_, p)| p).take(limit).collect()
}

fn url_decode(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            if let Ok(b) = u8::from_str_radix(&text[i + 1..i + 3], 16) {
                out.push(b);
                i += 3;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).to_string()
}

fn files_recent() -> Outcome {
    let path = home().join(".local/share/recently-used.xbel");
    let recent = std::fs::read_to_string(path).map(|t| parse_recent(&t, 20)).unwrap_or_default();
    if recent.is_empty() {
        return Outcome::data("Nenhum recente", "Nenhum arquivo recente.");
    }
    Outcome::data(format!("{} recentes", recent.len()), recent.join("\n"))
}

// ---- sistema ----

async fn wifi_status() -> Outcome {
    let state = crate::wifi::wifi_state().await;
    if !state.available {
        return Outcome::data("Sem Wi-Fi", "Este computador não tem Wi-Fi (ou o iwd não está rodando).");
    }
    if !state.powered {
        return Outcome::data("Wi-Fi desligado", "O Wi-Fi está desligado.");
    }
    let networks: Vec<String> = state
        .networks
        .iter()
        .take(15)
        .map(|n| format!("{} (sinal {}/4{}{})", n.name, n.signal, if n.known { ", salva" } else { "" }, if n.security == "open" { ", aberta" } else { "" }))
        .collect();
    let connected = state.connected.clone().unwrap_or_else(|| "nenhuma".into());
    Outcome::data(format!("Conectado em {connected}"), format!("Rede conectada: {connected}\nRedes por perto:\n{}", networks.join("\n")))
}

async fn wifi_connect(name: &str) -> Outcome {
    let state = crate::wifi::wifi_state().await;
    let Some(network) = best(&state.networks, name, |n| vec![n.name.clone()]) else {
        return Outcome::fail(format!("nenhuma rede parecida com \"{name}\" por perto"));
    };
    if network.connected {
        return Outcome::done(format!("Já conectado em {}", network.name));
    }
    if !network.known && network.security != "open" {
        return Outcome::fail(format!("{} pede senha: conecte pelo menu Wi-Fi da barra de cima", network.name));
    }
    match crate::wifi::wifi_connect(network.path.clone(), None).await {
        Ok(()) => Outcome::done(format!("Conectado em {}", network.name)),
        Err(err) => Outcome::fail(err),
    }
}

async fn store_search(query: &str) -> Outcome {
    let results = crate::store::store_search(query.to_string()).await;
    if results.is_empty() {
        return Outcome::data("Nada na Loja", format!("Nada encontrado para \"{query}\"."));
    }
    let lines: Vec<String> = results
        .iter()
        .take(15)
        .map(|p| {
            format!(
                "source={} name={}{} — {}: {}{}",
                p.source,
                p.name,
                if p.repo.is_empty() { String::new() } else { format!(" repo={}", p.repo) },
                p.title,
                p.summary,
                if p.installed { " (já instalado)" } else { "" }
            )
        })
        .collect();
    Outcome::data(format!("{} na Loja", lines.len()), lines.join("\n"))
}

fn clipboard_set(text: &str) -> Result<Outcome, String> {
    use std::io::Write;
    let mut child = Command::new("wl-copy").stdin(Stdio::piped()).spawn().map_err(|e| format!("wl-copy: {e}"))?;
    child.stdin.take().ok_or("wl-copy sem entrada")?.write_all(text.as_bytes()).map_err(|e| e.to_string())?;
    child.wait().map_err(|e| e.to_string())?;
    Ok(Outcome::done("Copiado"))
}

fn screenshot() -> Result<Outcome, String> {
    let dir = user_dir("imagens").unwrap_or_else(|| home().join("Imagens")).join("Capturas");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let stamp = run_cmd(Command::new("date").arg("+%Y-%m-%d %H-%M-%S")).unwrap_or_default();
    let path = dir.join(format!("Captura {}.png", stamp.trim()));
    run_cmd(Command::new("grim").arg(&path))?;
    Ok(Outcome::data("Captura salva", format!("Captura salva em {}", path.display())))
}

fn power(compositor: &SharedCompositor, action: &str) -> Result<Outcome, String> {
    match action {
        "suspend" => run_cmd(Command::new("systemctl").arg("suspend")).map(|_| Outcome::done("Suspendendo")),
        "shutdown" => run_cmd(Command::new("systemctl").arg("poweroff")).map(|_| Outcome::done("Desligando")),
        "restart" => run_cmd(Command::new("systemctl").arg("reboot")).map(|_| Outcome::done("Reiniciando")),
        "logout" => compositor.exit().map(|()| Outcome::done("Saindo da sessão")),
        _ => Err(format!("ação de energia desconhecida: {action}")),
    }
}

/// Nome de serviço do usuário: letras, números, `-_.@`, e termina em `.service` se não disser outro tipo.
pub fn unit_name(name: &str) -> Option<String> {
    let name = name.trim();
    if name.is_empty() || name.starts_with('-') || !name.chars().all(|c| c.is_ascii_alphanumeric() || "-_.@".contains(c)) {
        return None;
    }
    let typed = [".service", ".timer", ".socket", ".target", ".path"].iter().any(|t| name.ends_with(t));
    Some(if typed { name.to_string() } else { format!("{name}.service") })
}

fn services_text(out: &str) -> String {
    let lines: Vec<String> = out
        .lines()
        .filter_map(|l| {
            let mut cols = l.split_whitespace();
            let unit = cols.next()?;
            let description = cols.skip(3).collect::<Vec<_>>().join(" ");
            Some(format!("{unit} — {description}"))
        })
        .collect();
    if lines.is_empty() {
        "Nenhum serviço do usuário rodando.".into()
    } else {
        lines.join("\n")
    }
}

fn service_control(name: &str, action: &str) -> Result<Outcome, String> {
    let unit = unit_name(name).ok_or_else(|| format!("nome de serviço inválido: {name}"))?;
    if !["status", "start", "stop", "restart", "enable", "disable"].contains(&action) {
        return Err(format!("ação desconhecida: {action}"));
    }
    if action == "status" {
        let out = Command::new("systemctl").args(["--user", "status", "--no-pager", "--lines=5", &unit]).output().map_err(|e| e.to_string())?;
        let text = String::from_utf8_lossy(&out.stdout).to_string();
        return Ok(Outcome::data(format!("Status de {unit}"), if text.is_empty() { format!("{unit} não existe") } else { text }));
    }
    let mut command = Command::new("systemctl");
    command.args(["--user", action]);
    if matches!(action, "enable" | "disable") {
        command.arg("--now");
    }
    run_cmd(command.arg(&unit))?;
    let done = match action {
        "start" => "iniciado",
        "stop" => "parado",
        "restart" => "reiniciado",
        "enable" => "ligado (e inicia junto com o sistema)",
        _ => "desligado",
    };
    Ok(Outcome::done(format!("{unit} {done}")))
}

/// `sh -c` com limite de 60 s e de saída (o modelo lê só o fim).
fn shell_run(command: &str) -> Result<Outcome, String> {
    let mut child = Command::new("sh")
        .args(["-c", command])
        .current_dir(home())
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| e.to_string())?;
    let started = Instant::now();
    loop {
        if child.try_wait().map_err(|e| e.to_string())?.is_some() {
            break;
        }
        if started.elapsed() > Duration::from_secs(60) {
            let _ = child.kill();
            let _ = child.wait();
            return Err("o comando passou de 60 segundos e foi parado".into());
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    let out = child.wait_with_output().map_err(|e| e.to_string())?;
    let mut text = String::from_utf8_lossy(&out.stdout).to_string();
    text.push_str(&String::from_utf8_lossy(&out.stderr));
    let chars: Vec<char> = text.chars().collect();
    let tail: String = chars[chars.len().saturating_sub(6000)..].iter().collect();
    let code = out.status.code().unwrap_or(-1);
    Ok(Outcome {
        ok: out.status.success(),
        summary: format!("Comando terminou ({code})"),
        content: format!("código de saída {code}\n{tail}"),
    })
}

fn run_cmd(command: &mut Command) -> Result<String, String> {
    let program = command.get_program().to_string_lossy().to_string();
    let out = command.stdin(Stdio::null()).output().map_err(|e| format!("{program}: {e}"))?;
    if out.status.success() {
        Ok(String::from_utf8_lossy(&out.stdout).to_string())
    } else {
        let err = String::from_utf8_lossy(&out.stderr).trim().to_string();
        Err(if err.is_empty() { format!("{program} falhou") } else { err })
    }
}

// ---- área de transferência (menu de texto do LUMNA) ----

/// Texto copiado agora (vazio se não houver). O menu "Colar" do LUMNA lê por aqui: o menu nativo
/// do WebKit abre um popup do GTK, que derruba as camadas do layer-shell.
#[tauri::command]
pub async fn clipboard_read() -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let out = Command::new("wl-paste").args(["-n", "-t", "text"]).stdin(Stdio::null()).output().map_err(|e| format!("wl-paste: {e}"))?;
        // Sem nada copiado o wl-paste sai com erro: para quem cola, é só vazio.
        Ok(if out.status.success() { String::from_utf8_lossy(&out.stdout).to_string() } else { String::new() })
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn clipboard_write(text: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || clipboard_set(&text).map(|_| ())).await.map_err(|e| e.to_string())?
}

// ---- comandos do Tauri (painel da Emma) ----

#[tauri::command]
pub fn actions_tool_defs() -> Vec<Value> {
    tool_defs()
}

/// Executa uma chamada da Emma (`lumna_apps_open`). A permissão já foi tratada pelo backend
/// (que perguntou à pessoa quando precisava); aqui só se barra o que está desligado.
#[tauri::command]
pub async fn actions_run_tool(app: AppHandle, tool: String, args: Value) -> Outcome {
    let Some(name) = from_tool_name(&tool) else { return Outcome::fail(format!("ação desconhecida: {tool}")) };
    let def = find(name).expect("nome vindo do catálogo");
    if permission(def, &overrides()) == "off" {
        return Outcome::fail(format!("a ação \"{}\" está desligada no Painel → Emma", def.label));
    }
    run_from(app, name, args, Origin::Emma).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn catalog_is_valid() {
        assert!(CATALOG.len() <= 120, "o backend aceita até 120 ações por mensagem");
        let mut names = std::collections::HashSet::new();
        for def in CATALOG {
            assert!(names.insert(def.name), "nome repetido: {}", def.name);
            let tool = tool_name(def.name);
            assert!(tool.len() <= 66 && tool.chars().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '_'), "{tool}");
            assert_eq!(from_tool_name(&tool), Some(def.name));
            let params: Value = serde_json::from_str(def.params).unwrap_or_else(|e| panic!("{}: {e}", def.name));
            for key in def.required {
                assert!(params.get(*key).is_some(), "{}: {key} obrigatório mas não descrito", def.name);
            }
            assert!(["auto", "ask", "off"].contains(&def.default));
            // Limites do backend da Emma (LumnaToolDef em routes_chat.py).
            assert!(def.label.chars().count() <= 80 && def.description.chars().count() <= 800, "{}", def.name);
        }
    }

    #[test]
    fn danger_always_asks() {
        let shutdown = find("power").unwrap();
        let mut overrides = HashMap::new();
        overrides.insert("power".to_string(), "auto".to_string());
        assert_eq!(permission(shutdown, &overrides), "ask");
        overrides.insert("power".to_string(), "off".to_string());
        assert_eq!(permission(shutdown, &overrides), "off");
        assert_eq!(permission(find("shell.run").unwrap(), &HashMap::new()), "off");
        let open = find("apps.open").unwrap();
        overrides.insert("apps.open".to_string(), "ask".to_string());
        assert_eq!(permission(open, &overrides), "ask");
    }

    #[test]
    fn fuzzy_finds_apps() {
        let app = |name: &str, id: &str, keywords: &str| apps::App {
            id: id.into(),
            name: name.into(),
            comment: String::new(),
            icon: None,
            wm_class: None,
            exec: id.into(),
            keywords: keywords.into(),
            terminal: false,
        };
        let list = vec![
            app("Firefox", "firefox", "Navegador Web internet"),
            app("Mousepad", "org.xfce.mousepad", "Editor de texto"),
            app("Visual Studio Code", "code", "Editor de código"),
            app("Configurações", "lumna-settings", ""),
        ];
        let find = |q: &str| best(&list, q, app_keys).map(|a| a.id.as_str());
        assert_eq!(find("firefox"), Some("firefox"));
        assert_eq!(find("Firefox"), Some("firefox"));
        assert_eq!(find("navegador"), Some("firefox"));
        assert_eq!(find("editor de texto"), Some("org.xfce.mousepad"));
        assert_eq!(find("vs code"), Some("code"));
        assert_eq!(find("visual studio"), Some("code"));
        assert_eq!(find("configuracoes"), Some("lumna-settings"));
        assert_eq!(find("photoshop"), None);
    }

    #[test]
    fn reads_recent_and_units() {
        let xbel = r#"<xbel><bookmark href="file:///home/a/Documentos/rel%C3%B3rio.pdf" added="x" modified="2026-10-12T10:00:00Z"></bookmark>
            <bookmark href="file:///home/a/foto.png" modified="2026-10-12T12:00:00Z"/><bookmark href="https://x.com" modified="2026-10-13T00:00:00Z"/></xbel>"#;
        assert_eq!(parse_recent(xbel, 10), vec!["/home/a/foto.png", "/home/a/Documentos/relório.pdf"]);
        assert_eq!(unit_name("syncthing").as_deref(), Some("syncthing.service"));
        assert_eq!(unit_name("backup.timer").as_deref(), Some("backup.timer"));
        assert_eq!(unit_name("x; rm -rf ~"), None);
        assert_eq!(unit_name("--all"), None);
        assert_eq!(normalize("Área de Trabalho!"), "area de trabalho");
    }

    #[test]
    fn searches_files_by_name() {
        let root = std::env::temp_dir().join(format!("lumna-actions-{}", std::process::id()));
        std::fs::create_dir_all(root.join("Documentos/.escondido")).unwrap();
        std::fs::write(root.join("Documentos/Relatório Final.pdf"), "").unwrap();
        std::fs::write(root.join("Documentos/.escondido/relatorio.txt"), "").unwrap();
        let out = files_search(&root, "relatorio", 10);
        assert!(out.content.contains("Relatório Final.pdf"), "{}", out.content);
        assert!(!out.content.contains(".escondido"));
        let _ = std::fs::remove_dir_all(root);
    }
}
