#!/bin/sh
# Os 3 espaços do Desktop dão a volta em círculo: 1 -> 2 -> 3 -> 1 desliza sempre para a esquerda (avançando), e voltar (1 -> 3 -> 2 -> 1) para a direita.
# Uso: sh testes/workspaces_deslize.sh <pasta do orbit>
orbit=${1:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg" "$run/home"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
ok=1
falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5; sock=$(ls "$run"/orbit.*.sock | head -1)
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
dir() { msg carrossel | grep -o '"dir_deslize":-\?[0-9]*' | cut -d: -f2; }
n() { msg carrossel | grep -o '"deslizes":[0-9]*' | cut -d: -f2; }
vai() { # vai DESTINO ESPERADO: o espaço (1 a 3) e o lado em que o conteúdo desliza
	antes=$(n); msg espaco "$1" >/dev/null; sleep 0.4
	[ "$(n)" -gt "$antes" ] || falha "ir para o espaço $1 devia deslizar"
	[ "$(dir)" = "$2" ] || falha "ir para o espaço $1: o lado devia ser $2 e foi $(dir)"
}
msg espaco 1 >/dev/null; sleep 0.5
vai 2 1; vai 3 1; vai 1 1      # avançar, inclusive o 3 -> 1 que dá a volta: sempre para a esquerda
vai 3 -1; vai 2 -1; vai 1 -1   # voltar: para a direita (o 1 -> 3 volta pelo caminho mais curto)
vai 2 1
# animacoes.ini (o Painel > Animações): deslize = nao desliga o deslize na hora; sim liga de novo
mkdir -p "$run/cfg/lumna"
printf '[geral]\ndeslize = nao\n' > "$run/cfg/lumna/animacoes.ini"; sleep 1
antes=$(n); msg espaco 3 >/dev/null; sleep 0.4; msg espaco 1 >/dev/null; sleep 0.4
[ "$(n)" = "$antes" ] || falha "com deslize = nao a troca de espaço não devia deslizar"
printf '[geral]\ndeslize = sim\n' > "$run/cfg/lumna/animacoes.ini"; sleep 1
vai 2 1
pkill -x orbit
[ $ok = 1 ] && { echo "workspaces_deslize: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
