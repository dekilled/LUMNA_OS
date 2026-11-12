/* O desenho dos ícones de arquivo e de pasta (feitos à mão, sem arquivo de imagem): o Atlas e a área de trabalho usam os mesmos. */
#ifndef LUMNA_ARICONE_H
#define LUMNA_ARICONE_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "arquivos.h"
#include "gravity.h"

typedef struct ic_dados {
	enum ar_tipo tipo;
	char selo[8];
	bool link;
	uint32_t cor; /* a cor da pasta (0: a do tema) */
	enum ar_tipo previa[4]; /* o que há dentro da pasta, em miniatura */
	int nprevia;
} ic_dados;

/* para gv_livre: dados é um ic_dados que vale até o fim do quadro */
void ar_icone_desenhar(cairo_t *cr, Clay_BoundingBox c, void *dados);

#endif
