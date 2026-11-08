#!/bin/sh
# A correção de erro com a Emma, no Pulse de verdade (Orbit headless, teclado virtual) e uma Emma de mentira: o comando que falha ganha um cartão de sugestão; Esc
# descarta, Enter executa (só depois de uma folga, e comando destrutivo só vai ao prompt), Tab põe no prompt para editar; o que não é erro (grep sem achar) não pergunta;
# sem a Emma o terminal segue normal. sh correcao_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; g=${2:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/casa" "$run/marcas"
export XDG_RUNTIME_DIR=$run HOME=$run/casa XDG_DATA_HOME=$run/casa/.local/share XDG_CONFIG_HOME=$run/casa/.config LANG=C.UTF-8 SHELL=/bin/bash PS1='$ ' HISTFILE=/dev/null
export LUMNA_PULSE_INTEGRACAO=$g/pulse-shell/integracao.bash LUMNA_EMMA_SOCK=$run/emma.sock EMMA_FALSA_DIR=$run/marcas
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
python3 "$g/testes/pulse/emma-falsa.py" "$run/emma.sock" "$run/emma.log" &
fake=$!
for _ in $(seq 1 30); do [ -S "$run/emma.sock" ] && break; sleep 0.1; done
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 3
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
"$g/pulse" >"$run/p.log" 2>&1 &
pid=$!
sleep 4
BD=$run/casa/.local/share/lumna/pulse/pulse.db
q() { python3 -c "import sqlite3,sys; c=sqlite3.connect(sys.argv[1]); r=c.execute(sys.argv[2]).fetchone(); print(r[0] if r else '')" "$BD" "$1" 2>/dev/null; }
decisao() { q "select decisao from emma_interactions where prompt_enviado like '%pulse-erro-$1%' order by id desc limit 1"; }
digita() { wtype "$1"; wtype -k Return; }

# 1. Esc descarta
digita "pulse-erro-1"; sleep 2.2
grim "$run/cartao.png" 2>/dev/null
wtype -k Escape; sleep 0.8
[ ! -e "$run/marcas/m1" ] || fail "o Esc não pode executar a sugestão"
[ "$(decisao 1)" = descartada ] || fail "o Esc devia gravar 'descartada': [$(decisao 1)]"
# 2. Enter executa
digita "pulse-erro-2"; sleep 2.2
wtype -k Return; sleep 1.2
[ -e "$run/marcas/m2" ] || fail "o Enter devia executar a sugestão corrigida"
[ "$(decisao 2)" = aplicada ] || fail "o Enter devia gravar 'aplicada': [$(decisao 2)]"
# 3. Enter por hábito logo depois do erro (antes da folga) não executa
digita "pulse-erro-3"; wtype -k Return; sleep 2.2
[ ! -e "$run/marcas/m3" ] || fail "um Enter por hábito, logo depois do erro, não pode executar a sugestão"
wtype -k Escape; sleep 0.5
# 4. comando destrutivo: o Enter só põe no prompt
touch "$run/marcas/m4"
digita "pulse-erro-4"; sleep 2.2
wtype -k Return; sleep 1
[ -e "$run/marcas/m4" ] || fail "um comando destrutivo não pode rodar com o Enter do cartão (só entra no prompt)"
[ "$(decisao 4)" = inserida ] || fail "o destrutivo devia gravar 'inserida': [$(decisao 4)]"
wtype -M ctrl u -m ctrl; sleep 0.4   # limpa a linha
# 5. Tab põe no prompt sem rodar, e o Enter seguinte roda
digita "pulse-erro-5"; sleep 2.2
wtype -k Tab; sleep 0.8
[ ! -e "$run/marcas/m5" ] || fail "o Tab só coloca no prompt"
[ "$(decisao 5)" = inserida ] || fail "o Tab devia gravar 'inserida': [$(decisao 5)]"
wtype -k Return; sleep 1.2
[ -e "$run/marcas/m5" ] || fail "o Enter seguinte devia rodar o comando que o Tab colocou"
# 6. só explicação: o Enter é do shell, e a primeira tecla some com o cartão
digita "pulse-erro-6"; sleep 2.2
digita "echo seguiu > $run/marcas/m6"; sleep 1
[ -e "$run/marcas/m6" ] || fail "digitar por cima do cartão devia seguir normal"
# 7. um grep que não achou não é erro: nada é perguntado
antes=$(q "select count(*) from emma_interactions")
digita "grep zzzz-nao-existe /etc/hostname"; sleep 2
[ "$(q "select count(*) from emma_interactions")" = "$antes" ] || fail "um grep sem resultado (código 1) não pede correção"
# 8. sem a Emma o terminal segue normal
kill $fake 2>/dev/null; wait $fake 2>/dev/null; rm -f "$run/emma.sock"
digita "pulse-erro-7"; sleep 2
digita "echo vivo > $run/marcas/m8"; sleep 1
[ -e "$run/marcas/m8" ] || fail "sem a Emma o terminal devia seguir funcionando"
kill $pid 2>/dev/null; wait $pid 2>/dev/null; kill $orbit 2>/dev/null
[ "$falhas" -eq 0 ] && echo "pulse (correção de erro com a Emma): ok" || { echo "$falhas falha(s) (log em $run)"; exit 1; }
