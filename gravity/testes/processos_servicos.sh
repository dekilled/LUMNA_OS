#!/bin/sh
# A tela Serviços do gerenciador (com um systemctl de mentira) e o atalho "Aplicativos de inicialização" (abre o Painel na seção certa).
# Uso: sh testes/processos_servicos.sh PASTA_DO_ORBIT (com o make processos feito)
orbit_dir=${1:?}
aqui=$(cd "$(dirname "$0")" && pwd); gravity=$(dirname "$aqui")
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/bin"
cp "$aqui/processos/bin/systemctl" "$run/bin/systemctl"
# um lumna-painel de mentira: só anota os argumentos
printf '#!/bin/sh\necho "$@" > "%s/painel.args"\n' "$run" > "$run/bin/lumna-painel"; chmod +x "$run/bin/lumna-painel"
export PATH="$run/bin:$PATH" XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 LUMNA_PROCESSOS_DEBUG=1
unset XDG_CONFIG_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
rc=0
python3 "$aqui/processos/teste_servicos.py" "$orbit_dir" "$run" "$gravity" || rc=$?
pkill -x orbit; pkill -f "processos janela"
[ $rc -eq 0 ] || echo "(log em $run)"
exit $rc
