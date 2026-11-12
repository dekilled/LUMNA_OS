#!/bin/sh
# O componente de modos como um só (dois monitores sem tela): monitor que sai, a ilha do modo, a Mídia que não fica vazia.
# Uso: sh testes/modos_componente.sh <pasta do orbit> [binário, ex.: orbit-asan] (com make calculadora midia)
orbit=${1:?}; bin=${2:-orbit}; gravity=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
# o que o Orbit chama pelo nome: a Mídia de verdade; o resto não abre nada
ln -s "$gravity/midia" "$run/bin/lumna-midia"
printf '#!/bin/sh\nexit 0\n' > "$run/bin/lumna-jogos"; chmod +x "$run/bin/lumna-jogos"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run" XDG_STATE_HOME="$run/home/.local/state" ASAN_OPTIONS=detect_leaks=0
unset XDG_DATA_HOME
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./$bin >"$run/log" 2>&1 &)
sleep 2.5
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$')
rc=0
python3 "$(dirname "$0")/modos/teste_componente.py" "$orbit" "$run" "$gravity" || rc=$?
pkill -x "$bin"; pkill -f "$gravity/midia"; pkill -x calculadora; pkill -x signals
[ $rc -eq 0 ] || { echo "(log em $run)"; grep -E 'ativação|ficou vazia|saída' "$run/log" | tail -5; }
echo "rc=$rc"
exit $rc
