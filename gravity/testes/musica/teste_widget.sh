#!/bin/sh
# O widget do LUMNA Music na área de trabalho: some enquanto a janela do Music está aberta, aparece quando ela é fechada (o Music segue tocando em segundo plano), os botões
# mandam no Music e tocar no cartão abre a janela de volta. sh teste_widget.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY (com o `make widgets musica` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
if [ -z "$LUMNA_WIDGET_DBUS" ]; then
	LUMNA_WIDGET_DBUS=1 exec dbus-run-session -- sh "$0" "$@"
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
mkdir -p "$run/bin" && ln -sf "$MUSICA_BIN" "$run/bin/lumna-music" && export PATH="$run/bin:$PATH"
LUMNA_WIDGETS_DEBUG=1 "${WIDGETS_BIN:-$gravity/widgets}" > "$run/w.out" 2> "$run/w.err" &
sleep 2
LUMNA_MUSICA_DEBUG=1 "$MUSICA_BIN" "$casa/longa.mp3" > "$run/g.out" 2> "$run/g.err" &
sleep 3
python3 "$aqui/teste_widget.py" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x musica; pkill -x widgets; pkill -f "lumna-musica-"
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/w.err"; }
exit $rc
