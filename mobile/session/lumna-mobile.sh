#!/bin/sh
# Compila o lumna-shell no próprio celular e liga o LUMNA na tela.
# Rode NO CELULAR, como alarm (sem sudo), dentro do clone do LUMNA_OS, com internet pela USB:
#   sh mobile/session/lumna-mobile.sh
# A compilação leva uns 30-60 min na primeira vez (Rust + WebKit no Snapdragon 845).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ "$(id -u)" != 0 ] || { echo "rode como alarm, sem sudo (o makepkg não roda como root)"; exit 1; }

sudo pacman -S --needed --noconfirm base-devel rust nodejs pnpm webkit2gtk-4.1 gtk3 gtk-layer-shell \
    hyprland wireplumber mesa foot ttf-dejavu

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
