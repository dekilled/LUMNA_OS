#!/bin/sh
# A tela de login nova (lumna-entrada) numa janela, sem mexer no login de verdade do PC:
#
#   ./testar-login.sh            o layout Centralizado
#   ./testar-login.sh lateral    o Painel lateral
#
# Um greetd de mentira (testes/greetd-falso.py) faz de conta: qualquer usuário entra com a senha
# "lumna", e no terminal aparece a sessão que o greetd de verdade abriria. Ao entrar, a janela
# fecha (o Orbit -S fecha com a tela de login, como no PC de verdade).
# Use o ./testar-desktop.sh uma vez antes (ele compila o Orbit).
set -e
AQUI=$(cd "$(dirname "$0")" && pwd)
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/lumna
O="${ORBIT_DIR:-$CACHE/orbit}/src/compositor/orbit"
[ -x "$O/orbit" ] || { echo "Rode o ./testar-desktop.sh --refazer primeiro (ele compila o Orbit)."; exit 1; }
command -v python3 >/dev/null || { echo "Falta o python3 (o greetd de mentira)."; exit 1; }
[ -x "$AQUI/entrada" ] || make -C "$AQUI" entrada
mkdir -p "$CACHE/bin"
ln -sf "$AQUI/entrada" "$CACHE/bin/lumna-entrada"

SOCK="${XDG_RUNTIME_DIR:-/tmp}/lumna-greetd-falso.sock"
python3 "$AQUI/testes/greetd-falso.py" "$SOCK" &
FALSO=$!
trap 'kill $FALSO 2>/dev/null' EXIT
INI="$CACHE/entrada.ini"
echo "layout = ${1:-centro}" > "$INI"
echo "Senha de teste: lumna"
PATH="$CACHE/bin:$PATH" GREETD_SOCK="$SOCK" LUMNA_ENTRADA_INI="$INI" ORBIT_BORDA=0 "$O/orbit" -S lumna-entrada
