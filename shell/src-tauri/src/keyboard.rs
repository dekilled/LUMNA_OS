//! Teclado e idioma.
//!
//! Teclado: um ou mais layouts do xkb (Alt + Shift troca entre eles). Vale na hora (o Hyprland
//! recarrega o teclado) e fica em `~/.config/lumna/keyboard.json`; a regra pronta vai para
//! `~/.config/lumna/keyboard.lua`, que o `hyprland.lua` carrega no login. Sem escolha da pessoa,
//! vale o teclado da instalação (`/etc/lumna/keyboard`).
//!
//! Idioma do sistema: `localectl set-locale` (pede a senha de administrador pelo polkit) e vale a
//! partir do próximo login. Só aparecem os idiomas já gerados no sistema.

use std::path::{Path, PathBuf};
use std::process::Command;

use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter, Manager};

use crate::SharedCompositor;

const RULES: &str = "/usr/share/X11/xkb/rules/base.lst";
/// Alt + Shift troca de layout quando há mais de um.
const SWITCH_OPTION: &str = "grp:alt_shift_toggle";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Layout {
    pub layout: String,
    /// Vazio = a variante padrão do layout.
    #[serde(default)]
    pub variant: String,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Variant {
    pub id: String,
    pub name: String,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct CatalogLayout {
    pub id: String,
    pub name: String,
    pub variants: Vec<Variant>,
}

/// Lê o catálogo do xkb: as seções `! layout` e `! variant` do `base.lst`.
pub fn parse_catalog(text: &str) -> Vec<CatalogLayout> {
    let mut layouts: Vec<CatalogLayout> = Vec::new();
    let mut section = "";
    for line in text.lines() {
        if let Some(name) = line.strip_prefix("! ") {
            section = match name.trim() {
                "layout" => "layout",
                "variant" => "variant",
                _ => "",
            };
            continue;
        }
        let Some((id, rest)) = line.trim().split_once(char::is_whitespace) else { continue };
        let rest = rest.trim();
        match section {
            "layout" => layouts.push(CatalogLayout { id: id.into(), name: rest.into(), variants: Vec::new() }),
            "variant" => {
                // "  nodeadkeys      br: Portuguese (Brazil, no dead keys)"
                let Some((parent, name)) = rest.split_once(": ") else { continue };
                if let Some(layout) = layouts.iter_mut().find(|l| l.id == parent) {
                    layout.variants.push(Variant { id: id.into(), name: name.into() });
                }
            }
            _ => {}
        }
    }
    layouts.sort_by(|a, b| a.name.cmp(&b.name));
    layouts
}

fn config_dir() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna")
}

/// "br abnt2" (o formato de `/etc/lumna/keyboard`) → layout e variante.
fn from_install(text: &str) -> Option<Layout> {
    let mut parts = text.split_whitespace();
    let layout = parts.next()?.to_string();
    Some(Layout { layout, variant: parts.next().unwrap_or_default().to_string() })
}

pub fn current_layouts() -> Vec<Layout> {
    let saved = std::fs::read_to_string(config_dir().join("keyboard.json"))
        .ok()
        .and_then(|t| serde_json::from_str::<Vec<Layout>>(&t).ok())
        .filter(|list| !list.is_empty());
    saved
        .or_else(|| std::fs::read_to_string("/etc/lumna/keyboard").ok().and_then(|t| from_install(&t)).map(|l| vec![l]))
        .unwrap_or_else(|| vec![Layout { layout: "br".into(), variant: "abnt2".into() }])
}

/// Os três campos do xkb para a lista de layouts.
pub fn xkb_fields(layouts: &[Layout]) -> (String, String, String) {
    let join = |f: fn(&Layout) -> &str| layouts.iter().map(f).collect::<Vec<_>>().join(",");
    let options = if layouts.len() > 1 { SWITCH_OPTION.to_string() } else { String::new() };
    (join(|l| &l.layout), join(|l| &l.variant), options)
}

// ---- idioma ----

fn system_locale() -> String {
    std::fs::read_to_string("/etc/locale.conf")
        .ok()
        .and_then(|t| t.lines().find_map(|l| l.strip_prefix("LANG=").map(|v| v.trim().trim_matches('"').to_string())))
        .unwrap_or_else(|| "pt_BR.UTF-8".into())
}

fn available_locales() -> Vec<String> {
    let output = Command::new("localectl").arg("list-locales").output();
    let mut list: Vec<String> = output
        .map(|o| String::from_utf8_lossy(&o.stdout).lines().map(|l| l.trim().to_string()).collect())
        .unwrap_or_default();
    list.retain(|l| !l.is_empty() && l != "C.UTF-8" && l != "C" && l != "POSIX");
    list
}

/// "pt_BR.UTF-8" → "Português (Brasil)". Os que não estão aqui aparecem pelo código.
pub fn locale_name(code: &str) -> String {
    let base = code.split('.').next().unwrap_or(code);
    let name = match base {
        "pt_BR" => "Português (Brasil)",
        "pt_PT" => "Português (Portugal)",
        "en_US" => "English (United States)",
        "en_GB" => "English (United Kingdom)",
        "es_ES" => "Español (España)",
        "es_MX" => "Español (México)",
        "es_AR" => "Español (Argentina)",
        "fr_FR" => "Français (France)",
        "de_DE" => "Deutsch (Deutschland)",
        "it_IT" => "Italiano (Italia)",
        "ja_JP" => "日本語 (日本)",
        "zh_CN" => "中文 (中国)",
        "ru_RU" => "Русский (Россия)",
        _ => return code.to_string(),
    };
    name.to_string()
}

#[derive(Debug, Clone, Serialize)]
pub struct Locale {
    pub code: String,
    pub name: String,
}

#[derive(Debug, Clone, Serialize)]
pub struct KeyboardState {
    pub layouts: Vec<Layout>,
    pub catalog: Vec<CatalogLayout>,
    pub locale: String,
    pub locales: Vec<Locale>,
}

#[tauri::command]
pub async fn keyboard_state() -> KeyboardState {
    tauri::async_runtime::spawn_blocking(|| {
        let catalog = std::fs::read_to_string(RULES).map(|t| parse_catalog(&t)).unwrap_or_default();
        let locale = system_locale();
        let mut codes = available_locales();
        // O idioma em uso aparece mesmo se não estiver na lista (ex.: C.UTF-8).
        if !codes.contains(&locale) {
            codes.insert(0, locale.clone());
        }
        let locales = codes.into_iter().map(|code| Locale { name: locale_name(&code), code }).collect();
        KeyboardState { layouts: current_layouts(), catalog, locale, locales }
    })
    .await
    .unwrap_or_else(|_| KeyboardState { layouts: current_layouts(), catalog: Vec::new(), locale: system_locale(), locales: Vec::new() })
}

/// Layouts do teclado principal e o que está em uso (indicador da topbar).
#[tauri::command]
pub fn keyboard_current(app: AppHandle) -> Result<lumna_core::KeyboardInfo, String> {
    app.state::<SharedCompositor>().keyboard()
}

#[tauri::command]
pub fn keyboard_next(app: AppHandle) -> Result<(), String> {
    app.state::<SharedCompositor>().next_keyboard_layout()
}

#[tauri::command]
pub fn keyboard_set(app: AppHandle, layouts: Vec<Layout>) -> Result<(), String> {
    if layouts.is_empty() {
        return Err("escolha pelo menos um teclado".into());
    }
    // Só nomes que o xkb conhece (variante vazia = padrão do layout).
    if Path::new(RULES).exists() {
        let catalog = parse_catalog(&std::fs::read_to_string(RULES).map_err(|e| e.to_string())?);
        for l in &layouts {
            let known = catalog.iter().find(|c| c.id == l.layout).ok_or_else(|| format!("teclado desconhecido: {}", l.layout))?;
            let symbol_default = l.layout == "br" && l.variant == "abnt2";
            if !l.variant.is_empty() && !symbol_default && !known.variants.iter().any(|v| v.id == l.variant) {
                return Err(format!("variante desconhecida: {} ({})", l.variant, l.layout));
            }
        }
    }
    let (layout, variant, options) = xkb_fields(&layouts);
    let rule = lumna_hypr::keyboard_rule(&layout, &variant, &options)?;

    let dir = config_dir();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let header = "-- Gerado pelo LUMNA (Configurações → Teclado e idioma).\n";
    std::fs::write(dir.join("keyboard.lua"), format!("{header}{rule}\n")).map_err(|e| e.to_string())?;
    let json = serde_json::to_string_pretty(&layouts).map_err(|e| e.to_string())?;
    std::fs::write(dir.join("keyboard.json"), json + "\n").map_err(|e| e.to_string())?;

    app.state::<SharedCompositor>().set_keyboard(&layout, &variant, &options)?;
    // A tela de bloqueio mostra o teclado em uso quando há mais de um.
    crate::lock::write_lock_config();
    // A topbar refaz o indicador (aparece com dois ou mais teclados).
    let _ = app.emit("lumna://keyboard", ());
    Ok(())
}

/// Troca o idioma do sistema. O `localectl` pede a senha pelo polkit (a janela de senha do LUMNA),
/// por isso roda fora da thread principal.
#[tauri::command]
pub async fn locale_set(code: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        if !available_locales().contains(&code) {
            return Err(format!("idioma não disponível: {code}"));
        }
        let output = Command::new("localectl")
            .arg("set-locale")
            .arg(format!("LANG={code}"))
            .output()
            .map_err(|e| format!("localectl: {e}"))?;
        if output.status.success() {
            Ok(())
        } else {
            let err = String::from_utf8_lossy(&output.stderr).trim().to_string();
            Err(if err.is_empty() { "não deu para trocar o idioma".into() } else { err })
        }
    })
    .await
    .map_err(|e| e.to_string())?
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = "! model\n  pc105           Generic 105-key PC\n\n! layout\n  us              English (US)\n  br              Portuguese (Brazil)\n\n! variant\n  intl            us: English (US, intl., with dead keys)\n  nodeadkeys      br: Portuguese (Brazil, no dead keys)\n  thinkpad_nodeadkeys br: Portuguese (Brazil, IBM/Lenovo ThinkPad, no dead keys)\n\n! option\n  grp:alt_shift_toggle Alt+Shift\n";

    #[test]
    fn catalog_from_base_lst() {
        let catalog = parse_catalog(SAMPLE);
        assert_eq!(catalog.iter().map(|l| l.id.as_str()).collect::<Vec<_>>(), ["us", "br"]);
        let br = &catalog[1];
        assert_eq!(br.name, "Portuguese (Brazil)");
        assert_eq!(br.variants.len(), 2);
        assert_eq!(br.variants[1].id, "thinkpad_nodeadkeys");
        assert_eq!(catalog[0].variants[0].name, "English (US, intl., with dead keys)");
    }

    #[test]
    fn xkb_fields_switch_only_with_two_or_more() {
        let br = Layout { layout: "br".into(), variant: "abnt2".into() };
        let us = Layout { layout: "us".into(), variant: "intl".into() };
        assert_eq!(xkb_fields(&[br.clone()]), ("br".into(), "abnt2".into(), "".into()));
        assert_eq!(
            xkb_fields(&[br, us]),
            ("br,us".into(), "abnt2,intl".into(), SWITCH_OPTION.into())
        );
        assert_eq!(from_install("us intl\n"), Some(Layout { layout: "us".into(), variant: "intl".into() }));
        assert_eq!(from_install("us\n"), Some(Layout { layout: "us".into(), variant: "".into() }));
    }

    #[test]
    fn locale_names() {
        assert_eq!(locale_name("pt_BR.UTF-8"), "Português (Brasil)");
        assert_eq!(locale_name("sv_SE.UTF-8"), "sv_SE.UTF-8");
    }
}
