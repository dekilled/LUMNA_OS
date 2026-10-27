#!/bin/sh
# O "Firefox" dos testes: não abre nada, só roda a extensão de mentira (extensao_falsa.py) com os mesmos argumentos.
echo "$@" > "${LUMNA_FF_ARGS:-/dev/null}"
exec python3 "$(dirname "$0")/extensao_falsa.py" "$@"
