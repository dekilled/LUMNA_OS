-- LUMNA OS: Hyprland da tela de login (só o lumna-shell --greeter em tela cheia, sem atalhos).

hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "auto" })

-- Ordem das telas escolhida em Configurações → Telas (a última pessoa que arrumou). O arquivo é só
-- dados, "NOME X Y ESCALA" por linha, e cada campo é conferido antes de virar regra.
pcall(function()
    local file = io.open("/var/lib/lumna/monitors", "r")
    if not file then return end
    for line in file:lines() do
        local name, x, y, scale = line:match("^([%w%-_]+) (%-?%d+) (%-?%d+) (%d+%.?%d*)$")
        if name and #name <= 32 and tonumber(scale) and tonumber(scale) > 0 and tonumber(scale) <= 4 then
            hl.monitor({ output = name, mode = "preferred", position = x .. "x" .. y, scale = scale })
        end
    end
    file:close()
end)
hl.env("XCURSOR_SIZE", "24")

local kb_layout, kb_variant = "br", "abnt2"
pcall(function()
    local file = io.open("/etc/lumna/keyboard", "r")
    if not file then return end
    local layout, variant = (file:read("l") or ""):match("^(%S+)%s*(%S*)")
    file:close()
    if layout then kb_layout, kb_variant = layout, variant end
end)

hl.config({
    general = { border_size = 0, gaps_in = 0, gaps_out = 0 },
    decoration = { rounding = 0 },
    animations = { enabled = true },
    misc = {
        -- Sem o start-hyprland de propósito (ver run_hyprland em gpu-env): sem o aviso dele.
        disable_watchdog_warning = true,
        force_default_wallpaper = 0,
        disable_hyprland_logo = true,
        disable_splash_rendering = true,
    },
    input = { kb_layout = kb_layout, kb_variant = kb_variant, follow_mouse = 1 },
})

hl.layer_rule({
    name = "lumna-greeter",
    match = { namespace = "^lumna-greeter$" },
    blur = true,
    no_anim = true,
})

hl.on("hyprland.start", function()
    hl.exec_cmd("swaybg -m fill -i /usr/share/lumna/wallpaper.png")
    hl.exec_cmd("systemd-cat -t lumna-greeter lumna-shell --greeter")
end)
