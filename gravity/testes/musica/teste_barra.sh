#!/bin/sh
# A topbar com o ícone do LUMNA Music: aparece quando o Music toca (mesmo sem janela), tocar nele abre o painel com os controles, que mandam no Music; Fechar o Music encerra o app.
# sh teste_barra.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY (com o `make barra musica` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
if [ -z "$LUMNA_BARRA_DBUS" ]; then
	LUMNA_BARRA_DBUS=1 exec dbus-run-session -- sh "$0" "$@"
fi
run=$(mktemp -d); chmod 700 "$run"
casa="$run/casa"
mkdir -p "$casa/.config"
python3 "$aqui/fixtures.py" "$casa" >/dev/null || exit 1
printf 'XDG_MUSIC_DIR="$HOME/Músicas"\n' > "$casa/.config/user-dirs.dirs"
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 LUMNA_MUSICA_AO=null
unset XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME XDG_MUSIC_DIR
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 4
export WAYLAND_DISPLAY=wayland-0
export MUSICA_BIN=${MUSICA_BIN:-$gravity/musica}
# a barra abre o app pelo nome de instalação (lumna-music)
mkdir -p "$run/bin" && ln -sf "$MUSICA_BIN" "$run/bin/lumna-music" && export PATH="$run/bin:$PATH"
LUMNA_BARRA_DEBUG=1 "${BARRA_BIN:-$gravity/barra}" > "$run/b.out" 2> "$run/b.err" &
sleep 2.5
LUMNA_MUSICA_DEBUG=1 "$MUSICA_BIN" "$casa/longa.mp3" > "$run/g.out" 2> "$run/g.err" &
sleep 3
python3 "$aqui/teste_barra.py" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x musica; pkill -x barra; pkill -f "lumna-musica-"
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/b.err"; }
exit $rc
