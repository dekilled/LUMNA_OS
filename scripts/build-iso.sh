#!/usr/bin/env bash
# Monta o ISO em qualquer Linux com Docker. Resultado em out/lumna-os-*.iso.
# EMMA_SRC=caminho/do/NewHarnessV2 inclui o backend da Emma (pacote lumna-emma).
set -euo pipefail
root="$(realpath "$(dirname "$0")/..")"
docker build -t lumna-builder "$root/iso/builder"
# --privileged: o mkarchiso precisa montar o sistema que está montando.
# No fim, out/ volta a ser de quem chamou o script (dentro do container tudo é root).
docker run --rm --privileged --network host \
  -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  -v "$root:/src" \
  -v lumna-cargo:/cache \
  ${EMMA_SRC:+-v "$(realpath "$EMMA_SRC"):/emma:ro" -e EMMA_SRC=/emma} \
  -e CARGO_HOME=/cache/cargo -e CARGO_TARGET_DIR=/cache/target \
  ${NODE_EXTRA_CA_CERTS:+-e NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt} \
  lumna-builder bash -c 'mkdir -p /cache && chown -R builder:builder /cache && /src/iso/build.sh; status=$?
    chown -R "$HOST_UID:$HOST_GID" /src/out; exit $status'
