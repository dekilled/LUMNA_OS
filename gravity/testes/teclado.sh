#!/bin/sh
# O teclado na tela. Sem argumentos: o layout e os ajustes (sem tela). Com a pasta do build do Orbit (sh teclado.sh PASTA_DO_ORBIT), também o app
# inteiro num Orbit headless: o método de entrada, o teclado com toques de verdade e os modos. Precisa do `make teclado`.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0)"
cc $CF -o "$T/testa_layout" testes/teclado/testa_layout.c servicos/teclado_layout.c $(pkg-config --libs glib-2.0) -lm
"$T/testa_layout"
cc $CF -o "$T/testa_cfg" testes/teclado/testa_cfg.c servicos/teclado_cfg.c servicos/teclado_layout.c $(pkg-config --libs glib-2.0) -lm
env HOME="$T/casa" XDG_CONFIG_HOME="$T/casa/c" XDG_DATA_HOME="$T/casa/d" "$T/testa_cfg"
if [ -n "${1:-}" ]; then
	sh testes/teclado/teste_im.sh "$1"
	sh testes/teclado/teste_app.sh "$1" "$AQUI"
	sh testes/teclado/teste_modos.sh "$1" "$AQUI"
fi
