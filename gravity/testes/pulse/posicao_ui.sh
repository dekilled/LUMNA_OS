#!/bin/sh
# Uma janela livre que ficou com o título atrás da barra de cima (aberta, ou arrastada, antes de a barra subir) volta para a área útil quando
# a barra aparece. sh posicao_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY ARRASTO2
orbit_dir=${1:?}; g=${2:?}; M=${3:?}
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run HOME=$run
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 3
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
H="env ORBIT_SOCK=$run/orbit.$WAYLAND_DISPLAY.sock $orbit_dir/orbit-msg"
y_de() { $H janelas | python3 -c "import json,sys; print(json.load(sys.stdin)['janelas'][0]['y'])"; }
"$g/pulse" >"$run/c.log" 2>&1 &
sleep 3
"$M" casa; sleep 0.3; "$M" mover 640 100; sleep 0.3; "$M" arrastar 0 -90 300; sleep 1
antes=$(y_de)
for a in papel barra dock; do "$g/$a" >"$run/$a.log" 2>&1 & done
sleep 4
depois=$(y_de)
pkill -x pulse; pkill -x barra; pkill -x dock; pkill -x papel; kill $orbit 2>/dev/null
if [ "$antes" -lt 40 ] && [ "$depois" -ge 40 ]; then echo "janela livre sob a barra de cima: ok"; else echo "FALHOU: y antes $antes, depois $depois (devia sair de menos de 40 e ficar em 40 ou mais)"; exit 1; fi
