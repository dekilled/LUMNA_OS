#!/bin/sh
# Mesmo teste com uma janela X11 (XWayland): o menu do xterm (Ctrl+botão direito) é uma janela X posicionada em coordenadas globais.
#   sh teste_menu_x11.sh PASTA_DO_ORBIT PASTA_COM_menu_gtk [esquerda|direita]
. "$(dirname "$0")/comum.sh"
trap fim EXIT
lado=${3:-direita}
sleep 1
DISPLAY=$(grep -o 'DISPLAY=:[0-9]*' $run/orbit.log | tail -1 | cut -d= -f2); export DISPLAY
unset GDK_BACKEND
xterm -geometry 60x20 > /dev/null 2>&1 &
sleep 3
$M acao encaixar_$lado
sleep 1.2
read X Y W H <<EOF2
$(janelas | campo 0 x y w h)
EOF2
cx=$((X + W/2)); cy=$((Y + H/2))
$M mouse mover $cx $cy; sleep 0.4
xdotool keydown ctrl
sleep 0.4
$M mouse botao baixo direito; sleep 0.15; $M mouse botao cima direito; sleep 0.6
grim -t ppm $run/tela.ppm; xdotool keyup ctrl
cp $run/tela.ppm /tmp/claude-0/enc/x11_tela.ppm
echo "janela $X $Y $W $H; cursor em $cx $cy"
