#!/bin/sh
# O controle de ponta a ponta: controles de mentira (FIFOs) -> lumna-gamepad -> o wtype de verdade -> o Orbit -> o lumna-jogos, com a aba Configurações.
#   sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
python3 "$aqui/../jogos/fixtures.py" "$run"
casa="$run/casa"; mkdir -p "$casa" "$run/falsos" "$run/energia"
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
export PATH="$run/bin:$PATH" FALSO_LOG="$run/falso.log" LC_ALL=C RAIZ_TESTE="$run"
export LUMNA_STEAM_DIR="$run/steam" LUMNA_LUTRIS_DB="$run/lutris.db" LUMNA_HEROIC_DIR="$run/heroic"
export LUMNA_GAMEPAD_FALSOS="$run/falsos" LUMNA_POWER_DIR="$run/energia"
unset XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME LUMNA_GAMEPAD_FOCO
ln -sf "$gravity/jogos" "$run/bin/lumna-jogos"   # o processo se chama lumna-jogos (o Guia procura por esse nome)
mkfifo "$run/falsos/xbox.ev"
printf '[controle]\nnome=Xbox Wireless Controller\nuniq=AA:BB:CC:DD:EE:01\nbus=bluetooth\nrumble=sim\nbateria=73\n' > "$run/falsos/xbox.dev"
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 4
export WAYLAND_DISPLAY=wayland-0
"$gravity/gamepad" > "$run/pad.out" 2>&1 &
LUMNA_JOGOS_DEBUG=1 "$run/bin/lumna-jogos" > "$run/g.out" 2> "$run/g.err" &
sleep 3
python3 "$aqui/teste_ui.py" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x lumna-jogos; pkill -x gamepad
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/g.err"; tail -5 "$run/pad.out"; }
[ $rc -ne 0 ] || rm -rf "$run"
exit $rc
