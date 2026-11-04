#!/bin/sh
# Diagnóstico dos monitores: o que o kernel enxerga e o que o Orbit enxerga.
# Rode DENTRO da sessão do LUMNA (um terminal aberto nela) e mande a saída inteira:
#   sh diag-telas.sh > ~/telas-diag.txt 2>&1
# Não muda nada no sistema.

sec() { printf '\n===== %s =====\n' "$1"; }

sec "placas de vídeo (PCI) e o driver em uso"
lspci -k 2>/dev/null | grep -A3 -i -E "vga|3d|display" || echo "(lspci não achou nada ou não existe)"

sec "dispositivos DRM (/dev/dri)"
ls -l /dev/dri 2>&1
for c in /sys/class/drm/card[0-9]; do
	[ -e "$c" ] || continue
	printf '%s -> driver: ' "$(basename "$c")"
	basename "$(readlink -f "$c/device/driver" 2>/dev/null)" 2>/dev/null || echo "?"
	[ -e "$c/device/boot_vga" ] && printf '   boot_vga=%s\n' "$(cat "$c/device/boot_vga")"
done

sec "conectores que o KERNEL vê (status, ligado, EDID, modos)"
for c in /sys/class/drm/card*-*; do
	[ -e "$c/status" ] || continue
	n=$(basename "$c")
	printf '%-22s status=%-13s enabled=%-8s edid=%s bytes  modo0=%s\n' "$n" "$(cat "$c/status" 2>/dev/null)" \
		"$(cat "$c/enabled" 2>/dev/null)" "$(wc -c < "$c/edid" 2>/dev/null || echo 0)" "$(head -1 "$c/modes" 2>/dev/null)"
done

sec "módulos de vídeo carregados"
lsmod 2>/dev/null | grep -i -E "^(nvidia|nvidia_drm|nvidia_modeset|amdgpu|radeon|i915|xe|nouveau|simpledrm|simplefb|efifb|vesafb)\b" || echo "(nenhum dos conhecidos)"
[ -e /sys/module/nvidia_drm/parameters/modeset ] && printf 'nvidia_drm modeset=%s (precisa ser Y)\n' "$(cat /sys/module/nvidia_drm/parameters/modeset)"
[ -e /sys/module/nvidia_drm/parameters/fbdev ] && printf 'nvidia_drm fbdev=%s\n' "$(cat /sys/module/nvidia_drm/parameters/fbdev)"

sec "o que o Orbit enxerga (orbit-msg saidas)"
if command -v orbit-msg >/dev/null 2>&1; then
	orbit-msg saidas 2>&1 | python3 -c '
import json, sys
t = sys.stdin.read()
try:
    d = json.loads(t)
except Exception:
    print(t); sys.exit()
for s in d.get("saidas", []):
    print("%-12s %dx%d@%s  pos=%s,%s  ligada=%s  fabricante=%r modelo=%r  mm=%s" % (
        s["nome"], s["largura"], s["altura"], s["hz"], s["x"], s["y"], s["ligada"], s["fabricante"], s["modelo"], s["mm"]))
print("total:", len(d.get("saidas", [])))
' 2>&1
else
	echo "(orbit-msg não está no PATH)"
fi

sec "variáveis que mexem no backend"
env | grep -E "^(WLR_|ORBIT_|XDG_SESSION|WAYLAND_DISPLAY|DISPLAY)=" | sort

sec "seat e sessão"
loginctl show-session "${XDG_SESSION_ID:-self}" -p Type -p Seat -p Active -p State 2>&1

sec "telas.ini"
cat "${XDG_CONFIG_HOME:-$HOME/.config}/lumna/telas.ini" 2>&1

sec "últimas linhas do kernel sobre vídeo"
dmesg 2>/dev/null | grep -i -E "drm|nvidia|amdgpu|i915|simpledrm|efifb|fb0" | tail -25 || echo "(dmesg sem permissão: rode 'sudo dmesg | grep -i -E \"drm|nvidia|amdgpu|i915|simpledrm\"')"
