#!/bin/sh
# A barra de título que o Orbit desenha (deco.c), com o foot (que usa o xdg-decoration) e o mouse virtual: arrastar pela barra, duplo clique,
# maximizar, minimizar e fechar. sh teste_barra.sh PASTA_DO_BUILD [ARRASTO2]
B=${1:?}; M=${2:-/tmp/claude-0/drag/arrasto2}
run=$(mktemp -d); chmod 700 $run
export XDG_RUNTIME_DIR=$run HOME=$run ORBIT_BORDA=0 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
(cd $B && ./orbit > $run/orbit.log 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
H="env ORBIT_SOCK=$run/orbit.wayland-0.sock $B/orbit-msg"
(foot -T "Teste da barra" > $run/foot.log 2>&1 &)
sleep 4
campo() { $H janelas | python3 -c "import json,sys; j=json.load(sys.stdin)['janelas']; print(j[0]['$1'] if j else 'nenhuma')"; }
X=$(campo x); Y=$(campo y); W=$(campo w)
[ "$X" != nenhuma ] || { echo "FALHOU: o foot não abriu" >&2; pkill -x orbit; exit 1; }
BY=$((Y - 27))                                                       # o meio da barra (54 px acima da área do cliente)
FECHA=$((X + W - 10 - 22)); MAXIM=$((FECHA - 14 - 44)); MINIM=$((MAXIM - 14 - 44))
clicar_em() { $M casa; sleep 0.2; $M mover $1 $2; sleep 0.3; $M clicar; sleep 0.8; }
# 1. arrastar pelo corpo da barra: a janela anda com o mouse
$M casa; sleep 0.2; $M mover $((X + 100)) $BY; sleep 0.3
$M arrastar 150 80 200; sleep 0.8
X2=$(campo x); Y2=$(campo y)
[ $((X2 - X)) -ge 140 ] && [ $((X2 - X)) -le 160 ] && [ $((Y2 - Y)) -ge 70 ] && [ $((Y2 - Y)) -le 90 ] || fail "arrastar pela barra devia mover a janela uns (150,80): era ($X,$Y), ficou ($X2,$Y2)"
X=$X2; Y=$Y2; BY=$((Y - 27)); FECHA=$((X + W - 10 - 22)); MAXIM=$((FECHA - 14 - 44)); MINIM=$((MAXIM - 14 - 44))
# 2. botão maximizar e de novo (volta)
clicar_em $MAXIM $BY
[ "$(campo maximizada)" = True ] || fail "o botão maximizar devia maximizar: $(campo maximizada)"
Y3=$(campo y); BY3=$((Y3 - 27)); W3=$(campo w)
clicar_em $((W3 - 10 - 44 - 14 - 22)) $BY3
[ "$(campo maximizada)" = False ] || fail "o botão maximizar de novo devia restaurar: $(campo maximizada)"
X=$(campo x); Y=$(campo y); BY=$((Y - 27)); W=$(campo w)
FECHA=$((X + W - 10 - 22)); MAXIM=$((FECHA - 14 - 44)); MINIM=$((MAXIM - 14 - 44))
# 3. duplo clique no corpo da barra maximiza
$M casa; sleep 0.2; $M mover $((X + 120)) $BY; sleep 0.3
$M clicar; sleep 0.1; $M clicar; sleep 0.8
[ "$(campo maximizada)" = True ] || fail "o duplo clique na barra devia maximizar: $(campo maximizada)"
Y3=$(campo y); BY3=$((Y3 - 27))
$M casa; sleep 0.2; $M mover 200 $BY3; sleep 0.3
$M clicar; sleep 0.1; $M clicar; sleep 0.8
[ "$(campo maximizada)" = False ] || fail "o duplo clique de novo devia restaurar: $(campo maximizada)"
X=$(campo x); Y=$(campo y); BY=$((Y - 27)); W=$(campo w)
FECHA=$((X + W - 10 - 22)); MAXIM=$((FECHA - 14 - 44)); MINIM=$((MAXIM - 14 - 44))
# 4. minimizar
clicar_em $MINIM $BY
[ "$(campo minimizada)" = True ] || fail "o botão minimizar devia minimizar: $(campo minimizada)"
$H foco 1 >/dev/null 2>&1 || $H focar 1 >/dev/null 2>&1; sleep 0.8
X=$(campo x); Y=$(campo y); BY=$((Y - 27)); W=$(campo w)
FECHA=$((X + W - 10 - 22))
# 5. fechar: o foot sai
clicar_em $FECHA $BY
sleep 1
[ "$(campo x)" = nenhuma ] || fail "o botão fechar devia fechar a janela: $($H janelas | head -c 200)"
pkill -x foot; pkill -x orbit
[ "$falhas" -eq 0 ] && echo "barra de título do Orbit (cliques): ok" || { echo "$falhas falha(s)"; exit 1; }
