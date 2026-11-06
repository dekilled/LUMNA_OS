#!/bin/sh
# Como teste_alt.sh, mas com o alvo e o tempo parado na borda: sh teste_borda.sh PASTA_DO_BUILD [X_ALVO] [DY] [ESPERA_MS]
B=$1; ALVO_X=${2:-1279}; DY=${3:-0}; ESPERA=${4:-300}
pkill -x orbit; pkill -x weston-simple-shm; pkill -x wtype; sleep 1
export XDG_RUNTIME_DIR=/tmp/claude-0/drag/run2 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2
mkdir -p $XDG_RUNTIME_DIR /tmp/claude-0/drag/cfg2; chmod 700 $XDG_RUNTIME_DIR; rm -f $XDG_RUNTIME_DIR/wayland-* $XDG_RUNTIME_DIR/orbit.*
(cd $B; XDG_CONFIG_HOME=/tmp/claude-0/drag/cfg2 ./orbit > /tmp/claude-0/drag/o2.log 2>&1 &)
sleep 2
export WAYLAND_DISPLAY=wayland-0
H="env ORBIT_SOCK=$XDG_RUNTIME_DIR/orbit.wayland-0.sock $B/orbit-msg"
(weston-simple-shm > /dev/null 2>&1 &)
sleep 3
J=$($H janelas)
python3 - "$J" <<'PY' > /tmp/claude-0/drag/jan.txt
import json,sys
j=json.loads(sys.argv[1])['janelas'][0]; print(j['saida'], j['x'], j['y'], j['w'], j['h'], j['flutuante'])
PY
read SAIDA X Y W HH FLUT < /tmp/claude-0/drag/jan.txt
echo "janela em $SAIDA x=$X y=$Y ${W}x${HH} flutuante=$FLUT"
# saídas: a da esquerda e onde acaba (x=1280)
GX=$((X + ${OFF:-40})); GY=$((Y + 40))
/tmp/claude-0/drag/arrasto2 casa; sleep 0.3
/tmp/claude-0/drag/arrasto2 mover $GX $GY; sleep 0.3
(wtype -M alt -s 7000 -m alt >/dev/null 2>&1 &)
sleep 0.8
# arrasta até a última coluna do monitor da esquerda (x=1279)
ALVO=$((ALVO_X - GX))
/tmp/claude-0/drag/arrasto2 arrastar $ALVO $DY $ESPERA
sleep 0.8
$H janelas | python3 -c "import json,sys; j=json.load(sys.stdin)['janelas'][0]; print('depois: saida', j['saida'], 'x', j['x'], 'y', j['y'], 'w', j['w'], 'h', j['h'], 'chaves', [k for k in j if 'snap' in k or 'enc' in k or 'max' in k], {k: j[k] for k in j if 'snap' in k or 'enc' in k or 'max' in k})"
pkill -x orbit; pkill -x weston-simple-shm; pkill -x wtype
