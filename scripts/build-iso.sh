#!/usr/bin/env bash
# Monta o ISO em qualquer Linux com Docker. Resultado em out/lumna-os-*.iso.
set -euo pipefail
root="$(realpath "$(dirname "$0")/..")"
docker build -t lumna-builder "$root/iso/builder"
# --privileged: o mkarchiso precisa montar o sistema que está montando.
docker run --rm --privileged --network host \
  -v "$root:/src" \
  -v lumna-cargo:/cache \
  -e CARGO_HOME=/cache/cargo -e CARGO_TARGET_DIR=/cache/target \
  ${NODE_EXTRA_CA_CERTS:+-e NODE_EXTRA_CA_CERTS=/etc/ssl/certs/ca-certificates.crt} \
  lumna-builder bash -c 'mkdir -p /cache && chown -R builder:builder /cache && /src/iso/build.sh'
