#!/bin/sh
# O LUMNA Music em segundo plano: fechar a janela tocando esconde a janela e a música segue; o MPRIS controla; Raise a traz de volta. sh teste_segundo_plano.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
aqui=$(cd "$(dirname "$0")" && pwd)
MUSICA_ARG="@CASA@/longa.mp3" TESTE=teste_segundo_plano.py exec sh "$aqui/teste_ui.sh" "$@"
