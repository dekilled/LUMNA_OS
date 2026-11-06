#!/bin/sh
# lumna-inicio com um Orbit headless: roda o autostart, reabre a última sessão e guarda a sessão de agora.
# sh inicio.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY PROGRAMA_JANELA   (leva ~20 s: a sessão se guarda de 15 em 15 s)
orbit_dir=${1:?}; g=${2:?}; janela=${3:?}
run=$(mktemp -d); chmod 700 "$run"
mkdir -p "$run/cfg/lumna" "$run/cfg/autostart" "$run/share/applications" "$run/state/lumna"
export XDG_RUNTIME_DIR=$run XDG_CONFIG_HOME=$run/cfg XDG_DATA_HOME=$run/share XDG_DATA_DIRS=$run/share XDG_STATE_HOME=$run/state XDG_CONFIG_DIRS=$run/etc
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
printf '[Desktop Entry]\nType=Application\nName=Auto\nExec=touch %s/ran-auto\n' "$run" >"$run/cfg/autostart/auto.desktop"
printf '[Desktop Entry]\nType=Application\nName=Notas\nExec=touch %s/ran-notas\n' "$run" >"$run/share/applications/org.exemplo.Notas.desktop"
printf '[Desktop Entry]\nType=Application\nName=Outro\nExec=touch %s/ran-outro\n' "$run" >"$run/share/applications/outro.desktop"
printf 'reabrir = sim\n' >"$run/cfg/lumna/inicio.ini"
printf 'org.exemplo.Notas.desktop\n' >"$run/state/lumna/sessao.list"
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 2
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
LUMNA_INICIO_ESPERA=0 "$g/inicio" >"$run/inicio.log" 2>&1 &
ini=$!
sleep 2
[ -f "$run/ran-auto" ] || fail "o autostart devia ter rodado"
[ -f "$run/ran-notas" ] || fail "a sessão antiga devia ter reaberto o Notas"
[ -f "$run/ran-outro" ] && fail "o Outro não estava na sessão: não devia abrir"
# a sessão de agora: uma janela do Outro (app_id outro) abre, e em até 15 s a lista passa a ser só ela
"$janela" outro >/dev/null 2>&1 &
jan=$!
sleep 18
grep -qx "outro.desktop" "$run/state/lumna/sessao.list" || fail "a sessão devia ter guardado o outro.desktop (lista: $(cat "$run/state/lumna/sessao.list" | tr '\n' ' '))"
grep -q "Notas" "$run/state/lumna/sessao.list" && fail "o Notas (já fechado) devia ter saído da lista"
# fechar tudo: a lista não se apaga na hora (as janelas fecham juntas ao sair), só depois de um minuto
kill $jan 2>/dev/null
sleep 17
grep -qx "outro.desktop" "$run/state/lumna/sessao.list" || fail "a lista vazia não devia apagar a sessão logo"
kill $ini $orbit 2>/dev/null; wait 2>/dev/null
[ $falhas -eq 0 ] && { echo "lumna-inicio: ok"; rm -rf "$run"; exit 0; }
cat "$run/inicio.log" >&2; exit 1
