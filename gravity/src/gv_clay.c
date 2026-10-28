/* A implementação do Clay (vendor/clay, zlib), compilada uma vez para o Gravity inteiro. */
#define CLAY_IMPLEMENTATION
#include "clay.h"

/* O cache de medidas de texto do Clay enche (uma resposta que chega aos pedaços mede o texto inteiro a cada pedaço) e, cheio, mede todo
 * texto novo com tamanho zero: balões sem fundo, linhas soltas e uma por cima da outra. Esvazia-o e deixa o aviso voltar a valer. */
void gv__clay_limpar_medidas(void) {
	Clay_ResetMeasureTextCache();
	Clay_GetCurrentContext()->booleanWarnings.maxTextMeasureCacheExceeded = false;
}
