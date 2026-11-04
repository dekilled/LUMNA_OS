#!/bin/sh
# O Connect de ponta a ponta: lumna-controle de verdade + celular falso + wl-paste/wl-copy/notify-send de mentira.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
cd "$AQUI"
make controle >/dev/null
python3 testes/connect/testa_connect.py "$AQUI/controle"
