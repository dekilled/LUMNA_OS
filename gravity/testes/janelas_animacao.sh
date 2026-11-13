#!/bin/sh
# A janela nasce um pouco maior e assenta no tamanho (e ao fechar o contrário, com um "fantasma" da última imagem); jogos não animam; janelas = nao desliga.
# O Orbit sem tela usa o renderizador de software, que não aplica transparência: aqui se prova o TAMANHO (a caixa da janela na imagem da tela) e os contadores.
# Uso: sh testes/janelas_animacao.sh <pasta do orbit> (com make calculadora)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd)
for c in grim; do command -v $c >/dev/null || { echo "janelas_animacao: dispensado (falta $c)"; exit 0; }; done
python3 -c "import PIL" 2>/dev/null || { echo "janelas_animacao: dispensado (falta PIL)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
ok=1
falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && LUMNA_TESTE_JANELA_MS=2000 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5; sock=$(ls "$run"/orbit.*.sock | head -1); export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$')
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
cont() { msg animacoes | grep -o "\"$1\":[0-9]*" | cut -d: -f2; }
caixa() { grim "$run/$1.png"; python3 - "$run/$1.png" <<'PY'
import sys
from PIL import Image, ImageChops
x = Image.open(sys.argv[1]).convert("RGB")
d = ImageChops.difference(x, Image.new("RGB", x.size, (14, 15, 14))).convert("L").point(lambda v: 255 if v > 6 else 0)
b = d.getbbox()
print("%d %d" % (b[2] - b[0], b[3] - b[1]) if b else "0 0")
PY
}
largura() { echo "${1%% *}"; }
# 1. abrir: nasce maior e assenta
"$gravity/calculadora" >/dev/null 2>&1 & sleep 0.7
[ "$(cont janelas_abertas)" = 1 ] || falha "abrir uma janela normal devia animar (janelas_abertas $(cont janelas_abertas))"
inicio=$(caixa a1); sleep 2.5; final=$(caixa a2)
[ "$(largura "$inicio")" -gt "$(largura "$final")" ] || falha "no começo a janela devia ser maior que no fim (começo $inicio, fim $final)"
# 2. fechar: o fantasma cresce e some
msg fechar 1 >/dev/null; sleep 0.5
[ "$(cont fantasmas)" = 1 ] || falha "ao fechar devia haver um fantasma (fantasmas $(cont fantasmas))"
[ "$(cont janelas_fechadas)" = 1 ] || falha "fechar devia animar (janelas_fechadas $(cont janelas_fechadas))"
fantasma=$(caixa f1)
[ "$(largura "$fantasma")" -gt "$(largura "$final")" ] || falha "o fantasma devia ser maior que a janela (fantasma $fantasma, janela $final)"
sleep 2.5
[ "$(cont fantasmas)" = 0 ] || falha "o fantasma devia sumir no fim da animação (fantasmas $(cont fantasmas))"
# 3. um jogo não anima
a=$(cont janelas_abertas)
LUMNA_JOGO=1 "$gravity/calculadora" >/dev/null 2>&1 & sleep 1.2
[ "$(cont janelas_abertas)" = "$a" ] || falha "a janela de um jogo não devia animar"
pkill -x calculadora; sleep 2.6
# 4. janelas = nao (o Painel grava animacoes.ini; o Orbit lê na hora)
printf '[geral]\njanelas = nao\n' > "$run/cfg/lumna/animacoes.ini"; sleep 1
a=$(cont janelas_abertas); c=$(cont janelas_fechadas)
"$gravity/calculadora" >/dev/null 2>&1 & sleep 1.2
[ "$(cont janelas_abertas)" = "$a" ] || falha "com janelas = nao a janela não devia animar ao abrir"
id=$(msg janelas | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2); msg fechar "$id" >/dev/null; sleep 0.6
[ "$(cont janelas_fechadas)" = "$c" ] || falha "com janelas = nao a janela não devia animar ao fechar"
# 5. voltou a sim
printf '[geral]\njanelas = sim\n' > "$run/cfg/lumna/animacoes.ini"; sleep 1
a=$(cont janelas_abertas)
"$gravity/calculadora" >/dev/null 2>&1 & sleep 1.2
[ "$(cont janelas_abertas)" -gt "$a" ] || falha "com janelas = sim voltou a animar"
pkill -x calculadora; pkill -x orbit
[ $ok = 1 ] && { echo "janelas_animacao: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
