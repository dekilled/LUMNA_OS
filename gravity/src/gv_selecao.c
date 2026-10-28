/*
 * Seleção de texto com o mouse (gv_texto_sel, gv_texto_rico_sel): arrastar marca, clique duplo marca a palavra, triplo o parágrafo,
 * Ctrl+C copia. Cada texto selecionável se registra a cada quadro, na ordem em que aparece; a seleção é um ponto de partida e um de
 * chegada (bloco + byte) e vale entre eles, atravessando blocos. Os blocos são medidos pela caixa que o Clay deu no layout do quadro.
 */
#include <math.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <glib.h>

#include "gv_interno.h"

struct bloco {
	uint32_t id;
	char *markup, *plano;
	float tamanho;
	bool mono;
};

static GArray *blocos; /* struct bloco, deste quadro */

static struct {
	bool ativa, arrastando;
	uint32_t a_id, c_id; /* de onde partiu e onde está agora */
	int a_idx, c_idx;
	int cliques;
	double ultimo;
	float ux, uy;
} sel;

static void bloco_limpar(gpointer p) {
	struct bloco *b = p;
	g_free(b->markup);
	g_free(b->plano);
}

void gv__selecao_quadro_inicio(void) {
	if (!blocos) {
		blocos = g_array_new(FALSE, TRUE, sizeof(struct bloco));
		g_array_set_clear_func(blocos, bloco_limpar);
	}
	g_array_set_size(blocos, 0);
}

void gv__selecao_registrar(uint32_t id, const char *markup, float tamanho, bool mono) {
	if (!blocos) {
		return;
	}
	struct bloco b = { .id = id, .markup = g_strdup(markup), .plano = gv__rico_plano(markup), .tamanho = tamanho, .mono = mono };
	g_array_append_val(blocos, b);
}

static int posicao_de(uint32_t id) {
	for (guint i = 0; blocos && i < blocos->len; i++) {
		if (g_array_index(blocos, struct bloco, i).id == id) {
			return (int)i;
		}
	}
	return -1;
}

static bool caixa_de(const struct bloco *b, Clay_BoundingBox *c) {
	Clay_ElementData d = Clay_GetElementData((Clay_ElementId){ .id = b->id });
	if (!d.found) {
		return false;
	}
	*c = d.boundingBox;
	return c->width > 0;
}

/* o byte do bloco sob o ponto (que pode estar fora dele: o pango fixa nas bordas) */
static int indice_em(const struct bloco *b, Clay_BoundingBox c, float x, float y) {
	int i = gv__rico_indice_xy(b->markup, b->tamanho, b->mono, c.width, x - c.x, y - c.y);
	int n = (int)strlen(b->plano);
	return i < 0 ? 0 : i > n ? n : i;
}

/* o bloco sob o ponto; senão o mais perto na vertical (arrastar para fora do texto continua a seleção) */
static int bloco_perto(float x, float y, bool so_dentro, Clay_BoundingBox *caixa) {
	int melhor = -1;
	float dist_melhor = 1e9f;
	for (guint i = 0; blocos && i < blocos->len; i++) {
		Clay_BoundingBox c;
		if (!caixa_de(&g_array_index(blocos, struct bloco, i), &c)) {
			continue;
		}
		bool dentro_y = y >= c.y && y <= c.y + c.height;
		if (dentro_y && x >= c.x && x <= c.x + c.width) {
			*caixa = c;
			return (int)i;
		}
		if (so_dentro) {
			continue;
		}
		float dy = y < c.y ? c.y - y : y > c.y + c.height ? y - (c.y + c.height) : 0;
		if (dy < dist_melhor) {
			dist_melhor = dy;
			melhor = (int)i;
			*caixa = c;
		}
	}
	return melhor;
}

static bool e_palavra(gunichar c) {
	return g_unichar_isalnum(c) || c == '_';
}

static void palavra_em(const char *t, int idx, int *ini, int *fim) {
	int n = (int)strlen(t);
	if (idx >= n) {
		idx = n > 0 ? (int)(g_utf8_prev_char(t + n) - t) : 0;
	}
	if (n == 0) {
		*ini = *fim = 0;
		return;
	}
	gunichar c = g_utf8_get_char(t + idx);
	bool pal = e_palavra(c), espaco = g_unichar_isspace(c);
	const char *a = t + idx, *b = g_utf8_next_char(t + idx);
	while (a > t) {
		const char *p = g_utf8_prev_char(a);
		gunichar q = g_utf8_get_char(p);
		if (pal ? !e_palavra(q) : espaco ? !g_unichar_isspace(q) : (e_palavra(q) || g_unichar_isspace(q))) {
			break;
		}
		a = p;
	}
	while (*b) {
		gunichar q = g_utf8_get_char(b);
		if (pal ? !e_palavra(q) : espaco ? !g_unichar_isspace(q) : (e_palavra(q) || g_unichar_isspace(q))) {
			break;
		}
		b = g_utf8_next_char(b);
	}
	*ini = (int)(a - t);
	*fim = (int)(b - t);
}

static void paragrafo_em(const char *t, int idx, int *ini, int *fim) {
	int n = (int)strlen(t);
	int a = idx > n ? n : idx, b = a;
	while (a > 0 && t[a - 1] != '\n') {
		a--;
	}
	while (b < n && t[b] != '\n') {
		b++;
	}
	*ini = a;
	*fim = b;
}

/* os dois extremos em ordem: bloco e byte */
static bool extremos(int *bi, int *ii, int *bf, int *fi) {
	if (!sel.ativa) {
		return false;
	}
	int pa = posicao_de(sel.a_id), pc = posicao_de(sel.c_id);
	if (pa < 0 || pc < 0) {
		return false;
	}
	if (pa < pc || (pa == pc && sel.a_idx <= sel.c_idx)) {
		*bi = pa;
		*ii = sel.a_idx;
		*bf = pc;
		*fi = sel.c_idx;
	} else {
		*bi = pc;
		*ii = sel.c_idx;
		*bf = pa;
		*fi = sel.a_idx;
	}
	return true;
}

bool gv__selecao_faixa(uint32_t id, int *ini, int *fim) {
	int bi, ii, bf, fi;
	if (!extremos(&bi, &ii, &bf, &fi)) {
		return false;
	}
	int k = posicao_de(id);
	if (k < bi || k > bf) {
		return false;
	}
	int n = (int)strlen(g_array_index(blocos, struct bloco, k).plano);
	*ini = k == bi ? ii : 0;
	*fim = k == bf ? fi : n;
	return *ini < *fim;
}

const char *gv_selecao_texto(void) {
	static char *texto;
	g_free(texto);
	texto = NULL;
	int bi, ii, bf, fi;
	if (!extremos(&bi, &ii, &bf, &fi)) {
		return "";
	}
	GString *g = g_string_new(NULL);
	for (int k = bi; k <= bf; k++) {
		const char *p = g_array_index(blocos, struct bloco, k).plano;
		int n = (int)strlen(p), a = k == bi ? ii : 0, b = k == bf ? fi : n;
		if (a < b && b <= n) {
			if (g->len) {
				g_string_append_c(g, '\n');
			}
			g_string_append_len(g, p + a, b - a);
		}
	}
	texto = g_string_free(g, FALSE);
	return texto;
}

bool gv_selecao_ativa(void) {
	return gv_selecao_texto()[0] != '\0';
}

void gv_selecao_copiar(void) {
	const char *t = gv_selecao_texto();
	if (t[0]) {
		gv_copiar(t);
	}
}

void gv_selecao_limpar(void) {
	sel.ativa = sel.arrastando = false;
}

void gv__selecao_processar(void) {
	if (!blocos) {
		return;
	}
	if (sel.ativa && (posicao_de(sel.a_id) < 0 || posicao_de(sel.c_id) < 0)) {
		gv_selecao_limpar(); /* a conversa mudou: os blocos já não são os mesmos */
	}
	float px = gv__entrada.x, py = gv__entrada.y;
	if (gv__entrada.apertou && !gv__entrada.toque) {
		Clay_BoundingBox c;
		int k = bloco_perto(px, py, true, &c);
		double agora = gv_agora();
		if (k < 0) {
			gv_selecao_limpar();
			sel.cliques = 0;
		} else {
			struct bloco *b = &g_array_index(blocos, struct bloco, k);
			bool perto = fabsf(px - sel.ux) < 6 && fabsf(py - sel.uy) < 6;
			sel.cliques = (agora - sel.ultimo < 0.45 && perto && sel.cliques < 3) ? sel.cliques + 1 : 1;
			int idx = indice_em(b, c, px, py);
			sel.a_id = sel.c_id = b->id;
			sel.a_idx = sel.c_idx = idx;
			sel.ativa = true;
			sel.arrastando = true;
			if (sel.cliques == 2) {
				palavra_em(b->plano, idx, &sel.a_idx, &sel.c_idx);
			} else if (sel.cliques >= 3) {
				paragrafo_em(b->plano, idx, &sel.a_idx, &sel.c_idx);
			}
		}
		sel.ultimo = agora;
		sel.ux = px;
		sel.uy = py;
	} else if (sel.arrastando && gv__entrada.baixo && sel.cliques == 1) {
		Clay_BoundingBox c;
		int k = bloco_perto(px, py, false, &c);
		if (k >= 0) {
			struct bloco *b = &g_array_index(blocos, struct bloco, k);
			sel.c_id = b->id;
			sel.c_idx = indice_em(b, c, px, py);
		}
		gv__app_pedir();
	}
	if (!gv__entrada.baixo) {
		sel.arrastando = false;
	}
	if (sel.ativa) {
		for (int i = 0; i < gv__entrada.nteclas; i++) {
			uint32_t sym = xkb_keysym_to_lower(gv__entrada.teclas[i]);
			if (sym == XKB_KEY_Escape) {
				gv_selecao_limpar();
			} else if (sym == XKB_KEY_c && (gv__entrada.mods & GV_MOD_CTRL)) {
				gv_selecao_copiar();
			}
		}
	}
}
