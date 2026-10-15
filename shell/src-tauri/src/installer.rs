//! "Instalar LUMNA OS" (só no pendrive): lista os discos, os fusos e roda o
//! /usr/lib/lumna/lumna-install pelo pkexec, mandando o andamento para a tela.

use std::io::{BufRead, BufReader, Write};
use std::process::{Command, Stdio};

use serde::{Deserialize, Serialize};
use serde_json::Value;
use tauri::{AppHandle, Emitter};

const INSTALLER: &str = "/usr/lib/lumna/lumna-install";

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Disk {
    pub path: String,
    /// "Samsung SSD 980 1TB".
    pub model: String,
    pub size: u64,
    /// `nvme`, `sata`, `usb`...
    pub transport: String,
    /// O que tem nele hoje ("Windows (NTFS) · LUMNA (btrfs)"), para a pessoa reconhecer.
    pub contents: String,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Plan {
    pub disk: String,
    pub user: String,
    pub name: String,
    pub hostname: String,
    pub timezone: String,
    pub keyboard: String,
    pub locale: String,
}

#[derive(Debug, Clone, Serialize)]
struct Progress<'a> {
    percent: u32,
    text: &'a str,
}

/// `lsblk -J -b -o PATH,MODEL,SIZE,TRAN,TYPE,RM,LABEL,FSTYPE,MOUNTPOINTS`: discos inteiros, sem o
/// pendrive do próprio live, sem CD e sem disco de memória.
pub fn parse_disks(json: &str) -> Vec<Disk> {
    let Ok(value) = serde_json::from_str::<Value>(json) else { return Vec::new() };
    let mut disks = Vec::new();
    for d in value["blockdevices"].as_array().into_iter().flatten() {
        if d["type"] != "disk" {
            continue;
        }
        let path = d["path"].as_str().unwrap_or_default();
        if path.starts_with("/dev/zram") || path.starts_with("/dev/loop") || path.starts_with("/dev/sr") {
            continue;
        }
        // O pendrive em que o LUMNA está rodando tem algo montado em /run/archiso.
        let mut parts: Vec<&Value> = vec![d];
        parts.extend(d["children"].as_array().into_iter().flatten());
        let live = parts.iter().any(|p| {
            p["mountpoints"].as_array().into_iter().flatten().filter_map(Value::as_str).any(|m| m.starts_with("/run/archiso"))
        });
        if live {
            continue;
        }
        let contents: Vec<String> = parts
            .iter()
            .filter_map(|p| {
                let fs = p["fstype"].as_str()?;
                Some(match p["label"].as_str() {
                    Some(label) if !label.is_empty() => format!("{label} ({fs})"),
                    _ => fs.to_string(),
                })
            })
            .collect();
        disks.push(Disk {
            path: path.to_string(),
            model: d["model"].as_str().map(str::trim).filter(|m| !m.is_empty()).unwrap_or("Disco").to_string(),
            size: d["size"].as_u64().unwrap_or(0),
            transport: d["tran"].as_str().unwrap_or_default().to_string(),
            contents: if contents.is_empty() { "Vazio".into() } else { contents.join(" · ") },
        });
    }
    disks
}

/// "@@ 42 Copiando o sistema" → (42, "Copiando o sistema").
pub fn parse_progress(line: &str) -> Option<(u32, &str)> {
    let rest = line.strip_prefix("@@ ")?;
    let (pct, text) = rest.split_once(' ')?;
    Some((pct.parse().ok()?, text))
}

#[tauri::command]
pub fn installer_disks() -> Vec<Disk> {
    Command::new("lsblk")
        .args(["-J", "-b", "-o", "PATH,MODEL,SIZE,TRAN,TYPE,RM,LABEL,FSTYPE,MOUNTPOINTS"])
        .output()
        .ok()
        .map(|o| parse_disks(&String::from_utf8_lossy(&o.stdout)))
        .unwrap_or_default()
}

/// Fusos do sistema (`Area/Cidade`), da tabela do tzdata; sem ela, do timedatectl.
#[tauri::command]
pub fn installer_timezones() -> Vec<String> {
    let mut zones: Vec<String> = std::fs::read_to_string("/usr/share/zoneinfo/zone1970.tab")
        .map(|t| parse_zone_tab(&t))
        .unwrap_or_default();
    if zones.is_empty() {
        if let Ok(out) = Command::new("timedatectl").arg("list-timezones").output() {
            zones = String::from_utf8_lossy(&out.stdout).lines().filter(|l| l.contains('/')).map(str::to_string).collect();
        }
    }
    if zones.is_empty() {
        zones.push("America/Sao_Paulo".into());
    }
    zones.sort();
    zones.dedup();
    zones
}

/// `zone1970.tab`: `BR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil (southeast...)`.
pub fn parse_zone_tab(text: &str) -> Vec<String> {
    text.lines()
        .filter(|l| !l.starts_with('#'))
        .filter_map(|l| l.split('\t').nth(2))
        .filter(|z| z.contains('/'))
        .map(str::to_string)
        .collect()
}

/// Roda a instalação. A senha vai pela entrada padrão (nunca na linha de comando).
#[tauri::command]
pub async fn installer_run(app: AppHandle, plan: Plan, password: String) -> Result<(), String> {
    tauri::async_runtime::spawn_blocking(move || {
        let mut child = Command::new("pkexec")
            .arg(INSTALLER)
            .args(["--disk", &plan.disk, "--user", &plan.user, "--name", &plan.name, "--hostname", &plan.hostname])
            .args(["--timezone", &plan.timezone, "--keyboard", &plan.keyboard, "--locale", &plan.locale])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|e| format!("pkexec: {e}"))?;
        if let Some(mut stdin) = child.stdin.take() {
            let _ = writeln!(stdin, "{password}");
        }
        let errors = child.stderr.take().map(|err| {
            std::thread::spawn(move || BufReader::new(err).lines().map_while(Result::ok).collect::<Vec<_>>())
        });
        if let Some(out) = child.stdout.take() {
            for line in BufReader::new(out).lines().map_while(Result::ok) {
                if let Some((percent, text)) = parse_progress(&line) {
                    let _ = app.emit("lumna://install-progress", Progress { percent, text });
                }
            }
        }
        let status = child.wait().map_err(|e| e.to_string())?;
        if status.success() {
            return Ok(());
        }
        let errors = errors.and_then(|h| h.join().ok()).unwrap_or_default();
        if matches!(status.code(), Some(126) | Some(127)) && errors.is_empty() {
            return Err("A instalação precisa da permissão de administrador.".into());
        }
        let message = errors
            .iter()
            .rev()
            .find_map(|l| l.strip_prefix("ERRO: "))
            .map(str::to_string)
            .unwrap_or_else(|| errors.iter().rev().take(4).rev().cloned().collect::<Vec<_>>().join("\n"));
        Err(if message.is_empty() { "A instalação falhou.".into() } else { message })
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn installer_reboot() -> Result<(), String> {
    Command::new("systemctl").arg("reboot").status().map(|_| ()).map_err(|e| e.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_disks_and_progress() {
        let lsblk = r#"{"blockdevices":[
          {"path":"/dev/nvme0n1","model":"Samsung SSD 980 1TB ","size":1000204886016,"tran":"nvme","type":"disk","rm":false,"label":null,"fstype":null,"mountpoints":[null],"children":[
             {"path":"/dev/nvme0n1p1","model":null,"size":104857600,"tran":null,"type":"part","rm":false,"label":"SYSTEM","fstype":"vfat","mountpoints":[null]},
             {"path":"/dev/nvme0n1p2","model":null,"size":999000000000,"tran":null,"type":"part","rm":false,"label":"Windows","fstype":"ntfs","mountpoints":[null]}]},
          {"path":"/dev/sda","model":"Cruzer","size":32000000000,"tran":"usb","type":"disk","rm":true,"label":"LUMNA_202610","fstype":"iso9660","mountpoints":["/run/archiso/bootmnt"]},
          {"path":"/dev/sdb","model":null,"size":500107862016,"tran":"sata","type":"disk","rm":false,"label":null,"fstype":null,"mountpoints":[null]},
          {"path":"/dev/zram0","model":null,"size":4000000000,"tran":null,"type":"disk","rm":false,"label":null,"fstype":"swap","mountpoints":["[SWAP]"]}]}"#;
        let disks = parse_disks(lsblk);
        assert_eq!(disks.len(), 2);
        assert_eq!(disks[0].model, "Samsung SSD 980 1TB");
        assert_eq!(disks[0].contents, "SYSTEM (vfat) · Windows (ntfs)");
        assert_eq!((disks[1].path.as_str(), disks[1].model.as_str(), disks[1].contents.as_str()), ("/dev/sdb", "Disco", "Vazio"));
        assert_eq!(parse_progress("@@ 42 Copiando o sistema (60%)"), Some((42, "Copiando o sistema (60%)")));
        assert_eq!(parse_progress("outra coisa"), None);
        let tab = "# comentário\nBR\t-2332-04637\tAmerica/Sao_Paulo\tBrazil (southeast)\nPT\t+3843-00908\tEurope/Lisbon\tPortugal\n";
        assert_eq!(parse_zone_tab(tab), vec!["America/Sao_Paulo", "Europe/Lisbon"]);
    }
}
