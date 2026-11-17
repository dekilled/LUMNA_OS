# Ambiente comum dos testes de encaixe: um Orbit headless com 2 monitores (1280x720 lado a lado), um cliente GTK3 com menu de clique direito.
#   . comum.sh PASTA_DO_ORBIT PASTA_DOS_BINARIOS(menu_gtk)
B=${1:?pasta do orbit}; T=${2:?pasta com o menu_gtk}
run=$(mktemp -d); chmod 700 $run
export XDG_RUNTIME_DIR=$run HOME=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=${SAIDAS:-2} GDK_BACKEND=wayland
mkdir -p $run/cfg; export XDG_CONFIG_HOME=$run/cfg
(cd $B && ./orbit > $run/orbit.log 2>&1 &)
sleep 2.5
export WAYLAND_DISPLAY=wayland-0
M="$B/orbit-msg"
ORBIT_SOCK=$(ls $run/orbit.*.sock 2>/dev/null | head -1); export ORBIT_SOCK
fim() { pkill -x orbit; pkill -x menu_gtk; rm -rf $run; }
janelas() { $M janelas; }
campo() { python3 -c "import json,sys; j=json.load(sys.stdin)['janelas']; print(' '.join(str(j[int(sys.argv[1])].get(k)) for k in sys.argv[2:]))" "$@"; }
