#!/bin/sh
# O lumna-abertura de verdade num Orbit sem tela, com o arquivo de verdade (iso/branding/abertura/abertura.mp4): aparece por cima de tudo, toca o
# começo, entra no laço enquanto o desktop "não está pronto", sai do laço sem salto quando a barra, a dock e o papel aparecem, toca o fim, esmaece e
# sai. Também: pular com uma tecla, e a opção "abertura = nao" não abre nada. Uso: sh testes/abertura.sh <pasta do orbit> (com make abertura)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd); dir=$gravity/../iso/branding/abertura
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
ln -s "$gravity/abertura" "$run/bin/lumna-abertura"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run" LUMNA_ABERTURA_DIR="$dir" LUMNA_ABERTURA_AO=null LUMNA_ABERTURA_LOG=1
ok=1
falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && LUMNA_TESTE_TECLADOS=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 &)
sleep 2.5; sock=$(ls "$run"/orbit.*.sock | head -1)
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
camada() { msg camadas | grep -c 'lumna-abertura'; }
vivo() { ps -eo stat=,args= | grep "[l]umna-abertura" | grep -qv "^Z"; }   # (sem os zumbis: o init do contêiner não os recolhe)
wl=$(ls "$run"/wayland-* 2>/dev/null | grep -v lock | head -1); export WAYLAND_DISPLAY=$(basename "$wl")
claro() { # a luz média do quadro de agora (0..255): preto = 0
	timeout 5 grim "$run/f.png" 2>/dev/null && python3 -c "
from PIL import Image
im = Image.open('$run/f.png').convert('L')
print(round(sum(im.getdata()) / (im.width * im.height)))"
}
# ---- 1. um giro completo, com o desktop já pronto (LUMNA_ABERTURA_CAMADAS vazio): começo + F + fim ~ 6.8 s ----
export LUMNA_ABERTURA_CAMADAS=""
abertura_inicio=$(date +%s.%N)
(lumna-abertura 2>"$run/ab1.log" &)
sleep 1.0
[ "$(camada)" -ge 1 ] || falha "a abertura devia aparecer na camada ao abrir"
l1=$(claro)
sleep 3.2; l2=$(claro)                 # ~4.2 s: o logo está aceso
[ "${l2:-0}" -gt 3 ] || falha "no meio do começo o quadro devia ter o logo aceso (luz $l2)"
sleep 12
vivo && falha "depois do fim e do esmaecer o programa devia ter saído"
[ "$(camada)" = 0 ] || falha "a camada devia sumir no fim"
fim=$(date +%s.%N)
dur=$(python3 -c "print(round($fim-$abertura_inicio-1.0,1))")
echo "info: giro completo (logo na luz $l2) — o teste só olha se saiu; veja o log:"; grep -E 'busca|pronto|acabou|saindo' "$run/ab1.log" | head -8
grep -q 'o desktop está pronto' "$run/ab1.log" || falha "devia registrar que o desktop ficou pronto"
grep -q 'acabou o vídeo' "$run/ab1.log" || falha "devia registrar o fim do vídeo"
# ---- 2. pular com uma tecla depois de 1 s: vai direto ao fim ----
export LUMNA_ABERTURA_CAMADAS="camada-que-nunca-existe"
(lumna-abertura 2>"$run/ab2.log" &)
sleep 2.2
[ "$(camada)" -ge 1 ] || falha "a abertura devia estar na tela (sem o desktop pronto ela fica no laço)"
msg tecla_falsa 0 28 >/dev/null; sleep 0.1; msg tecla_falsa 0 28 >/dev/null
sleep 3.5
vivo && falha "uma tecla devia pular para o fim e o programa sair em poucos segundos"
grep -q 'pulou' "$run/ab2.log" || falha "devia registrar o pulo"
# ---- 2b. pronto no meio do laço: sai sem salto (em F termina F; em R espelha para o mesmo quadro de F e termina F) ----
ini="$dir/abertura.ini"
fr=$(sed -n 's/^frente *= *//p' "$ini"); tr=$(sed -n 's/^duracao_trecho *= *//p' "$ini"); fi=$(sed -n 's/^duracao_fim *= *//p' "$ini")
conta() { # $1: o instante do pronto; confere quando o fim começa e a busca de saída do laço
	export LUMNA_ABERTURA_PRONTO_EM=$1 LUMNA_ABERTURA_CAMADAS="camada-que-nunca-existe"
	(lumna-abertura 2>"$run/ab_p.log" &)
	sleep $(python3 -c "print($1 + $tr * 2 + $fi + 2.5)")
	vivo && falha "pronto em $1: o programa devia ter saído"
	unset LUMNA_ABERTURA_PRONTO_EM
}
# pronto em R (segundo meio-ciclo): fr + 1.2*tr (R) -> espelha: salta para F(tr - r) com r = 0.2*tr
em_r=$(python3 -c "print(round($fr + 1.2*$tr, 3))")
conta "$em_r"
pronto_log=$(sed -n 's/^abertura\[principal \([0-9.]*\)\]: o desktop está pronto$/\1/p' "$run/ab_p.log" | head -1)
esperado_espelho=$(python3 -c "print(round($fr + $tr - ($pronto_log - $fr - $tr), 3))")
b1=$(grep -o 'busca [0-9.]* ([a-z]*)' "$run/ab_p.log" | head -3 | tr '\n' ' ')
echo "info: pronto em R ($em_r s; o log o viu em $pronto_log): $b1(esperado o espelho ~ $esperado_espelho)"
busca1=$(sed -n 's/^abertura\[principal [0-9.]*\]: busca \([0-9.]*\) (salto)$/\1/p' "$run/ab_p.log" | head -1)
python3 -c "import sys; sys.exit(0 if abs($busca1 - $esperado_espelho) < 0.08 else 1)" || falha "pronto em R devia buscar o mesmo quadro de F (~ $esperado_espelho, buscou $busca1)"
grep -q 'acabou o vídeo' "$run/ab_p.log" || falha "pronto em R: devia chegar ao fim"
# pronto em F (do segundo giro): continua em F, sem busca de espelho; só a busca do F para o fim
em_f=$(python3 -c "print(round($fr + 2*$tr + 0.3*$tr, 3))")
conta "$em_f"
b2=$(grep -c 'busca' "$run/ab_p.log")
echo "info: pronto em F ($em_f s): $(grep -o 'busca [0-9.]* ([a-z]*)' "$run/ab_p.log" | tr '\n' ' ')"
grep -q 'acabou o vídeo' "$run/ab_p.log" || falha "pronto em F: devia chegar ao fim"
# ---- 3. desligada no Painel: não abre nada ----
printf '[geral]\nabertura = nao\n' > "$run/cfg/lumna/animacoes.ini"
(lumna-abertura 2>"$run/ab3.log" &)
sleep 1.5
[ "$(camada)" = 0 ] || falha "com abertura = nao nada devia abrir"
vivo && falha "com abertura = nao o programa devia sair na hora"
pkill -x orbit 2>/dev/null
[ $ok = 1 ] && { echo "abertura: ok"; rm -rf "$run"; } || { echo "(em $run)"; exit 1; }
