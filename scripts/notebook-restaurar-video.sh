#!/bin/sh
# Roda DENTRO DO LIVE (pendrive), como root (sudo sh notebook-restaurar-video.sh).
# Devolve a imagem ao LUMNA instalado no notebook: tira o splash (Plymouth), volta ao modo gráfico e desfaz o teste do
# initramfs separado. Não apaga nada do usuário; guarda cópias (.antes-do-reparo) do que altera.
# Para testar sem disco: RAIZ_JA_MONTADA=/uma/pasta sh notebook-restaurar-video.sh
set -u
UUID_RAIZ="${UUID_RAIZ:-7ee21e61-ad67-48f0-bbdb-f39d8ad1489e}"   # o root=UUID= do notebook (linha do kernel)
MNT="${MNT:-/mnt}"
dizer() { printf '>> %s\n' "$*"; }
falhar() { printf 'ERRO: %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" = 0 ] || [ -n "${RAIZ_JA_MONTADA:-}" ] || falhar "rode como root: sudo sh $0"

# 1. achar e montar a raiz (subvolume @ do Btrfs)
if [ -n "${RAIZ_JA_MONTADA:-}" ]; then
  R="$RAIZ_JA_MONTADA"
else
  DEV="${DEV:-$(blkid -U "$UUID_RAIZ" 2>/dev/null)}"
  if [ -z "$DEV" ]; then
    dizer "UUID $UUID_RAIZ não achado; procurando uma partição btrfs com o LUMNA..."
    for d in $(blkid -t TYPE=btrfs -o device); do
      mkdir -p /tmp/sonda && mount -o ro,subvol=@ "$d" /tmp/sonda 2>/dev/null || continue
      [ -f /tmp/sonda/etc/mkinitcpio.conf ] && [ -d /tmp/sonda/boot/grub ] && DEV="$d"
      umount /tmp/sonda
      [ -n "$DEV" ] && break
    done
  fi
  [ -n "$DEV" ] || falhar "não achei a partição do LUMNA. Rode 'lsblk -f' e repita com: DEV=/dev/sdXN sh $0"
  dizer "partição do LUMNA: $DEV"
  mkdir -p "$MNT"
  mountpoint -q "$MNT" && falhar "$MNT já está montado; rode 'umount -R $MNT' e repita"
  mount -o subvol=@ "$DEV" "$MNT" || falhar "não consegui montar $DEV (subvol=@)"
  R="$MNT"
fi
[ -f "$R/etc/mkinitcpio.conf" ] && [ -d "$R/boot/grub" ] || falhar "$R não parece o LUMNA (faltam /etc/mkinitcpio.conf e /boot/grub)"

guardar() { [ -f "$1" ] && [ ! -f "$1.antes-do-reparo" ] && cp -p "$1" "$1.antes-do-reparo"; return 0; }

# 2. tirar o splash do menu atual e das configurações (para uma atualização de kernel não trazê-lo de volta)
GCFG="$R/boot/grub/grub.cfg"
if [ -f "$GCFG" ]; then
  guardar "$GCFG"
  sed -i 's/ splash\b//g' "$GCFG"
  # se o teste do initramfs separado estiver ligado, volta ao initramfs normal
  sed -i 's#/initramfs-teste\.img#/initramfs-linux.img#g' "$GCFG"
  dizer "grub.cfg: splash tirado e initramfs normal"
else
  dizer "aviso: não achei $GCFG"
fi
GDEF="$R/etc/default/grub"
if [ -f "$GDEF" ]; then
  guardar "$GDEF"
  sed -i -E '/^GRUB_CMDLINE_LINUX(_DEFAULT)?=/ s/ splash\b//g; /^GRUB_CMDLINE_LINUX(_DEFAULT)?=/ s/"splash /"/g' "$GDEF"
  dizer "/etc/default/grub: splash tirado (o tema do GRUB continua)"
fi

# 3. modo gráfico de volta (o teste de modo texto deixou multi-user como padrão)
mkdir -p "$R/etc/systemd/system"
ln -sf /usr/lib/systemd/system/graphical.target "$R/etc/systemd/system/default.target"
dizer "modo gráfico (graphical.target) restaurado"

# 4. nenhum driver de vídeo forçado no initramfs (era o MODULES=(radeon) do teste que apagou a tela)
MK="$R/etc/mkinitcpio.conf"
if grep -Eq '^MODULES=\(.*radeon' "$MK"; then
  guardar "$MK"
  sed -i -E '/^MODULES=/ s/\bradeon\b//; /^MODULES=/ s/ +\)/)/; /^MODULES=/ s/\( +/(/' "$MK"
  dizer "mkinitcpio.conf: radeon tirado do MODULES (refaça o initramfs depois: veja o aviso final)"
  PRECISA_INITRAMFS=1
else
  PRECISA_INITRAMFS=0
fi

# 5. conferência (o que ficou valendo)
echo
dizer "conferência:"
printf '   splash no grub.cfg ........ %s\n' "$(grep -c ' splash' "$GCFG" 2>/dev/null || true) ocorrências (esperado: 0)"
printf '   initramfs no grub.cfg ..... %s\n' "$(grep -o 'initramfs-[a-z-]*\.img' "$GCFG" 2>/dev/null | sort | uniq -c | tr '\n' ' ')"
printf '   alvo padrão ............... %s\n' "$(readlink "$R/etc/systemd/system/default.target")"
printf '   MODULES ................... %s\n' "$(grep '^MODULES=' "$MK")"

if [ "$PRECISA_INITRAMFS" = 1 ] && [ -z "${RAIZ_JA_MONTADA:-}" ]; then
  dizer "refazendo o initramfs no sistema instalado..."
  arch-chroot "$R" mkinitcpio -P >/tmp/mkinitcpio.log 2>&1 && dizer "initramfs refeito" || { echo "ERRO no mkinitcpio; últimas linhas:"; tail -n 12 /tmp/mkinitcpio.log; }
fi

if [ -z "${RAIZ_JA_MONTADA:-}" ]; then
  sync; umount -R "$MNT" && dizer "desmontado. Pode reiniciar (sudo reboot) e tirar o pendrive." || dizer "aviso: não consegui desmontar $MNT; rode 'umount -R $MNT' antes de reiniciar"
fi
