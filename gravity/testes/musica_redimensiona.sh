#!/bin/sh
# O LUMNA Music estreitado: a barra de título (minimizar, maximizar e FECHAR) continua dentro da janela, a barra lateral vira só de ícones e o player perde o volume e a
# saída de áudio. Arrasta a borda de verdade num Orbit sem tela e olha o botão de fechar na foto. Uso: sh testes/musica_redimensiona.sh <pasta do orbit> (com make musica)
orbit=${1:?}; g=$(cd "$(dirname "$0")/.." && pwd)
command -v grim >/dev/null || { echo "musica_redimensiona: dispensado (falta grim)"; exit 0; }
python3 -c "import PIL" 2>/dev/null || { echo "musica_redimensiona: dispensado (falta PIL)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home"
export XDG_RUNTIME_DIR="$run" XDG_CONFIG_HOME="$run/cfg" HOME="$run/home"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 &)
sleep 2.5
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1)
m() { "$orbit/orbit-msg" -s "$sock" "$@" >/dev/null; }
"$g/musica" >"$run/a.log" 2>&1 & pid=$!
sleep 4
caixa() { "$orbit/orbit-msg" -s "$sock" janelas | grep -o '"x":[0-9]*,"y":[0-9]*,"w":[0-9]*' | head -1 | tr -d '"xyw:' | tr ',' ' '; }   # x y w
set -- $(caixa); x0=$1; w0=$3
# o botão de fechar tem o ícone claro: há pixels claros no quadrado de 24 px a 34 px da borda direita, na altura da barra
fechar_visivel() { # $1 x da janela, $2 largura
	timeout 5 grim "$run/f.png" && python3 -W ignore -c "
from PIL import Image
im = Image.open('$run/f.png').convert('L')
cx = $1 + $2 - 34
claros = sum(1 for dx in range(-10, 11) for dy in range(-10, 11) if im.getpixel((cx + dx, 8 + 27 + dy)) > 150)
print(claros)"
}
n=$(fechar_visivel "$x0" "$w0"); echo "info: janela grande ($w0): pixels claros do fechar $n"
[ "$n" -gt 8 ] || falha "na janela grande o botão de fechar devia aparecer ($n)"
m mouse mover $((x0 + w0 - 1)) 360; sleep 0.3; m mouse botao baixo; sleep 0.2
for xx in 1100 1000 900 800 700 600 500; do m mouse mover $xx 360; sleep 0.12; done; m mouse botao cima; sleep 1.5
set -- $(caixa); x1=$1; w1=$3
echo "info: janela estreita: $w1"
[ "$w1" -lt 700 ] || falha "a janela devia ter encolhido ($w1)"
[ "$w1" -ge 480 ] || falha "a janela não podia ficar menor que o mínimo do app (480): $w1"
n=$(fechar_visivel "$x1" "$w1"); echo "info: janela estreita ($w1): pixels claros do fechar $n"
[ "$n" -gt 8 ] || falha "na janela estreita o botão de fechar devia continuar dentro da janela ($n)"
