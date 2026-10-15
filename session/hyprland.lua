-- LUMNA OS: configuração do Hyprland (compositor) da sessão LUMNA.
-- Instalada em /usr/share/lumna/hyprland.lua. Ajustes pessoais vão em
-- ~/.config/lumna/hyprland.lua, carregado no fim deste arquivo.

---------------------------------
---- MONITORES E AMBIENTE ----
---------------------------------

hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "auto" })

-- Ordem e alinhamento das telas escolhidos em Configurações → Telas (gerado pelo LUMNA).
do
    local home = os.getenv("HOME")
    local arranged = home and io.open(home .. "/.config/lumna/monitors.lua", "r")
    if arranged then
        arranged:close()
        pcall(dofile, home .. "/.config/lumna/monitors.lua")
    end
end

hl.env("XCURSOR_SIZE", "24")
hl.env("HYPRCURSOR_SIZE", "24")
-- Apps GTK e Qt no Wayland, sem cair no XWayland à toa.
hl.env("GDK_BACKEND", "wayland,x11")
hl.env("QT_QPA_PLATFORM", "wayland;xcb")

local terminal = "foot"

hl.on("hyprland.start", function()
    hl.exec_cmd("swaybg -m fill -i /usr/share/lumna/wallpaper.png")
    -- A saída do shell vai para o journal: journalctl -t lumna-shell. Se ele cair, volta sozinho.
    hl.exec_cmd("systemd-cat -t lumna-shell /usr/lib/lumna/lumna-shell-run")
end)

-----------------
---- VISUAL ----
-----------------

-- Teclado escolhido na instalação: /etc/lumna/keyboard tem "layout variante" ("br abnt2",
-- "us", "us intl"). Sem o arquivo (live), ABNT2.
local kb_layout, kb_variant = "br", "abnt2"
pcall(function()
    local file = io.open("/etc/lumna/keyboard", "r")
    if not file then return end
    local layout, variant = (file:read("l") or ""):match("^(%S+)%s*(%S*)")
    file:close()
    if layout then kb_layout, kb_variant = layout, variant end
end)

hl.config({
    general = {
        gaps_in = 6,
        gaps_out = 14,
        border_size = 2,
        col = {
            active_border = { colors = { "rgba(a78bfaee)", "rgba(7c3aedee)" }, angle = 45 },
            inactive_border = "rgba(2a2440aa)",
        },
        resize_on_border = true,
        allow_tearing = false,
        layout = "dwindle",
    },

    decoration = {
        rounding = 14,
        rounding_power = 2,
        shadow = { enabled = true, range = 18, render_power = 3, color = 0x88080512 },
        blur = { enabled = true, size = 8, passes = 3, vibrancy = 0.2 },
    },

    animations = { enabled = true },

    misc = {
        -- Sem o start-hyprland de propósito (ver run_hyprland em gpu-env): sem o aviso dele.
        disable_watchdog_warning = true,
        force_default_wallpaper = 0,
        disable_hyprland_logo = true,
        disable_splash_rendering = true,
    },

    input = {
        kb_layout = kb_layout,
        kb_variant = kb_variant,
        follow_mouse = 1,
        touchpad = { natural_scroll = true },
    },
})

-- Curva única do LUMNA: tudo no sistema acelera e freia do mesmo jeito.
hl.curve("lumna", { type = "bezier", points = { { 0.22, 1 }, { 0.36, 1 } } })
hl.curve("lumnaSpring", { type = "spring", mass = 1, stiffness = 220, dampening = 24 })

hl.animation({ leaf = "global", enabled = true, speed = 6, bezier = "lumna" })
hl.animation({ leaf = "windows", enabled = true, speed = 4.5, spring = "lumnaSpring" })
hl.animation({ leaf = "windowsIn", enabled = true, speed = 4, spring = "lumnaSpring", style = "popin 85%" })
hl.animation({ leaf = "windowsOut", enabled = true, speed = 2, bezier = "lumna", style = "popin 85%" })
hl.animation({ leaf = "fade", enabled = true, speed = 3, bezier = "lumna" })
hl.animation({ leaf = "layers", enabled = true, speed = 3.5, bezier = "lumna" })
hl.animation({ leaf = "layersIn", enabled = true, speed = 3.5, bezier = "lumna", style = "slide" })
hl.animation({ leaf = "layersOut", enabled = true, speed = 2, bezier = "lumna", style = "fade" })
hl.animation({ leaf = "workspaces", enabled = true, speed = 4, bezier = "lumna", style = "slidefade 12%" })
hl.animation({ leaf = "specialWorkspace", enabled = true, speed = 4, bezier = "lumna", style = "slidefadevert 20%" })

-- Vidro fosco da topbar: o blur é do compositor, não do CSS (ver docs/VIABILIDADE.md, 3.2).
hl.layer_rule({
    name = "lumna-topbar-blur",
    match = { namespace = "^lumna-topbar$" },
    blur = true,
    ignore_alpha = 0.2,
})

hl.layer_rule({
    name = "lumna-dock-blur",
    match = { namespace = "^lumna-dock$" },
    blur = true,
    ignore_alpha = 0.2,
})

-- O launcher cobre a tela com um véu: blur no véu inteiro, sem animação própria (o Vue anima).
hl.layer_rule({
    name = "lumna-launcher",
    match = { namespace = "^lumna-launcher$" },
    blur = true,
    no_anim = true,
})

hl.layer_rule({
    name = "lumna-switcher-blur",
    match = { namespace = "^lumna-switcher$" },
    blur = true,
    ignore_alpha = 0.3,
})

-- Menu do Wi-Fi: a camada cobre a tela (clique fora fecha), mas só o painel é vidro.
hl.layer_rule({
    name = "lumna-wifi",
    match = { namespace = "^lumna-wifi$" },
    blur = true,
    ignore_alpha = 0.3,
    no_anim = true,
})

-- Senha de administrador: véu escuro com blur na tela toda, como o launcher.
hl.layer_rule({
    name = "lumna-auth",
    match = { namespace = "^lumna-auth$" },
    blur = true,
    no_anim = true,
})

hl.layer_rule({
    name = "lumna-notifications-blur",
    match = { namespace = "^lumna-notifications$" },
    blur = true,
    ignore_alpha = 0.3,
})

-------------------------------
---- JANELAS (Desktop) ----
-------------------------------

-- No modo Desktop as janelas abrem flutuando e centralizadas, como num desktop tradicional.
hl.window_rule({
    name = "lumna-desktop-float",
    match = { class = ".*" },
    float = true,
    center = true,
})

hl.window_rule({
    name = "suppress-maximize-events",
    match = { class = ".*" },
    suppress_event = "maximize",
})

-----------------
---- ATALHOS ----
-----------------

local mod = "SUPER"

hl.bind(mod .. " + Return", hl.dsp.exec_cmd(terminal))
hl.bind(mod .. " + Space", hl.dsp.exec_cmd("lumna-ctl launcher toggle"))
hl.bind(mod .. " + E", hl.dsp.exec_cmd("lumna-ctl files"))
hl.bind(mod .. " + B", hl.dsp.exec_cmd("firefox"))
hl.bind(mod .. " + I", hl.dsp.exec_cmd("lumna-ctl settings"))
hl.bind(mod .. " + L", hl.dsp.exec_cmd("lumna-ctl store"))
hl.bind(mod .. " + A", hl.dsp.exec_cmd("lumna-ctl emma"))
-- Na Mídia: fecha o serviço/vídeo em tela cheia e volta para a tela da Mídia.
hl.bind(mod .. " + Escape", hl.dsp.exec_cmd("lumna-ctl media home"))
hl.bind(mod .. " + Q", hl.dsp.window.close())
hl.bind(mod .. " + F", hl.dsp.window.fullscreen())
hl.bind(mod .. " + T", hl.dsp.window.float({ action = "toggle" }))
hl.bind(mod .. " + SHIFT + E", hl.dsp.exit())
-- Modos: Desktop → Gaming → Mídia (ver docs/VIABILIDADE.md, seção 4).
hl.bind(mod .. " + Tab", hl.dsp.exec_cmd("lumna-ctl mode next"))
hl.bind(mod .. " + SHIFT + Tab", hl.dsp.exec_cmd("lumna-ctl mode prev"))

for i = 1, 9 do
    hl.bind(mod .. " + " .. i, hl.dsp.focus({ workspace = i }))
    hl.bind(mod .. " + SHIFT + " .. i, hl.dsp.window.move({ workspace = i }))
end

hl.bind(mod .. " + mouse:272", hl.dsp.window.drag(), { mouse = true })
hl.bind(mod .. " + mouse:273", hl.dsp.window.resize(), { mouse = true })

hl.bind("XF86AudioRaiseVolume", hl.dsp.exec_cmd("wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ 5%+"), { locked = true, repeating = true })
hl.bind("XF86AudioLowerVolume", hl.dsp.exec_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-"), { locked = true, repeating = true })
hl.bind("XF86AudioMute", hl.dsp.exec_cmd("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"), { locked = true })

-----------------------------
---- AJUSTES PESSOAIS ----
-----------------------------

local home = os.getenv("HOME")
if home then
    local user = home .. "/.config/lumna/hyprland.lua"
    local f = io.open(user, "r")
    if f then
        f:close()
        dofile(user)
    end
end
