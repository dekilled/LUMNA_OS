#!/bin/sh
# A versão de um pacote do celular, pelo git: 0.1.r<commits que mexeram nele>.g<o último deles>.
# Um pacote que não mudou mantém a versão (a Loja não oferece "atualização" à toa) e cada
# mudança sobe o número. Rode na raiz do LUMNA_OS:
#   sh mobile/scripts/versao.sh lumna-shell|lumna-mobile
#   sh mobile/scripts/versao.sh lumna-emma CAMINHO_DO_NEWHARNESSV2
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

# conta REPO CAMINHO... / ultimo REPO CAMINHO...
conta() { r=$1; shift; git -C "$r" rev-list --count HEAD -- "$@"; }
ultimo() { r=$1; shift; git -C "$r" log -1 --format=%h -- "$@"; }

case "${1:-}" in
lumna-shell)
    set -- "$ROOT" shell crates Cargo.toml Cargo.lock packages/lumna-shell
    echo "0.1.r$(conta "$@").g$(ultimo "$@")"
    ;;
lumna-mobile)
    set -- "$ROOT" mobile/session session/lumna-pkg session/os.lumna.pkg.policy packages/lumna-mobile
    echo "0.1.r$(conta "$@").g$(ultimo "$@")"
    ;;
lumna-emma)
    emma=${2:?"falta o caminho do NewHarnessV2"}
    a=$(conta "$emma" backend)
    b=$(conta "$ROOT" packages/lumna-emma)
    echo "0.1.r$((a + b)).g$(ultimo "$emma" backend)"
    ;;
*)
    echo "uso: $0 lumna-shell|lumna-mobile|lumna-emma [NewHarnessV2]" >&2
    exit 2
    ;;
esac
