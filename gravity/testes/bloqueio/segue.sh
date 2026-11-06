#!/bin/sh
# A interface do bloqueio (relógio, player, senha) aparece na tela onde está o cursor e SEGUE o cursor entre monitores.
# sh segue.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY ARRASTO2 (mouse virtual: arrasto2 casa | mover DX DY)  — precisa de grim e wtype
orbit_dir=${1:?}; g=${2:?}; mouse=${3:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna"
export XDG_RUNTIME_DIR=$run XDG_CONFIG_HOME=$run/cfg
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 2
sock=$(ls "$run"/orbit.*.sock | head -1); export WAYLAND_DISPLAY=$(basename "$sock" .sock | sed 's/^orbit\.//')
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
claros() { # quantos pixels quase brancos há na saída (o relógio e os textos da interface; o papel com o véu quase não tem)
	grim -o "$1" "$run/$1.png" && python3 -c "
from PIL import Image
im = Image.open('$run/$1.png').convert('RGB')
px = im.load()
print(sum(1 for x in range(0, im.width, 2) for y in range(0, im.height, 2) if min(px[x, y]) > 235))"
}
onde() { # qual saída tem a interface (a com mais pixels claros) e quantos
	a=$(claros HEADLESS-1); b=$(claros HEADLESS-2)
	[ "$a" -gt $((b * 3 + 50)) ] && echo "HEADLESS-1" || { [ "$b" -gt $((a * 3 + 50)) ] && echo "HEADLESS-2" || echo "nenhuma($a/$b)"; }
}
"$mouse" casa; sleep 0.5
primeira=$("$orbit_dir/orbit-msg" -s "$sock" modo | grep -o '"saida":"[A-Z0-9-]*","modo":"[a-z]*","espaco":[0-9]*,"ponteiro":true' | cut -d'"' -f4)
"$g/bloqueio" >"$run/bloqueio.log" 2>&1 &
bq=$!
sleep 4
r1=$(onde)
[ "$r1" = "$primeira" ] || fail "no começo a interface devia estar na tela do cursor ($primeira), está em $r1"
"$mouse" mover 1500 0; sleep 3
r2=$(onde)
[ "$r2" != "$r1" ] && [ "${r2#nenhuma}" = "$r2" ] || fail "o cursor foi para a outra tela e a interface devia ir junto (antes $r1, agora $r2)"
# o teclado continua funcionando depois de trocar: uma tecla abre a tela de entrar nessa tela (a imagem muda bastante)
claros "$r2" >/dev/null; cp "$run/$r2.png" "$run/antes.png"
wtype x; sleep 1.5
claros "$r2" >/dev/null
mudou=$(python3 -c "
from PIL import Image
a = Image.open('$run/antes.png').convert('RGB').load(); b = Image.open('$run/$r2.png').convert('RGB')
w, h = b.size; b = b.load()
print(sum(1 for x in range(0, w, 4) for y in range(0, h, 4) if sum(abs(a[x, y][i] - b[x, y][i]) for i in range(3)) > 60))")
[ "$mudou" -gt 1500 ] || fail "a tecla digitada depois da troca devia abrir a tela de entrar (só $mudou pontos mudaram)"
"$mouse" mover -1500 0; sleep 3
r3=$(onde)
[ "$r3" = "$r1" ] || fail "o cursor voltou e a interface devia voltar para $r1, está em $r3"
kill -0 $bq 2>/dev/null || fail "o bloqueio caiu: $(tail -3 "$run/bloqueio.log")"
kill $bq 2>/dev/null; kill $orbit 2>/dev/null; wait 2>/dev/null
[ $falhas -eq 0 ] && { echo "interface do bloqueio segue o cursor: ok ($r1 -> $r2 -> $r3)"; rm -rf "$run"; exit 0; }
tail -n 5 "$run/orbit.log" "$run/bloqueio.log" >&2; exit 1
