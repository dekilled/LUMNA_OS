#!/bin/sh
# Prova que a troca de modo não vaza a janela para o monitor vizinho: orbit-msg com 2 saídas headless, uma janela em Mídia,
# troca para Desktop e compara a saída vizinha com grim. Uso: sh teste-animacao.sh PASTA_DO_BUILD ROTULO (precisa de grim, weston-simple-shm e python3-pil).
# Antes da correção (clipping em anim.c) a vizinha mudava; depois, fica igual.
B=$1
pkill -x orbit; pkill -x weston-simple-shm; sleep 1
export XDG_RUNTIME_DIR=/tmp/claude-0/anim/run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2
mkdir -p $XDG_RUNTIME_DIR /tmp/claude-0/anim/cfg/lumna; chmod 700 $XDG_RUNTIME_DIR
rm -f $XDG_RUNTIME_DIR/wayland-* $XDG_RUNTIME_DIR/orbit.*
printf '[geral]\nanimacao = 400\n' > /tmp/claude-0/anim/cfg/orbit.ini
mkdir -p /tmp/claude-0/anim/cfg/orbit; cp /tmp/claude-0/anim/cfg/orbit.ini /tmp/claude-0/anim/cfg/orbit/orbit.ini
cd $B
(XDG_CONFIG_HOME=/tmp/claude-0/anim/cfg ./orbit > /tmp/claude-0/anim/orbit.log 2>&1 &)
sleep 2
H="env ORBIT_SOCK=$XDG_RUNTIME_DIR/orbit.wayland-0.sock $B/orbit-msg"
$H saidas | python3 -c "import json,sys; [print(s['nome'],s['x'],s['y'],s['largura'],s['altura']) for s in json.load(sys.stdin)['saidas']]"
$H modo midia HEADLESS-1 >/dev/null; $H modo midia HEADLESS-2 >/dev/null; sleep 4
(WAYLAND_DISPLAY=wayland-0 weston-simple-shm > /tmp/claude-0/anim/cli.log 2>&1 &)
sleep 3
J=$($H janelas); echo "$J" | head -c 330; echo
O=$(echo "$J" | python3 -c "import json,sys; print(json.load(sys.stdin)['janelas'][0]['saida'])")
N=$([ "$O" = HEADLESS-1 ] && echo HEADLESS-2 || echo HEADLESS-1)
echo "janela em $O, vizinha $N"
grim -o $N /tmp/claude-0/anim/antes1.png; grim -o $O /tmp/claude-0/anim/antes_O.png
$H modo desktop $O >/dev/null
sleep 0.15
grim -o $N /tmp/claude-0/anim/ida_$2.png; grim -o $O /tmp/claude-0/anim/ida_O_$2.png
sleep 4
$H modo midia $O >/dev/null
sleep 0.15
grim -o $N /tmp/claude-0/anim/volta_$2.png
sleep 3
pkill -x orbit; pkill -x weston-simple-shm
python3 - <<PY
from PIL import Image, ImageChops
a=Image.open('/tmp/claude-0/anim/antes1.png').convert('RGB')
for f in ('ida','volta'):
    b=Image.open('/tmp/claude-0/anim/%s_$2.png'%f).convert('RGB')
    d=ImageChops.difference(a,b).getbbox()
    print(f, 'saida vizinha intacta' if d is None else 'VAZOU para a saida vizinha %s'%(d,), '($2)')
PY
