/* O LUMNA Music: a janela, o motor (fila, eventos do mpv, MPRIS), as capas, as playlists e a depuração. As telas estão nos outros arquivos (app.h). */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

estado M;

#define MIME_URIS "text/uri-list"

static char aviso_txt[256];
static double aviso_ate;

void aviso(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	g_vsnprintf(aviso_txt, sizeof(aviso_txt), fmt, ap);
	va_end(ap);
	aviso_ate = gv_agora() + 5;
	gv_acordar_em(5.1);
	gv_app_redesenhar(M.app);
}

const char *aviso_atual(void) {
	return aviso_txt[0] && gv_agora() < aviso_ate ? aviso_txt : NULL;
}

/* ---------------- capas (em threads) ---------------- */

typedef struct capa {
	char *arq;       /* "" = não tem */
	bool pronta;
	float r, g, b;   /* a cor média, para o brilho da tela Ouvir agora */
} capa;

static GHashTable *capas;   /* chave -> capa* */

static char *chave_da_capa(const mu_faixa *f) {
	return g_strdup_printf("%s\x01%s\x01%s", f->pasta, f->album, f->album_artista);
}

typedef struct {
	char *chave;
	mu_faixa f;
	char *arq;
	float r, g, b;
} capa_job;

static void capa_chegou(void *d) {
	capa_job *j = d;
	capa *c = g_hash_table_lookup(capas, j->chave);
	if (c) {
		g_free(c->arq);
		c->arq = j->arq ? j->arq : g_strdup("");
		c->r = j->r;
		c->g = j->g;
		c->b = j->b;
		c->pronta = true;
		gv_app_redesenhar(M.app);
	} else {
		g_free(j->arq);
	}
	g_free(j->chave);
	mu_faixa_liberar(&j->f);
	g_free(j);
}

static void capa_fazer(gpointer dado, gpointer user) {
	capa_job *j = dado;
	j->arq = mu_capa_gerar(&j->f);
	j->r = 0.35f;
	j->g = 0.2f;
	j->b = 0.7f;
	if (j->arq) {
		GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(j->arq, 24, 24, NULL);
		if (pb && gdk_pixbuf_get_n_channels(pb) >= 3) {
			long r = 0, g = 0, b = 0, n = 0;
			int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), rs = gdk_pixbuf_get_rowstride(pb), nc = gdk_pixbuf_get_n_channels(pb);
			const guchar *px = gdk_pixbuf_get_pixels(pb);
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					const guchar *p = px + (gsize)y * (gsize)rs + (gsize)x * (gsize)nc;
					r += p[0]; g += p[1]; b += p[2]; n++;
				}
			}
			if (n) {
				j->r = (float)r / (float)n / 255.0f;
				j->g = (float)g / (float)n / 255.0f;
				j->b = (float)b / (float)n / 255.0f;
			}
		}
		if (pb) {
			g_object_unref(pb);
		}
	}
	gv_na_linha_principal(capa_chegou, j);
}

static GThreadPool *capa_pool;

const char *capa_da_faixa(const mu_faixa *f) {
	if (!f) {
		return "";
	}
	char *k = chave_da_capa(f);
	capa *c = g_hash_table_lookup(capas, k);
	if (!c) {
		c = g_new0(capa, 1);
		g_hash_table_insert(capas, g_strdup(k), c);
		char *pronta = mu_capa_pronta(f);
		if (pronta || !f->capa_embutida) {
			/* a da pasta (ou nenhuma): já se sabe */
			c->arq = pronta ? pronta : g_strdup("");
			c->pronta = true;
			c->r = 0.35f; c->g = 0.2f; c->b = 0.7f;
			if (pronta) { /* a cor média, numa thread também */
				c->pronta = false;
				g_free(c->arq);
				c->arq = NULL;
			}
		}
		if (!c->pronta) {
			capa_job *j = g_new0(capa_job, 1);
			j->chave = g_strdup(k);
			mu_faixa_copiar(&j->f, f);
			g_thread_pool_push(capa_pool, j, NULL);
		}
	}
	g_free(k);
	return c->pronta ? c->arq : NULL;
}

void capa_cor(const mu_faixa *f, float *r, float *g, float *b) {
	*r = 0.35f; *g = 0.2f; *b = 0.7f;
	if (!f) {
		return;
	}
	char *k = chave_da_capa(f);
	capa *c = g_hash_table_lookup(capas, k);
	if (c && c->pronta) {
		*r = c->r; *g = c->g; *b = c->b;
	}
	g_free(k);
}

/* ---------------- favoritas e playlists ---------------- */

static void favoritas_ler(void) {
	if (M.favoritas) {
		g_hash_table_destroy(M.favoritas);
	}
	M.favoritas = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	mu_playlist_ler(MU_FAVORITAS, v);
	for (guint i = 0; i < v->len; i++) {
		g_hash_table_add(M.favoritas, g_strdup(v->pdata[i]));
	}
	g_ptr_array_free(v, TRUE);
}

bool eh_favorita(const char *caminho) {
	return M.favoritas && g_hash_table_contains(M.favoritas, caminho);
}

void alternar_favorita(const mu_faixa *f) {
	char *c[] = { f->caminho };
	if (eh_favorita(f->caminho)) {
		mu_playlist_remover(MU_FAVORITAS, f->caminho);
		aviso("Removida das Favoritas");
	} else {
		mu_playlist_adicionar(MU_FAVORITAS, c, 1);
		aviso("Adicionada às Favoritas");
	}
	favoritas_ler();
	if (M.vista == V_DETALHE && M.det_tipo == D_PLAYLIST && !strcmp(M.det_playlist, MU_FAVORITAS)) {
		recarregar_listas();
	}
}

void listar_playlists(void) {
	if (!M.playlists) {
		M.playlists = g_ptr_array_new_with_free_func(g_free);
	}
	g_ptr_array_set_size(M.playlists, 0);
	mu_playlists_listar(M.playlists);
	favoritas_ler();
}

int acervo_indice(const char *caminho) {
	return M.por_caminho ? GPOINTER_TO_INT(g_hash_table_lookup(M.por_caminho, caminho)) - 1 : -1;
}

/* ---------------- a fila ---------------- */

const mu_faixa *faixa_atual(void) {
	return M.atual >= 0 && M.atual < M.nfila ? &M.fila[M.atual].f : NULL;
}

static void fila_esvaziar(void) {
	for (int i = 0; i < M.nfila; i++) {
		mu_faixa_liberar(&M.fila[i].f);
	}
	M.nfila = 0;
	M.atual = -1;
	M.anunciada = -1;
}

static void fila_somar(const mu_faixa *f, int seq) {
	if (M.nfila == M.capfila) {
		M.capfila = M.capfila ? M.capfila * 2 : 64;
		M.fila = g_renew(item, M.fila, (guint)M.capfila);
	}
	mu_faixa_copiar(&M.fila[M.nfila].f, f);
	M.fila[M.nfila].seq = seq;
	M.nfila++;
}

static void embaralhar(int de) {
	for (int i = M.nfila - 1; i > de; i--) {
		int j = de + (int)g_random_int_range(0, i - de + 1);
		item t = M.fila[i];
		M.fila[i] = M.fila[j];
		M.fila[j] = t;
	}
}

static int cmp_seq(const void *a, const void *b) {
	return ((const item *)a)->seq - ((const item *)b)->seq;
}

static int proximo_indice(bool manual) {
	if (M.nfila == 0 || M.atual < 0) {
		return -1;
	}
	if (M.cfg.repetir == 2 && !manual) {
		return M.atual;
	}
	int n = M.atual + 1;
	if (n < M.nfila) {
		return n;
	}
	return M.cfg.repetir == 1 ? 0 : -1;
}

static void anunciar_proxima(void) {
	int n = proximo_indice(false);
	M.anunciada = n;
	mp_proxima(M.mp, n >= 0 ? M.fila[n].f.caminho : NULL);
}

static void salvar_fila(double pos) {
	char **v = g_new(char *, (guint)M.nfila + 1);
	for (int i = 0; i < M.nfila; i++) {
		v[i] = M.fila[i].f.caminho;
	}
	mu_fila_gravar(v, M.nfila, M.atual, pos);
	g_free(v);
}

static double ultimo_salvar;

void tocar_indice(int i) {
	if (i < 0 || i >= M.nfila) {
		return;
	}
	M.atual = i;
	M.erros_seguidos = 0;
	M.pos_pendente = 0;
	M.carregada_no_motor = true;
	if (!mp_tocar(M.mp, M.fila[i].f.caminho, 0, false)) {
		aviso("Não deu para iniciar o player de áudio (o mpv está instalado?).");
		return;
	}
	anunciar_proxima();
	ouvir_letra_mudou();
	salvar_fila(0);
	gv_app_redesenhar(M.app);
}

void tocar_lista(const int *idx, int n, int inicio, bool aleatorio) {
	if (n <= 0) {
		return;
	}
	fila_esvaziar();
	for (int i = 0; i < n; i++) {
		fila_somar(&M.ac.v[idx[i]], i);
	}
	int atual = CLAMP(inicio, 0, n - 1);
	if (aleatorio && n > 1) {
		item t = M.fila[0];
		M.fila[0] = M.fila[atual];
		M.fila[atual] = t;
		atual = 0;
		embaralhar(1);
	}
	M.cfg.aleatorio = aleatorio;
	tocar_indice(atual);
}

void fila_adicionar(const int *idx, int n, bool depois_da_atual) {
	if (n <= 0) {
		return;
	}
	if (M.nfila == 0) {
		tocar_lista(idx, n, 0, M.cfg.aleatorio);
		return;
	}
	int pos = depois_da_atual ? M.atual + 1 : M.nfila;
	int antes = M.nfila;
	int maxseq = 0;
	for (int i = 0; i < M.nfila; i++) {
		maxseq = MAX(maxseq, M.fila[i].seq);
	}
	for (int i = 0; i < n; i++) {
		fila_somar(&M.ac.v[idx[i]], maxseq + 1 + i);
	}
	if (depois_da_atual && pos < antes) { /* gira o que entrou para logo depois da atual */
		item *novos = g_new(item, (guint)n);
		memcpy(novos, &M.fila[antes], sizeof(item) * (gsize)n);
		memmove(&M.fila[pos + n], &M.fila[pos], sizeof(item) * (gsize)(antes - pos));
		memcpy(&M.fila[pos], novos, sizeof(item) * (gsize)n);
		g_free(novos);
	}
	aviso(depois_da_atual ? "Vai tocar a seguir" : "Adicionada à fila");
	anunciar_proxima();
	salvar_fila(0);
}

void fila_remover(int i) {
	if (i < 0 || i >= M.nfila) {
		return;
	}
	bool era_atual = i == M.atual;
	mu_faixa_liberar(&M.fila[i].f);
	memmove(&M.fila[i], &M.fila[i + 1], sizeof(item) * (gsize)(M.nfila - i - 1));
	M.nfila--;
	if (era_atual) {
		if (M.atual >= M.nfila) {
			M.atual = M.nfila - 1;
		}
		if (M.atual >= 0) {
			tocar_indice(M.atual);
		} else {
			mp_parar(M.mp);
		}
	} else {
		if (i < M.atual) {
			M.atual--;
		}
		anunciar_proxima();
	}
	salvar_fila(0);
}

void fila_mover(int de, int para) {
	if (de < 0 || de >= M.nfila || para < 0 || para >= M.nfila || de == para) {
		return;
	}
	item t = M.fila[de];
	if (de < para) {
		memmove(&M.fila[de], &M.fila[de + 1], sizeof(item) * (gsize)(para - de));
	} else {
		memmove(&M.fila[para + 1], &M.fila[para], sizeof(item) * (gsize)(de - para));
	}
	M.fila[para] = t;
	if (M.atual == de) {
		M.atual = para;
	} else if (de < M.atual && para >= M.atual) {
		M.atual--;
	} else if (de > M.atual && para <= M.atual) {
		M.atual++;
	}
	anunciar_proxima();
	salvar_fila(0);
}

void fila_limpar(void) {
	fila_esvaziar();
	mp_parar(M.mp);
	M.carregada_no_motor = false;
	salvar_fila(0);
	ouvir_letra_mudou();
}

static void aleatorio_mudou(void) {
	if (M.nfila > 1 && M.atual >= 0) {
		if (M.cfg.aleatorio) {
			embaralhar(M.atual + 1);
		} else {
			qsort(&M.fila[M.atual + 1], (size_t)(M.nfila - M.atual - 1), sizeof(item), cmp_seq);
		}
		anunciar_proxima();
		salvar_fila(M.est.pos);
	}
}

void alternar_toque(void) {
	if (M.atual < 0) {
		if (M.ac.n > 0) {
			int *todas = g_new(int, (guint)M.ac.n);
			for (int i = 0; i < M.ac.n; i++) {
				todas[i] = i;
			}
			tocar_lista(todas, M.ac.n, 0, M.cfg.aleatorio);
			g_free(todas);
		} else {
			aviso("A biblioteca está vazia: adicione uma pasta de músicas.");
		}
		return;
	}
	if (!M.est.vivo || !M.est.carregada || !M.carregada_no_motor) {
		double p = M.pos_pendente;
		M.carregada_no_motor = true;
		M.pos_pendente = 0;
		if (!mp_tocar(M.mp, M.fila[M.atual].f.caminho, p, false)) {
			aviso("Não deu para iniciar o player de áudio (o mpv está instalado?).");
			return;
		}
		anunciar_proxima();
		return;
	}
	mp_pausar(M.mp, M.est.tocando);
}

void proxima(void) {
	int n = proximo_indice(true);
	if (n >= 0) {
		tocar_indice(n);
	} else if (M.atual >= 0) {
		mp_parar(M.mp);
		M.carregada_no_motor = false;
		aviso("Fim da fila");
	}
}

void anterior(void) {
	if (M.atual < 0) {
		return;
	}
	if (M.est.pos > 3 || (M.atual == 0 && M.cfg.repetir != 1)) {
		mp_buscar(M.mp, 0);
	} else if (M.atual > 0) {
		tocar_indice(M.atual - 1);
	} else if (M.nfila > 1) {
		tocar_indice(M.nfila - 1);
	}
}

/* ---------------- os avisos do motor ---------------- */

static void trata_evento(void *d) {
	int tipo = GPOINTER_TO_INT(d);
	if (tipo == MP_EV_AVANCOU) {
		int n = M.anunciada >= 0 ? M.anunciada : proximo_indice(false);
		if (n >= 0 && n < M.nfila) {
			M.atual = n;
			M.erros_seguidos = 0;
			M.carregada_no_motor = true;
			anunciar_proxima();
			ouvir_letra_mudou();
			salvar_fila(0);
		}
	} else if (tipo == MP_EV_FIM) {
		int n = proximo_indice(false);
		if (n >= 0) {
			tocar_indice(n);
		} else {
			M.carregada_no_motor = false;
			if (M.atual >= 0) {
				salvar_fila(0);
			}
		}
	} else if (tipo == MP_EV_ERRO) {
		const mu_faixa *f = faixa_atual();
		aviso("Não deu para tocar \"%s\".", f ? f->titulo : "a faixa");
		int n = proximo_indice(true);
		if (++M.erros_seguidos < 5 && n >= 0) {
			tocar_indice(n);
			M.erros_seguidos = M.erros_seguidos; /* tocar_indice zera: guarda o que já deu errado */
		}
	} else if (tipo == MP_EV_MORREU) {
		M.carregada_no_motor = false;
		aviso("O player de áudio parou. Toque de novo.");
	}
	gv_app_redesenhar(M.app);
}

static void ao_evento(int tipo, void *dados) {
	(void)dados;
	gv_na_linha_principal(trata_evento, GINT_TO_POINTER(tipo));
}

/* ---------------- ajustes ---------------- */

void salvar_cfg(void) {
	mu_cfg_gravar(&M.cfg);
}

void aplicar_cfg(void) {
	mp_ajustes(M.mp, M.cfg.saida, M.cfg.normalizar, M.cfg.sem_intervalos, M.cfg.transicao);
	mp_volume(M.mp, M.cfg.volume);
	anunciar_proxima();
}

/* ---------------- a biblioteca ---------------- */

typedef struct {
	mu_cfg cfg;
	mu_acervo novo;
} carga;

static gint prog_f, prog_t;

static void progresso(int f, int t, void *d) {
	(void)d;
	g_atomic_int_set(&prog_f, f);
	g_atomic_int_set(&prog_t, t);
}

static bool primeira_carga = true;
static bool veio_arquivo_de_fora;   /* aberto com arquivos: a fila salva não volta por cima */
static double prox_varredura;

static void restaurar_fila(void) {
	mu_fila_salva fs;
	if (!M.cfg.restaurar_fila || !mu_fila_ler(&fs)) {
		return;
	}
	for (guint i = 0; i < fs.caminhos->len; i++) {
		const char *c = fs.caminhos->pdata[i];
		int k = acervo_indice(c);
		if (k >= 0) {
			fila_somar(&M.ac.v[k], (int)i);
		} else {
			mu_faixa f;
			if (mu_ler_faixa(c, &f)) {
				fila_somar(&f, (int)i);
				mu_faixa_liberar(&f);
			}
		}
	}
	if (M.nfila > 0) {
		M.atual = CLAMP(fs.atual, 0, M.nfila - 1);
		M.pos_pendente = fs.posicao;
		M.carregada_no_motor = false;
		ouvir_letra_mudou();
	}
	mu_fila_liberar(&fs);
}

static void carga_pronta(void *d) {
	carga *c = d;
	mu_acervo_liberar(&M.ac);
	M.ac = c->novo;
	mu_grupos_liberar(&M.albuns);
	mu_grupos_liberar(&M.artistas);
	mu_grupos_liberar(&M.pastas);
	mu_albuns(&M.ac, M.cfg.ordem_albuns, &M.albuns);
	mu_artistas(&M.ac, &M.artistas);
	mu_pastas(&M.ac, &M.pastas);
	if (M.por_caminho) {
		g_hash_table_destroy(M.por_caminho);
	}
	M.por_caminho = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
	for (int i = 0; i < M.ac.n; i++) {
		g_hash_table_insert(M.por_caminho, M.ac.v[i].caminho, GINT_TO_POINTER(i + 1));
	}
	M.carregando = false;
	g_free(c);
	listar_playlists();
	if (primeira_carga) {
		primeira_carga = false;
		if (!veio_arquivo_de_fora) {
			restaurar_fila();
		}
	}
	recarregar_listas();
	prox_varredura = gv_agora() + 90;
	gv_acordar_em(91);
	gv_app_redesenhar(M.app);
}

static gpointer carga_thread(gpointer d) {
	carga *c = d;
	mu_varrer(&c->cfg, &c->novo, progresso, NULL);
	gv_na_linha_principal(carga_pronta, c);
	return NULL;
}

void recarregar_biblioteca(void) {
	if (M.carregando) {
		return;
	}
	M.carregando = true;
	g_atomic_int_set(&prog_f, 0);
	g_atomic_int_set(&prog_t, 0);
	carga *c = g_new0(carga, 1);
	c->cfg = M.cfg;
	g_thread_unref(g_thread_new("musica-varrer", carga_thread, c));
}

/* ---------------- navegação ---------------- */

void ir_para(int vista) {
	if (vista != M.vista && vista != V_BUSCA) {
		M.vista_antes = M.vista;
	}
	if (vista != V_BUSCA) {
		M.busca[0] = 0;
	}
	M.vista = vista;
	barra_de_rolagem_zero();
}

/* ---------------- o MPRIS ---------------- */

typedef struct {
	int cmd;
	int64_t val;
} mcmd;

static void mpris_na_principal(void *d) {
	mcmd *c = d;
	switch (c->cmd) {
	case 1: if (!M.est.tocando) alternar_toque(); break;
	case 2: if (M.est.tocando) alternar_toque(); break;
	case 3: alternar_toque(); break;
	case 4: mp_parar(M.mp); M.carregada_no_motor = false; break;
	case 5: proxima(); break;
	case 6: anterior(); break;
	case 7: mp_buscar(M.mp, fmax(0, M.est.pos + (double)c->val / 1e6)); break;
	case 8: mp_buscar(M.mp, (double)c->val / 1e6); break;
	case 9: M.cfg.volume = (int)lround((double)c->val / 1000.0); mp_volume(M.mp, M.cfg.volume); salvar_cfg(); break;
	case 10: M.cfg.aleatorio = c->val != 0; aleatorio_mudou(); salvar_cfg(); break;
	case 11: M.cfg.repetir = (int)c->val; anunciar_proxima(); salvar_cfg(); break;
	default: break;
	}
	M.mm_ultimo = 0; /* manda o estado novo já */
	gv_app_redesenhar(M.app);
	g_free(c);
}

static void mp_cmd(int cmd, int64_t val) {
	mcmd *c = g_new0(mcmd, 1);
	c->cmd = cmd;
	c->val = val;
	gv_na_linha_principal(mpris_na_principal, c);
}

static void m_tocar(void *d) { mp_cmd(1, 0); }
static void m_pausar(void *d) { mp_cmd(2, 0); }
static void m_alternar(void *d) { mp_cmd(3, 0); }
static void m_parar(void *d) { mp_cmd(4, 0); }
static void m_proxima(void *d) { mp_cmd(5, 0); }
static void m_anterior(void *d) { mp_cmd(6, 0); }
static void m_buscar(void *d, int64_t x) { mp_cmd(7, x); }
static void m_posicionar(void *d, int64_t x) { mp_cmd(8, x); }
static void m_volume(void *d, double v) { mp_cmd(9, (int64_t)lround(v * 100000.0)); }
static void m_aleatorio(void *d, bool b) { mp_cmd(10, b); }
static void m_repetir(void *d, int m) { mp_cmd(11, m); }

/* abrir arquivos de fora (o gerenciador de arquivos, a linha de comando, o MPRIS): a fila passa a ser eles e o primeiro toca. Numa seleção de vários arquivos,
 * o segundo em diante chega com a marca #lumna-anexar no URI e entra depois, em vez de trocar a fila de novo. */
static void abrir_externos(char *const *caminhos, int n, bool anexar) {
	bool somar = anexar && M.nfila > 0;
	GArray *fs = g_array_new(FALSE, TRUE, sizeof(mu_faixa));
	for (int i = 0; i < n; i++) {
		if (g_file_test(caminhos[i], G_FILE_TEST_IS_DIR) || !mu_extensao_de_audio(caminhos[i])) {
			continue;
		}
		mu_faixa f;
		int k = acervo_indice(caminhos[i]);
		if (k >= 0) {
			mu_faixa_copiar(&f, &M.ac.v[k]);
			g_array_append_val(fs, f);
		} else if (mu_ler_faixa(caminhos[i], &f)) {
			g_array_append_val(fs, f);
		}
	}
	if (fs->len > 0) {
		int antes = somar ? M.nfila : 0;
		if (!somar) {
			fila_esvaziar();
		}
		for (guint i = 0; i < fs->len; i++) {
			fila_somar(&g_array_index(fs, mu_faixa, i), antes + (int)i);
			mu_faixa_liberar(&g_array_index(fs, mu_faixa, i));
		}
		if (somar) {
			anunciar_proxima();
			salvar_fila(0);
		} else {
			tocar_indice(0);
		}
		gv_app_redesenhar(M.app);
	}
	g_array_free(fs, TRUE);
}

static void abrir_uri_na_principal(void *d) {
	char *uri = d;
	bool anexar = false;
	char *marca = strstr(uri, "#lumna-anexar");
	if (marca) {
		*marca = '\0';
		anexar = true;
	}
	char *c = g_filename_from_uri(uri, NULL, NULL);
	if (c) {
		char *v[] = { c, NULL };
		abrir_externos(v, 1, anexar);
		g_free(c);
	}
	g_free(uri);
}

static void abrir_janela_na_principal(void *d) {
	const gv_janela *js[64];
	int n = gv_janelas(js, 64);
	for (int i = 0; i < n; i++) {
		if (js[i]->app_id && !strcmp(js[i]->app_id, "lumna-music")) {
			gv_janela_ativar(js[i]->id);
			return;
		}
	}
}

static void m_abrir_janela(void *d) {
	gv_na_linha_principal(abrir_janela_na_principal, NULL);
}

typedef struct {
	char **v;
	int n;
} lote;

static void abrir_lote_inicial(void *d) {
	lote *l = d;
	abrir_externos(l->v, l->n, false);
	g_strfreev(l->v);
	g_free(l);
}

static void m_abrir_uri(void *d, const char *uri) {
	gv_na_linha_principal(abrir_uri_na_principal, g_strdup(uri));
}

static void mpris_atualizar(void) {
	if (!M.mm) {
		return;
	}
	double agora = gv_agora();
	const mu_faixa *f = faixa_atual();
	mm_estado e;
	memset(&e, 0, sizeof(e));
	e.volume = M.cfg.volume / 100.0;
	e.aleatorio = M.cfg.aleatorio;
	e.repetir = M.cfg.repetir;
	if (f) {
		e.tem_faixa = true;
		e.tocando = M.est.tocando;
		g_strlcpy(e.titulo, f->titulo, sizeof(e.titulo));
		g_strlcpy(e.artista, f->artista, sizeof(e.artista));
		g_strlcpy(e.album, f->album, sizeof(e.album));
		const char *c = capa_da_faixa(f);
		if (c && *c) {
			g_strlcpy(e.capa, c, sizeof(e.capa));
		}
		char *h = g_compute_checksum_for_string(G_CHECKSUM_MD5, f->caminho, -1);
		g_strlcpy(e.id, h, 17);
		g_free(h);
		e.duracao_us = (int64_t)((M.est.dur > 0 ? M.est.dur : f->duracao) * 1e6);
		e.pos_us = (int64_t)((M.carregada_no_motor ? M.est.pos : M.pos_pendente) * 1e6);
		e.pode_proxima = proximo_indice(true) >= 0;
		e.pode_anterior = true;
	}
	/* só manda quando algo mudou ou a cada 3 s (o MPRIS anda a posição sozinho) */
	static mm_estado ultimo;
	bool mudou = memcmp(e.titulo, ultimo.titulo, sizeof(e.titulo)) || memcmp(e.id, ultimo.id, sizeof(e.id)) || e.tocando != ultimo.tocando || e.tem_faixa != ultimo.tem_faixa
		|| memcmp(e.capa, ultimo.capa, sizeof(e.capa)) || fabs(e.volume - ultimo.volume) > 0.001 || e.aleatorio != ultimo.aleatorio || e.repetir != ultimo.repetir
		|| e.pode_proxima != ultimo.pode_proxima || llabs(e.duracao_us - ultimo.duracao_us) > 100000;
	if (mudou || agora - M.mm_ultimo > 3) {
		mm_atualizar(M.mm, &e);
		if (llabs(e.pos_us - ultimo.pos_us) > 2500000 && !mudou && ultimo.tocando == e.tocando && agora - M.mm_ultimo > 0) {
			/* a posição pulou além do que o tempo explica: avisa os controles */
			double esperado = (double)ultimo.pos_us + (e.tocando ? (agora - M.mm_ultimo) * 1e6 : 0);
			if (fabs((double)e.pos_us - esperado) > 1500000) {
				mm_pulou(M.mm, e.pos_us);
			}
		}
		ultimo = e;
		M.mm_ultimo = agora;
	}
}

/* ---------------- o que soltam na janela ---------------- */

void tocar_arquivos(char **caminhos) {
	GArray *idx = g_array_new(FALSE, FALSE, sizeof(int));
	bool pasta_nova = false;
	for (int i = 0; caminhos[i]; i++) {
		if (g_file_test(caminhos[i], G_FILE_TEST_IS_DIR)) {
			bool tem = false;
			for (int k = 0; k < M.cfg.npastas; k++) {
				tem |= !strcmp(M.cfg.pastas[k], caminhos[i]);
			}
			if (!tem && M.cfg.npastas < MU_PASTAS_MAX) {
				g_strlcpy(M.cfg.pastas[M.cfg.npastas++], caminhos[i], sizeof(M.cfg.pastas[0]));
				pasta_nova = true;
			}
		} else {
			int k = acervo_indice(caminhos[i]);
			if (k >= 0) {
				g_array_append_val(idx, k);
			} else if (mu_extensao_de_audio(caminhos[i])) {
				mu_faixa f;
				if (mu_ler_faixa(caminhos[i], &f)) {
					/* uma música de fora da biblioteca: entra direto na fila */
					int sei = M.nfila;
					fila_somar(&f, sei);
					mu_faixa_liberar(&f);
					if (M.atual < 0) {
						tocar_indice(M.nfila - 1);
					}
					aviso("Adicionada à fila");
				}
			}
		}
	}
	if (idx->len > 0) {
		fila_adicionar((int *)idx->data, (int)idx->len, false);
		if (M.atual < 0 || !M.est.tocando) {
			/* fila_adicionar com fila vazia já toca; com a fila cheia só soma */
		}
	}
	g_array_free(idx, TRUE);
	if (pasta_nova) {
		salvar_cfg();
		aviso("Pasta adicionada à biblioteca");
		recarregar_biblioteca();
	}
}

static void soltar_rever(void) {
	char *dados = NULL;
	if (!gv_arraste_soltou(MIME_URIS, &dados)) {
		return;
	}
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	char **ls = g_strsplit_set(dados, "\r\n", -1);
	for (int i = 0; ls[i]; i++) {
		if (g_str_has_prefix(ls[i], "file://")) {
			char *c = g_filename_from_uri(ls[i], NULL, NULL);
			if (c) {
				g_ptr_array_add(v, c);
			}
		}
	}
	g_strfreev(ls);
	free(dados);
	g_ptr_array_add(v, NULL);
	tocar_arquivos((char **)v->pdata);
	g_ptr_array_free(v, TRUE);
}

/* ---------------- teclado ---------------- */

static void teclado(void) {
	if (dialogo_aberto() || gv_focado(CLAY_ID("mu-busca")) || gv_focado(CLAY_ID("mu-dlg-campo"))) {
		return;
	}
	for (int i = 0;; i++) {
		uint32_t t = gv_tecla_n(i);
		if (!t) {
			break;
		}
		uint32_t mods = gv_modificadores();
		if (t == ' ') {
			alternar_toque();
		} else if ((mods & GV_MOD_CTRL) && t == 0xff53) {
			proxima();
		} else if ((mods & GV_MOD_CTRL) && t == 0xff51) {
			anterior();
		} else if ((mods & GV_MOD_CTRL) && t == 0xff52) {
			M.cfg.volume = MIN(100, M.cfg.volume + 5);
			mp_volume(M.mp, M.cfg.volume);
		} else if ((mods & GV_MOD_CTRL) && t == 0xff54) {
			M.cfg.volume = MAX(0, M.cfg.volume - 5);
			mp_volume(M.mp, M.cfg.volume);
		} else if (t == 0xff53 && M.est.carregada) {
			mp_buscar(M.mp, M.est.pos + 5);
		} else if (t == 0xff51 && M.est.carregada) {
			mp_buscar(M.mp, M.est.pos - 5);
		} else if ((mods & GV_MOD_CTRL) && (t == 'f' || t == 'F')) {
			gv_focar(CLAY_ID("mu-busca"));
		}
	}
}

/* ---------------- a janela ---------------- */

static void refazer_busca(void) {
	if (!M.busca[0]) {
		if (M.vista == V_BUSCA) {
			M.vista = M.vista_antes == V_BUSCA ? V_OUVIR : M.vista_antes;
		}
		return;
	}
	if (M.vista != V_BUSCA) {
		M.vista_antes = M.vista;
		M.vista = V_BUSCA;
	}
	g_free(M.busca_idx);
	M.busca_idx = g_new(int, (guint)M.ac.n + 1);
	M.busca_n = mu_buscar(&M.ac, M.busca, M.busca_idx);
	barra_de_rolagem_zero();
}

static void topo(void) {
	CLAY(CLAY_ID("mu-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(72) }, .padding = { 20, 20, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_ID("mu-busca-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			if (gv_campo(CLAY_ID("mu-busca"), M.busca, sizeof(M.busca), "Buscar na sua biblioteca", GV_CAMPO_BUSCA) & GV_CAMPO_MUDOU) {
				refazer_busca();
			}
		}
		if (M.cfg.emma_no_app && gv_botao_redondo(CLAY_ID("mu-emma"), "emma", 42, false, false)) {
			char *argv[] = { "lumna-emma-chat", NULL };
			g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
		}
		if (gv_botao_redondo(CLAY_ID("mu-perfil"), "user", 42, false, false)) {
			gv_popup_alternar(CLAY_ID("menu-perfil"));
		}
		GV_POPUP(CLAY_ID("menu-perfil"), CLAY_ID("mu-perfil"), .alinhar = GV_ALINHAR_FIM, .largura = 300) {
			CLAY(CLAY_ID("mu-perfil-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 6 } }) {
				gv_texto(CLAY_STRING("Contas de streaming"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("O Qobuz e outros serviços chegam numa próxima fase. Por ora o LUMNA Music toca a sua biblioteca local."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
		}
	}
}

static void conteudo(void) {
	switch (M.vista) {
	case V_BIBLIOTECA: tela_biblioteca(); break;
	case V_ALBUNS: tela_albuns(); break;
	case V_ARTISTAS: tela_artistas(); break;
	case V_PASTAS: tela_pastas(); break;
	case V_DETALHE: tela_detalhe(); break;
	case V_BUSCA: tela_busca(); break;
	case V_AJUSTES: tela_ajustes(); break;
	default: tela_ouvir(); break;
	}
}

static void depurar(void);
int musica_fila_arrasto(void);

static void desenhar(gv_app *a, void *dados) {
	mp_estado_ler(M.mp, &M.est);
	ouvir_quadro();
	soltar_rever();
	if (M.vista != V_AJUSTES || !ajustes_dialogo()) {
		teclado();
	}
	CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("barra"), "music", "LUMNA Music");
		CLAY(CLAY_ID("mu-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			barra_lateral();
			CLAY(CLAY_ID("mu-principal"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
				topo();
				CLAY(CLAY_ID("mu-conteudo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
					conteudo();
				}
			}
			if (M.cfg.fila_aberta) {
				painel_da_fila();
			}
		}
		barra_do_player();
	}
	menus_e_dialogos();
	/* o que anda sozinho */
	double agora = gv_agora();
	if (M.est.tocando || M.carregando) {
		gv_acordar_em(M.carregando ? 0.3 : 0.2);
	}
	if (M.est.tocando && agora - ultimo_salvar > 10 && M.atual >= 0) {
		ultimo_salvar = agora;
		salvar_fila(M.est.pos);
	}
	if (M.cfg.buscar_auto && !M.carregando && prox_varredura > 0 && agora >= prox_varredura) {
		prox_varredura = agora + 90;
		recarregar_biblioteca();
	}
	mpris_atualizar();
	depurar();
}

/* ---------------- depuração para os testes (LUMNA_MUSICA_DEBUG) ---------------- */

static void depurar(void) {
	static int ligado = -1;
	if (ligado < 0) {
		ligado = getenv("LUMNA_MUSICA_DEBUG") != NULL;
	}
	if (!ligado) {
		return;
	}
	static GHashTable *antes;
	static char *estado_antes;
	static int extras;
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	bool mudou = false;
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	GArray *ids = g_array_new(FALSE, FALSE, sizeof(uint32_t));
#define ADD(nome_, id_) do { g_ptr_array_add(nomes, g_strdup(nome_)); Clay_ElementId e_ = (id_); g_array_append_val(ids, e_.id); } while (0)
#define F(nome_) ADD(nome_, CLAY_ID(nome_))
#define SERIE(base, n) for (int i_ = 0; i_ < (n); i_++) { g_ptr_array_add(nomes, g_strdup_printf("%s/%d", base, i_)); Clay_ElementId e_ = CLAY_IDI(base, (uint32_t)i_); g_array_append_val(ids, e_.id); }
	F("mu-busca"); F("mu-perfil"); F("mu-emma"); F("mu-play"); F("mu-prox"); F("mu-ant"); F("mu-aleatorio"); F("mu-repetir"); F("mu-coracao"); F("mu-seek"); F("mu-volume");
	F("mu-saida"); F("mu-letra-bt"); F("mu-fila-bt"); F("mu-capa-bar"); F("mu-vol-icone"); F("mu-nova-playlist"); F("mu-adicionar"); F("mu-config");
	F("mu-reproduzir"); F("mu-aleatorio-bt"); F("mu-mais-det"); F("mu-seguir"); F("mu-fila-mais"); F("mu-fila-limpar"); F("mu-fila-salvar");
	F("mu-ordem"); F("mu-vista-grade"); F("mu-vista-lista"); F("mu-dlg-ok"); F("mu-dlg-cancelar"); F("mu-dlg-campo"); F("mu-rolagem");
	F("mf-tocar"); F("mf-seguir"); F("mf-fila"); F("mf-fav"); F("mf-album"); F("mf-artista"); F("mf-atlas"); F("mf-tirar"); F("menu-faixa");
	F("mq-remover"); F("mq-seguir"); F("mp-renomear"); F("mp-apagar"); F("mp-tocar");
	F("mf-nova"); F("md-renomear"); F("md-apagar"); F("md-seguir"); F("md-fila"); F("mu-abrir-emma"); F("mu-adicionar-pasta"); F("mu-reescanear"); F("mu-limpar-capas");
	F("mu-cfg-saida"); F("mu-cfg-tam-letra"); F("mu-vazio-add"); F("mu-letra-rol"); F("mu-fila-rol"); F("mu-ouvir-capa"); F("menu-det"); F("mu-pl-cab");
	SERIE("mu-nav", 5); SERIE("mu-pl", 20); SERIE("mu-lin", 120); SERIE("mu-lin-mais", 120); SERIE("mu-fl", 60); SERIE("mu-album", 60); SERIE("mu-artista", 60); SERIE("mu-pasta", 40);
	SERIE("mu-letra-l", 80); SERIE("mu-aba", 5); SERIE("mu-cfg", 24); SERIE("mf-pl", 12); SERIE("mu-saida-i", 12); SERIE("mu-pasta-rm", 8); SERIE("mu-grip", 120); SERIE("mu-fgrip", 60);
#undef F
#undef SERIE
#undef ADD
	for (guint i = 0; i < nomes->len; i++) {
		Clay_BoundingBox b = gv_caixa((Clay_ElementId){ .id = g_array_index(ids, uint32_t, i) });
		char *ant0 = g_hash_table_lookup(antes, nomes->pdata[i]);
		if (b.width <= 0 && (!ant0 || !strcmp(ant0, "0 0 0 0"))) {
			continue;
		}
		char *linha = b.width <= 0 ? g_strdup("0 0 0 0") : g_strdup_printf("%.0f %.0f %.0f %.0f", b.x, b.y, b.width, b.height);
		char *ant = g_hash_table_lookup(antes, nomes->pdata[i]);
		if (!ant || strcmp(ant, linha)) {
			printf("pos %s %s\n", (char *)nomes->pdata[i], linha);
			g_hash_table_insert(antes, g_strdup(nomes->pdata[i]), linha);
			mudou = true;
		} else {
			g_free(linha);
		}
	}
	const mu_faixa *f = faixa_atual();
	char *titulo = g_strdup(f ? f->titulo : "");
	for (char *p = titulo; *p; p++) {
		if (*p == ' ') {
			*p = '_';
		}
	}
	char *busca = g_strdup(M.busca);
	for (char *p = busca; *p; p++) {
		if (*p == ' ') {
			*p = '_';
		}
	}
	GString *ordem = g_string_new(NULL);
	for (int i = 0; i < M.nfila && i < 8; i++) {
		char *t = g_strdup(M.fila[i].f.titulo);
		for (char *p = t; *p; p++) {
			if (*p == ' ' || *p == ',') {
				*p = '_';
			}
		}
		g_string_append_printf(ordem, "%s%s", i ? "," : "", t);
		g_free(t);
	}
	char *estado = g_strdup_printf("vista=%d carregando=%d nfaixas=%d nalbuns=%d nartistas=%d npastas=%d nplaylists=%d nfila=%d atual=%d tocando=%d carregada=%d pos=%.1f dur=%.1f titulo=%s "
		"aleatorio=%d repetir=%d volume=%d fila_aberta=%d busca=[%s] nbusca=%d det_tipo=%d det_n=%d letra_n=%d letra_sinc=%d letra_linha=%d seguir=%d saida=%s normalizar=%d gapless=%d transicao=%d "
		"restaurar=%d emma=%d aba=%d fav=%d mpris=%d vivo=%d fila=[%s] npastas_cfg=%d subpastas=%d farr=%d dlg=%d",
		M.vista, M.carregando, M.ac.n, M.albuns.n, M.artistas.n, M.pastas.n, M.playlists ? (int)M.playlists->len : 0, M.nfila, M.atual, M.est.tocando, M.est.carregada, M.est.pos, M.est.dur, titulo,
		M.cfg.aleatorio, M.cfg.repetir, M.cfg.volume, M.cfg.fila_aberta, busca, M.busca_n, M.det_tipo, M.det_n, M.letra.n, M.letra.sincronizada, M.letra_linha, M.letra_seguir,
		M.cfg.saida[0] ? M.cfg.saida : "padrao", M.cfg.normalizar, M.cfg.sem_intervalos, M.cfg.transicao, M.cfg.restaurar_fila, M.cfg.emma_no_app, M.ajustes_aba,
		f ? eh_favorita(f->caminho) : 0, M.mm != NULL, M.est.vivo, ordem->str, M.cfg.npastas, M.cfg.subpastas, musica_fila_arrasto(), dialogo_aberto());
	g_string_free(ordem, TRUE);
	g_free(titulo);
	g_free(busca);
	if (!estado_antes || strcmp(estado_antes, estado)) {
		/* a posição anda sempre: só vale como mudança o resto */
		char *sem_pos = g_strdup(estado), *p1 = strstr(sem_pos, " pos=");
		if (p1) {
			char *p2 = strchr(p1 + 1, ' ');
			memmove(p1, p2, strlen(p2) + 1);
		}
		static char *sem_pos_antes;
		if (!sem_pos_antes || strcmp(sem_pos_antes, sem_pos)) {
			mudou = true;
		}
		g_free(sem_pos_antes);
		sem_pos_antes = sem_pos;
		printf("estado %s\n", estado);
		g_free(estado_antes);
		estado_antes = estado;
	} else {
		g_free(estado);
	}
	fflush(stdout);
	g_ptr_array_free(nomes, TRUE);
	g_array_free(ids, TRUE);
	if (mudou) {
		extras = 2;
	}
	if (extras > 0) {
		extras--;
		gv_app_redesenhar(M.app);
	}
}

/* ---------------- main ---------------- */

static void dispositivos_pronto(void *d) {
	GPtrArray *v = d;
	if (M.dispositivos) {
		g_ptr_array_free(M.dispositivos, TRUE);
	}
	M.dispositivos = v;
	gv_app_redesenhar(M.app);
}

static gpointer dispositivos_thread(gpointer d) {
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	mp_dispositivos(v);
	gv_na_linha_principal(dispositivos_pronto, v);
	return NULL;
}

int main(int argc, char **argv) {
	/* instância única: se o LUMNA Music já está aberto, os arquivos vão para ele, a janela dele sobe e este processo sai */
	char **entradas = g_new0(char *, (gsize)argc + 1);
	int nent = 0;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			entradas[nent++] = strstr(argv[i], "://") ? g_filename_from_uri(argv[i], NULL, NULL) : g_canonicalize_filename(argv[i], NULL);
		}
	}
	if (!g_getenv("LUMNA_MUSICA_MULTI") && mm_encaminhar(NULL, entradas, nent)) {
		return 0;
	}
	if (getenv("LUMNA_MUSICA_DEBUG")) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	signal(SIGPIPE, SIG_IGN);
	mu_cfg_ler(&M.cfg);
	M.vista = V_OUVIR;
	M.atual = -1;
	M.anunciada = -1;
	M.letra_seguir = M.cfg.letras_seguir;
	capas = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	capa_pool = g_thread_pool_new(capa_fazer, NULL, 2, FALSE, NULL);
	M.mp = mp_criar(ao_evento, NULL);
	M.app = gv_app_criar(&(gv_opcoes){ .titulo = "LUMNA Music", .app_id = "lumna-music", .largura = 1280, .altura = 800 });
	if (!M.app) {
		return 1;
	}
	mm_acoes acoes = { m_tocar, m_pausar, m_alternar, m_parar, m_proxima, m_anterior, m_buscar, m_posicionar, m_volume, m_aleatorio, m_repetir, m_abrir_janela, m_abrir_uri, NULL };
	M.mm = mm_iniciar(&acoes, NULL);
	gv_arraste_aceitar(MIME_URIS);
	aplicar_cfg();
	listar_playlists();
	g_thread_unref(g_thread_new("musica-dispositivos", dispositivos_thread, NULL));
	recarregar_biblioteca();
	if (nent > 0) {   /* aberto com arquivos (o app padrão para áudio): toca o que veio, e a fila de antes não volta */
		veio_arquivo_de_fora = true;
		lote *l = g_new0(lote, 1);
		l->v = g_strdupv(entradas);
		l->n = nent;
		gv_na_linha_principal(abrir_lote_inicial, l);
	}
	g_strfreev(entradas);
	int r = gv_app_correr(M.app, desenhar, NULL);
	if (M.atual >= 0) {
		salvar_fila(M.est.pos);
	}
	salvar_cfg();
	mm_parar(M.mm);
	mp_liberar(M.mp);
	return r;
}

/* ---- o que a barra do player chama ---- */
void musica_aleatorio_mudou(void) {
	aleatorio_mudou();
}

void musica_repetir_mudou(void) {
	anunciar_proxima();
}
