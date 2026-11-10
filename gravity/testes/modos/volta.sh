#!/bin/sh
# Uma janela do Desktop tem de continuar desenhada depois de ir à Mídia (ou ao Gaming) e voltar, mesmo com as trocas feitas depressa (uma animação de entrada
# interrompida por outro arranjo deixava a janela recortada: ela constava como ativa e sumia da tela, parecia minimizada, e clicar nela na dock não fazia nada).
#   sh volta.sh PASTA_DO_ORBIT_COMPILADO PROGRAMA_JANELA
set -u
orbit_dir=${1:?pasta do orbit}
janela=${2:?programa janela}
run=$(mktemp -d); chmod 700 "$run"
mkdir -p "$run/cfg/lumna"
printf '[geral]\nao_ligar = desktop\n' >"$run/cfg/lumna/modos.ini"
export XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR=$run WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman
"$orbit_dir/orbit" >"$run/log" 2>&1 &
sock=
for _ in 1 2 3 4 5 6 7 8 9 10; do
	sock=$(ls "$run"/orbit.*.sock 2>/dev/null | head -n1)
	[ -n "$sock" ] && break
	sleep 0.3
done
[ -z "$sock" ] && { echo "o orbit não abriu o socket" >&2; cat "$run/log" >&2; exit 1; }
export WAYLAND_DISPLAY=$(basename "$sock" .sock | sed 's/^orbit\.//')
msg() { "$orbit_dir/orbit-msg" -s "$sock" "$@"; }
falhas=0
fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
campo() { msg janelas | tr '{' '\n' | grep "\"app_id\":\"$1\"" | grep -o "\"$2\":[a-z0-9]*" | head -1 | cut -d: -f2; }
# a cor de um pixel da tela (R G B), pela captura
pixel() { grim -g "$1,$2 1x1" -t ppm - 2>/dev/null | tail -c 3 | od -An -tu1 | tr -s ' ' | sed 's/^ //'; }
# a cor no centro da janela do Desktop (onde ela está agora, segundo o Orbit)
centro() {
	x=$(campo janela-desktop x); y=$(campo janela-desktop y); w=$(campo janela-desktop w); h=$(campo janela-desktop h)
	pixel $((x + w / 2)) $((y + h / 2))
}

"$janela" janela-desktop azul >"$run/j1.log" 2>&1 &
sleep 1.5
c=$(centro)
[ "$c" = "0 0 255" ] || fail "antes de tudo a janela azul devia estar na tela: pixel $c"

confere_voltou() { # confere_voltou "descrição"
	sleep 1.6
	[ "$(campo janela-desktop visivel)" = "true" ] || fail "$1: a janela do Desktop não está visível"
	[ "$(campo janela-desktop recortada)" = "false" ] || fail "$1: a janela do Desktop ficou recortada por uma animação (some da tela, ainda que conste como ativa)"
	c=$(centro)
	[ "$c" = "0 0 255" ] || fail "$1: a janela azul do Desktop não está desenhada na tela (pixel $c)"
}

# 1. Mídia (com a janela dela) e volta, sem pressa
msg modo midia HEADLESS-1 >/dev/null; sleep 1.0
"$janela" lumna-midia-inicio >/dev/null 2>&1 &
sleep 1.2
msg modo desktop HEADLESS-1 >/dev/null
confere_voltou "depois da Mídia"

# 2. Gaming e volta
msg modo gaming HEADLESS-1 >/dev/null; sleep 1.0
msg modo desktop HEADLESS-1 >/dev/null
confere_voltou "depois do Gaming"

# 3. trocas depressa: a animação de entrada é interrompida pela seguinte
for _ in 1 2 3 4 5; do
	msg modo midia HEADLESS-1 >/dev/null; sleep 0.12
	msg modo desktop HEADLESS-1 >/dev/null; sleep 0.12
	msg modo gaming HEADLESS-1 >/dev/null; sleep 0.1
	msg modo desktop HEADLESS-1 >/dev/null; sleep 0.1
done
confere_voltou "depois de trocas rápidas"

# 4. uma ativação pela dock no meio de uma troca (o arranjo roda de novo durante a animação)
msg modo midia HEADLESS-1 >/dev/null; sleep 0.8
msg modo desktop HEADLESS-1 >/dev/null; sleep 0.1
id=$(msg janelas | tr '{' '\n' | grep '"app_id":"janela-desktop"' | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
msg foco "$id" >/dev/null
confere_voltou "com o foco pedido durante a troca"

pkill -f "orbit-msg" 2>/dev/null
kill %1 2>/dev/null
pkill -x janela 2>/dev/null; pkill -x orbit 2>/dev/null
if [ $falhas -ne 0 ]; then
	echo "$falhas falha(s)"
	exit 1
fi
echo "modos (a janela do Desktop volta inteira depois da Mídia e do Gaming, mesmo com trocas rápidas): ok"
