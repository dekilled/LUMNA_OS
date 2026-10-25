/*
 * Texto: o pango mede e desenha (acentos, emoji, ligaduras, a fonte do tema). O Clay só sabe
 * de tamanhos; o peso vai no fontId (gv_peso).
 */
#include "gv_interno.h"

#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- memória do quadro ---- */

#define GV_QUADRO_BLOCO (256 * 1024)

struct bloco {
	struct bloco *seguinte;
	size_t usado, capacidade;
	char dados[];
};

static struct bloco *blocos;

void *gv__quadro_alloc(size_t tamanho) {
	tamanho = (tamanho + 15) & ~(size_t)15;
	if (!blocos || blocos->usado + tamanho > blocos->capacidade) {
		size_t capacidade = tamanho > GV_QUADRO_BLOCO ? tamanho : GV_QUADRO_BLOCO;
		struct bloco *novo = malloc(sizeof(*novo) + capacidade);
		if (!novo) {
			return NULL;
		}
		novo->seguinte = blocos;
		novo->usado = 0;
		novo->capacidade = capacidade;
		blocos = novo;
	}
	void *p = blocos->dados + blocos->usado;
	blocos->usado += tamanho;
	return p;
}

void gv__quadro_limpar(void) {
	/* fica o bloco mais recente (o tamanho de um quadro normal), os outros saem */
	if (!blocos) {
		return;
	}
	struct bloco *b = blocos->seguinte;
	while (b) {
		struct bloco *s = b->seguinte;
		free(b);
		b = s;
	}
	blocos->seguinte = NULL;
	blocos->usado = 0;
}

Clay_String gv_str(const char *texto) {
	size_t n = texto ? strlen(texto) : 0;
	char *copia = gv__quadro_alloc(n + 1);
	if (!copia) {
		return (Clay_String){ 0 };
	}
	memcpy(copia, texto ? texto : "", n + 1);
	return (Clay_String){ .length = (int32_t)n, .chars = copia };
}

Clay_String gv_fmt(const char *formato, ...) {
	va_list ap;
	va_start(ap, formato);
	int n = vsnprintf(NULL, 0, formato, ap);
	va_end(ap);
	if (n < 0) {
		return (Clay_String){ 0 };
	}
	char *texto = gv__quadro_alloc(n + 1);
	if (!texto) {
		return (Clay_String){ 0 };
	}
	va_start(ap, formato);
	vsnprintf(texto, n + 1, formato, ap);
	va_end(ap);
	return (Clay_String){ .length = n, .chars = texto };
}

/* ---- pango ---- */

static PangoContext *contexto;
static PangoLayout *medidor;

void gv__texto_iniciar(void) {
	if (contexto) {
		return;
	}
	PangoFontMap *mapa = pango_cairo_font_map_get_default();
	contexto = pango_font_map_create_context(mapa);
	/* medidas em pixels lógicos, sem arredondar ao pixel (o desenho é que escala) */
	cairo_font_options_t *opcoes = cairo_font_options_create();
	cairo_font_options_set_hint_metrics(opcoes, CAIRO_HINT_METRICS_OFF);
	pango_cairo_context_set_font_options(contexto, opcoes);
	cairo_font_options_destroy(opcoes);
	medidor = pango_layout_new(contexto);
}

static const PangoWeight pesos[] = {
	[GV_PESO_NORMAL] = PANGO_WEIGHT_NORMAL,
	[GV_PESO_MEDIO] = PANGO_WEIGHT_MEDIUM,
	[GV_PESO_SEMINEGRITO] = PANGO_WEIGHT_SEMIBOLD,
	[GV_PESO_NEGRITO] = PANGO_WEIGHT_BOLD,
};

static void preparar(PangoLayout *layout, const char *texto, int n, int peso, float tamanho) {
	PangoFontDescription *fonte = pango_font_description_from_string(gv_t.fonte);
	pango_font_description_set_weight(fonte, pesos[peso <= GV_PESO_NEGRITO ? peso : 0]);
	pango_font_description_set_absolute_size(fonte, tamanho * PANGO_SCALE);
	pango_layout_set_font_description(layout, fonte);
	pango_font_description_free(fonte);
	pango_layout_set_text(layout, texto, n);
	pango_layout_set_width(layout, -1);
}

/* como preparar, quebrando as linhas na largura dada (0: sem quebra) */
static void preparar_largura(PangoLayout *layout, const char *texto, float tamanho, float largura) {
	preparar(layout, texto, -1, GV_PESO_NORMAL, tamanho);
	if (largura > 0) {
		pango_layout_set_width(layout, (int)(largura * PANGO_SCALE));
		pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	}
}

/* Onde fica o cursor antes do byte "indice", num texto quebrado na largura dada. */
void gv__texto_pos(const char *texto, float tamanho, float largura, int indice, float *x, float *y, float *altura) {
	preparar_largura(medidor, texto, tamanho, largura);
	PangoRectangle forte;
	pango_layout_get_cursor_pos(medidor, indice, &forte, NULL);
	*x = forte.x / (float)PANGO_SCALE;
	*y = forte.y / (float)PANGO_SCALE;
	*altura = forte.height / (float)PANGO_SCALE;
}

/* O byte mais perto de (x, y), num texto quebrado na largura dada. */
int gv__texto_indice_xy(const char *texto, float tamanho, float largura, float x, float y) {
	preparar_largura(medidor, texto, tamanho, largura);
	int indice = 0, depois = 0;
	pango_layout_xy_to_index(medidor, (int)(x * PANGO_SCALE), (int)(y * PANGO_SCALE), &indice, &depois);
	/* depois de um caractere (ou no fim de uma linha quebrada, antes do primeiro da seguinte) */
	if (depois > 0 && texto[indice] && texto[indice] != '\n') {
		indice = (int)(g_utf8_next_char(texto + indice) - texto);
	}
	return indice;
}

/* A altura do texto quebrado na largura dada (uma linha vazia conta). */
float gv__texto_altura(const char *texto, float tamanho, float largura) {
	preparar_largura(medidor, texto[0] ? texto : " ", tamanho, largura);
	PangoRectangle logico;
	pango_layout_get_extents(medidor, NULL, &logico);
	return logico.height / (float)PANGO_SCALE;
}

/* Onde começa e acaba a linha (visual) com o byte "indice". */
void gv__texto_linha_de(const char *texto, float tamanho, float largura, int indice, int *ini, int *fim) {
	preparar_largura(medidor, texto, tamanho, largura);
	int linha = 0, x = 0;
	pango_layout_index_to_line_x(medidor, indice, false, &linha, &x);
	PangoLayoutLine *l = pango_layout_get_line_readonly(medidor, linha);
	*ini = l ? l->start_index : 0;
	*fim = l ? l->start_index + l->length : (int)strlen(texto);
	/* o fim de uma linha quebrada é antes do espaço onde quebrou */
	int n = (int)strlen(texto);
	if (*fim < n && *fim > *ini && texto[*fim - 1] == ' ') {
		(*fim)--;
	}
}

/* Onde fica (em x) o caractere que começa no byte "indice" de um texto de uma linha. */
float gv__texto_x(const char *texto, float tamanho, int indice) {
	preparar(medidor, texto, -1, GV_PESO_NORMAL, tamanho);
	PangoRectangle pos;
	pango_layout_index_to_pos(medidor, indice, &pos);
	return pos.x / (float)PANGO_SCALE;
}

/* O byte mais perto do x dado (o cursor vai para antes ou depois do caractere tocado). */
int gv__texto_indice(const char *texto, float tamanho, float x) {
	preparar(medidor, texto, -1, GV_PESO_NORMAL, tamanho);
	int indice = 0, depois = 0;
	pango_layout_xy_to_index(medidor, (int)(x * PANGO_SCALE), 0, &indice, &depois);
	while (depois-- > 0 && texto[indice]) {
		indice = (int)(g_utf8_next_char(texto + indice) - texto);
	}
	return indice;
}

Clay_Dimensions gv__texto_medir(Clay_StringSlice texto, Clay_TextElementConfig *config, void *dados) {
	(void)dados;
	preparar(medidor, texto.chars, texto.length, config->fontId, config->fontSize);
	PangoRectangle logico;
	pango_layout_get_extents(medidor, NULL, &logico);
	float altura = config->lineHeight ? config->lineHeight : logico.height / (float)PANGO_SCALE;
	return (Clay_Dimensions){ logico.width / (float)PANGO_SCALE, altura };
}

void gv__texto_desenhar(cairo_t *cr, Clay_RenderCommand *cmd) {
	Clay_TextRenderData *t = &cmd->renderData.text;
	PangoLayout *layout = pango_cairo_create_layout(cr);
	cairo_font_options_t *opcoes = cairo_font_options_create();
	cairo_font_options_set_hint_metrics(opcoes, CAIRO_HINT_METRICS_OFF);
	cairo_font_options_set_antialias(opcoes, CAIRO_ANTIALIAS_GRAY);
	pango_cairo_context_set_font_options(pango_layout_get_context(layout), opcoes);
	cairo_font_options_destroy(opcoes);
	pango_layout_context_changed(layout);
	preparar(layout, t->stringContents.chars, t->stringContents.length, t->fontId, t->fontSize);
	PangoRectangle logico;
	pango_layout_get_extents(layout, NULL, &logico);
	float altura = logico.height / (float)PANGO_SCALE;
	cairo_set_source_rgba(cr, t->textColor.r / 255.0, t->textColor.g / 255.0, t->textColor.b / 255.0,
		t->textColor.a / 255.0);
	/* centrado na altura da linha, se ela for maior que a do texto */
	cairo_move_to(cr, cmd->boundingBox.x, cmd->boundingBox.y + (cmd->boundingBox.height - altura) / 2);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);
}

static void fonte_cor(cairo_t *cr, Clay_Color c) {
	cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
}

/* O campo de texto: seleção por trás, o texto (a palavra a compor sublinhada), o cursor. */
void gv__campo_desenhar(cairo_t *cr, Clay_BoundingBox b, const struct gv_extra *e) {
	cairo_save(cr);
	/* uma linha: folga para os acentos; várias: corta rente (a de cima não espreita ao rolar) */
	bool varias = e->campo.largura > 0;
	cairo_rectangle(cr, b.x - 2, b.y - (varias ? 0 : 2), b.width + 4, b.height + (varias ? 0 : 4));
	cairo_clip(cr);
	PangoLayout *layout = pango_cairo_create_layout(cr);
	cairo_font_options_t *opcoes = cairo_font_options_create();
	cairo_font_options_set_hint_metrics(opcoes, CAIRO_HINT_METRICS_OFF);
	pango_cairo_context_set_font_options(pango_layout_get_context(layout), opcoes);
	cairo_font_options_destroy(opcoes);
	pango_layout_context_changed(layout);
	bool vazio = !e->campo.mostrado[0];
	const char *texto = vazio && e->campo.dica ? e->campo.dica : e->campo.mostrado;
	bool linhas = e->campo.largura > 0;
	preparar_largura(layout, texto, e->campo.tamanho, e->campo.largura);
	if (!vazio && e->campo.comp_fim > e->campo.comp_ini) {
		PangoAttrList *attrs = pango_attr_list_new();
		PangoAttribute *sub = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);
		sub->start_index = e->campo.comp_ini;
		sub->end_index = e->campo.comp_fim;
		pango_attr_list_insert(attrs, sub);
		pango_layout_set_attributes(layout, attrs);
		pango_attr_list_unref(attrs);
	}
	PangoRectangle logico;
	pango_layout_get_extents(layout, NULL, &logico);
	double altura = logico.height / (double)PANGO_SCALE;
	double x0 = b.x - (vazio ? 0 : e->campo.rolagem);
	double y0 = linhas ? b.y - e->campo.rolagem_y : b.y + (b.height - altura) / 2;
	if (linhas && !vazio && e->campo.sel_fim > e->campo.sel_ini) {
		/* a seleção linha a linha: o pedaço de cada linha que cai dentro dela */
		fonte_cor(cr, e->campo.cor_sel);
		PangoLayoutIter *it = pango_layout_get_iter(layout);
		do {
			PangoLayoutLine *l = pango_layout_iter_get_line_readonly(it);
			int li = l->start_index, lf = l->start_index + l->length;
			int a = e->campo.sel_ini > li ? e->campo.sel_ini : li;
			int z = e->campo.sel_fim < lf ? e->campo.sel_fim : lf;
			bool continua = e->campo.sel_fim > lf; /* a seleção passa para a linha seguinte */
			if (a > z || (a == z && !continua)) {
				continue;
			}
			int y_cima, y_baixo;
			pango_layout_iter_get_line_yrange(it, &y_cima, &y_baixo);
			int *faixas = NULL, nfaixas = 0;
			pango_layout_line_get_x_ranges((PangoLayoutLine *)l, a, z, &faixas, &nfaixas);
			for (int k = 0; k < nfaixas; k++) {
				double fx0 = faixas[2 * k] / (double)PANGO_SCALE, fx1 = faixas[2 * k + 1] / (double)PANGO_SCALE;
				if (continua && k == nfaixas - 1) {
					fx1 += e->campo.tamanho * 0.3; /* a quebra de linha selecionada também se vê */
				}
				cairo_rectangle(cr, x0 + fx0, y0 + y_cima / (double)PANGO_SCALE, fx1 - fx0, (y_baixo - y_cima) / (double)PANGO_SCALE);
			}
			if (!nfaixas && continua) {
				cairo_rectangle(cr, x0, y0 + y_cima / (double)PANGO_SCALE, e->campo.tamanho * 0.3, (y_baixo - y_cima) / (double)PANGO_SCALE);
			}
			g_free(faixas);
			cairo_fill(cr);
		} while (pango_layout_iter_next_line(it));
		pango_layout_iter_free(it);
	} else if (!vazio && e->campo.sel_fim > e->campo.sel_ini) {
		PangoRectangle a, z;
		pango_layout_index_to_pos(layout, e->campo.sel_ini, &a);
		pango_layout_index_to_pos(layout, e->campo.sel_fim, &z);
		fonte_cor(cr, e->campo.cor_sel);
		cairo_rectangle(cr, x0 + a.x / (double)PANGO_SCALE, y0, (z.x - a.x) / (double)PANGO_SCALE, altura);
		cairo_fill(cr);
	}
	if (texto[0]) {
		fonte_cor(cr, vazio ? e->campo.cor_dica : e->campo.cor);
		cairo_move_to(cr, x0, y0);
		pango_cairo_show_layout(cr, layout);
	}
	if (e->campo.cursor_alfa > 0) {
		PangoRectangle c;
		pango_layout_get_cursor_pos(layout, vazio ? 0 : e->campo.cursor, &c, NULL);
		Clay_Color cor = e->campo.cor_cursor;
		cor.a *= e->campo.cursor_alfa;
		fonte_cor(cr, cor);
		double cy = linhas ? c.y / (double)PANGO_SCALE : 0, ch = linhas ? c.height / (double)PANGO_SCALE : altura;
		cairo_rectangle(cr, x0 + (vazio ? 0 : c.x / (double)PANGO_SCALE) - 1, y0 + cy + 1, 2, ch - 2);
		cairo_fill(cr);
	}
	g_object_unref(layout);
	cairo_restore(cr);
}

void gv_texto(Clay_String texto, float tamanho, Clay_Color cor, enum gv_peso peso) {
	if (!texto.isStaticallyAllocated && texto.chars) {
		/* um texto do chamador pode não viver até ao desenho: copia-o para o quadro */
		char *copia = gv__quadro_alloc(texto.length + 1);
		if (copia) {
			memcpy(copia, texto.chars, texto.length);
			copia[texto.length] = '\0';
			texto.chars = copia;
		}
	}
	CLAY_TEXT(texto, {
		.textColor = cor,
		.fontSize = (uint16_t)(tamanho > 0 ? tamanho : gv_t.tamanho_corpo),
		.fontId = (uint16_t)peso,
		.wrapMode = CLAY_TEXT_WRAP_WORDS,
	});
}
