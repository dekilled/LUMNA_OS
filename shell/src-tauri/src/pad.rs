//! Controle de videogame (e, na Fase 4, o celular) vira um conjunto pequeno de ações que as telas
//! navegáveis entendem: `up`, `down`, `left`, `right`, `accept`, `back`, `menu`.
//!
//! O controle é lido aqui no Rust (gilrs, pelo evdev) e não pela Gamepad API do WebKit: assim o
//! shell decide quem recebe o comando e não briga com o jogo aberto.

use std::time::{Duration, Instant};

use gilrs::{Axis, Button, EventType, Gilrs};
use serde::Serialize;
use tauri::AppHandle;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Action {
    Up,
    Down,
    Left,
    Right,
    Accept,
    Back,
    Menu,
    /// Botão central (Guide / PS / Xbox): troca de modo, como `Super + Tab`.
    Guide,
}

impl Action {
    pub fn parse(text: &str) -> Option<Action> {
        Some(match text {
            "up" => Action::Up,
            "down" => Action::Down,
            "left" => Action::Left,
            "right" => Action::Right,
            "accept" => Action::Accept,
            "back" => Action::Back,
            "menu" => Action::Menu,
            "guide" => Action::Guide,
            _ => return None,
        })
    }

    fn is_direction(self) -> bool {
        matches!(self, Action::Up | Action::Down | Action::Left | Action::Right)
    }
}

fn button_action(button: Button) -> Option<Action> {
    Some(match button {
        Button::DPadUp => Action::Up,
        Button::DPadDown => Action::Down,
        Button::DPadLeft => Action::Left,
        Button::DPadRight => Action::Right,
        Button::South => Action::Accept,
        Button::East => Action::Back,
        Button::Start => Action::Menu,
        Button::Mode => Action::Guide,
        _ => return None,
    })
}

/// Direção do analógico esquerdo, com zona morta generosa (controles velhos derivam).
fn stick_action(x: f32, y: f32) -> Option<Action> {
    const DEAD_ZONE: f32 = 0.6;
    if x.abs() < DEAD_ZONE && y.abs() < DEAD_ZONE {
        return None;
    }
    Some(if x.abs() > y.abs() {
        if x > 0.0 { Action::Right } else { Action::Left }
    } else if y > 0.0 {
        // No gilrs o eixo Y do analógico cresce para cima.
        Action::Up
    } else {
        Action::Down
    })
}

const FIRST_REPEAT: Duration = Duration::from_millis(380);
const NEXT_REPEAT: Duration = Duration::from_millis(110);

/// Lê os controles numa thread própria. Direção segurada se repete, como num console.
pub fn watch(app: AppHandle) {
    std::thread::spawn(move || {
        let mut gilrs = match Gilrs::new() {
            Ok(gilrs) => gilrs,
            Err(err) => {
                eprintln!("lumna-shell: controles indisponíveis: {err}");
                return;
            }
        };
        let (mut stick_x, mut stick_y) = (0.0_f32, 0.0_f32);
        // Direção segurada agora (botão ou analógico) e quando ela se repete.
        let mut held: Option<(Action, Instant)> = None;
        loop {
            while let Some(event) = gilrs.next_event_blocking(Some(Duration::from_millis(30))) {
                match event.event {
                    EventType::ButtonPressed(button, _) => {
                        if let Some(action) = button_action(button) {
                            crate::pad_action(&app, action);
                            if action.is_direction() {
                                held = Some((action, Instant::now() + FIRST_REPEAT));
                            }
                        }
                    }
                    EventType::ButtonReleased(button, _) => {
                        if button_action(button).is_some_and(|a| held.is_some_and(|(h, _)| h == a)) {
                            held = None;
                        }
                    }
                    EventType::AxisChanged(axis, value, _) => {
                        match axis {
                            Axis::LeftStickX => stick_x = value,
                            Axis::LeftStickY => stick_y = value,
                            _ => continue,
                        }
                        let now = stick_action(stick_x, stick_y);
                        let before = held.map(|(a, _)| a);
                        if now != before {
                            held = now.map(|a| {
                                crate::pad_action(&app, a);
                                (a, Instant::now() + FIRST_REPEAT)
                            });
                        }
                    }
                    EventType::Disconnected => held = None,
                    _ => {}
                }
            }
            if let Some((action, at)) = held {
                if Instant::now() >= at {
                    crate::pad_action(&app, action);
                    held = Some((action, Instant::now() + NEXT_REPEAT));
                }
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stick_has_a_dead_zone() {
        assert_eq!(stick_action(0.3, -0.2), None);
        assert_eq!(stick_action(0.9, 0.1), Some(Action::Right));
        assert_eq!(stick_action(-0.1, -0.95), Some(Action::Down));
        assert_eq!(stick_action(0.0, 0.8), Some(Action::Up));
    }

    #[test]
    fn parses_actions_from_text() {
        assert_eq!(Action::parse("accept"), Some(Action::Accept));
        assert_eq!(Action::parse("pular"), None);
    }
}
