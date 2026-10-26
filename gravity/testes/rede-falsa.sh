#!/bin/sh
# Corre COMANDO com um NetworkManager de mentira no D-Bus do sistema (so deste processo). Precisa
# do python-dbusmock (pip install --break-system-packages python-dbusmock) e do python3-dbus.
AQUI=$(cd "$(dirname "$0")" && pwd)
exec dbus-run-session -- /usr/bin/python3.12 "$AQUI/rede-falsa.py" "$@"
