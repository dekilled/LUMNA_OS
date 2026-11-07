#!/bin/sh
# O instalador com cliques de verdade num Orbit headless e o mouse virtual: passa pelos seis passos (disco com o aviso, nome,
# senha, resumo) e confere o que chegou ao lumna-install de mentira (os argumentos e a senha pela entrada padrão).
# sh instalador_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY ARRASTO2
orbit_dir=${1:?}; g=${2:?}; M=${3:?}
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run HOME=$run PATH=$g/testes/instalador/bin:$PATH LUMNA_INSTALADOR_CMD=$g/testes/instalador/instalar-falso.sh FALSO_DIR=$run FALSO_MODO=ok
chmod +x "$g/testes/instalador/instalar-falso.sh" "$g/testes/instalador/bin/lsblk"
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 4
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
clk() { "$M" casa; sleep 0.2; "$M" mover "$1" "$2"; sleep 0.3; "$M" clicar; sleep 0.9; }
"$g/instalador" >"$run/i.log" 2>&1 &
pid=$!
sleep 4
clk 836 507            # Boas-vindas: Instalar
clk 1030 650           # Região: Continuar
clk 745 276            # Disco: o segundo (/dev/sdb)
clk 1030 650           # sem aceitar o aviso, o Continuar não vale
clk 1026 430           # Entendi
clk 1030 650           # Continuar
clk 745 223; wtype 'Maria da Silva'; sleep 0.4
clk 745 385; wtype 'segredo1'; sleep 0.3
clk 745 466; wtype 'segredo1'; sleep 0.4
clk 1030 650           # Você: Continuar
clk 488 441            # Resumo: Instalar agora
for _ in $(seq 1 30); do [ -s "$run/args" ] && [ -s "$run/senha" ] && break; sleep 0.5; done
args=$(cat "$run/args" 2>/dev/null); senha=$(cat "$run/senha" 2>/dev/null)
[ "$args" = "--disk /dev/sdb --user maria --name Maria da Silva --hostname lumna --timezone America/Sao_Paulo --keyboard br abnt2 --locale pt_BR.UTF-8" ] \
	|| fail "o instalador devia receber o plano: [$args]"
[ "$senha" = segredo1 ] || fail "a senha devia chegar pela entrada padrão: [$senha]"
case "$args" in *segredo*) fail "a senha foi na linha de comando" ;; esac
sleep 2
grim "$run/final.png" 2>/dev/null
kill $pid 2>/dev/null; wait $pid 2>/dev/null
kill $orbit 2>/dev/null
[ "$falhas" -eq 0 ] && echo "instalador (cliques): ok" || { echo "$falhas falha(s)"; exit 1; }
