#!/bin/sh
# A Galeria: o serviço (biblioteca, EXIF, ordem, filtros, grupos, favoritos, álbuns, miniaturas) e os dos editores de fotos e de vídeos (servicos/edicao.c e edicao_video.c), cada um
# numa biblioteca de mentira e, com a pasta do build do Orbit (sh galeria.sh PASTA_DO_ORBIT), o app inteiro num Orbit headless
# (testes/galeria/teste_ui.sh: a biblioteca e, com TESTE=teste_editor.py, o editor de fotos e, com TESTE=teste_editor_video.py, o de vídeos).
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0 gdk-pixbuf-2.0)"
mkdir -p "$T/casa/c" "$T/casa/d" "$T/casa/cache"
python3 testes/galeria/fixtures.py "$T/casa" >/dev/null
cc $CF -o "$T/testa_galeria" testes/galeria/testa_galeria.c servicos/galeria.c servicos/arquivos.c $(pkg-config --libs glib-2.0 gdk-pixbuf-2.0)
env HOME="$T/casa" XDG_CONFIG_HOME="$T/casa/c" XDG_DATA_HOME="$T/casa/d" XDG_CACHE_HOME="$T/casa/cache" XDG_PICTURES_DIR="$T/casa/Imagens" XDG_VIDEOS_DIR="$T/casa/Vídeos" TZ=America/Sao_Paulo "$T/testa_galeria" "$T/casa"
mkdir -p "$T/edicao"
python3 testes/galeria/fixtures_edicao.py "$T/edicao"
cc $CF -O2 -o "$T/testa_edicao" testes/galeria/testa_edicao.c servicos/edicao.c $(pkg-config --libs glib-2.0 gdk-pixbuf-2.0) -lm
"$T/testa_edicao" "$T/edicao"
mkdir -p "$T/video"
cc $CF -O2 -o "$T/testa_edicao_video" testes/galeria/testa_edicao_video.c servicos/edicao_video.c $(pkg-config --libs glib-2.0 gdk-pixbuf-2.0) -lm
"$T/testa_edicao_video" "$T/video"
if [ -n "${1:-}" ]; then
	sh testes/galeria/teste_ui.sh "$1" "$AQUI"
	TESTE=teste_editor.py sh testes/galeria/teste_ui.sh "$1" "$AQUI"
	TESTE=teste_editor_video.py sh testes/galeria/teste_ui.sh "$1" "$AQUI"
fi
rm -rf "$T"
