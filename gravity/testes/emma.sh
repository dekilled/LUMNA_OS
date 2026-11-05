#!/bin/sh
# O serviço da Emma de ponta a ponta, com um backend e uma Mídia de mentira (testes/emma/): nada toca no sistema.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
cd "$AQUI"
make emma connect >/dev/null
python3 testes/emma/testa_emma.py "$AQUI/emma" "$AQUI/connect"
# as histórias (servicos/emmah.c), num HOME de mentira
cc -O0 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0 gio-2.0) -o /tmp/emma-historia-$$ testes/emma/historia_teste.c servicos/emmah.c servicos/emmaq.c servicos/json.c $(pkg-config --libs glib-2.0 gio-2.0) 2>&1 | grep -E "error" || true
/tmp/emma-historia-$$
rm -f /tmp/emma-historia-$$
