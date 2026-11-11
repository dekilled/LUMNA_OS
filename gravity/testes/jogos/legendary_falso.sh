#!/bin/sh
# Um legendary de mentira: auth --code / --delete, list --json, install APP --base-path DIR -y, launch APP, uninstall APP -y.
# Guarda o login e os jogos instalados em $LEGENDARY_CONFIG_PATH, como o de verdade. Anota os argumentos em $FALSO_LOG.
echo "legendary $*" >> "$FALSO_LOG"
cfg=${LEGENDARY_CONFIG_PATH:?}
mkdir -p "$cfg"
cmd=$1; shift
case "$cmd" in
auth)
	if [ "$1" = "--delete" ]; then rm -f "$cfg/user.json"; exit 0; fi
	[ "$1" = "--code" ] || exit 2
	[ "$2" = "ruimruimruimruimruimruim" ] && { echo "invalid code" >&2; exit 1; }
	echo '{"displayName":"Eric"}' > "$cfg/user.json"
	;;
list)
	[ -f "$cfg/user.json" ] || { echo "not logged in" >&2; exit 1; }
	cat <<'JSON'
[{"app_name":"Sugar","app_title":"Rocket League","metadata":{"title":"Rocket League","description":"Futebol com carros.","developer":"Psyonix","keyImages":[{"type":"DieselGameBox","url":"https://cdn1.epicgames.com/offer/sugar/wide_1.jpg"},{"type":"DieselGameBoxTall","url":"https://cdn1.epicgames.com/offer/sugar/tall_1.jpg"}]}},
 {"app_name":"Fortnite","app_title":"Fortnite","metadata":{"title":"Fortnite","description":"Battle royale.","developer":"Epic Games","keyImages":[]}}]
JSON
	;;
install)
	app=$1; shift
	base=""; while [ $# -gt 0 ]; do case "$1" in --base-path) base=$2; shift 2 ;; *) shift ;; esac; done
	[ "$app" = "Falha" ] && { echo "[cli] ERROR: falhou de mentira" >&2; exit 1; }
	for p in 10 40 80 100; do echo "[DLManager] INFO: = Progress: $p.00% (1/4), Running for 00:00:0$((p/50)), ETA: 00:00:01"; sleep ${LEGENDARY_ESPERA:-0}; done
	dest="$base/$app"
	mkdir -p "$dest"
	printf '#!/bin/sh\necho "epic jogo rodou" >> "%s"\n' "$FALSO_LOG" > "$dest/jogo.sh"; chmod +x "$dest/jogo.sh"
	python3 - "$cfg" "$app" "$dest" <<'PY'
import json, os, sys
cfg, app, dest = sys.argv[1:4]
p = os.path.join(cfg, "installed.json")
d = json.load(open(p)) if os.path.exists(p) else {}
d[app] = {"app_name": app, "app_title": {"Sugar": "Rocket League", "Fortnite": "Fortnite"}.get(app, app), "install_path": dest, "version": "1.0", "executable": "jogo.sh", "launch_parameters": ""}
json.dump(d, open(p, "w"))
PY
	;;
launch)
	app=$1
	python3 - "$cfg" "$app" <<'PY'
import json, os, subprocess, sys
cfg, app = sys.argv[1:3]
d = json.load(open(os.path.join(cfg, "installed.json")))
g = d[app]; subprocess.run([os.path.join(g["install_path"], g["executable"])])
PY
	;;
uninstall)
	app=$1
	python3 - "$cfg" "$app" <<'PY'
import json, os, shutil, sys
cfg, app = sys.argv[1:3]
p = os.path.join(cfg, "installed.json")
d = json.load(open(p)); g = d.pop(app, None)
json.dump(d, open(p, "w"))
if g: shutil.rmtree(g["install_path"], ignore_errors=True)
PY
	;;
*) exit 2 ;;
esac
