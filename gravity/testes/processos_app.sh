#!/bin/sh
# O app Processos inteiro num Orbit headless: o serviço residente (prioridade de tempo real, alternar pelo socket) e a janela (lista, busca, encerrar um processo
# ocupado pela interface, prioridade, confirmação para processo do sistema). Uso: sh testes/processos_app.sh PASTA_DO_ORBIT (com o make processos feito)
orbit_dir=${1:?}
aqui=$(cd "$(dirname "$0")" && pwd); gravity=$(dirname "$aqui")
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 LUMNA_PROCESSOS_DEBUG=1
unset XDG_CONFIG_HOME
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
# o "aplicativo ocupado" que vai ser encerrado (um nome só dele)
cp /bin/sleep "$run/ocupadoteste"
"$run/ocupadoteste" 600 & ocupado=$!
"$gravity/processos" servico > "$run/srv.out" 2> "$run/srv.err" & srv=$!
sleep 1
rc=0
python3 "$aqui/processos/teste_app.py" "$orbit_dir" "$run" "$srv" "$ocupado" "$gravity" || rc=$?
kill $ocupado $srv 2>/dev/null; pkill -x orbit; pkill -f "processos janela"
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -5 "$run/srv.err"; } || true
exit $rc
