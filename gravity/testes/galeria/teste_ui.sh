#!/bin/sh
# (o player de vídeo fala MPRIS: precisa de um D-Bus de sessão; o mpv toca sem som, LUMNA_GALERIA_AO=null)
[ -n "$LUMNA_GALERIA_DBUS" ] || { LUMNA_GALERIA_DBUS=1 exec dbus-run-session -- sh "$0" "$@"; }
export LUMNA_GALERIA_AO=null
# A Galeria inteira num Orbit headless. sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY (com o `make galeria` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
casa="$run/casa"
mkdir -p "$casa/.config"
python3 "$aqui/fixtures.py" "$casa" >/dev/null || exit 1
printf 'XDG_PICTURES_DIR="$HOME/Imagens"\nXDG_VIDEOS_DIR="$HOME/Vídeos"\n' > "$casa/.config/user-dirs.dirs"
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
unset XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME XDG_PICTURES_DIR XDG_VIDEOS_DIR
(cd "$orbit_dir" && ORBIT_LOG_DEBUG=${ORBIT_LOG_DEBUG:-} ./orbit > "$run/orbit.log" 2>&1 &)
sleep 4
export WAYLAND_DISPLAY=wayland-0
LUMNA_GALERIA_DEBUG=1 "${GALERIA_BIN:-$gravity/galeria}" > "$run/g.out" 2> "$run/g.err" &
sleep 3
python3 "$aqui/${TESTE:-teste_ui.py}" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x galeria; pkill -x mpv
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -10 "$run/g.err"; }
exit $rc
