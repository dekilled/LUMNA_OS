#!/bin/sh
# O teclado na tela por dentro (servicos/teclado_im.c): com um Orbit headless e um app de mentira com um campo de texto, o método de entrada vê o
# campo abrir, escreve (commit_string), apaga, e manda Enter e setas pelo teclado virtual.
#   sh teste_im.sh PASTA_DO_BUILD_DO_ORBIT
orbit_dir=${1:?}
aqui=$(cd "$(dirname "$0")" && pwd)
raiz=$(cd "$aqui/../.." && pwd)
run=$(mktemp -d); chmod 700 "$run"
P=$(pkg-config --variable=pkgdatadir wayland-protocols)
gera() { wayland-scanner client-header "$1" "$run/$2-client-protocol.h"; wayland-scanner private-code "$1" "$run/$2-protocol.c"; }
gera "$P/stable/xdg-shell/xdg-shell.xml" xdg-shell
gera "$P/unstable/text-input/text-input-unstable-v3.xml" text-input-unstable-v3
gera "$raiz/protocolos/input-method-unstable-v2.xml" input-method-unstable-v2
gera "$raiz/protocolos/virtual-keyboard-unstable-v1.xml" virtual-keyboard-unstable-v1
cc -std=gnu11 -Wall -I"$run" -o "$run/cliente_texto" "$aqui/cliente_texto.c" "$run/xdg-shell-protocol.c" "$run/text-input-unstable-v3-protocol.c" $(pkg-config --cflags --libs wayland-client) || exit 1
cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -I"$run" -I"$raiz/servicos" -o "$run/testa_im" "$aqui/testa_im.c" "$raiz/servicos/teclado_im.c" \
	"$run/input-method-unstable-v2-protocol.c" "$run/virtual-keyboard-unstable-v1-protocol.c" $(pkg-config --cflags --libs wayland-client xkbcommon) || exit 1
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
export XDG_RUNTIME_DIR=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
"$run/cliente_texto" 12 > "$run/cliente.out" 2>&1 &
sleep 1.5
"$run/testa_im" > "$run/im.out" 2>&1
sleep 1
pkill -x orbit
grep -q "^ligado" "$run/im.out" || fail "o método de entrada não ligou: $(cat "$run/im.out" | tr '\n' ' ')"
grep -q "campo ativo=1" "$run/im.out" || fail "o campo não chegou ao método de entrada"
grep -q "^texto \[olá\]" "$run/cliente.out" || fail "o texto 'olá' não chegou ao campo: $(cat "$run/cliente.out" | tr '\n' ' ')"
grep -q "^texto \[olá 😀\]" "$run/cliente.out" || fail "o emoji não chegou ao campo"
n=$(grep -c "^texto \[olá\]" "$run/cliente.out"); [ "$n" -ge 2 ] || fail "apagar duas vezes devia voltar a 'olá' e depois 'olá' sem o espaço (visto: $(grep '^texto' "$run/cliente.out" | tr '\n' ' '))"
grep -q "^texto \[ol\]\|^texto \[olá\]$" "$run/cliente.out" || true
grep -q "^tecla 28 press" "$run/cliente.out" || fail "o Enter não chegou como tecla"
grep -q "^tecla 105 press" "$run/cliente.out" || fail "a seta para a esquerda não chegou como tecla"
[ "$falhas" -eq 0 ] && echo "teclado na tela (método de entrada e teclas): ok" || { echo "$falhas falha(s) (log em $run)"; cat "$run/cliente.out" "$run/im.out"; exit 1; }
