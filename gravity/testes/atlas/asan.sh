#!/bin/sh
# O Atlas com o ASAN e o UBSAN (acesso a memória liberada, fora dos limites, ponteiro nulo...) rodando a bateria do app inteiro.
#   sh testes/atlas/asan.sh PASTA_DO_ORBIT [teste_a.py teste_b.py ...]   (sem testes: todos, como o teste_ui.sh)
# Compila numa cópia (nada do build normal é tocado) e usa o ATLAS_BIN do teste_ui.sh. Qualquer erro do sanitizador derruba o Atlas e o teste falha.
set -e
orbit_dir=${1:?}; shift
aqui=$(cd "$(dirname "$0")" && pwd)
gravity=$(cd "$aqui/../.." && pwd)
copia=$(mktemp -d)
cp -r "$gravity/." "$copia/"
cd "$copia"
rm -f src/*.o libgravity.a atlas
CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" make atlas >/dev/null
export ATLAS_BIN="$copia/atlas" ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
export SO="$*"
sh "$aqui/teste_ui.sh" "$orbit_dir" "$copia"
rc=$?
rm -rf "$copia"
exit $rc
