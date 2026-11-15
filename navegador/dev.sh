#!/bin/sh
# Compila o navegador e monta a pasta de execução em navegador/build/stage (binário + libcef + recursos + ui).
# Uso: CEF_ROOT=/caminho/do/cef_binary_... sh navegador/dev.sh [compilar]     (sem argumento, compila e abre a Mídia)
set -e
aqui=$(cd "$(dirname "$0")" && pwd)
[ -d "${CEF_ROOT:-}" ] || { echo "defina CEF_ROOT=<pasta do cef_binary_... extraído> (veja scripts/teste-cef-codecs.sh, que baixa um)"; exit 1; }
cmake -S "$aqui" -B "$aqui/build" -G Ninja -DCEF_ROOT="$CEF_ROOT" -DCMAKE_BUILD_TYPE=Release >"$aqui/build.log" 2>&1 || { tail -20 "$aqui/build.log"; exit 1; }
cmake --build "$aqui/build" >>"$aqui/build.log" 2>&1 || { grep -E 'error|Error' "$aqui/build.log" | head -20; exit 1; }
stage="$aqui/build/stage"; mkdir -p "$stage"
cp -r "$CEF_ROOT"/Release/* "$CEF_ROOT"/Resources/* "$stage/"
cp "$aqui/build/lumna-navegador" "$stage/.novo" && mv -f "$stage/.novo" "$stage/lumna-navegador"   # troca atômica: funciona mesmo com o app aberto
rm -rf "$stage/ui"; cp -r "$aqui/ui" "$stage/ui"
echo "pronto: $stage/lumna-navegador"
[ "${1:-}" = compilar ] || exec "$stage/lumna-navegador"
