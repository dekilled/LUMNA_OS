#!/bin/sh
# O serviço da Emma de ponta a ponta, com um backend e uma Mídia de mentira (testes/emma/): nada toca no sistema.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
cd "$AQUI"
make emma controle >/dev/null
python3 testes/emma/testa_emma.py "$AQUI/emma" "$AQUI/controle"
