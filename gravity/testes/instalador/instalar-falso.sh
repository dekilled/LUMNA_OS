#!/bin/sh
# O lumna-install de mentira: grava os argumentos e a senha que recebeu e conta o andamento. FALSO_MODO: ok (padrão), nega, erro.
echo "$*" > "$FALSO_DIR/args"
IFS= read -r senha
echo "$senha" > "$FALSO_DIR/senha"
case "$FALSO_MODO" in
nega) exit 126 ;;
erro) echo "@@ 10 Preparando o disco"; echo "ERRO: as partições novas não apareceram" >&2; exit 1 ;;
esac
echo "@@ 3 Preparando o disco"
echo "mensagem solta que não é andamento"
echo "@@ 50 Copiando o sistema"
echo "@@ 100 Pronto"
