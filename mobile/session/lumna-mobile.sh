#!/bin/sh
# Compila o lumna-shell no próprio celular e liga o LUMNA na tela.
# Rode NO CELULAR, como alarm (sem sudo), dentro do clone do LUMNA_OS, com internet pela USB:
#   sh mobile/session/lumna-mobile.sh
# A compilação leva uns 30-60 min na primeira vez (Rust + WebKit no Snapdragon 845).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ "$(id -u)" != 0 ] || { echo "rode como alarm, sem sudo (o makepkg não roda como root)"; exit 1; }

# O rootfs nasce com 6 GB; compilar Rust + WebKit precisa de uns 10 GB livres. O ext4 cresce
# ligado, até ocupar a userdata inteira.
livre=$(df -Pk / | awk 'NR == 2 { print int($4 / 1048576) }')
if [ "$livre" -lt 15 ]; then
    echo "só ${livre} GB livres: aumentando o rootfs para a partição inteira"
    sudo resize2fs "$(findmnt -no SOURCE /)"
    df -h /
fi

sudo pacman -S --needed --noconfirm base-devel rust nodejs pnpm webkit2gtk-4.1 gtk3 gtk-layer-shell \
    hyprland wireplumber mesa foot ttf-dejavu

# O perfil release do LUMNA usa LTO com 1 codegen unit: o último passo (o próprio lumna-shell) roda
# num núcleo só e leva uns 30 min no 6T. Pra testar mudanças: LUMNA_RAPIDO=1 sh lumna-mobile.sh
# (binário um pouco maior e mais lento, mas o último passo cai pra poucos minutos).
if [ -n "${LUMNA_RAPIDO:-}" ]; then
    export CARGO_PROFILE_RELEASE_LTO=false CARGO_PROFILE_RELEASE_CODEGEN_UNITS=16
fi

# -f refaz o pacote, --nocheck pula os testes (lentos aqui), -i instala
cd "$ROOT/packages/lumna-shell"
makepkg -sfi --nocheck --noconfirm

mkdir -p "$HOME/.config/lumna"
cp "$ROOT/mobile/session/hyprland-mobile.lua" "$HOME/.config/lumna/hyprland-mobile.lua"
# a primeira-tela.sh já liga o login automático; aqui só troca a config do teste pela do LUMNA
sed -i 's|hyprland-teste.lua|hyprland-mobile.lua|' "$HOME/.bash_profile"
grep -q hyprland-mobile.lua "$HOME/.bash_profile" || { echo "rode antes: sudo sh mobile/session/primeira-tela.sh"; exit 1; }

sudo rm -f /tmp/lumna-hypr-tentou
sudo systemctl restart getty@tty1
echo "ok: olhe a tela. Logs: tail -f ~/hyprland.log e journalctl -f -t lumna-shell"
