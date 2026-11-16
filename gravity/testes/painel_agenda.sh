#!/bin/sh
# Painel > Sistema > Sobre > Atualizações do sistema: a atualização agendada e o "Só o LUMNA". Num Orbit sem tela, com o checkupdates/pacman de mentira (1 atualização do
# LUMNA e 2 do Arch) e um lumna-pkg de mentira que só anota os argumentos: confere o que a página mostra (a configuração e a última execução) e o que os cliques mandam
# (dia, "Tudo"/"Só o LUMNA", interruptor). Uso: sh testes/painel_agenda.sh <pasta do orbit>   (com make painel; precisa de grim e python3 com PIL)
orbit=${1:?}; g=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home"
python3 "$g/testes/painel/monta_sistema_falso.py" "$run/raiz"   # (a configuração: ligado, só LUMNA, 04:30, seg,qua,sex; a última execução: ok)
printf '#!/bin/sh\necho "$*" >> %s/pkg.log\n' "$run" > "$run/lumna-pkg-falso"; chmod +x "$run/lumna-pkg-falso"
(cd "$orbit" && XDG_RUNTIME_DIR="$run" XDG_CONFIG_HOME="$run/cfg" WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 &)
sleep 2.5
W=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1)
export XDG_RUNTIME_DIR="$run" WAYLAND_DISPLAY="$W" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" LUMNA_RAIZ_SISTEMA="$run/raiz" LUMNA_ETC="$run/raiz/etc" LUMNA_VARLIB="$run/raiz/var/lib/lumna" LUMNA_PKG="$run/lumna-pkg-falso" LUMNA_PKG_SEM_PKEXEC=1 PATH="$run/raiz/bin:$PATH"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
m() { "$orbit/orbit-msg" -s "$sock" "$@" >/dev/null; }
clica() { m mouse mover "$1" "$2"; sleep 0.3; m mouse botao baixo; sleep 0.1; m mouse botao cima; sleep 1.5; }
cheio() { # o botão neste ponto é roxo (marcado)?
	python3 -W ignore -c "
from PIL import Image
r, g, b = Image.open('$run/t.png').convert('RGB').getpixel(($1, $2))
print('sim' if r > 140 and b > 200 else 'nao')"
}
"$g/painel" --secao sobre/atualizacoes >"$run/p.log" 2>&1 & pid=$!
sleep 6
timeout 5 grim "$run/t.png" || falha "grim"
# a página mostra a configuração gravada: Seg, Qua e Sex marcados; Ter, Qui, Sáb, Dom não; o interruptor ligado
for x in 684 840 997; do [ "$(cheio $x 535)" = sim ] || falha "o dia em x=$x devia estar marcado (seg, qua, sex)"; done
for x in 762 920 1075 1157; do [ "$(cheio $x 535)" = nao ] || falha "o dia em x=$x NÃO devia estar marcado"; done
# um dia a mais (Qui): o Painel manda o lumna-pkg agendar com os dias novos, o que atualizar e o horário que já estavam
clica 920 552
grep -qx "agendar ligado lumna 04:30 seg,qua,qui,sex" "$run/pkg.log" || falha "Qui devia mandar: agendar ligado lumna 04:30 seg,qua,qui,sex (foi: $(cat "$run/pkg.log" 2>&1))"
# "Tudo": o que atualizar muda e o resto fica
clica 1011 477
grep -qx "agendar ligado tudo 04:30 seg,qua,sex" "$run/pkg.log" || falha "Tudo devia mandar: agendar ligado tudo 04:30 seg,qua,sex (foi: $(cat "$run/pkg.log" 2>&1))"
# o interruptor desliga
clica 1165 397
grep -qx "agendar desligado lumna 04:30 seg,qua,sex" "$run/pkg.log" || falha "o interruptor devia mandar: agendar desligado lumna 04:30 seg,qua,sex (foi: $(tail -1 "$run/pkg.log"))"
# procurar: aparece "1 do LUMNA" e o botão "Só o LUMNA"
clica 1125 278; sleep 4
timeout 5 grim "$run/t.png"
python3 -W ignore -c "
from PIL import Image
im = Image.open('$run/t.png').convert('RGB')
# o botão 'Só o LUMNA' (superfície elevada com contorno) existe ao lado de 'Atualizar tudo' (roxo)? confere o roxo do 'Atualizar tudo' e o texto do contador
roxos = sum(1 for r, g, b in im.crop((1020, 262, 1195, 308)).getdata() if r > 140 and b > 200)
print('sim' if roxos > 500 else 'nao')" | grep -q sim || falha "depois de Procurar devia aparecer o botão Atualizar tudo (roxo)"
kill $pid 2>/dev/null; pkill -x orbit
[ $ok = 1 ] && { rm -rf "$run"; echo "painel_agenda: ok"; } || { echo "(em $run)"; exit 1; }
