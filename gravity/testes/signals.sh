#!/bin/sh
# O núcleo do Signals (servicos/sgbuffer.c), com ASAN/UBSAN: o modelo aleatório, os arquivos, o desfazer, a busca.
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI" || exit 1
cc -std=gnu11 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0) -o "$T/testa_buffer" testes/signals/testa_buffer.c servicos/sgbuffer.c $(pkg-config --libs glib-2.0) || exit 1
"$T/testa_buffer"; r=$?
rm -rf "$T"
exit $r
