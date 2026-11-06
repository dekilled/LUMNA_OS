#!/bin/sh
# lumna-ocioso e o vídeo tocando: sh ocioso.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY PROGRAMA_INIBIR (compilado de inibir.c)   (leva ~2,5 minutos: o tempo mínimo de ociosidade é 1 minuto)
# 1) com alguém pedindo para não trancar (D-Bus org.freedesktop.ScreenSaver, como o Firefox faz) passado 1 minuto não tranca;
# 2) quando solta, a contagem recomeça (não tranca na hora) e depois tranca.
orbit_dir=${1:?pasta do orbit}; g=${2:?pasta do gravity}; inibir=${3:?programa inibir}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/bin"
printf 'bloquear_apos = 1\nbloquear_suspender = nao\n' >"$run/cfg/lumna/bloqueio.ini"
# o "lumna-bloqueio" de mentira: só marca que foi chamado
printf '#!/bin/sh\ndate +%%s >> %s/trancou\nsleep 600\n' "$run" >"$run/bin/lumna-bloqueio"; chmod +x "$run/bin/lumna-bloqueio"
export XDG_RUNTIME_DIR=$run XDG_CONFIG_HOME=$run/cfg PATH="$run/bin:$PATH"
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 2
wl=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
dbus-run-session -- sh -c '
	export WAYLAND_DISPLAY='"$wl"'
	'"$g"'/ocioso >'"$run"'/ocioso.log 2>&1 &
	oc=$!
	sleep 1
	'"$inibir"' solta >'"$run"'/inibir.log 2>&1 &
	ib=$!
	sleep 75
	[ -f '"$run"'/trancou ] && echo "trancou-com-inibidor" >'"$run"'/etapas
	kill -USR1 $ib
	sleep 30
	[ -f '"$run"'/trancou ] && echo "trancou-cedo-demais" >>'"$run"'/etapas
	sleep 45
	[ -f '"$run"'/trancou ] && echo "trancou-depois" >>'"$run"'/etapas
	kill $oc $ib
'
cat "$run/etapas" 2>/dev/null
grep -q trancou-com-inibidor "$run/etapas" && fail "trancou com alguém pedindo para não trancar"
grep -q trancou-cedo-demais "$run/etapas" && fail "trancou na hora em que o vídeo parou (a contagem devia recomeçar)"
grep -q trancou-depois "$run/etapas" || fail "não trancou depois que o último soltou e o tempo passou"
kill $orbit 2>/dev/null; wait $orbit 2>/dev/null
[ $falhas -eq 0 ] && { echo "ocioso com vídeo: ok"; rm -rf "$run"; exit 0; }
cat "$run/ocioso.log" >&2; exit 1
