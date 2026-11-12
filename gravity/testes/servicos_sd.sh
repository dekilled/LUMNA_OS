#!/bin/sh
# O serviço de leitura dos serviços do systemd (servicos/sdservicos.c) contra um systemctl de mentira, com ASAN/UBSAN.
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI" || exit 1
cc -std=gnu11 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0) -o "$T/testa_servicos" testes/processos/testa_servicos.c servicos/sdservicos.c $(pkg-config --libs glib-2.0) || exit 1
PATH="$AQUI/testes/processos/bin:$PATH" "$T/testa_servicos"; r=$?
rm -rf "$T"
exit $r
