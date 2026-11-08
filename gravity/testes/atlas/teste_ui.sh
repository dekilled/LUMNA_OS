#!/bin/sh
# O explorador inteiro num Orbit headless. sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY (com o `make atlas` feito)
orbit_dir=${1:?}; gravity=${2:?}
aqui=$(cd "$(dirname "$0")" && pwd)
run=$(mktemp -d); chmod 700 "$run"
casa="$run/casa"
mkdir -p "$casa/Documentos/Projetos/a" "$casa/Documentos/Projetos/b" "$casa/Documentos/Projetos/c" "$casa/Documentos/Contratos" "$casa/Documentos/Pessoal" "$casa/Documentos/Estudos" \
	"$casa/Documentos/Modelos" "$casa/Documentos/Arquivados" "$casa/Imagens" "$casa/Downloads" "$casa/.config"
for f in planejamento.pdf notas.md orçamento.ods apresentação.odp; do echo "x" > "$casa/Documentos/$f"; done
printf 'XDG_DOCUMENTS_DIR="$HOME/Documentos"\nXDG_PICTURES_DIR="$HOME/Imagens"\nXDG_DOWNLOAD_DIR="$HOME/Downloads"\n' > "$casa/.config/user-dirs.dirs"
export XDG_RUNTIME_DIR=$run HOME=$casa WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_DOCUMENTS_DIR XDG_DOWNLOAD_DIR XDG_PICTURES_DIR
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
LUMNA_ATLAS_DEBUG=1 "$gravity/atlas" "$casa/Documentos" > "$run/ar.out" 2> "$run/ar.err" &
sleep 2
python3 "$aqui/teste_ui.py" "$orbit_dir" "$run" "$casa"
rc=$?
if [ $rc -eq 0 ]; then
	python3 "$aqui/teste_ops.py" "$orbit_dir" "$run" "$casa"
	rc=$?
fi
if [ $rc -eq 0 ]; then
	python3 "$aqui/teste_cfg.py" "$orbit_dir" "$run" "$casa"
	rc=$?
fi
if [ $rc -eq 0 ]; then
	python3 "$aqui/teste_abas.py" "$orbit_dir" "$run" "$casa"
	rc=$?
fi
pkill -x orbit; pkill -x atlas
[ $rc -eq 0 ] || { echo "(log em $run)"; tail -10 "$run/ar.err"; }
exit $rc
