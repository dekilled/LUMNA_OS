#!/bin/sh
# O ajudante lumna-boot-tema numa raiz de mentira (com grub-mkconfig e mkinitcpio falsos): aplica, aplica de novo sem duplicar, não mexe no que é do usuário
# (a detecção de outros sistemas, a linha do kernel, as entradas) e desfaz. Uso: sh iso/testes/boot_tema.sh
aqui=$(cd "$(dirname "$0")/.." && pwd)
ajudante="$aqui/../packages/lumna-boot/lumna-boot-tema"
tema="$aqui/../packages/lumna-boot/grub/lumna"
run=$(mktemp -d); raiz="$run/raiz"; bin="$run/bin"
mkdir -p "$raiz/etc/default" "$raiz/boot/grub" "$bin"
cat > "$raiz/etc/default/grub" <<'EOF'
GRUB_DEFAULT=0
GRUB_TIMEOUT=2
GRUB_DISTRIBUTOR="LUMNA OS"
GRUB_CMDLINE_LINUX_DEFAULT="quiet loglevel=3"
GRUB_PRELOAD_MODULES="part_gpt part_msdos btrfs"
GRUB_DISABLE_OS_PROBER=true
EOF
echo 'HOOKS=(base udev autodetect modconf kms keyboard keymap consolefont block filesystems fsck)' > "$raiz/etc/mkinitcpio.conf"
cat > "$bin/grub-mkconfig" <<EOF
#!/bin/sh
echo "\$*" >> "$run/mkconfig.log"
while [ \$# -gt 0 ]; do [ "\$1" = -o ] && saida=\$2; shift; done
cat > "\$saida" <<'CFG'
menuentry 'LUMNA OS' --class lumna --class gnu-linux --class os \$menuentry_id_option 'gnulinux-simple-x' {
	linux /boot/vmlinuz-linux
}
submenu 'Advanced options for LUMNA OS' \$menuentry_id_option 'gnulinux-advanced-x' {
	menuentry 'LUMNA OS, with Linux linux' --class lumna { true }
}
menuentry 'Windows 11' --class windows --class os \$menuentry_id_option 'osprober-efi-x' {
	chainloader /EFI/Microsoft/bootmgfw.efi
}
menuentry 'UEFI Firmware Settings' \$menuentry_id_option 'uefi-firmware' {
	fwsetup
}
CFG
EOF
printf '#!/bin/sh\necho "$*" >> %s/mkinitcpio.log\n' "$run" > "$bin/mkinitcpio"
chmod +x "$bin/"*
export PATH="$bin:$PATH" LUMNA_RAIZ="$raiz" LUMNA_TEMA_GRUB="$tema"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
def="$raiz/etc/default/grub"; cfg="$raiz/boot/grub/grub.cfg"; mk="$raiz/etc/mkinitcpio.conf"
# 0. o diagnóstico roda sem root e sem quebrar
sh "$ajudante" diagnostico >/dev/null 2>&1 || falha "diagnostico devia terminar bem"
# 1. aplicar
sh "$ajudante" aplicar >/dev/null || falha "aplicar devia terminar bem"
[ -f "$raiz/boot/grub/themes/lumna/theme.txt" ] && [ -f "$raiz/boot/grub/themes/lumna/background.png" ] || falha "o tema devia ir para /boot/grub/themes/lumna"
grep -q '^GRUB_THEME="/boot/grub/themes/lumna/theme.txt"' "$def" || falha "GRUB_THEME"
grep -q '^GRUB_GFXMODE="1920x1080,auto"' "$def" || falha "GRUB_GFXMODE"
grep -q '^GRUB_GFXPAYLOAD_LINUX=keep' "$def" || falha "GRUB_GFXPAYLOAD_LINUX"
grep -q '^GRUB_CMDLINE_LINUX_DEFAULT="quiet loglevel=3 splash"' "$def" || falha "o splash entra na linha do kernel sem tirar o resto ($(grep CMDLINE "$def"))"
grep -q '^GRUB_DISABLE_OS_PROBER=true' "$def" || falha "a detecção de outros sistemas não é do tema: tem de ficar como estava"
grep -q '^GRUB_TIMEOUT=2' "$def" || falha "o tempo do menu não é do tema"
grep -Eq '^HOOKS=\(base udev plymouth autodetect' "$mk" || falha "o gancho plymouth vai logo depois do udev ($(cat "$mk"))"
grep -q '^Theme=lumna' "$raiz/etc/plymouth/plymouthd.conf" || falha "o tema do Plymouth"
grep -q "^submenu 'Advanced options for LUMNA OS' --class advanced " "$cfg" || falha "as opções avançadas ganham a classe do ícone ($(grep submenu "$cfg"))"
grep -q "^menuentry 'UEFI Firmware Settings' --class firmware " "$cfg" || falha "o firmware ganha a classe do ícone"
grep -q "^menuentry 'Windows 11' --class windows --class os " "$cfg" || falha "uma entrada que já tinha classe não pode mudar"
[ "$(wc -l < "$run/mkinitcpio.log")" = 1 ] || falha "o initramfs devia ser refeito uma vez"
antes=$(cat "$def" "$mk" "$cfg" | md5sum)
# 2. de novo: nada duplica
sh "$ajudante" aplicar >/dev/null || falha "aplicar de novo"
[ "$(cat "$def" "$mk" "$cfg" | md5sum)" = "$antes" ] || falha "aplicar duas vezes não podia mudar nada (duplicou algo)"
[ "$(grep -c splash "$def")" = 1 ] || falha "o splash devia estar uma vez só"
# 3. as opções do instalador
: > "$run/mkinitcpio.log"
sh "$ajudante" plymouth --sem-initramfs >/dev/null
[ ! -s "$run/mkinitcpio.log" ] || falha "--sem-initramfs não devia refazer o initramfs"
# 4. o gancho do pacman só mexe no menu de quem aplicou
: > "$run/mkconfig.log"
sh "$ajudante" regerar >/dev/null; [ -s "$run/mkconfig.log" ] || falha "regerar com o tema aplicado devia refazer o menu"
# 5. desfazer
sh "$ajudante" desfazer >/dev/null || falha "desfazer"
[ ! -e "$raiz/boot/grub/themes/lumna" ] || falha "desfazer devia tirar o tema"
grep -q 'GRUB_THEME\|GRUB_GFXMODE\|splash' "$def" && falha "desfazer devia limpar o /etc/default/grub ($(cat "$def"))"
grep -q 'plymouth' "$mk" && falha "desfazer devia tirar o gancho plymouth"
grep -q '^GRUB_DISABLE_OS_PROBER=true' "$def" && grep -q '^GRUB_CMDLINE_LINUX_DEFAULT="quiet loglevel=3"' "$def" || falha "desfazer devolve o arquivo como estava ($(cat "$def"))"
: > "$run/mkconfig.log"
sh "$ajudante" regerar >/dev/null; [ ! -s "$run/mkconfig.log" ] || falha "regerar sem o tema aplicado não devia mexer no menu"
[ $ok = 1 ] && { echo "boot_tema: ok"; rm -rf "$run"; } || { echo "(em $run)"; exit 1; }
