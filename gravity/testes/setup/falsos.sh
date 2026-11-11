#!/bin/sh
# falsos.sh PASTA: timedatectl, rclone, pkill, localectl e lumna-emma de mentira (cada um só anota o que recebeu em $FALSO_LOG)
d=${1:?}/bin; mkdir -p "$d"
for p in timedatectl rclone pkill localectl; do
	printf '#!/bin/sh\necho "%s $*" >> "$FALSO_LOG"\nexit 0\n' "$p" > "$d/$p"
done
printf '#!/bin/sh\necho "lumna-emma subiu" >> "$FALSO_LOG"\nexit 0\n' > "$d/lumna-emma"
chmod +x "$d"/*
