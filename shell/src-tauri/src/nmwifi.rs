//! Wi-Fi pelo NetworkManager (o celular usa ele; o PC usa o iwd, em `wifi.rs`).
//!
//! Os comandos do Wi-Fi (`wifi_state`, `wifi_connect`...) caem aqui quando o iwd não está no
//! barramento e o NetworkManager está. As redes são identificadas por `nm:<SSID>` no lugar do
//! caminho de objeto do iwd. Tudo pelo `nmcli`, que o polkit libera pro grupo wheel.

use std::process::Command;

use serde::Serialize;

use crate::wifi::{Network, WifiState};

pub const PREFIX: &str = "nm:";

fn nmcli(args: &[&str]) -> Result<String, String> {
    let out = Command::new("nmcli").args(args).output().map_err(|e| format!("nmcli: {e}"))?;
    if out.status.success() {
        Ok(String::from_utf8_lossy(&out.stdout).into_owned())
    } else {
        Err(explain(String::from_utf8_lossy(&out.stderr).trim()))
    }
}

fn explain(err: &str) -> String {
    let e = err.trim_start_matches("Error: ");
    if e.contains("Secrets were required") || e.contains("802-11-wireless-security.psk") {
        "Senha errada (ou faltando).".into()
    } else if e.contains("No network with SSID") {
        "Essa rede não está ao alcance.".into()
    } else {
        e.to_string()
    }
}

/// Divide uma linha do `nmcli -t` (":" separa, "\:" é dois-pontos de verdade).
fn fields(line: &str) -> Vec<String> {
    let mut out = vec![String::new()];
    let mut chars = line.chars();
    while let Some(c) = chars.next() {
        match c {
            '\\' => {
                if let Some(n) = chars.next() {
                    out.last_mut().unwrap().push(n);
                }
            }
            ':' => out.push(String::new()),
            c => out.last_mut().unwrap().push(c),
        }
    }
    out
}

/// A placa Wi-Fi e o estado dela (`connected`, `disconnected`, `unavailable`...).
pub fn device() -> Option<(String, String)> {
    nmcli(&["-t", "-f", "DEVICE,TYPE,STATE", "device"]).ok()?.lines().find_map(|l| {
        let f = fields(l);
        (f.get(1).map(String::as_str) == Some("wifi")).then(|| (f[0].clone(), f.get(2).cloned().unwrap_or_default()))
    })
}

/// Nomes das redes salvas (as conexões Wi-Fi do NetworkManager).
fn known() -> Vec<String> {
    nmcli(&["-t", "-f", "NAME,TYPE", "connection", "show"])
        .unwrap_or_default()
        .lines()
        .map(fields)
        .filter(|f| f.get(1).map(String::as_str) == Some("802-11-wireless"))
        .map(|f| f[0].clone())
        .collect()
}

/// 0-100% do NetworkManager em 0 a 4 tracinhos.
fn bars(signal: u8) -> u8 {
    match signal {
        0..=20 => 1,
        21..=45 => 2,
        46..=70 => 3,
        _ => 4,
    }
}

fn security(text: &str) -> String {
    if text.is_empty() || text == "--" {
        "open".into()
    } else if text.contains("802.1X") {
        "8021x".into()
    } else {
        "psk".into()
    }
}

pub fn state() -> WifiState {
    let Some((dev, dev_state)) = device() else { return WifiState::default() };
    let powered = nmcli(&["-t", "-f", "WIFI", "radio"]).map(|r| r.trim() == "enabled").unwrap_or(false);
    let known = known();
    let mut networks: Vec<Network> = Vec::new();
    if powered {
        let list = nmcli(&["-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "ifname", &dev, "--rescan", "no"])
            .unwrap_or_default();
        for f in list.lines().map(fields) {
            let (Some(in_use), Some(ssid), Some(signal), Some(sec)) = (f.first(), f.get(1), f.get(2), f.get(3)) else { continue };
            if ssid.is_empty() {
                continue;
            }
            let connected = in_use == "*";
            let signal = bars(signal.parse().unwrap_or(0));
            // a mesma rede aparece uma vez por antena (BSSID): fica a mais forte
            if let Some(existing) = networks.iter_mut().find(|n| &n.name == ssid) {
                existing.signal = existing.signal.max(signal);
                existing.connected |= connected;
                continue;
            }
            networks.push(Network {
                path: format!("{PREFIX}{ssid}"),
                name: ssid.clone(),
                security: security(sec),
                signal,
                known: known.contains(ssid),
                connected,
            });
        }
    }
    networks.sort_by_key(|n| std::cmp::Reverse((n.connected, n.known, n.signal)));
    let connected = networks.iter().find(|n| n.connected).map(|n| n.name.clone());
    WifiState {
        available: true,
        powered,
        scanning: false,
        state: match dev_state.as_str() {
            "connected" => "connected".into(),
            s if s.starts_with("connecting") => "connecting".into(),
            _ => "disconnected".into(),
        },
        connected,
        networks,
    }
}

pub fn scan() -> Result<(), String> {
    let (dev, _) = device().ok_or("Nenhuma placa Wi-Fi encontrada.")?;
    // "Scanning not allowed immediately following previous scan": já está procurando
    let _ = nmcli(&["device", "wifi", "rescan", "ifname", &dev]);
    Ok(())
}

pub fn connect(path: &str, passphrase: Option<String>) -> Result<(), String> {
    let ssid = path.strip_prefix(PREFIX).unwrap_or(path);
    if known().iter().any(|k| k == ssid) && passphrase.is_none() {
        return nmcli(&["connection", "up", "id", ssid]).map(|_| ());
    }
    let mut args = vec!["device", "wifi", "connect", ssid];
    if let Some(p) = passphrase.as_deref() {
        args.extend(["password", p]);
    }
    nmcli(&args).map(|_| ())
}

pub fn disconnect() -> Result<(), String> {
    let (dev, _) = device().ok_or("Nenhuma placa Wi-Fi encontrada.")?;
    nmcli(&["device", "disconnect", &dev]).map(|_| ())
}

pub fn forget(path: &str) -> Result<(), String> {
    let ssid = path.strip_prefix(PREFIX).unwrap_or(path);
    if !known().iter().any(|k| k == ssid) {
        return Err("Essa rede não está salva.".into());
    }
    nmcli(&["connection", "delete", "id", ssid]).map(|_| ())
}

pub fn power(on: bool) -> Result<(), String> {
    nmcli(&["radio", "wifi", if on { "on" } else { "off" }]).map(|_| ())
}

/// Rede oculta (ou digitada à mão): nome, senha (vazia = aberta).
pub fn add(ssid: &str, password: &str, hidden: bool) -> Result<(), String> {
    let mut args = vec!["device", "wifi", "connect", ssid];
    if !password.is_empty() {
        args.extend(["password", password]);
    }
    if hidden {
        args.extend(["hidden", "yes"]);
    }
    nmcli(&args).map(|_| ())
}

/// Propriedades da rede conectada (página de detalhes do Wi-Fi).
#[derive(Debug, Clone, Serialize, Default)]
pub struct Details {
    pub ssid: String,
    pub signal: u8,
    pub frequency: String,
    pub security: String,
    pub rate: String,
    pub ipv4: String,
    pub gateway: String,
    pub dns: Vec<String>,
    pub mac: String,
}

pub fn details() -> Option<Details> {
    let (dev, _) = device()?;
    let mut d = Details::default();
    let ap = nmcli(&["-t", "-f", "IN-USE,SSID,SIGNAL,FREQ,RATE,SECURITY", "device", "wifi", "list", "ifname", &dev, "--rescan", "no"])
        .ok()?
        .lines()
        .map(fields)
        .find(|f| f.first().map(String::as_str) == Some("*"))?;
    d.ssid = ap.get(1).cloned().unwrap_or_default();
    d.signal = ap.get(2).and_then(|s| s.parse().ok()).unwrap_or(0);
    d.frequency = ap.get(3).cloned().unwrap_or_default();
    d.rate = ap.get(4).cloned().unwrap_or_default();
    d.security = ap.get(5).cloned().filter(|s| !s.is_empty() && s != "--").unwrap_or_else(|| "Aberta".into());
    for f in nmcli(&["-t", "-f", "IP4.ADDRESS,IP4.GATEWAY,IP4.DNS,GENERAL.HWADDR", "device", "show", &dev])
        .unwrap_or_default()
        .lines()
    {
        let Some((key, value)) = f.split_once(':') else { continue };
        let value = value.replace("\\:", ":");
        match key {
            k if k.starts_with("IP4.ADDRESS") && d.ipv4.is_empty() => d.ipv4 = value,
            "IP4.GATEWAY" => d.gateway = value,
            k if k.starts_with("IP4.DNS") => d.dns.push(value),
            "GENERAL.HWADDR" => d.mac = value,
            _ => {}
        }
    }
    Some(d)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn campos_do_nmcli() {
        assert_eq!(fields("*:Casa\\: 5G:80:WPA2"), vec!["*", "Casa: 5G", "80", "WPA2"]);
        assert_eq!(fields(":Vizinho::--"), vec!["", "Vizinho", "", "--"]);
    }

    #[test]
    fn tipos_de_seguranca() {
        assert_eq!(security("--"), "open");
        assert_eq!(security("WPA2 802.1X"), "8021x");
        assert_eq!(security("WPA1 WPA2"), "psk");
    }
}
