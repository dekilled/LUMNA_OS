#!/bin/sh
# O pré-setup: os serviços (servicos/setup.c, num HOME de mentira) e a janela inteira num Orbit headless, com o serviço do celular e o da Emma de mentira.
#   sh testes/setup.sh PASTA_DO_ORBIT   (com o `make setup` feito; sem a pasta, só os serviços)
set -e
aqui=$(cd "$(dirname "$0")" && pwd)
g=$(cd "$aqui/.." && pwd)
cd "$g"
make setup >/dev/null
run=$(mktemp -d); chmod 700 "$run"
sh "$aqui/setup/falsos.sh" "$run/f"
export HOME="$run/casa" XDG_RUNTIME_DIR="$run" FALSO_LOG="$run/falso.log" PATH="$run/f/bin:$PATH" LC_ALL=C
unset XDG_CONFIG_HOME
mkdir -p "$HOME"
cc -O0 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -Iservicos $(pkg-config --cflags glib-2.0 gio-2.0 gio-unix-2.0) -o "$run/testa_setup" \
	testes/setup/testa_setup.c servicos/setup.c servicos/emma_ativa.c servicos/emmar.c servicos/emmac.c servicos/autostart.c servicos/entrada.c servicos/modos.c servicos/json.c \
	$(pkg-config --libs glib-2.0 gio-2.0 gio-unix-2.0) -lpthread 2>&1 | grep -E "error" || true
"$run/testa_setup"
# a Emma desligada no sistema: o serviço, o chat e o app saem na hora, sem abrir nada (nem socket, nem janela)
mkdir -p "$HOME/.config/lumna"
printf '[emma]\nativa=nao\n' > "$HOME/.config/lumna/emma-sistema.ini"
make emma emma-chat emma-app >/dev/null
for b in emma emma-chat emma-app; do
	timeout 10 "$g/$b" > "$run/$b.out" 2>&1 || { echo "FALHOU: $b desligada devia sair com 0"; exit 1; }
	grep -qi "desligada" "$run/$b.out" || { echo "FALHOU: $b desligada devia avisar"; exit 1; }
done
[ ! -S "$run/lumna-emma.sock" ] || { echo "FALHOU: a Emma desligada abriu o socket"; exit 1; }
echo "emma desligada no sistema: o serviço, o chat e o app saem sem abrir nada: ok"
rm -f "$HOME/.config/lumna/emma-sistema.ini"
if [ -n "$1" ]; then
	sh "$aqui/setup/teste_ui.sh" "$1" "$g"
fi
rm -rf "$run"
