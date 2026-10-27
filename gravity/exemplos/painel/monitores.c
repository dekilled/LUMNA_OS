/*
 * Telas e modos → Monitores: o desenho de onde cada tela está, e por tela a resolução, a taxa, a escala, a
 * rotação, ligar/desligar e a posição (à direita, à esquerda, acima ou abaixo de outra). Vai para
 * ~/.config/lumna/telas.ini no Aplicar e o Orbit aplica na hora (servicos/telas.c).
 *
 * Mudar a resolução, a escala, a rotação ou ligar/desligar uma tela pode deixar a imagem ilegível: depois do
 * Aplicar o Painel pergunta "Manter?" e, sem resposta em 15 s, volta ao que estava (sair da seção, ou seja,
 * ver o Painel, conta como manter).
 */
#include "painel.h"

#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "telas.h"

static const double escalas[] = { 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0 };
static const char *const escala_nomes[] = { "100%", "125%", "150%", "175%", "200%", "250%", "300%" };
#define NESCALAS 7
static const char *const rot_nomes[] = { "Normal", "90°", "180°", "270°" };
static const char *const lado_nomes[] = { "À direita de", "À esquerda de", "Acima de", "Abaixo de" };

struct ed {
	bool ligada;
	int l, a, hz;
	int escala_i, rot_i;
	int ref, lado;
};

static struct telas_estado vivo;
static struct ed e[TELAS_MAX], g[TELAS_MAX];
static int sel;
static bool lido;
static bool pos_mexida;       /* a posição de alguma tela foi mexida (senão as posições ficam como estão) */
static bool confirmando;
static double ate;            /* a hora em que volta sozinho */
static struct tela_cfg antes[TELAS_MAX];
static int nantes;
static char mensagem[200];
static double reler_em;

static int escala_indice(double s) {
	int melhor = 0;
	for (int i = 1; i < NESCALAS; i++) {
		if (fabs(escalas[i] - s) < fabs(escalas[melhor] - s)) {
			melhor = i;
		}
	}
	return melhor;
}

static int rot_indice(int graus) {
	return graus == 90 ? 1 : graus == 180 ? 2 : graus == 270 ? 3 : 0;
}

static int primeira_ligada(void) {
	for (int i = 0; i < vivo.n; i++) {
		if (e[i].ligada) {
			return i;
		}
	}
	return 0;
}

static void ler_vivo(void) {
	telas_ler(&vivo);
	int ancora = -1;
	for (int i = 0; i < vivo.n; i++) {
		const struct tela *t = &vivo.v[i];
		e[i] = (struct ed){ .ligada = t->ligada, .l = t->largura, .a = t->altura, .hz = t->hz, .escala_i = escala_indice(t->escala),
			.rot_i = rot_indice(t->rotacao), .ref = 0, .lado = 0 };
		if (t->ligada && ancora < 0) {
			ancora = i;
		}
	}
	/* a relação de cada uma com a âncora, vista das posições de agora */
	for (int i = 0; i < vivo.n; i++) {
		if (!vivo.v[i].ligada || i == ancora || ancora < 0) {
			continue;
		}
		int w, h, rw, rh;
		tela_tamanho_logico(&vivo.v[i], &w, &h);
		tela_tamanho_logico(&vivo.v[ancora], &rw, &rh);
		const struct tela *t = &vivo.v[i], *r = &vivo.v[ancora];
		e[i].ref = ancora;
		e[i].lado = t->x >= r->x + rw - 1 ? 0 : t->x + w <= r->x + 1 ? 1 : t->y + h <= r->y + 1 ? 2 : 3;
	}
	memcpy(g, e, sizeof(g));
	pos_mexida = false;
	if (sel >= vivo.n) {
		sel = 0;
	}
	lido = true;
}

void pn_monitores_abrir(void) {
	mensagem[0] = '\0';
	confirmando = false;
	ler_vivo();
}

static bool igual(const struct ed *a, const struct ed *b) {
	return a->ligada == b->ligada && a->l == b->l && a->a == b->a && a->hz == b->hz && a->escala_i == b->escala_i && a->rot_i == b->rot_i &&
		a->ref == b->ref && a->lado == b->lado;
}

bool pn_monitores_pendente(void) {
	for (int i = 0; i < vivo.n; i++) {
		if (!igual(&e[i], &g[i])) {
			return true;
		}
	}
	return false;
}

void pn_monitores_descartar(void) {
	memcpy(e, g, sizeof(e));
	pos_mexida = false;
}

void pn_monitores_restaurar(void) {
	for (int i = 0; i < vivo.n; i++) {
		const struct tela *t = &vivo.v[i];
		int melhor = -1;
		/* o maior modo, e dele a maior taxa */
		for (int k = 0; k < t->nmodos; k++) {
			const struct tela_modo *m = &t->modos[k];
			if (melhor < 0 || (long)m->l * m->a > (long)t->modos[melhor].l * t->modos[melhor].a ||
				((long)m->l * m->a == (long)t->modos[melhor].l * t->modos[melhor].a && m->hz > t->modos[melhor].hz)) {
				melhor = k;
			}
		}
		if (melhor >= 0) {
			e[i].l = t->modos[melhor].l;
			e[i].a = t->modos[melhor].a;
			e[i].hz = t->modos[melhor].hz;
		}
		e[i].ligada = true;
		e[i].escala_i = 0;
		e[i].rot_i = 0;
	}
}

/* ---- as posições ---- */

struct caixa {
	int x, y, w, h;
	bool ok;
};

static void tamanho_ed(int i, int *w, int *h) {
	struct tela t = vivo.v[i];
	t.largura = e[i].l;
	t.altura = e[i].a;
	t.escala = escalas[e[i].escala_i];
	t.rotacao = e[i].rot_i * 90;
	tela_tamanho_logico(&t, w, h);
}

static bool posicionar(int i, struct caixa *c, int profundidade) {
	if (c[i].ok) {
		return true;
	}
	if (profundidade > TELAS_MAX) {
		return false;
	}
	int w, h;
	tamanho_ed(i, &w, &h);
	c[i].w = w;
	c[i].h = h;
	int ancora = primeira_ligada();
	if (i == ancora) {
		c[i].x = vivo.v[i].x;
		c[i].y = vivo.v[i].y;
	} else {
		int r = e[i].ref;
		if (r == i || r < 0 || r >= vivo.n || !e[r].ligada || !posicionar(r, c, profundidade + 1)) {
			c[i].x = vivo.v[i].x;
			c[i].y = vivo.v[i].y;
		} else {
			switch (e[i].lado) {
			case 0: c[i].x = c[r].x + c[r].w; c[i].y = c[r].y; break;
			case 1: c[i].x = c[r].x - w; c[i].y = c[r].y; break;
			case 2: c[i].x = c[r].x; c[i].y = c[r].y - h; break;
			default: c[i].x = c[r].x; c[i].y = c[r].y + c[r].h; break;
			}
		}
	}
	c[i].ok = true;
	return true;
}

/* o desenho (e, no Aplicar, as posições): o de agora se ninguém mexeu, senão o que as relações dão */
static bool tamanhos_mudaram(void) {
	for (int i = 0; i < vivo.n; i++) {
		int w, h;
		tamanho_ed(i, &w, &h);
		int vw, vh;
		tela_tamanho_logico(&vivo.v[i], &vw, &vh);
		if (e[i].ligada != vivo.v[i].ligada || w != vw || h != vh) {
			return true;
		}
	}
	return false;
}

static void caixas(struct caixa *c) {
	memset(c, 0, sizeof(struct caixa) * TELAS_MAX);
	bool recalcular = pos_mexida || tamanhos_mudaram();
	for (int i = 0; i < vivo.n; i++) {
		if (!e[i].ligada) {
			continue;
		}
		if (recalcular) {
			posicionar(i, c, 0);
		} else {
			c[i].x = vivo.v[i].x;
			c[i].y = vivo.v[i].y;
			tela_tamanho_logico(&vivo.v[i], &c[i].w, &c[i].h);
			c[i].ok = true;
		}
	}
	if (!recalcular) {
		return;
	}
	/* tudo a partir de 0,0 */
	int mx = 1 << 30, my = 1 << 30;
	for (int i = 0; i < vivo.n; i++) {
		if (e[i].ligada && c[i].ok) {
			mx = MIN(mx, c[i].x);
			my = MIN(my, c[i].y);
		}
	}
	for (int i = 0; i < vivo.n; i++) {
		if (e[i].ligada && c[i].ok) {
			c[i].x -= mx;
			c[i].y -= my;
		}
	}
}

/* ---- aplicar e desfazer ---- */

static bool arriscado(void) {
	for (int i = 0; i < vivo.n; i++) {
		const struct ed *a = &e[i], *b = &g[i];
		if (a->ligada != b->ligada || a->l != b->l || a->a != b->a || a->hz != b->hz || a->escala_i != b->escala_i || a->rot_i != b->rot_i) {
			return true;
		}
	}
	return false;
}

void pn_monitores_aplicar(void) {
	struct tela_cfg novo[TELAS_MAX];
	struct caixa c[TELAS_MAX];
	caixas(c);
	bool com_pos = pos_mexida || tamanhos_mudaram();
	bool risco = arriscado();
	nantes = vivo.n;
	for (int i = 0; i < vivo.n; i++) {
		telas_cfg_de_estado(&vivo.v[i], &antes[i]);
		novo[i] = (struct tela_cfg){ .ligada = e[i].ligada, .l = e[i].l, .a = e[i].a, .hz = e[i].hz, .escala = escalas[e[i].escala_i], .rotacao = e[i].rot_i * 90 };
		g_strlcpy(novo[i].nome, vivo.v[i].nome, sizeof(novo[i].nome));
		if (com_pos && e[i].ligada) {
			novo[i].pos = true;
			novo[i].x = c[i].x;
			novo[i].y = c[i].y;
		}
	}
	if (!telas_cfg_gravar(novo, vivo.n)) {
		g_strlcpy(mensagem, "Não consegui gravar a configuração.", sizeof(mensagem));
		return;
	}
	mensagem[0] = '\0';
	reler_em = gv_agora() + 1.0;
	if (risco) {
		confirmando = true;
		ate = gv_agora() + 15;
	}
	memcpy(g, e, sizeof(g));
	pos_mexida = false;
}

static void reverter(void) {
	if (nantes > 0) {
		telas_cfg_gravar(antes, nantes);
	}
	confirmando = false;
	reler_em = gv_agora() + 1.0;
	g_strlcpy(mensagem, "Voltei ao que estava.", sizeof(mensagem));
}

/* a cada quadro (de painel.c): o relógio do "Manter?" e a releitura depois de aplicar */
void pn_monitores_tick(bool aqui) {
	double agora = gv_agora();
	if (confirmando) {
		if (!aqui) {
			confirmando = false; /* você está vendo o Painel: a imagem está boa */
		} else if (agora >= ate) {
			reverter();
		} else {
			gv_acordar_em(0.5);
		}
	}
	if (reler_em > 0 && agora >= reler_em) {
		reler_em = 0;
		bool pend = pn_monitores_pendente();
		struct ed guardado[TELAS_MAX];
		memcpy(guardado, e, sizeof(e));
		ler_vivo();
		if (pend) {
			memcpy(e, guardado, sizeof(e));
		}
		if (aqui) {
			gv_app_redesenhar(pn_app);
		}
	} else if (reler_em > 0) {
		gv_acordar_em(reler_em - agora + 0.02);
	}
}

/* ---- a seção ---- */

static const char *nome_da_tela(int i, char *buf, size_t tam) {
	const struct tela *t = &vivo.v[i];
	if (t->modelo[0]) {
		g_snprintf(buf, tam, "%s%s%s", t->fabricante, t->fabricante[0] ? " " : "", t->modelo);
	} else {
		g_strlcpy(buf, t->nome, tam);
	}
	return buf;
}

static void diagrama(void) {
	struct caixa c[TELAS_MAX];
	caixas(c);
	int maxx = 1, maxy = 1;
	for (int i = 0; i < vivo.n; i++) {
		if (e[i].ligada && c[i].ok) {
			maxx = MAX(maxx, c[i].x + c[i].w);
			maxy = MAX(maxy, c[i].y + c[i].h);
		}
	}
	const float area_w = 640, area_h = 170;
	float k = MIN(area_w / (float)maxx, area_h / (float)maxy);
	CLAY(CLAY_ID("mo-diag"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(area_h + 28) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		CLAY(CLAY_ID("mo-diag-i"), { .layout = { .sizing = { CLAY_SIZING_FIXED(maxx * k), CLAY_SIZING_FIXED(maxy * k) } } }) {
			for (int i = 0; i < vivo.n; i++) {
				if (!e[i].ligada || !c[i].ok) {
					continue;
				}
				Clay_ElementId id = CLAY_IDI("mo-tela", (uint32_t)i);
				if (gv_tocado(id)) {
					sel = i;
				}
				bool s = i == sel;
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(c[i].w * k - 4), CLAY_SIZING_FIXED(c[i].h * k - 4) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
						.layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2 },
					.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .clipTo = CLAY_CLIP_TO_ATTACHED_PARENT, .offset = { c[i].x * k + 2, c[i].y * k + 2 } },
					.backgroundColor = s ? gv_alfa(gv_t.acento, 0.28f) : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.superficie,
					.cornerRadius = CLAY_CORNER_RADIUS(6), .border = { .color = s ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(s ? 2 : 1) } }) {
					gv_focavel(id, 6);
					gv_texto(gv_fmt("%d", i + 1), 22, s ? gv_t.acento : gv_t.texto, GV_PESO_MEDIO);
					if (c[i].w * k > 90) {
						char buf[96];
						gv_texto(gv_str(nome_da_tela(i, buf, sizeof(buf))), gv_t.tamanho_corpo - 3, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
				}
			}
		}
	}
}

/* as resoluções (maior primeiro) que a tela oferece */
static int resolucoes(const struct tela *t, struct tela_modo *r, int max) {
	int n = 0;
	for (int k = 0; k < t->nmodos; k++) {
		bool tem = false;
		for (int j = 0; j < n; j++) {
			tem |= r[j].l == t->modos[k].l && r[j].a == t->modos[k].a;
		}
		if (!tem && n < max) {
			r[n++] = t->modos[k];
		}
	}
	for (int i = 1; i < n; i++) { /* inserção: poucas */
		struct tela_modo m = r[i];
		int j = i - 1;
		while (j >= 0 && (long)r[j].l * r[j].a < (long)m.l * m.a) {
			r[j + 1] = r[j];
			j--;
		}
		r[j + 1] = m;
	}
	return n;
}

void pn_monitores_desenhar(void) {
	if (!lido) {
		ler_vivo();
	}
	if (!vivo.orbit || vivo.n == 0) {
		gv_texto(CLAY_STRING("O Orbit não respondeu: sem ele não há como ver nem mudar as telas."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	if (confirmando) {
		int resta = (int)ceil(ate - gv_agora());
		/* flutua por cima de tudo, no pé da janela: dá para rolar a seção e ele continua à vista */
		CLAY(CLAY_ID("mo-manter"), { .layout = { .sizing = { CLAY_SIZING_FIXED(720), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 50, .offset = { 0, -96 },
				.attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_BOTTOM, .parent = CLAY_ATTACH_POINT_CENTER_BOTTOM } },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.aviso, .width = GV_BORDA(2) } }) {
			CLAY(CLAY_ID("mo-manter-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_fmt("Manter estas configurações? Volta sozinho em %d s se você não responder.", resta), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao(CLAY_ID("mo-reverter"), NULL, "Reverter", GV_BOTAO_ELEVADO)) {
				reverter();
			}
			if (gv_botao(CLAY_ID("mo-manter-bt"), "check", "Manter", GV_BOTAO_CHEIO)) {
				confirmando = false;
			}
		}
	}
	if (mensagem[0]) {
		gv_texto(gv_str(mensagem), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	diagrama();
	if (vivo.n > 1) {
		/* para telas desligadas, que o desenho não mostra */
		for (int i = 0; i < vivo.n; i++) {
			if (!e[i].ligada) {
				char buf[96];
				Clay_ElementId id = CLAY_IDI("mo-off", (uint32_t)i);
				if (gv_botao(id, NULL, gv_fmt("%d · %s (desligada)", i + 1, nome_da_tela(i, buf, sizeof(buf))).chars, GV_BOTAO_SIMPLES)) {
					sel = i;
				}
			}
		}
	}
	struct ed *d = &e[sel];
	const struct tela *t = &vivo.v[sel];
	char nome[96];
	nome_da_tela(sel, nome, sizeof(nome));
	pn_subtitulo(CLAY_ID("mo-sub"), gv_fmt("Tela %d · %s%s%s", sel + 1, nome, t->mm_l > 0 ? " · " : "", t->mm_l > 0 ? tela_polegadas(t) : "").chars);

	int ligadas = 0;
	for (int i = 0; i < vivo.n; i++) {
		ligadas += e[i].ligada;
	}
	bool pode_desligar = t->desligavel && (ligadas > 1 || !d->ligada);
	pn_linha_comecar(CLAY_ID("mo-lig"), "screens", "Ligada",
		!t->desligavel ? "Esta tela é uma janela dentro de outro sistema: não dá para desligar" : ligadas <= 1 && d->ligada ? "É a única tela ligada" : "Uma tela desligada deixa suas janelas na outra");
	{
		bool v = d->ligada;
		if (pode_desligar && gv_interruptor(CLAY_ID("mo-lig-v"), &v)) {
			d->ligada = v;
			pos_mexida = true;
		} else if (!pode_desligar) {
			gv_interruptor(CLAY_ID("mo-lig-v0"), &v);
		}
	}
	pn_linha_acabar();
	if (!d->ligada) {
		return;
	}

	struct tela_modo res[48];
	int nres = resolucoes(t, res, 48);
	if (nres > 0) {
		const char *rnomes[48];
		int ri = 0;
		for (int i = 0; i < nres; i++) {
			rnomes[i] = gv_fmt("%d × %d", res[i].l, res[i].a).chars;
			if (res[i].l == d->l && res[i].a == d->a) {
				ri = i;
			}
		}
		if (pn_ajuste_lista(CLAY_ID("mo-res"), "screens", "Resolução", "A maior é a melhor para a tela", rnomes, nres, &ri)) {
			d->l = res[ri].l;
			d->a = res[ri].a;
			/* a taxa mais perto da de antes */
			int melhor = -1;
			for (int k = 0; k < t->nmodos; k++) {
				if (t->modos[k].l == d->l && t->modos[k].a == d->a && (melhor < 0 || abs(t->modos[k].hz - d->hz) < abs(t->modos[melhor].hz - d->hz))) {
					melhor = k;
				}
			}
			if (melhor >= 0) {
				d->hz = t->modos[melhor].hz;
			}
		}
		const char *tnomes[32];
		int taxas[32], nt = 0, ti = 0;
		for (int k = 0; k < t->nmodos && nt < 32; k++) {
			if (t->modos[k].l == d->l && t->modos[k].a == d->a) {
				taxas[nt] = t->modos[k].hz;
				tnomes[nt] = gv_fmt("%d Hz", t->modos[k].hz).chars;
				if (t->modos[k].hz == d->hz) {
					ti = nt;
				}
				nt++;
			}
		}
		if (nt > 1 && pn_ajuste_lista(CLAY_ID("mo-taxa"), "clock", "Taxa de atualização", "Mais Hz deixa tudo mais suave e gasta mais energia", tnomes, nt, &ti)) {
			d->hz = taxas[ti];
		}
	} else {
		pn_linha_comecar(CLAY_ID("mo-res0"), "screens", "Resolução", "Esta tela não oferece outras resoluções");
		gv_texto(gv_fmt("%d × %d", d->l, d->a), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		pn_linha_acabar();
	}
	if (pn_ajuste_lista(CLAY_ID("mo-esc"), "search", "Escala", "O tamanho de tudo na tela", escala_nomes, NESCALAS, &d->escala_i)) {
		/* a tela muda de tamanho lógico: as vizinhas se ajustam */
	}
	pn_ajuste_lista(CLAY_ID("mo-rot"), "refresh", "Rotação", "Para uma tela em pé ou de cabeça para baixo", rot_nomes, 4, &d->rot_i);

	if (vivo.n > 1 && sel != primeira_ligada()) {
		const char *outras[TELAS_MAX];
		int idx[TELAS_MAX], no = 0, oi = 0;
		for (int i = 0; i < vivo.n; i++) {
			if (i != sel && e[i].ligada) {
				idx[no] = i;
				outras[no] = gv_fmt("Tela %d", i + 1).chars;
				if (i == d->ref) {
					oi = no;
				}
				no++;
			}
		}
		pn_subtitulo(CLAY_ID("mo-pos"), "Posição");
		if (pn_ajuste_lista(CLAY_ID("mo-lado"), "screens", "Esta tela fica", "Em relação a uma outra", lado_nomes, 4, &d->lado)) {
			pos_mexida = true;
		}
		if (no > 0 && pn_ajuste_lista(CLAY_ID("mo-ref"), "screens", "Em relação a", NULL, outras, no, &oi)) {
			d->ref = idx[oi];
			pos_mexida = true;
		}
	}
}
