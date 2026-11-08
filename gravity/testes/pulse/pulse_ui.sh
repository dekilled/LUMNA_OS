#!/bin/sh
# O Pulse Terminal num Orbit headless, com teclado e mouse de verdade (wtype e o mouse virtual): digitar (com acentos), Ctrl+C, abas, selecionar
# e copiar, fechar com exit. O que o shell recebe se confere por arquivos que ele mesmo escreve.
# sh pulse_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY ARRASTO2
orbit_dir=${1:?}; g=${2:?}; M=${3:?}
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run HOME=$run LANG=C.UTF-8 LC_ALL=C.UTF-8 SHELL=/bin/bash PS1='$ '
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 4
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
H="env ORBIT_SOCK=$run/orbit.$WAYLAND_DISPLAY.sock $orbit_dir/orbit-msg"
"$g/pulse" >"$run/c.log" 2>&1 &
pid=$!
sleep 4
digita() { wtype "$1"; wtype -k Return; sleep 0.7; }
tecla_ctrl() { wtype -M ctrl "$1" -m ctrl; sleep 0.5; }
tecla_ctrl_shift() { wtype -M ctrl -M shift "$1" -m shift -m ctrl; sleep 0.8; }
espera_arquivo() { for _ in $(seq 1 20); do [ -s "$1" ] && return 0; sleep 0.3; done; return 1; }

# 1. digitar, com acentos
digita "echo ola > $run/t1"
espera_arquivo "$run/t1" || fail "o texto digitado não chegou ao shell"
digita "echo çãé > $run/t2"
espera_arquivo "$run/t2" && [ "$(cat "$run/t2")" = "çãé" ] || fail "os acentos devem chegar inteiros: [$(cat "$run/t2" 2>/dev/null)]"
# 2. Ctrl+C interrompe
digita "sleep 100"
tecla_ctrl c
digita "echo vivo > $run/t3"
espera_arquivo "$run/t3" || fail "Ctrl+C devia interromper o sleep"
# 3. abas: Ctrl+Shift+T abre outra no mesmo diretório; Ctrl+PgUp volta
tecla_ctrl_shift t
digita "echo dois > $run/t4"
espera_arquivo "$run/t4" || fail "a aba nova devia receber o teclado"
wtype -M ctrl -k Prior -m ctrl; sleep 0.6
digita "echo um > $run/t5"
espera_arquivo "$run/t5" || fail "Ctrl+PgUp devia voltar à primeira aba"
# 4. selecionar a palavra com duplo clique e copiar
wtype -M ctrl l -m ctrl; sleep 0.6   # limpa a tela (o bash faz o clear no Ctrl+L)
digita "echo SELECIONE-ISTO"
x=$($H janelas | python3 -c "import json,sys; print(json.load(sys.stdin)['janelas'][0]['x'])")
y=$($H janelas | python3 -c "import json,sys; print(json.load(sys.stdin)['janelas'][0]['y'])")
px=$((x + 10 + 4 * 9 + 4)); py=$((y + 94 + 10 + 18 + 9))   # a coluna 4 da linha 1 (o que o echo escreveu)
"$M" casa; sleep 0.2; "$M" mover $px $py; sleep 0.3; "$M" clicar; sleep 0.12; "$M" clicar; sleep 0.6
grim "$run/selecao.png" 2>/dev/null
tecla_ctrl_shift c
# 5. colar (Ctrl+Shift+V): a palavra copiada vai para o shell (a ida e volta pela área de transferência do próprio app; o wl-paste/wl-copy de fora
#    não valem num Orbit headless)
wtype -k Return; sleep 0.3
wtype "echo "; tecla_ctrl_shift v
wtype " > $run/t6"; wtype -k Return; sleep 0.8
espera_arquivo "$run/t6" && [ "$(cat "$run/t6")" = "SELECIONE-ISTO" ] || fail "copiar e colar: o shell devia receber a palavra selecionada: [$(cat "$run/t6" 2>/dev/null)]"
# 6. fechar a aba (Ctrl+Shift+W) deixa a janela; exit na última fecha a janela
tecla_ctrl_shift w
sleep 0.8
[ "$($H janelas | python3 -c "import json,sys; print(len(json.load(sys.stdin)['janelas']))")" = 1 ] || fail "fechar uma aba não devia fechar a janela"
digita "exit"
sleep 1.5
[ "$($H janelas | python3 -c "import json,sys; print(len(json.load(sys.stdin)['janelas']))")" = 0 ] || fail "exit na última aba devia fechar a janela"
kill $pid 2>/dev/null; wait $pid 2>/dev/null
kill $orbit 2>/dev/null
[ "$falhas" -eq 0 ] && echo "pulse (teclado e mouse): ok" || { echo "$falhas falha(s) (log em $run)"; exit 1; }
