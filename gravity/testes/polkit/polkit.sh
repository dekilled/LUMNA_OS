#!/bin/sh
# O agente do polkit contra um polkit de mentira (python-dbus no D-Bus de sessão), com o helper e a janela falsos, nos dois
# modos do helper (programa e socket). Precisa do python3.12 com dbus (o mesmo dos testes de rede e Bluetooth).
# sh polkit.sh PASTA_DO_GRAVITY
g=${1:?}
AQUI=$(cd "$(dirname "$0")" && pwd)
t=$(mktemp -d)
chmod +x "$AQUI/janela-falsa.sh" "$AQUI/helper-falso.sh"
dbus-run-session -- /usr/bin/python3.12 "$AQUI/autoridade-falsa.py" "$g" "$t"
