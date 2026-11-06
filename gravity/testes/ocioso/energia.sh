#!/bin/sh
# lumna-ocioso apagando a tela e suspendendo (energia.ini), num Orbit headless; "um minuto" vale um segundo (LUMNA_OCIOSO_SEGUNDOS).
# sh energia.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; g=${2:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/bin"
export XDG_RUNTIME_DIR=$run XDG_CONFIG_HOME=$run/cfg PATH="$run/bin:$PATH" LUMNA_OCIOSO_SEGUNDOS=1
printf 'bloquear_apos = 0\n' > "$run/cfg/lumna/bloqueio.ini"
printf 'tela_apagar = 3\nsuspender = 8\n' > "$run/cfg/lumna/energia.ini"
printf '#!/bin/sh\necho "$@" >> %s/systemctl.log\n' "$run" > "$run/bin/systemctl"; chmod +x "$run/bin/systemctl"
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 2
sock=$(ls "$run"/orbit.*.sock | head -1); export WAYLAND_DISPLAY=$(basename "$sock" .sock | sed 's/^orbit\.//')
apagadas() { "$orbit_dir/orbit-msg" -s "$sock" saidas | grep -o '"apagada":true' | wc -l; }
dbus-run-session -- "$g/ocioso" >"$run/ocioso.log" 2>&1 &
oc=$!
sleep 1.5
[ "$(apagadas)" = 0 ] || fail "as telas começam acesas"
sleep 4
[ "$(apagadas)" = 2 ] || fail "depois de 3 s parado as duas telas devem estar apagadas (estão: $(apagadas))"
# mexer acende (o evento do teclado conta como atividade)
wtype x; sleep 1
[ "$(apagadas)" = 0 ] || fail "mexer devia acender as telas (apagadas: $(apagadas))"
# suspende depois de 8 s parado
sleep 9
grep -q suspend "$run/systemctl.log" 2>/dev/null || fail "depois de 8 s parado devia suspender (log do systemctl: $(cat "$run/systemctl.log" 2>/dev/null))"
# tirar o tempo (0 = nunca): as telas acendem e a contagem some
printf 'tela_apagar = 0\nsuspender = 0\n' > "$run/cfg/lumna/energia.ini.n" && mv "$run/cfg/lumna/energia.ini.n" "$run/cfg/lumna/energia.ini"
wtype y; sleep 5
[ "$(apagadas)" = 0 ] || fail "com tela_apagar = 0 a tela não devia apagar"
kill $oc $orbit 2>/dev/null; wait 2>/dev/null
[ $falhas -eq 0 ] && { echo "lumna-ocioso: tela e suspensão ok"; rm -rf "$run"; exit 0; }
cat "$run/ocioso.log" >&2; tail -5 "$run/orbit.log" >&2; exit 1
