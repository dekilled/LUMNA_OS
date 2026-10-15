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
# Versão dos pacotes pela data do build: cada ISO (e cada publicação do repositório [lumna])
# tem uma versão maior que a anterior, e o pacman sabe que é atualização.
LUMNA_VERSION="${LUMNA_VERSION:-0.1.0.$(date -u +%Y%m%d%H%M)}"
sed -i "s/^pkgver=.*/pkgver=$LUMNA_VERSION/" "$tree"/packages/*/PKGBUILD
echo "   versão $LUMNA_VERSION"
build_pkg() {
  local name="$1"; shift
  (cd "$tree/packages/$name" && sudo -u "$BUILDER" --preserve-env=CARGO_TARGET_DIR,CARGO_HOME,NODE_EXTRA_CA_CERTS,PKGDEST,EMMA_SRC \
     env PKGDEST="$OUT/repo" makepkg --noconfirm --cleanbuild "$@")
}
chown "$BUILDER:$BUILDER" "$OUT/repo"
build_pkg lumna-shell --syncdeps
build_pkg lumna-session --nodeps
# Barra de título das janelas: plugin compilado contra o Hyprland do ISO. Se não compilar (Hyprland
# novo demais para o plugin), o ISO sai sem ele e a topbar continua com os botões da janela.
if build_pkg lumna-hyprbars --syncdeps; then
  with_bars=1
else
  echo "   lumna-hyprbars não compilou: o ISO sai sem a barra de título nas janelas"
  with_bars=0
fi
# A Emma vem de outro repositório (NewHarnessV2, privado): só entra quando o código foi passado.
if [ -n "${EMMA_SRC:-}" ] && [ -f "$EMMA_SRC/backend/server.py" ]; then
  pacman -S --needed --noconfirm python python-pip >/dev/null
  EMMA_SRC="$EMMA_SRC" build_pkg lumna-emma --nodeps
  with_emma=1
else
  echo "   sem EMMA_SRC: o ISO sai sem o backend da Emma (o painel pede um servidor)"
  with_emma=0
fi
repo-add "$OUT/repo/lumna.db.tar.gz" "$OUT"/repo/*.pkg.tar.zst

# A configuração do Hyprland é validada pelo próprio Hyprland (já instalado pelo makepkg).
for config in hyprland.lua greeter.lua; do
  sudo -u "$BUILDER" bash -c 'export XDG_RUNTIME_DIR=$(mktemp -d); Hyprland --verify-config --config "$1"' _ "$tree/session/$config" \
    | tee "$WORK/verify-config.log"
  grep -qx "config ok" "$WORK/verify-config.log"
done

echo ">> 2/3 perfil"
profile="$WORK/profile"
cp -r /usr/share/archiso/configs/releng "$profile"
cp -a "$SRC/iso/overlay/airootfs/." "$profile/airootfs/"
# O greetd assume o tty1 no lugar do login automático do root.
rm -rf "$profile/airootfs/etc/systemd/system/getty@tty1.service.d"
# O releng espera o relógio sincronizar pela internet antes de seguir o boot (por causa do
# chaveiro do pacman). Sem rede, ou com NTP bloqueado, o boot fica parado minutos nisso.
# O LUMNA não espera: o relógio acerta sozinho quando a rede aparecer (o timesyncd segue ativo).
rm -f "$profile/airootfs/etc/systemd/system/sysinit.target.wants/systemd-time-wait-sync.service"
grep -v '^#' "$SRC/iso/packages.x86_64" | sed '/^$/d' >> "$profile/packages.x86_64"
[ "$with_emma" = 1 ] && echo lumna-emma >> "$profile/packages.x86_64"
[ "$with_bars" = 1 ] && echo lumna-hyprbars >> "$profile/packages.x86_64"
# Sem NVIDIA: troca o metapacote linux-firmware pelos firmwares separados, menos o da NVIDIA.
sed -i '/^linux-firmware$/d' "$profile/packages.x86_64"
printf '%s\n' linux-firmware-{amd,amdgpu,radeon,intel,atheros,broadcom,cirrus,mediatek,realtek,ti,other} >> "$profile/packages.x86_64"
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
# O que é só do live (login automático, senha fixa, serviços do pendrive do Arch): o instalador
# apaga isso do sistema copiado para o disco.
live_files="$profile/airootfs/usr/share/lumna/live-files"
mkdir -p "$(dirname "$live_files")"
{
  (cd "$SRC/iso/overlay/airootfs" && find . \( -type f -o -type l \) -path '*live*' | sed 's|^\./||')
  (cd "$SRC/iso/overlay/airootfs" && find usr/share/applications -name 'lumna-installer.desktop' 2>/dev/null)
  echo etc/motd
  grep -v "^#" "$SRC/iso/releng-live-files"
} | sort -u > "$live_files"
# Sem as entradas "with speech" (leitor de tela do Arch): elas sobem o espeakup, que disputa o som
# e o console com a sessão, e o LUMNA não abre.
rm -f "$profile"/efiboot/loader/entries/*speech*.conf
sed -i '/^menuentry .*speech/,/^}/d' "$profile"/grub/*.cfg
sed -i '/^LABEL .*speech/,/^$/d' "$profile"/syslinux/*.cfg
sed -i -e 's/Arch Linux install medium/LUMNA OS/g' -e 's/Arch Linux/LUMNA OS/g' \
  "$profile"/efiboot/loader/entries/*.conf "$profile"/syslinux/*.cfg "$profile"/grub/*.cfg

echo ">> 3/3 mkarchiso"
mkarchiso -v -w "$WORK/mkarchiso" -o "$OUT" "$profile"
ls -lh "$OUT"/*.iso
