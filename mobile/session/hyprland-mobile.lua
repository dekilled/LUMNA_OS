-- LUMNA no celular (OnePlus 6T): tela 1080x2340 (402 ppi), escala 2.5 = 432x936 lógicos.
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "2.5" })
hl.env("XCURSOR_SIZE", "24")
hl.env("GDK_BACKEND", "wayland")
hl.env("QT_QPA_PLATFORM", "wayland")
hl.env("XDG_CURRENT_DESKTOP", "Hyprland:LUMNA")

-- Celular: um app por vez, ocupando o espaço entre a barra de status e a faixa da barrinha (o
-- dock some com um app aberto). Sem vãos, bordas nem cantos arredondados: o app encosta reto
-- nas barras. O vidro fosco fica pras barras do LUMNA.
hl.config({
    general = { gaps_in = 0, gaps_out = 0, border_size = 0, layout = "dwindle" },
    decoration = {
        rounding = 0,
        blur = { enabled = true, size = 8, passes = 3, vibrancy = 0.2 },
    },
    misc = {
        disable_watchdog_warning = true,
        force_default_wallpaper = 0,
        disable_hyprland_logo = true,
        disable_splash_rendering = true,
    },
})

hl.window_rule({
    name = "lumna-mobile-maximize",
    match = { class = ".*" },
    maximize = true,
})

hl.on("hyprland.start", function()
    -- A saída do shell vai para o journal: journalctl -t lumna-shell
    hl.exec_cmd("systemd-cat -t lumna-shell lumna-shell --mobile")
end)
