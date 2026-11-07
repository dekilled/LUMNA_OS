#!/bin/bash
# scripts/publica-repo.sh: a poda das versões velhas (fica a nova de cada pacote; os outros pacotes ficam) e a conferência que
# reprova um pacote incompleto. sh limpar.sh   (de qualquer pasta)
AQUI=$(cd "$(dirname "$0")" && pwd)
RAIZ=$AQUI/../../..
t=$(mktemp -d); d=$t/repo; mkdir -p $d
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
faz_pacote() { # nome versao
  local p=$t/p-$1-$2; mkdir -p $p
  printf 'pkgname = %s\npkgver = %s\n' "$1" "$2" > $p/.PKGINFO
  (cd $p && bsdtar --zstd -cf $d/$1-$2-1-x86_64.pkg.tar.zst .PKGINFO)
}
faz_pacote lumna-gravity 0.1.0.202611060000
faz_pacote lumna-gravity 0.1.0.202611070000
faz_pacote lumna-orbit 0.1.0.202611060000
faz_pacote lumna-shell 0.1.0.202610150000          # não é recompilado: tem de ficar
faz_pacote lumna-gravity-extra 0.1.0.202611060000  # nome que começa igual: não pode ser confundido
source "$RAIZ/scripts/publica-repo.sh"
limpar_antigos "$d" lumna-gravity-0.1.0.202611070000-1-x86_64.pkg.tar.zst lumna-orbit-0.1.0.202611060000-1-x86_64.pkg.tar.zst
ls "$d" | sort > $t/ficou
esperado="lumna-gravity-0.1.0.202611070000-1-x86_64.pkg.tar.zst
lumna-gravity-extra-0.1.0.202611060000-1-x86_64.pkg.tar.zst
lumna-orbit-0.1.0.202611060000-1-x86_64.pkg.tar.zst
lumna-shell-0.1.0.202610150000-1-x86_64.pkg.tar.zst"
[ "$(cat $t/ficou)" = "$esperado" ] || fail "devia ficar só a versão nova de cada pacote e os não recompilados: $(cat $t/ficou)"
# a conferência reprova pacotes incompletos (estes só têm o .PKGINFO)
"$RAIZ/scripts/confere-pacotes.sh" "$d" > $t/saida 2>&1 && fail "a conferência devia reprovar pacotes sem os arquivos da sessão"
grep -q "FALTA" $t/saida || fail "a conferência devia dizer o que falta: $(cat $t/saida)"
[ "$falhas" -eq 0 ] && echo "publica-repo (poda e conferência): ok" || { echo "$falhas falha(s)"; exit 1; }
