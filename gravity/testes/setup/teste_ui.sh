#!/bin/sh
# O pré-setup inteiro num Orbit headless. sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY (com o `make setup` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
casa="$run/casa"; mkdir -p "$casa/.config"
sh "$aqui/falsos.sh" "$run/f"
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
export PATH="$run/f/bin:$PATH" FALSO_LOG="$run/falso.log" LC_ALL=C
unset XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME
python3 "$aqui/celular_emma_falsos.py" "$run" &
fal=$!
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 4
export WAYLAND_DISPLAY=wayland-0
LUMNA_SETUP_DEBUG=1 "${SETUP_BIN:-$gravity/setup}" --login > "$run/g.out" 2> "$run/g.err" &
sleep 3
python3 "$aqui/teste_ui.py" "$orbit_dir" "$run" "$casa" "$gravity"
rc=$?
pkill -x orbit; pkill -x setup; kill $fal 2>/dev/null
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -10 "$run/g.err"; }
exit $rc
