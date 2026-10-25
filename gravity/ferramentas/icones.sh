#!/bin/sh
# Gera src/gv_icones.c com os ícones do shell do LUMNA (shell/src/components/Icon.vue): o mesmo
# desenho nos dois lados, mais os de ferramentas/icones-extra.txt. Rode de novo quando o Icon.vue ganhar ícones:
#   sh gravity/ferramentas/icones.sh
set -eu
export LC_ALL=C # a ordem do bsearch é a do strcmp
RAIZ=$(cd "$(dirname "$0")/../.." && pwd)
VUE="$RAIZ/shell/src/components/Icon.vue"
SAIDA="$RAIZ/gravity/src/gv_icones.c"

{
	echo "/* Gerado por ferramentas/icones.sh a partir de shell/src/components/Icon.vue: não edite à mão. */"
	echo '#include "gv_interno.h"'
	echo
	echo '#include <string.h>'
	echo
	echo 'struct icone {'
	echo '	const char *nome;'
	echo '	const char *caminho;'
	echo '};'
	echo
	echo 'static const struct icone icones[] = {'
	# linhas como:   volume: "M4 9v6...",   ou   "volume-off": "M4 9h4...",
	# mais os do próprio Gravity (ferramentas/icones-extra.txt), no mesmo formato
	cat "$VUE" "$RAIZ/gravity/ferramentas/icones-extra.txt" |
		sed -n 's/^[[:space:]]*"\{0,1\}\([a-z0-9-]*\)"\{0,1\}: \("[^"]*"\),\{0,1\}$/	{ "\1", \2 },/p' | sort -u -t'"' -k2,2
	echo '};'
	echo
	echo 'static int comparar(const void *chave, const void *item) {'
	echo '	return strcmp(chave, ((const struct icone *)item)->nome);'
	echo '}'
	echo
	echo '#include <stdlib.h>'
	echo
	echo 'const char *gv__icone_caminho(const char *nome) {'
	echo '	const struct icone *i = bsearch(nome, icones, sizeof(icones) / sizeof(icones[0]), sizeof(icones[0]), comparar);'
	echo '	return i ? i->caminho : NULL;'
	echo '}'
} >"$SAIDA"
echo "$(grep -c '^	{ "' "$SAIDA") ícones em $SAIDA"
