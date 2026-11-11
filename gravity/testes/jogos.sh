#!/bin/sh
# A biblioteca de jogos: o serviço (servicos/jogos.c, com ASAN/UBSAN) com Steam, Lutris e Heroic de mentira, e a janela do lumna-jogos num Orbit sem tela.
#   sh testes/jogos.sh [PASTA_DO_ORBIT]
set -e
aqui=$(cd "$(dirname "$0")" && pwd)
g=$(cd "$aqui/.." && pwd)
cd "$g"
run=$(mktemp -d); chmod 700 "$run"
python3 "$aqui/jogos/fixtures.py" "$run"
export HOME="$run/casa" XDG_RUNTIME_DIR="$run" RAIZ_TESTE="$run" FALSO_LOG="$run/falso.log" PATH="$run/bin:$PATH" LC_ALL=C
export LUMNA_STEAM_DIR="$run/steam" LUMNA_LUTRIS_DB="$run/lutris.db" LUMNA_HEROIC_DIR="$run/heroic"
unset XDG_CONFIG_HOME
mkdir -p "$HOME"
cc -O0 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -Iservicos $(pkg-config --cflags glib-2.0 sqlite3) -o "$run/testa_jogos" \
	testes/jogos/testa_jogos.c servicos/jogos.c servicos/json.c $(pkg-config --libs glib-2.0 sqlite3) 2>&1 | grep -E "error|warning" || true
"$run/testa_jogos"
rm -rf "$run"
if [ -n "$1" ]; then
	# a janela, com fixtures novas (o teste de cima já mexeu nos de lá)
	make jogos >/dev/null
	run=$(mktemp -d); chmod 700 "$run"
	python3 "$aqui/jogos/fixtures.py" "$run"
	export HOME="$run/casa" XDG_RUNTIME_DIR="$run" RAIZ_TESTE="$run" FALSO_LOG="$run/falso.log" PATH="$run/bin:$PATH"
	export LUMNA_STEAM_DIR="$run/steam" LUMNA_LUTRIS_DB="$run/lutris.db" LUMNA_HEROIC_DIR="$run/heroic"
	mkdir -p "$HOME"
	sh "$aqui/jogos/teste_ui.sh" "$1" "$g" "$run"
	rm -rf "$run"
	sh "$aqui/jogos/teste_loja.sh" "$1" "$g"
fi
