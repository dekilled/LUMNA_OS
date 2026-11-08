#!/bin/sh
# A trava e o confinamento do ponteiro (pointer-constraints + relative-pointer) num Orbit headless, com um cliente de verdade (cliente_trava.c) e o mouse virtual:
#  - lock: o cursor NÃO anda (a janela nunca vê "leave"), e o app recebe o movimento relativo mesmo assim
#  - confine: o cursor não sai da janela (sem "leave"; as posições ficam dentro), e o movimento relativo também chega
#  - sem a trava, o cursor sai da janela ("leave")
# sh teste_trava.sh PASTA_DO_ORBIT ARRASTO2
orbit_dir=${1:?}; M=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
P=/usr/share/wayland-protocols
gera() { wayland-scanner client-header "$1" "$run/$2-client-protocol.h"; wayland-scanner private-code "$1" "$run/$2-protocol.c"; }
gera $P/stable/xdg-shell/xdg-shell.xml xdg-shell
gera $P/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml pointer-constraints-unstable-v1
gera $P/unstable/relative-pointer/relative-pointer-unstable-v1.xml relative-pointer-unstable-v1
cc -std=gnu11 -Wall -I"$run" -o "$run/cliente" "$aqui/cliente_trava.c" "$run"/*-protocol.c $(pkg-config --cflags --libs wayland-client) || exit 1
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
export XDG_RUNTIME_DIR=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
H="env ORBIT_SOCK=$run/orbit.wayland-0.sock $orbit_dir/orbit-msg"
caixa() { $H janelas | python3 -c "import json,sys; j=json.load(sys.stdin)['janelas'][0]; print(j['x'], j['y'], j['w'], j['h'])"; }
rodar() { # modo -> o que o cliente viu (move o mouse para dentro da janela e depois 600 px para a direita)
	"$run/cliente" "$1" 9 > "$run/$1.out" 2>&1 &
	cli=$!
	sleep 2.5
	set -- $1 $(caixa)
	"$M" casa; sleep 0.3
	"$M" mover $(( $2 + $4 / 2 )) $(( $3 + $5 / 2 )); sleep 0.8
	"$M" mover 600 0; sleep 0.8
	wait $cli
}
rodar lock
grep -q "^locked" "$run/lock.out" || fail "lock: o Orbit devia travar o ponteiro (faltou 'locked'): $(cat "$run/lock.out" | tr '\n' ' ')"
grep -q "^leave" "$run/lock.out" && fail "lock: com o ponteiro travado o cursor não pode sair da janela (veio 'leave')"
soma=$(sed -n 's/^soma \([-0-9]*\) .*/\1/p' "$run/lock.out")
[ "${soma:-0}" -ge 550 ] || fail "lock: o app devia receber o movimento relativo de 600 px (soma $soma)"
rodar confine
grep -q "^confined" "$run/confine.out" || fail "confine: o Orbit devia confinar o ponteiro (faltou 'confined'): $(cat "$run/confine.out" | tr '\n' ' ')"
grep -q "^leave" "$run/confine.out" && fail "confine: o cursor confinado não pode sair da janela (veio 'leave')"
maior=$(sed -n 's/^motion \([0-9]*\) .*/\1/p' "$run/confine.out" | sort -n | tail -1)
[ "${maior:-0}" -lt 300 ] || fail "confine: o cursor passou da borda da janela (x local $maior de 300)"
soma=$(sed -n 's/^soma \([-0-9]*\) .*/\1/p' "$run/confine.out")
[ "${soma:-0}" -ge 550 ] || fail "confine: o app devia receber o movimento relativo de 600 px (soma $soma)"
# sem trava nenhuma (o cliente já saiu): o cursor anda livre
pkill -x orbit
[ "$falhas" -eq 0 ] && echo "trava e confinamento do ponteiro: ok" || { echo "$falhas falha(s) (log em $run)"; exit 1; }
