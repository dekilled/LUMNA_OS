#!/bin/sh
# Arquivos pelo cabo USB (MTP), como no Android: o PC vê o armazenamento do celular no
# gerenciador de arquivos. Roda como root pelo lumna-usb-arquivos.service (o shell liga e
# desliga ele quando a pessoa permite, no aviso do cabo).
#
# O gadget USB "lumna" já existe desde o initramfs, com a rede USB (NCM, o ssh pelo cabo). Aqui
# entra a função MTP (FunctionFS) do lado dela: soltar o controlador, criar a função, o umtprd
# escreve os descritores, ligar a função na configuração e prender o controlador de novo.
#   usb-arquivos.sh preparar   antes do umtprd
#   usb-arquivos.sh ligar      depois que o umtprd abriu
#   usb-arquivos.sh desligar   tira o MTP (a rede USB continua)
set -u
G=/sys/kernel/config/usb_gadget/lumna
FFS=/dev/ffs-mtp

udc() { ls /sys/class/udc 2>/dev/null | head -n1; }

case "${1:-}" in
preparar)
    [ -d "$G" ] || { echo "sem o gadget USB do LUMNA ($G)" >&2; exit 1; }
    modprobe usb_f_fs 2>/dev/null || true
    echo "" >"$G/UDC" 2>/dev/null || true
    mkdir -p "$G/functions/ffs.mtp" "$FFS"
    mountpoint -q "$FFS" || mount -t functionfs mtp "$FFS"
    ;;
ligar)
    # o umtprd escreve os descritores no ep0 e aí os outros endpoints aparecem
    i=0
    while [ ! -e "$FFS/ep1" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
    [ -e "$FFS/ep1" ] || { echo "o umtprd não abriu a função MTP" >&2; exit 1; }
    [ -e "$G/configs/c.1/ffs.mtp" ] || ln -s "$G/functions/ffs.mtp" "$G/configs/c.1/"
    udc >"$G/UDC"
    ;;
desligar)
    echo "" >"$G/UDC" 2>/dev/null || true
    rm -f "$G/configs/c.1/ffs.mtp"
    ! mountpoint -q "$FFS" || umount "$FFS"
    rmdir "$G/functions/ffs.mtp" 2>/dev/null || true
    udc >"$G/UDC" 2>/dev/null || true
    ;;
*)
    echo "uso: $0 preparar|ligar|desligar" >&2
    exit 2
    ;;
esac
