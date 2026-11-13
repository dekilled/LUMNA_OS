#!/bin/sh
# O papel de parede com dois monitores: cada tela usa o modo DELA (a TV na Mídia não troca o papel do monitor no Desktop, nem faz os
# papéis "por tela" deixarem de valer), e a área de trabalho recebe os cliques mesmo quando o papel nasce depois dela.
# Uso: sh testes/papel_telas.sh <pasta do orbit> (com make papel desktop)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home/Desktop" "$run/bin"
printf '#!/bin/sh\nexit 0\n' > "$run/bin/lumna-jogos"; cp "$run/bin/lumna-jogos" "$run/bin/lumna-midia"; cp "$run/bin/lumna-jogos" "$run/bin/lumna-modos"; chmod +x "$run/bin/"*
W="$gravity/../session/wallpapers"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run" LUMNA_PAPEL_DEBUG=1 LUMNA_DESKTOP_DEBUG=1
unset XDG_DATA_HOME XDG_STATE_HOME
printf oi > "$run/home/Desktop/nota.txt"
cat > "$run/cfg/lumna/papel.ini" <<INI
[geral]
por_tela = sim
trocar = nao
intervalo = 30
[todos]
imagem = $W/grafite.jpg
ajuste = preencher
[desktop HEADLESS-1]
imagem = $W/aurora-azul.jpg
ajuste = preencher
[desktop HEADLESS-2]
imagem = $W/mar-turquesa.jpg
ajuste = preencher
INI
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$')
sock=$(ls "$run"/orbit.*.sock | head -1); ok=1
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
falha() { echo "FALHOU: $1" >&2; ok=0; }
# a área de trabalho nasce ANTES do papel de parede (no PC de verdade o papel demora a carregar a imagem e nasce depois)
"$gravity/desktop" > "$run/dk.out" 2>&1 & dk=$!
sleep 1.5
"$gravity/papel" > "$run/papel.out" 2>&1 & pp=$!
sleep 3
ultimo() { grep "^papel tela=$1 " "$run/papel.out" | tail -1 | sed 's/.*imagem=.*\///'; }
[ "$(ultimo HEADLESS-1)" = aurora-azul.jpg ] || falha "o monitor 1 devia ter o papel dele (veio $(ultimo HEADLESS-1))"
[ "$(ultimo HEADLESS-2)" = mar-turquesa.jpg ] || falha "o monitor 2 devia ter o papel dele (veio $(ultimo HEADLESS-2))"
# uma tela vai para a Mídia: o papel da OUTRA (que segue no Desktop) não muda
msg modo midia HEADLESS-2 >/dev/null; sleep 2
[ "$(ultimo HEADLESS-1)" = aurora-azul.jpg ] || falha "com o monitor 2 na Mídia, o papel do monitor 1 (no Desktop) não devia mudar (veio $(ultimo HEADLESS-1))"
msg modo desktop HEADLESS-2 >/dev/null; sleep 2
[ "$(ultimo HEADLESS-2)" = mar-turquesa.jpg ] || falha "de volta ao Desktop, o monitor 2 devia ter o papel dele (veio $(ultimo HEADLESS-2))"
# a última troca foi do monitor 2 para o Gaming e de volta em outra tela: os papéis por tela continuam valendo nas duas
msg modo gaming HEADLESS-1 >/dev/null; sleep 1.5; msg modo desktop HEADLESS-1 >/dev/null; sleep 2
[ "$(ultimo HEADLESS-1)" = aurora-azul.jpg ] && [ "$(ultimo HEADLESS-2)" = mar-turquesa.jpg ] || falha "depois de ir e voltar do Gaming, cada tela com o papel dela ($(ultimo HEADLESS-1) / $(ultimo HEADLESS-2))"
# a área de trabalho por cima do papel: o clique no ícone chega nela
tela=$(grep -o 'camada lumna-desktop na saída [A-Z0-9-]*' "$run/log" | tail -1 | sed 's/.* //')
ox=$(msg saidas | tr '{' '\n' | grep "\"nome\":\"$tela\"" | grep -o '"x":-\?[0-9]*' | head -1 | cut -d: -f2)
pos=$(grep '^pos cel/nota.txt ' "$run/dk.out" | tail -1)
set -- $pos; x=$(( ${ox:-0} + $3 + $5 / 2 )); y=$(( $4 + $6 / 2 ))
msg mouse mover $x $y >/dev/null; sleep 0.3; msg mouse botao baixo >/dev/null; sleep 0.1; msg mouse botao cima >/dev/null; sleep 1
grep '^estado' "$run/dk.out" | tail -1 | grep -q 'sel=\[nota.txt\]' || falha "o clique no ícone devia chegar na área de trabalho, mesmo com o papel de parede nascido depois (tela $tela, clique em $x,$y; $(grep '^estado' "$run/dk.out" | tail -1 | cut -c1-80))"
kill $dk $pp 2>/dev/null; pkill -x orbit; pkill -x papel
[ $ok = 1 ] && { echo "papel por tela e área de trabalho por cima: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
