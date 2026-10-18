#!/bin/sh
# Instala o backend da Emma no celular e liga o serviço do usuário. Com ele rodando, o painel
# da Emma entra sozinho (conta local), igual ao desktop: só falta escolher o provedor.
# Rode NO CELULAR, como alarm (sem sudo), dentro do clone do LUMNA_OS, com internet:
#   sh mobile/session/emma.sh                  # código da Emma em ~/NewHarnessV2
#   EMMA_SRC=/outro/lugar sh mobile/session/emma.sh
#   sh mobile/session/emma.sh status
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
EMMA_SRC=${EMMA_SRC:-$HOME/NewHarnessV2}
[ "$(id -u)" != 0 ] || { echo "rode como alarm, sem sudo (o makepkg não roda como root)"; exit 1; }

if [ "${1:-}" = status ]; then
    systemctl --user --no-pager status lumna-emma || true
    journalctl --user -u lumna-emma -n 20 --no-pager || true
    curl -fsS -o /dev/null -w 'backend respondeu: HTTP %{http_code}\n' http://127.0.0.1:8001/api/ \
        || echo "backend não respondeu em 127.0.0.1:8001"
    exit 0
fi

if [ ! -f "$EMMA_SRC/backend/server.py" ]; then
    cat <<MSG
Não achei o código da Emma em $EMMA_SRC (o NewHarnessV2 é privado).
Copie do PC pela USB, rodando NO PC, de dentro da pasta que contém o NewHarnessV2:
  tar -czf - NewHarnessV2/backend | ssh alarm@172.16.42.1 'tar -C ~ -xzf -'
ou clone aqui com a sua conta do GitHub (o branch com as ações do LUMNA, lumna_tools; a main
voltou pro estável v71, que ainda não tem):
  git clone --depth 1 -b ccr-dbadfc5d-xoq8ga https://github.com/dekilled/NewHarnessV2 ~/NewHarnessV2
MSG
    exit 1
fi

sudo pacman -S --needed --noconfirm base-devel python python-pip curl

# O pip baixa wheels aarch64 prontos (numpy, cryptography, pydantic-core...); o que não tiver
# wheel compila aqui mesmo, por isso o base-devel e o rust (que o lumna-mobile.sh já instalou).
cd "$ROOT/packages/lumna-emma"
EMMA_SRC=$(cd "$EMMA_SRC" && pwd) makepkg -sfi --nocheck --noconfirm

systemctl --user daemon-reload
systemctl --user enable --now lumna-emma
systemctl --user restart lumna-emma

printf 'esperando o backend subir'
for _ in $(seq 60); do
    if curl -fsS -o /dev/null http://127.0.0.1:8001/api/ 2>/dev/null; then
        echo; echo "Emma no ar em 127.0.0.1:8001. Feche e abra o painel: ele entra sozinho."
        exit 0
    fi
    printf .; sleep 2
done
echo; echo "o backend não respondeu em 2 min. Veja: sh mobile/session/emma.sh status"
exit 1
