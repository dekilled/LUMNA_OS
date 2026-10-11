//! Transforma janelas do Tauri em superfícies layer-shell (barra presa na borda do monitor).
//!
//! O layer-shell precisa ser ligado antes de a janela GTK ser realizada, e o Tauri já entrega a
//! janela realizada. Então o conteúdo (a `vbox` com o webview) é movido para uma janela GTK nova,
//! que recebe o layer-shell e é mostrada no lugar da original, que fica escondida.

use gtk::prelude::*;
use gtk_layer_shell::{Edge, KeyboardMode, Layer, LayerShell};
use tauri::WebviewWindow;

pub const TOPBAR_HEIGHT: i32 = 36;

/// A janela nova pertence ao `GtkApplication` do Tauri, que a mantém viva enquanto o app rodar.
pub fn make_topbar(window: &WebviewWindow, monitor: &gtk::gdk::Monitor) -> tauri::Result<()> {
    let original = window.gtk_window()?;
    let content = window.default_vbox()?;
    original.remove(&content);

    let bar = gtk::ApplicationWindow::new(&original.application().expect("janela do Tauri sem GtkApplication"));
    // Fundo transparente: a cor e o vidro fosco vêm do CSS e do blur do Hyprland.
    if let Some(visual) = WidgetExt::screen(&bar).and_then(|s| s.rgba_visual()) {
        bar.set_visual(Some(&visual));
    }
    bar.set_app_paintable(true);

    bar.init_layer_shell();
    bar.set_namespace("lumna-topbar");
    bar.set_layer(Layer::Top);
    bar.set_monitor(monitor);
    for edge in [Edge::Top, Edge::Left, Edge::Right] {
        bar.set_anchor(edge, true);
    }
    bar.set_keyboard_mode(KeyboardMode::None);
    bar.set_size_request(-1, TOPBAR_HEIGHT);
    bar.auto_exclusive_zone_enable();

    bar.add(&content);
    bar.show_all();
    Ok(())
}
