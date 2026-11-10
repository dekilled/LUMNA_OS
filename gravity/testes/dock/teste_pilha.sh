#!/bin/sh
# A lista das janelas de um app na dock (vários Atlas abertos): fechar uma janela pela lista encolhe a lista, e o hover tem de acompanhar as linhas.
#   sh teste_pilha.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY (com o `make dock atlas` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
casa="$run/casa"
mkdir -p "$casa/.config/lumna" "$casa/Documentos"
printf 'lumna ; Início ; @lancador ; lumna-lancador\nfolder ; Atlas ; lumna-atlas ; lumna-atlas\nglobe ; Navegador ; firefox ; firefox\n' > "$casa/.config/lumna/dock"
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
unset XDG_CONFIG_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
LUMNA_DOCK_DEBUG=1 "$gravity/dock" > "$run/dock.out" 2> "$run/dock.err" &
sleep 2.5
for i in 1 2 3 4; do "$gravity/atlas" "$casa/Documentos" > /dev/null 2>&1 & sleep 1.2; done
python3 "$aqui/teste_pilha.py" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x dock; pkill -x atlas
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -10 "$run/dock.err"; }
exit $rc
