//! "Sobre este computador" nas Configurações: sistema, processador, vídeo, memória e disco.

use std::process::Command;

use serde::Serialize;

#[derive(Debug, Clone, Serialize, Default)]
pub struct SystemInfo {
    pub hostname: String,
    pub os: String,
    pub kernel: String,
    pub cpu: String,
    pub cores: usize,
    pub gpus: Vec<String>,
    /// Bytes.
    pub memory: u64,
    pub disk_total: u64,
    pub disk_used: u64,
    /// Segundos desde o boot.
    pub uptime: u64,
}

fn read(path: &str) -> String {
    std::fs::read_to_string(path).unwrap_or_default()
}

/// `PRETTY_NAME="Arch Linux"` → `Arch Linux`.
pub fn os_name(os_release: &str) -> String {
    os_release
        .lines()
        .find_map(|l| l.strip_prefix("PRETTY_NAME="))
        .map(|v| v.trim_matches('"').to_string())
        .unwrap_or_else(|| "Linux".into())
}

pub fn cpu_model(cpuinfo: &str) -> (String, usize) {
    let model = cpuinfo
        .lines()
        .find_map(|l| l.strip_prefix("model name").and_then(|r| r.split_once(':')).map(|(_, v)| v.trim().to_string()))
        .unwrap_or_default();
    let cores = cpuinfo.lines().filter(|l| l.starts_with("processor")).count();
    (model, cores)
}

/// `MemTotal:       16303780 kB` → bytes.
pub fn mem_total(meminfo: &str) -> u64 {
    meminfo
        .lines()
        .find_map(|l| l.strip_prefix("MemTotal:"))
        .and_then(|v| v.split_whitespace().next()?.parse::<u64>().ok())
        .map_or(0, |kb| kb * 1024)
}

/// Linhas de `lspci -mm` das placas de vídeo: `00:02.0 "VGA compatible controller" "Intel Corporation" "UHD Graphics 620" ...`.
pub fn gpus(lspci: &str) -> Vec<String> {
    lspci
        .lines()
        .filter(|l| l.contains("\"VGA") || l.contains("\"3D") || l.contains("\"Display"))
        .filter_map(|l| {
            let fields: Vec<&str> = l.split('"').collect();
            // fields: [slot, classe, " ", fabricante, " ", modelo, ...]
            let vendor = match *fields.get(3)? {
                v if v.contains("AMD") || v.contains("Advanced Micro") => "AMD".to_string(),
                v if v.contains("Intel") => "Intel".to_string(),
                v => v.replace(" Corporation", "").replace(", Inc.", ""),
            };
            let model = fields.get(5)?.to_string();
            // "Navi 23 [Radeon RX 6600/6600 XT/6600M]" → o nome comercial entre colchetes.
            let model = match (model.find('['), model.rfind(']')) {
                (Some(a), Some(b)) if b > a => model[a + 1..b].to_string(),
                _ => model,
            };
            Some(format!("{vendor} {model}").trim().to_string())
        })
        .collect()
}

/// `df -B1 --output=size,used /` → (total, usado).
pub fn disk(df: &str) -> (u64, u64) {
    df.lines()
        .nth(1)
        .and_then(|l| {
            let mut it = l.split_whitespace().map(|v| v.parse::<u64>().ok());
            Some((it.next()??, it.next()??))
        })
        .unwrap_or((0, 0))
}

#[tauri::command]
pub fn system_info() -> SystemInfo {
    let output = |program: &str, args: &[&str]| {
        Command::new(program)
            .args(args)
            .env("LC_ALL", "C")
            .output()
            .map(|o| String::from_utf8_lossy(&o.stdout).into_owned())
            .unwrap_or_default()
    };
    let (cpu, cores) = cpu_model(&read("/proc/cpuinfo"));
    let (disk_total, disk_used) = disk(&output("df", &["-B1", "--output=size,used", "/"]));
    SystemInfo {
        hostname: read("/etc/hostname").trim().to_string(),
        os: os_name(&read("/etc/os-release")),
        kernel: read("/proc/sys/kernel/osrelease").trim().to_string(),
        cpu,
        cores,
        gpus: gpus(&output("lspci", &["-mm"])),
        memory: mem_total(&read("/proc/meminfo")),
        disk_total,
        disk_used,
        uptime: read("/proc/uptime").split('.').next().and_then(|s| s.parse().ok()).unwrap_or(0),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_hardware() {
        assert_eq!(os_name("NAME=\"Arch\"\nPRETTY_NAME=\"LUMNA OS\"\n"), "LUMNA OS");
        assert_eq!(cpu_model("processor\t: 0\nmodel name\t: AMD Ryzen 5 5600\nprocessor\t: 1\n"), ("AMD Ryzen 5 5600".into(), 2));
        assert_eq!(mem_total("MemTotal:       16303780 kB\n"), 16303780 * 1024);
        let lspci = "03:00.0 \"VGA compatible controller\" \"Advanced Micro Devices, Inc. [AMD/ATI]\" \"Navi 23 [Radeon RX 6600/6600 XT/6600M]\" -rc1 \"x\" \"y\"\n\
00:02.0 \"VGA compatible controller\" \"Intel Corporation\" \"UHD Graphics 620\" -r07 \"x\" \"y\"\n\
00:1f.3 \"Audio device\" \"Intel Corporation\" \"Sunrise Point\"\n";
        assert_eq!(gpus(lspci), vec!["AMD Radeon RX 6600/6600 XT/6600M", "Intel UHD Graphics 620"]);
        assert_eq!(disk("     Size     Used\n512000 128000\n"), (512000, 128000));
    }
}
