#!/bin/sh
# Compila kernel, dtb e módulos. Saída em mobile/out/<aparelho>/
# Uso: ./build-kernel.sh oneplus-fajita
. "$(dirname "$0")/common.sh"
need curl tar git make clang ld.lld llvm-ar llvm-nm llvm-objcopy llvm-objdump llvm-readelf llvm-strip bison flex bc perl python3 zstd depmod
KERNEL_IMAGE=${KERNEL_IMAGE:-Image.gz}

SRC="$CACHE/linux-$KERNEL_TAG"
if [ ! -d "$SRC" ]; then
    case $DEVICE_METHOD in
        mainline)
            tarball=$(fetch "$KERNEL_URL" "linux-$KERNEL_TAG.tar.gz")
            tar -xzf "$tarball" -C "$CACHE" ;;
        downstream)
            git init -q "$SRC"
            git -C "$SRC" fetch -q --depth 1 "$KERNEL_GIT" "$KERNEL_REF"
            git -C "$SRC" checkout -q FETCH_HEAD ;;
        *) die "método desconhecido: $DEVICE_METHOD" ;;
    esac
fi

# Clang faz cross-compile sozinho, sem precisar de gcc aarch64.
# Kernel downstream da Qualcomm só compila com saída separada (O=), como no Lineage.
KBUILD=$SRC
[ "$DEVICE_METHOD" = downstream ] && KBUILD="$SRC/out"
KMAKE="make -C $SRC O=$KBUILD ARCH=arm64 LLVM=1 -j$(nproc) ${KERNEL_MAKE_ARGS:-}"
if [ -n "${KERNEL_DEFCONFIGS:-}" ]; then
    # fragmentos na ordem do BoardConfig do Lineage, depois o nosso
    cp "$OS_DIR/devices/$DEVICE/$KERNEL_FRAGMENT" "$SRC/arch/arm64/configs/lumna-$DEVICE.config"
    $KMAKE $KERNEL_DEFCONFIGS "lumna-$DEVICE.config"
else
    cp "$(pmaports_file "$KERNEL_CONFIG")" "$SRC/.config"
    # o nosso fragmento vem depois, então ganha do pmOS
    [ -n "${KERNEL_FRAGMENT:-}" ] && cat "$OS_DIR/devices/$DEVICE/$KERNEL_FRAGMENT" >> "$SRC/.config"
    $KMAKE olddefconfig
fi
# Um build antigo sem llvm-nm deixa este cabeçalho vazio, e o make não refaz sozinho
off="$KBUILD/include/generated/vdso-offsets.h"
[ -f "$off" ] && [ ! -s "$off" ] && rm -f "$off" "$KBUILD"/arch/arm64/kernel/vdso/vdso.so*
$KMAKE "$KERNEL_IMAGE" modules dtbs

rm -rf "$OUT/modules"
$KMAKE modules_install INSTALL_MOD_PATH="$OUT/modules" INSTALL_MOD_STRIP=1
rm -f "$OUT"/modules/lib/modules/*/build "$OUT"/modules/lib/modules/*/source
cp "$KBUILD/arch/arm64/boot/$KERNEL_IMAGE" "$OUT/kernel"
# mainline: dtb vai anexado ao Image.gz; downstream: o Image-dtb já traz os dtbs
if [ -n "${KERNEL_DTB:-}" ]; then
    cat "$KBUILD/arch/arm64/boot/dts/$KERNEL_DTB.dtb" >> "$OUT/kernel"
fi
cp "$KBUILD/include/config/kernel.release" "$OUT/kernel.release"
echo "ok: kernel $(cat "$OUT/kernel.release") em $OUT"
