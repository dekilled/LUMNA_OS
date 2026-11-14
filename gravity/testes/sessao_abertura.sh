#!/bin/sh
# O trecho do session/lumna-orbit-session que decide a cortina: só liga (ORBIT_CORTINA=1) com a abertura instalada e ligada. Roda só esse trecho, com um
# /usr/share/lumna de mentira (LUMNA_USR). Uso: sh gravity/testes/sessao_abertura.sh
aqui=$(cd "$(dirname "$0")/../.." && pwd)
trecho=$(sed -n '/^_ini=/,/^fi$/p' "$aqui/session/lumna-orbit-session" | sed 's#/usr/share/lumna/abertura/abertura.mp4#$ARQ#')
[ -n "$trecho" ] || { echo "FALHOU: não achei o trecho da cortina na sessão"; exit 1; }
run=$(mktemp -d); mkdir -p "$run/cfg/lumna"; ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
roda() { ARQ="$1" HOME="$run" XDG_CONFIG_HOME="$run/cfg" sh -c "$trecho; echo \${ORBIT_CORTINA:-sem}"; }
touch "$run/abertura.mp4"
[ "$(roda "$run/abertura.mp4")" = 1 ] || falha "com a abertura instalada e ligada a cortina devia ligar"
[ "$(roda "$run/nao-existe.mp4")" = sem ] || falha "sem o arquivo da abertura não pode ter cortina (o desktop ficaria preto)"
printf '[geral]\nabertura = nao\n' > "$run/cfg/lumna/animacoes.ini"
[ "$(roda "$run/abertura.mp4")" = sem ] || falha "com abertura = nao não pode ter cortina"
printf '[geral]\nabertura = sim\nabertura_som = nao\n' > "$run/cfg/lumna/animacoes.ini"
[ "$(roda "$run/abertura.mp4")" = 1 ] || falha "abertura = sim com o som desligado ainda precisa da cortina"
[ $ok = 1 ] && { echo "sessao_abertura: ok"; rm -rf "$run"; } || exit 1
