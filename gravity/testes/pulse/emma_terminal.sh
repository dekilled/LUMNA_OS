#!/bin/sh
# O módulo emma_terminal contra um lumna-emma de mentira. sh emma_terminal.sh PASTA_DO_GRAVITY BINARIO_DO_TESTE
g=${1:?}; bin=${2:?}
run=$(mktemp -d); chmod 700 "$run"
python3 "$g/testes/pulse/emma-falsa.py" "$run/emma.sock" "$run/log" &
fake=$!
for _ in $(seq 1 30); do [ -S "$run/emma.sock" ] && break; sleep 0.1; done
LUMNA_EMMA_SOCK="$run/emma.sock" "$bin" "$run/log"; rc=$?
kill $fake 2>/dev/null
exit $rc
