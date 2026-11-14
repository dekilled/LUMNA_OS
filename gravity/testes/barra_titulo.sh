#!/bin/sh
# A barra de título do Orbit em quem não tem: uma janela X11 que pede a moldura ganha a barra (com mover, minimizar e fechar de verdade); a que se desenha sozinha (_MOTIF_WM_HINTS sem
# moldura) não; um app Wayland da lista orbit.ini com_barra ganha; e "orbit-msg barra" põe e tira em qualquer janela. Orbit sem tela. Precisa de Xwayland e python3-xlib.
# Uso: sh gravity/testes/barra_titulo.sh <pasta do orbit>
orbit=${1:-orbit}; aqui=$(cd "$(dirname "$0")" && pwd); gravity="$aqui/.."
for c in Xwayland; do command -v $c >/dev/null || { echo "barra_titulo: dispensado (falta $c)"; exit 0; }; done
python3 -c "import Xlib" 2>/dev/null || { echo "barra_titulo: dispensado (falta python3-xlib)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/orbit" "$run/home"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
printf '[geral]\ncom_barra = lumna-calculadora, Outro-App\n' > "$run/cfg/orbit/orbit.ini"
cd "$orbit" || exit 1
LUMNA_TESTE_TECLADOS=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 & pid=$!
sleep 2
D=$(grep -o "DISPLAY=:[0-9]*" "$run/log" | head -1 | cut -d= -f2)
[ -n "$D" ] || { echo "barra_titulo: dispensado (esta wlroots não tem XWayland)"; kill $pid; rm -rf "$run"; exit 0; }
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1); ok=1
msg() { ./orbit-msg -s "$sock" "$@"; }
falha() { echo "FALHOU: $1" >&2; ok=0; }
jan() { msg janelas | tr '{' '\n' | grep "\"app_id\":\"$1\""; }
campo() { jan "$1" | grep -o "\"$2\":[-a-z0-9]*" | head -1 | cut -d: -f2; }
# 1. uma janela X11 comum pede a moldura: ganha a barra
DISPLAY=$D python3 "$aqui/xwayland/xcli.py" jan ff0000 Comum >/dev/null 2>&1 & p1=$!
# 2. uma que se desenha sozinha (sem moldura): não ganha
DISPLAY=$D python3 "$aqui/xwayland/xcli.py" jan 00ff00 Propria x semmoldura >/dev/null 2>&1 & p2=$!
sleep 3
[ "$(campo Comum barra)" = true ] || falha "a janela X11 que pede a moldura devia ganhar a barra do Orbit ($(jan Comum | head -c 200))"
[ "$(campo Propria barra)" = false ] || falha "a janela X11 sem moldura (que se desenha sozinha) não devia ganhar barra"
# 3. os botões da barra funcionam: a barra fica por cima da janela (y - 54) e o botão de fechar no canto direito
x=$(campo Comum x); y=$(campo Comum y); w=$(campo Comum w)
fecha_x=$((x + w - 10 - 22)); barra_y=$((y - 27))
msg mouse mover $((x + 100)) $barra_y >/dev/null; sleep 0.3
y0=$y
# arrastar o corpo da barra move a janela
msg mouse botao baixo >/dev/null; sleep 0.2; msg mouse mover $((x + 220)) $((barra_y + 60)) >/dev/null; sleep 0.2; msg mouse mover $((x + 300)) $((barra_y + 90)) >/dev/null; sleep 0.3; msg mouse botao cima >/dev/null; sleep 0.5
x2=$(campo Comum x); y2=$(campo Comum y)
[ "$x2" -gt "$x" ] && [ "$y2" -gt "$y0" ] || falha "arrastar a barra devia mover a janela ($x,$y -> $x2,$y2)"
x=$x2; y=$y2
# minimizar: o botão do meio-esquerdo (fecha - 14 - 44 - 14 - 44)
min_x=$((x + w - 10 - 44 - 14 - 44 - 14 - 22)); barra_y=$((y - 27))
msg mouse mover $min_x $barra_y >/dev/null; sleep 0.3; msg mouse botao baixo >/dev/null; sleep 0.1; msg mouse botao cima >/dev/null; sleep 0.6
[ "$(campo Comum minimizada)" = true ] || falha "o botão de minimizar da barra devia minimizar a janela"
msg minimizar >/dev/null 2>&1
# 4. orbit-msg barra: põe, tira e alterna em qualquer janela (a "Propria" não tinha)
id=$(jan Propria | grep -o '"id":[0-9]*' | cut -d: -f2)
msg barra $id sim | grep -q '"barra":true' || falha "orbit-msg barra ID sim devia pôr a barra"
[ "$(campo Propria barra)" = true ] || falha "depois de 'barra sim' a janela devia constar com barra"
msg barra $id | grep -q '"barra":false' || falha "orbit-msg barra ID (sem argumento) devia alternar para sem barra"
msg barra $id nao | grep -q '"barra":false' || falha "orbit-msg barra ID nao devia deixar sem barra"
# 4b. Super+B na janela em foco (clica no corpo da "Propria" para ela ter o foco)
msg barra $id nao >/dev/null; sleep 0.4
px=$(campo Propria x); py=$(campo Propria y)
msg mouse mover $((px + 60)) $((py + 60)) >/dev/null; sleep 0.3; msg mouse botao baixo >/dev/null; sleep 0.1; msg mouse botao cima >/dev/null; sleep 0.4
msg tecla_falsa 0 125 baixo >/dev/null; sleep 0.15; msg tecla_falsa 0 48 >/dev/null; sleep 0.2; msg tecla_falsa 0 125 cima >/dev/null; sleep 0.5
[ "$(campo Propria barra)" = true ] || falha "Super+B devia pôr a barra na janela em foco"
msg tecla_falsa 0 125 baixo >/dev/null; sleep 0.15; msg tecla_falsa 0 48 >/dev/null; sleep 0.2; msg tecla_falsa 0 125 cima >/dev/null; sleep 0.5
[ "$(campo Propria barra)" = false ] || falha "Super+B de novo devia tirar a barra"
# 5. um app Wayland da lista com_barra (a calculadora do Gravity não fala xdg-decoration): ganha a barra do Orbit
if [ -x "$gravity/calculadora" ]; then
	"$gravity/calculadora" >/dev/null 2>&1 & p3=$!
	sleep 3
	[ "$(campo lumna-calculadora barra)" = true ] || falha "um app Wayland da lista com_barra devia ganhar a barra ($(jan lumna-calculadora | head -c 160))"
	kill $p3 2>/dev/null
fi
# 6. fechar pelo botão da barra (a janela "Propria" agora tem barra de novo: põe e clica no fechar)
msg barra $id sim >/dev/null; sleep 0.5
x=$(campo Propria x); y=$(campo Propria y); w=$(campo Propria w)
msg mouse mover $((x + w - 10 - 22)) $((y - 27)) >/dev/null; sleep 0.3; msg mouse botao baixo >/dev/null; sleep 0.1; msg mouse botao cima >/dev/null; sleep 1
[ -z "$(jan Propria)" ] || falha "o botão de fechar da barra devia fechar a janela"
kill $p1 $p2 $pid 2>/dev/null
[ $ok = 1 ] && { echo "barra_titulo: ok"; rm -rf "$run"; } || { echo "(em $run)"; exit 1; }
