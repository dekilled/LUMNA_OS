#!/bin/sh
# A Loja do Gaming inteira num Orbit headless, com a loja da Steam de mentira (respostas reais gravadas) e a Steam/steam://install de mentira.
#   sh teste_loja.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
python3 "$aqui/fixtures.py" "$run"
casa="$run/casa"; mkdir -p "$casa"
porta=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
python3 "$aqui/servidor_steam_falso.py" "$porta" "$run/pedidos.log" &
srv=$!
export XDG_RUNTIME_DIR=$run HOME=$casa XDG_CACHE_HOME="$run/cache" WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
export PATH="$run/bin:$PATH" FALSO_LOG="$run/falso.log" LC_ALL=C RAIZ_TESTE="$run"
export LUMNA_STEAM_DIR="$run/steam" LUMNA_LUTRIS_DB="$run/lutris.db" LUMNA_HEROIC_DIR="$run/heroic"
export LUMNA_LOJA_STEAM_URL="http://127.0.0.1:$porta" LUMNA_LOJA_PROTONDB_URL="http://127.0.0.1:$porta" LUMNA_LOJA_IMG_URL="http://127.0.0.1:$porta" LUMNA_LOJA_API_URL="http://127.0.0.1:$porta"
unset XDG_CONFIG_HOME XDG_DATA_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 4
export WAYLAND_DISPLAY=wayland-0
LUMNA_JOGOS_DEBUG=1 "$gravity/jogos" --tela loja > "$run/g.out" 2> "$run/g.err" &
sleep 4
python3 "$aqui/teste_loja.py" "$orbit_dir" "$run" "$casa" "$gravity/"
rc=$?
pkill -x orbit; pkill -x jogos; kill $srv 2>/dev/null
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/g.err"; }
[ $rc -ne 0 ] || rm -rf "$run"
exit $rc
