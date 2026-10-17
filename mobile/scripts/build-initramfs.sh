#!/bin/sh
# Monta o initramfs: busybox estático + módulos + firmware da GPU + init do LUMNA
# Uso: ./build-initramfs.sh oneplus-fajita   (rode build-kernel.sh antes)
. "$(dirname "$0")/common.sh"
need curl git clang ld.lld tar ar xz zstd cpio gzip find awk sed
[ -f "$OUT/kernel.release" ] || die "rode build-kernel.sh $DEVICE primeiro"
KREL=$(cat "$OUT/kernel.release")
KMOD="$OUT/modules/lib/modules/$KREL"
[ -f "$KMOD/modules.dep" ] || die "sem modules.dep: instale o depmod (kmod) e rode build-kernel.sh de novo"

# busybox estático aarch64 do Debian estável
DEB_MIRROR=https://deb.debian.org/debian
pkgs=$(fetch "$DEB_MIRROR/dists/bookworm/main/binary-arm64/Packages.xz" debian-bookworm-arm64-Packages.xz)
pool=$(xz -dc "$pkgs" | awk '/^Package: busybox-static$/{p=1} p&&/^Filename:/{print $2; exit}')
[ -n "$pool" ] || die "não achei busybox-static no Debian"
deb=$(fetch "$DEB_MIRROR/$pool" "$(basename "$pool")")

IRD="$OUT/initramfs"
rm -rf "$IRD"
mkdir -p "$IRD"/bin "$IRD"/sbin "$IRD"/etc "$IRD"/proc "$IRD"/sys "$IRD"/dev "$IRD"/sysroot \
    "$IRD"/lib/modules/"$KREL" "$IRD"/lib/firmware

tmp=$(mktemp -d)
(cd "$tmp" && ar x "$deb" && tar -xf data.tar.* ./bin/busybox)
cp "$tmp/bin/busybox" "$IRD/bin/busybox"
rm -rf "$tmp"
for a in sh cat echo ls mkdir mount umount ln sleep seq head tee modprobe insmod ifconfig \
    udhcpd telnetd findfs blkid switch_root dmesg ps kill vi grep sed df free reboot poweroff; do
    ln -sf busybox "$IRD/bin/$a"
done

# módulos pedidos + dependências (via modules.dep)
mods=
for m in $INITFS_MODULES; do
    f=$(find "$KMOD/kernel" -name "$m.ko*" -o -name "$(echo "$m" | tr _ -).ko*" | head -n1)
    [ -n "$f" ] || { echo "aviso: módulo $m não existe (embutido no kernel?)"; continue; }
    rel=${f#"$KMOD"/}
    mods="$mods $rel $(awk -F': *' -v k="$rel" '$1==k{print $2}' "$KMOD/modules.dep")"
done
# o modprobe do busybox não lê .ko.zst: descomprime aqui e ajusta o modules.dep
for rel in $mods; do
    dst="$IRD/lib/modules/$KREL/${rel%.zst}"
    mkdir -p "$(dirname "$dst")"
    case $rel in *.zst) zstd -qdf "$KMOD/$rel" -o "$dst" ;; *) cp "$KMOD/$rel" "$dst" ;; esac
done
sed 's/\.ko\.zst/.ko/g' "$KMOD/modules.dep" > "$IRD/lib/modules/$KREL/modules.dep"
cp "$KMOD"/modules.builtin "$KMOD"/modules.order "$IRD/lib/modules/$KREL/" 2>/dev/null || true
echo "$INITFS_MODULES" | tr ' ' '\n' | grep . > "$IRD/etc/modules" || true

# firmware da GPU: blobs do aparelho, e o resto do linux-firmware
fwtar=
if [ -n "$INITFS_FIRMWARE" ]; then
    fwtar=$(fetch_git "$FIRMWARE_URL" "firmware-$DEVICE.tar.gz" "$FIRMWARE_GIT" "$FIRMWARE_REF")
fi
for fw in $INITFS_FIRMWARE; do
    mkdir -p "$IRD/lib/firmware/$(dirname "$fw")"
    tar -xzf "$fwtar" -O --wildcards "*/lib/firmware/$fw" > "$IRD/lib/firmware/$fw" 2>/dev/null ||
        curl -fsSL -o "$IRD/lib/firmware/$fw" \
            "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/$fw" ||
        die "firmware $fw não encontrado"
done

# reboot com modo (lumna-reboot bootloader = fastboot), sem libc
clang --target=aarch64-linux-gnu -Os -static -nostdlib -ffreestanding -fno-stack-protector \
    -fuse-ld=lld -Wl,--build-id=none -s -o "$IRD/bin/lumna-reboot" "$OS_DIR/initramfs/lumna-reboot.c"
install -m755 "$OS_DIR/initramfs/init" "$IRD/init"
cp "$OS_DIR/initramfs/udhcpd.conf" "$IRD/etc/"
(cd "$IRD" && find . | cpio -o -H newc --owner=0:0 --quiet | gzip -9) > "$OUT/initramfs.cpio.gz"
echo "ok: $OUT/initramfs.cpio.gz ($(du -h "$OUT/initramfs.cpio.gz" | cut -f1))"
