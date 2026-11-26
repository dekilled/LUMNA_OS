#!/bin/sh
# O xdg-foreign (v1 e v2) do Orbit, num Orbit headless com um cliente de verdade (cliente_foreign.c, que faz o papel do app e do portal):
#  - o Orbit anuncia os quatro globais (exporter e importer, v1 e v2)
#  - exportar uma janela devolve um handle; importar esse handle e fazer dele o pai de outra janela funciona, sem erro de protocolo
#  - importar um handle que não existe: o Orbit responde "destroyed"
#  - quando o app deixa de exportar (ou fecha a janela), o importador é avisado
# sh teste_foreign.sh PASTA_DO_ORBIT
orbit_dir=${1:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
P=${WAYLAND_PROTOCOLS_DIR:-/usr/share/wayland-protocols}
gera() { wayland-scanner client-header "$1" "$run/$2-client-protocol.h"; wayland-scanner private-code "$1" "$run/$2-protocol.c"; }
gera $P/stable/xdg-shell/xdg-shell.xml xdg-shell
gera $P/unstable/xdg-foreign/xdg-foreign-unstable-v1.xml xdg-foreign-unstable-v1
gera $P/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml xdg-foreign-unstable-v2
cc -std=gnu11 -Wall -I"$run" -o "$run/cliente" "$aqui/cliente_foreign.c" "$run"/*-protocol.c $(pkg-config --cflags --libs wayland-client) || exit 1
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
export XDG_RUNTIME_DIR=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
timeout 20 "$run/cliente" > "$run/saida.txt" 2>&1; rc=$?
for l in "global zxdg_exporter_v1" "global zxdg_importer_v1" "global zxdg_exporter_v2" "global zxdg_importer_v2" \
	"v2 handle ok" "v2 import vivo" "v2 handle ruim destruido" "v2 fim da exportacao destruido" \
	"v1 handle ok" "v1 import vivo" "v1 handle ruim destruido" "v1 fim da exportacao destruido" "vcruzado v1->v2 vivo" "fim"; do
	grep -qx "$l" "$run/saida.txt" || fail "faltou '$l'"
done
[ "$rc" -eq 0 ] || fail "o cliente terminou com código $rc"
pgrep -x orbit > /dev/null || fail "o Orbit caiu"
pkill -x orbit
[ "$falhas" -eq 0 ] && echo "xdg-foreign v1 e v2: ok" || { echo "$falhas falha(s) (saída e log em $run)"; cat "$run/saida.txt" >&2; exit 1; }
