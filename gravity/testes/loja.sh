#!/bin/sh
# A Loja: o serviço (catálogo, busca com filtros, categorias, AUR, atualizações e ações) com pacman, checkupdates, curl e paru de mentira e, com a pasta do build
# do Orbit (sh loja.sh PASTA_DO_ORBIT), o app inteiro num Orbit headless (testes/loja/teste_ui.sh e teste_drivers.sh).
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0)"
sh testes/loja/falsos.sh "$T/f"
cc $CF -o "$T/testa_loja" testes/loja/testa_loja.c servicos/loja.c servicos/json.c $(pkg-config --libs glib-2.0) -lm
env PATH="$T/f/bin:$PATH" FALSO_LOG="$T/log" LUMNA_SEM_PKEXEC=1 LC_ALL=C "$T/testa_loja"
cc $CF -o "$T/testa_loja_hw" testes/loja/testa_loja_hw.c servicos/loja_hw.c servicos/loja.c servicos/json.c $(pkg-config --libs glib-2.0) -lm
env PATH="$T/f/bin:$PATH" FALSO_LOG="$T/log2" LUMNA_SEM_PKEXEC=1 LC_ALL=C "$T/testa_loja_hw"
if [ -n "${1:-}" ]; then
	make -s loja
	sh testes/loja/teste_ui.sh "$1" "$AQUI"
	sh testes/loja/teste_drivers.sh "$1" "$AQUI"
fi
rm -rf "$T"
