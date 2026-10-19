#!/bin/sh
# A hora do celular sem internet. O relógio de hardware (RTC) do Snapdragon não deixa ser
# acertado: ele conta segundos desde um ponto qualquer (por isso o celular ligava em 1970).
# Como no Android, guardamos a diferença entre a hora certa e o RTC; no boot, hora = RTC + diferença.
#   relogio.sh restaura   (no boot, antes de tudo)
#   relogio.sh guarda     (de tempos em tempos, quando a hora já está certa, e ao desligar)
set -eu
ARQ=${LUMNA_RELOGIO:-/var/lib/lumna/relogio}
RTC=${LUMNA_RTC:-/sys/class/rtc/rtc0/since_epoch}
# antes disso a hora certamente está errada (não guarda lixo)
MINIMO=1767225600 # 2026-01-01

rtc() { cat "$RTC" 2>/dev/null || echo 0; }

case "${1:-}" in
restaura)
    [ -f "$ARQ" ] || exit 0
    read -r diff salvo <"$ARQ" || exit 0
    agora=$(date +%s)
    r=$(rtc)
    alvo=$salvo
    # o RTC andou desde que guardou (inclusive desligado): soma
    if [ "$r" -gt 0 ] && [ $((r + diff)) -ge "$salvo" ]; then alvo=$((r + diff)); fi
    # só adianta (a hora certa nunca volta pra trás de uma guardada)
    if [ "$alvo" -gt "$agora" ]; then
        date -s "@$alvo" >/dev/null
        echo "relogio: hora restaurada para $(date)"
    fi
    ;;
guarda)
    agora=$(date +%s)
    [ "$agora" -ge "$MINIMO" ] || exit 0
    r=$(rtc)
    mkdir -p "${ARQ%/*}"
    # um temporário por execução: o serviço e o lumna-mobile.sh podem guardar ao mesmo tempo
    tmp="$ARQ.$$"
    printf '%s %s\n' "$((agora - r))" "$agora" >"$tmp" && mv -f "$tmp" "$ARQ"
    ;;
*)
    echo "uso: $0 restaura|guarda" >&2
    exit 2
    ;;
esac
