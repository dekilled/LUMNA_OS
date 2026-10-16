#!/bin/sh
# Testa kernel + initramfs no QEMU (máquina virt), sem celular. Ctrl+A X para sair.
# Uso: ./test-qemu.sh oneplus-fajita [rootfs.img] [args extras da cmdline]
# Sem hardware do celular: a rede USB falha (normal) e sem rootfs ele cai no modo debug.
# O rootfs abre em -snapshot: nada do que acontece no teste é gravado na imagem.
. "$(dirname "$0")/common.sh"
need qemu-system-aarch64
img=${2:-}
[ $# -ge 2 ] && shift 2 || shift
[ -f "$OUT/initramfs.cpio.gz" ] || die "rode build-initramfs.sh $DEVICE primeiro"
exec qemu-system-aarch64 -M virt -cpu cortex-a57 -m 2048 -nographic \
    -kernel "$CACHE/linux-$KERNEL_TAG/arch/arm64/boot/Image" \
    -initrd "$OUT/initramfs.cpio.gz" \
    -append "console=ttyAMA0 $*" \
    ${img:+-snapshot -drive file="$img",format=raw,if=virtio}
