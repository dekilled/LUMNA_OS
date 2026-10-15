//! Transforma janelas do Tauri em superfícies layer-shell (topbar, dock, launcher, notificações).
//!
//! O layer-shell precisa ser ligado antes de a janela GTK ser realizada, e o Tauri já entrega a
//! janela realizada. Então o conteúdo (a `vbox` com o webview) é movido para uma janela GTK nova,
//! que recebe o layer-shell no lugar da original, que fica escondida.
//!
//! As janelas GTK só podem ser tocadas na thread principal: o registro abaixo é por thread, e
//! quem está em outra thread passa por `AppHandle::run_on_main_thread`.

use std::cell::RefCell;
use std::collections::HashMap;

use gtk::prelude::*;
use gtk_layer_shell::{Edge, KeyboardMode, Layer, LayerShell};
use tauri::WebviewWindow;

pub enum Exclusive {
    /// Reserva o próprio tamanho na borda (as janelas dos apps não ficam por baixo).
    Auto,
    /// Cobre tudo, inclusive o espaço reservado por outras camadas.
    Ignore,
    /// Não reserva nada.
    None,
}

pub struct LayerSpec {
    pub namespace: &'static str,
    pub layer: Layer,
    pub anchors: &'static [Edge],
    pub margins: &'static [(Edge, i32)],
    pub keyboard: KeyboardMode,
    pub exclusive: Exclusive,
    /// Tamanho inicial; -1 numa direção ancorada nas duas bordas ocupa a tela toda.
    pub size: (i32, i32),
    pub visible: bool,
}

thread_local! {
    static LAYERS: RefCell<HashMap<String, gtk::ApplicationWindow>> = RefCell::new(HashMap::new());
}

/// A janela nova pertence ao `GtkApplication` do Tauri, que a mantém viva enquanto o app rodar.
pub fn make_layer(window: &WebviewWindow, monitor: &gtk::gdk::Monitor, spec: &LayerSpec) -> tauri::Result<()> {
    let original = window.gtk_window()?;
    let content = window.default_vbox()?;
    original.remove(&content);

    let layer = gtk::ApplicationWindow::new(&original.application().expect("janela do Tauri sem GtkApplication"));
    // Fundo transparente: a cor e o vidro fosco vêm do CSS e do blur do Hyprland.
    if let Some(visual) = WidgetExt::screen(&layer).and_then(|s| s.rgba_visual()) {
        layer.set_visual(Some(&visual));
    }
    layer.set_app_paintable(true);

    layer.init_layer_shell();
    layer.set_namespace(spec.namespace);
    layer.set_layer(spec.layer);
    layer.set_monitor(monitor);
    for edge in spec.anchors {
        layer.set_anchor(*edge, true);
    }
    for (edge, margin) in spec.margins {
        layer.set_layer_shell_margin(*edge, *margin);
    }
    layer.set_keyboard_mode(spec.keyboard);
    match spec.exclusive {
        Exclusive::Auto => layer.auto_exclusive_zone_enable(),
        Exclusive::Ignore => layer.set_exclusive_zone(-1),
        Exclusive::None => layer.set_exclusive_zone(0),
    }
    layer.set_size_request(spec.size.0, spec.size.1);

    layer.add(&content);
    content.show_all();
    // Monitor desligado (ou suspenso e acordado): o compositor fecha a camada e o gtk-layer-shell pede
    // para fechar a janela. A janela (e a página) ficam; só escondem, até voltarem a um monitor.
    layer.connect_delete_event(|window, _| {
        window.hide();
        gtk::glib::Propagation::Stop
    });
    if spec.visible {
        show(&layer);
    }
    LAYERS.with(|layers| layers.borrow_mut().insert(window.label().to_string(), layer));
    Ok(())
}

fn with_layer(label: &str, f: impl FnOnce(&gtk::ApplicationWindow)) {
    LAYERS.with(|layers| {
        if let Some(layer) = layers.borrow().get(label) {
            f(layer);
        }
    });
}

pub fn is_visible(label: &str) -> bool {
    let mut visible = false;
    with_layer(label, |layer| visible = layer.is_mapped());
    visible
}

/// Mostra a camada e confere se ela apareceu de fato.
///
/// O gtk-layer-shell espera só 1 s pela primeira resposta do compositor; se ela atrasar (VM lenta,
/// sistema ocupado no login), a camada é desmapeada em silêncio e fica "visível" sem aparecer.
/// Aqui ela é mostrada de novo até aparecer, ou até alguém pedir para escondê-la.
fn show(layer: &gtk::ApplicationWindow) {
    layer.show_all();
    retry_until_mapped(layer.clone(), 5);
}

fn retry_until_mapped(layer: gtk::ApplicationWindow, attempts: u32) {
    gtk::glib::timeout_add_local_once(std::time::Duration::from_millis(1500), move || {
        if layer.is_visible() && !layer.is_mapped() && attempts > 0 {
            layer.hide();
            layer.show_all();
            retry_until_mapped(layer, attempts - 1);
        }
    });
}

pub fn set_visible(label: &str, visible: bool) {
    with_layer(label, |layer| {
        if visible && !layer.is_mapped() {
            show(layer);
        } else if !visible && layer.is_visible() {
            layer.hide();
        }
    });
}

/// Ajusta a camada ao tamanho do conteúdo (o GTK não encolhe a janela sozinho).
pub fn resize(label: &str, width: i32, height: i32) {
    with_layer(label, |layer| {
        layer.set_size_request(width, height);
        layer.resize(width.max(1), height.max(1));
    });
}

pub fn exists(label: &str) -> bool {
    let mut found = false;
    with_layer(label, |_| found = true);
    found
}

/// Põe a camada noutro monitor (o mesmo de antes, se ele voltou como um monitor GTK novo).
pub fn set_monitor(label: &str, monitor: &gtk::gdk::Monitor) {
    with_layer(label, |layer| {
        // O layer-shell só troca de monitor com a superfície desmapeada.
        let was_visible = layer.is_visible();
        if was_visible {
            layer.hide();
        }
        layer.set_monitor(monitor);
        if was_visible {
            show(layer);
        }
    });
}

/// Move a camada para o monitor GTK na posição lógica (x, y) informada pelo compositor.
pub fn move_to_monitor_at(label: &str, x: i32, y: i32) {
    let Some(display) = gtk::gdk::Display::default() else { return };
    let monitor = (0..display.n_monitors())
        .filter_map(|i| display.monitor(i))
        .find(|m| m.geometry().x() == x && m.geometry().y() == y);
    if let Some(monitor) = monitor {
        with_layer(label, |layer| {
            // O layer-shell só troca de monitor com a superfície desmapeada.
            let was_visible = layer.is_visible();
            if was_visible {
                layer.hide();
            }
            layer.set_monitor(&monitor);
            if was_visible {
                show(layer);
            }
        });
    }
}

/// Reposiciona a camada sem desmapear: bordas ancoradas, margem e reserva de espaço.
/// Usado pelo dock, que muda de lugar e de forma conforme as configurações e as janelas.
pub fn place(label: &str, anchors: &[Edge], margin: i32, exclusive: Exclusive) {
    with_layer(label, |layer| {
        for edge in [Edge::Top, Edge::Bottom, Edge::Left, Edge::Right] {
            let on = anchors.contains(&edge);
            layer.set_anchor(edge, on);
            layer.set_layer_shell_margin(edge, if on { margin } else { 0 });
        }
        match exclusive {
            Exclusive::Auto => layer.auto_exclusive_zone_enable(),
            Exclusive::Ignore => layer.set_exclusive_zone(-1),
            Exclusive::None => layer.set_exclusive_zone(0),
        }
    });
}
