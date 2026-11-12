#!/bin/sh
# O Signals (janela) num Orbit headless. Uso: sh testes/signals_app.sh PASTA_DO_ORBIT (com o make signals feito)
orbit_dir=${1:?}
aqui=$(cd "$(dirname "$0")" && pwd); gravity=$(dirname "$aqui")
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 LUMNA_SIGNALS_DEBUG=1
unset XDG_CONFIG_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
printf 'ola\nmundo\n' > "$run/a.txt"
# o seletor de arquivos de mentira: escolhe sempre o mesmo arquivo
cat > "$run/seletor.sh" <<EOS
#!/bin/sh
echo "$run/escolhido.txt"
EOS
chmod +x "$run/seletor.sh"
export LUMNA_SELETOR="$run/seletor.sh"
if [ -n "$SG_GDB" ]; then gdb -batch -ex run -ex bt --args "$gravity/signals" "$run/a.txt" > "$run/signals.out" 2> "$run/signals.err" & sg=$!; else "$gravity/signals" "$run/a.txt" > "$run/signals.out" 2> "$run/signals.err" & sg=$!; fi
sleep 2
rc=0
python3 "$aqui/signals/teste_app.py" "$orbit_dir" "$run" "$gravity" || rc=$?
if kill -0 $sg 2>/dev/null; then echo "signals: vivo"; else wait $sg; echo "signals: MORREU (status $?)"; fi
kill $sg 2>/dev/null; pkill -x orbit
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/signals.err"; }
echo "rc=$rc"
exit $rc
