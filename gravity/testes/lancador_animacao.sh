#!/bin/sh
# A animação do lançador de apps: o conteúdo nasce um pouco menor e cresce (o fundo acende), e fechar é o contrário e só então o processo acaba;
# com as animações dos apps desligadas (Painel > Animações) abre e fecha na hora. GV_ANIM_ESCALA=8 estica o tempo para ver os quadros do meio.
# Uso: sh testes/lancador_animacao.sh <pasta do orbit> (com make lancador)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd)
for c in grim; do command -v $c >/dev/null || { echo "lancador_animacao: dispensado (falta $c)"; exit 0; }; done
python3 -c "import PIL" 2>/dev/null || { echo "lancador_animacao: dispensado (falta PIL)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/gravity" "$run/cfg/lumna" "$run/home"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5; export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$')
# a margem esquerda do texto claro (o título "Início") e o quanto de brilho ele tem
medida() { grim "$run/$1.png"; python3 - "$run/$1.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("L")
w, h = im.size
xs = [x for x in range(0, w, 2) for y in range(40, 200, 4) if im.getpixel((x, y)) > 100]
print(min(xs) if xs else 9999, im.crop((0, 40, w // 2, 200)).getextrema()[1])
PY
}
# 1. abrir (tempo esticado): no começo o texto está mais para dentro (conteúdo menor) e mais fraco do que no fim
GV_ANIM_ESCALA=8 "$gravity/lancador" >/dev/null 2>&1 & sleep 0.35
inicio=$(medida a1); sleep 3; final=$(medida a2)
[ "${inicio%% *}" -gt "${final%% *}" ] || falha "no começo o conteúdo devia estar menor (margem do começo ${inicio%% *}, do fim ${final%% *})"
[ "${inicio##* }" -lt "${final##* }" ] || falha "no começo o conteúdo devia estar mais transparente (brilho do começo ${inicio##* }, do fim ${final##* })"
# 2. fechar (o sinal que a dock manda): anima e só então acaba
pkill -TERM -x lancador; sleep 0.4
pgrep -x lancador >/dev/null || falha "ao fechar, o lançador devia ficar um instante na tela (animando)"
meio=$(medida f1)
[ "${meio%% *}" -gt "${final%% *}" ] || falha "fechando, o conteúdo devia encolher (margem ${meio%% *}, aberto ${final%% *})"
sleep 3
pgrep -x lancador >/dev/null && falha "depois da animação o lançador devia ter saído"
# 3. com as animações dos apps desligadas, abre e fecha na hora
printf '[movimento]\nanimacoes = nao\n' > "$run/cfg/gravity/tema.ini"
GV_ANIM_ESCALA=8 "$gravity/lancador" >/dev/null 2>&1 & sleep 1.0
pkill -TERM -x lancador; sleep 0.6
pgrep -x lancador >/dev/null && falha "sem animações o lançador devia fechar na hora"
pkill -x orbit
[ $ok = 1 ] && { echo "lancador_animacao: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
