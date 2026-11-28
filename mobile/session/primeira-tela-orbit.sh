#!/bin/sh
# Primeira tela do Orbit + Gravity no celular (o primeira-tela.sh sobe o Hyprland; este sobe a UI nova).
# Rode NO CELULAR (ssh alarm@lumna), com internet pela USB:
#   sudo sh primeira-tela-orbit.sh
# Instala o lumna-orbit e o lumna-gravity (do repositório [lumna-celular], compilados em aarch64 pelo GitHub),
# liga o login automático do alarm no tty1 e sobe o Orbit nele, com papel de parede, barra, lançador, teclado
# na tela e um terminal (foot). Só UMA tentativa por boot: se o Orbit cair, volta ao terminal (nada de loop).
# Logs: ~/orbit-celular.log (o Orbit) e ~/orbit-celular-pecas.log (as peças do Gravity).
# Desfazer: sudo sh primeira-tela-orbit.sh desfazer   (tira o login automático e o arranque; os pacotes ficam)
# Escala diferente: ORBIT_PHONE_SCALE=2 sudo -E sh primeira-tela-orbit.sh   (de 1.0 a 3.0; padrão aqui: 2.5, a do teste do Hyprland)
set -eu
[ "$(id -u)" = 0 ] || { echo "rode com sudo"; exit 1; }
U=alarm
H=/home/$U
OVR=/etc/systemd/system/getty@tty1.service.d/lumna-autologin.conf
ESCALA=${ORBIT_PHONE_SCALE:-2.5}
REPO=https://github.com/dekilled/LUMNA_OS/releases/download/celular

if [ "${1:-}" = desfazer ]; then
    rm -f "$OVR"
    sed -i '/# lumna-teste/d' "$H/.bash_profile"
    systemctl daemon-reload && systemctl restart getty@tty1
    echo "ok: desfeito (o login automático e o arranque do Orbit saíram; os pacotes continuam instalados)"; exit 0
fi

# ---- os pacotes ----
# O repositório do LUMNA só entra no pacman quando o GitHub já publicou. O banco vem direto, sem o -Sy
# (sincronizar só um lado do sistema quebraria as atualizações do Arch).
if curl -fsSL --max-time 60 -o /tmp/lumna-celular.db "$REPO/lumna-celular.db"; then
    if ! grep -q '^\[lumna-celular\]' /etc/pacman.conf; then
        printf '\n# Os pacotes do LUMNA para o celular (workflow "Celular" do LUMNA_OS)\n[lumna-celular]\nSigLevel = Optional TrustAll\nServer = %s\n' "$REPO" >>/etc/pacman.conf
    fi
    install -Dm644 /tmp/lumna-celular.db /var/lib/pacman/sync/lumna-celular.db
    rm -f /tmp/lumna-celular.db
else
    echo "erro: o repositório [lumna-celular] não respondeu (sem internet, ou o GitHub ainda não publicou)"; exit 1
fi
# O wlroots0.20 e as bibliotecas vêm do Arch ARM. Se o pacman reclamar de arquivo 404, o banco do Arch está velho:
# rode "sudo pacman -Syu" (atualiza o sistema inteiro, de propósito) e este script de novo.
pacman -S --needed --noconfirm lumna-orbit lumna-gravity

# ---- o arranque ----
install -d -o "$U" -g "$U" "$H/.config/lumna"
cat > "$H/.config/lumna/orbit-celular.sh" <<EOF
#!/bin/sh
# Gerado por primeira-tela-orbit.sh. O Orbit no celular: a saída DSI vira "telefone" sozinha (escala própria, um app por vez).
export XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=LUMNA XDG_SESSION_DESKTOP=lumna
export ORBIT_BORDA=0 ORBIT_PHONE_SCALE=$ESCALA ORBIT_TERMINAL=foot
mkdir -p "\$HOME/.local/state/lumna"
exec dbus-run-session -- orbit -s 'dbus-update-activation-environment WAYLAND_DISPLAY XDG_CURRENT_DESKTOP XDG_SESSION_TYPE XDG_SESSION_DESKTOP
	lumna-papel & lumna-barra & lumna-lancador & lumna-teclado & foot &' >"\$HOME/orbit-celular.log" 2>&1
EOF
chmod 755 "$H/.config/lumna/orbit-celular.sh"
chown "$U:$U" "$H/.config/lumna/orbit-celular.sh"

mkdir -p "$(dirname "$OVR")"
cat > "$OVR" <<EOF
[Service]
ExecStart=
ExecStart=-/sbin/agetty -o '-p -f -- \\\\u' --noclear --autologin $U %I \$TERM
EOF

# só uma tentativa por boot; a mesma marca do primeira-tela.sh (cada um troca o arranque do outro)
touch "$H/.bash_profile"
sed -i '/# lumna-teste/d' "$H/.bash_profile"
cat >> "$H/.bash_profile" <<'EOF'
if [ "$(tty)" = /dev/tty1 ] && [ ! -e /tmp/lumna-orbit-tentou ]; then touch /tmp/lumna-orbit-tentou; sh ~/.config/lumna/orbit-celular.sh; fi # lumna-teste
EOF
chown "$U:$U" "$H/.bash_profile"

systemctl daemon-reload
rm -f /tmp/lumna-orbit-tentou
systemctl restart getty@tty1
cat <<EOF
ok: olhe a tela do celular (escala $ESCALA).
  o Orbit:   tail -f $H/orbit-celular.log
  diagnóstico (pelo ssh, com o Orbit rodando):  sudo -u $U sh -c 'orbit-msg -s \$(ls /run/user/\$(id -u $U)/orbit.*.sock | head -n1) saidas'
  se algo falhar, mande: o orbit-celular.log, journalctl -b -p warning --no-pager | tail -60, e a foto da tela.
EOF
