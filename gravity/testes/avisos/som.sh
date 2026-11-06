#!/bin/sh
# O som dos avisos: o lumna-avisos toca o som escolhido (sons.ini) quando chega um aviso normal, e fica quieto no baixo e no suppress-sound.
# sh som.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; g=${2:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/sons"
echo x > "$run/sons/message-new-instant.oga"; echo x > "$run/sons/dialog-warning.oga"
export PATH=$g/testes/painel/bin:$PATH XDG_RUNTIME_DIR=$run XDG_CONFIG_HOME=$run/cfg LUMNA_SONS_DIR=$run/sons SONS_LOG=$run/tocou.log
printf 'avisos = sim\nvolume = 40\n' > "$run/cfg/lumna/sons.ini"
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 2
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
dbus-run-session -- sh -c '
	'"$g"'/avisos >'"$run"'/avisos.log 2>&1 &
	av=$!
	sleep 2
	n() { gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications --method org.freedesktop.Notifications.Notify teste 0 "" "$1" "corpo" "[]" "$2" 3000 >/dev/null; sleep 1; }
	n "normal" "{}"
	n "baixo" "{\"urgency\": <byte 0>}"
	n "calado" "{\"suppress-sound\": <true>}"
	n "critico" "{\"urgency\": <byte 2>}"
	kill $av
'
linhas=$(wc -l < "$run/tocou.log" 2>/dev/null || echo 0)
[ "$linhas" = 2 ] || fail "devia tocar 2 vezes (normal e crítico), tocou $linhas: $(cat "$run/tocou.log" 2>/dev/null | tr '\n' '|')"
sed -n 1p "$run/tocou.log" | grep -q "message-new-instant.oga" || fail "o normal devia tocar o message-new-instant"
sed -n 1p "$run/tocou.log" | grep -q "volume=0.40" || fail "o volume devia ser 0.40"
sed -n 2p "$run/tocou.log" | grep -q "dialog-warning.oga" || fail "o crítico devia tocar o dialog-warning"
# desligado: nada toca
rm -f "$run/tocou.log"; printf 'avisos = nao\n' > "$run/cfg/lumna/sons.ini"
dbus-run-session -- sh -c '
	'"$g"'/avisos >'"$run"'/avisos2.log 2>&1 &
	av=$!; sleep 2
	gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications --method org.freedesktop.Notifications.Notify teste 0 "" "x" "y" "[]" "{}" 3000 >/dev/null; sleep 1; kill $av
'
[ -s "$run/tocou.log" ] && fail "com avisos = nao não devia tocar nada"
kill $orbit 2>/dev/null; wait 2>/dev/null
[ $falhas -eq 0 ] && { echo "som dos avisos: ok"; rm -rf "$run"; exit 0; }
cat "$run/avisos.log" >&2; exit 1
