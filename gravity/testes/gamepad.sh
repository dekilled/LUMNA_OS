#!/bin/sh
# Os controles: a leitura (servicos/gamepad.c, com ASAN/UBSAN) com controles de mentira, o serviço lumna-gamepad e a aba Configurações do Gaming.
#   sh testes/gamepad.sh [PASTA_DO_ORBIT]
set -e
aqui=$(cd "$(dirname "$0")" && pwd)
g=$(cd "$aqui/.." && pwd)
cd "$g"
run=$(mktemp -d); chmod 700 "$run"
mkdir -p "$run/falsos" "$run/energia" "$run/casa"
export HOME="$run/casa" XDG_RUNTIME_DIR="$run" LUMNA_GAMEPAD_FALSOS="$run/falsos" LUMNA_POWER_DIR="$run/energia" LC_ALL=C
unset XDG_CONFIG_HOME
cc -O0 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -Iservicos $(pkg-config --cflags glib-2.0) -o "$run/testa_gamepad" \
	testes/gamepad/testa_gamepad.c servicos/gamepad.c $(pkg-config --libs glib-2.0) 2>&1 | grep -E "error|warning" || true
"$run/testa_gamepad"
# o serviço, de ponta a ponta
make gamepad >/dev/null
mkdir -p "$run/bin"
printf '#!/bin/sh\necho "$*" >> "%s/teclas.log"\n' "$run" > "$run/bin/wtype"
printf '#!/bin/sh\nexit 1\n' > "$run/bin/pgrep"
for p in lumna-jogos lumna-emma-chat; do printf '#!/bin/sh\necho "%s" >> "%s/chamadas.log"\n' "$p" "$run" > "$run/bin/$p"; done
printf '#!/bin/sh\nexit 1\n' > "$run/bin/pkill"
chmod +x "$run/bin"/*
rm -rf "$HOME/.config/lumna"  # a configuração que o teste de cima gravou
: > "$run/teclas.log"; : > "$run/chamadas.log"
rm -rf "$run/falsos"; mkdir -p "$run/falsos"
export PATH="$run/bin:$PATH" LUMNA_GAMEPAD_FOCO="$run/foco" LUMNA_POWER_DIR="$run/energia2"; mkdir -p "$run/energia2"
python3 "$aqui/gamepad/teste_servico.py" "$run" "$g"
pkill -x gamepad 2>/dev/null || true
[ "${MANTER:-}" = 1 ] || rm -rf "$run"
if [ -n "$1" ]; then
	make jogos gamepad >/dev/null
	sh "$aqui/gamepad/teste_ui.sh" "$1" "$g"
fi
