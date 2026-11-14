#!/bin/sh
# Mostra o tema do GRUB como ele fica de verdade: monta um GRUB EFI com o tema dentro, sobe no QEMU com OVMF (1920x1080) e tira uma foto da tela.
# Uso: sh renderiza.sh [foto.png] [segundos_de_espera] [timeout_do_menu]     (precisa de qemu-system-x86_64, ovmf, grub-mkstandalone e python3 com PIL)
set -eu
aqui=$(cd "$(dirname "$0")" && pwd); tema="$aqui/../../../packages/lumna-boot/grub/lumna"
foto=${1:-/tmp/grub-tema.png}; espera=${2:-7}; limite=${3:-10}
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cat > "$T/grub.cfg" <<FIM
insmod all_video
insmod gfxterm
insmod gfxmenu
insmod png
set gfxmode=1920x1080
set gfxpayload=keep
terminal_output gfxterm
$(for f in "$tema"/*.pf2; do echo "loadfont (memdisk)/theme/$(basename "$f")"; done)
set theme=(memdisk)/theme/theme.txt
set timeout=$limite
menuentry 'LUMNA OS' --class lumna --class gnu-linux --class os { true }
submenu '${SUB:-OPÇÕES AVANÇADAS}' --class advanced { menuentry 'x' { true } }
menuentry 'WINDOWS' --class windows --class os { true }
menuentry 'RECOVERY' --class os { true }
FIM
args=""
for f in $(cd "$tema" && find . -type f | sed 's#^\./##'); do args="$args theme/$f=$tema/$f"; done
mkdir -p "$T/esp/EFI/BOOT"
grub-mkstandalone -O x86_64-efi --modules="part_gpt part_msdos fat all_video gfxterm gfxmenu png" -o "$T/esp/EFI/BOOT/BOOTX64.EFI" "boot/grub/grub.cfg=$T/grub.cfg" $args >/dev/null 2>&1
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$T/vars.fd"
python3 - "$T" "$foto" "$espera" <<'PY'
import json, os, socket, subprocess, sys, time
T, foto, espera = sys.argv[1], sys.argv[2], float(sys.argv[3])
q = subprocess.Popen(["qemu-system-x86_64", "-machine", "q35", "-m", "512", "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
    "-drive", f"if=pflash,format=raw,file={T}/vars.fd", "-drive", f"file=fat:rw:{T}/esp,format=raw", "-vga", "std", "-display", "none",
    "-qmp", f"unix:{T}/qmp.sock,server,nowait", "-no-reboot"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(50):
    if os.path.exists(f"{T}/qmp.sock"):
        break
    time.sleep(0.2)
s = socket.socket(socket.AF_UNIX); s.connect(f"{T}/qmp.sock"); f = s.makefile("rw")
def cmd(c, **a):
    f.write(json.dumps({"execute": c, "arguments": a} if a else {"execute": c}) + "\n"); f.flush()
    while True:
        l = f.readline()
        if '"return"' in l or '"error"' in l:
            return l
f.readline(); cmd("qmp_capabilities")
time.sleep(espera)
cmd("screendump", filename=f"{T}/s.ppm"); time.sleep(0.5)
from PIL import Image
Image.open(f"{T}/s.ppm").save(foto)
cmd("quit"); q.wait(timeout=10)
PY
echo "foto em $foto"
