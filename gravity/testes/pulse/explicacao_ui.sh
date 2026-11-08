#!/bin/sh
# A explicação antes de executar, no Pulse de verdade: o Enter de um comando da lista (pulse.ini: explicar_comandos=rm,touch) fica seguro e a Emma explica; Enter confirma
# (só depois de uma folga), Esc cancela e deixa o comando no prompt; o que não está na lista roda direto; sem a Emma o aviso fixo aparece e a decisão é da pessoa.
# sh explicacao_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; g=${2:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/casa/.config/lumna" "$run/m"
printf '[pulse]\nespera_inicio=1\nteto=1\nexplicar_comandos=rm,touch\n' > "$run/casa/.config/lumna/pulse.ini"
export XDG_RUNTIME_DIR=$run HOME=$run/casa XDG_DATA_HOME=$run/casa/.local/share XDG_CONFIG_HOME=$run/casa/.config LANG=C.UTF-8 SHELL=/bin/bash PS1='$ ' HISTFILE=/dev/null
export LUMNA_PULSE_INTEGRACAO=$g/pulse-shell/integracao.bash LUMNA_EMMA_SOCK=$run/emma.sock EMMA_FALSA_DIR=$run/m
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
decisao() { q "select decisao from emma_interactions where tipo='explicacao' and prompt_enviado like '%<comando>$1</comando>%' order by id desc limit 1"; }
explicacoes() { q "select count(*) from emma_interactions where tipo='explicacao'"; }
digita() { wtype "$1"; wtype -k Return; }
M=$run/m

# 1. Esc cancela: nada roda e a linha continua no prompt
touch $M/x1
digita "rm -f $M/x1"; sleep 2.2
grim "$run/cartao.png" 2>/dev/null
[ -e $M/x1 ] || fail "o Enter de um comando da lista não pode rodar antes da confirmação"
wtype -k Escape; sleep 0.6
[ -e $M/x1 ] || fail "o Esc cancela: o rm não pode rodar"
[ "$(decisao "rm -f $M/x1")" = cancelada ] || fail "o Esc devia gravar 'cancelada': [$(decisao "rm -f $M/x1")]"
# 2. o comando ficou no prompt: o Enter de novo pede a explicação outra vez, e a confirmação roda
wtype -k Return; sleep 2.2
wtype -k Return; sleep 1.2
[ ! -e $M/x1 ] || fail "o Enter do cartão (depois da folga) devia executar o comando"
[ "$(decisao "rm -f $M/x1")" = confirmada ] || fail "o Enter devia gravar 'confirmada': [$(decisao "rm -f $M/x1")]"
# 3. o Enter dobrado por hábito não confirma
digita "touch $M/t1"; wtype -k Return; sleep 2.2
[ ! -e $M/t1 ] || fail "o Enter dobrado logo depois (antes da folga) não pode confirmar"
wtype -k Escape; sleep 0.4; wtype -M ctrl u -m ctrl; sleep 0.3
# 4. o que não está na lista (o chmod -R está na padrão, não na do ini) roda direto, sem perguntar
mkdir -p $M/pasta; antes=$(explicacoes)
digita "chmod -R 777 $M/pasta"; sleep 1.2
[ "$(stat -c %a $M/pasta)" = 777 ] || fail "um comando fora da lista do ini roda direto (chmod -R 777): $(stat -c %a $M/pasta)"
digita "echo normal > $M/c1"; sleep 1
[ -e $M/c1 ] || fail "um comando normal roda direto"
[ "$(explicacoes)" = "$antes" ] || fail "o que não está na lista não pede explicação"
# 5. a Emma que demora: o cartão fica com o aviso fixo e a decisão é da pessoa
touch "$M/pulse-lento-x2"
digita "rm -f $M/pulse-lento-x2"; sleep 4.2
[ -e "$M/pulse-lento-x2" ] || fail "com a Emma demorando o comando continua seguro"
wtype -k Return; sleep 1.2
[ ! -e "$M/pulse-lento-x2" ] || fail "depois do aviso, o Enter executa (a decisão é da pessoa)"
# 6. outra tecla com o cartão aberto: ele some, a tecla segue e o Enter do shell volta ao normal
touch $M/x3
digita "rm -f $M/x3"; sleep 2.2
wtype "Z"; sleep 0.5
[ "$(decisao "rm -f $M/x3")" = ignorada ] || fail "uma tecla qualquer devia gravar 'ignorada': [$(decisao "rm -f $M/x3")]"
wtype -M ctrl u -m ctrl; sleep 0.3
[ -e $M/x3 ] || fail "o x3 não pode ter sido apagado"
# 7. a Emma fora do ar
kill $fake 2>/dev/null; wait $fake 2>/dev/null; rm -f "$run/emma.sock"
touch $M/x4
digita "rm -f $M/x4"; sleep 4
[ -e $M/x4 ] || fail "sem a Emma o comando perigoso continua esperando a pessoa"
wtype -k Return; sleep 1.2
[ ! -e $M/x4 ] || fail "sem a Emma, o Enter do cartão executa"
kill $pid 2>/dev/null; wait $pid 2>/dev/null; kill $orbit 2>/dev/null
[ "$falhas" -eq 0 ] && echo "pulse (explicação antes de executar): ok" || { echo "$falhas falha(s) (log em $run)"; exit 1; }
