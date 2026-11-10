#!/bin/sh
# O LUMNA Music: a biblioteca (tags, cache, capas, letras, playlists, fila), o motor (mpv de verdade, saída nula: tocar, gapless, transição) e o MPRIS
# (um D-Bus de sessão de mentira) em C e, com a pasta do build do Orbit (sh musica.sh PASTA_DO_ORBIT), o app inteiro num Orbit headless (testes/musica/teste_ui.sh).
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -O1 -Iservicos"
python3 testes/musica/fixtures.py "$T"
mkdir -p "$T/c" "$T/d" "$T/cache"
cc $CF $(pkg-config --cflags glib-2.0) -o "$T/testa_musica" testes/musica/testa_musica.c servicos/musica.c servicos/json.c $(pkg-config --libs glib-2.0) -lm
env HOME="$T" XDG_CONFIG_HOME="$T/c" XDG_DATA_HOME="$T/d" XDG_CACHE_HOME="$T/cache" XDG_MUSIC_DIR="$T/Músicas" "$T/testa_musica" "$T"
cc $CF $(pkg-config --cflags glib-2.0) -o "$T/testa_player" testes/musica/testa_player.c servicos/musica_player.c servicos/json.c $(pkg-config --libs glib-2.0) -lm -lpthread
"$T/testa_player" "$T"
cc $CF $(pkg-config --cflags gio-2.0) -o "$T/testa_mpris" testes/musica/testa_mpris.c servicos/musica_mpris.c $(pkg-config --libs gio-2.0) -lm
dbus-run-session -- "$T/testa_mpris" 2>&1 | grep -v "fd limit\|erro na chamada"
if [ -n "${1:-}" ]; then
	make -s musica
	sh testes/musica/teste_ui.sh "$1" "$AQUI"
fi
rm -rf "$T"
