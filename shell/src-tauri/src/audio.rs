//! Som pelo PipeWire: saídas, entradas e o volume de cada app. Lê tudo de uma vez pelo `pw-dump`
//! (JSON) e muda pelo `wpctl`, o mesmo que a topbar já usa para o volume.

use std::process::Command;

use serde::Serialize;
use serde_json::Value;

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Node {
    /// Id do PipeWire (o que o `wpctl` recebe).
    pub id: u64,
    pub name: String,
    /// 0 a 150 (%), na mesma escala do `wpctl`.
    pub volume: u8,
    pub muted: bool,
    /// Saída/entrada padrão (para os apps, sempre falso).
    pub default: bool,
}

#[derive(Debug, Clone, Serialize, Default, PartialEq)]
pub struct AudioState {
    pub outputs: Vec<Node>,
    pub inputs: Vec<Node>,
    /// Apps tocando som agora.
    pub apps: Vec<Node>,
}

/// O PipeWire guarda o volume de cada canal ao cubo; o `wpctl` mostra a raiz cúbica.
fn volume_and_mute(info: &Value) -> (u8, bool) {
    let props = info["params"]["Props"].as_array().into_iter().flatten();
    for p in props {
        if let Some(channels) = p["channelVolumes"].as_array() {
            let max = channels.iter().filter_map(Value::as_f64).fold(0.0_f64, f64::max);
            let volume = (max.cbrt() * 100.0).round().clamp(0.0, 150.0) as u8;
            return (volume, p["mute"].as_bool().unwrap_or(false));
        }
    }
    (0, false)
}

/// Monta o estado a partir da saída do `pw-dump`.
pub fn parse_dump(json: &str) -> AudioState {
    let Ok(Value::Array(objects)) = serde_json::from_str::<Value>(json) else { return AudioState::default() };

    // Saída e entrada padrão: metadado "default", chaves default.audio.sink/source = {"name": ...}.
    let mut default_sink = None;
    let mut default_source = None;
    for object in &objects {
        if object["type"] != "PipeWire:Interface:Metadata" || object["props"]["metadata.name"] != "default" {
            continue;
        }
        for entry in object["metadata"].as_array().into_iter().flatten() {
            let name = entry["value"]["name"].as_str().map(str::to_string);
            match entry["key"].as_str() {
                Some("default.audio.sink") => default_sink = name,
                Some("default.audio.source") => default_source = name,
                _ => {}
            }
        }
    }

    let mut state = AudioState::default();
    for object in &objects {
        if object["type"] != "PipeWire:Interface:Node" {
            continue;
        }
        let Some(id) = object["id"].as_u64() else { continue };
        let info = &object["info"];
        let props = &info["props"];
        let class = props["media.class"].as_str().unwrap_or_default();
        let node_name = props["node.name"].as_str().unwrap_or_default();
        let label = |keys: &[&str]| {
            keys.iter().find_map(|k| props[*k].as_str().filter(|s| !s.is_empty())).unwrap_or(node_name).to_string()
        };
        let (volume, muted) = volume_and_mute(info);
        match class {
            "Audio/Sink" => state.outputs.push(Node {
                id,
                name: label(&["node.description", "node.nick"]),
                volume,
                muted,
                default: default_sink.as_deref() == Some(node_name),
            }),
            "Audio/Source" => state.inputs.push(Node {
                id,
                name: label(&["node.description", "node.nick"]),
                volume,
                muted,
                default: default_source.as_deref() == Some(node_name),
            }),
            "Stream/Output/Audio" => state.apps.push(Node {
                id,
                name: label(&["application.name", "media.name"]),
                volume,
                muted,
                default: false,
            }),
            _ => {}
        }
    }
    for list in [&mut state.outputs, &mut state.inputs, &mut state.apps] {
        list.sort_by(|a, b| (!a.default, a.name.to_lowercase()).cmp(&(!b.default, b.name.to_lowercase())));
    }
    state
}

#[tauri::command]
pub fn audio_state() -> AudioState {
    Command::new("pw-dump")
        .output()
        .ok()
        .filter(|o| o.status.success())
        .map(|o| parse_dump(&String::from_utf8_lossy(&o.stdout)))
        .unwrap_or_default()
}

#[tauri::command]
pub fn audio_set_default(id: u64) -> Result<(), String> {
    crate::status::wpctl(&["set-default", &id.to_string()])
}

#[tauri::command]
pub fn audio_set_volume(id: u64, volume: u8) -> Result<(), String> {
    let value = format!("{:.2}", f64::from(volume.min(150)) / 100.0);
    crate::status::wpctl(&["set-volume", &id.to_string(), &value])
}

#[tauri::command]
pub fn audio_toggle_mute(id: u64) -> Result<(), String> {
    crate::status::wpctl(&["set-mute", &id.to_string(), "toggle"])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_pw_dump() {
        let dump = r#"[
          {"id": 30, "type": "PipeWire:Interface:Metadata", "props": {"metadata.name": "default"},
           "metadata": [
             {"subject": 0, "key": "default.audio.sink", "type": "Spa:String:JSON", "value": {"name": "alsa_output.hdmi"}},
             {"subject": 0, "key": "default.audio.source", "type": "Spa:String:JSON", "value": {"name": "alsa_input.mic"}}]},
          {"id": 41, "type": "PipeWire:Interface:Node", "info": {
             "props": {"media.class": "Audio/Sink", "node.name": "alsa_output.speakers", "node.description": "Alto-falantes"},
             "params": {"Props": [{"volume": 1.0}, {"channelVolumes": [0.125, 0.125], "mute": false}]}}},
          {"id": 42, "type": "PipeWire:Interface:Node", "info": {
             "props": {"media.class": "Audio/Sink", "node.name": "alsa_output.hdmi", "node.description": "HDMI"},
             "params": {"Props": [{"channelVolumes": [1.0, 1.0], "mute": true}]}}},
          {"id": 50, "type": "PipeWire:Interface:Node", "info": {
             "props": {"media.class": "Audio/Source", "node.name": "alsa_input.mic", "node.description": "Microfone"},
             "params": {"Props": [{"channelVolumes": [0.343], "mute": false}]}}},
          {"id": 60, "type": "PipeWire:Interface:Node", "info": {
             "props": {"media.class": "Stream/Output/Audio", "node.name": "Firefox", "application.name": "Firefox"},
             "params": {"Props": [{"channelVolumes": [0.729, 0.729], "mute": false}]}}},
          {"id": 70, "type": "PipeWire:Interface:Node", "info": {"props": {"media.class": "Video/Source", "node.name": "cam"}}}
        ]"#;
        let state = parse_dump(dump);
        assert_eq!(state.outputs.len(), 2);
        assert_eq!(state.outputs[0], Node { id: 42, name: "HDMI".into(), volume: 100, muted: true, default: true });
        assert_eq!(state.outputs[1].volume, 50);
        assert_eq!(state.inputs[0], Node { id: 50, name: "Microfone".into(), volume: 70, muted: false, default: true });
        assert_eq!(state.apps[0].name, "Firefox");
        assert_eq!(state.apps[0].volume, 90);
        assert_eq!(parse_dump("lixo"), AudioState::default());
    }
}
