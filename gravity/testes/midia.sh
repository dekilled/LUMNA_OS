#!/bin/sh
# Testa os serviços da Mídia: o catálogo e os recentes (servicos/midia.c) e o gerente do Firefox
# (servicos/firefox.c) contra um Firefox de mentira com uma extensão de mentira (testes/midia/extensao_falsa.py,
# precisa do módulo websockets do Python). Usa uma pasta de casa nova a cada vez.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -Iservicos"
cc $CF $(pkg-config --cflags glib-2.0) -o "$T/testa_midia" testes/midia/testa_midia.c servicos/midia.c servicos/json.c $(pkg-config --libs glib-2.0)
env -u XDG_CONFIG_HOME -u XDG_DATA_HOME HOME="$T" "$T/testa_midia"
cc $CF $(pkg-config --cflags libsoup-3.0 gio-2.0) -o "$T/testa_firefox" testes/midia/testa_firefox.c servicos/firefox.c servicos/midia.c servicos/json.c \
	$(pkg-config --libs libsoup-3.0 gio-2.0) -lpthread
mkdir -p "$T/h2"
env -u XDG_CONFIG_HOME -u XDG_DATA_HOME HOME="$T/h2" LUMNA_FIREFOX="$AQUI/testes/midia/firefox-falso.sh" LUMNA_FF_ARGS="$T/h2/args.txt" \
	LUMNA_EXTENSAO="$AQUI/extensao-firefox" LUMNA_TESTES="$AQUI/testes/midia" timeout 90 "$T/testa_firefox"
echo "(arquivos de teste em $T)"
