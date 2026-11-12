#!/bin/sh
# A área de trabalho (ícones e pastas) num Orbit headless. Uso: sh testes/desktop_app.sh PASTA_DO_ORBIT (com o make desktop feito)
orbit_dir=${1:?}
aqui=$(cd "$(dirname "$0")" && pwd); gravity=$(dirname "$aqui")
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 LUMNA_DESKTOP_DEBUG=1 LUMNA_DESKTOP_SEM_ESPERA=1
unset XDG_CONFIG_HOME XDG_DATA_HOME
mkdir -p "$run/Desktop/Projetos" "$run/Desktop/Fotos" "$run/bin"
printf oi > "$run/Desktop/nota.txt"; printf x > "$run/Desktop/relatorio.pdf"; printf x > "$run/Desktop/um arquivo com um nome bem comprido que quebra.png"
printf '[Desktop Entry]\nType=Application\nName=Meu App\nExec=touch %s/lancou\n' "$run" > "$run/Desktop/meuapp.desktop"
# o Atlas de mentira: guarda o que pediram para abrir
printf '#!/bin/sh\necho "$1" > %s/atlas-abriu\n' "$run" > "$run/bin/lumna-atlas"; chmod +x "$run/bin/lumna-atlas"
export PATH="$run/bin:$PATH"
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
"$gravity/desktop" > "$run/dk.out" 2> "$run/dk.err" & dk=$!
sleep 2
rc=0
python3 "$aqui/desktop/teste_app.py" "$orbit_dir" "$run" "$gravity" || rc=$?
if kill -0 $dk 2>/dev/null; then echo "desktop: vivo"; else wait $dk; echo "desktop: MORREU (status $?)"; fi
kill $dk 2>/dev/null; pkill -x orbit
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/dk.err"; }
echo "rc=$rc"
exit $rc
