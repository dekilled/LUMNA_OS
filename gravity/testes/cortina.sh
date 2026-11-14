#!/bin/sh
# A cortina preta do login (orbit/cortina.c): com ORBIT_CORTINA=1 o desktop nunca aparece antes da abertura. Num Orbit sem tela com o papel de parede de verdade:
# coberto de preto enquanto a abertura não aparece; a abertura aparece e a cortina sai; sem a abertura a cortina sai no prazo; sem ORBIT_CORTINA o papel aparece.
# Uso: sh testes/cortina.sh <pasta do orbit> (com make papel abertura)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
ln -s "$gravity/abertura" "$run/bin/lumna-abertura"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run" LUMNA_ABERTURA_DIR="$gravity/../iso/branding/abertura" LUMNA_ABERTURA_AO=null LUMNA_ABERTURA_CAMADAS=""
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
sobe() { # $1: o que ficar no ambiente do Orbit (ORBIT_CORTINA=1 ORBIT_CORTINA_MS=...)
	pkill -x orbit 2>/dev/null; sleep 0.5; rm -f "$run"/orbit.*.sock "$run"/wayland-*
	(cd "$orbit" && env $1 LUMNA_TESTE_TECLADOS=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 &)
	for i in 1 2 3 4 5 6 7 8 9 10 11 12; do ls "$run"/orbit.*.sock >/dev/null 2>&1 && break; sleep 0.2; done
	sock=$(ls "$run"/orbit.*.sock | head -1); export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$')
}
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
luz() { timeout 5 grim "$run/f.png" 2>/dev/null && python3 -W ignore -c "
from PIL import Image
im = Image.open('$run/f.png').convert('L'); print(round(sum(im.getdata()) / (im.width * im.height), 1))"; }
# 1. sem cortina: o papel de parede aparece (controle negativo do resto)
sobe "ORBIT_X=1"
"$gravity/papel" >/dev/null 2>&1 & p1=$!
sleep 2.5; sem=$(luz); kill $p1 2>/dev/null
echo "info: sem cortina o papel dá luz $sem"
python3 -c "import sys; sys.exit(0 if $sem > 8 else 1)" || falha "sem cortina o papel de parede devia aparecer (luz $sem)"
# 2. com cortina: o papel sobe e a tela continua preta; a abertura aparece e a cortina sai
sobe "ORBIT_CORTINA=1 ORBIT_CORTINA_MS=15000"
[ "$(msg cortina | grep -c '"cortina":true')" = 1 ] || falha "a cortina devia estar ligada"
"$gravity/papel" >/dev/null 2>&1 & p2=$!
sleep 1.5; coberto=$(luz)
echo "info: com cortina e o papel já na tela a luz é $coberto"
python3 -c "import sys; sys.exit(0 if $coberto < 1 else 1)" || falha "com a cortina a tela devia seguir preta mesmo com o papel de parede na tela (luz $coberto)"
(lumna-abertura 2>"$run/ab.log" &)
sleep 3.5; msg camadas | grep -q lumna-abertura || falha "a abertura devia estar na tela"
[ "$(msg cortina | grep -c '"cortina":false')" = 1 ] || falha "a cortina devia sair quando a abertura aparece"
aberta=$(luz); echo "info: com a abertura no ar a luz é $aberta"
python3 -c "import sys; sys.exit(0 if $aberta > 3 else 1)" || falha "a abertura devia aparecer depois da cortina (luz $aberta)"
kill $p2 2>/dev/null
# 3. a abertura nunca vem: a cortina sai no prazo
sobe "ORBIT_CORTINA=1 ORBIT_CORTINA_MS=3500"
"$gravity/papel" >/dev/null 2>&1 & p3=$!
sleep 1; [ "$(msg cortina | grep -c '"cortina":true')" = 1 ] || falha "a cortina devia estar ligada logo que o Orbit sobe"
sleep 3.5; [ "$(msg cortina | grep -c '"cortina":false')" = 1 ] || falha "sem a abertura a cortina devia sair no prazo (3.5 s)"
descoberto=$(luz); python3 -c "import sys; sys.exit(0 if $descoberto > 8 else 1)" || falha "depois do prazo o papel devia aparecer (luz $descoberto)"
kill $p3 2>/dev/null
# 4. "orbit-msg cortina tirar" descobre na hora
sobe "ORBIT_CORTINA=1 ORBIT_CORTINA_MS=15000"
msg cortina tirar | grep -q '"cortina":false' || falha "cortina tirar devia descobrir as telas"
pkill -x orbit 2>/dev/null
[ $ok = 1 ] && { echo "cortina: ok"; rm -rf "$run"; } || { echo "(em $run)"; exit 1; }
