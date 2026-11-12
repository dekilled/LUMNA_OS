#!/bin/sh
# O motor da calculadora: a tabela de contas (ASAN/UBSAN) e as contas sorteadas contra o Python. Uso: sh testes/calculadora.sh
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
cc -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Iservicos -o "$T/testa_calc" testes/calculadora/testa_calc.c servicos/calc.c -lm
"$T/testa_calc"
for semente in 1 2 3; do python3 testes/calculadora/diferencial.py "$T/testa_calc" 4000 $semente; done
rm -rf "$T"
