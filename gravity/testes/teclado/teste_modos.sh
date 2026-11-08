#!/bin/sh
# Quando o teclado na tela abre (Painel > Teclado na tela): modo "nunca" não abre sozinho mas abre e fecha pelo atalho (lumna-teclado alternar), modo
# "sempre" abre com o campo, e "auto" sem teclado físico (o Orbit headless não tem) também abre. sh teste_modos.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
P=$(pkg-config --variable=pkgdatadir wayland-protocols)
mkdir -p "$run/g" "$run/.config/lumna"
for x in "stable/xdg-shell/xdg-shell.xml xdg-shell" "unstable/text-input/text-input-unstable-v3.xml text-input-unstable-v3"; do
	set -- $x
	wayland-scanner client-header "$P/$1" "$run/g/$2-client-protocol.h"; wayland-scanner private-code "$P/$1" "$run/g/$2-protocol.c"
done
cc -std=gnu11 -I"$run/g" -o "$run/cliente_texto" "$aqui/cliente_texto.c" "$run/g/xdg-shell-protocol.c" "$run/g/text-input-unstable-v3-protocol.c" $(pkg-config --cflags --libs wayland-client) || exit 1
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
export XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
unset XDG_CONFIG_HOME XDG_DATA_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
"$run/cliente_texto" 90 > "$run/cliente.out" 2>&1 &
sleep 1.5
roda() { # modo -> o log do teclado em $run/$1.out
	printf '[teclado]\nmodo=%s\n' "$1" > "$run/.config/lumna/teclado.ini"
	LUMNA_TECLADO_DEBUG=1 "$gravity/teclado" > "$run/$1.out" 2>&1 &
	sleep 3
}
roda nunca
grep -q "^campo" "$run/nunca.out" || fail "nunca: o campo devia chegar ao teclado mesmo assim"
grep -q "^mostrar" "$run/nunca.out" && fail "nunca: não devia abrir sozinho"
"$gravity/teclado" alternar; sleep 1
grep -q "^mostrar" "$run/nunca.out" || fail "nunca: o atalho (alternar) devia abrir"
"$gravity/teclado" alternar; sleep 1
grep -q "^esconder" "$run/nunca.out" || fail "nunca: alternar de novo devia esconder"
"$gravity/teclado" alternar; sleep 1
"$gravity/teclado" esconder; sleep 1
[ "$(grep -c '^esconder' "$run/nunca.out")" -ge 2 ] || fail "nunca: 'esconder' devia fechar o teclado aberto"
pkill -x teclado; sleep 0.5
roda sempre
grep -q "^mostrar" "$run/sempre.out" || fail "sempre: devia abrir com o campo"
pkill -x teclado; sleep 0.5
roda auto
grep -q "^mostrar" "$run/auto.out" || fail "auto: sem teclado físico devia abrir"
pkill -x orbit; pkill -x teclado; pkill -x cliente_texto
[ "$falhas" -eq 0 ] && echo "teclado na tela (quando abre): ok" || { echo "$falhas falha(s) (log em $run)"; exit 1; }
