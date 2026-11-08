#!/bin/sh
# O Pulse num Orbit headless: o que se digita no shell vai para a memória (o SQLite) com o código de saída e o fim da saída, os segredos saem trocados por ***,
# o comando que começa com espaço não é lembrado, e --limpar-historico apaga tudo. sh memoria_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; g=${2:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/casa"
export XDG_RUNTIME_DIR=$run HOME=$run/casa XDG_DATA_HOME=$run/casa/.local/share LANG=C.UTF-8 SHELL=/bin/bash PS1='$ ' HISTCONTROL=ignorespace HISTFILE=/dev/null
export LUMNA_PULSE_INTEGRACAO=$g/pulse-shell/integracao.bash LUMNA_EMMA_SOCK=$run/nao-ha-emma.sock
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 3
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
"$g/pulse" >"$run/p.log" 2>&1 &
pid=$!
sleep 4
digita() { wtype "$1"; wtype -k Return; sleep 0.8; }
digita "echo ola-pulse"
digita "ls /nao-existe-aqui"
digita " echo comecou-com-espaco"
digita "curl --password=hunter2 -H 'Authorization: Bearer TOKEN123' http://localhost:1/x"
digita "false"
BD=$run/casa/.local/share/lumna/pulse/pulse.db
[ -f "$BD" ] || fail "o banco devia existir em $BD"
q() { python3 -c "import sqlite3,sys; c=sqlite3.connect(sys.argv[1]); print(c.execute(sys.argv[2]).fetchone()[0])" "$BD" "$1" 2>/dev/null; }
[ "$(q "select count(*) from commands where command='echo ola-pulse' and exit_code=0")" = 1 ] || fail "o echo devia estar na memória com código 0"
[ "$(q "select exit_code from commands where command='ls /nao-existe-aqui'")" = 2 ] || fail "o ls que falhou devia ter código 2"
q "select saida_snippet from commands where command='ls /nao-existe-aqui'" | grep -q "nao-existe-aqui" || fail "a mensagem de erro do ls devia ficar guardada"
[ "$(q "select count(*) from commands where command like '%comecou-com-espaco%'")" = 0 ] || fail "um comando com espaço na frente não pode ser lembrado"
[ "$(q "select count(*) from commands where command like '%hunter2%' or command like '%TOKEN123%'")" = 0 ] || fail "os segredos não podem ficar no banco"
[ "$(q "select count(*) from commands where command like '%--password=***%' and command like '%Bearer ***%'")" = 1 ] || fail "os segredos devem sair trocados por ***"
[ "$(q "select exit_code from commands where command='false'")" = 1 ] || fail "o false devia ter código 1"
[ "$(q "select count(*) from sessions")" = 1 ] || fail "uma sessão por janela"
[ "$(stat -c %a "$BD")" = 600 ] || fail "o banco devia ser 600: $(stat -c %a "$BD")"
digita "exit"   # a janela fecha e o Pulse encerra a sessão no banco
for _ in $(seq 1 20); do kill -0 $pid 2>/dev/null || break; sleep 0.3; done
kill $pid 2>/dev/null; wait $pid 2>/dev/null
[ "$(q "select count(*) from sessions where ended_at is not null")" = 1 ] || fail "ao sair, a sessão devia ficar encerrada no banco"
# os comandos da linha de comando
"$g/pulse" --migrar-banco | grep -q "versão 1" || fail "--migrar-banco devia dizer a versão"
"$g/pulse" --esquema | grep -q "CREATE TABLE commands" || fail "--esquema devia mostrar o SQL"
"$g/pulse" --limpar-historico | grep -q apagado || fail "--limpar-historico devia avisar"
[ "$(q "select count(*) from commands")" = 0 ] || fail "depois de limpar não sobra comando"
kill $orbit 2>/dev/null
[ "$falhas" -eq 0 ] && echo "pulse (memória do terminal): ok" || { echo "$falhas falha(s) (log em $run)"; exit 1; }
