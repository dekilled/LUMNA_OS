#!/bin/sh
echo "$@" > "${LUMNA_FF_ARGS:-/dev/null}"
exec python3 "$(dirname "$0")/bidi_falso.py"
