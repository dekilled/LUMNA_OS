#!/bin/sh
# O Atlas ao redimensionar: a barra de título (minimizar, maximizar, fechar) e a barra de ferramentas acompanham a largura da janela, e a janela não passa do
# tamanho mínimo que o app pediu (560). Arrasta a borda de verdade num Orbit sem tela e lê as posições que o Atlas escreve (LUMNA_ATLAS_DEBUG).
# Uso: sh testes/atlas_redimensiona.sh <pasta do orbit> (com make atlas)
orbit=${1:?}; g=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home"
export XDG_RUNTIME_DIR="$run" XDG_CONFIG_HOME="$run/cfg" HOME="$run/home" LUMNA_ATLAS_DEBUG=1
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 &)
sleep 2.5
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1)
m() { "$orbit/orbit-msg" -s "$sock" "$@" >/dev/null; }
"$g/atlas" >"$run/a.log" 2>&1 & pid=$!
sleep 4
larg() { "$orbit/orbit-msg" -s "$sock" janelas | grep -o '"w":[0-9]*' | head -1 | cut -d: -f2; }
pos() { grep "^pos $1 " "$run/a.log" | tail -1; }   # pos NOME x y w h
arrasta() { # a borda direita, de $1 até $2 (x)
	m mouse mover "$1" 360; sleep 0.3; m mouse botao baixo; sleep 0.2
	x=$1; passo=$([ "$2" -lt "$1" ] && echo -100 || echo 100)
	while [ "$x" != "$2" ]; do x=$((x + passo)); [ "$passo" -lt 0 ] && [ $x -lt $2 ] && x=$2; [ "$passo" -gt 0 ] && [ $x -gt $2 ] && x=$2; m mouse mover $x 360; sleep 0.12; done
	m mouse botao cima; sleep 1.5
}
confere() { # $1: o nome do caso
	caso=$1; w=$(larg)
	b=$(pos barra | awk '{print $5}'); tx=$(pos t-ajustes | awk '{print $3}'); tw=$(pos t-ajustes | awk '{print $5}')
	[ "$b" = "$((w - 2))" ] || falha "$caso: a barra de título devia ter a largura da janela ($((w - 2))) e tem $b"
	[ "$((tx + tw))" -le "$w" ] || falha "$caso: o botão de ajustes da barra de ferramentas (x $tx, largura $tw) devia caber na janela de $w"
	echo "info: $caso: janela $w, barra $b, ajustes em $tx"
}
w0=$(larg); echo "info: janela de partida $w0"
arrasta $((20 + w0 - 1)) 700;  confere "estreitou para ~700"
w1=$(larg); [ "$w1" -lt "$w0" ] || falha "a janela devia ter encolhido ($w0 -> $w1)"
arrasta $((20 + w1 - 1)) 300;  confere "tentou passar do mínimo"
w2=$(larg); [ "$w2" -ge 560 ] || falha "a janela não podia ficar menor que o mínimo do app (560): $w2"
[ "$w2" -le 560 ] || falha "arrastando até o fim a janela devia parar no mínimo (560): $w2"
arrasta $((20 + w2 - 1)) 1000; confere "alargou de novo"
kill $pid 2>/dev/null; pkill -x orbit
[ $ok = 1 ] && { echo "atlas_redimensiona: ok"; rm -rf "$run"; } || { echo "(em $run)"; exit 1; }
