#!/bin/sh
# O lumna-jogos inteiro num Orbit headless, com Steam, Lutris e Heroic de mentira (a pasta do fixtures.py). sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY PASTA_DOS_FALSOS
orbit_dir=${1:?}; gravity=${2:?}; run=${3:?}
aqui=$(cd "$(dirname "$0")" && pwd)
casa="$run/casa"
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
export PATH="$run/bin:$PATH" FALSO_LOG="$run/falso.log" LC_ALL=C RAIZ_TESTE="$run"
export LUMNA_STEAM_DIR="$run/steam" LUMNA_LUTRIS_DB="$run/lutris.db" LUMNA_HEROIC_DIR="$run/heroic"
unset XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME
ln -sf "${JOGOS_BIN:-$gravity/jogos}" "$run/bin/lumna-jogos"  # o vigia abre "lumna-jogos" pelo PATH
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 4
export WAYLAND_DISPLAY=wayland-0
LUMNA_JOGOS_DEBUG=1 "${JOGOS_BIN:-$gravity/jogos}" > "$run/g.out" 2> "$run/g.err" &
sleep 3
python3 "$aqui/teste_ui.py" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x jogos
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -10 "$run/g.err"; }
exit $rc
