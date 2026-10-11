#!/usr/bin/env bash
# Monta o ISO do LUMNA OS. Roda como root dentro do Arch (o container de iso/builder).
#   1. compila os pacotes lumna-* e cria um repositório local
#   2. monta o perfil do archiso: releng + overlay do LUMNA
#   3. roda o mkarchiso
set -euo pipefail

SRC="$(realpath "$(dirname "$0")/..")"
OUT="${OUT:-$SRC/out}"
WORK="${WORK:-/tmp/lumna-iso}"
BUILDER="${BUILDER:-builder}"

rm -rf "$WORK" "$OUT/repo"
mkdir -p "$WORK" "$OUT/repo"

echo ">> 1/3 pacotes"
# makepkg não roda como root: compila numa cópia do repositório do usuário de build.
tree="$WORK/src"
mkdir -p "$tree"
tar -C "$SRC" --exclude=./target --exclude=./out --exclude=./work --exclude=node_modules --exclude=./shell/dist -cf - . | tar -C "$tree" -xf -
chown -R "$BUILDER:$BUILDER" "$WORK"
build_pkg() {
  local name="$1"; shift
  (cd "$tree/packages/$name" && sudo -u "$BUILDER" --preserve-env=CARGO_TARGET_DIR,CARGO_HOME,NODE_EXTRA_CA_CERTS,PKGDEST \
     env PKGDEST="$OUT/repo" makepkg --noconfirm --cleanbuild "$@")
}
chown "$BUILDER:$BUILDER" "$OUT/repo"
build_pkg lumna-shell --syncdeps
build_pkg lumna-session --nodeps
repo-add "$OUT/repo/lumna.db.tar.gz" "$OUT"/repo/*.pkg.tar.zst

# A configuração do Hyprland é validada pelo próprio Hyprland (já instalado pelo makepkg).
sudo -u "$BUILDER" bash -c 'export XDG_RUNTIME_DIR=$(mktemp -d); Hyprland --verify-config --config "$1"' _ "$tree/session/hyprland.lua" \
  | tee "$WORK/verify-config.log"
grep -qx "config ok" "$WORK/verify-config.log"

echo ">> 2/3 perfil"
profile="$WORK/profile"
cp -r /usr/share/archiso/configs/releng "$profile"
cp -a "$SRC/iso/overlay/airootfs/." "$profile/airootfs/"
# O greetd assume o tty1 no lugar do login automático do root.
rm -rf "$profile/airootfs/etc/systemd/system/getty@tty1.service.d"
grep -v '^#' "$SRC/iso/packages.x86_64" | sed '/^$/d' >> "$profile/packages.x86_64"
cat >> "$profile/pacman.conf" <<CONF

[lumna]
SigLevel = Optional TrustAll
Server = file://$OUT/repo
CONF
sed -i \
  -e 's|^iso_name=.*|iso_name="lumna-os"|' \
  -e 's|^iso_label=.*|iso_label="LUMNA_$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y%m)"|' \
  -e 's|^iso_publisher=.*|iso_publisher="LUMNA OS <https://github.com/dekilled/LUMNA_OS>"|' \
  -e 's|^iso_application=.*|iso_application="LUMNA OS Live"|' \
  "$profile/profiledef.sh"
sed -i 's|^)$|  ["/etc/sudoers.d"]="0:0:750"\n  ["/etc/sudoers.d/lumna-live"]="0:0:440"\n)|' "$profile/profiledef.sh"
sed -i -e 's/Arch Linux install medium/LUMNA OS/g' -e 's/Arch Linux/LUMNA OS/g' \
  "$profile"/efiboot/loader/entries/*.conf "$profile"/syslinux/*.cfg "$profile"/grub/*.cfg

echo ">> 3/3 mkarchiso"
mkarchiso -v -w "$WORK/mkarchiso" -o "$OUT" "$profile"
ls -lh "$OUT"/*.iso
