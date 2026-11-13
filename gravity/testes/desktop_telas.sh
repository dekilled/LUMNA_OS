#!/bin/sh
# A área de trabalho em dois monitores (uma por tela, o ícone mora numa só) e a pasta própria do Desktop (o xdg-user-dirs apontando para a
# pasta pessoal não pode virar ícones). Uso: sh testes/desktop_telas.sh PASTA_DO_ORBIT (com o make desktop feito)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home/.cache" "$run/home/Projetos"
printf 'XDG_DESKTOP_DIR="$HOME/"\nXDG_DOCUMENTS_DIR="$HOME/Documentos"\n' > "$run/cfg/user-dirs.dirs"
printf lixo > "$run/home/arquivo-da-casa.txt"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run" LUMNA_DESKTOP_DEBUG=1
unset XDG_DATA_HOME XDG_STATE_HOME
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$')
"$gravity/desktop" > "$run/dk.out" 2> "$run/dk.err" & dk=$!
sleep 3
rc=0
python3 "$(dirname "$0")/desktop/teste_telas.py" "$orbit" "$run" "$gravity" || rc=$?
kill -0 $dk 2>/dev/null && echo "desktop: vivo" || echo "desktop: MORREU"
kill $dk 2>/dev/null; pkill -x orbit; pkill -x desktop
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -4 "$run/dk.err"; }
exit $rc
