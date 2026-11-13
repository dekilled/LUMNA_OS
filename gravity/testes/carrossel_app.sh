#!/bin/sh
# O lumna-carrossel de verdade num Orbit sem tela: aparece na camada de cima ao abrir, some ao confirmar/cancelar (e o programa sai),
# e um clique num cartão troca para aquele modo. Uso: sh testes/carrossel_app.sh <pasta do orbit> (com make carrossel)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
ln -s "$gravity/carrossel" "$run/bin/lumna-carrossel"
printf '#!/bin/sh\nexit 0\n' > "$run/bin/lumna-modos"; chmod +x "$run/bin/lumna-modos"
printf '[geral]\ntroca = carrossel\n' > "$run/cfg/lumna/modos.ini"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
ok=1
falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && LUMNA_TESTE_TECLADOS=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5; sock=$(ls "$run"/orbit.*.sock | head -1)
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
tecla() { msg tecla_falsa 0 "$@" >/dev/null; sleep 0.2; }
modo() { msg modo | grep -o '"modo":"[a-z]*"' | head -1 | cut -d'"' -f4; }
camada() { msg camadas | grep -c 'lumna-carrossel'; }
SUPER=125; TAB=15; ESC=1
# 1. abre: a camada aparece; Esc: some e o programa sai
tecla $SUPER baixo; tecla $TAB; sleep 0.8
[ "$(camada)" -ge 1 ] || falha "o carrossel devia aparecer na camada ao apertar Super+Tab"
pgrep -x lumna-carrossel >/dev/null || falha "o programa lumna-carrossel devia estar rodando"
tecla $ESC; sleep 0.8
[ "$(camada)" = 0 ] || falha "Esc devia tirar o carrossel da tela"
pgrep -x lumna-carrossel >/dev/null && falha "o programa devia sair depois do Esc"
tecla $SUPER cima
[ "$(modo)" = desktop ] || falha "Esc não podia trocar o modo (está $(modo))"
# 2. soltar o Super confirma e o carrossel some
tecla $SUPER baixo; tecla $TAB; tecla $TAB; sleep 0.6
tecla $SUPER cima; sleep 0.8
[ "$(modo)" = gaming ] || falha "soltar o Super devia trazer o Gaming (está $(modo))"
[ "$(camada)" = 0 ] || falha "o carrossel devia sumir depois de confirmar"
msg modo desktop >/dev/null; sleep 0.5
# 3. um clique no cartão da direita (o Gaming, com o Desktop no centro) troca para ele, ainda com o Super apertado
tecla $SUPER baixo; tecla $TAB; sleep 1
msg mouse mover 1040 380 >/dev/null; sleep 0.3; msg mouse botao baixo >/dev/null; sleep 0.1; msg mouse botao cima >/dev/null; sleep 0.8
[ "$(modo)" = gaming ] || falha "clicar no cartão do Gaming devia trazer o Gaming (está $(modo))"
[ "$(camada)" = 0 ] || falha "o carrossel devia sumir depois do clique"
tecla $SUPER cima
[ "$(modo)" = gaming ] || falha "soltar o Super depois do clique não podia mudar o modo (está $(modo))"
# 4. um clique fora dos cartões cancela
msg modo desktop >/dev/null; sleep 0.5
tecla $SUPER baixo; tecla $TAB; sleep 1
msg mouse mover 640 120 >/dev/null; sleep 0.3; msg mouse botao baixo >/dev/null; sleep 0.1; msg mouse botao cima >/dev/null; sleep 0.8
[ "$(camada)" = 0 ] || falha "um clique fora dos cartões devia fechar o carrossel"
tecla $SUPER cima
[ "$(modo)" = desktop ] || falha "cancelar não podia trocar o modo (está $(modo))"
pkill -x orbit; pkill -x lumna-carrossel
[ $ok = 1 ] && { echo "carrossel_app: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
