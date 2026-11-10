#!/bin/sh
# A página Drivers e kernels da Loja num Orbit headless: uma máquina com NVIDIA RTX 3060 e Intel, kernel linux, GRUB. sh teste_drivers.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
aqui=$(cd "$(dirname "$0")" && pwd)
fx=$(mktemp -d)
mkdir -p "$fx/boot/grub" "$fx/efi"
echo "menuentry 'Arch' { linux /vmlinuz-linux }" > "$fx/boot/grub/grub.cfg"
echo "01:00.0 VGA compatible controller [0300]: NVIDIA Corporation GA106 [GeForce RTX 3060] [10de:2503] (rev a1)" > "$fx/lspci.txt"
printf 'processor\t: 0\nvendor_id\t: GenuineIntel\nmodel name\t: 12th Gen Intel(R) Core(TM) i5-12400F\n' > "$fx/cpuinfo.txt"
export FALSO_LSPCI="$fx/lspci.txt" LUMNA_CPUINFO="$fx/cpuinfo.txt" LUMNA_UNAME_R=6.10.8-arch1-1 LUMNA_BOOT="$fx/boot" LUMNA_EFI="$fx/efi" LUMNA_BOOT_LIVRE=50000000000
export FALSO_CATALOGO_EXTRA="vulkan-radeon vulkan-intel vulkan-nouveau intel-media-driver nvidia nvidia-open nvidia-lts nvidia-dkms nvidia-utils nvidia-settings amd-ucode intel-ucode linux-zen linux-hardened linux-headers linux-lts-headers linux-zen-headers linux-hardened-headers"
export TESTE=${TESTE:-teste_drivers.py}
sh "$aqui/teste_ui.sh" "$@"
rc=$?
rm -rf "$fx"
exit $rc
