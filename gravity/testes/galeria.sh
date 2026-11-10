#!/bin/sh
# A Galeria: o serviço (biblioteca, EXIF, ordem, filtros, grupos, favoritos, álbuns, miniaturas) numa biblioteca de mentira e, com a pasta do build do Orbit
# (sh galeria.sh PASTA_DO_ORBIT), o app inteiro num Orbit headless (testes/galeria/teste_ui.sh).
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0 gdk-pixbuf-2.0)"
mkdir -p "$T/casa/c" "$T/casa/d" "$T/casa/cache"
python3 testes/galeria/fixtures.py "$T/casa" >/dev/null
cc $CF -o "$T/testa_galeria" testes/galeria/testa_galeria.c servicos/galeria.c servicos/arquivos.c $(pkg-config --libs glib-2.0 gdk-pixbuf-2.0)
env HOME="$T/casa" XDG_CONFIG_HOME="$T/casa/c" XDG_DATA_HOME="$T/casa/d" XDG_CACHE_HOME="$T/casa/cache" XDG_PICTURES_DIR="$T/casa/Imagens" XDG_VIDEOS_DIR="$T/casa/Vídeos" TZ=America/Sao_Paulo "$T/testa_galeria" "$T/casa"
if [ -n "${1:-}" ]; then
	sh testes/galeria/teste_ui.sh "$1" "$AQUI"
fi
rm -rf "$T"
