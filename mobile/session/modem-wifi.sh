#!/bin/sh
# Liga o modem e o Wi-Fi do OnePlus 6T (SDM845, kernel mainline).
# Rode NO CELULAR, como alarm (sem sudo), com internet pela USB:
#   sh mobile/session/modem-wifi.sh
#
# Como as peças se encaixam (é a mesma pilha do postmarketOS):
#   rmtfs      entrega ao modem as partições modemst1/modemst2/fsg (IMEI, calibração do rádio)
#   pd-mapper  diz aos processadores da Qualcomm (modem, DSP de áudio) quais serviços existem
#   tqftpserv  serve arquivos de firmware ao modem, inclusive o do Wi-Fi (wlanmdsp.mbn)
#   qrtr       a "rede" interna entre o Linux e o modem
# O Wi-Fi (WCN3990) só sobe com o modem rodando. ModemManager cuida de SIM, dados e chamadas.
set -eu

if [ "${1:-}" = status ]; then
    echo "== processadores (modem e DSPs tem que estar 'running')"
    for r in /sys/class/remoteproc/remoteproc*; do printf '%s: %s %s\n' "$(cat $r/name)" "$(cat $r/state)" "$(cat $r/firmware 2>/dev/null)"; done
    echo "== serviços"
    systemctl is-active rmtfs pd-mapper tqftpserv ModemManager NetworkManager | paste -sd' '
    echo "== Wi-Fi"
    nmcli -t device | grep -i wifi || echo "sem wlan0 ainda"
    echo "== modem (SIM, operadora)"
    mmcli -L && mmcli -m any 2>/dev/null | grep -E 'state|operator name|signal quality|access tech|own' || true
    echo "== últimos avisos do kernel sobre modem e Wi-Fi"
    sudo dmesg | grep -iE 'remoteproc|ath10k|qcom_q6v5|wlan|rmtfs|ipa' | tail -15
    exit 0
fi

[ "$(id -u)" != 0 ] || { echo "rode como alarm, sem sudo (o makepkg não roda como root)"; exit 1; }
sudo -v

sudo pacman -S --needed --noconfirm base-devel git meson modemmanager libqmi libqrtr-glib \
    networkmanager wireless-regdb iw

# pacotes do AUR, nesta ordem (rmtfs e pd-mapper dependem do qrtr)
work=$(mktemp -d)
for p in qrtr rmtfs pd-mapper tqftpserv; do
    if pacman -Q "$p" >/dev/null 2>&1; then echo "-> $p já instalado"; continue; fi
    git clone -q "https://aur.archlinux.org/$p.git" "$work/$p"
    (cd "$work/$p" && makepkg -si --noconfirm)
done
rm -rf "$work"

# Firmware que o postmarketOS guarda em /usr/lib/firmware/postmarketos: a calibração do Wi-Fi e do
# Bluetooth do OnePlus 6/6T. Vai por cima da genérica do linux-firmware.
fw=/usr/lib/firmware
if [ -d $fw/postmarketos ]; then
    sudo install -Dm644 $fw/postmarketos/ath10k/WCN3990/hw1.0/board-2.bin $fw/ath10k/WCN3990/hw1.0/board-2.bin
    sudo install -Dm644 $fw/postmarketos/qca/crbtfw21.tlv $fw/qca/crbtfw21.tlv
fi

# rmtfs lendo as partições pelo nome (-P), em modo real (-r), e avisando o systemd quando pronto (-s)
sudo mkdir -p /etc/systemd/system/rmtfs.service.d
printf '[Service]\nExecStart=\nExecStart=/usr/bin/rmtfs -r -P -s\nRestart=always\n' |
    sudo tee /etc/systemd/system/rmtfs.service.d/lumna.conf >/dev/null

sudo systemctl daemon-reload
for s in rmtfs pd-mapper tqftpserv ModemManager NetworkManager; do
    sudo systemctl enable "$s" 2>/dev/null || echo "aviso: não achei o serviço $s"
done
echo
echo "ok: reinicie o celular (sudo reboot) e depois rode:"
echo "  sh mobile/session/modem-wifi.sh status"
