#!/bin/sh
# A Loja inteira num Orbit headless, com pacman, checkupdates, curl e paru de mentira. sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY (com o `make loja` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
casa="$run/casa"
mkdir -p "$casa/.config"
sh "$aqui/falsos.sh" "$run/f"
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
export PATH="$run/f/bin:$PATH" FALSO_LOG="$run/pacman.log" LUMNA_SEM_PKEXEC=1 LC_ALL=C
unset XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 4
export WAYLAND_DISPLAY=wayland-0
LUMNA_LOJA_DEBUG=1 "${LOJA_BIN:-$gravity/loja}" > "$run/g.out" 2> "$run/g.err" &
sleep 3
python3 "$aqui/${TESTE:-teste_ui.py}" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x loja
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -10 "$run/g.err"; }
exit $rc
