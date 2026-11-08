#!/bin/sh
# O teclado na tela inteiro (exemplos/teclado.c) num Orbit headless. sh teste_app.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY (com o `make teclado` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
P=$(pkg-config --variable=pkgdatadir wayland-protocols)
mkdir -p "$run/g"
for x in "stable/xdg-shell/xdg-shell.xml xdg-shell" "unstable/text-input/text-input-unstable-v3.xml text-input-unstable-v3"; do
	set -- $x
	wayland-scanner client-header "$P/$1" "$run/g/$2-client-protocol.h"; wayland-scanner private-code "$P/$1" "$run/g/$2-protocol.c"
done
cc -std=gnu11 -I"$run/g" -o "$run/cliente_texto" "$aqui/cliente_texto.c" "$run/g/xdg-shell-protocol.c" "$run/g/text-input-unstable-v3-protocol.c" $(pkg-config --cflags --libs wayland-client) || exit 1
export XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
"$run/cliente_texto" 60 > "$run/cliente.out" 2>&1 &
sleep 1.5
LUMNA_TECLADO_DEBUG=1 "$gravity/teclado" > "$run/teclado.out" 2> "$run/teclado.err" &
sleep 1
python3 "$aqui/teste_app.py" "$orbit_dir" "$run"
rc=$?
pkill -x orbit; pkill -x teclado; pkill -x cliente_texto
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -20 "$run/teclado.err"; }
exit $rc
