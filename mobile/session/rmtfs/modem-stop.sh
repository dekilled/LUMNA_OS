#!/bin/sh
# Desliga o processador do modem (remoteproc) antes de parar o rmtfs. Sem isso o rmtfs fica preso
# esperando o modem largar a EFS e o desligar/reiniciar trava em "A stop job is running for
# Qualcomm remotefs service" (o prazo se estende sem fim).
for r in /sys/class/remoteproc/remoteproc*; do
    [ -d "$r" ] || continue
    info="$(cat "$r/name" "$r/firmware" 2>/dev/null)"
    case "$info" in
        *modem*|*mpss*|*4080000*)
            [ "$(cat "$r/state" 2>/dev/null)" = running ] && echo stop > "$r/state" 2>/dev/null
            ;;
    esac
done
exit 0
