#!/bin/sh
# O catálogo da Steam da Loja do Gaming: o serviço (servicos/loja_steam.c, ASAN/UBSAN) contra um servidor local com respostas reais gravadas.
#   sh testes/loja_steam.sh          (a conferência com a Steam de verdade: sh testes/loja_steam.sh real)
set -e
aqui=$(cd "$(dirname "$0")" && pwd)
g=$(cd "$aqui/.." && pwd)
cd "$g"
run=$(mktemp -d); chmod 700 "$run"
export HOME="$run/casa" XDG_CACHE_HOME="$run/cache" LC_ALL=C LOG_FALSO="$run/pedidos.log"
unset XDG_CONFIG_HOME
mkdir -p "$HOME/.local/share/Steam/config"
printf '"users"\n{\n\t"76561198000000001"\n\t{\n\t\t"AccountName"\t\t"velho"\n\t\t"MostRecent"\t\t"0"\n\t}\n\t"76561198111111111"\n\t{\n\t\t"AccountName"\t\t"novo"\n\t\t"MostRecent"\t\t"1"\n\t}\n}\n' > "$HOME/.local/share/Steam/config/loginusers.vdf"
cc -O0 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -Iservicos $(pkg-config --cflags glib-2.0 gio-2.0) -o "$run/testa" \
	testes/jogos/testa_loja_steam.c servicos/loja_steam.c servicos/json.c $(pkg-config --libs glib-2.0 gio-2.0) 2>&1 | grep -E "error|warning" || true
porta=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')
: > "$run/pedidos.log"
python3 "$aqui/jogos/servidor_steam_falso.py" "$porta" "$run/pedidos.log" &
srv=$!
sleep 1
export LUMNA_LOJA_STEAM_URL="http://127.0.0.1:$porta" LUMNA_LOJA_PROTONDB_URL="http://127.0.0.1:$porta" LUMNA_LOJA_API_URL="http://127.0.0.1:$porta"
rc=0
"$run/testa" || rc=$?
kill $srv 2>/dev/null
[ $rc -eq 0 ] || echo "(log em $run)"
[ $rc -ne 0 ] || rm -rf "$run"
exit $rc
