#!/bin/bash
# O lumna-install em modo seco (--dry-run: só confere, não toca em disco): o modo de boot (UEFI/BIOS) e as conferências que dependem dele.
# Precisa de root e de um dispositivo de bloco qualquer (/dev/loop0); o lsblk é de mentira. sh/bash dryrun.sh
AQUI=$(cd "$(dirname "$0")" && pwd)
INST=$AQUI/../../../session/lumna-install
t=$(mktemp -d); mkdir -p "$t/bin" "$t/src/usr"
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
[ "$(id -u)" = 0 ] && [ -b /dev/loop0 ] || { echo "(sem root ou sem /dev/loop0: o teste do modo seco do instalador ficou de fora)"; exit 0; }
# lsblk de mentira: o dispositivo é um disco inteiro e nada dele está montado
printf '#!/bin/sh\ncase "$*" in *TYPE*) echo disk ;; esac\n' > "$t/bin/lsblk"; chmod +x "$t/bin/lsblk"
export LUMNA_INSTALL_SOURCE=$t/src
# PATH só com o que o script precisa (sem grub-install, para testar a falta)
seco() { echo senha | PATH="$t/bin:$PATH" bash "$INST" --dry-run --disk /dev/loop0 --user maria --name "Maria" --hostname lumna --timezone America/Sao_Paulo --keyboard "br abnt2" --locale pt_BR.UTF-8 2>&1; }
if command -v grub-install >/dev/null; then
  fail "este teste espera um sistema sem grub-install no PATH (tem um em $(command -v grub-install))"
fi
saida=$(LUMNA_FIRMWARE=uefi seco); echo "$saida" | grep -q "boot uefi" || fail "UEFI devia passar sem o grub: $saida"
saida=$(LUMNA_FIRMWARE=bios seco); echo "$saida" | grep -q "falta o grub" || fail "BIOS sem o grub devia falhar dizendo que falta o grub: $saida"
printf '#!/bin/sh\nexit 0\n' > "$t/bin/grub-install"; chmod +x "$t/bin/grub-install"
saida=$(LUMNA_FIRMWARE=bios seco); echo "$saida" | grep -q "boot bios" || fail "BIOS com o grub devia passar: $saida"
saida=$(LUMNA_FIRMWARE=ia64 seco); echo "$saida" | grep -q "modo de boot desconhecido" || fail "modo desconhecido devia falhar: $saida"
# sem forçar, no sistema de teste (sem /sys/firmware/efi num container): vira BIOS se não houver EFI
if [ ! -d /sys/firmware/efi ]; then
  saida=$(seco); echo "$saida" | grep -q "boot bios" || fail "sem /sys/firmware/efi devia ser BIOS: $saida"
fi
[ "$falhas" -eq 0 ] && echo "lumna-install --dry-run (UEFI e BIOS): ok" || { echo "$falhas falha(s)"; exit 1; }
