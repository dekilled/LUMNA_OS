-- LUMNA no celular (OnePlus 6T): tela 1080x2340 (402 ppi), escala 2.5 = 432x936 lógicos.
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "2.5" })
hl.env("XCURSOR_SIZE", "24")
hl.env("GDK_BACKEND", "wayland")
hl.env("QT_QPA_PLATFORM", "wayland")
hl.env("XDG_CURRENT_DESKTOP", "Hyprland:LUMNA")

hl.on("hyprland.start", function()
    -- A saída do shell vai para o journal: journalctl -t lumna-shell
    hl.exec_cmd("systemd-cat -t lumna-shell lumna-shell --mobile")
end)
