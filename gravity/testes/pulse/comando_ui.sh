#!/bin/sh
# O campo da Emma (Ctrl+E) no Pulse de verdade: o pedido em palavras vira um cartão com o comando; nada roda sem confirmação (Enter executa, Tab põe no prompt, Esc
# descarta, destrutivo só entra no prompt); Ctrl+E com texto na linha continua sendo do shell; a Emma que demora ou não responde só mostra um aviso curto.
# sh comando_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; g=${2:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/casa/.config/lumna" "$run/marcas"
printf '[pulse]\nespera_inicio=1\nteto=1\n' > "$run/casa/.config/lumna/pulse.ini"   # a Emma que demora desiste logo
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
decisao() { q "select decisao from emma_interactions where tipo='comando' and prompt_enviado like '%linguagem natural): pulse-nl-$1%' order by id desc limit 1"; }
digita() { wtype "$1"; wtype -k Return; }
ctrl_e() { wtype -M ctrl e -m ctrl; sleep 0.5; }
pedidos() { grep -c "pergunta " "$run/emma.log" 2>/dev/null || echo 0; }

# um comando antes, para o contexto ter o que mostrar
digita "echo contexto-um"; sleep 1
# 1. o pedido em palavras, o cartão e o Enter que executa
ctrl_e
grim "$run/campo.png" 2>/dev/null
wtype "pulse-nl-1 criar o arquivo n1"; sleep 0.4
wtype -k Return; sleep 2.2
grim "$run/cartao.png" 2>/dev/null
[ ! -e "$run/marcas/n1" ] || fail "nada pode rodar antes da confirmação"
wtype -k Return; sleep 1.2
[ -e "$run/marcas/n1" ] || fail "o Enter do cartão devia executar o comando sugerido (a cerca de código foi tirada)"
[ "$(decisao 1)" = aplicada ] || fail "o Enter devia gravar 'aplicada': [$(decisao 1)]"
grep "pulse-nl-1" "$run/emma.log" | grep -q "ultimos_comandos" || fail "o pedido devia levar os últimos comandos da sessão"
grep "pulse-nl-1" "$run/emma.log" | grep -q "contexto-um" || fail "o pedido devia levar o comando que rodou antes"
grep "pulse-nl-1" "$run/emma.log" | grep -q "<pasta>" || fail "o pedido devia levar a pasta"
# 2. destrutivo: só entra no prompt
touch "$run/marcas/n2"
ctrl_e; wtype "pulse-nl-2 apagar o n2"; wtype -k Return; sleep 2.2
wtype -k Return; sleep 1
[ -e "$run/marcas/n2" ] || fail "um comando destrutivo não roda com o Enter do cartão"
[ "$(decisao 2)" = inserida ] || fail "o destrutivo devia gravar 'inserida': [$(decisao 2)]"
wtype -M ctrl u -m ctrl; sleep 0.4
# 3. Esc cancela e nada vai à Emma
antes=$(pedidos)
ctrl_e; wtype "pulse-nl-1 algo"; sleep 0.3; wtype -k Escape; sleep 0.5
[ "$(pedidos)" = "$antes" ] || fail "cancelar com Esc não pode mandar nada à Emma"
digita "echo depois-do-esc > $run/marcas/e1"; sleep 1
[ -e "$run/marcas/e1" ] || fail "depois do Esc o teclado volta ao shell"
# 4. Ctrl+E com texto na linha é do shell (fim da linha), e nada abre
antes=$(pedidos)
wtype "echo par"; wtype -M ctrl e -m ctrl; sleep 0.5; wtype "te > $run/marcas/e2"; wtype -k Return; sleep 1
[ -e "$run/marcas/e2" ] || fail "Ctrl+E com texto na linha devia ir ao fim da linha (do shell): o echo devia ter criado e2"
[ "$(pedidos)" = "$antes" ] || fail "Ctrl+E com texto na linha não abre o campo da Emma"
# 5. só explicação
ctrl_e; wtype "pulse-nl-3 criar"; wtype -k Return; sleep 2.2
wtype -k Return; sleep 0.8   # o Enter de um cartão só de explicação é do shell
wtype -k Escape; sleep 0.3
# 6. a Emma que demora: um aviso curto, sem travar
ctrl_e; wtype "pulse-nl-5 lento"; wtype -k Return; sleep 4.5
digita "echo nao-travou > $run/marcas/e3"; sleep 1
[ -e "$run/marcas/e3" ] || fail "com a Emma demorando o terminal tem de seguir funcionando"
[ ! -e "$run/marcas/n5" ] || fail "a resposta que chegou tarde não pode executar nada"
# 7. editar o pedido: Backspace, setas e Home
ctrl_e; wtype "abcd"; wtype -k Left; wtype -k BackSpace; wtype -k Home; wtype "X"; wtype -k Return; sleep 2
grep -q "linguagem natural): Xabd" "$run/emma.log" || fail "a edição do pedido (Left, BackSpace, Home): o pedido devia ser 'Xabd'"
wtype -k Escape; sleep 0.3
# 8. a Emma fora do ar: aviso, e o terminal segue
kill $fake 2>/dev/null; wait $fake 2>/dev/null; rm -f "$run/emma.sock"
ctrl_e; wtype "pulse-nl-1 qualquer"; wtype -k Return; sleep 2.5
digita "echo vivo > $run/marcas/e4"; sleep 1
[ -e "$run/marcas/e4" ] || fail "sem a Emma o terminal segue funcionando"
kill $pid 2>/dev/null; wait $pid 2>/dev/null; kill $orbit 2>/dev/null
[ "$falhas" -eq 0 ] && echo "pulse (campo da Emma, Ctrl+E): ok" || { echo "$falhas falha(s) (log em $run)"; exit 1; }
