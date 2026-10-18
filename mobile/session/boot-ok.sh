#!/bin/sh
# O bootloader da OnePlus (A/B) conta as tentativas de boot de cada slot e espera o sistema dizer
# "liguei direitinho". O Android diz; o Linux mainline não diz nada, então cada reinício gasta uma
# tentativa e, quando acabam, o slot vira "não inicializável" e o celular não liga mais o LUMNA
# (foi o "bootloop" de 18/10/2026: slot-retry-count:b:0, slot-unbootable:b:yes).
#
# Este script compila o qbootctl (linux-msm, o mesmo que o postmarketOS usa) e liga um serviço que
# marca o slot atual como bom a cada boot. Rode NO CELULAR, como alarm, uma vez:
#   sh mobile/session/boot-ok.sh
# Conferir depois: sudo qbootctl   (o slot atual deve aparecer como "successful")
set -eu
[ "$(id -u)" != 0 ] || { echo "rode como alarm, sem sudo"; exit 1; }
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

sudo pacman -S --needed --noconfirm git meson ninja gcc zlib pkgconf

src=$(mktemp -d)
trap 'rm -rf "$src"' EXIT
git clone -q --depth 1 https://github.com/linux-msm/qbootctl "$src"
meson setup -q "$src/build" "$src" --prefix=/usr/local
meson compile -C "$src/build"
sudo install -Dm755 "$src/build/qbootctl" /usr/local/bin/qbootctl

sudo install -Dm644 "$ROOT/mobile/session/lumna-boot-ok.service" /etc/systemd/system/lumna-boot-ok.service
sudo systemctl daemon-reload
sudo systemctl enable lumna-boot-ok.service
# e já marca este boot
sudo /usr/local/bin/qbootctl -m
sudo /usr/local/bin/qbootctl
echo "ok: a cada boot o slot atual é marcado como bom (lumna-boot-ok.service)"
