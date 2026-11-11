#!/bin/sh
# Ctrl+Shift+Esc chama o gerenciador de processos pelo Orbit (o comando de [geral] processos), mesmo com uma janela em tela cheia com o foco: o atalho é do
# compositor e nenhum app o recebe antes. Num Orbit sem tela. Uso: sh testes/processos_atalho.sh PASTA_DO_ORBIT
orbit=${1:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/orbit" "$run/home"
printf '[geral]\nprocessos = touch %s/chamado\n' "$run" > "$run/cfg/orbit/orbit.ini"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
cd "$orbit" || exit 1
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman ./orbit >"$run/log" 2>&1 & pid=$!
sleep 2
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-')
ok=1
wtype -M ctrl -M shift -k Escape -m shift -m ctrl >/dev/null 2>&1
sleep 1
[ -f "$run/chamado" ] || { echo "FALHOU: Ctrl+Shift+Esc devia rodar o comando de processos" >&2; ok=0; }
rm -f "$run/chamado"
# sem Ctrl (só Shift+Esc) não chama
wtype -M shift -k Escape -m shift >/dev/null 2>&1
sleep 1
[ ! -f "$run/chamado" ] || { echo "FALHOU: Shift+Esc sozinho não devia chamar" >&2; ok=0; }
kill $pid 2>/dev/null; wait $pid 2>/dev/null; rm -rf "$run"
[ $ok = 1 ] && echo "atalho do gerenciador de processos: ok" || exit 1
