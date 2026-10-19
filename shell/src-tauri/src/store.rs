//! Loja: programas de três fontes.
//!
//! - **Arch** (`pacman`): repositórios oficiais. Instalar e remover precisa de administrador, pelo
//!   `pkexec /usr/lib/lumna/lumna-pkg` (a senha é pedida pela tela do LUMNA, ver `polkit.rs`).
//! - **Flathub** (`flatpak --user`): apps isolados, instalados só para a pessoa, sem senha.
//! - **AUR**: pacotes da comunidade, compilados aqui com o `makepkg` (o `pacman -U` do fim também
//!   passa pelo `lumna-pkg`).
//!
//! Uma operação por vez; o andamento sai linha a linha no evento `lumna://store-job`.

use std::collections::HashSet;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};

use serde::{Deserialize, Serialize};
use serde_json::Value;
use tauri::{AppHandle, Emitter};

const HELPER: &str = "/usr/lib/lumna/lumna-pkg";
const FLATHUB: &str = "https://dl.flathub.org/repo/flathub.flatpakrepo";
const AUR: &str = "https://aur.archlinux.org";
/// Os ícones dos apps do Flathub (o caminho tem a arquitetura: x86_64 no PC, aarch64 no celular).
fn flathub_icons() -> String {
    format!("https://dl.flathub.org/repo/appstream/{}/icons/128x128", std::env::consts::ARCH)
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Package {
    /// `arch`, `flathub` ou `aur`.
    pub source: &'static str,
    /// Nome do pacote (Arch, AUR) ou id do app (Flathub).
    pub name: String,
    pub title: String,
    pub summary: String,
    pub version: String,
    /// Repositório no Arch (`core`, `extra`, `multilib`); no AUR, o PackageBase (o git a clonar).
    pub repo: String,
    pub installed: bool,
    pub icon: Option<String>,
    /// Peça do sistema que a Loja não deixa remover (o LUMNA, o kernel, o pacman...).
    pub essential: bool,
}

/// Pacotes sem os quais o sistema não liga ou não se atualiza. Os que o LUMNA usa já são
/// protegidos pelo próprio pacman (são dependências do lumna-session).
const ESSENTIAL: &[&str] = &[
    "base", "filesystem", "glibc", "bash", "coreutils", "systemd", "pacman", "sudo", "linux", "linux-lts",
    "linux-zen", "linux-firmware", "mkinitcpio", "grub", "efibootmgr", "archiso", "greetd", "polkit",
];

pub fn essential(name: &str) -> bool {
    ESSENTIAL.contains(&name) || name.starts_with("lumna-") || name.starts_with("linux-firmware")
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Update {
    pub source: &'static str,
    pub name: String,
    pub from: String,
    pub to: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct Sources {
    pub multilib: bool,
    pub flatpak: bool,
    pub flathub: bool,
    pub aur: bool,
    /// O AUR precisa de git e base-devel para compilar.
    pub aur_tools: bool,
}

#[derive(Debug, Clone, Serialize)]
struct Job<'a> {
    key: &'a str,
    line: &'a str,
}

#[derive(Default, Serialize, Deserialize)]
struct Settings {
    #[serde(default = "yes")]
    aur: bool,
}

fn yes() -> bool {
    true
}

fn settings_path() -> Option<PathBuf> {
    let base = std::env::var_os("XDG_CONFIG_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config")))?;
    Some(base.join("lumna/store.json"))
}

fn settings() -> Settings {
    settings_path()
        .and_then(|p| std::fs::read_to_string(p).ok())
        .and_then(|s| serde_json::from_str(&s).ok())
        .unwrap_or(Settings { aur: true })
}

fn output(program: &str, args: &[&str]) -> Option<String> {
    let out = Command::new(program).args(args).env("LC_ALL", "C").stdin(Stdio::null()).output().ok()?;
    out.status.success().then(|| String::from_utf8_lossy(&out.stdout).into_owned())
}

fn exists(program: &str) -> bool {
    std::env::var_os("PATH")
        .is_some_and(|paths| std::env::split_paths(&paths).any(|dir| dir.join(program).is_file()))
}

/// Nomes de pacote vêm da interface e viram argumentos: nada que pareça opção ou caminho.
pub fn valid_name(name: &str) -> bool {
    !name.is_empty()
        && !name.starts_with('-')
        && name.chars().all(|c| c.is_ascii_alphanumeric() || "@._+-".contains(c))
}

// ---- leitura das saídas ----

/// `pacman -Ss`: `extra/firefox 131.0-1 [installed]` e a descrição na linha de baixo.
pub fn parse_pacman_search(out: &str) -> Vec<Package> {
    let mut list: Vec<Package> = Vec::new();
    for line in out.lines() {
        if let Some(summary) = line.strip_prefix("    ") {
            if let Some(last) = list.last_mut() {
                last.summary = summary.trim().to_string();
            }
            continue;
        }
        let mut parts = line.split_whitespace();
        let (Some(id), Some(version)) = (parts.next(), parts.next()) else { continue };
        let Some((repo, name)) = id.split_once('/') else { continue };
        list.push(Package {
            source: "arch",
            name: name.to_string(),
            title: name.to_string(),
            summary: String::new(),
            version: version.to_string(),
            repo: repo.to_string(),
            installed: line.contains("[installed"),
            icon: None,
            essential: essential(name),
        });
    }
    list
}

/// `pacman -Qi`: blocos `Name : x`, `Version : y`, `Description : z` separados por linha em branco.
pub fn parse_pacman_info(out: &str, source: &'static str) -> Vec<Package> {
    out.split("\n\n")
        .filter_map(|block| {
            let field = |key: &str| {
                block.lines().find_map(|l| {
                    let (k, v) = l.split_once(':')?;
                    (k.trim() == key).then(|| v.trim().to_string())
                })
            };
            let name = field("Name")?;
            Some(Package {
                source,
                title: name.clone(),
                summary: field("Description").unwrap_or_default(),
                version: field("Version").unwrap_or_default(),
                repo: String::new(),
                installed: true,
                icon: None,
                essential: essential(&name),
                name,
            })
        })
        .collect()
}

/// `flatpak search --columns=application,name,description,version,remotes` (separado por tab).
pub fn parse_flatpak_search(out: &str, installed: &HashSet<String>) -> Vec<Package> {
    out.lines()
        .filter_map(|line| {
            let f: Vec<&str> = line.split('\t').collect();
            let (id, name) = (*f.first()?, *f.get(1)?);
            if !id.contains('.') || !f.get(4).is_some_and(|r| r.split(',').any(|r| r.trim() == "flathub")) || !is_app(id) {
                return None;
            }
            Some(flathub_package(id, name, f.get(2).copied().unwrap_or(""), f.get(3).copied().unwrap_or(""), installed.contains(id)))
        })
        .collect()
}

/// O `flatpak search` traz também temas, plataformas e extensões, que não são programas.
pub fn is_app(id: &str) -> bool {
    const PARTS: &[&str] = &[
        "Gtk3theme", "KStyle", "PlatformTheme", "Platform", "Sdk", "BaseApp", "Extension", "Plugin", "Codecs",
        "Debug", "Locale", "GL", "GL32", "VAAPI", "openh264", "ffmpeg-full", "Icontheme", "WaylandDecoration",
    ];
    !id.split('.').any(|part| PARTS.contains(&part))
}

fn flathub_package(id: &str, name: &str, summary: &str, version: &str, installed: bool) -> Package {
    Package {
        source: "flathub",
        name: id.to_string(),
        title: name.to_string(),
        summary: summary.to_string(),
        version: version.to_string(),
        repo: "flathub".into(),
        installed,
        icon: Some(format!("{}/{id}.png", flathub_icons())),
        essential: false,
    }
}

/// `flatpak list --app --columns=application,name,version,description`.
pub fn parse_flatpak_list(out: &str) -> Vec<Package> {
    out.lines()
        .filter_map(|line| {
            let f: Vec<&str> = line.split('\t').collect();
            let id = *f.first()?;
            id.contains('.').then(|| {
                flathub_package(id, f.get(1).copied().unwrap_or(id), f.get(3).copied().unwrap_or(""), f.get(2).copied().unwrap_or(""), true)
            })
        })
        .collect()
}

/// Busca do AUR (RPC v5), os mais populares primeiro.
pub fn parse_aur(json: &str, installed: &HashSet<String>) -> Vec<Package> {
    let Ok(value) = serde_json::from_str::<Value>(json) else { return Vec::new() };
    let mut results: Vec<&Value> = value["results"].as_array().map(|r| r.iter().collect()).unwrap_or_default();
    results.sort_by(|a, b| b["Popularity"].as_f64().unwrap_or(0.0).total_cmp(&a["Popularity"].as_f64().unwrap_or(0.0)));
    results
        .into_iter()
        .filter_map(|r| {
            let name = r["Name"].as_str()?.to_string();
            Some(Package {
                source: "aur",
                title: name.clone(),
                summary: r["Description"].as_str().unwrap_or_default().to_string(),
                version: r["Version"].as_str().unwrap_or_default().to_string(),
                repo: r["PackageBase"].as_str().unwrap_or(&name).to_string(),
                installed: installed.contains(&name),
                icon: None,
                essential: false,
                name,
            })
        })
        .collect()
}

/// `checkupdates`: `firefox 130.0-1 -> 131.0-1`.
pub fn parse_checkupdates(out: &str) -> Vec<Update> {
    out.lines()
        .filter_map(|l| {
            let f: Vec<&str> = l.split_whitespace().collect();
            (f.len() == 4 && f[2] == "->").then(|| Update { source: "arch", name: f[0].into(), from: f[1].into(), to: f[3].into() })
        })
        .collect()
}

/// Codifica a busca para a URL do AUR.
pub fn url_encode(text: &str) -> String {
    text.bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => (b as char).to_string(),
            _ => format!("%{b:02X}"),
        })
        .collect()
}

// ---- estado das fontes ----

fn multilib_enabled() -> bool {
    std::fs::read_to_string("/etc/pacman.conf").is_ok_and(|c| c.lines().any(|l| l.trim() == "[multilib]"))
}

/// O Flathub da instalação da pessoa (`--user`), que é onde a Loja instala. O flatpak do Arch já vem
/// com um Flathub de sistema: ele aparece em `flatpak remotes`, mas não serve para `install --user`
/// ("No remote refs found for 'flathub'").
fn flathub_enabled() -> bool {
    output("flatpak", &["remotes", "--user", "--columns=name"]).is_some_and(|o| o.lines().any(|l| l.trim() == "flathub"))
}

fn foreign() -> HashSet<String> {
    output("pacman", &["-Qmq"]).map(|o| o.lines().map(str::to_string).collect()).unwrap_or_default()
}

fn flatpak_installed() -> HashSet<String> {
    output("flatpak", &["list", "--app", "--columns=application"])
        .map(|o| o.lines().map(|l| l.trim().to_string()).collect())
        .unwrap_or_default()
}

fn aur_info(names: &[String]) -> Vec<Value> {
    if names.is_empty() {
        return Vec::new();
    }
    let query: String = names.iter().map(|n| format!("&arg[]={}", url_encode(n))).collect();
    let url = format!("{AUR}/rpc/v5/info?{}", &query[1..]);
    output("curl", &["-fsS", "--max-time", "15", &url])
        .and_then(|o| serde_json::from_str::<Value>(&o).ok())
        .and_then(|v| v["results"].as_array().cloned())
        .unwrap_or_default()
}

// ---- comandos de leitura ----

#[tauri::command]
pub async fn store_sources() -> Sources {
    tauri::async_runtime::spawn_blocking(|| Sources {
        multilib: multilib_enabled(),
        flatpak: exists("flatpak"),
        flathub: flathub_enabled(),
        aur: settings().aur,
        aur_tools: exists("git") && exists("makepkg") && std::path::Path::new("/usr/bin/fakeroot").exists(),
    })
    .await
    .unwrap_or(Sources { multilib: false, flatpak: false, flathub: false, aur: false, aur_tools: false })
}

/// Busca nas três fontes ao mesmo tempo. Nome exato primeiro, depois a ordem de cada fonte.
#[tauri::command]
pub async fn store_search(query: String) -> Vec<Package> {
    let query = query.trim().to_string();
    if query.len() < 2 {
        return Vec::new();
    }
    let q = query.clone();
    let arch = tauri::async_runtime::spawn_blocking(move || {
        // Cada palavra vira um filtro do pacman (que trata o termo como expressão regular).
        let words: Vec<String> = q.split_whitespace().map(regex_escape).collect();
        let args: Vec<&str> = std::iter::once("-Ss").chain(words.iter().map(String::as_str)).collect();
        output("pacman", &args).map(|o| parse_pacman_search(&o)).unwrap_or_default()
    });
    let q = query.clone();
    let flathub = tauri::async_runtime::spawn_blocking(move || {
        if !exists("flatpak") {
            return Vec::new();
        }
        let installed = flatpak_installed();
        output("flatpak", &["search", "--columns=application,name,description,version,remotes", &q])
            .map(|o| parse_flatpak_search(&o, &installed))
            .unwrap_or_default()
    });
    let q = query.clone();
    let aur = tauri::async_runtime::spawn_blocking(move || {
        if !settings().aur {
            return Vec::new();
        }
        let url = format!("{AUR}/rpc/v5/search/{}?by=name-desc", url_encode(&q));
        let installed = foreign();
        output("curl", &["-fsS", "--max-time", "10", &url]).map(|o| parse_aur(&o, &installed)).unwrap_or_default()
    });

    let mut all = Vec::new();
    for (task, limit) in [(arch, 40), (flathub, 40), (aur, 30)] {
        let mut list = task.await.unwrap_or_default();
        list.truncate(limit);
        all.extend(list);
    }
    let lower = query.to_lowercase();
    // sort estável: dentro de cada grupo, a ordem de cada fonte continua.
    all.sort_by_key(|p| {
        let name = p.name.to_lowercase();
        let title = p.title.to_lowercase();
        if title == lower || name == lower || name.ends_with(&format!(".{lower}")) {
            0
        } else if title.starts_with(&lower) || name.starts_with(&lower) {
            1
        } else {
            2
        }
    });
    all
}

fn regex_escape(word: &str) -> String {
    word.chars().map(|c| if "\\.^$|?*+()[]{}".contains(c) { format!("\\{c}") } else { c.to_string() }).collect()
}

/// Programas instalados: os do Arch escolhidos de propósito (não as dependências), os do AUR e
/// os do Flathub.
#[tauri::command]
pub async fn store_installed() -> Vec<Package> {
    tauri::async_runtime::spawn_blocking(|| {
        let mut list = Vec::new();
        // -Qen: explícitos dos repositórios; -Qm: de fora (AUR).
        list.extend(output("pacman", &["-Qein"]).map(|o| parse_pacman_info(&o, "arch")).unwrap_or_default());
        list.extend(output("pacman", &["-Qim"]).map(|o| parse_pacman_info(&o, "aur")).unwrap_or_default());
        if exists("flatpak") {
            list.extend(
                output("flatpak", &["list", "--app", "--columns=application,name,version,description"])
                    .map(|o| parse_flatpak_list(&o))
                    .unwrap_or_default(),
            );
        }
        list.sort_by_key(|p| p.title.to_lowercase());
        list
    })
    .await
    .unwrap_or_default()
}

/// Atualizações disponíveis nas três fontes (sem mexer no sistema: o `checkupdates` usa uma
/// cópia do banco do pacman).
#[tauri::command]
pub async fn store_updates() -> Vec<Update> {
    tauri::async_runtime::spawn_blocking(|| {
        let mut list = output("checkupdates", &[]).map(|o| parse_checkupdates(&o)).unwrap_or_default();
        if exists("flatpak") {
            let out = output("flatpak", &["remote-ls", "--updates", "--app", "--columns=application,version"]).unwrap_or_default();
            list.extend(out.lines().filter_map(|l| {
                let (id, version) = l.split_once('\t').unwrap_or((l, ""));
                id.contains('.').then(|| Update { source: "flathub", name: id.into(), from: String::new(), to: version.into() })
            }));
        }
        if settings().aur {
            let installed: Vec<(String, String)> = output("pacman", &["-Qm"])
                .map(|o| o.lines().filter_map(|l| l.split_once(' ').map(|(n, v)| (n.to_string(), v.to_string()))).collect())
                .unwrap_or_default();
            let names: Vec<String> = installed.iter().map(|(n, _)| n.clone()).collect();
            for info in aur_info(&names) {
                let (Some(name), Some(latest)) = (info["Name"].as_str(), info["Version"].as_str()) else { continue };
                let Some((_, current)) = installed.iter().find(|(n, _)| n == name) else { continue };
                let newer = output("vercmp", &[latest, current]).is_some_and(|o| o.trim() == "1");
                if newer {
                    list.push(Update { source: "aur", name: name.into(), from: current.clone(), to: latest.into() });
                }
            }
        }
        list
    })
    .await
    .unwrap_or_default()
}

// ---- operações ----

static BUSY: AtomicBool = AtomicBool::new(false);

/// Roda um comando mandando cada linha da saída para a tela. Em erro, devolve as últimas linhas
/// (o pacman e o makepkg explicam o problema no fim).
fn stream(app: &AppHandle, key: &str, mut command: Command) -> Result<(), String> {
    command.env("LC_ALL", "C").stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
    let program = command.get_program().to_string_lossy().into_owned();
    let mut child = command.spawn().map_err(|e| format!("{program}: {e}"))?;
    let stderr = child.stderr.take().map(|err| {
        std::thread::spawn(move || BufReader::new(err).lines().map_while(Result::ok).collect::<Vec<_>>())
    });
    if let Some(out) = child.stdout.take() {
        for line in BufReader::new(out).lines().map_while(Result::ok) {
            let line = line.trim();
            if !line.is_empty() {
                let _ = app.emit("lumna://store-job", Job { key, line });
            }
        }
    }
    let status = child.wait().map_err(|e| e.to_string())?;
    let errors = stderr.and_then(|h| h.join().ok()).unwrap_or_default();
    if status.success() {
        return Ok(());
    }
    // 126/127 do pkexec: a pessoa cancelou ou errou a senha.
    if program == "pkexec" && matches!(status.code(), Some(126) | Some(127)) {
        return Err("Precisa da senha de administrador para continuar.".into());
    }
    let useful: Vec<&String> = errors.iter().filter(|l| !l.trim().is_empty()).collect();
    let tail: Vec<&str> = useful.iter().rev().take(3).rev().map(|s| s.as_str()).collect();
    Err(explain(&tail.join("\n")))
}

/// Traduz os erros mais comuns do pacman e do makepkg.
pub fn explain(error: &str) -> String {
    let e = error.to_lowercase();
    if e.contains("target not found") {
        "O pacote não foi encontrado. Atualize o sistema (aba Atualizações) e tente de novo.".into()
    } else if e.contains("failed retrieving file") || e.contains("could not resolve host") {
        "Sem internet, ou o servidor não respondeu. Confira a conexão.".into()
    } else if e.contains("unable to lock database") {
        "Outra instalação está rodando. Espere ela terminar.".into()
    } else if e.contains("could not find all required packages") || e.contains("missing dependencies") {
        format!("Faltam dependências que não estão nos repositórios (talvez outro pacote do AUR):\n{error}")
    } else if e.contains("unknown public key") {
        format!("O pacote é assinado por uma chave que este computador ainda não conhece:\n{error}")
    } else if error.is_empty() {
        "A operação falhou.".into()
    } else {
        error.to_string()
    }
}

/// Guarda contra duas operações ao mesmo tempo (o pacman só deixa uma).
struct Busy;
impl Busy {
    fn take() -> Result<Self, String> {
        if BUSY.swap(true, Ordering::SeqCst) {
            Err("Já tem uma instalação em andamento. Espere ela terminar.".into())
        } else {
            Ok(Busy)
        }
    }
}
impl Drop for Busy {
    fn drop(&mut self) {
        BUSY.store(false, Ordering::SeqCst);
    }
}

fn pkexec(args: &[&str]) -> Command {
    let mut command = Command::new("pkexec");
    command.arg(HELPER).args(args);
    command
}

fn ensure_flathub() -> Result<(), String> {
    if flathub_enabled() {
        return Ok(());
    }
    let ok = Command::new("flatpak")
        .args(["remote-add", "--user", "--if-not-exists", "flathub", FLATHUB])
        .status()
        .is_ok_and(|s| s.success());
    if !ok {
        return Err("Não deu para adicionar o Flathub. Confira a internet.".into());
    }
    // O catálogo (nomes, descrições) vem à parte.
    let _ = Command::new("flatpak").args(["update", "--user", "--appstream", "flathub"]).status();
    Ok(())
}

fn aur_dir() -> Result<PathBuf, String> {
    let home = std::env::var_os("HOME").ok_or("sem HOME")?;
    let dir = PathBuf::from(home).join(".cache/lumna/aur");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    Ok(dir)
}

/// Baixa (ou atualiza) o PKGBUILD do AUR e compila com o makepkg, que instala pelo `lumna-pkg`.
fn build_aur(app: &AppHandle, key: &str, base: &str) -> Result<(), String> {
    let root = aur_dir()?;
    let dir = root.join(base);
    let mut git = Command::new("git");
    if dir.join(".git").exists() {
        git.args(["-C"]).arg(&dir).args(["pull", "--ff-only"]);
    } else {
        git.args(["clone", "--depth", "1", &format!("{AUR}/{base}.git")]).arg(&dir);
    }
    stream(app, key, git)?;
    if !dir.join("PKGBUILD").exists() {
        return Err("Esse pacote não existe mais no AUR.".into());
    }
    // O makepkg usa o PACMAN_AUTH para virar administrador só na hora de instalar.
    let conf = root.join("makepkg.conf");
    std::fs::write(&conf, format!("source /etc/makepkg.conf\nPACMAN_AUTH=(pkexec {HELPER} pacman)\n"))
        .map_err(|e| e.to_string())?;
    let mut makepkg = Command::new("makepkg");
    makepkg.current_dir(&dir).arg("--config").arg(&conf).args(["-si", "--noconfirm", "--needed", "--noprogressbar"]);
    stream(app, key, makepkg)
}

#[tauri::command]
pub async fn store_install(app: AppHandle, source: String, name: String, repo: String) -> Result<(), String> {
    if !valid_name(&name) || !(repo.is_empty() || valid_name(&repo)) {
        return Err("Nome de pacote inválido.".into());
    }
    tauri::async_runtime::spawn_blocking(move || {
        let _busy = Busy::take()?;
        let key = format!("{source}:{name}");
        match source.as_str() {
            "arch" => {
                if repo == "multilib" && !multilib_enabled() {
                    stream(&app, &key, pkexec(&["multilib", "on"]))?;
                }
                stream(&app, &key, pkexec(&["install", &name]))
            }
            "flathub" => {
                ensure_flathub()?;
                let mut c = Command::new("flatpak");
                c.args(["install", "--user", "-y", "--noninteractive", "flathub", &name]);
                stream(&app, &key, c)
            }
            "aur" => build_aur(&app, &key, if repo.is_empty() { &name } else { &repo }),
            _ => Err("Fonte desconhecida.".into()),
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn store_remove(app: AppHandle, source: String, name: String) -> Result<(), String> {
    if !valid_name(&name) {
        return Err("Nome de pacote inválido.".into());
    }
    if source != "flathub" && essential(&name) {
        return Err("Esse pacote é essencial para o sistema e não pode ser removido pela Loja.".into());
    }
    tauri::async_runtime::spawn_blocking(move || {
        let _busy = Busy::take()?;
        let key = format!("{source}:{name}");
        match source.as_str() {
            "arch" | "aur" => stream(&app, &key, pkexec(&["remove", &name])),
            "flathub" => {
                let mut c = Command::new("flatpak");
                c.args(["uninstall", "-y", "--noninteractive", &name]);
                stream(&app, &key, c)
            }
            _ => Err("Fonte desconhecida.".into()),
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Atualiza tudo de uma fonte (`arch`, `flathub` ou `aur`).
#[tauri::command]
pub async fn store_upgrade(app: AppHandle, source: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let _busy = Busy::take()?;
        let key = format!("upgrade:{source}");
        match source.as_str() {
            "arch" => stream(&app, &key, pkexec(&["upgrade"])),
            "flathub" => {
                let mut c = Command::new("flatpak");
                c.args(["update", "-y", "--noninteractive"]);
                stream(&app, &key, c)
            }
            "aur" => {
                let names: Vec<String> = output("pacman", &["-Qmq"]).map(|o| o.lines().map(str::to_string).collect()).unwrap_or_default();
                for info in aur_info(&names) {
                    let (Some(name), Some(base)) = (info["Name"].as_str(), info["PackageBase"].as_str()) else { continue };
                    let current = output("pacman", &["-Q", name]).and_then(|o| o.split_whitespace().nth(1).map(str::to_string));
                    let latest = info["Version"].as_str().unwrap_or_default();
                    let newer = current.is_some_and(|c| output("vercmp", &[latest, &c]).is_some_and(|o| o.trim() == "1"));
                    if newer && valid_name(base) {
                        build_aur(&app, &key, base)?;
                    }
                }
                Ok(())
            }
            _ => Err("Fonte desconhecida.".into()),
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

/// Liga o [multilib] (pacotes de 32 bits: Steam, Wine) no /etc/pacman.conf.
#[tauri::command]
pub async fn store_set_multilib(app: AppHandle, on: bool) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let _busy = Busy::take()?;
        stream(&app, "source:multilib", pkexec(&["multilib", if on { "on" } else { "off" }]))
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn store_set_flathub(on: bool) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        if on {
            ensure_flathub()
        } else {
            let ok = Command::new("flatpak").args(["remote-delete", "--user", "--force", "flathub"]).status().is_ok_and(|s| s.success());
            ok.then_some(()).ok_or_else(|| "Não deu para remover o Flathub.".into())
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn store_set_aur(on: bool) -> Result<(), String> {
    let path = settings_path().ok_or("sem pasta de configuração")?;
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(&path, serde_json::to_string(&Settings { aur: on }).unwrap()).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_pacman() {
        let out = "extra/firefox 131.0-1 [installed]\n    Fast, Private & Safe Web Browser\nmultilib/steam 1.0.0.81-1\n    Valve's digital software delivery system\n";
        let list = parse_pacman_search(out);
        assert_eq!(list.len(), 2);
        assert_eq!((list[0].name.as_str(), list[0].repo.as_str(), list[0].installed), ("firefox", "extra", true));
        assert_eq!(list[1].summary, "Valve's digital software delivery system");
        assert!(!list[1].installed);

        let info = "Name            : mpv\nVersion         : 1:0.39.0-4\nDescription     : a free media player\n\nName            : foot\nVersion         : 1.20-1\nDescription     : Fast terminal: wayland\n";
        let list = parse_pacman_info(info, "arch");
        assert_eq!(list.len(), 2);
        assert_eq!(list[1].summary, "Fast terminal: wayland");
        assert_eq!(list[0].version, "1:0.39.0-4");

        let up = parse_checkupdates("firefox 130.0-1 -> 131.0-1\nlixo\n");
        assert_eq!(up, vec![Update { source: "arch", name: "firefox".into(), from: "130.0-1".into(), to: "131.0-1".into() }]);
    }

    #[test]
    fn reads_flatpak_and_aur() {
        let installed = HashSet::from(["com.spotify.Client".to_string()]);
        let out = "com.spotify.Client\tSpotify\tOnline music\t1.2\tflathub\norg.other.App\tOther\tx\t1\tfedora\n";
        let list = parse_flatpak_search(out, &installed);
        assert_eq!(list.len(), 1);
        assert!(list[0].installed);
        assert_eq!(list[0].icon.as_deref(), Some("https://dl.flathub.org/repo/appstream/x86_64/icons/128x128/com.spotify.Client.png"));

        let json = r#"{"results":[
            {"Name":"pouco","PackageBase":"pouco","Version":"1","Description":"a","Popularity":0.1},
            {"Name":"visual-studio-code-bin","PackageBase":"visual-studio-code-bin","Version":"1.95-1","Description":"VS Code","Popularity":20.5}]}"#;
        let list = parse_aur(json, &HashSet::new());
        assert_eq!(list[0].name, "visual-studio-code-bin");
        assert_eq!(list[0].repo, "visual-studio-code-bin");
        assert!(parse_aur("{}", &HashSet::new()).is_empty());
        assert!(is_app("com.obsproject.Studio"));
        assert!(!is_app("org.gtk.Gtk3theme.Obsidian-2"));
        assert!(!is_app("com.obsproject.Studio.Plugin.Gstreamer"));
        assert!(!is_app("org.freedesktop.Platform.GL.default"));
    }

    #[test]
    fn guards_and_text() {
        assert!(valid_name("visual-studio-code-bin"));
        assert!(valid_name("com.spotify.Client"));
        assert!(!valid_name("--overwrite"));
        assert!(!valid_name("a b"));
        assert!(!valid_name("../x"));
        assert_eq!(url_encode("vs code+"), "vs%20code%2B");
        assert_eq!(regex_escape("c++"), "c\\+\\+");
        assert!(essential("linux") && essential("lumna-shell") && essential("linux-firmware-amdgpu"));
        assert!(!essential("firefox"));
        assert!(explain("error: target not found: xyz").starts_with("O pacote não foi encontrado"));
    }
}
