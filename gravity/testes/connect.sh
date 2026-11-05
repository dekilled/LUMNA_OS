#!/bin/sh
# O Connect de ponta a ponta: lumna-connect de verdade + celular falso + wl-paste/wl-copy/notify-send de mentira.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
cd "$AQUI"
make connect >/dev/null
python3 testes/connect/testa_connect.py "$AQUI/connect"
python3 testes/connect/testa_wtype.py "$AQUI/connect"
