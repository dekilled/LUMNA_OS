#!/bin/sh
# Compartilha a internet do PC com o celular pela rede USB (rode no PC, com sudo).
# Uso: sudo ./usb-internet.sh          liga
#      sudo ./usb-internet.sh desligar desfaz o NAT
# No celular a usb0 já usa 172.16.42.2 como gateway e DNS público (rootfs novo);
# num rootfs antigo: sudo nmcli con mod usb0 ipv4.gateway 172.16.42.2 ipv4.dns 1.1.1.1 && sudo nmcli con up usb0
set -eu
[ "$(id -u)" = 0 ] || { echo "rode com sudo"; exit 1; }

# a interface do celular é a que usa o driver cdc_ncm (nome muda: enp0s20u1, usb0...)
usb=
for i in /sys/class/net/*; do
    case $(basename "$(readlink -f "$i/device/driver" 2>/dev/null)" 2>/dev/null) in
        cdc_ncm) usb=$(basename "$i") ;;
    esac
done
[ -n "$usb" ] || { echo "não achei o celular na USB (ele está ligado no Arch e com o cabo?)"; exit 1; }
# a saída pra internet é a da rota padrão (Wi-Fi ou cabo do PC)
wan=$(ip route show default | awk '{for (i = 1; i < NF; i++) if ($i == "dev") { print $(i + 1); exit }}')
[ -n "$wan" ] || { echo "o PC está sem rota pra internet"; exit 1; }

if [ "${1:-}" = desligar ]; then
    iptables -t nat -D POSTROUTING -s 172.16.42.0/24 -o "$wan" -j MASQUERADE 2>/dev/null || true
    iptables -D FORWARD -i "$usb" -o "$wan" -j ACCEPT 2>/dev/null || true
    iptables -D FORWARD -i "$wan" -o "$usb" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true
    echo "ok: NAT desligado"
    exit 0
fi

command -v iptables >/dev/null || { echo "instale o iptables (Arch: iptables-nft)"; exit 1; }
ip addr show dev "$usb" | grep -q 'inet 172\.16\.42\.' || ip addr add 172.16.42.2/24 dev "$usb"
ip link set "$usb" up
sysctl -qw net.ipv4.ip_forward=1
# -C confere antes, pra não duplicar a regra se rodar duas vezes
iptables -t nat -C POSTROUTING -s 172.16.42.0/24 -o "$wan" -j MASQUERADE 2>/dev/null ||
    iptables -t nat -A POSTROUTING -s 172.16.42.0/24 -o "$wan" -j MASQUERADE
# o Docker e alguns firewalls deixam o FORWARD em DROP: libera só o celular
iptables -C FORWARD -i "$usb" -o "$wan" -j ACCEPT 2>/dev/null ||
    iptables -I FORWARD -i "$usb" -o "$wan" -j ACCEPT
iptables -C FORWARD -i "$wan" -o "$usb" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null ||
    iptables -I FORWARD -i "$wan" -o "$usb" -m state --state RELATED,ESTABLISHED -j ACCEPT
echo "ok: internet do PC ($wan) compartilhada com o celular ($usb)"
