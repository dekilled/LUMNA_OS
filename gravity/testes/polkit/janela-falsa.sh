#!/bin/sh
# A janela de senha de mentira: "digita" a próxima senha de $JANELA_DIR/senhas (sem mais senhas, cancela).
echo "$*" >> "$JANELA_DIR/janela.log"
if [ -f "$JANELA_DIR/demora" ]; then exec sleep 30; fi
s=$(head -n1 "$JANELA_DIR/senhas" 2>/dev/null)
[ -n "$s" ] || exit 1
sed -i 1d "$JANELA_DIR/senhas"
echo "$s"
