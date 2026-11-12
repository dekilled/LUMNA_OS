/*
 * lumna-signals: o editor de texto do LUMNA (e o bloco de notas). Abas, buscar e substituir, desfazer, abrir e salvar pelo seletor do sistema (o Atlas).
 * O documento é servicos/sgbuffer.c (sem tela); aqui ficam o componente de edição (desenhado com o Pango, com cursor, seleção, teclado e mouse) e a janela.
 *
 *   lumna-signals [ARQUIVO...]     abre os arquivos (cada um numa aba); sem argumentos, uma aba em branco
 *
 * LUMNA_SIGNALS_DEBUG=1 escreve posições e estado (os testes); LUMNA_SELETOR=COMANDO troca o seletor do sistema por um comando (os testes).
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"
#include "seletor.h"
#include "sgbuffer.h"

#define MAX_DOCS 24
#define ZOOM_PADRAO 18
#define ZOOM_MIN 8
#define ZOOM_MAX 64
#define MARGEM_X 18  /* o recuo do texto na área de edição */
#define MARGEM_Y 14

static gv_app *app;

/* ---- os documentos (uma aba cada) ---- */

struct doc {
	sg_buffer *b;
	struct sg_pos cur, anc; /* o cursor e a âncora da seleção (iguais: sem seleção) */
	float xdes;             /* a coluna que a pessoa quer manter ao subir e descer (px), -1: a do cursor */
	bool seguir;            /* rolar até o cursor no próximo quadro */
	int uid;                /* a identidade (a rolagem de cada aba é a dela) */
	int sem_titulo;         /* "Sem título N" (0: tem arquivo) */
	float larg_max;         /* a linha mais larga vista (px): a largura da área rolável */
	double mexeu_em;        /* o último toque no teclado ou no mouse (o cursor pisca a partir daí) */
};

static struct doc *docs[MAX_DOCS];
static int ndocs, ativo;
static int prox_uid = 1, prox_sem_titulo = 1;

/* ---- preferências ---- */

static int zoom_px = ZOOM_PADRAO;
static bool fonte_mono, numeros_linha, destaca_linha = true;

/* ---- estado da interface ---- */

enum dialogo { DLG_NENHUM, DLG_FECHAR_ABA, DLG_SAIR, DLG_MUDOU, DLG_IR_LINHA, DLG_ERRO, DLG_RECARREGAR };
static enum dialogo dialogo;
static int dlg_aba = -1;
static char dlg_texto[320];
static char ir_linha_txt[16];
static bool sair_pendente;       /* saindo: a cada aba com alteração, salvar ou descartar antes */
static bool f_aberta, f_subst, pedir_foco_busca, pedir_foco_editor;
static char f_busca[256], f_subst_txt[256];
static bool f_maius, f_palavra;
static char mensagem[200];
static double mensagem_ate;
static bool depurando;
static double ultima_conferencia;

static void avisar(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void avisar(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(mensagem, sizeof(mensagem), fmt, ap);
	va_end(ap);
	mensagem_ate = gv_agora() + 4;
}

/* ---- as preferências em ~/.config/lumna/signals.ini ---- */

static char *caminho_ini(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "signals.ini", NULL);
}

static void prefs_ler(void) {
	GKeyFile *kf = g_key_file_new();
	char *c = caminho_ini();
	if (g_key_file_load_from_file(kf, c, G_KEY_FILE_NONE, NULL)) {
		int z = g_key_file_get_integer(kf, "exibir", "zoom", NULL);
		if (z >= ZOOM_MIN && z <= ZOOM_MAX) {
			zoom_px = z;
		}
		fonte_mono = g_key_file_get_boolean(kf, "exibir", "monoespacada", NULL);
		numeros_linha = g_key_file_get_boolean(kf, "exibir", "numeros", NULL);
		if (g_key_file_has_key(kf, "exibir", "destacar_linha", NULL)) {
			destaca_linha = g_key_file_get_boolean(kf, "exibir", "destacar_linha", NULL);
		}
	}
	g_free(c);
	g_key_file_free(kf);
}

static void prefs_gravar(void) {
	GKeyFile *kf = g_key_file_new();
	g_key_file_set_integer(kf, "exibir", "zoom", zoom_px);
	g_key_file_set_boolean(kf, "exibir", "monoespacada", fonte_mono);
	g_key_file_set_boolean(kf, "exibir", "numeros", numeros_linha);
	g_key_file_set_boolean(kf, "exibir", "destacar_linha", destaca_linha);
	char *c = caminho_ini(), *dir = g_path_get_dirname(c);
	g_mkdir_with_parents(dir, 0700);
	g_key_file_save_to_file(kf, c, NULL);
	g_free(dir);
	g_free(c);
	g_key_file_free(kf);
}

static void zoom_mudar(int novo) {
	novo = novo < ZOOM_MIN ? ZOOM_MIN : novo > ZOOM_MAX ? ZOOM_MAX : novo;
	if (novo != zoom_px) {
		zoom_px = novo;
		prefs_gravar();
		for (int i = 0; i < ndocs; i++) {
			docs[i]->larg_max = 0;
		}
	}
}

static int zoom_percentual(void) {
	return (int)lround(100.0 * zoom_px / ZOOM_PADRAO);
}

/* ---- o texto: medir e desenhar com o Pango ---- */

static PangoLayout *medidor;
static cairo_surface_t *sup_medidor;
static int fonte_chave = -1;
static float largura_espaco;

static int altura_linha(void) {
	return (int)lroundf(zoom_px * 1.5f);
}

static PangoLayout *layout_de(const char *s, size_t len) {
	if (!medidor) {
		sup_medidor = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
		cairo_t *cr = cairo_create(sup_medidor);
		medidor = pango_cairo_create_layout(cr);
		cairo_destroy(cr);
	}
	int chave = zoom_px * 2 + (fonte_mono ? 1 : 0);
	if (chave != fonte_chave) {
		PangoFontDescription *f = pango_font_description_from_string(gv_t.fonte);
		if (fonte_mono) {
			pango_font_description_set_family(f, "monospace");
		}
		pango_font_description_set_weight(f, PANGO_WEIGHT_NORMAL);
		pango_font_description_set_absolute_size(f, zoom_px * PANGO_SCALE);
		pango_layout_set_font_description(medidor, f);
		pango_font_description_free(f);
		pango_layout_set_text(medidor, " ", 1);
		int w, h;
		pango_layout_get_pixel_size(medidor, &w, &h);
		PangoRectangle r;
		pango_layout_index_to_pos(medidor, 1, &r);
		largura_espaco = (float)r.x / PANGO_SCALE;
		if (largura_espaco < 2) {
			largura_espaco = zoom_px * 0.3f;
		}
		/* o Tab vai até a próxima múltipla de 4 espaços */
		PangoTabArray *tabs = pango_tab_array_new(64, FALSE);
		for (int i = 0; i < 64; i++) {
			pango_tab_array_set_tab(tabs, i, PANGO_TAB_LEFT, (int)(largura_espaco * 4 * (i + 1) * PANGO_SCALE) / PANGO_SCALE);
		}
		pango_layout_set_tabs(medidor, tabs);
		pango_tab_array_free(tabs);
		fonte_chave = chave;
	}
	pango_layout_set_text(medidor, s, (int)len);
	return medidor;
}

static float x_da_pos(const struct doc *d, struct sg_pos p) {
	size_t len;
	const char *s = sg_linha(d->b, p.linha, &len);
	PangoLayout *L = layout_de(s, len);
	PangoRectangle r;
	pango_layout_index_to_pos(L, p.col > (int)len ? (int)len : p.col, &r);
	return (float)r.x / PANGO_SCALE;
}

static int col_de_x(const struct doc *d, int linha, float x) {
	size_t len;
	const char *s = sg_linha(d->b, linha, &len);
	PangoLayout *L = layout_de(s, len);
	int idx, trail;
	pango_layout_xy_to_index(L, (int)(x * PANGO_SCALE), 0, &idx, &trail);
	if (x <= 0) {
		return 0;
	}
	if (idx >= (int)len) {
		return (int)len;
	}
	if (trail > 0) {
		idx = (int)(g_utf8_next_char(s + idx) - s);
	}
	return idx > (int)len ? (int)len : idx;
}

static float gutter_largura(const struct doc *d) {
	if (!numeros_linha) {
		return 0;
	}
	int n = sg_linhas(d->b), digitos = 1;
	while (n >= 10) {
		n /= 10;
		digitos++;
	}
	if (digitos < 3) {
		digitos = 3;
	}
	layout_de("0", 1);
	return (float)digitos * zoom_px * 0.62f + 28;
}

/* ---- documentos ---- */

static struct doc *doc_ativo(void) {
	return ndocs > 0 ? docs[ativo] : NULL;
}

static struct doc *doc_novo(void) {
	struct doc *d = g_new0(struct doc, 1);
	d->b = sg_novo();
	d->uid = prox_uid++;
	d->sem_titulo = prox_sem_titulo++;
	d->xdes = -1;
	d->mexeu_em = gv_agora();
	return d;
}

static const char *rotulo(const struct doc *d) {
	const char *c = sg_caminho(d->b);
	if (c) {
		const char *barra = strrchr(c, '/');
		return barra ? barra + 1 : c;
	}
	return d->sem_titulo > 1 ? gv_fmt("Sem título %d", d->sem_titulo).chars : "Sem título";
}

static void titulo_da_janela(void) {
	struct doc *d = doc_ativo();
	if (d && app) {
		char *t = g_strdup_printf("%s%s — Signals", sg_modificado(d->b) ? "• " : "", rotulo(d));
		gv_app_definir_titulo(app, t);
		g_free(t);
	}
}

static void doc_ativar(int i) {
	if (i >= 0 && i < ndocs) {
		ativo = i;
		docs[i]->seguir = true;
		pedir_foco_editor = true;
		titulo_da_janela();
	}
}

static int doc_adicionar(struct doc *d) {
	if (ndocs >= MAX_DOCS) {
		sg_liberar(d->b);
		g_free(d);
		avisar("Abas demais: feche alguma");
		return -1;
	}
	docs[ndocs] = d;
	doc_ativar(ndocs);
	return ndocs++;
}

static void doc_remover(int i) {
	sg_liberar(docs[i]->b);
	g_free(docs[i]);
	memmove(&docs[i], &docs[i + 1], sizeof(docs[0]) * (size_t)(ndocs - i - 1));
	ndocs--;
	if (ndocs == 0) {
		doc_adicionar(doc_novo()); /* a janela nunca fica sem aba: some a última, vem uma em branco */
		return;
	}
	if (ativo >= ndocs) {
		ativo = ndocs - 1;
	} else if (ativo > i) {
		ativo--;
	}
	doc_ativar(ativo);
}

static void mostrar_erro(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void mostrar_erro(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(dlg_texto, sizeof(dlg_texto), fmt, ap);
	va_end(ap);
	dialogo = DLG_ERRO;
}

/* ---- a seleção e o cursor ---- */

static bool tem_selecao(const struct doc *d) {
	return sg_pos_cmp(d->cur, d->anc) != 0;
}

static void selecao(const struct doc *d, struct sg_pos *a, struct sg_pos *z) {
	if (sg_pos_cmp(d->cur, d->anc) <= 0) {
		*a = d->cur;
		*z = d->anc;
	} else {
		*a = d->anc;
		*z = d->cur;
	}
}

static void ir_para(struct doc *d, struct sg_pos p, bool estende) {
	d->cur = sg_pos_valida(d->b, p);
	if (!estende) {
		d->anc = d->cur;
	}
	d->seguir = true;
	d->mexeu_em = gv_agora();
}

static char *texto_selecionado(const struct doc *d) {
	struct sg_pos a, z;
	selecao(d, &a, &z);
	return sg_texto(d->b, a, z);
}

/* apaga a seleção; devolve se havia */
static bool apagar_selecao(struct doc *d) {
	if (!tem_selecao(d)) {
		return false;
	}
	struct sg_pos a, z;
	selecao(d, &a, &z);
	sg_fechar_grupo(d->b);
	struct sg_pos p = sg_apagar(d->b, a, z, false);
	d->cur = d->anc = p;
	d->seguir = true;
	d->xdes = -1;
	return true;
}

static void inserir_texto(struct doc *d, const char *t, size_t n, bool junta) {
	if (tem_selecao(d)) {
		struct sg_pos a, z;
		selecao(d, &a, &z);
		sg_fechar_grupo(d->b);
		struct sg_pos p = sg_trocar(d->b, a, z, t, n);
		d->cur = d->anc = p;
	} else {
		struct sg_pos p = sg_inserir(d->b, d->cur, t, n, junta);
		d->cur = d->anc = p;
	}
	d->seguir = true;
	d->xdes = -1;
	d->mexeu_em = gv_agora();
}

/* o recuo (espaços e tabs) do começo da linha, até o cursor */
static char *recuo_da_linha(const struct doc *d, int linha, int ate_col) {
	size_t len;
	const char *s = sg_linha(d->b, linha, &len);
	size_t i = 0;
	while (i < len && (int)i < ate_col && (s[i] == ' ' || s[i] == '\t')) {
		i++;
	}
	return g_strndup(s, i);
}

/* indenta (ou tira o recuo de) as linhas tocadas pela seleção, numa operação só */
static void mexer_recuo(struct doc *d, bool tirar) {
	struct sg_pos a, z;
	selecao(d, &a, &z);
	int ultima = z.linha;
	if (z.linha > a.linha && z.col == 0) {
		ultima--; /* a seleção que termina no começo de uma linha não toca nela */
	}
	GString *novo = g_string_new(NULL);
	int delta_a = 0, delta_z = 0;
	for (int i = a.linha; i <= ultima; i++) {
		size_t len;
		const char *s = sg_linha(d->b, i, &len);
		int mexida = 0;
		if (tirar) {
			size_t k = 0;
			if (len > 0 && s[0] == '\t') {
				k = 1;
			} else {
				while (k < 4 && k < len && s[k] == ' ') {
					k++;
				}
			}
			g_string_append_len(novo, s + k, (gssize)(len - k));
			mexida = -(int)k;
		} else {
			g_string_append_c(novo, '\t');
			g_string_append_len(novo, s, (gssize)len);
			mexida = 1;
		}
		if (i == a.linha) {
			delta_a = mexida;
		}
		if (i == z.linha) {
			delta_z = mexida;
		}
		if (i < ultima) {
			g_string_append_c(novo, '\n');
		}
	}
	size_t len_ult;
	sg_linha(d->b, ultima, &len_ult);
	sg_fechar_grupo(d->b);
	sg_trocar(d->b, (struct sg_pos){ a.linha, 0 }, (struct sg_pos){ ultima, (int)len_ult }, novo->str, novo->len);
	g_string_free(novo, TRUE);
	struct sg_pos na = { a.linha, a.col + delta_a }, nz = { z.linha, z.col + delta_z };
	if (na.col < 0) {
		na.col = 0;
	}
	if (nz.col < 0) {
		nz.col = 0;
	}
	bool cursor_era_a = sg_pos_cmp(d->cur, d->anc) <= 0;
	d->anc = cursor_era_a ? nz : na;
	d->cur = cursor_era_a ? na : nz;
	d->anc = sg_pos_valida(d->b, d->anc);
	d->cur = sg_pos_valida(d->b, d->cur);
	d->seguir = true;
}

/* ---- buscar e substituir ---- */

static uint64_t cache_versao;
static int cache_uid = -1, cache_total, cache_indice;
static struct sg_pos cache_sel;
static char cache_busca[256];
static bool cache_maius, cache_palavra;

static struct sg_busca consulta(void) {
	return (struct sg_busca){ f_busca, f_maius, f_palavra };
}

/* "N de M": recalculado só quando muda o texto, a consulta ou a seleção */
static void recontar(struct doc *d) {
	struct sg_pos a, z;
	selecao(d, &a, &z);
	if (cache_uid == d->uid && cache_versao == sg_versao(d->b) && !sg_pos_cmp(cache_sel, a) && !strcmp(cache_busca, f_busca) && cache_maius == f_maius && cache_palavra == f_palavra) {
		return;
	}
	cache_uid = d->uid;
	cache_versao = sg_versao(d->b);
	cache_sel = a;
	g_strlcpy(cache_busca, f_busca, sizeof(cache_busca));
	cache_maius = f_maius;
	cache_palavra = f_palavra;
	struct sg_busca q = consulta();
	cache_total = f_busca[0] ? sg_contar(d->b, &q) : 0;
	cache_indice = 0;
	if (cache_total > 0 && tem_selecao(d)) {
		/* a seleção é um achado? então é o de número (quantos começam até ela) */
		struct sg_pos i, f;
		bool voltou;
		if (sg_buscar(d->b, &q, a, true, &i, &f, &voltou) && !voltou && !sg_pos_cmp(i, a) && !sg_pos_cmp(f, z)) {
			int ate = 0;
			int ini[64], tam[64];
			for (int l = 0; l <= a.linha; l++) {
				int n = sg_buscar_linha(d->b, &q, l, ini, tam, 64);
				for (int k = 0; k < n; k++) {
					if (l < a.linha || ini[k] <= a.col) {
						ate++;
					}
				}
			}
			cache_indice = ate;
		}
	}
}

static void achar(struct doc *d, bool frente, bool a_partir_do_comeco_da_selecao) {
	if (!f_busca[0]) {
		return;
	}
	struct sg_busca q = consulta();
	struct sg_pos a, z, i, f;
	selecao(d, &a, &z);
	bool voltou = false;
	struct sg_pos de = frente ? (a_partir_do_comeco_da_selecao ? a : z) : a;
	if (sg_buscar(d->b, &q, de, frente, &i, &f, &voltou)) {
		d->anc = i;
		d->cur = f;
		d->seguir = true;
		d->xdes = -1;
		if (voltou) {
			avisar(frente ? "Chegou ao fim: voltou ao início" : "Chegou ao início: voltou ao fim");
		}
	} else {
		avisar("Não achei “%s”", f_busca);
	}
}

static void substituir_atual(struct doc *d) {
	struct sg_busca q = consulta();
	struct sg_pos a, z, i, f;
	bool voltou;
	selecao(d, &a, &z);
	if (tem_selecao(d) && sg_buscar(d->b, &q, a, true, &i, &f, &voltou) && !voltou && !sg_pos_cmp(i, a) && !sg_pos_cmp(f, z)) {
		sg_fechar_grupo(d->b);
		struct sg_pos p = sg_trocar(d->b, a, z, f_subst_txt, strlen(f_subst_txt));
		d->cur = d->anc = p;
		sg_fechar_grupo(d->b);
	}
	achar(d, true, false);
}

static void substituir_tudo(struct doc *d) {
	struct sg_busca q = consulta();
	int n = sg_substituir_tudo(d->b, &q, f_subst_txt);
	d->cur = d->anc = sg_pos_valida(d->b, d->cur);
	avisar(n == 1 ? "1 substituição" : "%d substituições", n);
}

static void abrir_busca(bool com_substituir) {
	struct doc *d = doc_ativo();
	f_aberta = true;
	f_subst = com_substituir;
	pedir_foco_busca = true;
	/* uma seleção de uma linha só vira a busca */
	if (d && tem_selecao(d)) {
		struct sg_pos a, z;
		selecao(d, &a, &z);
		if (a.linha == z.linha && z.col - a.col < (int)sizeof(f_busca) - 1) {
			char *t = sg_texto(d->b, a, z);
			g_strlcpy(f_busca, t, sizeof(f_busca));
			g_free(t);
		}
	}
}

static void fechar_busca(void) {
	f_aberta = false;
	f_subst = false;
	pedir_foco_editor = true;
}

/* ---- abrir, salvar, fechar ---- */

static void continuar_saida(void);

static int doc_com_arquivo(const char *caminho) {
	char *real = realpath(caminho, NULL);
	const char *comp = real ? real : caminho;
	int achado = -1;
	for (int i = 0; i < ndocs; i++) {
		const char *c = sg_caminho(docs[i]->b);
		if (c && !strcmp(c, comp)) {
			achado = i;
			break;
		}
	}
	free(real);
	return achado;
}

static void abrir_arquivo(const char *caminho) {
	int ja = doc_com_arquivo(caminho);
	if (ja >= 0) {
		doc_ativar(ja);
		return;
	}
	struct doc *d = doc_novo();
	char erro[240];
	char *real = realpath(caminho, NULL);
	if (sg_abrir(d->b, real ? real : caminho, erro, sizeof(erro)) != 0) {
		/* um arquivo que não existe ainda vira um documento novo com esse nome (lumna-signals nome.txt cria ao salvar) */
		if (!g_file_test(caminho, G_FILE_TEST_EXISTS)) {
			free(real);
			char *cwd = g_get_current_dir();
			char *abs = g_path_is_absolute(caminho) ? g_strdup(caminho) : g_build_filename(cwd, caminho, NULL);
			sg_definir_caminho(d->b, abs);
			g_free(abs);
			g_free(cwd);
			d->sem_titulo = 0;
			doc_adicionar(d);
			return;
		}
		sg_liberar(d->b);
		g_free(d);
		mostrar_erro("Não consegui abrir “%s”: %s.", caminho, erro);
		free(real);
		return;
	}
	free(real);
	d->sem_titulo = 0;
	/* a aba em branco e sem alteração do começo dá lugar ao arquivo */
	if (ndocs == 1 && !sg_caminho(docs[0]->b) && !sg_modificado(docs[0]->b) && sg_bytes(docs[0]->b) == 0) {
		sg_liberar(docs[0]->b);
		g_free(docs[0]);
		ndocs = 0;
		ativo = 0;
	}
	if (doc_adicionar(d) >= 0 && sg_fim_misto(d->b)) {
		avisar("O arquivo misturava quebras de linha (LF e CRLF): ficou tudo em %s", sg_fim_de_linha(d->b) == SG_CRLF ? "CRLF" : "LF");
	}
}

static void aoabrir_seletor(char **caminhos, int n, const char *erro, void *dados) {
	(void)dados;
	if (erro) {
		mostrar_erro("%s.", erro);
	}
	for (int i = 0; i < n; i++) {
		abrir_arquivo(caminhos[i]);
	}
	g_strfreev(caminhos);
}

static void pedir_abrir(void) {
	static const char *const tipos[] = { "text/*", "application/json", "application/xml", "application/x-shellscript", "application/x-yaml", NULL };
	struct doc *d = doc_ativo();
	const char *c = d ? sg_caminho(d->b) : NULL;
	char *pasta = c ? g_path_get_dirname(c) : NULL;
	seletor_pedir(&(struct seletor_pedido){ .titulo = "Abrir arquivo", .pasta = pasta, .multiplo = true, .filtro_nome = "Texto", .tipos = tipos }, aoabrir_seletor, NULL);
	g_free(pasta);
}

struct salvando {
	int uid;
	bool depois_sair;
	bool fechar_aba;
};

static int achar_uid(int uid) {
	for (int i = 0; i < ndocs; i++) {
		if (docs[i]->uid == uid) {
			return i;
		}
	}
	return -1;
}

static void apos_salvar(struct salvando *s, bool ok) {
	int i = achar_uid(s->uid);
	if (ok && i >= 0) {
		if (s->fechar_aba) {
			doc_remover(i);
		}
		if (s->depois_sair) {
			continuar_saida();
		}
	} else if (s->depois_sair) {
		sair_pendente = false; /* cancelou o seletor ou deu erro: a saída para */
	}
	titulo_da_janela();
	g_free(s);
}

static bool gravar(struct doc *d, const char *caminho) {
	char erro[240];
	if (sg_salvar(d->b, caminho, erro, sizeof(erro)) != 0) {
		mostrar_erro("Não consegui salvar: %s.", erro);
		return false;
	}
	d->sem_titulo = 0;
	d->larg_max = 0;
	avisar("Salvo: %s", rotulo(d));
	titulo_da_janela();
	return true;
}

static void aosalvar_seletor(char **caminhos, int n, const char *erro, void *dados) {
	struct salvando *s = dados;
	int i = achar_uid(s->uid);
	bool ok = false;
	if (erro) {
		mostrar_erro("%s.", erro);
	} else if (n > 0 && i >= 0) {
		ok = gravar(docs[i], caminhos[0]);
	}
	g_strfreev(caminhos);
	apos_salvar(s, ok);
}

static void salvar_como(struct doc *d, bool depois_sair, bool fechar_aba) {
	struct salvando *s = g_new0(struct salvando, 1);
	s->uid = d->uid;
	s->depois_sair = depois_sair;
	s->fechar_aba = fechar_aba;
	const char *c = sg_caminho(d->b);
	char *pasta = c ? g_path_get_dirname(c) : NULL;
	static const char *const tipos[] = { "text/plain", NULL };
	seletor_pedir(&(struct seletor_pedido){ .salvar = true, .titulo = "Salvar como", .nome_sugerido = c ? rotulo(d) : "texto.txt", .pasta = pasta, .filtro_nome = "Texto", .tipos = tipos }, aosalvar_seletor, s);
	g_free(pasta);
}

/* salva (pede o nome se ainda não tem); depois_sair/fechar_aba: o que fazer quando terminar */
static void salvar(struct doc *d, bool depois_sair, bool fechar_aba) {
	if (!sg_caminho(d->b)) {
		salvar_como(d, depois_sair, fechar_aba);
		return;
	}
	struct salvando *s = g_new0(struct salvando, 1);
	s->uid = d->uid;
	s->depois_sair = depois_sair;
	s->fechar_aba = fechar_aba;
	apos_salvar(s, gravar(d, NULL));
}

static void pedir_fechar_aba(int i) {
	if (i < 0 || i >= ndocs) {
		return;
	}
	if (sg_modificado(docs[i]->b)) {
		ativo = i;
		dlg_aba = i;
		dialogo = DLG_FECHAR_ABA;
		return;
	}
	doc_remover(i);
}

/* sair: cada aba com alteração é salva ou descartada antes; a primeira que falta decide o próximo passo */
static void continuar_saida(void) {
	for (int i = 0; i < ndocs; i++) {
		if (sg_modificado(docs[i]->b)) {
			ativo = i;
			dlg_aba = i;
			dialogo = DLG_SAIR;
			return;
		}
	}
	gv_app_sair(app);
}

static bool ao_fechar_janela(void *dados) {
	(void)dados;
	for (int i = 0; i < ndocs; i++) {
		if (sg_modificado(docs[i]->b)) {
			sair_pendente = true;
			continuar_saida();
			return true; /* tratou: a janela só fecha depois de decidir o que salvar */
		}
	}
	return false;
}

static void pedir_sair(void) {
	if (!ao_fechar_janela(NULL)) {
		gv_app_sair(app);
	}
}

static void recarregar(struct doc *d) {
	char erro[240];
	struct sg_pos antes = d->cur;
	if (sg_abrir(d->b, sg_caminho(d->b), erro, sizeof(erro)) != 0) {
		mostrar_erro("Não consegui recarregar: %s.", erro);
		return;
	}
	d->cur = d->anc = sg_pos_valida(d->b, antes);
	d->larg_max = 0;
	d->seguir = true;
	avisar("Recarregado do disco");
}

/* o arquivo da aba ativa mudou no disco (outro programa): sem alterações nossas, recarrega calado; com elas, pergunta */
static void conferir_disco(void) {
	struct doc *d = doc_ativo();
	if (!d || !sg_caminho(d->b) || dialogo != DLG_NENHUM || !sg_mudou_no_disco(d->b)) {
		return;
	}
	if (!g_file_test(sg_caminho(d->b), G_FILE_TEST_EXISTS)) {
		sg_ignorar_mudanca_no_disco(d->b);
		avisar("O arquivo foi apagado do disco; salve para criá-lo de novo");
		return;
	}
	if (!sg_modificado(d->b)) {
		recarregar(d);
		return;
	}
	dialogo = DLG_MUDOU;
}

/* ---- o componente de edição: teclado ---- */

static float vista_altura(const struct doc *d) {
	Clay_BoundingBox b = gv_caixa(CLAY_IDI("sg-rol", (uint32_t)d->uid));
	return b.height > 0 ? b.height : 400;
}

static void mover_vertical(struct doc *d, int dl, bool estende) {
	if (d->xdes < 0) {
		d->xdes = x_da_pos(d, d->cur);
	}
	int alvo = d->cur.linha + dl;
	struct sg_pos p;
	if (alvo < 0) {
		p = (struct sg_pos){ 0, 0 };
	} else if (alvo >= sg_linhas(d->b)) {
		p = sg_pos_fim(d->b);
	} else {
		p = (struct sg_pos){ alvo, col_de_x(d, alvo, d->xdes) };
	}
	float x = d->xdes;
	ir_para(d, p, estende);
	d->xdes = x;
}

static struct sg_pos inicio_inteligente(const struct doc *d) {
	size_t len;
	const char *s = sg_linha(d->b, d->cur.linha, &len);
	int i = 0;
	while ((size_t)i < len && (s[i] == ' ' || s[i] == '\t')) {
		i++;
	}
	return (struct sg_pos){ d->cur.linha, d->cur.col == i ? 0 : i };
}

static bool tecla_so_modificador(uint32_t s) {
	return s == XKB_KEY_Shift_L || s == XKB_KEY_Shift_R || s == XKB_KEY_Control_L || s == XKB_KEY_Control_R || s == XKB_KEY_Alt_L || s == XKB_KEY_Alt_R || s == XKB_KEY_Super_L ||
		s == XKB_KEY_Super_R || s == XKB_KEY_Caps_Lock || s == XKB_KEY_Num_Lock || s == XKB_KEY_ISO_Level3_Shift || s == XKB_KEY_Meta_L || s == XKB_KEY_Meta_R;
}

/* as teclas que só valem quando o foco está no editor */
static void tecla_editor(struct doc *d, uint32_t sym, const char *txt, uint32_t mods) {
	bool ctrl = mods & GV_MOD_CTRL, shift = mods & GV_MOD_SHIFT, alt = mods & GV_MOD_ALT;
	d->mexeu_em = gv_agora();
	if (ctrl && !alt) {
		uint32_t k = sym >= 'A' && sym <= 'Z' ? sym + 32 : sym;
		switch (k) {
		case 'a':
			d->anc = (struct sg_pos){ 0, 0 };
			d->cur = sg_pos_fim(d->b);
			d->seguir = false;
			return;
		case 'c':
		case 'x':
			if (tem_selecao(d)) {
				char *t = texto_selecionado(d);
				gv_copiar(t);
				g_free(t);
				if (k == 'x') {
					apagar_selecao(d);
				}
			}
			return;
		case 'v': {
			const char *t = gv_colar();
			if (t && *t) {
				sg_fechar_grupo(d->b);
				inserir_texto(d, t, strlen(t), false);
				sg_fechar_grupo(d->b);
			}
			return;
		}
		case 'z':
			if (shift) {
				goto refazer;
			}
			if (sg_desfazer(d->b, &d->cur)) {
				d->anc = d->cur;
				d->seguir = true;
				d->xdes = -1;
			}
			return;
		case 'y':
		refazer:
			if (sg_refazer(d->b, &d->cur)) {
				d->anc = d->cur;
				d->seguir = true;
				d->xdes = -1;
			}
			return;
		case XKB_KEY_Home:
			ir_para(d, (struct sg_pos){ 0, 0 }, shift);
			d->xdes = -1;
			return;
		case XKB_KEY_End:
			ir_para(d, sg_pos_fim(d->b), shift);
			d->xdes = -1;
			return;
		case XKB_KEY_Left:
			ir_para(d, sg_pos_palavra_anterior(d->b, d->cur), shift);
			d->xdes = -1;
			return;
		case XKB_KEY_Right:
			ir_para(d, sg_pos_palavra_proxima(d->b, d->cur), shift);
			d->xdes = -1;
			return;
		case XKB_KEY_BackSpace:
			if (!apagar_selecao(d)) {
				struct sg_pos a = sg_pos_palavra_anterior(d->b, d->cur);
				d->cur = d->anc = sg_apagar(d->b, a, d->cur, false);
				d->seguir = true;
			}
			return;
		case XKB_KEY_Delete:
			if (!apagar_selecao(d)) {
				struct sg_pos z = sg_pos_palavra_proxima(d->b, d->cur);
				d->cur = d->anc = sg_apagar(d->b, d->cur, z, false);
				d->seguir = true;
			}
			return;
		default:
			return;
		}
	}
	switch (sym) {
	case XKB_KEY_Left:
		if (tem_selecao(d) && !shift) {
			struct sg_pos a, z;
			selecao(d, &a, &z);
			ir_para(d, a, false);
		} else {
			ir_para(d, sg_pos_anterior(d->b, d->cur), shift);
		}
		d->xdes = -1;
		return;
	case XKB_KEY_Right:
		if (tem_selecao(d) && !shift) {
			struct sg_pos a, z;
			selecao(d, &a, &z);
			ir_para(d, z, false);
		} else {
			ir_para(d, sg_pos_proximo(d->b, d->cur), shift);
		}
		d->xdes = -1;
		return;
	case XKB_KEY_Up:
		mover_vertical(d, -1, shift);
		return;
	case XKB_KEY_Down:
		mover_vertical(d, 1, shift);
		return;
	case XKB_KEY_Page_Up:
	case XKB_KEY_Page_Down: {
		int n = (int)(vista_altura(d) / altura_linha()) - 1;
		mover_vertical(d, (sym == XKB_KEY_Page_Up ? -1 : 1) * (n < 1 ? 1 : n), shift);
		return;
	}
	case XKB_KEY_Home:
		ir_para(d, inicio_inteligente(d), shift);
		d->xdes = -1;
		return;
	case XKB_KEY_End: {
		size_t len;
		sg_linha(d->b, d->cur.linha, &len);
		ir_para(d, (struct sg_pos){ d->cur.linha, (int)len }, shift);
		d->xdes = -1;
		return;
	}
	case XKB_KEY_BackSpace:
		if (!apagar_selecao(d) && sg_pos_cmp(d->cur, (struct sg_pos){ 0, 0 }) > 0) {
			struct sg_pos a = sg_pos_anterior(d->b, d->cur);
			d->cur = d->anc = sg_apagar(d->b, a, d->cur, true);
			d->seguir = true;
			d->xdes = -1;
		}
		return;
	case XKB_KEY_Delete:
		if (!apagar_selecao(d)) {
			struct sg_pos z = sg_pos_proximo(d->b, d->cur);
			d->cur = d->anc = sg_apagar(d->b, d->cur, z, true);
			d->seguir = true;
			d->xdes = -1;
		}
		return;
	case XKB_KEY_Return:
	case XKB_KEY_KP_Enter: {
		/* a linha nova começa com o mesmo recuo da de cima */
		struct sg_pos a, z;
		selecao(d, &a, &z);
		char *recuo = recuo_da_linha(d, a.linha, a.col);
		char *t = g_strconcat("\n", recuo, NULL);
		sg_fechar_grupo(d->b);
		inserir_texto(d, t, strlen(t), false);
		g_free(t);
		g_free(recuo);
		return;
	}
	case XKB_KEY_Tab:
	case XKB_KEY_ISO_Left_Tab: {
		struct sg_pos a, z;
		selecao(d, &a, &z);
		bool tirar = shift || sym == XKB_KEY_ISO_Left_Tab;
		if (a.linha != z.linha || tirar) {
			mexer_recuo(d, tirar);
		} else {
			inserir_texto(d, "\t", 1, true);
		}
		return;
	}
	case XKB_KEY_Escape:
		if (tem_selecao(d)) {
			d->anc = d->cur;
		}
		return;
	default:
		break;
	}
	if (!(mods & (GV_MOD_CTRL | GV_MOD_ALT | GV_MOD_LOGO)) && txt && (unsigned char)txt[0] >= 0x20 && txt[0] != 0x7f) {
		inserir_texto(d, txt, strlen(txt), true);
	}
}

/* ---- o componente de edição: mouse ---- */

static struct sg_pos pos_do_ponteiro(struct doc *d, Clay_BoundingBox cb, float gutter, Clay_Vector2 p) {
	int lh = altura_linha();
	int linha = (int)floorf((p.y - cb.y - MARGEM_Y) / (float)lh);
	if (linha < 0) {
		return (struct sg_pos){ 0, 0 };
	}
	if (linha >= sg_linhas(d->b)) {
		return sg_pos_fim(d->b);
	}
	return (struct sg_pos){ linha, col_de_x(d, linha, p.x - cb.x - gutter - MARGEM_X) };
}

static void mouse_editor(struct doc *d, float gutter) {
	static bool antes;
	static int cliques;
	static double ultimo;
	static struct sg_pos ult_pos, ini0, fim0;
	Clay_ElementId cont = CLAY_IDI("sg-cont", (uint32_t)d->uid), rol = CLAY_IDI("sg-rol", (uint32_t)d->uid);
	bool premido = gv_premido(cont);
	Clay_BoundingBox cb = gv_caixa(cont), vb = gv_caixa(rol);
	Clay_Vector2 p = gv_ponteiro();
	uint32_t mods = gv_modificadores();
	if (premido && !antes && dialogo == DLG_NENHUM) {
		double agora = gv_agora();
		struct sg_pos q = pos_do_ponteiro(d, cb, gutter, p);
		cliques = (agora - ultimo < 0.45 && q.linha == ult_pos.linha && abs(q.col - ult_pos.col) < 3) ? cliques % 3 + 1 : 1;
		ultimo = agora;
		ult_pos = q;
		gv_focar(CLAY_ID("sg-editor"));
		d->xdes = -1;
		sg_fechar_grupo(d->b);
		if (cliques == 1) {
			ir_para(d, q, mods & GV_MOD_SHIFT);
			ini0 = fim0 = q;
		} else if (cliques == 2) {
			sg_palavra_em(d->b, q, &ini0, &fim0);
			d->anc = ini0;
			d->cur = fim0;
		} else {
			ini0 = (struct sg_pos){ q.linha, 0 };
			size_t len;
			sg_linha(d->b, q.linha, &len);
			fim0 = q.linha + 1 < sg_linhas(d->b) ? (struct sg_pos){ q.linha + 1, 0 } : (struct sg_pos){ q.linha, (int)len };
			d->anc = ini0;
			d->cur = fim0;
		}
		d->mexeu_em = agora;
	} else if (premido && antes) {
		struct sg_pos q = pos_do_ponteiro(d, cb, gutter, p);
		if (cliques == 1) {
			d->cur = q;
		} else if (cliques == 2) {
			struct sg_pos qa, qz;
			sg_palavra_em(d->b, q, &qa, &qz);
			if (sg_pos_cmp(q, ini0) < 0) {
				d->anc = fim0;
				d->cur = qa;
			} else {
				d->anc = ini0;
				d->cur = qz;
			}
		} else {
			struct sg_pos linha_ini = { q.linha, 0 };
			if (sg_pos_cmp(q, ini0) < 0) {
				d->anc = fim0;
				d->cur = linha_ini;
			} else {
				d->anc = ini0;
				d->cur = q.linha + 1 < sg_linhas(d->b) ? (struct sg_pos){ q.linha + 1, 0 } : sg_pos_fim(d->b);
			}
		}
		/* arrastando para fora da área: rola */
		Clay_Vector2 pos = gv_rolagem_posicao(rol);
		if (p.y < vb.y + 6) {
			gv_rolar_para(rol, pos.x, pos.y - 14, false);
		} else if (p.y > vb.y + vb.height - 6) {
			gv_rolar_para(rol, pos.x, pos.y + 14, false);
		}
		d->mexeu_em = gv_agora();
		gv_app_redesenhar(app);
	}
	antes = premido;
}

/* ---- o componente de edição: desenho ---- */

struct desenho {
	struct doc *d;
	float gutter;
	bool cursor_visivel;
};
static struct desenho desenho_atual;

static void cor_cairo(cairo_t *cr, Clay_Color c, float alfa) {
	cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0, (c.a / 255.0) * alfa);
}

static void retangulo(cairo_t *cr, double x, double y, double w, double h) {
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
}

static void desenhar_editor(cairo_t *cr, Clay_BoundingBox cx, void *dados) {
	struct desenho *dd = dados;
	struct doc *d = dd->d;
	if (!d) {
		return;
	}
	int lh = altura_linha(), n = sg_linhas(d->b);
	double x1, y1, x2, y2;
	cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
	int primeira = (int)floor((y1 - cx.y - MARGEM_Y) / lh) - 1, ultima = (int)ceil((y2 - cx.y - MARGEM_Y) / lh) + 1;
	primeira = primeira < 0 ? 0 : primeira;
	ultima = ultima >= n ? n - 1 : ultima;
	double x0 = cx.x + dd->gutter + MARGEM_X;
	struct sg_pos sa, sz;
	selecao(d, &sa, &sz);
	bool sel = tem_selecao(d);
	struct sg_busca q = consulta();
	bool achando = f_aberta && f_busca[0];
	double largura_area = fmax(cx.width, x2 - cx.x);
	/* o número das linhas: o fundo da calha e a divisória */
	if (dd->gutter > 0) {
		cor_cairo(cr, gv_t.elevada, 0.5f);
		retangulo(cr, cx.x, y1, dd->gutter, y2 - y1);
		cor_cairo(cr, gv_t.borda, 1);
		retangulo(cr, cx.x + dd->gutter - 1, y1, 1, y2 - y1);
	}
	for (int i = primeira; i <= ultima; i++) {
		double y = cx.y + MARGEM_Y + (double)i * lh;
		size_t len;
		const char *s = sg_linha(d->b, i, &len);
		if (destaca_linha && !sel && i == d->cur.linha) {
			cor_cairo(cr, gv_t.texto, 0.045f);
			retangulo(cr, cx.x + dd->gutter, y, largura_area - dd->gutter, lh);
		}
		if (achando) {
			int ini[64], tam[64];
			int m = sg_buscar_linha(d->b, &q, i, ini, tam, 64);
			for (int k = 0; k < m; k++) {
				float xa = x_da_pos(d, (struct sg_pos){ i, ini[k] }), xb = x_da_pos(d, (struct sg_pos){ i, ini[k] + tam[k] });
				cor_cairo(cr, gv_t.aviso, 0.30f);
				retangulo(cr, x0 + xa, y + 2, xb - xa, lh - 4);
			}
		}
		if (sel && i >= sa.linha && i <= sz.linha) {
			int ca = i == sa.linha ? sa.col : 0, cz = i == sz.linha ? sz.col : (int)len;
			float xa = x_da_pos(d, (struct sg_pos){ i, ca }), xb = x_da_pos(d, (struct sg_pos){ i, cz });
			if (i < sz.linha) {
				xb += largura_espaco * 0.7f; /* a quebra de linha também está selecionada */
			}
			cor_cairo(cr, gv_t.acento, 0.50f);
			retangulo(cr, x0 + xa, y, xb - xa, lh);
		}
		PangoLayout *L = layout_de(s, len);
		pango_cairo_update_layout(cr, L);
		int tw, th;
		pango_layout_get_pixel_size(L, &tw, &th);
		if (tw + 8 > d->larg_max) {
			d->larg_max = (float)tw + 8;
		}
		cor_cairo(cr, gv_t.texto, 1);
		cairo_move_to(cr, x0, y + (lh - th) / 2.0);
		pango_cairo_show_layout(cr, L);
		if (dd->gutter > 0) {
			char num[16];
			int nn = snprintf(num, sizeof(num), "%d", i + 1);
			PangoLayout *N = layout_de(num, (size_t)nn);
			pango_cairo_update_layout(cr, N);
			int nw, nh;
			pango_layout_get_pixel_size(N, &nw, &nh);
			cor_cairo(cr, i == d->cur.linha ? gv_t.texto_secundario : gv_t.texto_suave, 1);
			cairo_move_to(cr, cx.x + dd->gutter - 14 - nw, y + (lh - nh) / 2.0);
			pango_cairo_show_layout(cr, N);
		}
	}
	/* o cursor */
	if (dd->cursor_visivel && d->cur.linha >= primeira && d->cur.linha <= ultima) {
		double y = cx.y + MARGEM_Y + (double)d->cur.linha * lh, x = x0 + x_da_pos(d, d->cur);
		cor_cairo(cr, gv_t.texto, 1);
		retangulo(cr, x - 1, y + 3, 2, lh - 6);
	}
}

/* a área de edição de um documento: rolável, com o texto desenhado à mão */
static void editor_area(struct doc *d, bool recebe_teclas) {
	float gutter = gutter_largura(d);
	int lh = altura_linha(), n = sg_linhas(d->b);
	Clay_ElementId rol = CLAY_IDI("sg-rol", (uint32_t)d->uid), cont = CLAY_IDI("sg-cont", (uint32_t)d->uid);
	Clay_BoundingBox vb = gv_caixa(rol);
	float vw = vb.width > 0 ? vb.width : 900, vh = vb.height > 0 ? vb.height : 400;
	float cw = fmaxf(vw, gutter + MARGEM_X * 2 + d->larg_max + 40), ch = (float)(n * lh + MARGEM_Y * 2) + vh * 0.35f;
	desenho_atual = (struct desenho){ .d = d, .gutter = gutter };
	double ocioso = gv_agora() - d->mexeu_em;
	desenho_atual.cursor_visivel = recebe_teclas && dialogo == DLG_NENHUM && fmod(ocioso, 1.06) < 0.53;
	mouse_editor(d, gutter);
	CLAY(CLAY_ID("sg-editor"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		gv_focavel(CLAY_ID("sg-editor"), 0);
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_AMBOS, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
			CLAY(cont, gv_livre(desenhar_editor, &desenho_atual, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(cw), CLAY_SIZING_FIXED(ch) } } })) {}
		}
	}
	/* seguir o cursor: rolar até ele aparecer (depois do layout, com a caixa de agora) */
	if (d->seguir && vb.width > 0) {
		Clay_Vector2 pos = gv_rolagem_posicao(rol);
		float cy = (float)(MARGEM_Y + d->cur.linha * lh), cxp = gutter + MARGEM_X + x_da_pos(d, d->cur);
		float ny = pos.y, nx = pos.x;
		if (cy < pos.y + 4) {
			ny = cy - 4;
		} else if (cy + lh > pos.y + vh - 4) {
			ny = cy + lh - vh + 4;
		}
		if (cxp - 20 < pos.x + gutter) {
			nx = fmaxf(0, cxp - gutter - 60);
		} else if (cxp + 30 > pos.x + vw) {
			nx = cxp + 30 - vw;
		}
		if (ny < 0) {
			ny = 0;
		}
		if (nx != pos.x || ny != pos.y) {
			gv_rolar_para(rol, nx, ny, false);
		}
		d->seguir = false;
	}
}

/* ---- a depuração dos testes ---- */

struct reg {
	char nome[40];
	Clay_ElementId id;
};
static struct reg regs[400];
static int n_regs;

static void reg(const char *nome, Clay_ElementId id) {
	if (depurando && n_regs < (int)(sizeof(regs) / sizeof(regs[0]))) {
		g_strlcpy(regs[n_regs].nome, nome, sizeof(regs[n_regs].nome));
		regs[n_regs++].id = id;
	}
}

static void debug_estado(void) {
	if (!depurando) {
		return;
	}
	static GHashTable *antes;
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	for (int i = 0; i < n_regs; i++) {
		Clay_BoundingBox b = gv_caixa(regs[i].id);
		if (b.width <= 0) {
			continue;
		}
		char *l = g_strdup_printf("%.0f %.0f %.0f %.0f", b.x, b.y, b.width, b.height);
		char *a = g_hash_table_lookup(antes, regs[i].nome);
		if (!a || strcmp(a, l)) {
			printf("pos %s %s\n", regs[i].nome, l);
			g_hash_table_insert(antes, g_strdup(regs[i].nome), l);
		} else {
			g_free(l);
		}
	}
	n_regs = 0;
	struct doc *d = doc_ativo();
	char *txt = NULL;
	if (d) {
		char *t = sg_texto_todo(d->b);
		GString *e = g_string_new(NULL);
		for (const char *p = t; *p && e->len < 160; p++) {
			if (*p == '\n') {
				g_string_append(e, "\\n");
			} else if (*p == '\t') {
				g_string_append(e, "\\t");
			} else if (*p == ' ') {
				g_string_append(e, "_");
			} else if (*p == '[' || *p == ']') {
				g_string_append_c(e, '(');
			} else {
				g_string_append_c(e, *p);
			}
		}
		g_free(t);
		txt = g_string_free(e, FALSE);
	}
	struct sg_pos a = { 0, 0 }, z = { 0, 0 };
	if (d) {
		selecao(d, &a, &z);
		recontar(d);
	}
	char *abas = g_strdup("");
	for (int i = 0; i < ndocs; i++) {
		char *n = g_strdup_printf("%s%s%s", abas, i ? "|" : "", rotulo(docs[i]));
		g_free(abas);
		abas = n;
	}
	char *est = g_strdup_printf("estado abas=%d ativa=%d linha=%d col=%d sel=%d,%d-%d,%d mod=%d busca=%d total=%d idx=%d zoom=%d dlg=%d fim=%s cod=%s bom=%d nomes=[%s] txt=[%s] msg=[%s]", ndocs, ativo,
		d ? d->cur.linha + 1 : 0, d ? sg_coluna(d->b, d->cur) + 1 : 0, a.linha, a.col, z.linha, z.col, d ? sg_modificado(d->b) : 0, f_aberta, cache_total, cache_indice, zoom_percentual(), dialogo,
		d && sg_fim_de_linha(d->b) == SG_CRLF ? "CRLF" : "LF", d && sg_codificacao(d->b) == SG_LATIN1 ? "Latin-1" : "UTF-8", d ? sg_tem_bom(d->b) : 0, abas, txt ? txt : "",
		mensagem_ate > gv_agora() ? mensagem : "");
	static char *ultimo;
	if (!ultimo || strcmp(ultimo, est)) {
		printf("%s\n", est);
		g_free(ultimo);
		ultimo = est;
	} else {
		g_free(est);
	}
	g_free(abas);
	g_free(txt);
	fflush(stdout);
}

/* ---- as ações (menus, botões e atalhos fazem a mesma coisa) ---- */

enum acao {
	A_NOVO, A_ABRIR, A_SALVAR, A_SALVAR_COMO, A_FECHAR_ABA, A_SAIR, A_DESFAZER, A_REFAZER, A_RECORTAR, A_COPIAR, A_COLAR, A_TUDO, A_BUSCAR, A_SUBSTITUIR, A_IR_LINHA, A_ZOOM_MAIS, A_ZOOM_MENOS,
	A_ZOOM_100, A_NUMEROS, A_MONO, A_DESTACAR, A_PROXIMA_ABA, A_ABA_ANTERIOR
};

static void executar(enum acao a) {
	struct doc *d = doc_ativo();
	if (!d) {
		return;
	}
	switch (a) {
	case A_NOVO:
		doc_adicionar(doc_novo());
		break;
	case A_ABRIR:
		pedir_abrir();
		break;
	case A_SALVAR:
		salvar(d, false, false);
		break;
	case A_SALVAR_COMO:
		salvar_como(d, false, false);
		break;
	case A_FECHAR_ABA:
		pedir_fechar_aba(ativo);
		break;
	case A_SAIR:
		pedir_sair();
		break;
	case A_DESFAZER:
		if (sg_desfazer(d->b, &d->cur)) {
			d->anc = d->cur;
			d->seguir = true;
		}
		break;
	case A_REFAZER:
		if (sg_refazer(d->b, &d->cur)) {
			d->anc = d->cur;
			d->seguir = true;
		}
		break;
	case A_RECORTAR:
	case A_COPIAR:
		if (tem_selecao(d)) {
			char *t = texto_selecionado(d);
			gv_copiar(t);
			g_free(t);
			if (a == A_RECORTAR) {
				apagar_selecao(d);
			}
		}
		break;
	case A_COLAR: {
		const char *t = gv_colar();
		if (t && *t) {
			sg_fechar_grupo(d->b);
			inserir_texto(d, t, strlen(t), false);
			sg_fechar_grupo(d->b);
		}
		break;
	}
	case A_TUDO:
		d->anc = (struct sg_pos){ 0, 0 };
		d->cur = sg_pos_fim(d->b);
		break;
	case A_BUSCAR:
		if (f_aberta && !f_subst) {
			pedir_foco_busca = true;
		} else {
			abrir_busca(false);
		}
		break;
	case A_SUBSTITUIR:
		abrir_busca(true);
		break;
	case A_IR_LINHA:
		ir_linha_txt[0] = '\0';
		dialogo = DLG_IR_LINHA;
		break;
	case A_ZOOM_MAIS:
		zoom_mudar(zoom_px + (zoom_px >= 24 ? 2 : 1));
		break;
	case A_ZOOM_MENOS:
		zoom_mudar(zoom_px - (zoom_px > 24 ? 2 : 1));
		break;
	case A_ZOOM_100:
		zoom_mudar(ZOOM_PADRAO);
		break;
	case A_NUMEROS:
		numeros_linha = !numeros_linha;
		prefs_gravar();
		break;
	case A_MONO:
		fonte_mono = !fonte_mono;
		prefs_gravar();
		for (int i = 0; i < ndocs; i++) {
			docs[i]->larg_max = 0;
		}
		break;
	case A_DESTACAR:
		destaca_linha = !destaca_linha;
		prefs_gravar();
		break;
	case A_PROXIMA_ABA:
		doc_ativar((ativo + 1) % ndocs);
		break;
	case A_ABA_ANTERIOR:
		doc_ativar((ativo + ndocs - 1) % ndocs);
		break;
	}
}

/* ---- as teclas: as de atalho primeiro, depois as do editor ---- */

static void teclas(bool editor_recebe) {
	struct doc *d = doc_ativo();
	uint32_t mods = gv_modificadores();
	bool ctrl = mods & GV_MOD_CTRL, shift = mods & GV_MOD_SHIFT, alt = mods & GV_MOD_ALT;
	if (editor_recebe) {
		gv_teclado_cru(); /* o Tab e as setas são do editor, não andam o foco */
	}
	for (int i = 0;; i++) {
		uint32_t s = gv_tecla_n(i);
		if (!s) {
			break;
		}
		if (tecla_so_modificador(s)) {
			continue;
		}
		const char *txt = gv_tecla_texto_n(i);
		if (dialogo != DLG_NENHUM) {
			continue; /* os diálogos tratam as próprias teclas */
		}
		if (ctrl && !alt) {
			uint32_t k = s >= 'A' && s <= 'Z' ? s + 32 : s;
			bool tratada = true;
			switch (k) {
			case 'n': executar(A_NOVO); break;
			case 'o': executar(A_ABRIR); break;
			case 's': executar(shift ? A_SALVAR_COMO : A_SALVAR); break;
			case 'w': executar(A_FECHAR_ABA); break;
			case 'q': executar(A_SAIR); break;
			case 'f': executar(A_BUSCAR); break;
			case 'h': executar(A_SUBSTITUIR); break;
			case 'g': executar(A_IR_LINHA); break;
			case XKB_KEY_plus:
			case XKB_KEY_equal:
			case XKB_KEY_KP_Add: executar(A_ZOOM_MAIS); break;
			case XKB_KEY_minus:
			case XKB_KEY_KP_Subtract: executar(A_ZOOM_MENOS); break;
			case '0':
			case XKB_KEY_KP_0: executar(A_ZOOM_100); break;
			case XKB_KEY_Tab: executar(A_PROXIMA_ABA); break;
			case XKB_KEY_ISO_Left_Tab: executar(A_ABA_ANTERIOR); break;
			case XKB_KEY_Page_Down: executar(A_PROXIMA_ABA); break;
			case XKB_KEY_Page_Up: executar(A_ABA_ANTERIOR); break;
			default: tratada = false; break;
			}
			if (tratada) {
				d = doc_ativo();
				continue;
			}
		}
		if (s == XKB_KEY_F3) {
			if (d) {
				achar(d, !shift, false);
			}
			continue;
		}
		if (s == XKB_KEY_Escape && f_aberta) {
			fechar_busca();
			continue;
		}
		if (editor_recebe && d) {
			tecla_editor(d, s, txt, mods);
		}
	}
	/* Ctrl + roda: o zoom */
	float roda = gv_roda();
	if (ctrl && roda != 0 && gv_por_cima(CLAY_ID("sg-editor"))) {
		static float resto;
		resto += roda;
		int passos = (int)(resto / 15.0f);
		resto -= (float)passos * 15.0f;
		if (passos != 0) {
			zoom_mudar(zoom_px - passos);
		}
	}
}

/* ---- peças da janela ---- */

static void menu_texto(int i, const char *rot, const char *nome_reg) {
	Clay_ElementId id = CLAY_IDI("sg-mb", (uint32_t)i), pop = CLAY_IDI("sg-mp", (uint32_t)i);
	reg(nome_reg, id);
	bool aberto = gv_popup_aberto(pop);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(36) }, .padding = { 14, 14, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = aberto ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
		gv_texto(gv_str(rot), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
	}
	if (gv_tocado(id)) {
		gv_popup_alternar(pop);
	}
}

#define ITEM(idn, icone, texto, atalho, acao) \
	if (gv_menu_item(CLAY_ID(idn), icone, texto, atalho)) { \
		gv_popup_fechar(pop); \
		executar(acao); \
	}
#define MARCADO(idn, texto, marcado, acao) \
	if (gv_menu_item_ext(CLAY_ID(idn), marcado ? "check" : NULL, gv_t.acento, texto, NULL, marcado)) { \
		gv_popup_fechar(pop); \
		executar(acao); \
	}

static void menus(void) {
	Clay_ElementId pop = CLAY_IDI("sg-mp", 0);
	GV_POPUP(pop, CLAY_IDI("sg-mb", 0), .largura = 320) {
		CLAY(CLAY_ID("sg-m0"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			ITEM("sg-i-novo", "plus", "Novo", "Ctrl+N", A_NOVO)
			ITEM("sg-i-abrir", "folder", "Abrir…", "Ctrl+O", A_ABRIR)
			ITEM("sg-i-salvar", "save", "Salvar", "Ctrl+S", A_SALVAR)
			ITEM("sg-i-salvar-como", "save", "Salvar como…", "Ctrl+Shift+S", A_SALVAR_COMO)
			ITEM("sg-i-fechar", "close", "Fechar a aba", "Ctrl+W", A_FECHAR_ABA)
			ITEM("sg-i-sair", "logout", "Sair", "Ctrl+Q", A_SAIR)
		}
	}
	pop = CLAY_IDI("sg-mp", 1);
	GV_POPUP(pop, CLAY_IDI("sg-mb", 1), .largura = 320) {
		CLAY(CLAY_ID("sg-m1"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			ITEM("sg-i-desfazer", "undo", "Desfazer", "Ctrl+Z", A_DESFAZER)
			ITEM("sg-i-refazer", "redo", "Refazer", "Ctrl+Y", A_REFAZER)
			gv_separador(false, 0);
			ITEM("sg-i-recortar", "cut", "Recortar", "Ctrl+X", A_RECORTAR)
			ITEM("sg-i-copiar", "copy", "Copiar", "Ctrl+C", A_COPIAR)
			ITEM("sg-i-colar", "paste", "Colar", "Ctrl+V", A_COLAR)
			ITEM("sg-i-tudo", "grid", "Selecionar tudo", "Ctrl+A", A_TUDO)
			gv_separador(false, 0);
			ITEM("sg-i-buscar", "search", "Buscar", "Ctrl+F", A_BUSCAR)
			ITEM("sg-i-substituir", "repeat", "Substituir", "Ctrl+H", A_SUBSTITUIR)
			ITEM("sg-i-linha", "enter", "Ir para a linha…", "Ctrl+G", A_IR_LINHA)
		}
	}
	pop = CLAY_IDI("sg-mp", 2);
	GV_POPUP(pop, CLAY_IDI("sg-mb", 2), .largura = 320) {
		CLAY(CLAY_ID("sg-m2"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			ITEM("sg-i-zmais", "zoom-in", "Aumentar a fonte", "Ctrl++", A_ZOOM_MAIS)
			ITEM("sg-i-zmenos", "zoom-out", "Diminuir a fonte", "Ctrl+-", A_ZOOM_MENOS)
			ITEM("sg-i-z100", "refresh", "Tamanho padrão", "Ctrl+0", A_ZOOM_100)
			gv_separador(false, 0);
			MARCADO("sg-i-numeros", "Números de linha", numeros_linha, A_NUMEROS)
			MARCADO("sg-i-mono", "Fonte monoespaçada", fonte_mono, A_MONO)
			MARCADO("sg-i-destacar", "Destacar a linha atual", destaca_linha, A_DESTACAR)
		}
	}
}

static void ajustes_popup(void) {
	Clay_ElementId pop = CLAY_ID("sg-pop-ajustes");
	GV_POPUP(pop, CLAY_ID("sg-b-ajustes"), .alinhar = GV_ALINHAR_FIM, .largura = 340) {
		CLAY(CLAY_ID("sg-aj"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 12 } }) {
			gv_texto(CLAY_STRING("Aparência do texto"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			CLAY(CLAY_ID("sg-aj-zoom"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(gv_fmt("Tamanho da fonte: %d%%", zoom_percentual()).chars), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				if (gv_botao_ext(CLAY_ID("sg-aj-menos"), &(gv_botao_opcoes){ .icone = "minus", .estilo = GV_BOTAO_ELEVADO, .altura = 36 })) {
					executar(A_ZOOM_MENOS);
				}
				if (gv_botao_ext(CLAY_ID("sg-aj-mais"), &(gv_botao_opcoes){ .icone = "plus", .estilo = GV_BOTAO_ELEVADO, .altura = 36 })) {
					executar(A_ZOOM_MAIS);
				}
			}
			bool n = numeros_linha, m = fonte_mono, h = destaca_linha;
			if (gv_linha(CLAY_ID("sg-aj-num"), "list", "Números de linha", NULL, &n)) {
				executar(A_NUMEROS);
			}
			if (gv_linha(CLAY_ID("sg-aj-mono"), "terminal", "Fonte monoespaçada", NULL, &m)) {
				executar(A_MONO);
			}
			if (gv_linha(CLAY_ID("sg-aj-hl"), "eye", "Destacar a linha atual", NULL, &h)) {
				executar(A_DESTACAR);
			}
		}
	}
}

static void barra_menus(void) {
	struct doc *d = doc_ativo();
	CLAY(CLAY_ID("sg-menus"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48) }, .padding = { 14, 14, 0, 0 }, .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		menu_texto(0, "Arquivo", "menu/arquivo");
		menu_texto(1, "Editar", "menu/editar");
		menu_texto(2, "Exibir", "menu/exibir");
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		if (gv_botao_ext(CLAY_ID("sg-b-busca"), &(gv_botao_opcoes){ .icone = "search", .estilo = f_aberta ? GV_BOTAO_ELEVADO : GV_BOTAO_SIMPLES, .altura = 38 })) {
			if (f_aberta) {
				fechar_busca();
			} else {
				abrir_busca(false);
			}
		}
		reg("btn/busca", CLAY_ID("sg-b-busca"));
		if (gv_botao_ext(CLAY_ID("sg-b-salvar"), &(gv_botao_opcoes){ .icone = "save", .estilo = GV_BOTAO_SIMPLES, .altura = 38, .desativado = !d || (!sg_modificado(d->b) && sg_caminho(d->b)) })) {
			executar(A_SALVAR);
		}
		reg("btn/salvar", CLAY_ID("sg-b-salvar"));
		if (gv_botao_ext(CLAY_ID("sg-b-ajustes"), &(gv_botao_opcoes){ .icone = "settings", .estilo = GV_BOTAO_SIMPLES, .altura = 38 })) {
			gv_popup_alternar(CLAY_ID("sg-pop-ajustes"));
		}
		reg("btn/ajustes", CLAY_ID("sg-b-ajustes"));
	}
	menus();
	ajustes_popup();
}

static void abas(void) {
	CLAY(CLAY_ID("sg-abas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 14, 14, 6, 0 }, .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		for (int i = 0; i < ndocs; i++) {
			struct doc *d = docs[i];
			Clay_ElementId id = CLAY_IDI("sg-aba", (uint32_t)i), x = CLAY_IDI("sg-aba-x", (uint32_t)i);
			bool ativa = i == ativo;
			char nome[24];
			snprintf(nome, sizeof(nome), "aba/%d", i);
			reg(nome, id);
			snprintf(nome, sizeof(nome), "aba-x/%d", i);
			reg(nome, x);
			if (gv_tocado(id)) {
				doc_ativar(i);
			}
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(90, 230), CLAY_SIZING_FIXED(38) }, .padding = { 16, 8, 0, 0 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = ativa ? gv_alfa(gv_t.acento, 0.12f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 },
				.cornerRadius = { .topLeft = gv_t.raio_pequeno, .topRight = gv_t.raio_pequeno }, .border = { .color = gv_t.acento, .width = { .bottom = ativa ? 2 : 0 } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(rotulo(d)), gv_t.tamanho_corpo, ativa ? gv_t.texto : gv_t.texto_secundario, ativa ? GV_PESO_MEDIO : GV_PESO_NORMAL);
				}
				if (sg_modificado(d->b)) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8) } }, .backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
				}
				CLAY(x, { .layout = { .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = gv_por_cima(x) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(12) }) {
					gv_icone("close", 14, gv_t.texto_secundario);
				}
			}
			if (gv_tocado(x)) {
				pedir_fechar_aba(i);
				break; /* a lista mudou: o resto no próximo quadro */
			}
		}
		if (gv_botao_ext(CLAY_ID("sg-aba-nova"), &(gv_botao_opcoes){ .icone = "plus", .estilo = GV_BOTAO_SIMPLES, .altura = 34 })) {
			executar(A_NOVO);
		}
		reg("aba-nova", CLAY_ID("sg-aba-nova"));
	}
}

static void chip_opcao(const char *id_s, const char *nome_reg, const char *texto, bool *ligado, const char *dica_mono) {
	Clay_ElementId id = CLAY_SID(gv_str(id_s));
	(void)dica_mono;
	reg(nome_reg, id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = *ligado ? gv_alfa(gv_t.acento, 0.25f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = *ligado ? gv_t.acento : gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, *ligado ? gv_t.texto : gv_t.texto_secundario, GV_PESO_MEDIO);
	}
	if (gv_tocado(id)) {
		*ligado = !*ligado;
	}
}

static void barra_busca(void) {
	struct doc *d = doc_ativo();
	if (!f_aberta || !d) {
		return;
	}
	recontar(d);
	CLAY(CLAY_ID("sg-busca-barra"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 10, 10 }, .childGap = 8 },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		CLAY(CLAY_ID("sg-busca-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("sg-busca-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				int r = gv_campo(CLAY_ID("sg-busca"), f_busca, sizeof(f_busca), "Buscar", GV_CAMPO_BUSCA);
				if (r & GV_CAMPO_MUDOU) {
					recontar(d);
					achar(d, true, true);
				}
				if (r & GV_CAMPO_ENTER) {
					achar(d, !(gv_modificadores() & GV_MOD_SHIFT), false);
				}
				/* o "1 de 3" dentro do campo, à direita */
				CLAY(CLAY_ID("sg-contador"), { .layout = { .padding = { 0, 40, 0, 0 } },
					.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_RIGHT_CENTER, CLAY_ATTACH_POINT_RIGHT_CENTER }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
						.zIndex = 5 } }) {
					const char *t = !f_busca[0] ? "" : cache_total == 0 ? "Nenhum resultado" : cache_indice > 0 ? gv_fmt("%d de %d", cache_indice, cache_total).chars : gv_fmt("%d resultados", cache_total).chars;
					gv_texto(gv_str(t), gv_t.tamanho_corpo - 1, f_busca[0] && cache_total == 0 ? gv_t.aviso : gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			reg("busca", CLAY_ID("sg-busca"));
			chip_opcao("sg-op-maius", "busca/maius", "Aa", &f_maius, NULL);
			chip_opcao("sg-op-palavra", "busca/palavra", "ab", &f_palavra, NULL);
			if (gv_botao_ext(CLAY_ID("sg-b-ant"), &(gv_botao_opcoes){ .icone = "chevron-up", .estilo = GV_BOTAO_ELEVADO, .altura = 40, .desativado = !cache_total })) {
				achar(d, false, false);
			}
			reg("busca/ant", CLAY_ID("sg-b-ant"));
			if (gv_botao_ext(CLAY_ID("sg-b-prox"), &(gv_botao_opcoes){ .icone = "chevron-down", .estilo = GV_BOTAO_ELEVADO, .altura = 40, .desativado = !cache_total })) {
				achar(d, true, false);
			}
			reg("busca/prox", CLAY_ID("sg-b-prox"));
			if (gv_botao_ext(CLAY_ID("sg-b-fechar-busca"), &(gv_botao_opcoes){ .icone = "close", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
				fechar_busca();
			}
			reg("busca/fechar", CLAY_ID("sg-b-fechar-busca"));
		}
		if (f_subst) {
			CLAY(CLAY_ID("sg-subst-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY(CLAY_ID("sg-subst-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					int r = gv_campo(CLAY_ID("sg-subst"), f_subst_txt, sizeof(f_subst_txt), "Substituir por", GV_CAMPO_TEXTO);
					if (r & GV_CAMPO_ENTER) {
						substituir_atual(d);
					}
				}
				reg("subst", CLAY_ID("sg-subst"));
				if (gv_botao_ext(CLAY_ID("sg-b-subst-um"), &(gv_botao_opcoes){ .texto = "Substituir", .estilo = GV_BOTAO_ELEVADO, .altura = 40, .desativado = !cache_total })) {
					substituir_atual(d);
				}
				reg("subst/um", CLAY_ID("sg-b-subst-um"));
				if (gv_botao_ext(CLAY_ID("sg-b-subst-todos"), &(gv_botao_opcoes){ .texto = "Substituir tudo", .estilo = GV_BOTAO_CHEIO, .altura = 40, .desativado = !cache_total })) {
					substituir_tudo(d);
				}
				reg("subst/todos", CLAY_ID("sg-b-subst-todos"));
			}
		}
	}
	if (pedir_foco_busca) {
		gv_focar(CLAY_ID("sg-busca"));
		pedir_foco_busca = false;
	}
}

static void chip_estado(const char *id_s, const char *nome_reg, const char *texto, Clay_ElementId pop) {
	Clay_ElementId id = CLAY_SID(gv_str(id_s));
	reg(nome_reg, id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(28) }, .padding = { 10, 10, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) || gv_popup_aberto(pop) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	if (gv_tocado(id)) {
		gv_popup_alternar(pop);
	}
}

static void divisor_estado(void) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(14) } }, .backgroundColor = gv_t.borda_controle }) {}
}

static void barra_estado(void) {
	struct doc *d = doc_ativo();
	if (!d) {
		return;
	}
	struct sg_pos a, z;
	selecao(d, &a, &z);
	const char *esq = gv_fmt("Linha %d, coluna %d", d->cur.linha + 1, sg_coluna(d->b, d->cur) + 1).chars;
	if (tem_selecao(d)) {
		char *t = sg_texto(d->b, a, z);
		size_t chars = g_utf8_strlen(t, -1);
		g_free(t);
		esq = gv_fmt("Linha %d, coluna %d  ·  %zu selecionados", d->cur.linha + 1, sg_coluna(d->b, d->cur) + 1, chars).chars;
	}
	bool msg = mensagem[0] && gv_agora() < mensagem_ate;
	const char *cod = sg_codificacao(d->b) == SG_LATIN1 ? "Latin-1" : sg_tem_bom(d->b) ? "UTF-8 com BOM" : "UTF-8";
	CLAY(CLAY_ID("sg-estado"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 20, 14, 0, 0 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(msg ? mensagem : esq), gv_t.tamanho_corpo - 2, msg ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		chip_estado("sg-e-cod", "status/codif", cod, CLAY_ID("sg-pop-cod"));
		divisor_estado();
		chip_estado("sg-e-fim", "status/fim", sg_fim_de_linha(d->b) == SG_CRLF ? "CRLF" : "LF", CLAY_ID("sg-pop-fim"));
		divisor_estado();
		CLAY_AUTO_ID({ .layout = { .padding = { 10, 10, 0, 0 } } }) {
			gv_texto(CLAY_STRING("Texto simples"), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		divisor_estado();
		chip_estado("sg-e-zoom", "status/zoom", gv_fmt("%d%%", zoom_percentual()).chars, CLAY_ID("sg-pop-zoom"));
	}
	GV_POPUP(CLAY_ID("sg-pop-cod"), CLAY_ID("sg-e-cod"), .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_FIM, .largura = 240) {
		CLAY(CLAY_ID("sg-pc"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			bool u = sg_codificacao(d->b) == SG_UTF8 && !sg_tem_bom(d->b), ub = sg_codificacao(d->b) == SG_UTF8 && sg_tem_bom(d->b), l = sg_codificacao(d->b) == SG_LATIN1;
			if (gv_menu_item_ext(CLAY_ID("sg-c-utf8"), u ? "check" : NULL, gv_t.acento, "UTF-8", NULL, u)) {
				sg_definir_codificacao(d->b, SG_UTF8);
				sg_definir_bom(d->b, false);
				gv_popup_fechar(CLAY_ID("sg-pop-cod"));
			}
			if (gv_menu_item_ext(CLAY_ID("sg-c-bom"), ub ? "check" : NULL, gv_t.acento, "UTF-8 com BOM", NULL, ub)) {
				sg_definir_codificacao(d->b, SG_UTF8);
				sg_definir_bom(d->b, true);
				gv_popup_fechar(CLAY_ID("sg-pop-cod"));
			}
			if (gv_menu_item_ext(CLAY_ID("sg-c-latin1"), l ? "check" : NULL, gv_t.acento, "Latin-1", NULL, l)) {
				sg_definir_codificacao(d->b, SG_LATIN1);
				sg_definir_bom(d->b, false);
				gv_popup_fechar(CLAY_ID("sg-pop-cod"));
			}
		}
	}
	GV_POPUP(CLAY_ID("sg-pop-fim"), CLAY_ID("sg-e-fim"), .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_FIM, .largura = 240) {
		CLAY(CLAY_ID("sg-pf"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			bool lf = sg_fim_de_linha(d->b) == SG_LF;
			if (gv_menu_item_ext(CLAY_ID("sg-f-lf"), lf ? "check" : NULL, gv_t.acento, "LF (Linux, macOS)", NULL, lf)) {
				sg_definir_fim_de_linha(d->b, SG_LF);
				gv_popup_fechar(CLAY_ID("sg-pop-fim"));
			}
			if (gv_menu_item_ext(CLAY_ID("sg-f-crlf"), !lf ? "check" : NULL, gv_t.acento, "CRLF (Windows)", NULL, !lf)) {
				sg_definir_fim_de_linha(d->b, SG_CRLF);
				gv_popup_fechar(CLAY_ID("sg-pop-fim"));
			}
		}
	}
	GV_POPUP(CLAY_ID("sg-pop-zoom"), CLAY_ID("sg-e-zoom"), .lado = GV_POPUP_ACIMA, .alinhar = GV_ALINHAR_FIM, .largura = 240) {
		CLAY(CLAY_ID("sg-pz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			if (gv_menu_item(CLAY_ID("sg-z-mais"), "zoom-in", "Aumentar", "Ctrl++")) {
				executar(A_ZOOM_MAIS);
			}
			if (gv_menu_item(CLAY_ID("sg-z-menos"), "zoom-out", "Diminuir", "Ctrl+-")) {
				executar(A_ZOOM_MENOS);
			}
			if (gv_menu_item(CLAY_ID("sg-z-100"), "refresh", "100%", "Ctrl+0")) {
				executar(A_ZOOM_100);
				gv_popup_fechar(CLAY_ID("sg-pop-zoom"));
			}
		}
	}
}

/* ---- os diálogos (por cima de tudo) ---- */

static void cartao_dialogo(const char *id_s, float largura, const char *titulo, void (*corpo)(void), void (*botoes)(void)) {
	Clay_ElementId id = CLAY_SID(gv_str(id_s));
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_IDI("sg-dlg-cartao", id.id), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIT(0) },
			.padding = CLAY_PADDING_ALL(22), .childGap = 14 }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
			.border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			corpo();
			CLAY(CLAY_IDI("sg-dlg-botoes", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				botoes();
			}
		}
	}
}

static void texto_dlg(const char *t) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
		gv_texto(gv_str(t), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static bool botao_dlg(const char *id_s, const char *texto, int estilo) {
	return gv_botao_ext(CLAY_SID(gv_str(id_s)), &(gv_botao_opcoes){ .texto = texto, .estilo = estilo, .altura = 40 });
}

static void fechar_dlg(void) {
	dialogo = DLG_NENHUM;
	dlg_aba = -1;
	pedir_foco_editor = true;
}

/* sair/fechar aba com alterações: salvar, descartar ou cancelar */
static void corpo_alteracoes(void) {
	struct doc *d = dlg_aba >= 0 && dlg_aba < ndocs ? docs[dlg_aba] : NULL;
	texto_dlg(gv_fmt("“%s” tem alterações que ainda não foram salvas.", d ? rotulo(d) : "").chars);
}

static void botoes_alteracoes(void) {
	bool sair = dialogo == DLG_SAIR;
	struct doc *d = dlg_aba >= 0 && dlg_aba < ndocs ? docs[dlg_aba] : NULL;
	if (botao_dlg("sg-d-cancelar", "Cancelar", GV_BOTAO_ELEVADO)) {
		sair_pendente = false;
		fechar_dlg();
		return;
	}
	if (botao_dlg("sg-d-descartar", "Descartar", GV_BOTAO_ELEVADO) && d) {
		int i = dlg_aba;
		fechar_dlg();
		doc_remover(i);
		if (sair) {
			continuar_saida();
		}
		return;
	}
	if (botao_dlg("sg-d-salvar", "Salvar", GV_BOTAO_CHEIO) && d) {
		fechar_dlg();
		salvar(d, sair, !sair);
	}
}

static void corpo_mudou(void) {
	struct doc *d = doc_ativo();
	texto_dlg(gv_fmt("“%s” foi alterado por outro programa, e você também mexeu nele aqui.", d ? rotulo(d) : "").chars);
}

static void botoes_mudou(void) {
	struct doc *d = doc_ativo();
	if (!d) {
		fechar_dlg();
		return;
	}
	if (botao_dlg("sg-d-manter", "Manter o meu", GV_BOTAO_ELEVADO)) {
		sg_ignorar_mudanca_no_disco(d->b);
		fechar_dlg();
		return;
	}
	if (botao_dlg("sg-d-recarregar", "Recarregar do disco", GV_BOTAO_CHEIO)) {
		fechar_dlg();
		recarregar(d);
	}
}

static void corpo_erro(void) {
	texto_dlg(dlg_texto);
}

static void botoes_erro(void) {
	if (botao_dlg("sg-d-ok", "Ok", GV_BOTAO_CHEIO)) {
		sair_pendente = false;
		fechar_dlg();
	}
}

static void ir_para_linha(struct doc *d) {
	int n = atoi(ir_linha_txt);
	if (n < 1) {
		return;
	}
	if (n > sg_linhas(d->b)) {
		n = sg_linhas(d->b);
	}
	d->cur = d->anc = (struct sg_pos){ n - 1, 0 };
	d->seguir = true;
	fechar_dlg();
}

static void corpo_ir_linha(void) {
	struct doc *d = doc_ativo();
	texto_dlg(gv_fmt("Linha de 1 a %d.", d ? sg_linhas(d->b) : 1).chars);
	CLAY(CLAY_ID("sg-ir-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
		int r = gv_campo(CLAY_ID("sg-ir-campo"), ir_linha_txt, sizeof(ir_linha_txt), "Número da linha", GV_CAMPO_NUMERO);
		static bool focou;
		if (!focou) {
			gv_focar(CLAY_ID("sg-ir-campo"));
			focou = true;
		}
		if ((r & GV_CAMPO_ENTER) && d) {
			ir_para_linha(d);
			focou = false;
		}
	}
	reg("dlg/ir-campo", CLAY_ID("sg-ir-campo"));
}

static void botoes_ir_linha(void) {
	if (botao_dlg("sg-d-ir-cancelar", "Cancelar", GV_BOTAO_ELEVADO)) {
		fechar_dlg();
		return;
	}
	struct doc *d = doc_ativo();
	if (botao_dlg("sg-d-ir-ok", "Ir", GV_BOTAO_CHEIO) && d) {
		ir_para_linha(d);
	}
}

static void dialogos(void) {
	switch (dialogo) {
	case DLG_NENHUM:
		break;
	case DLG_FECHAR_ABA:
		cartao_dialogo("sg-dlg-fechar", 440, "Salvar as alterações?", corpo_alteracoes, botoes_alteracoes);
		break;
	case DLG_SAIR:
		cartao_dialogo("sg-dlg-sair", 440, "Salvar antes de sair?", corpo_alteracoes, botoes_alteracoes);
		break;
	case DLG_MUDOU:
	case DLG_RECARREGAR:
		cartao_dialogo("sg-dlg-mudou", 460, "O arquivo mudou no disco", corpo_mudou, botoes_mudou);
		break;
	case DLG_IR_LINHA:
		cartao_dialogo("sg-dlg-ir", 360, "Ir para a linha", corpo_ir_linha, botoes_ir_linha);
		break;
	case DLG_ERRO:
		cartao_dialogo("sg-dlg-erro", 460, "Algo deu errado", corpo_erro, botoes_erro);
		break;
	}
	/* Esc cancela qualquer diálogo (menos o de erro, que só tem Ok) */
	if (dialogo != DLG_NENHUM) {
		for (int i = 0;; i++) {
			uint32_t s = gv_tecla_n(i);
			if (!s) {
				break;
			}
			if (s == XKB_KEY_Escape) {
				if (dialogo == DLG_SAIR || dialogo == DLG_FECHAR_ABA) {
					sair_pendente = false;
				}
				if (dialogo == DLG_MUDOU || dialogo == DLG_RECARREGAR) {
					struct doc *d = doc_ativo();
					if (d) {
						sg_ignorar_mudanca_no_disco(d->b);
					}
				}
				fechar_dlg();
				break;
			}
		}
	}
}

/* ---- o quadro ---- */

static void desenhar(gv_app *a, void *dados) {
	(void)a;
	(void)dados;
	double t = gv_agora();
	if (t - ultima_conferencia > 2.0) {
		ultima_conferencia = t;
		conferir_disco();
	}
	bool popup = false;
	for (int i = 0; i < 3; i++) {
		popup = popup || gv_popup_aberto(CLAY_IDI("sg-mp", (uint32_t)i));
	}
	popup = popup || gv_popup_aberto(CLAY_ID("sg-pop-ajustes")) || gv_popup_aberto(CLAY_ID("sg-pop-cod")) || gv_popup_aberto(CLAY_ID("sg-pop-fim")) || gv_popup_aberto(CLAY_ID("sg-pop-zoom"));
	bool campo = gv_focado(CLAY_ID("sg-busca")) || gv_focado(CLAY_ID("sg-subst"));
	bool editor_recebe = dialogo == DLG_NENHUM && !popup && !campo;
	teclas(editor_recebe);
	struct doc *d = doc_ativo();
	CLAY(CLAY_ID("sg-raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.fundo }) {
		gv_barra_janela(app, CLAY_ID("sg-barra"), "document", "Signals");
		barra_menus();
		abas();
		barra_busca();
		CLAY(CLAY_ID("sg-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			if (d) {
				editor_area(d, editor_recebe);
			}
		}
		barra_estado();
	}
	dialogos();
	if (pedir_foco_editor && dialogo == DLG_NENHUM) {
		gv_focar(CLAY_ID("sg-editor"));
		pedir_foco_editor = false;
	}
	/* o cursor pisca e a mensagem some: acordar no próximo meio segundo */
	gv_acordar_em(0.25);
	debug_estado();
}

static void abrir_argumento(const char *arg) {
	if (!arg || !*arg) {
		return;
	}
	char *caminho = NULL;
	if (g_str_has_prefix(arg, "file://")) {
		caminho = g_filename_from_uri(arg, NULL, NULL);
	}
	if (!caminho) {
		caminho = g_strdup(arg);
	}
	if (!g_path_is_absolute(caminho)) {
		char *cwd = g_get_current_dir();
		char *abs = g_build_filename(cwd, caminho, NULL);
		g_free(cwd);
		g_free(caminho);
		caminho = abs;
	}
	abrir_arquivo(caminho);
	g_free(caminho);
}

int main(int argc, char **argv) {
	depurando = getenv("LUMNA_SIGNALS_DEBUG") != NULL;
	if (depurando) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	prefs_ler();
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Signals", .app_id = "lumna-signals", .tipo = GV_JANELA, .largura = 1100, .altura = 760 });
	if (!app) {
		return 1;
	}
	gv_app_ao_fechar(app, ao_fechar_janela, NULL);
	int abertos = 0;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1]) {
			continue;
		}
		abrir_argumento(argv[i]);
		abertos++;
	}
	if (ndocs == 0) {
		doc_adicionar(doc_novo());
	}
	(void)abertos;
	pedir_foco_editor = true;
	titulo_da_janela();
	return gv_app_correr(app, desenhar, NULL);
}
