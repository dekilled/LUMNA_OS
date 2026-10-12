//! Impressoras pelo CUPS, com os comandos dele (`lpstat`, `lpinfo`, `lpadmin`, `lp`).
//!
//! Só impressoras "sem driver" (IPP Everywhere / AirPrint, quase todas as de rede e as USB modernas
//! pelo ipp-usb): o CUPS conversa com elas direto, sem PPD. Quem está no grupo `wheel` administra o
//! CUPS sem senha pelo socket local (SystemGroup do Arch).

use std::process::Command;

use serde::Serialize;

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Printer {
    pub name: String,
    /// `idle`, `printing` ou `disabled`.
    pub state: &'static str,
    pub default: bool,
    pub uri: String,
    /// Trabalhos na fila.
    pub jobs: usize,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
pub struct Found {
    pub uri: String,
    /// Nome que a impressora anuncia.
    pub info: String,
    /// `network` ou `direct` (USB).
    pub class: String,
}

fn run(program: &str, args: &[&str]) -> Result<String, String> {
    let output = Command::new(program)
        .args(args)
        .env("LC_ALL", "C")
        .stdin(std::process::Stdio::null())
        .output()
        .map_err(|e| format!("{program}: {e} (o CUPS está instalado?)"))?;
    if output.status.success() {
        Ok(String::from_utf8_lossy(&output.stdout).into_owned())
    } else {
        let err = String::from_utf8_lossy(&output.stderr).trim().to_string();
        Err(if err.is_empty() { format!("{program} falhou") } else { err })
    }
}

/// `lpstat -p`: `printer NOME is idle.  enabled since ...` / `printer NOME disabled since ...`.
pub fn parse_printers(output: &str) -> Vec<(String, &'static str)> {
    output
        .lines()
        .filter_map(|line| line.strip_prefix("printer "))
        .filter_map(|rest| {
            let (name, status) = rest.split_once(' ')?;
            let state = if status.starts_with("disabled") {
                "disabled"
            } else if status.starts_with("now printing") {
                "printing"
            } else {
                "idle"
            };
            Some((name.to_string(), state))
        })
        .collect()
}

/// `lpstat -d`: `system default destination: NOME`.
pub fn parse_default(output: &str) -> Option<String> {
    output.lines().find_map(|l| l.strip_prefix("system default destination: ")).map(|s| s.trim().to_string())
}

/// `lpstat -v`: `device for NOME: URI`.
pub fn parse_uris(output: &str) -> Vec<(String, String)> {
    output
        .lines()
        .filter_map(|l| l.strip_prefix("device for "))
        .filter_map(|rest| rest.split_once(": ").map(|(n, u)| (n.to_string(), u.trim().to_string())))
        .collect()
}

/// `lpstat -o`: `NOME-12  usuario  1024  data` → quantos trabalhos de NOME.
pub fn count_jobs(output: &str, printer: &str) -> usize {
    output
        .lines()
        .filter_map(|l| l.split_whitespace().next())
        .filter(|job| job.rsplit_once('-').is_some_and(|(name, id)| name == printer && id.parse::<u32>().is_ok()))
        .count()
}

/// `lpinfo -l -v`: blocos `Device: uri = ...` seguidos de `class = ...`, `info = ...`.
pub fn parse_found(output: &str) -> Vec<Found> {
    let mut found: Vec<Found> = Vec::new();
    for line in output.lines() {
        let line = line.trim();
        if let Some(uri) = line.strip_prefix("Device: uri = ") {
            found.push(Found { uri: uri.to_string(), info: String::new(), class: String::new() });
        } else if let Some(last) = found.last_mut() {
            if let Some(class) = line.strip_prefix("class = ") {
                last.class = class.to_string();
            } else if let Some(info) = line.strip_prefix("info = ") {
                last.info = info.to_string();
            }
        }
    }
    // Sem endereço de verdade ("ipp", "usb" sozinhos são só os tipos que o CUPS sabe procurar).
    found.retain(|f| f.uri.contains(":/"));
    for f in &mut found {
        if f.info.is_empty() || f.info == "Unknown" {
            f.info = f.uri.clone();
        }
    }
    found
}

/// Nome de fila aceito pelo CUPS a partir do nome anunciado: "HP LaserJet M110w" → "HP_LaserJet_M110w".
pub fn queue_name(info: &str) -> String {
    let mut name: String = info
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '-' { c } else { '_' })
        .collect::<String>()
        .split('_')
        .filter(|p| !p.is_empty())
        .collect::<Vec<_>>()
        .join("_");
    name.truncate(60);
    if name.is_empty() {
        name = "Impressora".into();
    }
    name
}

#[tauri::command]
pub fn printers() -> Vec<Printer> {
    let Ok(list) = run("lpstat", &["-p"]) else { return Vec::new() };
    let default = run("lpstat", &["-d"]).ok().and_then(|o| parse_default(&o));
    let uris = run("lpstat", &["-v"]).map(|o| parse_uris(&o)).unwrap_or_default();
    let jobs = run("lpstat", &["-o"]).unwrap_or_default();
    parse_printers(&list)
        .into_iter()
        .map(|(name, state)| Printer {
            default: default.as_deref() == Some(name.as_str()),
            uri: uris.iter().find(|(n, _)| *n == name).map(|(_, u)| u.clone()).unwrap_or_default(),
            jobs: count_jobs(&jobs, &name),
            name,
            state,
        })
        .collect()
}

/// Procura impressoras na rede e no USB (demora alguns segundos).
#[tauri::command]
pub async fn printers_discover() -> Result<Vec<Found>, String> {
    tauri::async_runtime::spawn_blocking(|| {
        let output = run("lpinfo", &["-l", "-v", "--timeout", "8", "--include-schemes", "dnssd,ipp,ipps,usb"])?;
        let added: Vec<String> =
            run("lpstat", &["-v"]).map(|o| parse_uris(&o).into_iter().map(|(_, u)| u).collect()).unwrap_or_default();
        Ok(parse_found(&output).into_iter().filter(|f| !added.contains(&f.uri)).collect())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn printer_add(uri: String, info: String) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let name = queue_name(&info);
        run("lpadmin", &["-p", &name, "-E", "-v", &uri, "-m", "everywhere", "-D", &info])
            .map_err(|e| format!("Não deu para adicionar: {e}"))?;
        Ok(name)
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn printer_remove(name: String) -> Result<(), String> {
    run("lpadmin", &["-x", &name]).map(|_| ())
}

/// Padrão só para este usuário (não precisa de administrador).
#[tauri::command]
pub fn printer_set_default(name: String) -> Result<(), String> {
    run("lpoptions", &["-d", &name]).map(|_| ())
}

#[tauri::command]
pub fn printer_test_page(name: String) -> Result<(), String> {
    run("lp", &["-d", &name, "-t", "Página de teste do LUMNA", "/usr/share/cups/data/testprint"]).map(|_| ())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_lpstat() {
        let p = "printer HP_Laser is idle.  enabled since Mon 12 Oct\nprinter Epson now printing Epson-3.  enabled since x\n\tWaiting\nprinter Velha disabled since y -\n";
        assert_eq!(parse_printers(p), vec![("HP_Laser".into(), "idle"), ("Epson".into(), "printing"), ("Velha".into(), "disabled")]);
        assert_eq!(parse_default("system default destination: Epson\n"), Some("Epson".into()));
        assert_eq!(parse_default("no system default destination\n"), None);
        assert_eq!(parse_uris("device for Epson: ipp://epson.local/ipp/print\n"), vec![("Epson".into(), "ipp://epson.local/ipp/print".into())]);
        assert_eq!(count_jobs("Epson-3 luan 1024 x\nEpson-4 luan 1 x\nHP_Laser-9 luan 1 x\n", "Epson"), 2);
    }

    #[test]
    fn reads_lpinfo() {
        let out = "Device: uri = ipp\n        class = network\n        info = Internet Printing Protocol (ipp)\n\
Device: uri = dnssd://HP%20LaserJet%20M110w._ipp._tcp.local/?uuid=1\n        class = network\n        info = HP LaserJet M110w\n        make-and-model = HP LaserJet\n\
Device: uri = usb://Brother/HL-1200?serial=1\n        class = direct\n        info = Unknown\n";
        let found = parse_found(out);
        assert_eq!(found.len(), 2);
        assert_eq!(found[0].info, "HP LaserJet M110w");
        assert_eq!(found[1].class, "direct");
        assert_eq!(found[1].info, "usb://Brother/HL-1200?serial=1");
        assert_eq!(queue_name("HP LaserJet M110w (sala)"), "HP_LaserJet_M110w_sala");
        assert_eq!(queue_name("???"), "Impressora");
    }
}
