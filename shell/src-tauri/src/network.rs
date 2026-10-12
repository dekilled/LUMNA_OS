//! Detalhes das conexões (cabo e Wi-Fi) para as Configurações: endereço, roteador, DNS, velocidade.
//! Quem configura a rede é o systemd-networkd (DHCP); aqui só se lê o estado, pelo sysfs e pelo `ip`.

use std::path::Path;
use std::process::Command;

use serde::Serialize;
use serde_json::Value;

#[derive(Debug, Clone, Serialize, PartialEq, Default)]
pub struct Interface {
    pub name: String,
    /// `wifi` ou `ethernet`.
    pub kind: &'static str,
    /// Cabo plugado / associado à rede.
    pub connected: bool,
    /// Mb/s, quando a placa informa (cabo).
    pub speed: Option<u32>,
    pub mac: String,
    pub ipv4: Vec<String>,
    pub ipv6: Vec<String>,
    pub gateway: Option<String>,
    pub dns: Vec<String>,
}

fn read(path: &Path) -> Option<String> {
    std::fs::read_to_string(path).ok().map(|s| s.trim().to_string())
}

fn run(program: &str, args: &[&str]) -> Option<String> {
    let output = Command::new(program).args(args).env("LC_ALL", "C").output().ok()?;
    output.status.success().then(|| String::from_utf8_lossy(&output.stdout).into_owned())
}

/// Endereços de `ip -j addr show dev X`, sem os de enlace local (fe80::), que não dizem nada.
pub fn parse_addresses(json: &str) -> (Vec<String>, Vec<String>) {
    let mut v4 = Vec::new();
    let mut v6 = Vec::new();
    let Ok(Value::Array(links)) = serde_json::from_str::<Value>(json) else { return (v4, v6) };
    for info in links.iter().filter_map(|l| l["addr_info"].as_array()).flatten() {
        let (Some(family), Some(local)) = (info["family"].as_str(), info["local"].as_str()) else { continue };
        if info["scope"].as_str() == Some("link") {
            continue;
        }
        let address = match info["prefixlen"].as_u64() {
            Some(prefix) => format!("{local}/{prefix}"),
            None => local.to_string(),
        };
        if family == "inet" {
            v4.push(address);
        } else if family == "inet6" {
            v6.push(address);
        }
    }
    (v4, v6)
}

/// Roteador de `ip -j route show default dev X`.
pub fn parse_gateway(json: &str) -> Option<String> {
    let routes: Value = serde_json::from_str(json).ok()?;
    routes.as_array()?.iter().find_map(|r| r["gateway"].as_str().map(str::to_string))
}

/// `Link 2 (enp1s0): 192.168.0.1 1.1.1.1` → os servidores.
pub fn parse_dns(output: &str) -> Vec<String> {
    output
        .lines()
        .filter_map(|line| line.split_once("): ").map(|(_, servers)| servers))
        .flat_map(|servers| servers.split_whitespace().map(str::to_string))
        .collect()
}

pub fn interfaces() -> Vec<Interface> {
    let Ok(entries) = std::fs::read_dir("/sys/class/net") else { return Vec::new() };
    let mut list: Vec<Interface> = entries
        .flatten()
        .filter(|e| e.path().join("device").exists())
        .map(|entry| {
            let path = entry.path();
            let name = entry.file_name().to_string_lossy().into_owned();
            let wireless = path.join("wireless").exists() || path.join("phy80211").exists();
            let (ipv4, ipv6) = run("ip", &["-j", "addr", "show", "dev", &name]).map(|j| parse_addresses(&j)).unwrap_or_default();
            Interface {
                kind: if wireless { "wifi" } else { "ethernet" },
                // O `carrier` dá erro de leitura com a placa desligada: conta como desconectada.
                connected: read(&path.join("carrier")).as_deref() == Some("1"),
                speed: read(&path.join("speed")).and_then(|s| s.parse::<i64>().ok()).filter(|s| *s > 0).map(|s| s as u32),
                mac: read(&path.join("address")).unwrap_or_default(),
                gateway: run("ip", &["-j", "route", "show", "default", "dev", &name]).and_then(|j| parse_gateway(&j)),
                dns: run("resolvectl", &["dns", &name]).map(|o| parse_dns(&o)).unwrap_or_default(),
                ipv4,
                ipv6,
                name,
            }
        })
        .collect();
    // Cabo antes do Wi-Fi, como a rota preferida.
    list.sort_by(|a, b| (a.kind != "ethernet", &a.name).cmp(&(b.kind != "ethernet", &b.name)));
    list
}

#[tauri::command]
pub fn network_interfaces() -> Vec<Interface> {
    interfaces()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_ip_output() {
        let addr = r#"[{"ifname":"enp1s0","addr_info":[
            {"family":"inet","local":"192.168.0.20","prefixlen":24,"scope":"global"},
            {"family":"inet6","local":"2804:14c::5","prefixlen":64,"scope":"global"},
            {"family":"inet6","local":"fe80::1","prefixlen":64,"scope":"link"}]}]"#;
        assert_eq!(parse_addresses(addr), (vec!["192.168.0.20/24".into()], vec!["2804:14c::5/64".into()]));
        assert_eq!(parse_gateway(r#"[{"dst":"default","gateway":"192.168.0.1","dev":"enp1s0"}]"#), Some("192.168.0.1".into()));
        assert_eq!(parse_gateway("[]"), None);
        assert_eq!(parse_dns("Link 2 (enp1s0): 192.168.0.1 1.1.1.1\n"), vec!["192.168.0.1", "1.1.1.1"]);
        assert!(parse_dns("Link 2 (enp1s0):\n").is_empty());
    }
}
