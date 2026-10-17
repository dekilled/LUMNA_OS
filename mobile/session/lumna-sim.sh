#!/bin/sh
# Ativa o chip no modem do OnePlus 6T. O modem lê o SIM (aplicativo USIM "detected"), mas no mainline
# ninguém abre a sessão de provisionamento ("Primary GW: session doesn't exist"), e o ModemManager
# fica em "sim-missing". No Android quem faz isso é um serviço da Qualcomm; aqui é este script,
# rodado pelo lumna-sim.service antes do ModemManager.
set -u
dev=qrtr://0

# o modem demora alguns segundos depois do boot para responder
for i in $(seq 30); do
    status=$(qmicli -d $dev --uim-get-card-status 2>/dev/null) && break
    sleep 2
done
[ -n "${status:-}" ] || { echo "lumna-sim: o modem não respondeu"; exit 0; }

if ! echo "$status" | grep -q "Primary GW: *session doesn't exist"; then
    echo "lumna-sim: o chip já está ativo"
    exit 0
fi

# primeiro aplicativo USIM de um cartão presente: "slot AID"
alvo=$(echo "$status" | awk '
    /^Slot \[[0-9]+\]:/ { slot = $2; gsub(/[^0-9]/, "", slot); present = 0 }
    /Card state: .present./ { present = 1 }
    /Application type: *.usim/ { usim = present }
    usim && /Application ID:/ { getline; gsub(/[ \t]/, ""); print slot, $0; exit }')
[ -n "$alvo" ] || { echo "lumna-sim: nenhum chip com USIM"; exit 0; }
set -- $alvo

echo "lumna-sim: ativando o chip do slot $1"
qmicli -d $dev --uim-change-provisioning-session="slot=$1,activate=yes,session-type=primary-gw-provisioning,aid=$2"
