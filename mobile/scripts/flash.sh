#!/bin/sh
# Coloca o LUMNA OS no aparelho (em modo fastboot)
#   ./flash.sh oneplus-fajita test      boot.img vai só pra RAM, nada é gravado
#   ./flash.sh oneplus-fajita slot      grava o boot.img no slot inativo e liga nele;
#                                       o Android fica intacto no slot atual
#   ./flash.sh oneplus-fajita voltar    volta pro slot do Android
#   ./flash.sh oneplus-fajita install   grava boot e rootfs (APAGA O ANDROID)
. "$(dirname "$0")/common.sh"
need fastboot
mode=${2:-test}
[ -f "$OUT/boot.img" ] || die "rode build-boot.sh $DEVICE primeiro"
case $mode in
    test)
        fastboot boot "$OUT/boot.img" ||
            die "o bootloader recusou o boot pela RAM (comum no OnePlus 6/6T). Use: $0 $DEVICE slot" ;;
    slot)
        [ -n "${BOOT_SLOTS:-}" ] || die "$DEVICE_NAME não tem slots A/B"
        atual=$(fastboot getvar current-slot 2>&1 | awk -F': *' '/current-slot/{print $2}')
        case $atual in a) outro=b ;; b) outro=a ;; *) die "não li o slot atual (fastboot getvar current-slot)" ;; esac
        echo "Android está no slot $atual. A LUMNA vai pro slot $outro (boot_$outro e dtbo_$outro são sobrescritos)."
        echo "O Android do slot $atual e os seus dados não são tocados. Fez o backup? Digite SIM:"
        read -r ok
        [ "$ok" = SIM ] || die "cancelado"
        echo "$atual" > "$OUT/slot-android"
        fastboot flash "boot_$outro" "$OUT/boot.img"
        # o dtbo da OnePlus é pro kernel do Android e atrapalha o dtb do mainline
        fastboot erase "dtbo_$outro"
        fastboot set_active "$outro"
        fastboot reboot
        echo "Pra voltar ao Android: no fastboot (sudo systemctl reboot bootloader no Arch), rode $0 $DEVICE voltar" ;;
    voltar)
        [ -f "$OUT/slot-android" ] || die "não sei qual era o slot do Android; use fastboot set_active a (ou b)"
        fastboot set_active "$(cat "$OUT/slot-android")"
        fastboot reboot ;;
    install)
        [ -n "${ROOTFS_PARTITION:-}" ] || die "$DEVICE_NAME ainda não tem instalação (só o modo test)"
        [ -f "$OUT/rootfs.img" ] || die "rode build-rootfs.sh $DEVICE primeiro"
        echo "Isto APAGA o Android e todos os dados de $DEVICE_NAME."
        echo "Fez o backup (scripts/backup.sh)? Digite APAGAR para continuar:"
        read -r ok
        [ "$ok" = APAGAR ] || die "cancelado"
        fastboot flash boot "$OUT/boot.img"
        fastboot flash "$ROOTFS_PARTITION" "$OUT/rootfs.img"
        fastboot reboot ;;
    *) die "modo desconhecido: $mode (use test, slot, voltar ou install)" ;;
esac
