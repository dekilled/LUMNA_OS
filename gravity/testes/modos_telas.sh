#!/bin/sh
# Gaming e Mídia em dois monitores, com a janela do Gaming de verdade: a janela acompanha o modo (nunca fica no monitor antigo, escondida).
# Uso: sh gravity/testes/modos_telas.sh <pasta do orbit>
orbit=${1:-orbit}; gravity=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
ln -s "$gravity/jogos" "$run/bin/lumna-jogos"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
cd "$orbit" || exit 1
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman ./orbit >"$run/log" 2>&1 & pid=$!
sleep 2
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-')
sock=$(ls "$run"/orbit.*.sock | head -1); ok=1
modo() { ./orbit-msg -s "$sock" modo "$@" >/dev/null; sleep 2.5; }
onde() { ./orbit-msg -s "$sock" janelas | tr '{' '\n' | grep '"app_id":"lumna-jogos"' | grep -o '"saida":"[^"]*"\|"visivel":[a-z]*' | paste -sd' '; }
confere() { [ "$(onde)" = "$1" ] || { echo "FALHOU: $2 (veio: $(onde))" >&2; ok=0; }; }
modo gaming HEADLESS-1; confere '"saida":"HEADLESS-1" "visivel":true' "o Gaming pedido no monitor 1 abre nele"
modo gaming HEADLESS-2; confere '"saida":"HEADLESS-2" "visivel":true' "o Gaming pedido no monitor 2 passa para ele"
modo desktop HEADLESS-2; confere '"saida":"HEADLESS-2" "visivel":false' "saindo do Gaming a janela some"
modo gaming HEADLESS-1; confere '"saida":"HEADLESS-1" "visivel":true' "o Gaming pedido de novo no monitor 1 aparece de primeira, sem passar pelo outro"
./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-2","modo":"desktop"' || { echo "FALHOU: o monitor 2 devia estar no Desktop" >&2; ok=0; }
kill $pid; pkill -x jogos; wait $pid 2>/dev/null; rm -rf "$run"
[ $ok = 1 ] && echo "modos em dois monitores: ok" || exit 1
