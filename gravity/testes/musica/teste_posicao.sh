#!/bin/sh
# Clicar numa música começa do 0:00, mesmo depois de a fila ter sido salva no meio de outra. sh teste_posicao.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
aqui=$(cd "$(dirname "$0")" && pwd)
MUSICA_ARG="@CASA@/longa.mp3" TESTE=teste_posicao.py exec sh "$aqui/teste_ui.sh" "$@"
