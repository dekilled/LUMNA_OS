#!/usr/bin/env bash
# Dá boot no ISO do LUMNA OS numa VM QEMU/KVM com UEFI, como um PC de verdade.
#
# Uso: scripts/run-vm.sh [opções] [arquivo.iso]
#   --telas N   número de monitores virtuais (padrão 1, até 3)
#   --sem-gl    sem aceleração OpenGL (lento; só para máquinas sem virgl)
#   --disco     liga um disco virtual de 64 GB (out/lumna-vm.qcow2) para testar instalação
#
# Precisa de: qemu-system-x86_64, firmware OVMF (edk2-ovmf / ovmf) e KVM (/dev/kvm).
set -euo pipefail

root="$(realpath "$(dirname "$0")/..")"
screens=1 gl=on disk=""
iso=""
while (($#)); do
  case "$1" in
    --telas) screens="$2"; shift ;;
    --sem-gl) gl=off ;;
    --disco) disk="$root/out/lumna-vm.qcow2" ;;
    *) iso="$1" ;;
  esac
  shift
done
iso="${iso:-$(ls -t "$root"/out/lumna-os-*.iso 2>/dev/null | head -1)}"
[[ -f "$iso" ]] || { echo "ISO não encontrado. Rode scripts/build-iso.sh antes." >&2; exit 1; }

# Firmware UEFI: cada distro guarda num lugar.
for pair in \
  /usr/share/edk2/x64/OVMF_CODE.4m.fd:/usr/share/edk2/x64/OVMF_VARS.4m.fd \
  /usr/share/OVMF/OVMF_CODE_4M.fd:/usr/share/OVMF/OVMF_VARS_4M.fd \
  /usr/share/edk2/ovmf/OVMF_CODE.fd:/usr/share/edk2/ovmf/OVMF_VARS.fd; do
  code="${pair%%:*}" vars="${pair##*:}"
  [[ -f "$code" ]] && break
  code=""
done
[[ -n "$code" ]] || { echo "Firmware OVMF não encontrado (instale edk2-ovmf ou ovmf)." >&2; exit 1; }
mkdir -p "$root/out"
[[ -f "$root/out/lumna-vm-vars.fd" ]] || cp "$vars" "$root/out/lumna-vm-vars.fd"

args=(
  -name "LUMNA OS"
  -machine q35,accel=kvm -cpu host -smp 4 -m 6G
  -drive if=pflash,format=raw,readonly=on,file="$code"
  -drive if=pflash,format=raw,file="$root/out/lumna-vm-vars.fd"
  -cdrom "$iso" -boot order=d
  -nic user,model=virtio-net-pci
  -audiodev pipewire,id=snd -device intel-hda -device hda-duplex,audiodev=snd
  -usb -device usb-tablet
)
if [[ "$gl" == on ]]; then
  args+=(-device "virtio-vga-gl,max_outputs=$screens" -display gtk,gl=on)
else
  args+=(-device "virtio-vga,max_outputs=$screens" -display gtk)
fi
if [[ -n "$disk" ]]; then
  [[ -f "$disk" ]] || qemu-img create -f qcow2 "$disk" 64G
  args+=(-drive file="$disk",if=virtio,format=qcow2)
fi

exec qemu-system-x86_64 "${args[@]}"
