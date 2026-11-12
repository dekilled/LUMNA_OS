#!/bin/sh
# Trocar de modo, de espaço e de tela com janelas X11 abertas (um jogo no Gaming, a Steam no Desktop) não derruba o Orbit. A animação da troca recorta as
# janelas (wlr_scene_subsurface_tree_set_clip), e a janela X11 sem a árvore de subsuperfícies fazia o wlroots abortar: a sessão caía para o login.
# Depois de cada troca o jogo volta à frente no Gaming. Uso: sh gravity/testes/trocas_x11.sh <pasta do orbit> [binário, ex.: orbit-asan]
orbit=${1:-orbit}; bin=${2:-orbit}; aqui=$(cd "$(dirname "$0")" && pwd); g=$(cd "$aqui/.." && pwd)
command -v Xwayland >/dev/null || { echo "trocas_x11: dispensado (falta Xwayland)"; exit 0; }
python3 -c "import Xlib" 2>/dev/null || { echo "trocas_x11: dispensado (falta python3-xlib)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
[ -x "$g/jogos" ] && ln -s "$g/jogos" "$run/bin/lumna-jogos"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run" ASAN_OPTIONS=detect_leaks=0
cd "$orbit" || exit 1
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman ./$bin >"$run/log" 2>&1 & pid=$!
sleep 3
D=$(grep -o "DISPLAY=:[0-9]*" "$run/log" | head -1 | cut -d= -f2)
[ -n "$D" ] || { echo "trocas_x11: dispensado (esta wlroots não tem XWayland)"; kill $pid; rm -rf "$run"; exit 0; }
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-'); sock=$(ls "$run"/orbit.*.sock | head -1); ok=1
msg() { ./orbit-msg -s "$sock" "$@"; }
jan() { msg janelas | tr '{' '\n' | grep "\"app_id\":\"$1\""; }
campo() { jan "$1" | grep -o "\"$2\":[^,}]*" | head -1 | cut -d: -f2 | tr -d '"'; }
falha() { echo "FALHOU: $1" >&2; ok=0; }
vivo() { kill -0 $pid 2>/dev/null || { falha "o Orbit caiu: $1"; grep -E "Assertion|AddressSanitizer|runtime error" "$run/log" | head -3 >&2; kill $pid 2>/dev/null; pkill -f xcli.py; exit 1; }; }
xcli() { DISPLAY=$D python3 "$aqui/xwayland/xcli.py" "$@" >/dev/null 2>&1; }

msg modo gaming HEADLESS-2 >/dev/null; sleep 2
SteamGameId=730 xcli jan 00ff00 cs2 "$run/t" & sleep 2.5
xcli jan 334455 editorx "$run/s" & sleep 2   # uma janela X11 comum no Desktop da tela 1
[ "$(campo cs2 saida)" = HEADLESS-2 ] || falha "o jogo devia estar no Gaming ($(jan cs2))"
for passo in "modo midia HEADLESS-2" "modo gaming HEADLESS-2" "modo desktop HEADLESS-2" "modo gaming HEADLESS-2" \
	"espaco 2 HEADLESS-1" "espaco 1 HEADLESS-1" "trocar_telas HEADLESS-1 HEADLESS-2" "trocar_telas HEADLESS-1 HEADLESS-2" \
	"modo gaming HEADLESS-1" "modo gaming HEADLESS-2"; do
	msg $passo >/dev/null; sleep 1.2
	vivo "depois de '$passo' com janelas X11 abertas"
done
# no fim o Gaming está na tela 2 de novo, e o jogo na frente
sleep 1
[ "$(campo cs2 saida)" = HEADLESS-2 ] && [ "$(campo cs2 visivel)" = true ] || falha "depois das trocas o jogo devia estar na frente no Gaming ($(jan cs2))"
[ "$(campo editorx visivel)" = true ] || falha "a janela X11 do Desktop devia continuar aparecendo ($(jan editorx))"
kill $pid; pkill -f xcli.py; wait $pid 2>/dev/null; rm -rf "$run"
[ $ok = 1 ] && echo "trocas com janelas X11: ok" || exit 1
