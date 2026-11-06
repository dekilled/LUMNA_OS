#!/bin/sh
# Painel > Emma > Privacidade e Automações, com cliques de verdade (Orbit headless + mouse virtual) contra o serviço da Emma de mentira:
# os "Apagar tudo" e o "Cancelar" do lembrete só valem no Confirmar, e cada um pede ao servidor o que diz.
# sh painel_dados.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY ARRASTO2
orbit_dir=${1:?}; g=${2:?}; M=${3:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna"
export XDG_RUNTIME_DIR=$run XDG_CONFIG_HOME=$run/cfg LUMNA_EMMA_SOCK=$run/emma.sock
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
python3 "$g/testes/emma/servico-falso.py" "$run/emma.sock" "$run/emma.log" & sv=$!
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 3
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
clicar_em() { "$M" casa; sleep 0.2; "$M" mover "$1" "$2"; sleep 0.3; "$M" clicar; sleep 0.8; }
ate_que() { cond=$1; shift; for _ in 1 2 3; do clicar_em "$@"; sleep 0.5; eval "$cond" && return 0; done; return 1; }
abrir() { "$g/painel" --secao "$1" >"$run/p.log" 2>&1 & pid=$!; sleep 5; }
fechar() { kill $pid 2>/dev/null; wait $pid 2>/dev/null; }
tem() { grep -q "$1" "$run/emma.log"; }

# Privacidade: ler as contagens e apagar as conversas do Chat (1º toque só pede confirmação: nada some ainda)
abrir emma/privacidade
tem "GET /api/projects/p2/memories" || fail "o Painel devia ler as memórias de cada projeto"
clicar_em 1115 381
tem "DELETE /api/sessions" && fail "o primeiro toque em Apagar não pode apagar nada"
clicar_em 1010 381
sleep 2
for c in c1 c2 c3; do tem "DELETE /api/sessions/$c" || fail "devia apagar a conversa $c"; done
tem "DELETE /api/sessions/a1" && fail "as conversas da popup não eram para ser apagadas"
# e as memórias dos projetos
clicar_em 1115 525
clicar_em 1010 525
sleep 2
for m in m1 m2 m3; do tem "DELETE /api/memories/$m" || fail "devia apagar a memória $m"; done
tem "DELETE /api/sessions/a1" && fail "as conversas da popup continuam intactas"
fechar

# Automações: cancelar o lembrete pendente (com confirmação)
abrir emma/automacoes
clicar_em 1127 381
tem "DELETE /api/reminders" && fail "o primeiro toque em Cancelar não pode cancelar"
clicar_em 1010 381
sleep 2
tem "DELETE /api/reminders/r1" || fail "devia cancelar o lembrete r1"
fechar

kill $orbit $sv 2>/dev/null
[ "$falhas" -eq 0 ] && echo "painel Emma (privacidade e automações): ok" || { echo "$falhas falha(s)"; exit 1; }
