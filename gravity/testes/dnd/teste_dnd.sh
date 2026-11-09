#!/bin/sh
# Arrastar e soltar entre aplicativos num Orbit headless: um app do Início para a dock (com o dedo e com o mouse). sh teste_dnd.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
# (com o `make dock lancador` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
casa="$run/casa"
mkdir -p "$casa/.config/lumna" "$casa/.local/share/applications"
printf 'lumna ; Início ; @lancador ; lumna-lancador\nfolder ; Atlas ; lumna-atlas ; lumna-atlas\n-\nmic ; Emma ; lumna-emma-chat ; lumna-emma-chat\n' > "$casa/.config/lumna/dock"
cat > "$casa/.local/share/applications/teste-app.desktop" <<EOT
[Desktop Entry]
Type=Application
Name=App de Teste
Comment=Para o arraste
Exec=sh -c true
Icon=utilities-terminal
Categories=Utility;
EOT
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 XDG_DATA_HOME=$casa/.local/share XDG_DATA_DIRS=$casa/.local/share
unset XDG_CONFIG_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
LUMNA_DOCK_DEBUG=1 "$gravity/dock" > "$run/dock.out" 2> "$run/dock.err" &
sleep 2
"$gravity/lancador" > "$run/l.out" 2> "$run/l.err" &
sleep 3
python3 "$aqui/teste_dnd.py" "$orbit_dir" "$run" "$casa"
rc=$?
pkill -x orbit; pkill -x dock; pkill -x lancador
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/dock.err" "$run/l.err"; }
exit $rc
