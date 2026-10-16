#!/bin/sh
# Primeiro teste do Hyprland no celular. Rode NO CELULAR (ssh), com internet pela USB:
#   sudo sh primeira-tela.sh
# Instala o básico, liga o login automático do alarm no tty1 e sobe o Hyprland nele com um
# terminal (foot) aberto. O log fica em /home/alarm/hyprland.log. Desfazer: sudo sh primeira-tela.sh desfazer
set -eu
[ "$(id -u)" = 0 ] || { echo "rode com sudo"; exit 1; }
U=alarm
H=/home/$U
OVR=/etc/systemd/system/getty@tty1.service.d/lumna-autologin.conf

if [ "${1:-}" = desfazer ]; then
    rm -f "$OVR" "$H/.config/lumna/hyprland-teste.lua"
    sed -i '/# lumna-teste/d' "$H/.bash_profile"
    systemctl daemon-reload && systemctl restart getty@tty1
    echo "ok: desfeito"; exit 0
fi

pacman -S --needed --noconfirm hyprland mesa foot ttf-dejavu

mkdir -p "$H/.config/lumna" "$(dirname "$OVR")"
cat > "$H/.config/lumna/hyprland-teste.lua" <<'EOF'
-- Teste do LUMNA no 6T: tela 1080x2340 (402 ppi), escala 2.5 = 432x936 lógicos
hl.monitor({ output = "", mode = "preferred", position = "auto", scale = "2.5" })
hl.env("XCURSOR_SIZE", "24")
hl.on("hyprland.start", function()
    hl.exec_cmd("foot")
end)
EOF
chown -R "$U:$U" "$H/.config"

cat > "$OVR" <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty -o '-p -f -- \\\\u' --noclear --autologin $U %I \$TERM
EOF

# só uma tentativa por boot: se o Hyprland cair, não fica reiniciando em loop
touch "$H/.bash_profile"
sed -i '/# lumna-teste/d' "$H/.bash_profile"
cat >> "$H/.bash_profile" <<'EOF'
if [ "$(tty)" = /dev/tty1 ] && [ ! -e /tmp/lumna-hypr-tentou ]; then touch /tmp/lumna-hypr-tentou; Hyprland --config ~/.config/lumna/hyprland-teste.lua > ~/hyprland.log 2>&1; fi # lumna-teste
EOF
chown "$U:$U" "$H/.bash_profile"

systemctl daemon-reload
rm -f /tmp/lumna-hypr-tentou
systemctl restart getty@tty1
echo "ok: olhe a tela do celular. Log: tail -f $H/hyprland.log"
