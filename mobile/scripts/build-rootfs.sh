#!/bin/sh
# Monta o rootfs Arch Linux ARM e gera mobile/out/<aparelho>/rootfs.img (ext4, LABEL=lumna-root)
# Uso (root): ./build-rootfs.sh oneplus-fajita
# Precisa rodar binários aarch64: host ARM64 ou qemu-user-static com binfmt registrado.
. "$(dirname "$0")/common.sh"
need curl bsdtar md5sum mkfs.ext4 chroot mount mountpoint
[ "$(id -u)" = 0 ] || die "rode como root (precisa de chroot e de preservar donos dos arquivos)"
[ -d "$OUT/modules" ] || die "rode build-kernel.sh $DEVICE primeiro"
[ "$(uname -m)" = aarch64 ] || [ -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ] ||
    die "sem suporte a aarch64: instale qemu-user-static + binfmt (Arch: qemu-user-static-binfmt)"

ALARM=http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz
tarball=$(fetch "$ALARM" ArchLinuxARM-aarch64-latest.tar.gz)
md5=$(fetch "$ALARM.md5" ArchLinuxARM-aarch64-latest.tar.gz.md5)
(cd "$CACHE" && md5sum -c "$md5") || die "md5 do Arch Linux ARM não confere (apague $CACHE e baixe de novo)"

ROOT="$OUT/rootfs"
cleanup() {
    for m in "$ROOT/dev" "$ROOT/sys" "$ROOT/proc" "$ROOT"; do
        ! mountpoint -q "$m" || umount -R "$m" || umount -R -l "$m"
    done
}
cleanup
# nunca apagar com /dev ou /sys do host montados aqui dentro
! grep -q " $ROOT[ /]" /proc/mounts || die "ainda há montagens em $ROOT; desmonte antes"
rm -rf "$ROOT"
mkdir -p "$ROOT"
bsdtar -xpf "$tarball" -C "$ROOT"

trap cleanup EXIT
# como o arch-chroot: a raiz precisa ser ponto de montagem (o pacman checa espaço livre por ela)
mount --bind "$ROOT" "$ROOT"
mount -t proc proc "$ROOT/proc"
mount --rbind /sys "$ROOT/sys"
mount --make-rslave "$ROOT/sys"
mount --rbind /dev "$ROOT/dev"
mount --make-rslave "$ROOT/dev"
rm -f "$ROOT/etc/resolv.conf"
cp -L /etc/resolv.conf "$ROOT/etc/resolv.conf"

# Kernel e firmware nossos no lugar do kernel genérico do ALARM
cp -a "$OUT"/modules/lib/modules/. "$ROOT/usr/lib/modules/"
fwtar=$(fetch_git "$FIRMWARE_URL" "firmware-$DEVICE.tar.gz" "$FIRMWARE_GIT" "$FIRMWARE_REF")
mkdir -p "$ROOT/usr/lib/firmware"
bsdtar -xf "$fwtar" -C "$ROOT/usr/lib" --strip-components 2 '*/lib/firmware'
# calibração do Wi-Fi (WCN3990) e do Bluetooth do OnePlus 6/6T, por cima da genérica do linux-firmware
fwdir="$ROOT/usr/lib/firmware"
if [ -d "$fwdir/postmarketos" ]; then
    install -Dm644 "$fwdir/postmarketos/ath10k/WCN3990/hw1.0/board-2.bin" "$fwdir/ath10k/WCN3990/hw1.0/board-2.bin"
    install -Dm644 "$fwdir/postmarketos/qca/crbtfw21.tlv" "$fwdir/qca/crbtfw21.tlv"
    # amplificador do alto-falante (TFA98xx): o driver procura na raiz do firmware
    install -Dm644 "$fwdir/postmarketos/tfa98xx.cnt" "$fwdir/tfa98xx.cnt"
fi

chroot "$ROOT" /bin/bash -eu <<'CHROOT'
pacman-key --init
pacman-key --populate archlinuxarm
# o sandbox do pacman 7 (Landlock + usuário alpm) não funciona sob qemu-user; só no build
P="pacman --disable-sandbox"
$P -Rdd --noconfirm linux-aarch64 linux-firmware || true
$P -Syu --noconfirm --needed \
    base-devel git sudo openssh networkmanager modemmanager bluez bluez-utils \
    flatpak pipewire wireplumber linux-firmware-qcom linux-firmware-atheros
# sobras do kernel genérico e firmwares de PC (AMD, NVIDIA, Intel...)
orphans=$(pacman -Qdtq || true)
[ -z "$orphans" ] || $P -Rns --noconfirm $orphans
systemctl enable sshd NetworkManager
# o ALARM liga o systemd-networkd também; dois gerenciadores de rede brigam
systemctl disable systemd-networkd systemd-networkd.socket systemd-networkd-wait-online
# henchman: o pau-mandado oficial. É só um apelido; o pacman continua lá pro makepkg e pros helpers do AUR
ln -sf /usr/bin/pacman /usr/local/bin/henchman
echo lumna > /etc/hostname
# sem isto os programas reclamam que "C" não é UTF-8
sed -i 's/^#\(pt_BR.UTF-8\|en_US.UTF-8\)/\1/' /etc/locale.gen
locale-gen
echo LANG=pt_BR.UTF-8 > /etc/locale.conf
# horário de Brasília, e a hora certa pela internet (systemd-timesyncd) assim que houver rede
ln -sf /usr/share/zoneinfo/America/Sao_Paulo /etc/localtime
systemctl enable systemd-timesyncd
echo 'LABEL=lumna-root / ext4 defaults,noatime 0 1' > /etc/fstab
echo '%wheel ALL=(ALL:ALL) ALL' > /etc/sudoers.d/wheel
# o grupo wheel controla rede e modem também pelo ssh (o polkit só libera quem está na tela)
mkdir -p /etc/polkit-1/rules.d
cat > /etc/polkit-1/rules.d/50-lumna-rede.rules <<'RULE'
polkit.addRule(function(action, subject) {
    if (subject.isInGroup("wheel") &&
        (action.id.indexOf("org.freedesktop.NetworkManager.") === 0 ||
         action.id.indexOf("org.freedesktop.ModemManager1.") === 0)) {
        return polkit.Result.YES;
    }
});
RULE
# o teclado do LUMNA vibra a cada toque: o motor de vibração fica liberado pra quem está na tela
cat > /etc/udev/rules.d/70-lumna-vibra.rules <<'RULE'
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{capabilities/ff}=="?*", ATTRS{capabilities/ff}!="0", TAG+="uaccess"
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{name}=="pm8941_pwrkey", TAG+="uaccess"
RULE
# o botão power é do LUMNA (tocar bloqueia, segurar abre o menu), não do logind
mkdir -p /etc/systemd/logind.conf.d
printf '[Login]\nHandlePowerKey=ignore\nHandlePowerKeyLongPress=ignore\n' > /etc/systemd/logind.conf.d/50-lumna-power.conf
# Rede USB fixa: o PC conversa com o celular em 172.16.42.1
cat > /etc/NetworkManager/system-connections/usb0.nmconnection <<NM
[connection]
id=usb0
type=ethernet
interface-name=usb0
[ipv4]
method=manual
# o PC (172.16.42.2) repassa a internet com scripts/usb-internet.sh; métrica alta
# pra o Wi-Fi ganhar quando estiver ligado
address1=172.16.42.1/24,172.16.42.2
dns=1.1.1.1;8.8.8.8;
route-metric=700
[ipv6]
method=ignore
NM
chmod 600 /etc/NetworkManager/system-connections/usb0.nmconnection
# No primeiro boot o rootfs (6 GB na imagem) cresce até ocupar a userdata inteira
cat > /etc/systemd/system/lumna-resize-root.service <<'UNIT'
[Unit]
Description=Aumenta o rootfs do LUMNA para a partição inteira
ConditionPathExists=!/var/lib/lumna/rootfs-resized
[Service]
Type=oneshot
ExecStart=/bin/sh -c 'resize2fs "$$(findmnt -no SOURCE /)" && mkdir -p /var/lib/lumna && touch /var/lib/lumna/rootfs-resized'
[Install]
WantedBy=multi-user.target
UNIT
systemctl enable lumna-resize-root
$P -Scc --noconfirm
CHROOT

cleanup
trap - EXIT
rm -f "$ROOT/etc/resolv.conf"
ln -s /run/systemd/resolve/stub-resolv.conf "$ROOT/etc/resolv.conf"

rm -f "$OUT/rootfs.img"
mkfs.ext4 -q -L lumna-root -b "$ROOTFS_BLOCKSIZE" -d "$ROOT" "$OUT/rootfs.img" "$ROOTFS_SIZE"
echo "ok: $OUT/rootfs.img  (login: alarm/alarm, root/root -> TROQUE as senhas)"
