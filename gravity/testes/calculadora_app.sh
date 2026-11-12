#!/bin/sh
# A calculadora (janela) num Orbit headless, e depois reaberta para ver o que ficou guardado. Uso: sh testes/calculadora_app.sh PASTA_DO_ORBIT (com o make calculadora feito)
orbit_dir=${1:?}
aqui=$(cd "$(dirname "$0")" && pwd); gravity=$(dirname "$aqui")
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 LUMNA_CALCULADORA_DEBUG=1
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
"$gravity/calculadora" > "$run/c.out" 2> "$run/c.err" & c=$!
sleep 2
rc=0
python3 "$aqui/calculadora/teste_app.py" "$orbit_dir" "$run" "$gravity" || rc=$?
if kill -0 $c 2>/dev/null; then echo "calculadora: viva"; else wait $c; echo "calculadora: MORREU (status $?)"; fi
kill $c 2>/dev/null; sleep 0.5
# reabrir: a fita e o teclado voltam
printf '15%% de 200\t30\n' > "$run/.local/state/lumna/calculadora.txt"
printf '[calculadora]\nteclado=true\n' > "$run/.config/lumna/calculadora.ini"
"$gravity/calculadora" > "$run/c2.out" 2> "$run/c2.err" & c2=$!
sleep 2
if grep -q 'linhas=1 teclado=1 ultima=\[15% de 200\] ultimo=\[30\]' "$run/c2.out"; then echo "reabrir: a fita e o teclado voltaram"; else echo "FALHOU: reabrir não restaurou a fita e o teclado"; grep '^estado' "$run/c2.out" | tail -1; rc=1; fi
kill $c2 2>/dev/null; pkill -x orbit
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/c.err"; }
echo "rc=$rc"
exit $rc
