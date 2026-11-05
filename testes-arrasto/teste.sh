#!/bin/sh
# $1 = pasta do orbit (aninhado). Host headless com 2 monitores; o nested com 2 saídas; aperta, anda para o outro monitor e vê onde está o cursor do nested.
B=$1
pkill -x orbit; pkill -x arrasto; sleep 1
export XDG_RUNTIME_DIR=/tmp/claude-0/ob/run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1
rm -f $XDG_RUNTIME_DIR/wayland-* $XDG_RUNTIME_DIR/orbit.*
mkdir -p /tmp/claude-0/drag/cfgh /tmp/claude-0/drag/cfgn
(cd /tmp/claude-0/orbit_host; WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 XDG_CONFIG_HOME=/tmp/claude-0/drag/cfgh ./orbit > /tmp/claude-0/drag/host.log 2>&1 &)
sleep 2
(cd $B; WAYLAND_DISPLAY=wayland-0 XDG_CONFIG_HOME=/tmp/claude-0/drag/cfgn ./orbit > /tmp/claude-0/drag/nest.log 2>&1 &)
sleep 4
N="env ORBIT_SOCK=$XDG_RUNTIME_DIR/orbit.wayland-1.sock $B/orbit-msg"
ls $XDG_RUNTIME_DIR | tr '\n' ' '; echo
onde() { $N saidas | python3 -c "import json,sys; print([s['nome'] for s in json.load(sys.stdin)['saidas'] if s['ponteiro']], [(s['nome'],s['x']) for s in json.load(sys.stdin)['saidas']] if False else '')"; }
export WAYLAND_DISPLAY=wayland-0
/tmp/claude-0/drag/arrasto 3000 so-mover; sleep 0.5
echo "cursor do nested no canto esquerdo: $(onde)"
echo "arrastando -1500 px para a esquerda com o botão apertado..."
/tmp/claude-0/drag/arrasto -1500 x 2 &
sleep 3.5
echo "durante o arrasto: $(onde)"
sleep 2
pkill -x orbit
