/*
 * lumna-teclado: o teclado na tela do LUMNA OS. Uma camada (layer-shell) colada embaixo que sobe empurrando as janelas (zona exclusiva) quando
 * um campo de texto ganha o foco e a pessoa está no toque ou sem teclado físico (ajustável no Painel > Teclado na tela).
 *
 *   - Método de entrada (servicos/teclado_im.c): o Orbit avisa o campo; o texto volta pronto. Enter, Tab, Esc e setas vão por um teclado virtual.
 *   - Layout (servicos/teclado_layout.c): o da língua de quem usa, com acentos por toque longo, Shift, símbolos e emojis.
 *   - Vários dedos ao mesmo tempo (gv_pontos): digita com os dois polegares; o mouse faz o mesmo com o clique.
 *
 *   lumna-teclado            sobe com a sessão
 *   lumna-teclado alternar   mostra ou esconde (um botão, um atalho): manda um sinal ao que está rodando
 *   lumna-teclado esconder
 *
 * Em teste (LUMNA_TECLADO_DEBUG=1) escreve no stdout o que acontece: "mostrar", "esconder", "tecla X Y W H" (as caixas, em pixels da camada), "texto ...".
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <fcntl.h>
#include <glib.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gravity.h"
#include "modos.h"
#include "teclado_cfg.h"
#include "teclado_im.h"
#include "teclado_layout.h"

#define MAX_DEDOS 10
#define FOLGA 6.0f
#define LARGURA_MAX 1100.0f   /* numa tela larga o teclado não passa disto (centralizado) */
#define TEMPO_SEGURAR 0.40
#define TEMPO_REPETIR 0.45
#define INTERVALO_REPETIR 0.06

static gv_app *app_teclado;
static ti_cliente *im;
static tcfg cfg;
static tl_estado est;
static tl_pagina pag;
static tl_ret rets[TL_MAX_TECLAS];
static bool pag_suja = true;
static bool debug;

static bool visivel;          /* o teclado está à vista (ou subindo) */
static bool camada_grande;    /* a camada está no tamanho do teclado (senão 1 px) */
static bool manual;           /* aberto à mão (atalho): não fecha quando o campo some */
static bool dispensado;       /* a pessoa fechou (⌄) enquanto este campo está aberto */
static bool campo_ativo;
static double esconder_em;    /* o campo sumiu: fecha daqui a pouco, se não voltar (trocar de campo desativa e ativa) */
static float altura_camada;
static float largura_tela;    /* a largura da camada inteira, medida enquanto está a toda a largura */
static bool fisico = true;    /* há teclado físico */
static int pipe_sinal[2] = { -1, -1 };

typedef struct dedo {
	bool usado;
	int id;
	int tecla;            /* a tecla sob o dedo (índice na página), -1 nenhuma */
	double desde;         /* quando começou nesta tecla */
	bool abriu_alts;      /* o toque longo abriu as variantes */
	int alt_sel;          /* a variante sob o dedo, -1 nenhuma */
	bool repetindo;
	double proxima_repeticao;
	bool agiu;            /* já fez o que tinha de fazer (teclas que agem ao descer) */
} dedo;
static dedo dedos[MAX_DEDOS];

static void log_debug(const char *formato, ...) __attribute__((format(printf, 1, 2)));
static void log_debug(const char *formato, ...) {
	if (!debug) {
		return;
	}
	va_list ap;
	va_start(ap, formato);
	vprintf(formato, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

/* ---- o que a página é agora ---- */

static float altura_linha(void) {
	return (largura_tela >= 1000 ? 50.0f : 58.0f) * tcfg_escala(&cfg);
}

static float altura_total(void) {
	return 5 * altura_linha() + 6 * FOLGA;
}

static void montar_pagina(float w, float h) {
	est.largo = w >= 760;
	tl_montar(&est, &pag);
	tl_geometria(&pag, w, h, FOLGA, rets);
	pag_suja = false;
	if (debug) {
		for (int i = 0; i < pag.total; i++) {
			const char *nome = pag.teclas[i].tipo == TL_ESPACO ? "espaco" : pag.teclas[i].texto[0] ? pag.teclas[i].texto : pag.teclas[i].rotulo;
			log_debug("tecla %s %.0f %.0f %.0f %.0f", nome, rets[i].x, rets[i].y, rets[i].w, rets[i].h);
		}
	}
}

/* ---- mostrar e esconder ---- */

static void camada_ajustar(bool grande) {
	if (grande == camada_grande && altura_camada > 0) {
		return;
	}
	camada_grande = grande;
	altura_camada = grande ? altura_total() : 1;
	gv_app_camada(app_teclado, 0, (int)altura_camada, grande ? (int)altura_camada : 0);
}

static void mostrar(bool por_atalho) {
	esconder_em = 0;
	if (visivel) {
		return;
	}
	visivel = true;
	manual = por_atalho;
	camada_ajustar(true);
	log_debug("mostrar");
	gv_app_redesenhar(app_teclado);
}

static void esconder(void) {
	esconder_em = 0;
	if (!visivel) {
		return;
	}
	visivel = false;
	manual = false;
	/* as janelas voltam a ocupar a tela na hora; o teclado desce deslizando e só então a camada encolhe */
	gv_app_camada(app_teclado, 0, (int)altura_camada, 0);
	log_debug("esconder");
	gv_app_redesenhar(app_teclado);
}

/* Orbit: o que ele diz da entrada (teclado físico, última entrada, layout). */
static void orbit_entrada(bool *tem_fisico, bool *toque, int *layout) {
	char r[16384];
	*tem_fisico = true;
	*toque = false;
	*layout = est.layout;
	if (!orbit_pedido("entrada", r, sizeof(r)) || !strstr(r, "\"ok\":true")) {
		return;
	}
	*tem_fisico = strstr(r, "\"teclado_fisico\":true") != NULL;
	*toque = strstr(r, "\"ultima_entrada\":\"toque\"") != NULL;
	char lay[32] = "", var[32] = "";
	const char *l = strstr(r, "\"layout\":\"");
	if (l) {
		sscanf(l + 10, "%31[^\"]", lay);
	}
	const char *v = strstr(r, "\"variante\":\"");
	if (v) {
		sscanf(v + 12, "%31[^\"]", var);
	}
	*layout = tl_layout_do_orbit(lay, var);
}

static bool deve_abrir_sozinho(void) {
	bool tem_fisico, toque;
	int layout;
	orbit_entrada(&tem_fisico, &toque, &layout);
	fisico = tem_fisico;
	if (!strcmp(cfg.modo, "sempre")) {
		return true;
	}
	if (!strcmp(cfg.modo, "nunca")) {
		return false;
	}
	return !tem_fisico || toque;
}

/* ---- o campo (vindo do método de entrada) ---- */

static void ao_campo(void *dados, const ti_campo *c) {
	bool entrou = c->ativo && !campo_ativo;
	campo_ativo = c->ativo;
	if (!c->ativo) {
		if (!manual) {
			esconder_em = gv_agora() + 0.3;
			gv_acordar_em(0.35);
		}
		dispensado = false;
		return;
	}
	esconder_em = 0;
	if (entrou) {
		tcfg_ler(&cfg); /* o Painel pode ter mudado o modo ou o tamanho */
		bool tem_fisico, toque;
		int layout;
		orbit_entrada(&tem_fisico, &toque, &layout);
		est.layout = layout;
		/* números e telefone abrem nos símbolos; o resto, nas letras */
		est.pagina = c->proposito == TI_DIGITOS || c->proposito == TI_NUMERO || c->proposito == TI_TELEFONE || c->proposito == TI_PIN ||
				c->proposito == TI_DATA || c->proposito == TI_HORA || c->proposito == TI_DATAHORA ? TL_SIMB1 : TL_LETRAS;
		est.shift = TL_SHIFT_NAO;
		pag_suja = true;
		log_debug("campo proposito=%u dica=%u", c->proposito, c->dica);
		if (!dispensado && deve_abrir_sozinho()) {
			mostrar(false);
		}
	}
	/* a primeira letra da frase é maiúscula, sozinha */
	if ((c->dica & TI_DICA_CAPITALIZAR || (c->proposito == TI_NORMAL && !(c->dica & (TI_DICA_ESCONDIDO | TI_DICA_SENSIVEL)))) &&
			est.pagina == TL_LETRAS && est.shift == TL_SHIFT_NAO && tl_capitalizar_agora(c->antes)) {
		est.shift = TL_SHIFT_UMA;
		pag_suja = true;
	}
	gv_app_redesenhar(app_teclado);
}

static void ao_indisponivel(void *dados) {
	fprintf(stderr, "lumna-teclado: já há outro método de entrada ligado\n");
}

static void im_pronto(gv_app *app, int fd, void *dados) {
	if (!im) {
		return;
	}
	if (!ti_despachar(im)) {
		fprintf(stderr, "lumna-teclado: a ligação com o compositor caiu\n");
		gv_app_sair(app);
	}
}

/* ---- sinais: alternar e esconder à mão ---- */

static void sinal(int s) {
	char c = s == SIGUSR1 ? 'a' : 'e';
	if (pipe_sinal[1] >= 0 && write(pipe_sinal[1], &c, 1) < 0) {
		/* nada a fazer */
	}
}

static void sinal_pronto(gv_app *app, int fd, void *dados) {
	char c;
	while (read(fd, &c, 1) > 0) {
		if (c == 'a') {
			if (visivel) {
				dispensado = true;
				esconder();
			} else {
				dispensado = false;
				mostrar(true);
			}
		} else {
			dispensado = true;
			esconder();
		}
	}
}

static char *caminho_pid(void) {
	const char *rt = g_get_user_runtime_dir();
	return g_build_filename(rt && *rt ? rt : g_get_tmp_dir(), "lumna-teclado.pid", NULL);
}

static int comando_remoto(const char *cmd) {
	char *caminho = caminho_pid();
	char *txt = NULL;
	int ret = 1;
	if (g_file_get_contents(caminho, &txt, NULL, NULL)) {
		pid_t pid = (pid_t)atoi(txt);
		if (pid > 1 && kill(pid, !strcmp(cmd, "alternar") ? SIGUSR1 : SIGUSR2) == 0) {
			ret = 0;
		}
	}
	if (ret) {
		fprintf(stderr, "lumna-teclado: não está rodando\n");
	}
	g_free(txt);
	g_free(caminho);
	return ret;
}

/* ---- as ações das teclas ---- */

static uint32_t tecla_da_seta(enum tl_seta s) {
	switch (s) {
	case TL_ESQ: return TI_KEY_LEFT;
	case TL_DIR: return TI_KEY_RIGHT;
	case TL_CIMA: return TI_KEY_UP;
	default: return TI_KEY_DOWN;
	}
}

static void executar(tl_acao a) {
	switch (a.resultado) {
	case TL_TEXTO:
		if (im) {
			ti_texto(im, a.texto);
		}
		log_debug("texto %s", a.texto);
		if (est.pagina == TL_EMOJI) {
			tcfg_recentes_gravar();
		}
		break;
	case TL_APAGAR_UM:
		if (im) {
			ti_apagar(im);
		}
		log_debug("apagar");
		break;
	case TL_ENTER_: if (im) { ti_tecla(im, TI_KEY_ENTER); } log_debug("enter"); break;
	case TL_TAB_: if (im) { ti_tecla(im, TI_KEY_TAB); } log_debug("tab"); break;
	case TL_ESC_: if (im) { ti_tecla(im, TI_KEY_ESC); } log_debug("esc"); break;
	case TL_SETA_: if (im) { ti_tecla(im, tecla_da_seta(a.seta)); } log_debug("seta %d", (int)a.seta); break;
	case TL_ESCONDER:
		dispensado = true;
		esconder();
		break;
	case TL_REDESENHAR:
	case TL_NADA:
		break;
	}
	pag_suja = true;
}

static void tocar_tecla(int i) {
	if (i < 0 || i >= pag.total) {
		return;
	}
	tl_tecla copia = pag.teclas[i];
	executar(tl_tocar(&est, &copia));
}

/* uma variante escolhida no toque longo: escreve e solta a maiúscula de uma letra */
static void escrever_variante(const char *texto) {
	tl_acao a = { TL_TEXTO, "", TL_ESQ };
	snprintf(a.texto, sizeof(a.texto), "%s", texto);
	if (est.shift == TL_SHIFT_UMA) {
		est.shift = TL_SHIFT_NAO;
	}
	executar(a);
}

/* ---- as variantes de uma tecla segurada ---- */

static int n_alts(const tl_tecla *t) {
	int n = 0;
	for (const char *p = t->alts; *p; n++) {
		while (*p && *p != ' ') {
			p++;
		}
		while (*p == ' ') {
			p++;
		}
	}
	return n;
}

static void alt_n(const tl_tecla *t, int n, char *saida, size_t tam) {
	saida[0] = '\0';
	const char *p = t->alts;
	for (int i = 0; *p; i++) {
		const char *fim = strchr(p, ' ');
		size_t len = fim ? (size_t)(fim - p) : strlen(p);
		if (i == n) {
			snprintf(saida, tam, "%.*s", (int)len, p);
			return;
		}
		p += len;
		while (*p == ' ') {
			p++;
		}
	}
}

/* a caixa de cada variante (acima da tecla, presa dentro do teclado) */
static tl_ret caixa_alt(int tecla, int n, int total, float larg_total) {
	const tl_ret r = rets[tecla];
	float w = r.w < 46 ? 46 : r.w;
	float tw = w * (float)total;
	float x0 = r.x + r.w / 2 - tw / 2;
	if (x0 < 4) {
		x0 = 4;
	}
	if (x0 + tw > larg_total - 4) {
		x0 = larg_total - 4 - tw;
	}
	float y = r.y - r.h - 8;
	if (y < 4) {
		y = 4;
	}
	return (tl_ret){ x0 + w * (float)n, y, w, r.h };
}

/* ---- o toque ---- */

static dedo *dedo_de(int id, bool criar) {
	for (int i = 0; i < MAX_DEDOS; i++) {
		if (dedos[i].usado && dedos[i].id == id) {
			return &dedos[i];
		}
	}
	if (!criar) {
		return NULL;
	}
	for (int i = 0; i < MAX_DEDOS; i++) {
		if (!dedos[i].usado) {
			dedos[i] = (dedo){ .usado = true, .id = id, .tecla = -1, .alt_sel = -1 };
			return &dedos[i];
		}
	}
	return NULL;
}

static bool repete(const tl_tecla *t) {
	return t->tipo == TL_APAGAR || t->tipo == TL_SETA;
}

static void processar_toque(Clay_BoundingBox cx) {
	gv_ponto pts[MAX_DEDOS];
	int n = gv_pontos(pts, MAX_DEDOS);
	double agora = gv_agora();
	bool algum = false;
	for (int k = 0; k < n; k++) {
		float x = pts[k].x - cx.x, y = pts[k].y - cx.y;
		dedo *d = dedo_de(pts[k].id, pts[k].desceu);
		if (!d) {
			continue;
		}
		int sob = tl_tecla_em(rets, pag.total, x, y, FOLGA);
		if (pts[k].desceu) {
			d->tecla = sob;
			d->desde = agora;
			d->abriu_alts = false;
			d->alt_sel = -1;
			d->repetindo = false;
			d->agiu = false;
			if (sob >= 0 && repete(&pag.teclas[sob])) {
				tocar_tecla(sob); /* apagar e setas agem ao descer, e repetem se o dedo ficar */
				d->agiu = true;
				d->proxima_repeticao = agora + TEMPO_REPETIR;
			}
		} else if (!pts[k].subiu) {
			if (d->abriu_alts && d->tecla >= 0) {
				const tl_tecla *t = &pag.teclas[d->tecla];
				int total = n_alts(t);
				d->alt_sel = -1;
				for (int a = 0; a < total; a++) {
					tl_ret c = caixa_alt(d->tecla, a, total, cx.width);
					if (x >= c.x && x <= c.x + c.w && y >= c.y - 12 && y <= c.y + c.h + 12) {
						d->alt_sel = a;
					}
				}
			} else if (sob != d->tecla && sob >= 0 && !d->agiu) {
				d->tecla = sob; /* o dedo escorregou para a tecla do lado: vale a nova */
				d->desde = agora;
			}
		}
		if (d->tecla >= 0 && !pts[k].subiu) {
			const tl_tecla *t = &pag.teclas[d->tecla];
			if (!d->abriu_alts && !d->agiu && t->alts[0] && agora - d->desde >= TEMPO_SEGURAR) {
				d->abriu_alts = true;
				log_debug("variantes %s", t->alts);
				if (debug) {
					for (int a = 0; a < n_alts(t); a++) {
						tl_ret c = caixa_alt(d->tecla, a, n_alts(t), cx.width);
						char alt[16];
						alt_n(t, a, alt, sizeof(alt));
						log_debug("variante %s %.0f %.0f %.0f %.0f", alt, c.x, c.y, c.w, c.h);
					}
				}
			}
			if (d->agiu && repete(t) && agora >= d->proxima_repeticao) {
				tocar_tecla(d->tecla);
				d->proxima_repeticao = agora + INTERVALO_REPETIR;
			}
			algum = true;
		}
		if (pts[k].subiu) {
			if (d->abriu_alts && d->tecla >= 0) {
				char alt[16];
				alt_n(&pag.teclas[d->tecla], d->alt_sel, alt, sizeof(alt));
				if (d->alt_sel >= 0 && alt[0]) {
					escrever_variante(alt);
				} else {
					tocar_tecla(d->tecla); /* soltou sem escolher: vale a tecla mesmo */
				}
			} else if (d->tecla >= 0 && !d->agiu) {
				tocar_tecla(d->tecla);
			}
			d->usado = false;
		}
	}
	/* dedos que sumiram sem subir (cancelados) */
	for (int i = 0; i < MAX_DEDOS; i++) {
		if (!dedos[i].usado) {
			continue;
		}
		bool existe = false;
		for (int k = 0; k < n; k++) {
			existe |= pts[k].id == dedos[i].id;
		}
		if (!existe) {
			dedos[i].usado = false;
		}
	}
	if (algum) {
		gv_acordar_em(0.05); /* o toque longo e a repetição andam com o tempo */
		gv_app_redesenhar(app_teclado);
	}
}

/* ---- desenho ---- */

static void cor(cairo_t *cr, Clay_Color c, double a) {
	cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0, a * c.a / 255.0);
}

static void ret_redondo(cairo_t *cr, double x, double y, double w, double h, double r) {
	if (r > h / 2) {
		r = h / 2;
	}
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

static void texto_centrado(cairo_t *cr, const char *texto, double cx, double cy, double tamanho, Clay_Color c) {
	PangoLayout *l = pango_cairo_create_layout(cr);
	char fonte[96];
	snprintf(fonte, sizeof(fonte), "%s %.1f", gv_t.fonte, tamanho * 0.75);
	PangoFontDescription *fd = pango_font_description_from_string(fonte);
	pango_layout_set_font_description(l, fd);
	pango_font_description_free(fd);
	pango_layout_set_text(l, texto, -1);
	int w, h;
	pango_layout_get_pixel_size(l, &w, &h);
	cairo_move_to(cr, cx - w / 2.0, cy - h / 2.0);
	cor(cr, c, 1);
	pango_cairo_show_layout(cr, l);
	g_object_unref(l);
}

static bool tecla_premida(int i) {
	for (int k = 0; k < MAX_DEDOS; k++) {
		if (dedos[k].usado && dedos[k].tecla == i && !dedos[k].abriu_alts) {
			return true;
		}
	}
	return false;
}

static void desenhar_teclado(cairo_t *cr, Clay_BoundingBox caixa, void *dados) {
	float pos = *(float *)dados; /* 0 embaixo, fora da tela; 1 no lugar */
	cairo_save(cr);
	cairo_translate(cr, caixa.x, caixa.y + (1 - pos) * caixa.height);
	/* o fundo */
	cairo_save(cr);
	ret_redondo(cr, 0, 0, caixa.width, caixa.height + 40, gv_t.raio_grande);
	cor(cr, gv_t.superficie, 1);
	cairo_fill_preserve(cr);
	cor(cr, gv_t.borda, 1);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	cairo_restore(cr);

	if (pag_suja) {
		montar_pagina(caixa.width, caixa.height);
	}
	double ft = rets[0].h;
	for (int i = 0; i < pag.total; i++) {
		const tl_tecla *t = &pag.teclas[i];
		if (t->tipo == TL_CHAR && !t->texto[0]) {
			continue; /* um lugar vazio da grade de emojis */
		}
		bool premida = tecla_premida(i);
		bool especial = t->tipo != TL_CHAR && t->tipo != TL_ESPACO;
		Clay_Color fundo = premida ? gv_t.acento : t->ativa ? gv_t.selecionado : especial ? gv_t.sob_cursor : gv_t.elevada;
		ret_redondo(cr, rets[i].x, rets[i].y, rets[i].w, rets[i].h, gv_t.raio_pequeno + 2);
		cor(cr, fundo, 1);
		cairo_fill(cr);
		Clay_Color cor_texto = premida ? gv_t.sobre_acento : t->ativa ? gv_t.acento : gv_t.texto;
		double cx = rets[i].x + rets[i].w / 2, cy = rets[i].y + rets[i].h / 2;
		if (t->tipo == TL_ESPACO) {
			texto_centrado(cr, tl_layout_nome(est.layout), cx, cy, ft * 0.28, gv_t.texto_suave);
		} else if (t->rotulo[0]) {
			bool emoji = est.pagina == TL_EMOJI || t->tipo == TL_CATEGORIA || (t->tipo == TL_PAGINA && t->destino == TL_EMOJI);
			double tam = emoji ? ft * 0.46 : t->tipo == TL_CHAR ? ft * 0.46 : ft * 0.34;
			texto_centrado(cr, t->rotulo, cx, cy, tam, cor_texto);
		}
		if (t->alts[0] && t->tipo == TL_CHAR) {
			cairo_arc(cr, rets[i].x + rets[i].w - 7, rets[i].y + 7, 1.8, 0, 2 * M_PI); /* um pontinho: tem variantes */
			cor(cr, gv_t.texto_suave, 0.8);
			cairo_fill(cr);
		}
		if (t->tipo == TL_SHIFT && t->destino) { /* travado */
			cairo_rectangle(cr, rets[i].x + rets[i].w * 0.3, rets[i].y + rets[i].h - 9, rets[i].w * 0.4, 3);
			cor(cr, gv_t.acento, 1);
			cairo_fill(cr);
		}
	}
	/* as variantes abertas, por cima de tudo */
	for (int k = 0; k < MAX_DEDOS; k++) {
		if (!dedos[k].usado || !dedos[k].abriu_alts || dedos[k].tecla < 0) {
			continue;
		}
		const tl_tecla *t = &pag.teclas[dedos[k].tecla];
		int total = n_alts(t);
		tl_ret f = caixa_alt(dedos[k].tecla, 0, total, caixa.width);
		tl_ret u = caixa_alt(dedos[k].tecla, total - 1, total, caixa.width);
		ret_redondo(cr, f.x - 4, f.y - 4, u.x + u.w - f.x + 8, f.h + 8, gv_t.raio_medio);
		cor(cr, gv_t.fundo, 1);
		cairo_fill_preserve(cr);
		cor(cr, gv_t.borda_controle, 1);
		cairo_stroke(cr);
		for (int a = 0; a < total; a++) {
			tl_ret c = caixa_alt(dedos[k].tecla, a, total, caixa.width);
			char alt[16];
			alt_n(t, a, alt, sizeof(alt));
			if (a == dedos[k].alt_sel) {
				ret_redondo(cr, c.x + 2, c.y, c.w - 4, c.h, gv_t.raio_pequeno);
				cor(cr, gv_t.acento, 1);
				cairo_fill(cr);
			}
			texto_centrado(cr, alt, c.x + c.w / 2, c.y + c.h / 2, ft * 0.5, a == dedos[k].alt_sel ? gv_t.sobre_acento : gv_t.texto);
		}
	}
	cairo_restore(cr);
}

static void desenhar(gv_app *app, void *dados) {
	float w = gv_app_largura(app);
	if (w > 0) {
		largura_tela = w; /* a camada vai de ponta a ponta da tela, escondida ou não */
	}
	if (esconder_em > 0 && gv_agora() >= esconder_em && !campo_ativo) {
		esconder();
	}
	float pos = gv_anim(CLAY_ID("tec-anim").id, visivel ? 1.0f : 0.0f, 200);
	if (!visivel && pos < 0.01f && camada_grande) {
		camada_grande = false;
		altura_camada = 1;
		gv_app_camada(app, 0, 1, 0);
		gv_app_entrada_em(app, CLAY_ID("tec-vazio"));
	}
	if (!visivel && !camada_grande) {
		CLAY(CLAY_ID("tec-vazio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}
		return;
	}
	float larg_kb = largura_tela > LARGURA_MAX ? LARGURA_MAX : (w > 0 ? w : LARGURA_MAX);
	static float ultima_larg;
	if (pag_suja || larg_kb != ultima_larg) {
		ultima_larg = larg_kb;
		montar_pagina(larg_kb, altura_total());
	}
	Clay_BoundingBox cx = gv_caixa(CLAY_ID("teclado"));
	if (debug && cx.width > 0) {
		static float ux, uy, uw, uh;
		if (cx.x != ux || cx.y != uy || cx.width != uw || cx.height != uh) {
			ux = cx.x; uy = cx.y; uw = cx.width; uh = cx.height;
			log_debug("caixa %.0f %.0f %.0f %.0f camada %.0f", cx.x, cx.y, cx.width, cx.height, altura_camada);
		}
	}
	if (cx.width > 0) {
		cx.y += (1 - pos) * cx.height; /* o teclado está subindo: os toques acompanham o desenho */
		processar_toque(cx);
	}
	static float pos_quadro;
	pos_quadro = pos;
	CLAY(CLAY_ID("tec-raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
		CLAY(CLAY_ID("teclado"), gv_livre(desenhar_teclado, &pos_quadro, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_FIXED(larg_kb), CLAY_SIZING_GROW(0) } } })) {}
	}
	gv_app_entrada_em(app, CLAY_ID("teclado"));
}

int main(int argc, char **argv) {
	if (argc > 1 && (!strcmp(argv[1], "alternar") || !strcmp(argv[1], "esconder"))) {
		return comando_remoto(argv[1]);
	}
	debug = getenv("LUMNA_TECLADO_DEBUG") != NULL;
	if (debug) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	tcfg_ler(&cfg);
	tcfg_recentes_ler();
	est.layout = 0;
	char *pid = caminho_pid();
	char txt[24];
	snprintf(txt, sizeof(txt), "%d\n", (int)getpid());
	g_file_set_contents(pid, txt, -1, NULL);
	g_free(pid);
	altura_camada = 1;
	app_teclado = gv_app_criar(&(gv_opcoes){ .titulo = "Teclado", .app_id = "lumna-teclado", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_BAIXO | GV_ANCORA_ESQUERDA | GV_ANCORA_DIREITA, .altura = 1, .zona_exclusiva = 0, .saida = "@principal" });
	if (!app_teclado) {
		return 1;
	}
	largura_tela = gv_app_largura(app_teclado);
	if (pipe(pipe_sinal) == 0) {
		fcntl(pipe_sinal[0], F_SETFL, O_NONBLOCK);
		fcntl(pipe_sinal[1], F_SETFL, O_NONBLOCK);
		signal(SIGUSR1, sinal);
		signal(SIGUSR2, sinal);
		gv_app_vigiar(app_teclado, pipe_sinal[0], sinal_pronto, NULL);
	}
	bool tem_fisico, toque;
	int layout;
	orbit_entrada(&tem_fisico, &toque, &layout);
	est.layout = layout;
	im = ti_abrir(&(ti_ouvinte){ .campo = ao_campo, .indisponivel = ao_indisponivel }, NULL, NULL, NULL);
	if (im) {
		gv_app_vigiar(app_teclado, ti_fd(im), im_pronto, NULL);
	} else {
		fprintf(stderr, "lumna-teclado: o compositor não fala input-method; o teclado só abre pelo atalho\n");
	}
	log_debug("pronto");
	return gv_app_correr(app_teclado, desenhar, NULL);
}
