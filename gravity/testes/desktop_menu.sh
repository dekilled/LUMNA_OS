#!/bin/sh
# O menu de um ícone da área de trabalho: Abrir com…, Abrir local do arquivo, Enviar para (Documentos, E-mail, nuvem, rede) e Enviar para dispositivo….
# Uso: sh testes/desktop_menu.sh PASTA_DO_ORBIT (com o make desktop feito)
orbit_dir=${1:?}
aqui=$(cd "$(dirname "$0")" && pwd); gravity=$(dirname "$aqui")
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 LUMNA_DESKTOP_DEBUG=1
unset XDG_CONFIG_HOME XDG_DATA_HOME
mkdir -p "$run/Desktop/Projetos" "$run/bin" "$run/Documentos" "$run/Nuvem/Conta" "$run/.config/lumna/atlas" "$run/.local/share/applications"
printf 'XDG_DOCUMENTS_DIR="$HOME/Documentos"\n' > "$run/.config/user-dirs.dirs"
printf oi > "$run/Desktop/nota.txt"
# uma conta na nuvem (a pasta local dela), um cliente de e-mail, um Atlas e um aplicativo de mentira que anotam o que receberam
printf '[conta000]\nnome=Minha nuvem\ntipo=webdav\nremote=minha-nuvem\npasta=%s/Nuvem/Conta\n' "$run" > "$run/.config/lumna/atlas/rede.ini"
printf '#!/bin/sh\necho "$@" >> %s/email.log\n' "$run" > "$run/bin/xdg-email"
printf '#!/bin/sh\necho "$1" > %s/atlas-abriu\n' "$run" > "$run/bin/lumna-atlas"
chmod +x "$run/bin/"*
printf '[Desktop Entry]\nType=Application\nName=Teste Abridor\nExec=sh -c '"'"'echo "$0" >> %s/aberto.log'"'"' %%f\nMimeType=text/plain;\n' "$run" > "$run/.local/share/applications/teste-abridor.desktop"
printf '[MIME Cache]\ntext/plain=teste-abridor.desktop;\n' > "$run/.local/share/applications/mimeinfo.cache"
export PATH="$run/bin:$PATH"
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
"$gravity/desktop" > "$run/dk.out" 2> "$run/dk.err" & dk=$!
sleep 2
rc=0
python3 "$aqui/desktop/teste_menu.py" "$orbit_dir" "$run" || rc=$?
if kill -0 $dk 2>/dev/null; then echo "desktop: vivo"; else wait $dk; echo "desktop: MORREU (status $?)"; rc=1; fi
kill $dk 2>/dev/null; pkill -x orbit
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/dk.err"; }
[ $rc -eq 0 ] && echo "desktop_menu: ok"
exit $rc
