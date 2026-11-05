#!/bin/sh
# O tocador de mentira (LUMNA_TOCADOR): registra o que tocou e demora um pouco, como um tocador de verdade; sem o arquivo, sai com erro 1.
[ -f "$1" ] || { echo "tocador: sem o arquivo $1" >> "$EMMA_TESTE_LOG"; exit 1; }
echo "tocou:$(tail -c +13 "$1")" >> "$EMMA_TESTE_LOG"
sleep 0.25
exit 0
