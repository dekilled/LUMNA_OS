#!/bin/sh
# Compila o lumna-shell no próprio celular e liga o LUMNA na tela.
# Rode NO CELULAR, como alarm (sem sudo), dentro do clone do LUMNA_OS, com internet pela USB:
#   sh mobile/session/lumna-mobile.sh
# A compilação leva uns 30-60 min na primeira vez (Rust + WebKit no Snapdragon 845).
# Mais leve: compile no PC (mobile/scripts/build-pc.sh), traga o pacote e rode
#   LUMNA_PACOTE=lumna-shell-0.0.1-1-aarch64.pkg.tar.zst sh mobile/session/lumna-mobile.sh
# Só reaplicar as configurações, sem mexer no lumna-shell instalado: LUMNA_PACOTE=manter
# (o mobile/scripts/atualizar.sh, no PC, faz tudo isso sozinho)
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
    hyprland wireplumber mesa foot ttf-dejavu inter-font noto-fonts-emoji wtype wl-clipboard grim ffmpeg bluez bluez-utils hypridle \
    wf-recorder gst-plugins-good gst-libav mpv playerctl git

# O perfil release do LUMNA usa LTO com 1 codegen unit: o último passo (o próprio lumna-shell) roda
# num núcleo só e leva uns 30 min no 6T. Pra testar mudanças: LUMNA_RAPIDO=1 sh lumna-mobile.sh
# (binário um pouco maior e mais lento, mas o último passo cai pra poucos minutos).
if [ -n "${LUMNA_RAPIDO:-}" ]; then
    export CARGO_PROFILE_RELEASE_LTO=false CARGO_PROFILE_RELEASE_CODEGEN_UNITS=16
fi

if [ "${LUMNA_PACOTE:-}" = manter ]; then
    pacman -Q lumna-shell >/dev/null || { echo "o lumna-shell não está instalado: rode sem LUMNA_PACOTE=manter"; exit 1; }
elif [ -n "${LUMNA_PACOTE:-}" ]; then
    # compilado no PC: só instala
    sudo pacman -U --noconfirm "$(realpath "$LUMNA_PACOTE")"
else
    # -f refaz o pacote, --nocheck pula os testes (lentos aqui), -i instala
    cd "$ROOT/packages/lumna-shell"
    makepkg -sfi --nocheck --noconfirm
fi

# desligar/reiniciar sem esperar serviço travado ("A stop job is running..."): 10 s no máximo
sudo install -Dm644 "$ROOT/mobile/session/50-lumna-stop.conf" /etc/systemd/system.conf.d/50-lumna-stop.conf
sudo install -Dm644 "$ROOT/mobile/session/50-lumna-stop.conf" /etc/systemd/user.conf.d/50-lumna-stop.conf

# Wi-Fi sem economia de energia: o firmware do WCN3990 caía e levava o aparelho pro crashdump
sudo install -Dm644 "$ROOT/mobile/session/50-lumna-wifi.conf" /etc/NetworkManager/conf.d/50-lumna-wifi.conf
sudo systemctl reload NetworkManager || true

# o rmtfs (EFS do modem) travava o desligar: o modem é desligado antes dele parar
sudo install -Dm755 "$ROOT/mobile/session/rmtfs/modem-stop.sh" /usr/local/lib/lumna/modem-stop.sh
sudo install -Dm644 "$ROOT/mobile/session/rmtfs/50-lumna.conf" /etc/systemd/system/rmtfs.service.d/50-lumna.conf
sudo systemctl daemon-reload

# a hora certa já no boot, sem internet (o RTC do Snapdragon não guarda a hora: ligava em 1970)
sudo install -Dm755 "$ROOT/mobile/session/relogio/relogio.sh" /usr/local/lib/lumna/relogio.sh
for u in lumna-relogio.service lumna-relogio-guarda.service lumna-relogio-guarda.timer lumna-relogio-guarda.path; do
    sudo install -Dm644 "$ROOT/mobile/session/relogio/$u" "/etc/systemd/system/$u"
done
sudo systemctl daemon-reload
sudo systemctl enable lumna-relogio.service lumna-relogio-guarda.timer lumna-relogio-guarda.path || true
sudo systemctl start lumna-relogio-guarda.timer lumna-relogio-guarda.path || true
sudo /usr/local/lib/lumna/relogio.sh guarda || true

# arquivos pelo cabo USB (MTP): o uMTP-Responder (versão fixa) e o serviço que o shell liga quando
# a pessoa permite no aviso do cabo
UMTP_REF=b174b03a3c2641a578b9299e540994a35b2f03a9
if [ "$(cat /usr/local/share/lumna/umtprd.ref 2>/dev/null)" != "$UMTP_REF" ]; then
    echo "--- arquivos pelo cabo: baixando e compilando o uMTP-Responder (só na primeira vez, ~1 min)"
    tmp=$(mktemp -d)
    # sem internet (ou lenta demais) não fica esperando: segue sem, e tenta de novo na próxima
    timeout 180 git clone --progress https://github.com/viveris/uMTP-Responder "$tmp/umtp" &&
        git -C "$tmp/umtp" checkout -q "$UMTP_REF" &&
        timeout 300 make -C "$tmp/umtp" &&
        sudo install -Dm755 "$tmp/umtp/umtprd" /usr/local/bin/umtprd &&
        echo "$UMTP_REF" | sudo install -Dm644 /dev/stdin /usr/local/share/lumna/umtprd.ref ||
        echo "aviso: não deu pra baixar/compilar o umtprd (sem internet?). Arquivos pelo cabo fica pra próxima atualização."
    rm -rf "$tmp"
fi
sudo install -Dm755 "$ROOT/mobile/session/usb/usb-arquivos.sh" /usr/local/lib/lumna/usb-arquivos.sh
sudo install -Dm644 "$ROOT/mobile/session/usb/lumna-usb-arquivos.service" /etc/systemd/system/lumna-usb-arquivos.service
sudo install -Dm644 "$ROOT/mobile/session/usb/50-lumna-usb-arquivos.rules" /etc/polkit-1/rules.d/50-lumna-usb-arquivos.rules
serial=$(head -c 16 /etc/machine-id 2>/dev/null || echo LUMNA0001)
sed -e "s|@HOME@|$HOME|" -e "s|@UID@|$(id -u)|" -e "s|@GID@|$(id -g)|" -e "s|@SERIAL@|$serial|" \
    "$ROOT/mobile/session/usb/umtprd.conf.in" | sudo install -Dm644 /dev/stdin /etc/umtprd/umtprd.conf
sudo systemctl daemon-reload

# o bootloader A/B precisa ouvir "liguei direitinho" a cada boot (senão desiste do slot)
[ -x /usr/local/bin/qbootctl ] || sh "$ROOT/mobile/session/boot-ok.sh"

# Bluetooth (WCN3990 pelo hci_uart; o firmware já vem no rootfs)
sudo systemctl enable --now bluetooth.service || true
# os botões do fone Bluetooth (AVRCP) viram comandos de mídia (MPRIS): pausar, pular
systemctl --user enable --now mpris-proxy.service 2>/dev/null || true

# Telefone, Contatos, Controle e Emma na grade de apps (o lumna-ctl abre as janelas do shell)
sudo install -Dm644 "$ROOT/mobile/session/apps/phone.svg" /usr/share/lumna/icons/phone.svg
sudo install -Dm644 "$ROOT/mobile/session/apps/contacts.svg" /usr/share/lumna/icons/contacts.svg
sudo install -Dm644 "$ROOT/mobile/session/apps/remote.svg" /usr/share/lumna/icons/remote.svg
sudo install -Dm644 "$ROOT/mobile/session/apps/emma.svg" /usr/share/lumna/icons/emma.svg
mkdir -p "$HOME/.local/share/applications"
cp "$ROOT"/mobile/session/apps/*.desktop "$HOME/.local/share/applications/"

# o teclado vibra a cada toque: libera o motor de vibração pra quem está na tela
sudo install -Dm644 "$ROOT/mobile/session/70-lumna-vibra.rules" /etc/udev/rules.d/70-lumna-vibra.rules
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=input --action=change

# o botão power é do LUMNA: o logind para de desligar/reiniciar com ele (recarrega sem derrubar a sessão)
sudo install -Dm644 "$ROOT/mobile/session/50-lumna-power.conf" /etc/systemd/logind.conf.d/50-lumna-power.conf
sudo systemctl kill -s HUP systemd-logind

mkdir -p "$HOME/.config/lumna"
cp "$ROOT/mobile/session/hyprland-mobile.lua" "$HOME/.config/lumna/hyprland-mobile.lua"
# a primeira-tela.sh já liga o login automático; aqui só troca a config do teste pela do LUMNA
sed -i 's|hyprland-teste.lua|hyprland-mobile.lua|' "$HOME/.bash_profile"
grep -q hyprland-mobile.lua "$HOME/.bash_profile" || { echo "rode antes: sudo sh mobile/session/primeira-tela.sh"; exit 1; }

sudo rm -f /tmp/lumna-hypr-tentou
sudo systemctl restart getty@tty1
echo "ok: olhe a tela. Logs: tail -f ~/hyprland.log e journalctl -f -t lumna-shell"
