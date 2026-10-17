//! Vibração curta pro teclado do celular (o "tato" de cada tecla).
//!
//! O motor de vibração aparece como um dispositivo de entrada com force feedback (`/dev/input/eventN`
//! com `capabilities/ff`). Grava um efeito com a duração pedida (EVIOCSFF) e toca escrevendo um
//! evento EV_FF. Tudo numa thread só, pra o toque nunca esperar o kernel. Sem permissão no
//! dispositivo (regra udev `70-lumna-vibra.rules`), avisa uma vez no journal e fica quieto.
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::os::fd::AsRawFd;
use std::sync::mpsc::{channel, Sender};
use std::sync::{Mutex, OnceLock};

const EV_FF: u16 = 0x15;
const FF_RUMBLE: u16 = 0x50;
const FF_PERIODIC: u16 = 0x51;
const FF_CONSTANT: u16 = 0x52;
const FF_SINE: u16 = 0x5a;
const FF_GAIN: u16 = 0x60;
/// _IOW('E', 0x80, struct ff_effect), com sizeof(ff_effect) = 48 no arm64/x86_64.
const EVIOCSFF: libc::c_ulong = 0x4030_4580;

/// `struct ff_effect`: cabeçalho de 14 bytes e a união, que no kernel começa no byte 16 (ela tem
/// um ponteiro, então alinha em 8). Sem o `_pad` explícito o Rust põe a união no byte 14 e as
/// intensidades caem uma casa pro lado: o kernel aceitava o efeito, mas com força zero.
#[repr(C, align(8))]
struct FfEffect {
    kind: u16,
    id: i16,
    direction: u16,
    trigger: [u16; 2],
    replay_length: u16,
    replay_delay: u16,
    _pad: u16,
    union: [u16; 16],
}

struct Motor {
    file: File,
    path: String,
    id: i16,
    kind: u16,
    length: u16,
}

impl Motor {
    fn open() -> Result<Motor, String> {
        let mut last = "nenhum motor de vibração (capabilities/ff) em /sys/class/input".to_string();
        for entry in std::fs::read_dir("/sys/class/input").map_err(|e| e.to_string())?.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if !name.starts_with("event") {
                continue;
            }
            let ff = std::fs::read_to_string(entry.path().join("device/capabilities/ff")).unwrap_or_default();
            if ff.split_whitespace().all(|w| w.trim_start_matches('0').is_empty()) {
                continue;
            }
            let path = format!("/dev/input/{name}");
            match OpenOptions::new().read(true).write(true).open(&path) {
                Ok(file) => {
                    let mut motor = Motor { file, path, id: -1, kind: 0, length: 0 };
                    // Alguns drivers (o spmi_haptics do 6T entre eles) aceitam FF_GAIN; sem
                    // ajustar, o ganho pode começar baixo. Ganho máximo: a força vem da duração.
                    let _ = motor.send(FF_GAIN, 0xffff);
                    return Ok(motor);
                }
                Err(e) => last = format!("{path}: {e}"),
            }
        }
        Err(last)
    }

    /// Grava (ou regrava) o efeito com a duração pedida. Cada driver aceita tipos diferentes:
    /// tenta rumble, depois periódico, depois constante.
    fn upload(&mut self, length: u16) -> Result<(), String> {
        let kinds: Vec<u16> = if self.kind != 0 { vec![self.kind] } else { vec![FF_RUMBLE, FF_PERIODIC, FF_CONSTANT] };
        for kind in kinds {
            let mut union = [0u16; 16];
            match kind {
                // forte e fraco no máximo: cada driver lê um (o memless junta os dois)
                FF_RUMBLE => union = [0xffff, 0xffff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
                FF_PERIODIC => {
                    union[0] = FF_SINE; // waveform
                    union[1] = 10; // period (ms)
                    union[2] = 0x7fff; // magnitude
                }
                _ => union[0] = 0x7fff, // level
            }
            let mut effect = FfEffect {
                kind,
                id: self.id,
                direction: 0,
                trigger: [0; 2],
                replay_length: length,
                replay_delay: 0,
                _pad: 0,
                union,
            };
            // SAFETY: effect tem o layout de struct ff_effect e vive durante a chamada.
            if unsafe { libc::ioctl(self.file.as_raw_fd(), EVIOCSFF as _, &mut effect) } == 0 {
                self.id = effect.id;
                self.kind = kind;
                self.length = length;
                return Ok(());
            }
        }
        Err(format!("o motor recusou o efeito: {}", std::io::Error::last_os_error()))
    }

    fn play(&mut self, length: u16) -> Result<(), String> {
        if self.id < 0 || self.length != length {
            self.upload(length)?;
        }
        self.send(self.id as u16, 1)
    }

    /// struct input_event do tipo EV_FF: timeval (16 bytes, ignorado na escrita), type, code, value.
    fn send(&mut self, code: u16, value: i32) -> Result<(), String> {
        let mut event = [0u8; 24];
        event[16..18].copy_from_slice(&EV_FF.to_ne_bytes());
        event[18..20].copy_from_slice(&code.to_ne_bytes());
        event[20..24].copy_from_slice(&value.to_ne_bytes());
        self.file.write_all(&event).map_err(|e| e.to_string())
    }
}

fn sender() -> &'static Mutex<Sender<u16>> {
    static TX: OnceLock<Mutex<Sender<u16>>> = OnceLock::new();
    TX.get_or_init(|| {
        let (tx, rx) = channel::<u16>();
        std::thread::spawn(move || {
            // Sem motor (ou sem permissão ainda: a regra udev pode chegar depois do shell), tenta
            // de novo a cada 30 s de toques, e só repete no journal quando o erro muda.
            let mut motor: Option<Motor> = None;
            let mut last_error = String::new();
            let mut retry_at = std::time::Instant::now();
            for length in rx {
                if motor.is_none() && std::time::Instant::now() >= retry_at {
                    match Motor::open() {
                        Ok(m) => {
                            eprintln!("lumna-haptics: motor em {}", m.path);
                            motor = Some(m);
                        }
                        Err(e) => {
                            if e != last_error {
                                eprintln!("lumna-haptics: {e}");
                                last_error = e;
                            }
                            retry_at = std::time::Instant::now() + std::time::Duration::from_secs(30);
                        }
                    }
                }
                let Some(m) = motor.as_mut() else { continue };
                let first = m.id < 0;
                match m.play(length) {
                    Ok(()) if first => eprintln!("lumna-haptics: efeito tipo 0x{:x} gravado ({length} ms)", m.kind),
                    Ok(()) => {}
                    Err(e) => {
                        eprintln!("lumna-haptics: {e}");
                        motor = None;
                        retry_at = std::time::Instant::now() + std::time::Duration::from_secs(30);
                    }
                }
            }
        });
        Mutex::new(tx)
    })
}

/// Uma vibração de `ms` milissegundos (limitada entre 15 e 80: o motor LRA do 6T precisa de uns
/// 20 ms só pra embalar, abaixo disso não se sente nada).
#[tauri::command]
pub fn haptic(ms: u16) {
    if let Ok(tx) = sender().lock() {
        let _ = tx.send(ms.clamp(15, 80));
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn ff_effect_tem_o_tamanho_do_kernel() {
        assert_eq!(std::mem::size_of::<super::FfEffect>(), 48);
        assert_eq!(std::mem::offset_of!(super::FfEffect, replay_length), 10);
        assert_eq!(std::mem::offset_of!(super::FfEffect, union), 16);
    }
}
