/*
 * Aparência → Tema, Cores e Tamanho e textos. As três partilham um modelo (o estilo) e, no
 * Aplicar, geram ~/.config/gravity/tema.ini: todas as apps Gravity vigiam esse arquivo e se
 * repintam na hora (gv_app.c). O que se escolheu fica em ~/.config/lumna/aparencia.ini, para o
 * Painel voltar a mostrar.
 *
 * "Usar cores do papel de parede": a cor dominante do papel da tela principal vira o destaque
 * (gdk-pixbuf num tamanho pequeno, a cor mais frequente entre as saturadas, clareada para o acento).
 */
#include "painel.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "papelini.h"

struct estilo {
	/* Tema */
	int estilo;        /* 0 escuro, 1 claro, 2 automático */
	int densidade;     /* 0 compacta, 1 confortável */
	int cantos;        /* 0 suaves, 1 retos */
	bool animacoes;
	/* Cores */
	uint32_t acento;   /* 0xRRGGBB */
	bool acento_papel;
	int contraste;     /* 0 padrão, 1 alto */
	/* Tamanho e textos */
	int texto;         /* 0 pequeno, 1 padrão, 2 grande, 3 muito grande */
	int fonte;
};

static const struct estilo fabrica = { .animacoes = true, .acento = 0xa78bfa, .texto = 1 };
static struct estilo e, gravado;
static bool lido;

static const struct {
	uint32_t cor;
	const char *nome;
} acentos[] = {
	{ 0xa78bfa, "LUMNA Lavanda" }, { 0x6b9cf0, "Azul céu" }, { 0x3cc8b4, "Turquesa" },
	{ 0x8fb069, "Oliva" }, { 0xe8a04a, "Âmbar" }, { 0x8a93a6, "Grafite" },
};
#define NACENTOS (int)(sizeof(acentos) / sizeof(acentos[0]))

static const char *const fontes[] = { "Inter", "Noto Sans", "Cantarell", "DejaVu Sans" };
#define NFONTES 4

static char hex[16]; /* o campo do código da cor */

/* ---- ler e gravar ---- */

static void ler(void) {
	e = fabrica;
	char caminho[1024];
	pn_caminho(caminho, sizeof(caminho), "aparencia.ini");
	FILE *f = fopen(caminho, "r");
	if (f) {
		char linha[256];
		while (fgets(linha, sizeof(linha), f)) {
			char chave[64], valor[64];
			if (sscanf(linha, " %63[^= ] = %63s", chave, valor) != 2) {
				continue;
			}
			int v = atoi(valor);
			if (!strcmp(chave, "estilo")) {
				e.estilo = v;
			} else if (!strcmp(chave, "densidade")) {
				e.densidade = v;
			} else if (!strcmp(chave, "cantos")) {
				e.cantos = v;
			} else if (!strcmp(chave, "animacoes")) {
				e.animacoes = v;
			} else if (!strcmp(chave, "acento")) {
				e.acento = (uint32_t)strtoul(valor, NULL, 16);
			} else if (!strcmp(chave, "acento_papel")) {
				e.acento_papel = v;
			} else if (!strcmp(chave, "contraste")) {
				e.contraste = v;
			} else if (!strcmp(chave, "texto")) {
				e.texto = v;
			} else if (!strcmp(chave, "fonte")) {
				e.fonte = v >= 0 && v < NFONTES ? v : 0;
			}
		}
		fclose(f);
	}
	gravado = e;
	snprintf(hex, sizeof(hex), "#%06X", e.acento);
	lido = true;
}

static uint32_t misturar(uint32_t a, uint32_t b, float t) {
	uint32_t r = 0;
	for (int s = 0; s <= 16; s += 8) {
		float x = ((a >> s) & 0xff) * (1 - t) + ((b >> s) & 0xff) * t;
		r |= (uint32_t)(x + 0.5f) << s;
	}
	return r;
}

static float luz(uint32_t c) {
	return (0.299f * ((c >> 16) & 0xff) + 0.587f * ((c >> 8) & 0xff) + 0.114f * (c & 0xff)) / 255.0f;
}

/* o tema.ini do Gravity a partir do estilo (a ordem conta: o estilo primeiro, o contraste no fim) */
static void gravar_tema(const struct estilo *s) {
	bool claro = s->estilo == 1;
	uint32_t base = claro ? 0xffffff : 0x1d1e22;
	uint32_t acento = s->acento;
	if (claro && luz(acento) > 0.6f) {
		acento = misturar(acento, 0x000000, 0.25f); /* um acento claro demais some no fundo claro */
	}
	static const float tamanhos[] = { -1, 0, 2, 4 };
	float d = tamanhos[s->texto];
	char texto[2048];
	snprintf(texto, sizeof(texto),
		"# Gerado pelo Painel de controle (Aparência). As apps Gravity recarregam-no na hora.\n"
		"[tema]\nestilo = %s\n\n"
		"[cores]\naccent = #%06X\naccent-hover = #%06X\naccent-pressed = #%06X\nfocus = #%06X\nselected = #%06X\n"
		"on-accent = #%06X\n%s\n"
		"[forma]\nraio_pequeno = %d\nraio_medio = %d\nraio_grande = %d\nespaco = %d\n\n"
		"[texto]\nfonte = %s, Noto Sans, sans-serif\ntamanho_legenda = %g\ntamanho_corpo = %g\ntamanho_destaque = %g\n"
		"tamanho_titulo = %g\n\n"
		"[movimento]\nanimacoes = %s\n",
		s->estilo == 1 ? "claro" : s->estilo == 2 ? "automatico" : "escuro", acento, misturar(acento, 0xffffff, 0.15f),
		misturar(acento, 0x000000, 0.15f), misturar(acento, 0xffffff, 0.35f), misturar(base, acento, claro ? 0.16f : 0.2f),
		luz(acento) > 0.55f ? 0x160f26 : 0xffffff, s->contraste ? "contraste = alto\n" : "",
		s->cantos ? 2 : 6, s->cantos ? 2 : 8, s->cantos ? 3 : 12, s->densidade ? 5 : 4, fontes[s->fonte], 13 + d, 15 + d,
		17 + d, 20 + d, s->animacoes ? "sim" : "nao");
	const char *config = getenv("XDG_CONFIG_HOME");
	char *pasta = config && *config ? g_build_filename(config, "gravity", NULL)
					: g_build_filename(g_get_home_dir(), ".config", "gravity", NULL);
	g_mkdir_with_parents(pasta, 0755);
	char *caminho = g_build_filename(pasta, "tema.ini", NULL);
	char *temp = g_strconcat(caminho, ".novo", NULL);
	if (g_file_set_contents(temp, texto, -1, NULL)) {
		rename(temp, caminho);
	}
	g_free(temp);
	g_free(caminho);
	g_free(pasta);
}

static void gravar(void) {
	char texto[512];
	snprintf(texto, sizeof(texto),
		"# A Aparência escolhida no Painel (o tema.ini do Gravity sai daqui)\n"
		"estilo = %d\ndensidade = %d\ncantos = %d\nanimacoes = %d\nacento = %06X\nacento_papel = %d\ncontraste = %d\n"
		"texto = %d\nfonte = %d\n",
		gravado.estilo, gravado.densidade, gravado.cantos, gravado.animacoes, gravado.acento, gravado.acento_papel,
		gravado.contraste, gravado.texto, gravado.fonte);
	pn_gravar("aparencia.ini", texto);
	gravar_tema(&gravado);
}

/* ---- a cor do papel de parede ---- */

static uint32_t cor_do_papel(void) {
	struct papel_cfg cfg;
	papel_ler(&cfg);
	const char *saidas[8];
	int n = gv_saidas(pn_app, saidas, 8);
	const struct papel_item *it = papel_para(&cfg, papel_modo_atual(), n ? saidas[0] : "");
	GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(it ? it->imagem : papel_padrao(), 48, 48, FALSE, NULL);
	if (!pb) {
		return fabrica.acento;
	}
	/* 24 faixas de matiz: ganha a que junta mais cor (pesada pela saturação) */
	double peso[24] = { 0 }, soma_r[24] = { 0 }, soma_g[24] = { 0 }, soma_b[24] = { 0 };
	int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), passo = gdk_pixbuf_get_rowstride(pb);
	int canais = gdk_pixbuf_get_n_channels(pb);
	const guchar *px = gdk_pixbuf_read_pixels(pb);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const guchar *p = px + y * passo + x * canais;
			double r = p[0] / 255.0, g = p[1] / 255.0, b = p[2] / 255.0;
			double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b)), sat = mx > 0 ? (mx - mn) / mx : 0;
			if (sat < 0.2 || mx < 0.12) {
				continue;
			}
			double hue;
			if (mx == r) {
				hue = fmod((g - b) / (mx - mn), 6);
			} else if (mx == g) {
				hue = (b - r) / (mx - mn) + 2;
			} else {
				hue = (r - g) / (mx - mn) + 4;
			}
			int faixa = ((int)floor(hue * 4) % 24 + 24) % 24;
			double pw = sat * mx;
			peso[faixa] += pw;
			soma_r[faixa] += r * pw;
			soma_g[faixa] += g * pw;
			soma_b[faixa] += b * pw;
		}
	}
	g_object_unref(pb);
	int melhor = 0;
	for (int i = 1; i < 24; i++) {
		if (peso[i] > peso[melhor]) {
			melhor = i;
		}
	}
	if (peso[melhor] <= 0) {
		return acentos[5].cor; /* um papel sem cor: o grafite */
	}
	double r = soma_r[melhor] / peso[melhor], g = soma_g[melhor] / peso[melhor], b = soma_b[melhor] / peso[melhor];
	/* clareia até à luz de um acento (como o lavanda): legível sobre o escuro */
	double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
	double l = (mx + mn) / 2, s = mx == mn ? 0 : (mx - mn) / (1 - fabs(2 * l - 1));
	double alvo_l = 0.72, alvo_s = fmin(fmax(s, 0.5), 0.9);
	double k = alvo_l < 0.5 ? alvo_l * (1 + alvo_s) : alvo_l + alvo_s - alvo_l * alvo_s, m = 2 * alvo_l - k;
	/* a matiz da cor média */
	double hue = 0;
	if (mx != mn) {
		if (mx == r) {
			hue = fmod((g - b) / (mx - mn), 6);
		} else if (mx == g) {
			hue = (b - r) / (mx - mn) + 2;
		} else {
			hue = (r - g) / (mx - mn) + 4;
		}
	}
	hue /= 6;
	if (hue < 0) {
		hue += 1;
	}
	double cor[3];
	for (int i = 0; i < 3; i++) {
		double t = hue + (1 - i) / 3.0;
		t -= floor(t);
		cor[i] = t < 1 / 6.0 ? m + (k - m) * 6 * t : t < 0.5 ? k : t < 2 / 3.0 ? m + (k - m) * (2 / 3.0 - t) * 6 : m;
	}
	return (uint32_t)(cor[0] * 255 + 0.5) << 16 | (uint32_t)(cor[1] * 255 + 0.5) << 8 | (uint32_t)(cor[2] * 255 + 0.5);
}

/* ---- as três seções: abrir, pendente, aplicar ---- */

void pn_estilo_abrir(void) {
	if (!lido) {
		ler();
	} else {
		/* volta a ler o que está gravado, mas não perde o que outra seção tem por aplicar */
		struct estilo atual = e;
		ler();
		e = atual;
	}
}

bool pn_tema_pendente(void) {
	return e.estilo != gravado.estilo || e.densidade != gravado.densidade || e.cantos != gravado.cantos ||
		e.animacoes != gravado.animacoes;
}

bool pn_cores_pendente(void) {
	return e.acento != gravado.acento || e.acento_papel != gravado.acento_papel || e.contraste != gravado.contraste;
}

bool pn_tamanho_pendente(void) {
	return e.texto != gravado.texto || e.fonte != gravado.fonte;
}

void pn_tema_aplicar(void) {
	gravado.estilo = e.estilo;
	gravado.densidade = e.densidade;
	gravado.cantos = e.cantos;
	gravado.animacoes = e.animacoes;
	gravar();
}

void pn_cores_aplicar(void) {
	gravado.acento = e.acento;
	gravado.acento_papel = e.acento_papel;
	gravado.contraste = e.contraste;
	gravar();
}

void pn_tamanho_aplicar(void) {
	gravado.texto = e.texto;
	gravado.fonte = e.fonte;
	gravar();
}

void pn_tema_descartar(void) {
	e.estilo = gravado.estilo;
	e.densidade = gravado.densidade;
	e.cantos = gravado.cantos;
	e.animacoes = gravado.animacoes;
}

void pn_cores_descartar(void) {
	e.acento = gravado.acento;
	e.acento_papel = gravado.acento_papel;
	e.contraste = gravado.contraste;
	snprintf(hex, sizeof(hex), "#%06X", e.acento);
}

void pn_tamanho_descartar(void) {
	e.texto = gravado.texto;
	e.fonte = gravado.fonte;
}

void pn_tema_restaurar(void) {
	e.estilo = fabrica.estilo;
	e.densidade = fabrica.densidade;
	e.cantos = fabrica.cantos;
	e.animacoes = fabrica.animacoes;
}

void pn_cores_restaurar(void) {
	e.acento = fabrica.acento;
	e.acento_papel = fabrica.acento_papel;
	e.contraste = fabrica.contraste;
	snprintf(hex, sizeof(hex), "#%06X", e.acento);
}

void pn_tamanho_restaurar(void) {
	e.texto = fabrica.texto;
	e.fonte = fabrica.fonte;
}

/* ---- Tema ---- */

/* uma janelinha desenhada: o escuro, o claro, ou metade de cada (o automático) */
static void janelinha(bool claro_esq, bool claro_dir) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(180), CLAY_SIZING_FIXED(92) } }, .cornerRadius = CLAY_CORNER_RADIUS(6),
		.clip = { .horizontal = true, .vertical = true } }) {
		for (int lado = 0; lado < 2; lado++) {
			bool claro = lado ? claro_dir : claro_esq;
			Clay_Color fundo = claro ? gv_cor(0xf3f3f6ff) : gv_cor(0x24252aff);
			Clay_Color linha = claro ? gv_cor(0xd6d7dcff) : gv_cor(0x45464dff);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_PERCENT(0.5f), CLAY_SIZING_GROW(0) }, .layoutDirection = CLAY_TOP_TO_BOTTOM,
				.padding = { 10, 10, 10, 8 }, .childGap = 7 }, .backgroundColor = fundo }) {
				CLAY_AUTO_ID({ .layout = { .childGap = 4 } }) {
					for (int i = 0; i < 3 && !lado; i++) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(6), CLAY_SIZING_FIXED(6) } },
							.backgroundColor = i == 0 ? gv_t.acento : linha, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {}
					}
				}
				for (int i = 0; i < 3; i++) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_PERCENT(i == 0 && !lado ? 0.7f : 0.9f - i * 0.15f), CLAY_SIZING_FIXED(6) } },
						.backgroundColor = i == 0 && !lado ? gv_t.acento : linha, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {}
				}
			}
		}
	}
}

static bool cartao_estilo(Clay_ElementId id, const char *nome, bool claro_esq, bool claro_dir, bool escolhido) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14),
			.childGap = 12, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
		.backgroundColor = escolhido ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = escolhido ? gv_t.acento : gv_t.borda, .width = GV_BORDA(escolhido ? 2 : 1) } }) {
		tocado |= gv_focavel(id, gv_t.raio_grande);
		janelinha(claro_esq, claro_dir);
		gv_texto(gv_str(nome), gv_t.tamanho_corpo, gv_t.texto, escolhido ? GV_PESO_MEDIO : GV_PESO_NORMAL);
	}
	return tocado;
}

void pn_tema_desenhar(void) {
	pn_subtitulo(CLAY_ID("tema-s1"), "Estilo do sistema");
	CLAY(CLAY_ID("tema-estilos"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .padding = { 0, 0, 0, 16 } } }) {
		if (cartao_estilo(CLAY_ID("tema-escuro"), "Escuro", false, false, e.estilo == 0)) {
			e.estilo = 0;
		}
		if (cartao_estilo(CLAY_ID("tema-claro"), "Claro", true, true, e.estilo == 1)) {
			e.estilo = 1;
		}
		if (cartao_estilo(CLAY_ID("tema-auto"), "Automático", false, true, e.estilo == 2)) {
			e.estilo = 2;
		}
	}
	pn_linha_comecar(CLAY_ID("tema-densidade"), "stack", "Densidade da interface", "Espaçamento dos controles");
	{
		static const char *const op[] = { "Compacta", "Confortável" };
		pn_segmentos(CLAY_ID("tema-densidade-v"), op, 2, &e.densidade);
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("tema-cantos"), "window", "Cantos das janelas", NULL);
	{
		static const char *const op[] = { "Suaves", "Retos" };
		pn_segmentos(CLAY_ID("tema-cantos-v"), op, 2, &e.cantos);
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("tema-anim"), "play", "Animações", "Transições entre janelas e painéis");
	gv_interruptor(CLAY_ID("tema-anim-v"), &e.animacoes);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("tema-atual"), NULL, "Tema atual",
		gravado.estilo == 1 ? "LUMNA Claro" : gravado.estilo == 2 ? "LUMNA Automático (claro de dia)" : "LUMNA Escuro");
	CLAY_AUTO_ID({ .layout = { .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("check", 16, gv_t.sucesso);
		gv_texto(CLAY_STRING("Ativo"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	pn_linha_acabar();
}

/* ---- Cores ---- */

static bool bolinha(Clay_ElementId id, uint32_t cor, bool escolhida) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(52), CLAY_SIZING_FIXED(52) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.cornerRadius = CLAY_CORNER_RADIUS(26),
		.border = { .color = escolhida ? gv_cor(cor << 8 | 0xff) : gv_por_cima(id) ? gv_t.borda_controle : (Clay_Color){ 0 },
			.width = GV_BORDA(2) } }) {
		tocado |= gv_focavel(id, 26);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_cor(cor << 8 | 0xff), .cornerRadius = CLAY_CORNER_RADIUS(20) }) {
			if (escolhida) {
				gv_icone("check", 20, luz(cor) > 0.55f ? gv_cor(0x160f26ff) : gv_cor(0xffffffff));
			}
		}
	}
	return tocado;
}

void pn_cores_desenhar(void) {
	pn_subtitulo(CLAY_ID("cores-s1"), "Cor de destaque");
	CLAY(CLAY_ID("cores-bolinhas"), { .layout = { .childGap = 10, .padding = { 0, 0, 0, 8 } } }) {
		for (int i = 0; i < NACENTOS; i++) {
			if (bolinha(CLAY_IDI("cor", (uint32_t)i), acentos[i].cor, !e.acento_papel && e.acento == acentos[i].cor)) {
				e.acento = acentos[i].cor;
				e.acento_papel = false;
				snprintf(hex, sizeof(hex), "#%06X", e.acento);
			}
		}
	}
	const char *nome = "Personalizada";
	for (int i = 0; i < NACENTOS; i++) {
		if (acentos[i].cor == e.acento) {
			nome = acentos[i].nome;
		}
	}
	if (e.acento_papel) {
		nome = "Do papel de parede";
	}
	CLAY(CLAY_ID("cores-codigo"), { .layout = { .childGap = 14, .padding = { 0, 0, 0, 16 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			gv_texto(gv_str(nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			if (gv_campo(CLAY_ID("cores-hex"), hex, sizeof(hex), "#RRGGBB", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
				const char *h = hex[0] == '#' ? hex + 1 : hex;
				char *fim;
				unsigned long v = strtoul(h, &fim, 16);
				if (strlen(h) == 6 && !*fim) {
					e.acento = (uint32_t)v;
					e.acento_papel = false;
				}
			}
		}
	}

	/* a prévia: os controles com o destaque escolhido (o tema muda só durante este pedaço) */
	CLAY(CLAY_ID("cores-previa"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
			.padding = CLAY_PADDING_ALL(16), .childGap = 12 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_texto(CLAY_STRING("Prévia"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_tema guardado = gv_t;
		gv_t.acento = gv_cor(e.acento << 8 | 0xff);
		gv_t.acento_cursor = gv_cor(misturar(e.acento, 0xffffff, 0.15f) << 8 | 0xff);
		gv_t.acento_premido = gv_cor(misturar(e.acento, 0x000000, 0.15f) << 8 | 0xff);
		gv_t.sobre_acento = luz(e.acento) > 0.55f ? gv_cor(0x160f26ff) : gv_cor(0xffffffff);
		gv_t.selecionado = gv_cor(misturar(0x1d1e22, e.acento, 0.2f) << 8 | 0xff);
		CLAY(CLAY_ID("cores-previa-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 28, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(240), CLAY_SIZING_FIXED(42) }, .childGap = 12,
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = gv_t.selecionado,
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(4), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.acento }) {}
				gv_icone("list", 18, gv_t.texto);
				gv_texto(CLAY_STRING("Item selecionado"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			gv_botao(CLAY_ID("cores-previa-botao"), NULL, "Botão", GV_BOTAO_CHEIO);
			static bool ligado = true;
			gv_interruptor(CLAY_ID("cores-previa-int"), &ligado);
			static float valor = 0.55f;
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_deslizante(CLAY_ID("cores-previa-desl"), &valor, 0, 0);
			}
		}
		gv_t = guardado;
	}

	CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 10, 0 } } }) {}
	pn_linha_comecar(CLAY_ID("cores-papel"), "image", "Usar cores do papel de parede", "A cor de destaque sai do papel da tela principal");
	if (gv_interruptor(CLAY_ID("cores-papel-v"), &e.acento_papel)) {
		if (e.acento_papel) {
			e.acento = cor_do_papel();
		} else {
			e.acento = gravado.acento_papel ? fabrica.acento : gravado.acento;
		}
		snprintf(hex, sizeof(hex), "#%06X", e.acento);
	}
	pn_linha_acabar();
	static const char *const contrastes[] = { "Padrão", "Alto" };
	pn_ajuste_lista(CLAY_ID("cores-contraste"), "eye", "Contraste", "Alto: textos e bordas mais marcados", contrastes, 2, &e.contraste);
}

/* ---- Tamanho e textos ---- */

void pn_tamanho_desenhar(void) {
	pn_subtitulo(CLAY_ID("tam-s1"), "Tamanho do texto");
	static const char *const tamanhos[] = { "Pequeno", "Padrão", "Grande", "Muito grande" };
	CLAY(CLAY_ID("tam-escolha"), { .layout = { .padding = { 0, 0, 0, 14 } } }) {
		pn_segmentos(CLAY_ID("tam-texto"), tamanhos, 4, &e.texto);
	}
	static const float d[] = { -1, 0, 2, 4 };
	CLAY(CLAY_ID("tam-previa"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
			.padding = CLAY_PADDING_ALL(18), .childGap = 8 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_tema guardado = gv_t;
		snprintf(gv_t.fonte, sizeof(gv_t.fonte), "%s, Noto Sans, sans-serif", fontes[e.fonte]);
		gv_texto(CLAY_STRING("Tudo no seu lugar"), 20 + d[e.texto], gv_t.texto, GV_PESO_NORMAL);
		gv_texto(CLAY_STRING("Assim fica o texto das janelas, dos menus e dos painéis do LUMNA."), 15 + d[e.texto],
			gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(CLAY_STRING("Legenda e detalhes pequenos"), 13 + d[e.texto], gv_t.texto_suave, GV_PESO_NORMAL);
		gv_t = guardado;
	}
	CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 10, 0 } } }) {}
	pn_ajuste_lista(CLAY_ID("tam-fonte"), "text", "Fonte", "A letra da interface", fontes, NFONTES, &e.fonte);
}
