#!/bin/sh
# Um gogdl de mentira: auth --code, download ID --platform P --path DIR. Anota os argumentos em $FALSO_LOG.
echo "gogdl $*" >> "$FALSO_LOG"
auth_path=""; while [ $# -gt 0 ]; do case "$1" in --auth-config-path) auth_path=$2; shift 2 ;; *) break ;; esac; done
cmd=$1; shift
case "$cmd" in
auth)
	[ "$1" = "--code" ] || exit 2
	[ "$2" = "ruim12345678" ] && { echo "invalid code" >&2; exit 1; }
	printf '{"46899977096215655": {"access_token": "TOKEN-FALSO", "refresh_token": "r", "user_id": "1"}}' > "$auth_path"
	;;
download)
	id=$1; shift
	plat=""; dest=""
	while [ $# -gt 0 ]; do case "$1" in --platform) plat=$2; shift 2 ;; --path) dest=$2; shift 2 ;; *) shift ;; esac; done
	[ "$id" = "999" ] && { echo "falhou de mentira" >&2; exit 1; }
	for p in 10 35 70 100; do echo "Progress: $p.0 de mentira"; sleep ${GOGDL_ESPERA:-0}; done
	mkdir -p "$dest"
	printf '#!/bin/sh\necho "gog jogo rodou" >> "%s"\n' "$FALSO_LOG" > "$dest/start.sh"
	chmod +x "$dest/start.sh"
	printf '{"gameId":"%s","playTasks":[{"path":"game.exe","isPrimary":true}]}' "$id" > "$dest/goggame-$id.info"
	;;
*) exit 2 ;;
esac
