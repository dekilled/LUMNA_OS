#!/bin/sh
# Áudio do OnePlus 6T: alto-falante, fone e o som das ligações.
# Rode NO CELULAR, como alarm (sem sudo), dentro do clone do LUMNA_OS, com internet:
#   sh mobile/session/audio.sh           # instala e liga tudo
#   sh mobile/session/audio.sh status    # o que está rodando (manda pra Claudia se algo falhar)
#   sh mobile/session/audio.sh teste     # um apito no alto-falante
#
# O que entra:
#  - PipeWire + WirePlumber (o som do sistema), com o ajuste da Qualcomm (48 kHz, buffers grandes)
#  - alsa-ucm-conf-sdm845: o UCM do sdm845-mainline, com os perfis HiFi e "Voice Call" do 6T
#  - lumna-voiced: abre o PCM de voz do DSP quando uma ligação fica ativa (no lugar do q6voiced)
#  - alsa-restore/alsa-state desligados (no 6T eles deixam o alto-falante mudo, diz o pmOS)
# Durante a ligação o lumna-shell troca a placa pro perfil "Voice Call" sozinho.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE="$ROOT/mobile/session/audio"
[ "$(id -u)" != 0 ] || { echo "rode como alarm, sem sudo (o makepkg não roda como root)"; exit 1; }

if [ "${1:-}" = status ]; then
    echo "== placas";   cat /proc/asound/cards
    echo "== voz";      grep -i voice /proc/asound/pcm || echo "sem VoiceMMode1 (o módulo q6voice carregou? lsmod | grep q6voice)"
    echo "== perfis";   pactl list cards 2>/dev/null | grep -E "Name:|Active Profile|^\s+(HiFi|Voice Call)" || echo "pactl sem resposta (pipewire-pulse rodando?)"
    echo "== saídas";   wpctl status 2>/dev/null | sed -n '/Audio/,/Video/p' | head -30
    echo "== lumna-voiced"
    systemctl --no-pager status lumna-voiced | head -5 || true
    journalctl -u lumna-voiced -n 15 --no-pager || true
    exit 0
fi

if [ "${1:-}" = teste ]; then
    speaker-test -c 2 -t sine -f 440 -l 1
    exit 0
fi

sudo pacman -S --needed --noconfirm base-devel git pipewire pipewire-pulse pipewire-alsa wireplumber \
    alsa-utils alsa-lib libpulse dbus

# UCM do SDM845 (substitui o alsa-ucm-conf do Arch; o pacman pergunta e a gente responde sim)
build="$HOME/.cache/lumna-build/alsa-ucm-conf-sdm845"
rm -rf "$build"
mkdir -p "$build"
cp "$HERE/ucm/PKGBUILD" "$build/"
(cd "$build" && makepkg -f --noconfirm)
sudo pacman -U --noconfirm --ask=4 "$build"/alsa-ucm-conf-sdm845-*.pkg.tar.*

# no 6T o alsa-restore deixa o alto-falante mudo
sudo systemctl mask alsa-restore.service alsa-state.service 2>/dev/null || true

# PipeWire com os ajustes da Qualcomm
install -Dm644 "$HERE/51-qcom.conf" "$HOME/.config/wireplumber/wireplumber.conf.d/51-qcom.conf"

# daemon da voz
cc -O2 -Wall -o /tmp/lumna-voiced "$HERE/lumna-voiced.c" $(pkg-config --cflags --libs alsa dbus-1)
sudo install -Dm755 /tmp/lumna-voiced /usr/local/bin/lumna-voiced
sudo install -Dm644 "$HERE/lumna-voiced.service" /etc/systemd/system/lumna-voiced.service
sudo systemctl daemon-reload
sudo systemctl enable --now lumna-voiced
sudo systemctl restart lumna-voiced

# o som do usuário (o Arch liga por socket; aqui garante e recarrega o UCM novo)
systemctl --user enable --now pipewire.socket pipewire-pulse.socket wireplumber.service
systemctl --user restart wireplumber pipewire pipewire-pulse

sleep 2
echo
echo "Pronto. Confira com: sh mobile/session/audio.sh status"
echo "Apito no alto-falante: sh mobile/session/audio.sh teste"
