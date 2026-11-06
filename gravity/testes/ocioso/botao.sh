#!/bin/sh
# O botão de ligar (XF86PowerOff) no Orbit: roda o session/lumna-botao-ligar, que faz o que o energia.ini diz.
# sh botao.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; g=${2:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/bin"
ln -s "$g/../session/lumna-botao-ligar" "$run/bin/lumna-botao-ligar"
printf '#!/bin/sh\necho "energia aberto" >> %s/acoes.log\n' "$run" > "$run/bin/lumna-energia"
printf '#!/bin/sh\necho "systemctl $@" >> %s/acoes.log\n' "$run" > "$run/bin/systemctl"
chmod +x "$run/bin/lumna-energia" "$run/bin/systemctl"
export XDG_RUNTIME_DIR=$run XDG_CONFIG_HOME=$run/cfg PATH="$run/bin:$PATH"
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 2
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
aperta() { wtype -k XF86PowerOff; sleep 0.8; }
ultimo() { tail -n1 "$run/acoes.log" 2>/dev/null; }
aperta; [ "$(ultimo)" = "energia aberto" ] || fail "sem energia.ini o botão devia abrir o menu (perguntar): '$(ultimo)'"
for par in desligar:poweroff suspender:suspend hibernar:hibernate; do
	printf 'botao = %s\n' "${par%%:*}" > "$run/cfg/lumna/energia.ini"
	aperta; [ "$(ultimo)" = "systemctl ${par##*:}" ] || fail "botao = ${par%%:*} devia rodar systemctl ${par##*:}: '$(ultimo)'"
done
n=$(wc -l < "$run/acoes.log")
printf 'botao = nada\n' > "$run/cfg/lumna/energia.ini"
aperta; [ "$(wc -l < "$run/acoes.log")" = "$n" ] || fail "botao = nada não devia fazer nada"
printf 'botao = perguntar\n' > "$run/cfg/lumna/energia.ini"
aperta; [ "$(ultimo)" = "energia aberto" ] || fail "botao = perguntar devia abrir o menu: '$(ultimo)'"
kill $orbit 2>/dev/null; wait 2>/dev/null
[ $falhas -eq 0 ] && { echo "botão de ligar: ok"; rm -rf "$run"; exit 0; }
tail -3 "$run/orbit.log" >&2; exit 1
