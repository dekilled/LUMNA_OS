#!/bin/sh
# O LUMNA Music aberto com um arquivo (o app padrão para áudio): toca na hora, uma instância só, sem mpv sobrando. sh teste_abrir.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
aqui=$(cd "$(dirname "$0")" && pwd)
MUSICA_ARG="@CASA@/Músicas/Horizonte violeta/01 Entre órbitas.mp3" TESTE=teste_abrir.py exec sh "$aqui/teste_ui.sh" "$@"
