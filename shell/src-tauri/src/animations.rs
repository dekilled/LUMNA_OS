//! Modo desenvolvedor → Animações: as animações do Hyprland ajustadas ao vivo (pelo `hyprctl
//! eval`, a config em Lua) e salvas em `~/.config/lumna/animations.lua`, que a config do Hyprland
//! carrega ao iniciar. Velocidade no Hyprland é em décimos de segundo: menor = mais rápido.

use std::path::PathBuf;
use std::process::Command;

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Animations {
    pub enabled: bool,
    /// Abrir/fechar/mover janelas.
    pub windows: f32,
    /// "popin", "slide" ou "gnomed".
    pub windows_style: String,
    /// Barras e painéis do LUMNA (camadas).
    pub layers: f32,
    pub fade: f32,
    pub workspaces: f32,
    /// Curva: "lumna" (freia suave), "rapida" (quase linear) ou "mola".
    pub curve: String,
}

impl Default for Animations {
    fn default() -> Self {
        Animations {
            enabled: true,
            windows: 2.5,
            windows_style: "popin".into(),
            layers: 2.0,
            fade: 2.0,
            workspaces: 2.5,
            curve: "lumna".into(),
        }
    }
}

fn path() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from("/"));
    std::env::var_os("XDG_CONFIG_HOME").map(PathBuf::from).unwrap_or_else(|| home.join(".config")).join("lumna/animations.json")
}

fn load() -> Animations {
    std::fs::read_to_string(path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
}

/// O Lua que aplica a configuração (o mesmo vai pro arquivo e pro `hyprctl eval`).
fn lua(a: &Animations) -> Vec<String> {
    let clamp = |v: f32| v.clamp(0.5, 10.0);
    let on = a.enabled;
    let curve = match a.curve.as_str() {
        "rapida" => "bezier = \"lumnaFast\"",
        "mola" => "spring = \"lumnaSpring\"",
        _ => "bezier = \"lumna\"",
    };
    let style = match a.windows_style.as_str() {
        "slide" => "slide",
        "gnomed" => "gnomed",
        _ => "popin 90%",
    };
    vec![
        "hl.curve(\"lumna\", { type = \"bezier\", points = { { 0.22, 1 }, { 0.36, 1 } } })".into(),
        "hl.curve(\"lumnaFast\", { type = \"bezier\", points = { { 0.2, 0.8 }, { 0.3, 1 } } })".into(),
        "hl.curve(\"lumnaSpring\", { type = \"spring\", mass = 1, stiffness = 260, dampening = 26 })".into(),
        format!("hl.config({{ animations = {{ enabled = {on} }} }})"),
        format!("hl.animation({{ leaf = \"windows\", enabled = {on}, speed = {:.1}, {curve} }})", clamp(a.windows)),
        format!("hl.animation({{ leaf = \"windowsIn\", enabled = {on}, speed = {:.1}, {curve}, style = \"{style}\" }})", clamp(a.windows)),
        format!("hl.animation({{ leaf = \"windowsOut\", enabled = {on}, speed = {:.1}, bezier = \"lumnaFast\", style = \"{style}\" }})", clamp(a.windows * 0.7)),
        format!("hl.animation({{ leaf = \"layers\", enabled = {on}, speed = {:.1}, {curve} }})", clamp(a.layers)),
        format!("hl.animation({{ leaf = \"layersIn\", enabled = {on}, speed = {:.1}, {curve}, style = \"fade\" }})", clamp(a.layers)),
        format!("hl.animation({{ leaf = \"layersOut\", enabled = {on}, speed = {:.1}, bezier = \"lumnaFast\", style = \"fade\" }})", clamp(a.layers * 0.7)),
        format!("hl.animation({{ leaf = \"fade\", enabled = {on}, speed = {:.1}, bezier = \"lumna\" }})", clamp(a.fade)),
        format!("hl.animation({{ leaf = \"workspaces\", enabled = {on}, speed = {:.1}, {curve}, style = \"slidefade 12%\" }})", clamp(a.workspaces)),
        format!("hl.animation({{ leaf = \"specialWorkspace\", enabled = {on}, speed = {:.1}, {curve}, style = \"fade\" }})", clamp(a.workspaces)),
    ]
}

fn apply(a: &Animations) -> Result<(), String> {
    let mut failed = Vec::new();
    for line in lua(a) {
        match Command::new("hyprctl").args(["eval", &line]).output() {
            Ok(out) if String::from_utf8_lossy(&out.stdout).trim() == "ok" => {}
            Ok(out) => failed.push(String::from_utf8_lossy(&out.stdout).trim().to_string()),
            Err(e) => return Err(format!("hyprctl: {e}")),
        }
    }
    if failed.is_empty() {
        Ok(())
    } else {
        Err(format!("o Hyprland recusou: {}", failed.join("; ")))
    }
}

fn save(a: &Animations) -> Result<(), String> {
    let path = path();
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(&path, serde_json::to_string_pretty(a).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    // e o Lua que o Hyprland carrega ao iniciar
    let text = format!("-- Gerado pelo LUMNA (Ajustes → Desenvolvedor → Animações).\n{}\n", lua(a).join("\n"));
    std::fs::write(path.with_extension("lua"), text).map_err(|e| e.to_string())
}

#[tauri::command]
pub fn animations_get() -> Animations {
    load()
}

/// Aplica na hora e guarda.
#[tauri::command]
pub async fn animations_set(config: Animations) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        save(&config)?;
        apply(&config)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn animations_reset() -> Result<Animations, String> {
    let a = Animations::default();
    animations_set(a.clone()).await?;
    Ok(a)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lua_valido() {
        let lines = lua(&Animations::default());
        assert!(lines.iter().any(|l| l.contains("leaf = \"windowsIn\"") && l.contains("speed = 2.5") && l.contains("popin 90%")));
        let off = lua(&Animations { enabled: false, ..Default::default() });
        assert!(off.iter().any(|l| l == "hl.config({ animations = { enabled = false } })"));
        let fast = lua(&Animations { windows: 0.1, ..Default::default() });
        assert!(fast.iter().any(|l| l.contains("leaf = \"windows\"") && l.contains("speed = 0.5")));
    }
}
