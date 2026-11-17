#!/bin/sh
# O menu de contexto (botão direito) de uma janela encaixada nasce junto do cursor, na tela dela.
#   sh teste_menu.sh PASTA_DO_ORBIT PASTA_COM_menu_gtk
. "$(dirname "$0")/comum.sh"
trap fim EXIT
"$T/menu_gtk" $run/menu.txt > $run/gtk.log 2>&1 &
sleep 2.5
janelas | tee $run/j0.txt
$M acao encaixar_direita
sleep 1
janelas
# clique direito dentro da janela
cx=${CX:-900}; cy=${CY:-300}; $M saidas
$M mouse mover $cx $cy; sleep 0.3
$M mouse botao baixo direito; sleep 0.1; $M mouse botao cima direito
sleep 1.5
cat $run/menu.txt
grim -t ppm $run/tela.ppm 2>/dev/null && cp $run/tela.ppm ${SAIDA_PPM:-/tmp/menu_tela.ppm}   # a captura, para ver onde o menu nasceu
grep "menu da janela" $run/orbit.log
