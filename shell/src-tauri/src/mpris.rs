//! O LUMNA Music no MPRIS (`org.mpris.MediaPlayer2.lumna_music`): o padrão de mídia do Linux.
//! Com ele, os botões do fone (pelo `playerctl` nos atalhos do Hyprland), o Bluetooth (AVRCP pelo
//! `mpris-proxy`), a Emma e o Connect controlam a música sem saber que é o LUMNA.

use std::collections::HashMap;
use std::sync::OnceLock;

use tauri::{AppHandle, Manager};
use zbus::object_server::SignalEmitter;
use zbus::zvariant::{ObjectPath, OwnedValue, Value};
use zbus::{interface, Connection};

use crate::music::{self, Repeat, SharedMusic};

const PATH: &str = "/org/mpris/MediaPlayer2";
static CONNECTION: OnceLock<Connection> = OnceLock::new();

struct Root {
    app: AppHandle,
}

#[interface(name = "org.mpris.MediaPlayer2")]
impl Root {
    fn raise(&self) {
        let _ = crate::open_music(&self.app);
    }
    fn quit(&self) {
        let _ = music::control(&self.app, "stop");
    }
    #[zbus(property)]
    fn can_raise(&self) -> bool {
        true
    }
    #[zbus(property)]
    fn can_quit(&self) -> bool {
        true
    }
    #[zbus(property)]
    fn has_track_list(&self) -> bool {
        false
    }
    #[zbus(property)]
    fn identity(&self) -> String {
        "LUMNA Music".into()
    }
    #[zbus(property)]
    fn desktop_entry(&self) -> String {
        "lumna-music".into()
    }
    #[zbus(property)]
    fn supported_uri_schemes(&self) -> Vec<String> {
        vec!["file".into()]
    }
    #[zbus(property)]
    fn supported_mime_types(&self) -> Vec<String> {
        ["audio/mpeg", "audio/flac", "audio/ogg", "audio/mp4", "audio/x-wav"].iter().map(|s| s.to_string()).collect()
    }
}

struct Player {
    app: AppHandle,
}

/// Roda fora da thread do D-Bus (o mpv pode levar um instante pra abrir).
fn run(app: &AppHandle, f: impl FnOnce(&AppHandle) -> Result<(), String> + Send + 'static) {
    let app = app.clone();
    std::thread::spawn(move || {
        if let Err(err) = f(&app) {
            eprintln!("lumna-shell: música: {err}");
        }
    });
}

fn track_id(index: Option<usize>) -> ObjectPath<'static> {
    match index {
        Some(i) => ObjectPath::try_from(format!("/org/lumna/music/track/{i}")).unwrap_or_else(|_| ObjectPath::from_static_str_unchecked("/org/mpris/MediaPlayer2/TrackList/NoTrack")),
        None => ObjectPath::from_static_str_unchecked("/org/mpris/MediaPlayer2/TrackList/NoTrack"),
    }
}

fn owned(value: Value<'_>) -> Option<OwnedValue> {
    value.try_to_owned().ok()
}

#[interface(name = "org.mpris.MediaPlayer2.Player")]
impl Player {
    fn play(&self) {
        run(&self.app, |a| music::control(a, "play"));
    }
    fn pause(&self) {
        run(&self.app, |a| music::control(a, "pause"));
    }
    fn play_pause(&self) {
        run(&self.app, |a| music::control(a, "toggle"));
    }
    fn stop(&self) {
        run(&self.app, |a| music::control(a, "stop"));
    }
    fn next(&self) {
        run(&self.app, |a| music::control(a, "next"));
    }
    fn previous(&self) {
        run(&self.app, |a| music::control(a, "previous"));
    }
    /// `offset` em microssegundos, a partir de onde está.
    fn seek(&self, offset: i64) {
        let position = music::state(&self.app).position;
        run(&self.app, move |a| music::seek(a, position + offset as f64 / 1e6));
    }
    fn set_position(&self, _track: ObjectPath<'_>, position: i64) {
        run(&self.app, move |a| music::seek(a, position as f64 / 1e6));
    }
    fn open_uri(&self, _uri: String) {}

    #[zbus(property)]
    fn playback_status(&self) -> String {
        let s = music::state(&self.app);
        if s.index.is_none() {
            "Stopped"
        } else if s.playing {
            "Playing"
        } else {
            "Paused"
        }
        .into()
    }
    #[zbus(property)]
    fn loop_status(&self) -> String {
        match music::state(&self.app).repeat {
            Repeat::Off => "None",
            Repeat::All => "Playlist",
            Repeat::One => "Track",
        }
        .into()
    }
    #[zbus(property)]
    fn shuffle(&self) -> bool {
        music::state(&self.app).shuffle
    }
    #[zbus(property)]
    fn metadata(&self) -> HashMap<String, OwnedValue> {
        let s = music::state(&self.app);
        let mut map = HashMap::new();
        if let Some(v) = owned(Value::from(track_id(s.index))) {
            map.insert("mpris:trackid".to_string(), v);
        }
        if let Some(t) = s.current() {
            let length = (t.duration.max(s.duration) * 1e6) as i64;
            let entries: Vec<(&str, Value<'_>)> = vec![
                ("mpris:length", Value::from(length)),
                ("xesam:title", Value::from(t.title.clone())),
                ("xesam:album", Value::from(t.album.clone())),
                ("xesam:artist", Value::from(vec![t.artist.clone()])),
                ("xesam:albumArtist", Value::from(vec![t.album_artist.clone()])),
                ("xesam:trackNumber", Value::from(t.track as i32)),
                ("xesam:url", Value::from(format!("file://{}", t.path))),
            ];
            for (key, value) in entries {
                if let Some(v) = owned(value) {
                    map.insert(key.to_string(), v);
                }
            }
            if let Some(cover) = &t.cover {
                if let Some(v) = owned(Value::from(format!("file://{cover}"))) {
                    map.insert("mpris:artUrl".to_string(), v);
                }
            }
        }
        map
    }
    #[zbus(property)]
    fn volume(&self) -> f64 {
        1.0
    }
    #[zbus(property)]
    fn position(&self) -> i64 {
        (music::state(&self.app).position * 1e6) as i64
    }
    #[zbus(property)]
    fn rate(&self) -> f64 {
        1.0
    }
    #[zbus(property)]
    fn minimum_rate(&self) -> f64 {
        1.0
    }
    #[zbus(property)]
    fn maximum_rate(&self) -> f64 {
        1.0
    }
    #[zbus(property)]
    fn can_go_next(&self) -> bool {
        music::state(&self.app).index.is_some()
    }
    #[zbus(property)]
    fn can_go_previous(&self) -> bool {
        music::state(&self.app).index.is_some()
    }
    #[zbus(property)]
    fn can_play(&self) -> bool {
        music::state(&self.app).index.is_some()
    }
    #[zbus(property)]
    fn can_pause(&self) -> bool {
        true
    }
    #[zbus(property)]
    fn can_seek(&self) -> bool {
        true
    }
    #[zbus(property)]
    fn can_control(&self) -> bool {
        true
    }
}

/// Entra no D-Bus da sessão com o nome do LUMNA Music.
pub fn start(app: AppHandle) {
    tauri::async_runtime::spawn(async move {
        let built = async {
            zbus::connection::Builder::session()?
                .name("org.mpris.MediaPlayer2.lumna_music")?
                .serve_at(PATH, Root { app: app.clone() })?
                .serve_at(PATH, Player { app: app.clone() })?
                .build()
                .await
        };
        match built.await {
            Ok(conn) => {
                let _ = CONNECTION.set(conn);
            }
            Err(err) => eprintln!("lumna-shell: MPRIS: {err}"),
        }
    });
}

/// O estado mudou (outra faixa, pausou): avisa quem escuta o MPRIS.
pub fn changed(app: &AppHandle) {
    let Some(conn) = CONNECTION.get().cloned() else { return };
    let _ = app.try_state::<SharedMusic>();
    tauri::async_runtime::spawn(async move {
        let Ok(iface) = conn.object_server().interface::<_, Player>(PATH).await else { return };
        let emitter: &SignalEmitter<'static> = iface.signal_emitter();
        let player = iface.get().await;
        let _ = player.playback_status_changed(emitter).await;
        let _ = player.metadata_changed(emitter).await;
        let _ = player.loop_status_changed(emitter).await;
        let _ = player.shuffle_changed(emitter).await;
        let _ = player.can_play_changed(emitter).await;
    });
}
