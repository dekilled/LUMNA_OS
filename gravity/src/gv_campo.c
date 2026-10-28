/*
 * O campo de texto. A app guarda o texto; o Gravity guarda, por id, o cursor, a seleção (o
 * cursor e a âncora, em bytes), a rolagem e o pisca do cursor.
 *
 * Teclado: letras (com os acentos compostos), ← → (Ctrl: palavra a palavra), Home/End, Shift
 * para selecionar, Backspace/Delete (Ctrl: a palavra), Ctrl+A, Ctrl+C/X/V, Enter, Esc (sai).
 * Várias linhas (gv_campo_area): Enter quebra a linha e Ctrl+Enter envia, ↑ ↓ andam entre as
 * linhas (lembrando a coluna), Home/End vão às pontas da linha (Ctrl: do texto); o campo cresce
 * até linhas_max e depois rola por dentro, seguindo o cursor.
 * Rato e dedo: tocar põe o cursor, arrastar seleciona, dois toques selecionam a palavra.
 * Teclado no ecrã: o text-input recebe o texto à volta e o tipo do campo, e o que ele escreve
 * (e a palavra a compor, sublinhada) entra aqui.
 */
#include "gv_interno.h"

#include <glib.h>
#include <math.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#define GV_MAX_CAMPOS 32
#define PISCA 0.53 /* segundos aceso e apagado */

struct campo {
	uint32_t id;
	int cursor, ancora;
	float rolagem;
	float rolagem_y;       /* várias linhas: quanto rolou para baixo */
	float x_desejado;      /* ↑ ↓ lembram a coluna (-1: nenhuma) */
	double editado;        /* o cursor fica aceso depois de mexer */
	double ultimo_toque;
	int toques;            /* dois seguidos: palavra */
	bool mostrar;          /* senha à vista */
	uint32_t visto;
	Clay_BoundingBox caixa_texto; /* a área do texto, do quadro anterior */
};

static struct campo campos[GV_MAX_CAMPOS];
/* uma ação do menu de contexto (Recortar, Copiar, Colar, Selecionar tudo): o campo a faz no quadro seguinte como se fosse o atalho */
static uint32_t acao_menu, acao_menu_id;
static uint32_t quadro;
/* o foco é o da app inteira (gv_foco.c): tocar fora de tudo tira-o, e o teclado no ecrã some */
void gv__campos_fim_do_quadro(void) {
	quadro++;
}

static struct campo *campo_de(uint32_t id) {
	struct campo *livre = NULL, *velho = NULL;
	for (int i = 0; i < GV_MAX_CAMPOS; i++) {
		if (campos[i].id == id) {
			return &campos[i];
		}
		if (!campos[i].id) {
			if (!livre) {
				livre = &campos[i];
			}
		} else if (!velho || campos[i].visto < velho->visto) {
			velho = &campos[i];
		}
	}
	struct campo *c = livre ? livre : velho;
	*c = (struct campo){ .id = id, .cursor = -1 };
	return c;
}

/* um id filho, para as partes do campo */
static Clay_ElementId parte(Clay_ElementId id, uint32_t n) {
	return Clay__HashStringWithOffset(id.stringId, id.offset * 64 + 1000003u + n, id.baseId);
}

/* ---- UTF-8 ---- */

static int anterior(const char *t, int i) {
	if (i <= 0) {
		return 0;
	}
	const char *p = g_utf8_find_prev_char(t, t + i);
	return p ? (int)(p - t) : 0;
}

static int seguinte(const char *t, int i) {
	if (!t[i]) {
		return i;
	}
	return (int)(g_utf8_next_char(t + i) - t);
}

static bool espaco(const char *t, int i) {
	return t[i] == ' ' || t[i] == '\t' || t[i] == '\n';
}

static int palavra_antes(const char *t, int i) {
	while (i > 0 && espaco(t, anterior(t, i))) {
		i = anterior(t, i);
	}
	while (i > 0 && !espaco(t, anterior(t, i))) {
		i = anterior(t, i);
	}
	return i;
}

static int palavra_depois(const char *t, int i) {
	while (t[i] && espaco(t, i)) {
		i = seguinte(t, i);
	}
	while (t[i] && !espaco(t, i)) {
		i = seguinte(t, i);
	}
	return i;
}

/* a senha aparece como pontos: um "•" (3 bytes) por caractere */
static int para_mostrado(const char *t, int i, bool pontos) {
	return pontos ? (int)g_utf8_pointer_to_offset(t, t + i) * 3 : i;
}

static int do_mostrado(const char *t, int i, bool pontos) {
	if (!pontos) {
		return i;
	}
	long n = i / 3, total = g_utf8_strlen(t, -1);
	if (n > total) {
		n = total;
	}
	return (int)(g_utf8_offset_to_pointer(t, n) - t);
}

/* ---- edição ---- */

static void apagar(char *t, int ini, int fim) {
	if (fim > ini) {
		memmove(t + ini, t + fim, strlen(t + fim) + 1);
	}
}

/* põe "novo" no cursor (no lugar da seleção); devolve se coube */
static bool inserir(char *t, size_t cap, struct campo *c, const char *novo) {
	int ini = c->cursor < c->ancora ? c->cursor : c->ancora;
	int fim = c->cursor < c->ancora ? c->ancora : c->cursor;
	apagar(t, ini, fim);
	size_t n = strlen(novo), tam = strlen(t);
	if (tam + n + 1 > cap) {
		/* corta o que não cabe, sem partir um caractere */
		n = cap > tam + 1 ? cap - tam - 1 : 0;
		while (n > 0 && (novo[n] & 0xc0) == 0x80) {
			n--;
		}
	}
	memmove(t + ini + n, t + ini, tam - ini + 1);
	memcpy(t + ini, novo, n);
	c->cursor = c->ancora = ini + (int)n;
	return n > 0 || fim > ini;
}

static bool numero_valido(const char *s) {
	for (; *s; s++) {
		if (!((*s >= '0' && *s <= '9') || *s == ',' || *s == '.' || *s == '-' || *s == '+')) {
			return false;
		}
	}
	return true;
}

/* gv_campo_chat: Enter envia e Shift+Enter quebra a linha (nas várias linhas comuns é o contrário: Ctrl+Enter envia) */
static bool modo_chat;
static bool sem_moldura; /* gv_campo_chat sem moldura: quem o envolve (uma pílula) é que desenha o contorno */

static int teclas(char *t, size_t cap, struct campo *c, enum gv_campo_tipo tipo, Clay_ElementId id, float tamanho,
		float largura) {
	bool multi = largura > 0;
	int resultado = 0;
	bool shift = gv__entrada.mods & GV_MOD_SHIFT;
	bool ctrl = gv__entrada.mods & GV_MOD_CTRL;
	bool senha = tipo == GV_CAMPO_SENHA;
	for (int k = 0; k < gv__entrada.nteclas; k++) {
		uint32_t sym = gv__entrada.teclas[k];
		int ini = c->cursor < c->ancora ? c->cursor : c->ancora;
		int fim = c->cursor < c->ancora ? c->ancora : c->cursor;
		int antes = c->cursor;
		if (sym != XKB_KEY_Up && sym != XKB_KEY_Down) {
			c->x_desejado = -1;
		}
		switch (sym) {
		case XKB_KEY_Up:
		case XKB_KEY_Down: {
			if (!multi) {
				continue;
			}
			float x, y, h;
			gv__texto_pos(t, tamanho, largura, c->cursor, &x, &y, &h);
			if (c->x_desejado < 0) {
				c->x_desejado = x;
			}
			if (sym == XKB_KEY_Up) {
				c->cursor = y < h / 2 ? 0 : gv__texto_indice_xy(t, tamanho, largura, c->x_desejado, y - h / 2);
			} else {
				float total = gv__texto_altura(t, tamanho, largura);
				c->cursor = y + h * 1.5f > total ? (int)strlen(t) : gv__texto_indice_xy(t, tamanho, largura, c->x_desejado, y + h * 1.5f);
			}
			break;
		}
		case XKB_KEY_Left:
			c->cursor = !shift && ini != fim ? ini : ctrl ? palavra_antes(t, c->cursor) : anterior(t, c->cursor);
			break;
		case XKB_KEY_Right:
			c->cursor = !shift && ini != fim ? fim : ctrl ? palavra_depois(t, c->cursor) : seguinte(t, c->cursor);
			break;
		case XKB_KEY_Home:
		case XKB_KEY_KP_Home:
		case XKB_KEY_End:
		case XKB_KEY_KP_End: {
			bool home = sym == XKB_KEY_Home || sym == XKB_KEY_KP_Home;
			if (multi && !ctrl) {
				int li, lf;
				gv__texto_linha_de(t, tamanho, largura, c->cursor, &li, &lf);
				c->cursor = home ? li : lf;
			} else {
				c->cursor = home ? 0 : (int)strlen(t);
			}
			break;
		}
		case XKB_KEY_BackSpace:
			if (ini == fim) {
				ini = ctrl ? palavra_antes(t, c->cursor) : anterior(t, c->cursor);
			}
			apagar(t, ini, fim);
			c->cursor = c->ancora = ini;
			resultado |= fim > ini ? GV_CAMPO_MUDOU : 0;
			continue;
		case XKB_KEY_Delete:
		case XKB_KEY_KP_Delete:
			if (ini == fim) {
				fim = ctrl ? palavra_depois(t, c->cursor) : seguinte(t, c->cursor);
			}
			apagar(t, ini, fim);
			c->cursor = c->ancora = ini;
			resultado |= fim > ini ? GV_CAMPO_MUDOU : 0;
			continue;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			/* várias linhas: Enter quebra a linha; Ctrl+Enter é o "enviar" (no modo chat, o contrário: Shift+Enter quebra) */
			if (multi && (modo_chat ? shift : !ctrl)) {
				if (inserir(t, cap, c, "\n")) {
					resultado |= GV_CAMPO_MUDOU;
				}
			} else {
				resultado |= GV_CAMPO_ENTER;
			}
			continue;
		case XKB_KEY_Escape:
			gv__foco_definir(0, false);
			continue;
		default:
			if (ctrl) {
				uint32_t letra = xkb_keysym_to_lower(sym);
				if (letra == XKB_KEY_a) {
					c->ancora = 0;
					c->cursor = (int)strlen(t);
				} else if ((letra == XKB_KEY_c || letra == XKB_KEY_x) && ini != fim && !senha) {
					char *pedaco = g_strndup(t + ini, fim - ini);
					gv_copiar(pedaco);
					g_free(pedaco);
					if (letra == XKB_KEY_x) {
						apagar(t, ini, fim);
						c->cursor = c->ancora = ini;
						resultado |= GV_CAMPO_MUDOU;
					}
				} else if (letra == XKB_KEY_v) {
					const char *colado = gv_colar();
					if (colado) {
						/* um campo de uma linha: as quebras viram espaços (num de várias ficam) */
						char *linha = g_strdup(colado);
						char *escrita = linha;
						for (char *p = linha; *p; p++) {
							if (*p == '\r') {
								continue;
							}
							*escrita++ = (*p == '\n' && !multi) || *p == '\t' ? ' ' : *p;
						}
						*escrita = '\0';
						if ((tipo != GV_CAMPO_NUMERO || numero_valido(linha)) && inserir(t, cap, c, linha)) {
							resultado |= GV_CAMPO_MUDOU;
						}
						g_free(linha);
					}
				}
			}
			continue;
		}
		/* só os movimentos chegam aqui: sem Shift, a seleção acompanha o cursor */
		if (!shift) {
			c->ancora = c->cursor;
		}
		(void)antes;
	}
	(void)id;
	return resultado;
}

/* ---- o componente ---- */

static int campo(Clay_ElementId id, char *texto, size_t capacidade, const char *dica, enum gv_campo_tipo tipo,
		int linhas_min, int linhas_max) {
	struct campo *c = campo_de(id.id);
	bool multi = linhas_max > 1;
	c->visto = quadro;
	int n = (int)strlen(texto);
	if (c->cursor < 0 || c->cursor > n) {
		c->cursor = c->ancora = n;
	}
	if (c->ancora > n) {
		c->ancora = n;
	}
	int resultado = 0;
	bool senha = tipo == GV_CAMPO_SENHA;
	bool pontos = senha && !c->mostrar;
	float tamanho = gv_t.tamanho_corpo;
	Clay_ElementId caixa_id = parte(id, 0);
	Clay_ElementId botao_id = parte(id, 1);
	/* várias linhas: quebram na largura da área do texto (a do quadro anterior) */
	float largura_quebra = multi ? (c->caixa_texto.width > 0 ? c->caixa_texto.width : 300) : 0;

	/* o botão direito: o menu de contexto (o campo fica com o foco, e a ação vale para ele) */
	Clay_ElementId menu_id = parte(id, 9);
	if (gv_menu_no_ponteiro(id, menu_id)) {
		gv__foco_definir(id.id, false);
	}

	/* ---- toque e rato: pôr o cursor, arrastar para selecionar, dois toques: palavra ---- */
	bool no_botao = gv_por_cima(botao_id);
	if (gv__entrada.apertou && !gv__entrada.travado && gv_por_cima(id) && !no_botao) {
		gv__foco_definir(id.id, false);
		gv__entrada.ativo = id.id;
		const char *mostrado = pontos ? NULL : texto;
		char *bolinhas = NULL;
		if (pontos) {
			long k = g_utf8_strlen(texto, -1);
			bolinhas = g_malloc(k * 3 + 1);
			for (long j = 0; j < k; j++) {
				memcpy(bolinhas + j * 3, "•", 3);
			}
			bolinhas[k * 3] = '\0';
			mostrado = bolinhas;
		}
		int i = multi ? gv__texto_indice_xy(texto, tamanho, largura_quebra, gv__entrada.x - c->caixa_texto.x,
				gv__entrada.y - c->caixa_texto.y + c->rolagem_y)
			: do_mostrado(texto, gv__texto_indice(mostrado, tamanho, gv__entrada.x - c->caixa_texto.x + c->rolagem), pontos);
		g_free(bolinhas);
		c->x_desejado = -1;
		double agora = gv_agora();
		c->toques = agora - c->ultimo_toque < 0.4 ? c->toques + 1 : 1;
		c->ultimo_toque = agora;
		if (c->toques >= 2 && !pontos) {
			/* dois toques: a palavra inteira */
			int ini = i, fim = i;
			while (ini > 0 && !espaco(texto, anterior(texto, ini))) {
				ini = anterior(texto, ini);
			}
			while (texto[fim] && !espaco(texto, fim)) {
				fim = seguinte(texto, fim);
			}
			c->ancora = ini;
			c->cursor = fim;
		} else {
			c->cursor = i;
			if (!(gv__entrada.mods & GV_MOD_SHIFT)) {
				c->ancora = i;
			}
		}
		c->editado = agora;
	} else if (gv__entrada.apertou && gv_por_cima(id)) {
	}
	bool arrastando = gv__entrada.ativo == id.id && gv__entrada.baixo && c->toques < 2;
	if (arrastando && gv__foco_id() == id.id) {
		char *bolinhas = NULL;
		const char *mostrado = texto;
		if (pontos) {
			long k = g_utf8_strlen(texto, -1);
			bolinhas = g_malloc(k * 3 + 1);
			for (long j = 0; j < k; j++) {
				memcpy(bolinhas + j * 3, "•", 3);
			}
			bolinhas[k * 3] = '\0';
			mostrado = bolinhas;
		}
		c->cursor = multi ? gv__texto_indice_xy(texto, tamanho, largura_quebra, gv__entrada.x - c->caixa_texto.x,
				gv__entrada.y - c->caixa_texto.y + c->rolagem_y)
			: do_mostrado(texto, gv__texto_indice(mostrado, tamanho, gv__entrada.x - c->caixa_texto.x + c->rolagem), pontos);
		g_free(bolinhas);
	}

	/* ---- teclado ---- */
	bool focado = gv__focavel(id, false); /* entra na fila do Tab; o anel é a própria borda */
	const char *compondo = "";
	if (focado) {
		int antes = (int)strlen(texto);
		bool do_menu = acao_menu && acao_menu_id == id.id && gv__entrada.nteclas < GV_MAX_TECLAS;
		uint32_t mods_antes = gv__entrada.mods;
		int nteclas_antes = gv__entrada.nteclas;
		if (do_menu) {
			gv__entrada.teclas[gv__entrada.nteclas++] = acao_menu;
			gv__entrada.mods |= GV_MOD_CTRL;
			acao_menu = 0;
		}
		resultado |= teclas(texto, capacidade, c, tipo, id, tamanho, largura_quebra);
		if (do_menu) {
			gv__entrada.mods = mods_antes;
			gv__entrada.nteclas = nteclas_antes;
		}
		if (gv__entrada.texto[0] && (tipo != GV_CAMPO_NUMERO || numero_valido(gv__entrada.texto))) {
			if (inserir(texto, capacidade, c, gv__entrada.texto)) {
				resultado |= GV_CAMPO_MUDOU;
			}
		}
		if (gv__entrada.ime.chegou) {
			/* o teclado no ecrã: apaga à volta do cursor, escreve, e mostra a palavra a compor */
			int ini = c->cursor - (int)gv__entrada.ime.apagar_antes;
			int fim = c->cursor + (int)gv__entrada.ime.apagar_depois;
			int tam = (int)strlen(texto);
			ini = ini < 0 ? 0 : ini;
			fim = fim > tam ? tam : fim;
			if (fim > ini) {
				apagar(texto, ini, fim);
				c->cursor = c->ancora = ini;
				resultado |= GV_CAMPO_MUDOU;
			}
			if (gv__entrada.ime.escrito[0]) {
				/* "\n" do teclado no ecrã é o "ir" (num campo de várias linhas, uma linha nova) */
				char *sem_enter = g_strdup(gv__entrada.ime.escrito);
				char *nl = multi ? NULL : strchr(sem_enter, '\n');
				if (nl) {
					*nl = '\0';
					resultado |= GV_CAMPO_ENTER;
				}
				if (sem_enter[0] && inserir(texto, capacidade, c, sem_enter)) {
					resultado |= GV_CAMPO_MUDOU;
				}
				g_free(sem_enter);
			}
		}
		compondo = gv__entrada.ime.compondo;
		if (resultado || (int)strlen(texto) != antes || gv__entrada.nteclas) {
			c->editado = gv_agora();
		}
	}

	/* ---- o que aparece ---- */
	n = (int)strlen(texto);
	const char *base = texto;
	if (pontos) {
		long k = g_utf8_strlen(texto, -1);
		char *b = gv__quadro_alloc(k * 3 + 1);
		for (long j = 0; j < k; j++) {
			memcpy(b + j * 3, "•", 3);
		}
		b[k * 3] = '\0';
		base = b;
	}
	int cur = para_mostrado(texto, c->cursor, pontos);
	int anc = para_mostrado(texto, c->ancora, pontos);
	size_t lc = strlen(compondo), lb = strlen(base);
	char *mostrado = gv__quadro_alloc(lb + lc + 1);
	memcpy(mostrado, base, cur);
	memcpy(mostrado + cur, compondo, lc);
	memcpy(mostrado + cur + lc, base + cur, lb - cur + 1);
	int cursor_mostrado = cur + (lc ? gv__entrada.ime.compondo_fim : 0);
	if (cursor_mostrado > cur + (int)lc) {
		cursor_mostrado = cur + (int)lc;
	}

	/* a rolagem segue o cursor */
	float largura = c->caixa_texto.width > 0 ? c->caixa_texto.width : 200;
	float cx, cy = 0, ch = 0;
	float linha = gv__texto_altura("", tamanho, 0);
	int nlinhas = 1;
	if (multi) {
		gv__texto_pos(mostrado, tamanho, largura_quebra, cursor_mostrado, &cx, &cy, &ch);
		float total = gv__texto_altura(mostrado, tamanho, largura_quebra);
		nlinhas = (int)ceilf(total / linha - 0.01f);
		nlinhas = nlinhas < linhas_min ? linhas_min : nlinhas > linhas_max ? linhas_max : nlinhas;
		float vista = nlinhas * linha;
		c->rolagem = 0;
		/* a roda rola por dentro (e não chega à rolagem de fora) quando há o que rolar */
		if (gv_por_cima(id) && gv__entrada.roda_y != 0 && total > vista) {
			c->rolagem_y -= gv__entrada.roda_y * 5;
			gv__entrada.roda_y = 0;
		} else if (focado && (resultado || gv__entrada.nteclas || gv__entrada.ime.chegou || arrastando)) {
			if (cy - c->rolagem_y < 0) {
				c->rolagem_y = cy;
			}
			if (cy + ch - c->rolagem_y > vista) {
				c->rolagem_y = cy + ch - vista;
			}
		}
		c->rolagem_y = fmaxf(0, fminf(c->rolagem_y, total - vista));
	} else {
		cx = gv__texto_x(mostrado, tamanho, cursor_mostrado);
		float total = gv__texto_x(mostrado, tamanho, (int)strlen(mostrado));
		if (cx - c->rolagem > largura - 2) {
			c->rolagem = cx - largura + 2;
		}
		if (cx - c->rolagem < 0) {
			c->rolagem = cx;
		}
		if (total - c->rolagem < largura - 2) {
			c->rolagem = fmaxf(0, total - largura + 2);
		}
	}

	/* o pisca: aceso logo depois de mexer; acorda o Gravity só para a próxima troca */
	float alfa = 0;
	if (focado) {
		double t = gv_agora() - c->editado;
		double fase = fmod(t, 2 * PISCA);
		alfa = fase < PISCA ? 1 : 0;
		gv__app_acordar_em((fase < PISCA ? PISCA : 2 * PISCA) - fase + 0.005);
		/* o teclado no ecrã fica a saber do campo */
		uint32_t dicas = 0, proposito = 0;
		switch (tipo) {
		case GV_CAMPO_SENHA:
			dicas = 0x40 | 0x80; /* hidden_text | sensitive_data */
			proposito = 8;
			break;
		case GV_CAMPO_NUMERO:
			proposito = 3;
			break;
		case GV_CAMPO_EMAIL:
			proposito = 6;
			break;
		case GV_CAMPO_URL:
			proposito = 5;
			break;
		default:
			dicas = 0x1 | 0x2 | 0x4; /* completion | spellcheck | auto_capitalization */
			break;
		}
		if (multi) {
			dicas |= 0x200; /* multiline: o Enter do teclado no ecrã quebra a linha */
		}
		Clay_BoundingBox cursor_caixa = multi
			? (Clay_BoundingBox){ c->caixa_texto.x + cx, c->caixa_texto.y + cy - c->rolagem_y, 2, ch }
			: (Clay_BoundingBox){ c->caixa_texto.x + cx - c->rolagem, c->caixa_texto.y, 2, c->caixa_texto.height };
		gv__campo_pede(senha ? "" : texto, senha ? 0 : c->cursor, senha ? 0 : c->ancora, cursor_caixa, dicas, proposito);
	}

	struct gv_extra *e = gv__quadro_alloc(sizeof(*e));
	if (e) {
		memset(e, 0, sizeof(*e));
		e->tipo = GV_EXTRA_CAMPO;
		e->campo.mostrado = mostrado;
		e->campo.dica = (!n && !lc && dica) ? dica : NULL;
		e->campo.tamanho = tamanho;
		e->campo.rolagem = c->rolagem;
		e->campo.largura = largura_quebra;
		e->campo.rolagem_y = c->rolagem_y;
		e->campo.cursor = cursor_mostrado;
		int si = cur < anc ? cur : anc, sf = cur < anc ? anc : cur;
		/* a seleção (com a palavra a compor no meio, desloca o que vem depois) */
		e->campo.sel_ini = si <= cur ? si : si + (int)lc;
		e->campo.sel_fim = sf <= cur ? sf : sf + (int)lc;
		if (!focado) {
			e->campo.sel_ini = e->campo.sel_fim = 0;
		}
		e->campo.comp_ini = cur;
		e->campo.comp_fim = cur + (int)lc;
		e->campo.cursor_alfa = alfa;
		e->campo.cor = gv_t.texto;
		e->campo.cor_dica = gv_t.texto_suave;
		e->campo.cor_sel = gv_alfa(gv_t.acento, 0.35f);
		e->campo.cor_cursor = gv_t.acento;
	}

	/* ---- desenho ---- */
	float realce = gv_anim(id.id, focado ? 1 : 0, gv_t.duracao_rapida);
	/* em repouso a borda discreta, sob o cursor a de controle, com foco o anel de foco */
	Clay_Color borda = gv_por_cima(id) && !gv__entrada.toque ? gv_t.borda_controle : gv_t.borda;
	borda = (Clay_Color){
		borda.r + (gv_t.foco.r - borda.r) * realce,
		borda.g + (gv_t.foco.g - borda.g) * realce,
		borda.b + (gv_t.foco.b - borda.b) * realce,
		borda.a + (gv_t.foco.a - borda.a) * realce,
	};
	bool com_botao = !multi && ((tipo == GV_CAMPO_BUSCA && n > 0) || senha);
	bool moldura = !sem_moldura;
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), multi ? CLAY_SIZING_FIT(44) : CLAY_SIZING_FIXED(44) },
			.padding = { moldura ? (tipo == GV_CAMPO_BUSCA ? 14 : 16) : 8, com_botao ? 6 : moldura ? 16 : 8, multi ? 11 : 0, multi ? 11 : 0 }, .childGap = 10,
			.childAlignment = { .y = multi ? CLAY_ALIGN_Y_TOP : CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = moldura ? gv_t.elevada : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = borda, .width = moldura ? GV_BORDA(1) : (Clay_BorderWidth){ 0 } },
	}) {
		if (tipo == GV_CAMPO_BUSCA) {
			gv_icone("search", 20, focado ? gv_t.icone : gv_t.texto_secundario);
		}
		CLAY(caixa_id, {
			.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(multi ? nlinhas * linha : tamanho * 1.5f) } },
			.custom = { .customData = e },
		}) {}
		if (com_botao) {
			bool sobre = gv_por_cima(botao_id) && !gv__entrada.toque;
			CLAY(botao_id, {
				.layout = { .sizing = { CLAY_SIZING_FIXED(38), CLAY_SIZING_FIXED(38) },
					.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_alfa(gv_t.sob_cursor, sobre ? 1 : 0),
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
			}) {
				if (senha) {
					gv_icone(c->mostrar ? "eye-off" : "eye", 20, c->mostrar ? gv_t.acento : gv_t.texto_secundario);
				} else {
					gv_icone("close", 18, gv_t.texto_suave);
				}
			}
			if (gv_tocado(botao_id)) {
				if (senha) {
					c->mostrar = !c->mostrar;
				} else {
					texto[0] = '\0';
					c->cursor = c->ancora = 0;
					c->rolagem = 0;
					resultado |= GV_CAMPO_MUDOU;
					gv__foco_definir(id.id, false);
				}
			}
		}
	}
	GV_MENU_PONTEIRO(menu_id) {
		bool marcado = c->cursor != c->ancora && !senha;
		if (marcado && gv_menu_item(parte(id, 10), "cut", "Recortar", "Ctrl+X")) {
			acao_menu = XKB_KEY_x;
			acao_menu_id = id.id;
			gv_popup_fechar(menu_id);
			gv__app_pedir();
		}
		if (marcado && gv_menu_item(parte(id, 11), "copy", "Copiar", "Ctrl+C")) {
			acao_menu = XKB_KEY_c;
			acao_menu_id = id.id;
			gv_popup_fechar(menu_id);
			gv__app_pedir();
		}
		if (gv_menu_item(parte(id, 12), "paste", "Colar", "Ctrl+V")) {
			acao_menu = XKB_KEY_v;
			acao_menu_id = id.id;
			gv_popup_fechar(menu_id);
			gv__app_pedir();
		}
		if (texto[0] && gv_menu_item(parte(id, 13), "text", "Selecionar tudo", "Ctrl+A")) {
			acao_menu = XKB_KEY_a;
			acao_menu_id = id.id;
			gv_popup_fechar(menu_id);
			gv__app_pedir();
		}
	}
	Clay_ElementData dados = Clay_GetElementData(caixa_id);
	if (dados.found) {
		if (multi && dados.boundingBox.width != c->caixa_texto.width) {
			gv__app_pedir(); /* a largura mudou: as linhas quebram noutro sítio */
		}
		c->caixa_texto = dados.boundingBox;
	}
	return resultado;
}

int gv_campo(Clay_ElementId id, char *texto, size_t capacidade, const char *dica, enum gv_campo_tipo tipo) {
	return campo(id, texto, capacidade, dica, tipo, 1, 1);
}

int gv_campo_area(Clay_ElementId id, char *texto, size_t capacidade, const char *dica, int linhas_min, int linhas_max) {
	if (linhas_min < 1) {
		linhas_min = 3;
	}
	if (linhas_max < linhas_min) {
		linhas_max = linhas_min > 8 ? linhas_min : 8;
	}
	return campo(id, texto, capacidade, dica, GV_CAMPO_TEXTO, linhas_min, linhas_max < 2 ? 2 : linhas_max);
}

int gv_campo_chat(Clay_ElementId id, char *texto, size_t capacidade, const char *dica, int linhas_max, bool moldura) {
	modo_chat = true;
	sem_moldura = !moldura;
	int r = gv_campo_area(id, texto, capacidade, dica, 1, linhas_max < 2 ? 2 : linhas_max);
	modo_chat = sem_moldura = false;
	return r;
}
