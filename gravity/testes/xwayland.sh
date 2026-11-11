#!/bin/sh
# XWayland no Orbit, com clientes X11 de teste (python3-xlib): render, teclado, tela cheia, menu sem moldura, fechar e o jogo da Steam indo para o Gaming.
# Precisa de Xwayland, python3-xlib, grim, wtype e PIL; se faltar algo, ou a wlroots não tem XWayland, o teste se dispensa.
# Uso: sh gravity/testes/xwayland.sh <pasta do orbit>
orbit=${1:-orbit}; aqui=$(cd "$(dirname "$0")" && pwd)
for c in Xwayland grim wtype; do command -v $c >/dev/null || { echo "xwayland: dispensado (falta $c)"; exit 0; }; done
python3 -c "import Xlib, PIL" 2>/dev/null || { echo "xwayland: dispensado (falta python3-xlib ou PIL)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
[ -x "$aqui/../jogos" ] && ln -s "$aqui/../jogos" "$run/bin/lumna-jogos"
export PATH="$run/bin:$PATH"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
cd "$orbit" || exit 1
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman ./orbit >"$run/log" 2>&1 & pid=$!
sleep 2
D=$(grep -o "DISPLAY=:[0-9]*" "$run/log" | head -1 | cut -d= -f2)
[ -n "$D" ] || { echo "xwayland: dispensado (esta wlroots não tem XWayland)"; kill $pid; rm -rf "$run"; exit 0; }
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-'); sock=$(ls "$run"/orbit.*.sock | head -1); ok=1
msg() { ./orbit-msg -s "$sock" "$@"; }
jan() { msg janelas | tr '{' '\n' | grep "\"app_id\":\"$1\""; }
campos() { jan "$1" | grep -o '"saida":"[^"]*"\|"espaco":[0-9]*\|"na_dock":[a-z]*\|"ecra":[a-z]*\|"w":[0-9]*\|"h":[0-9]*' | paste -sd' '; }
falha() { echo "FALHOU: $1" >&2; ok=0; }
DISPLAY=$D python3 "$aqui/xwayland/xcli.py" jan ff0000 Vermelha "$run/teclas" & sleep 4
jan Vermelha | grep -q '"titulo":"X Vermelha"' || falha "a janela X11 não apareceu como janela do Orbit (classe como app_id, título)"
pos=$(jan Vermelha | grep -o '"x":[-0-9]*,"y":[-0-9]*,"w":[0-9]*,"h":[0-9]*' | tr -dc '0-9,-')
grim "$run/a.png"; python3 - "$run/a.png" "$pos" <<PY || falha "a janela X11 não desenhou"
import sys
from PIL import Image
x, y, w, h = [int(v) for v in sys.argv[2].split(",")]
im = Image.open(sys.argv[1]).convert("RGB")
sys.exit(0 if im.getpixel((x + w // 2, y + h // 2)) == (255, 0, 0) else 1)
PY
wtype x >/dev/null 2>&1; sleep 1; grep -q tecla "$run/teclas" 2>/dev/null || falha "o teclado não chegou na janela X11"
id=$(jan Vermelha | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
msg ecra "$id" >/dev/null; sleep 2
campos Vermelha | grep -q '"ecra":true.*"w":1280.*"h":720' || falha "tela cheia da janela X11 ($(campos Vermelha))"
DISPLAY=$D python3 "$aqui/xwayland/xcli.py" or 00ff00 300 200 & sleep 2
grim "$run/b.png"; python3 - "$run/b.png" <<PY || falha "o menu sem moldura não apareceu na posição pedida"
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
sys.exit(0 if im.getpixel((350, 230)) == (0, 255, 0) and im.getpixel((600, 400)) != (0, 255, 0) else 1)
PY
msg fechar "$id" >/dev/null; sleep 1.5
[ -z "$(jan Vermelha)" ] || falha "a janela X11 não fechou"
pkill -f xcli.py
# o jogo da Steam (a marca SteamGameId no ambiente do processo) vai para a tela do Gaming; o mesmo app sem a marca fica onde está
msg modo gaming HEADLESS-2 >/dev/null; sleep 0.5
# com a biblioteca do Gaming (que tem o foco) já aberta nessa tela, se existir o binário: o jogo que abre por cima dela tem de aparecer logo, e não só depois
SteamGameId=730 DISPLAY=$D python3 "$aqui/xwayland/xcli.py" jan 00ff00 csgo_linux64 "$run/t1" & sleep 2.5
[ -x "$aqui/../jogos" ] && { jan lumna-jogos | grep -q '"visivel":false' || falha "a biblioteca do Gaming devia ficar por trás do jogo que abriu ($(campos lumna-jogos))"; jan csgo_linux64 | grep -q '"visivel":true' || falha "o jogo devia aparecer logo ao abrir, por cima da biblioteca"; }
sleep 1.5
campos csgo_linux64 | grep -q '"saida":"HEADLESS-2".*"espaco":4.*"na_dock":false.*"w":1280.*"h":720' || falha "o jogo da Steam devia ir, isolado e do tamanho da tela, para o Gaming ($(campos csgo_linux64))"
DISPLAY=$D python3 "$aqui/xwayland/xcli.py" jan ff0000 outro_app "$run/t2" & sleep 4
campos outro_app | grep -q '"saida":"HEADLESS-1".*"espaco":1' || falha "um app X11 comum devia ficar no Desktop ($(campos outro_app))"
kill $pid; pkill -f xcli.py; wait $pid 2>/dev/null; rm -rf "$run"
[ $ok = 1 ] && echo "xwayland: ok" || exit 1
