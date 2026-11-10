#!/bin/sh
# O LUMNA Music inteiro num Orbit headless, com um D-Bus de sessão de mentira e o mpv com saída nula.
# sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY   (com o `make musica` feito; TESTE=outro.py troca o roteiro)
if [ -z "$LUMNA_MUSICA_DBUS" ]; then
	LUMNA_MUSICA_DBUS=1 exec dbus-run-session -- sh "$0" "$@"
fi
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
casa="$run/casa"
mkdir -p "$casa/.config"
python3 "$aqui/fixtures.py" "$casa" >/dev/null || exit 1
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
export LUMNA_MUSICA_AO=null
printf 'XDG_MUSIC_DIR="$HOME/Músicas"\n' > "$casa/.config/user-dirs.dirs"
unset XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME XDG_MUSIC_DIR
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 4
export WAYLAND_DISPLAY=wayland-0
export MUSICA_BIN=${MUSICA_BIN:-$gravity/musica}
# MUSICA_ARG: um arquivo de música dado ao app ao abrir, como faz o gerenciador de arquivos (@CASA@ vira a casa do teste)
marg=$(printf %s "$MUSICA_ARG" | sed "s|@CASA@|$casa|")
if [ -n "$marg" ]; then
	LUMNA_MUSICA_DEBUG=1 "$MUSICA_BIN" "$marg" > "$run/g.out" 2> "$run/g.err" &
else
	LUMNA_MUSICA_DEBUG=1 "$MUSICA_BIN" > "$run/g.out" 2> "$run/g.err" &
fi
sleep 3
python3 "$aqui/${TESTE:-teste_ui.py}" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x musica; pkill -f "lumna-musica-"
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -10 "$run/g.err"; }
exit $rc
