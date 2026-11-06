#!/bin/sh
# Os modos por tela, com 3 monitores (Orbit headless): sh modos3.sh PASTA_DO_ORBIT_COMPILADO PROGRAMA_JANELA
# Prova a regra: Mídia ou Gaming numa tela muda SÓ aquela, as outras seguem Desktop estendidas; a janela da Mídia
# abre na tela pedida; sem nenhuma em modo especial é tudo Desktop de novo. (Janelas de verdade, com o app_id da Mídia.)
set -u
orbit_dir=${1:?pasta do orbit}
janela=${2:?programa janela}
run=$(mktemp -d); chmod 700 "$run"
mkdir -p "$run/cfg/lumna"
printf '[geral]\nao_ligar = desktop\n' >"$run/cfg/lumna/modos.ini"
XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR=$run WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=3 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/log" 2>&1 &
orbit=$!
sock=
for _ in 1 2 3 4 5 6 7 8 9 10; do
	sock=$(ls "$run"/orbit.*.sock 2>/dev/null | head -n1)
	[ -n "$sock" ] && break
	sleep 0.3
done
msg() { "$orbit_dir/orbit-msg" -s "$sock" "$@"; }
falhas=0
fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
modo_de() { msg modo | grep -o "\"saida\":\"$1\",\"modo\":\"[a-z]*\"" | sed 's/.*"modo":"\([a-z]*\)"/\1/'; }
confere() { # confere "descrição" modo1 modo2 modo3
	d=$1; shift
	r="$(modo_de HEADLESS-1) $(modo_de HEADLESS-2) $(modo_de HEADLESS-3)"
	[ "$r" = "$*" ] || fail "$d: esperava '$*' e veio '$r'"
}
saida_da_janela() { msg janelas | tr '{' '\n' | grep "\"app_id\":\"$1\"" | grep -o '"saida":"[A-Z0-9-]*"' | head -1 | sed 's/.*:"\(.*\)"/\1/'; }

[ -z "$sock" ] && { echo "o orbit não abriu o socket" >&2; cat "$run/log" >&2; exit 1; }
confere "ao ligar" desktop desktop desktop

for k in 1 2 3; do
	msg modo midia HEADLESS-$k >/dev/null; sleep 0.4
	case $k in 1) esp="midia desktop desktop" ;; 2) esp="desktop midia desktop" ;; 3) esp="desktop desktop midia" ;; esac
	confere "Mídia na tela $k" $esp
	msg modo desktop HEADLESS-$k >/dev/null; sleep 0.3
	confere "voltar ao Desktop da tela $k" desktop desktop desktop
done

# Gaming: o mesmo
for k in 1 2 3; do
	msg modo gaming HEADLESS-$k >/dev/null; sleep 0.4
	case $k in 1) esp="gaming desktop desktop" ;; 2) esp="desktop gaming desktop" ;; 3) esp="desktop desktop gaming" ;; esac
	confere "Gaming na tela $k" $esp
	msg modo desktop HEADLESS-$k >/dev/null; sleep 0.3
done

# Mídia numa tela e Gaming noutra: cada uma a sua, a terceira segue Desktop
msg modo midia HEADLESS-1 >/dev/null; msg modo gaming HEADLESS-3 >/dev/null; sleep 0.4
confere "Mídia na 1 e Gaming na 3" midia desktop gaming

# a Mídia pedida noutra tela leva o espaço dela (e as janelas) e a que a tinha volta ao Desktop; o Gaming da 3 não é roubado
msg modo midia HEADLESS-2 >/dev/null; sleep 0.4
confere "Mídia passa da 1 para a 2" desktop midia gaming
# o ciclo (Super+Tab) da tela 1 não rouba nem a Mídia nem o Gaming: desktop -> (nenhum livre) -> desktop
msg modo seguinte HEADLESS-1 >/dev/null; sleep 0.3
confere "ciclo da tela 1 com Mídia e Gaming ocupados" desktop midia gaming
msg modo desktop HEADLESS-2 >/dev/null; msg modo desktop HEADLESS-3 >/dev/null; sleep 0.3
confere "tudo de volta" desktop desktop desktop

# janelas de verdade: a Mídia (app_id lumna-midia) abre na tela que está em Mídia, mesmo com o cursor noutra
msg modo midia HEADLESS-2 >/dev/null; sleep 0.3
XDG_RUNTIME_DIR=$run WAYLAND_DISPLAY=$(basename "$sock" .sock | sed 's/^orbit\.//') "$janela" lumna-midia-inicio >"$run/j1.log" 2>&1 &
j1=$!
sleep 1.5
s=$(saida_da_janela lumna-midia-inicio)
[ "$s" = "HEADLESS-2" ] || fail "a janela da Mídia devia abrir na HEADLESS-2 (a que está em Mídia), abriu em '$s'"
# a Mídia pedida na tela 3 leva a janela junto
msg modo midia HEADLESS-3 >/dev/null; sleep 0.5
confere "Mídia passa da 2 para a 3" desktop desktop midia
s=$(saida_da_janela lumna-midia-inicio)
[ "$s" = "HEADLESS-3" ] || fail "a janela da Mídia devia ir junto para a HEADLESS-3, está em '$s'"
# uma janela comum não vai para a tela da Mídia
XDG_RUNTIME_DIR=$run WAYLAND_DISPLAY=$(basename "$sock" .sock | sed 's/^orbit\.//') "$janela" janela-comum >"$run/j2.log" 2>&1 &
j2=$!
sleep 1.5
s=$(saida_da_janela janela-comum)
[ "$s" != "HEADLESS-3" ] && [ -n "$s" ] || fail "uma janela comum não devia abrir na tela da Mídia (abriu em '$s')"
kill $j1 $j2 2>/dev/null
msg modo desktop HEADLESS-3 >/dev/null; sleep 0.3
confere "fim" desktop desktop desktop

kill "$orbit" 2>/dev/null; wait "$orbit" 2>/dev/null
if [ "$falhas" -ne 0 ]; then tail -20 "$run/log" >&2; rm -rf "$run"; exit 1; fi
rm -rf "$run"
echo "modos por tela com 3 monitores: ok"
