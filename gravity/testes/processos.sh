#!/bin/sh
# O serviço de processos (leitura do /proc, CPU, prioridade, encerrar por pidfd, árvore, proteções) contra processos de verdade, com ASAN/UBSAN.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
cc -std=gnu11 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -Iservicos -o "$T/testa_processos" testes/processos/testa_processos.c servicos/processos.c
"$T/testa_processos"
rm -rf "$T"
