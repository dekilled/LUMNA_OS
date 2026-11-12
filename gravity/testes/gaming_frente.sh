#!/bin/sh
# O Gaming manda na tela: o jogo abre em tela cheia, com o foco e com o cursor nele, mesmo com o cursor no outro monitor; a Steam (uma janela que abre depois,
# ou que pede o foco) não o tira da frente; um clique no Desktop do outro monitor também não; ao fechar o jogo volta a biblioteca, não a Steam; a tela de
# Gaming nunca mostra o Desktop (papel de parede, barra, dock); vazia, a biblioteca volta sozinha. E um jogo Wayland (com a marca da Steam) também é jogo.
# Clientes X11 de teste (python3-xlib). Uso: sh gravity/testes/gaming_frente.sh <pasta do orbit>
orbit=${1:-orbit}; aqui=$(cd "$(dirname "$0")" && pwd); g=$(cd "$aqui/.." && pwd)
for c in Xwayland grim; do command -v $c >/dev/null || { echo "gaming_frente: dispensado (falta $c)"; exit 0; }; done
python3 -c "import Xlib, PIL" 2>/dev/null || { echo "gaming_frente: dispensado (falta python3-xlib ou PIL)"; exit 0; }
[ -x "$g/jogos" ] || { echo "gaming_frente: falta o make jogos"; exit 1; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
ln -s "$g/jogos" "$run/bin/lumna-jogos"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
cd "$orbit" || exit 1
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman ./orbit >"$run/log" 2>&1 & pid=$!
sleep 2
D=$(grep -o "DISPLAY=:[0-9]*" "$run/log" | head -1 | cut -d= -f2)
[ -n "$D" ] || { echo "gaming_frente: dispensado (esta wlroots não tem XWayland)"; kill $pid; rm -rf "$run"; exit 0; }
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-'); sock=$(ls "$run"/orbit.*.sock | head -1); ok=1
msg() { ./orbit-msg -s "$sock" "$@"; }
jan() { msg janelas | tr '{' '\n' | grep "\"app_id\":\"$1\""; }
campo() { jan "$1" | grep -o "\"$2\":[^,}]*" | head -1 | cut -d: -f2 | tr -d '"'; }
ponteiro() { msg saidas | tr '{' '\n' | grep '"ponteiro":true' | grep -o '"nome":"[^"]*"' | cut -d'"' -f4; }
camada() { msg camadas | tr '{' '\n' | grep "\"nome\":\"$1\"" | grep "\"saida\":\"$2\"" | grep -o '"visivel":[a-z]*' | cut -d: -f2; }
falha() { echo "FALHOU: $1" >&2; ok=0; }
espera() { i=0; while [ $i -lt ${2:-30} ]; do eval "$1" && return 0; sleep 0.2; i=$((i + 1)); done; return 1; }
xcli() { DISPLAY=$D python3 "$aqui/xwayland/xcli.py" "$@"; }

# o Desktop nas duas telas tem papel de parede (a camada de fundo do LUMNA); a tela 2 entra em Gaming com a biblioteca
"$g/papel" >/dev/null 2>&1 & papel1=$!
msg modo gaming HEADLESS-2 >/dev/null
espera '[ "$(campo lumna-jogos visivel)" = true ]' 50 || falha "a biblioteca do Gaming devia abrir na tela 2"
# o papel de parede da tela 2 (se o lumna-papel abriu uma camada lá) não aparece no Gaming; o da tela 1 sim
sleep 1
[ "$(camada lumna-papel HEADLESS-1)" = true ] || falha "o papel de parede do Desktop (tela 1) devia aparecer ($(msg camadas))"
[ -z "$(camada lumna-papel HEADLESS-2)" ] || [ "$(camada lumna-papel HEADLESS-2)" = false ] || falha "o papel de parede não devia aparecer na tela do Gaming"

# 1. o cursor está no Desktop (tela 1); a Steam já está aberta no Gaming, e o jogo abre (marca SteamGameId)
dx=$(msg saidas | tr '{' '\n' | grep '"nome":"HEADLESS-1"' | grep -o '"x":[0-9-]*' | head -1 | cut -d: -f2)
msg mouse mover $((dx + 300)) 300 >/dev/null
[ "$(ponteiro)" = HEADLESS-1 ] || falha "o teste devia começar com o cursor no Desktop (está em $(ponteiro))"
xcli jan 334455 steam "$run/ts" & sleep 2
SteamGameId=730 xcli jan 00ff00 cs2 "$run/tj" & sleep 2.5
[ "$(campo cs2 jogo)" = true ] || falha "o cs2 (SteamGameId no ambiente) devia ser reconhecido como jogo"
[ "$(campo cs2 saida)" = HEADLESS-2 ] && [ "$(campo cs2 ecra)" = true ] && [ "$(campo cs2 w)" = 1280 ] || falha "o jogo devia abrir na tela do Gaming, em tela cheia ($(jan cs2))"
[ "$(campo cs2 foco)" = true ] && [ "$(campo cs2 visivel)" = true ] || falha "o jogo devia abrir na frente e com o foco ($(jan cs2))"
[ "$(ponteiro)" = HEADLESS-2 ] || falha "o cursor devia ir para a tela do jogo (está em $(ponteiro))"

# 2. a Steam abre outra janela (lista de amigos) e pede o foco: o jogo continua na frente e com o foco
xcli jan 556677 steam "$run/ts2" ativar=0.8 & sleep 2.5
[ "$(campo cs2 visivel)" = true ] && [ "$(campo cs2 foco)" = true ] || falha "uma janela nova da Steam (e o pedido de foco dela) não devia tirar o jogo da frente ($(jan cs2))"

# 3. um clique no Desktop da tela 1 (uma janela comum lá): a tela do Gaming continua mostrando o jogo
xcli jan ff0000 editor "$run/te" & sleep 2
[ "$(campo editor saida)" = HEADLESS-1 ] || falha "um app comum devia abrir no Desktop ($(jan editor))"
idj=$(campo editor id); msg foco "$idj" >/dev/null; sleep 0.8
[ "$(campo cs2 visivel)" = true ] || falha "com o foco no outro monitor, a tela do Gaming devia continuar mostrando o jogo ($(jan cs2))"
grim -o HEADLESS-2 "$run/g.png" 2>/dev/null || grim "$run/g.png"
python3 - "$run/g.png" <<PY || falha "a tela do Gaming devia estar mostrando o jogo (verde)"
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
x = im.width - 640 if im.width > 1280 else 640
sys.exit(0 if im.getpixel((x, 360)) == (0, 255, 0) else 1)
PY

# 4. o jogo fecha: volta a biblioteca (e não a Steam), com o foco
idg=$(campo cs2 id); msg fechar "$idg" >/dev/null; sleep 1.5
[ "$(campo lumna-jogos visivel)" = true ] || falha "ao fechar o jogo devia voltar a biblioteca ($(jan lumna-jogos))"

# 5. sem nada na tela do Gaming (a biblioteca e a Steam fecham): a biblioteca volta sozinha; o Desktop não aparece nem enquanto isso
for a in steam lumna-jogos; do for id in $(jan $a | grep -o '"id":[0-9]*' | cut -d: -f2); do msg fechar "$id" >/dev/null; done; done
pkill -f "xcli.py jan 334455"; pkill -f "xcli.py jan 556677"; sleep 0.5
espera '[ "$(campo lumna-jogos visivel)" = true ]' 50 || falha "a tela do Gaming ficou vazia: a biblioteca devia voltar sozinha"
[ "$(camada lumna-papel HEADLESS-2)" != true ] || falha "o papel de parede não devia aparecer na tela do Gaming vazia"

# 6. um jogo Wayland (o processo tem a marca da Steam): vai para o Gaming, é jogo e fica na frente da biblioteca (o Atlas faz de jogo)
SteamGameId=99 "$g/atlas" "$run/home" >/dev/null 2>&1 & at=$!
espera '[ "$(campo lumna-atlas jogo)" = true ]' 50 || falha "uma janela Wayland com SteamGameId devia ser jogo ($(jan lumna-atlas))"
[ "$(campo lumna-atlas saida)" = HEADLESS-2 ] && [ "$(campo lumna-atlas visivel)" = true ] || falha "o jogo Wayland devia abrir na frente, no Gaming ($(jan lumna-atlas))"
kill $at 2>/dev/null
# uma camada overlay (o gerenciador de processos, os avisos) continua aparecendo por cima do Gaming
gx=$(msg saidas | tr '{' '\n' | grep '"nome":"HEADLESS-2"' | grep -o '"x":[0-9-]*' | head -1 | cut -d: -f2)
msg mouse mover $((gx + 300)) 300 >/dev/null
"$g/processos" janela >/dev/null 2>&1 & sleep 1.5
[ "$(camada lumna-processos HEADLESS-2)" = true ] || falha "o gerenciador de processos (overlay) devia aparecer na tela do Gaming ($(msg camadas))"
pkill -f "processos janela"
kill $pid $papel1 2>/dev/null; pkill -f xcli.py; wait $pid 2>/dev/null; rm -rf "$run"
[ $ok = 1 ] && echo "gaming na frente: ok" || exit 1
