/* A janela da Galeria: barra lateral, cabeçalho, barra de seleção, rodapé, menus, diálogos e as operações (lixeira, importar, compartilhar). */
#include <math.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "arquivos.h"
#include "arquivos_op.h"
#include "connectc.h"
#include "json.h"

estado G;

#define ID_ROLAGEM CLAY_ID("g-rolagem")
#define MIME_URIS "text/uri-list"

static const char *const NOMES_VISTA[] = { "Todas as mídias", "Fotos", "Vídeos", "Favoritos", "Recentes", "Lixeira", "" };

/* ---------------- aviso (o recado no rodapé) ---------------- */

static char aviso_txt[256];
static double aviso_ate;
static char desfazer_txt[128];
static double desfazer_ate;

void aviso(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	g_vsnprintf(aviso_txt, sizeof(aviso_txt), fmt, ap);
	va_end(ap);
	aviso_ate = gv_agora() + 6;
	gv_acordar_em(6.1);
	gv_app_redesenhar(G.app);
}

static void redesenhar_na_principal(void *d) {
	(void)d;
	gv_app_redesenhar(G.app);
}

void ao_miniatura_pronta(void *dados) { /* da thread das miniaturas */
	gv_na_linha_principal(redesenhar_na_principal, dados);
}

/* ---------------- seleção ---------------- */

void sel_alternar(const char *caminho) {
	if (!g_hash_table_remove(G.sel, caminho)) {
		g_hash_table_add(G.sel, g_strdup(caminho));
	}
}

void sel_limpar(void) {
	g_hash_table_remove_all(G.sel);
}

int sel_n(void) {
	return (int)g_hash_table_size(G.sel);
}

void sel_intervalo(int de, int ate) {
	if (de > ate) {
		int t = de;
		de = ate;
		ate = t;
	}
	for (int k = de < 0 ? 0 : de; k <= ate && k < G.nvis; k++) {
		const char *c = midia_vis(k)->caminho;
		if (!g_hash_table_contains(G.sel, c)) {
			g_hash_table_add(G.sel, g_strdup(c));
		}
	}
}

/* os caminhos selecionados que a vista mostra, na ordem dela (g_ptr_array de char *, que são do acervo: não libere) */
static GPtrArray *selecionados(void) {
	GPtrArray *v = g_ptr_array_new();
	for (int k = 0; k < G.nvis; k++) {
		if (g_hash_table_contains(G.sel, midia_vis(k)->caminho)) {
			g_ptr_array_add(v, midia_vis(k)->caminho);
		}
	}
	return v;
}

static gl_midia *achar_na_biblioteca(const char *caminho) {
	for (int i = 0; i < G.acervo.n; i++) {
		if (!strcmp(G.acervo.v[i].caminho, caminho)) {
			return &G.acervo.v[i];
		}
	}
	return NULL;
}

/* ---------------- a vista: filtros, grupos ---------------- */

void refiltrar(void) {
	const gl_acervo *a = acervo_atual();
	g_free(G.vis);
	G.vis = g_new(int, a->n > 0 ? a->n : 1);
	gl_filtro f = { .tipo = GL_F_TODAS, .busca = G.busca };
	switch (G.vista) {
	case V_FOTOS: f.tipo = GL_F_FOTOS; break;
	case V_VIDEOS: f.tipo = GL_F_VIDEOS; break;
	case V_FAVORITOS: f.tipo = GL_F_FAVORITOS; break;
	case V_RECENTES: f.tipo = GL_F_RECENTES; break;
	case V_ALBUM:
		f.tipo = GL_F_ALBUM;
		if (G.album >= 0 && G.album < G.albuns.n) {
			f.album_caminhos = (char *const *)G.albuns.v[G.album].caminhos->pdata;
			f.album_n = (int)G.albuns.v[G.album].caminhos->len;
		}
		break;
	default: break;
	}
	G.nvis = gl_filtrar(a, &f, G.vis);
	int agrupar = G.cfg.agrupar;
	if (G.vista == V_LIXEIRA || (G.cfg.ordem >= GL_ORDEM_NOME && agrupar != GL_AGRUPAR_PASTA)) {
		agrupar = GL_AGRUPAR_NENHUM;
	}
	G.ngrupos = gl_agrupar(a, G.vis, G.nvis, agrupar, time(NULL), G.grupos, 512);
	if (G.ancora >= G.nvis) {
		G.ancora = -1;
	}
	if (G.tela == T_PLAYER && !visualizador_reposicionar_video()) {
		player_fechar();
	}
	if (G.tela == T_VISUALIZADOR) { /* a lista mudou com uma foto aberta: ela segue onde estiver, ou o visualizador fecha */
		if (!visualizador_reposicionar()) {
			visualizador_fechar();
		}
	}
}

static void reordenar(void) {
	gl_acervo_ordenar(&G.acervo, G.cfg.ordem, G.cfg.agrupar);
	refiltrar();
}

void gravar_cfg(void) {
	gl_cfg_gravar(&G.cfg);
}

/* ---------------- ler a biblioteca (e a lixeira) numa thread ---------------- */

typedef struct {
	gl_cfg cfg;
	bool lixeira;
	unsigned gen;
	gl_acervo res;
} carga;

static unsigned gen_bib, gen_lix;
static carga *chegada_bib, *chegada_lix;

static void carga_chegou(void *dados) {
	carga *c = dados;
	carga **alvo = c->lixeira ? &chegada_lix : &chegada_bib;
	if (c->gen != (c->lixeira ? gen_lix : gen_bib)) { /* já se pediu outra depois desta */
		gl_acervo_liberar(&c->res);
		g_free(c);
		return;
	}
	if (*alvo) {
		gl_acervo_liberar(&(*alvo)->res);
		g_free(*alvo);
	}
	*alvo = c;
	gv_app_redesenhar(G.app);
}

static gpointer carga_thread(gpointer dados) {
	carga *c = dados;
	if (c->lixeira) {
		gl_varrer_lixeira(&c->res);
	} else {
		gl_varrer(&c->cfg, &c->res);
	}
	gv_na_linha_principal(carga_chegou, c);
	return NULL;
}

static void iniciar_carga(bool lixeira) {
	carga *c = g_new0(carga, 1);
	c->cfg = G.cfg;
	c->lixeira = lixeira;
	c->gen = lixeira ? ++gen_lix : ++gen_bib;
	if (!lixeira) {
		G.carregando = true;
	}
	g_thread_unref(g_thread_new("galeria-ler", carga_thread, c));
}

void recarregar_biblioteca(void) {
	iniciar_carga(false);
	if (G.vista == V_LIXEIRA) {
		iniciar_carga(true);
	}
}

/* no começo do quadro: o que chegou entra, com nada segurando ponteiros para a lista */
static void aplicar_cargas(void) {
	bool mudou = false;
	if (chegada_bib) {
		gl_acervo_liberar(&G.acervo);
		G.acervo = chegada_bib->res;
		g_free(chegada_bib);
		chegada_bib = NULL;
		G.carregando = false;
		/* o que estava selecionado e já não existe sai da seleção */
		GHashTable *vivos = g_hash_table_new(g_str_hash, g_str_equal);
		for (int i = 0; i < G.acervo.n; i++) {
			g_hash_table_add(vivos, G.acervo.v[i].caminho);
		}
		GHashTableIter it;
		gpointer k;
		g_hash_table_iter_init(&it, G.sel);
		while (g_hash_table_iter_next(&it, &k, NULL)) {
			if (!g_hash_table_contains(vivos, k) && G.vista != V_LIXEIRA) {
				g_hash_table_iter_remove(&it);
			}
		}
		g_hash_table_destroy(vivos);
		gl_albuns_liberar(&G.albuns);
		gl_albuns_ler(&G.albuns);
		mudou = true;
	}
	if (chegada_lix) {
		gl_acervo_liberar(&G.lixo);
		G.lixo = chegada_lix->res;
		g_free(chegada_lix);
		chegada_lix = NULL;
		mudou = true;
	}
	if (mudou) {
		refiltrar();
	}
}

/* ---------------- olhar as pastas (mídias novas entram sozinhas) ---------------- */

static int ino_fd = -1;
static GArray *ino_wds;
static bool ino_pendente;
static double ino_ultimo, ino_primeiro;

static void vigiar_pasta_rec(const char *pasta, int fundo) {
	if (ino_wds->len >= 600) {
		return;
	}
	int wd = inotify_add_watch(ino_fd, pasta, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE);
	if (wd >= 0) {
		g_array_append_val(ino_wds, wd);
	}
	if (!G.cfg.subpastas || fundo >= 6) {
		return;
	}
	GDir *d = g_dir_open(pasta, 0, NULL);
	const char *n;
	while (d && (n = g_dir_read_name(d))) {
		if (n[0] == '.') {
			continue;
		}
		char *c = g_build_filename(pasta, n, NULL);
		struct stat st;
		if (stat(c, &st) == 0 && S_ISDIR(st.st_mode)) {
			vigiar_pasta_rec(c, fundo + 1);
		}
		g_free(c);
	}
	if (d) {
		g_dir_close(d);
	}
	g_free(NULL);
}

/* só ao começar e quando os ajustes mudam (tirar e pôr os olhos a cada leitura faria o kernel avisar da própria troca) */
static void vigiar_pastas(void) {
	if (ino_fd < 0) {
		return;
	}
	for (guint i = 0; i < ino_wds->len; i++) {
		inotify_rm_watch(ino_fd, g_array_index(ino_wds, int, i));
	}
	g_array_set_size(ino_wds, 0);
	if (!G.cfg.buscar_auto) {
		return;
	}
	for (int i = 0; i < G.cfg.npastas; i++) {
		vigiar_pasta_rec(G.cfg.pastas[i], 0);
	}
}

static void ino_pronto(gv_app *a, int fd, void *dados) {
	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	bool mudou = false, subpasta = false;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (char *p = buf; p < buf + n;) {
			const struct inotify_event *ev = (const struct inotify_event *)p;
			if (!(ev->mask & IN_IGNORED)) {
				mudou = true;
				subpasta |= (ev->mask & IN_ISDIR) && (ev->mask & (IN_CREATE | IN_MOVED_TO));
			}
			p += sizeof(*ev) + ev->len;
		}
	}
	if (subpasta) {
		vigiar_pastas(); /* uma pasta nova também entra na vigia */
	}
	if (mudou) {
		double agora = gv_agora();
		if (!ino_pendente) {
			ino_pendente = true;
			ino_primeiro = agora;
		}
		ino_ultimo = agora;
		gv_acordar_em(0.9);
		gv_app_redesenhar(G.app);
	}
}

static void ino_rever(void) {
	if (!ino_pendente) {
		return;
	}
	double agora = gv_agora();
	if (agora - ino_ultimo >= 0.8 || agora - ino_primeiro >= 4) {
		ino_pendente = false;
		recarregar_biblioteca();
	} else {
		gv_acordar_em(0.85 - (agora - ino_ultimo));
	}
}

/* ---------------- as operações com arquivos ---------------- */

static ar_op *op;
static enum ar_op_tipo op_tipo;
static ar_op_estado op_st;
static bool op_importando;

static void op_acordou(void *d) {
	(void)d;
	gv_app_redesenhar(G.app);
}

static void op_acordar(void *d) { /* da thread */
	gv_na_linha_principal(op_acordou, d);
}

static void op_iniciar(enum ar_op_tipo tipo, char *const *origens, int n, const char *destino) {
	if (op || n <= 0) {
		aviso(op ? "Espere a operação de agora terminar" : "Nada para fazer");
		return;
	}
	op_tipo = tipo;
	op = ar_op_iniciar(tipo, origens, n, destino, op_acordar, NULL);
	memset(&op_st, 0, sizeof(op_st));
}

static void op_rever(void) {
	if (!op) {
		return;
	}
	ar_op_estado_ler(op, &op_st);
	if (!op_st.terminou) {
		gv_acordar_em(0.25);
		return;
	}
	ar_op_liberar(op);
	op = NULL;
	const char *quantos = op_st.itens_total == 1 ? "1 item" : gv_fmt("%d itens", op_st.itens_total).chars;
	if (op_st.cancelado) {
		aviso("Cancelado");
	} else if (op_st.erros) {
		aviso("%s", op_st.erro);
	} else {
		switch (op_tipo) {
		case AR_OP_LIXEIRA: aviso("%s movido%s para a lixeira", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_COPIAR: aviso(op_importando ? "%s importado%s" : "%s copiado%s", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_MOVER: aviso(op_importando ? "%s importado%s" : "%s movido%s", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_APAGAR: aviso("%s apagado%s para sempre", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_RESTAURAR: aviso("%s restaurado%s", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		default: break;
		}
	}
	if (!op_st.erros && !op_st.cancelado && op_tipo == AR_OP_LIXEIRA && ar_desfazer_ha(desfazer_txt, sizeof(desfazer_txt))) {
		desfazer_ate = gv_agora() + 10;
		gv_acordar_em(10.1);
	}
	op_importando = false;
	gl_limpar_orfaos();
	recarregar_biblioteca();
	if (G.vista == V_ALBUM || G.vista == V_FAVORITOS) {
		refiltrar();
	}
}

void mover_para_lixeira(char *const *caminhos, int n) {
	for (int i = 0; i < n; i++) {
		g_hash_table_remove(G.sel, caminhos[i]);
	}
	op_iniciar(AR_OP_LIXEIRA, caminhos, n, NULL);
}

/* ---------------- importar ---------------- */

typedef struct {
	char **raizes;      /* o que foi pedido (arquivos ou pastas) */
	GPtrArray *achados; /* o que sobrou: só mídias (g_free) */
} importacao;

static void importar_pronto(void *dados) {
	importacao *im = dados;
	if (im->achados->len == 0) {
		aviso("Não achei fotos nem vídeos para importar");
	} else {
		char *destino = g_build_filename(G.cfg.npastas > 0 ? G.cfg.pastas[0] : g_get_home_dir(), "Importadas", NULL);
		g_mkdir_with_parents(destino, 0755);
		int p = G.cfg.duplicados;
		ar_op_conflito_padrao(p == GL_DUP_SUBSTITUIR ? AR_C_SUBSTITUIR : p == GL_DUP_MANTER_AMBOS ? AR_C_MANTER_AMBOS : p == GL_DUP_PULAR ? AR_C_PULAR : AR_C_PERGUNTAR);
		op_importando = true;
		op_iniciar(G.cfg.manter_original ? AR_OP_COPIAR : AR_OP_MOVER, (char *const *)im->achados->pdata, (int)im->achados->len, destino);
		g_free(destino);
	}
	g_ptr_array_free(im->achados, TRUE);
	g_strfreev(im->raizes);
	g_free(im);
}

static gpointer importar_thread(gpointer dados) {
	importacao *im = dados;
	im->achados = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; im->raizes[i]; i++) {
		struct stat st;
		if (stat(im->raizes[i], &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			gl_cfg c;
			gl_cfg_padrao(&c);
			c.npastas = 1;
			g_strlcpy(c.pastas[0], im->raizes[i], sizeof(c.pastas[0]));
			gl_acervo a;
			gl_varrer(&c, &a);
			for (int k = 0; k < a.n; k++) {
				g_ptr_array_add(im->achados, g_strdup(a.v[k].caminho));
			}
			gl_acervo_liberar(&a);
		} else {
			char *b = g_path_get_basename(im->raizes[i]);
			if (gl_eh_foto(b) || gl_eh_video(b)) {
				g_ptr_array_add(im->achados, g_strdup(im->raizes[i]));
			}
			g_free(b);
		}
	}
	gv_na_linha_principal(importar_pronto, im);
	return NULL;
}

static void importar_caminhos(char **raizes) {
	importacao *im = g_new0(importacao, 1);
	im->raizes = g_strdupv(raizes);
	aviso("Procurando o que importar…");
	g_thread_unref(g_thread_new("galeria-importar", importar_thread, im));
}

/* o que se solta na janela (arquivos ou pastas) é importado */
static void soltar_rever(void) {
	char *dados = NULL;
	if (!gv_arraste_soltou(MIME_URIS, &dados)) {
		return;
	}
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	char **linhas = g_strsplit_set(dados, "\r\n", -1);
	for (int i = 0; linhas[i]; i++) {
		if (g_str_has_prefix(linhas[i], "file://")) {
			char *c = g_filename_from_uri(linhas[i], NULL, NULL);
			if (c) {
				g_ptr_array_add(v, c);
			}
		}
	}
	g_strfreev(linhas);
	free(dados);
	if (v->len) {
		g_ptr_array_add(v, NULL);
		importar_caminhos((char **)v->pdata);
	}
	g_ptr_array_free(v, TRUE);
}

/* ---------------- favoritar, álbuns, compartilhar ---------------- */

void alternar_favorita(const gl_midia *m) {
	bool nova = !gl_fav_tem(m->caminho);
	gl_fav_definir(m->caminho, nova);
	gl_midia *x = achar_na_biblioteca(m->caminho);
	if (x) {
		x->favorita = nova;
	}
	refiltrar();
}

static void favoritar_selecao(void) {
	GPtrArray *s = selecionados();
	bool todas = s->len > 0;
	for (guint i = 0; i < s->len; i++) {
		todas &= gl_fav_tem(s->pdata[i]);
	}
	for (guint i = 0; i < s->len; i++) {
		gl_fav_definir(s->pdata[i], !todas);
		gl_midia *x = achar_na_biblioteca(s->pdata[i]);
		if (x) {
			x->favorita = !todas;
		}
	}
	aviso(todas ? "Tirado dos favoritos" : "Adicionado aos favoritos");
	g_ptr_array_free(s, TRUE);
	refiltrar();
}

static void adicionar_ao_album(const char *nome) {
	GPtrArray *s = selecionados();
	int n = gl_album_adicionar(nome, (char *const *)s->pdata, (int)s->len);
	if (n) {
		aviso("%d adicionad%s a “%s”", n, n == 1 ? "o" : "os", nome);
	} else {
		aviso("Já estava em “%s”", nome);
	}
	g_ptr_array_free(s, TRUE);
	gl_albuns_liberar(&G.albuns);
	gl_albuns_ler(&G.albuns);
	refiltrar();
}

static struct { char id[64], nome[96]; } celulares[8];
static int n_celulares;
static bool celulares_lendo;

static void celulares_chegaram(void *d) {
	celulares_lendo = false;
	gv_app_redesenhar(G.app);
}

static gpointer celulares_thread(gpointer d) {
	int n = 0;
	char *buf = g_malloc(64 * 1024);
	if (connectc_pedido("connect", buf, 64 * 1024)) {
		struct json *j = json_ler(buf);
		const struct json *ds = json_chave(j, "dispositivos");
		bool ativo = json_bool(json_chave(j, "ativo"), true);
		for (int i = 0; ativo && ds && i < ds->nfilhos && n < 8; i++) {
			const struct json *x = json_item(ds, i);
			if (json_bool(json_chave(x, "conectado"), false) && json_bool(json_chave(json_chave(x, "recursos"), "files"), true)) {
				g_strlcpy(celulares[n].id, json_chave_texto(x, "id"), sizeof(celulares[0].id));
				g_strlcpy(celulares[n].nome, json_chave_texto(x, "nome"), sizeof(celulares[0].nome));
				n++;
			}
		}
		json_liberar(j);
	}
	g_free(buf);
	n_celulares = n;
	gv_na_linha_principal(celulares_chegaram, NULL);
	return NULL;
}

static void procurar_celulares(void) {
	if (celulares_lendo) {
		return;
	}
	celulares_lendo = true;
	g_thread_unref(g_thread_new("galeria-celulares", celulares_thread, NULL));
}

typedef struct {
	char id[64], nome[96];
	GPtrArray *caminhos;
	int enviados;
	char *erro;
} envio;

static void envio_pronto(void *dados) {
	envio *e = dados;
	if (e->erro) {
		aviso("%s", e->erro);
	} else {
		aviso("Oferecido a %s: ele puxa %s (veja o progresso no Painel > LUMNA Connect)", e->nome, e->enviados == 1 ? "o arquivo" : "os arquivos");
	}
	g_free(e->erro);
	g_ptr_array_free(e->caminhos, TRUE);
	g_free(e);
}

static gpointer envio_thread(gpointer dados) {
	envio *e = dados;
	for (guint i = 0; i < e->caminhos->len; i++) {
		char *pedido = g_strdup_printf("enviar %s %s", e->id, (char *)e->caminhos->pdata[i]), r[1024] = "";
		if (!connectc_pedido(pedido, r, sizeof(r))) {
			e->erro = g_strdup("O serviço do celular não respondeu");
		} else {
			struct json *j = json_ler(r);
			if (json_bool(json_chave(j, "ok"), false)) {
				e->enviados++;
			} else if (!e->erro) {
				e->erro = g_strdup(*json_chave_texto(j, "erro") ? json_chave_texto(j, "erro") : "Não deu para enviar");
			}
			json_liberar(j);
		}
		g_free(pedido);
	}
	gv_na_linha_principal(envio_pronto, e);
	return NULL;
}

static void compartilhar_com(int c) {
	GPtrArray *s = selecionados();
	envio *e = g_new0(envio, 1);
	g_strlcpy(e->id, celulares[c].id, sizeof(e->id));
	g_strlcpy(e->nome, celulares[c].nome, sizeof(e->nome));
	e->caminhos = g_ptr_array_new_with_free_func(g_free);
	for (guint i = 0; i < s->len; i++) {
		g_ptr_array_add(e->caminhos, g_strdup(s->pdata[i]));
	}
	g_ptr_array_free(s, TRUE);
	g_thread_unref(g_thread_new("galeria-enviar", envio_thread, e));
}

/* ---------------- abrir ---------------- */

/* Fora da Galeria: no mpv (janela separada) ou no aplicativo padrão do sistema. */
void abrir_no_player_externo(const gl_midia *m) {
	char *mpv = G.cfg.video_com == GL_VIDEO_PADRAO ? NULL : g_find_program_in_path("mpv");
	char *volume = g_strdup_printf("--volume=%d", G.cfg.volume);
	if (mpv) {
		char *argv[] = { mpv, volume, G.cfg.repetir ? "--loop=inf" : "--loop=no", "--force-window=yes", "--", (char *)m->caminho, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
	} else {
		char *argv[] = { "xdg-open", (char *)m->caminho, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
	}
	g_free(mpv);
	g_free(volume);
}

void abrir_midia(int k) {
	if (k < 0 || k >= G.nvis) {
		return;
	}
	const gl_midia *m = midia_vis(k);
	if (m->tipo == GL_VIDEO) {
		if (G.cfg.video_com == GL_VIDEO_GALERIA) {
			player_abrir(k);
		} else {
			abrir_no_player_externo(m);
		}
	} else {
		visualizador_abrir(k);
	}
}

/* ---------------- vistas e ajustes ---------------- */

void mudar_vista(int vista, int album) {
	G.vista = vista;
	G.album = album;
	G.tela = T_BIBLIOTECA;
	G.busca[0] = '\0';
	G.ancora = -1;
	sel_limpar();
	gl_miniaturas_descartar();
	if (vista == V_LIXEIRA) {
		iniciar_carga(true);
	}
	refiltrar();
	gv_rolar_para(ID_ROLAGEM, 0, 0, false);
}

void aplicar_ajustes(const gl_cfg *novos) {
	bool refazer = novos->npastas != G.cfg.npastas || novos->subpastas != G.cfg.subpastas || novos->agrupar != G.cfg.agrupar;
	for (int i = 0; !refazer && i < novos->npastas; i++) {
		refazer |= strcmp(novos->pastas[i], G.cfg.pastas[i]) != 0;
	}
	bool vigia = refazer || novos->buscar_auto != G.cfg.buscar_auto;
	bool miniaturas = novos->qualidade != G.cfg.qualidade || novos->cache != G.cfg.cache;
	G.cfg = *novos;
	gravar_cfg();
	gl_cache_usar_disco(G.cfg.cache);
	if (miniaturas) {
		gl_miniaturas_descartar();
	}
	if (vigia) {
		vigiar_pastas();
	}
	if (refazer) {
		recarregar_biblioteca();
	}
	if (G.vista == V_LIXEIRA && !G.cfg.lixeira_na_barra) {
		mudar_vista(V_TODAS, -1);
	}
	reordenar();
	aviso("Configurações aplicadas");
}

/* ---------------- diálogos ---------------- */

enum { D_NENHUM, D_NOVO_ALBUM, D_RENOMEAR_ALBUM, D_IMPORTAR, D_APAGAR, D_ESVAZIAR };
static int dlg;
static char dlg_texto[256];
static char dlg_erro[160];
static bool dlg_levar_selecao;   /* "Novo álbum" aberto pelo Adicionar ao álbum: a seleção entra nele */
static int album_menu = -1;      /* o álbum do menu de contexto da barra lateral */
static char menu_caminho[4096];  /* a mídia do menu de contexto */

static void dialogo(Clay_ElementId id, float largura, const char *titulo, void (*corpo)(void), void (*botoes)(void)) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_IDI("dlg-cartao", id.id), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIT(0) },
			.padding = CLAY_PADDING_ALL(22), .childGap = 14 }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
			.border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			corpo();
			CLAY(CLAY_IDI("dlg-botoes", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				botoes();
			}
		}
	}
}

static void texto_dialogo(const char *t) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
		gv_texto(gv_str(t), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static bool confirmou;
static void campo_do_dialogo(const char *dica) {
	int r = gv_campo(CLAY_ID("dlg-campo"), dlg_texto, sizeof(dlg_texto), dica, GV_CAMPO_TEXTO);
	if (r & GV_CAMPO_ENTER) {
		confirmou = true;
	}
	if (dlg_erro[0]) {
		gv_texto(gv_str(dlg_erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
	}
}

static void corpo_album(void) {
	texto_dialogo(dlg == D_NOVO_ALBUM ? "Dê um nome ao álbum." : "Escolha o novo nome do álbum.");
	campo_do_dialogo("Nome do álbum");
}

static void corpo_importar(void) {
	texto_dialogo("Digite o caminho de uma pasta (ou de um arquivo). As fotos e os vídeos dela entram na biblioteca. Também dá para soltar arquivos na janela.");
	campo_do_dialogo("/home/você/Fotos do celular");
}

static void corpo_apagar(void) {
	GPtrArray *s = selecionados();
	texto_dialogo(s->len == 1 ? "Este item será apagado para sempre. Não dá para desfazer." : gv_fmt("Estes %u itens serão apagados para sempre. Não dá para desfazer.", s->len).chars);
	g_ptr_array_free(s, TRUE);
}

static void corpo_esvaziar(void) {
	texto_dialogo("Tudo o que está na lixeira será apagado para sempre. Não dá para desfazer.");
}

static void botoes_padrao(const char *ok, bool perigo) {
	if (gv_botao_ext(CLAY_ID("dlg-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
		dlg = D_NENHUM;
	}
	if (gv_botao_ext(CLAY_ID("dlg-ok"), &(gv_botao_opcoes){ .texto = ok, .estilo = perigo ? GV_BOTAO_PERIGO : GV_BOTAO_CHEIO })) {
		confirmou = true;
	}
}

static void botoes_album(void) {
	botoes_padrao(dlg == D_NOVO_ALBUM ? "Criar" : "Renomear", false);
}
static void botoes_importar(void) {
	botoes_padrao("Importar", false);
}
static void botoes_apagar(void) {
	botoes_padrao("Apagar para sempre", true);
}
static void botoes_esvaziar(void) {
	botoes_padrao("Esvaziar", true);
}

static void abrir_dialogo(int d, const char *texto) {
	dlg = d;
	g_strlcpy(dlg_texto, texto ? texto : "", sizeof(dlg_texto));
	dlg_erro[0] = '\0';
	gv_focar(CLAY_ID("dlg-campo"));
}

static void pedir_apagar_para_sempre(void) {
	GPtrArray *s = selecionados();
	bool algum = s->len > 0;
	g_ptr_array_free(s, TRUE);
	if (!algum) {
		return;
	}
	if (G.cfg.confirmar_apagar) {
		dlg = D_APAGAR;
	} else {
		GPtrArray *c = selecionados();
		op_iniciar(AR_OP_APAGAR, (char *const *)c->pdata, (int)c->len, NULL);
		g_ptr_array_free(c, TRUE);
		sel_limpar();
	}
}

static void restaurar_selecao(void) {
	/* a operação de restaurar quer o nome do item dentro da pasta da lixeira (não o caminho inteiro) */
	GPtrArray *s = selecionados();
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	for (guint i = 0; i < s->len; i++) {
		g_ptr_array_add(nomes, g_path_get_basename(s->pdata[i]));
	}
	op_iniciar(AR_OP_RESTAURAR, (char *const *)nomes->pdata, (int)nomes->len, NULL);
	g_ptr_array_free(nomes, TRUE);
	g_ptr_array_free(s, TRUE);
	sel_limpar();
}

static void dialogos(void) {
	confirmou = false;
	if (dlg == D_NOVO_ALBUM || dlg == D_RENOMEAR_ALBUM) {
		dialogo(CLAY_ID("dlg-album"), 460, dlg == D_NOVO_ALBUM ? "Novo álbum" : "Renomear o álbum", corpo_album, botoes_album);
	} else if (dlg == D_IMPORTAR) {
		dialogo(CLAY_ID("dlg-importar"), 560, "Importar", corpo_importar, botoes_importar);
	} else if (dlg == D_APAGAR) {
		dialogo(CLAY_ID("dlg-apagar"), 440, "Apagar para sempre?", corpo_apagar, botoes_apagar);
	} else if (dlg == D_ESVAZIAR) {
		dialogo(CLAY_ID("dlg-esvaziar"), 440, "Esvaziar a lixeira?", corpo_esvaziar, botoes_esvaziar);
	} else if (op && op_st.pergunta) {
		/* a operação parou numa pergunta: já existe um arquivo com este nome */
		static bool todos;
		CLAY(CLAY_ID("dlg-conflito"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
			.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
			CLAY(CLAY_ID("dlg-conflito-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(480), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(22), .childGap = 14 },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				gv_texto(CLAY_STRING("Já existe"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
				texto_dialogo(gv_fmt("Já existe um arquivo chamado \"%s\" na pasta de destino.", op_st.pergunta_nome).chars);
				CLAY(CLAY_ID("dlg-todos"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_interruptor(CLAY_ID("dlg-todos-i"), &todos);
					gv_texto(CLAY_STRING("Fazer o mesmo com os próximos"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
				CLAY(CLAY_ID("dlg-conf-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
					if (gv_botao_ext(CLAY_ID("cf-pular"), &(gv_botao_opcoes){ .texto = "Pular", .estilo = GV_BOTAO_ELEVADO })) {
						ar_op_responder(op, AR_C_PULAR, todos);
					}
					if (gv_botao_ext(CLAY_ID("cf-ambos"), &(gv_botao_opcoes){ .texto = "Manter os dois", .estilo = GV_BOTAO_ELEVADO })) {
						ar_op_responder(op, AR_C_MANTER_AMBOS, todos);
					}
					if (gv_botao_ext(CLAY_ID("cf-subst"), &(gv_botao_opcoes){ .texto = "Substituir", .estilo = GV_BOTAO_CHEIO })) {
						ar_op_responder(op, AR_C_SUBSTITUIR, todos);
					}
				}
			}
		}
	}
	if (!confirmou) {
		if (dlg != D_NENHUM && gv_tecla() == 0xff1b) {
			dlg = D_NENHUM;
		}
		return;
	}
	switch (dlg) {
	case D_NOVO_ALBUM:
		if (!gl_album_criar(dlg_texto)) {
			g_snprintf(dlg_erro, sizeof(dlg_erro), "Esse nome não serve (vazio, repetido ou com colchetes).");
			return;
		}
		gl_albuns_liberar(&G.albuns);
		gl_albuns_ler(&G.albuns);
		if (dlg_levar_selecao) {
			char *n = g_strstrip(g_strdup(dlg_texto));
			adicionar_ao_album(n);
			g_free(n);
		}
		break;
	case D_RENOMEAR_ALBUM:
		if (album_menu < 0 || album_menu >= G.albuns.n || !gl_album_renomear(G.albuns.v[album_menu].nome, dlg_texto)) {
			g_snprintf(dlg_erro, sizeof(dlg_erro), "Esse nome não serve (vazio, repetido ou com colchetes).");
			return;
		}
		gl_albuns_liberar(&G.albuns);
		gl_albuns_ler(&G.albuns);
		if (G.vista == V_ALBUM) {
			G.vista = V_TODAS;
		}
		refiltrar();
		break;
	case D_IMPORTAR: {
		char *p = g_strstrip(g_strdup(dlg_texto));
		if (p[0] == '~') {
			char *x = g_strconcat(g_get_home_dir(), p + 1, NULL);
			g_free(p);
			p = x;
		}
		if (!g_file_test(p, G_FILE_TEST_EXISTS)) {
			g_snprintf(dlg_erro, sizeof(dlg_erro), "Esse caminho não existe.");
			g_free(p);
			return;
		}
		char *lista[] = { p, NULL };
		importar_caminhos(lista);
		g_free(p);
		break;
	}
	case D_APAGAR: {
		GPtrArray *c = selecionados();
		op_iniciar(AR_OP_APAGAR, (char *const *)c->pdata, (int)c->len, NULL);
		g_ptr_array_free(c, TRUE);
		sel_limpar();
		break;
	}
	case D_ESVAZIAR: {
		GPtrArray *c = g_ptr_array_new();
		for (int i = 0; i < G.lixo.n; i++) {
			g_ptr_array_add(c, G.lixo.v[i].caminho);
		}
		op_iniciar(AR_OP_APAGAR, (char *const *)c->pdata, (int)c->len, NULL);
		g_ptr_array_free(c, TRUE);
		break;
	}
	default: break;
	}
	dlg = D_NENHUM;
	dlg_levar_selecao = false;
}

/* ---------------- a barra lateral ---------------- */

static bool item_lateral(Clay_ElementId id, const char *icone, const char *texto, bool ativo, int n) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42) }, .padding = { 14, 10, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativo ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		gv_icone(icone, 22, ativo ? gv_t.texto : gv_t.texto_secundario);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo, ativo ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (n > 0 && ativo) {
			gv_texto(gv_fmt("%d", n), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	return gv_tocado(id);
}

static void barra_lateral(void) {
	CLAY(CLAY_ID("g-lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(246), CLAY_SIZING_GROW(0) } },
		.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
		Clay_ElementId rol = CLAY_ID("g-lat-rolagem");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = { 12, 12, 10, 10 }, .childGap = 2 } })) {
			CLAY(CLAY_ID("g-lat-t"), { .layout = { .padding = { 8, 8, 10, 8 } } }) {
				gv_texto(CLAY_STRING("Biblioteca"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_NORMAL);
			}
			static const char *const ICONES[] = { "image", "image", "video", "star", "clock", "trash" };
			for (int v = 0; v <= V_LIXEIRA; v++) {
				if (v == V_LIXEIRA && !G.cfg.lixeira_na_barra) {
					continue;
				}
				bool ativa = G.vista == v;
				if (item_lateral(CLAY_IDI("g-vista", (uint32_t)v), ICONES[v], NOMES_VISTA[v], ativa, 0) && !ativa) {
					mudar_vista(v, -1);
				}
			}
			CLAY(CLAY_ID("g-lat-div"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) }, .padding = { 0, 0, 0, 0 } }, .backgroundColor = gv_t.borda }) {}
			CLAY(CLAY_ID("g-alb-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 0, 12, 4 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Álbuns"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_botao_redondo(CLAY_ID("g-alb-novo"), "plus", 34, false, false)) {
					dlg_levar_selecao = false;
					abrir_dialogo(D_NOVO_ALBUM, "");
				}
			}
			for (int i = 0; i < G.albuns.n; i++) {
				Clay_ElementId id = CLAY_IDI("g-album", (uint32_t)i);
				bool ativo = G.vista == V_ALBUM && G.album == i;
				bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-album"));
				if (pediu) {
					album_menu = i;
				}
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(48) }, .padding = { 10, 10, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = ativo ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
					const gl_midia *capa = NULL;
					for (guint k = 0; k < G.albuns.v[i].caminhos->len && !capa; k++) {
						capa = achar_na_biblioteca(G.albuns.v[i].caminhos->pdata[k]);
					}
					if (capa) {
						miniatura(capa, 34, CLAY_IDI("g-album-capa", (uint32_t)i), false);
					} else {
						gv_icone("folder", 26, gv_t.texto_secundario);
					}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
						gv_texto(gv_str(G.albuns.v[i].nome), gv_t.tamanho_corpo, ativo ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
					}
				}
				if (gv_tocado(id) && !pediu) {
					mudar_vista(V_ALBUM, i);
				}
			}
		}
		CLAY(CLAY_ID("g-lat-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 8, 12 } }, .border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
			if (item_lateral(CLAY_ID("g-config"), "settings", "Configurações", false, 0)) {
				ajustes_abrir(0);
			}
		}
	}
}

/* ---------------- o cabeçalho, a barra de seleção e o rodapé ---------------- */

static const char *titulo_da_vista(void) {
	if (G.vista == V_ALBUM && G.album >= 0 && G.album < G.albuns.n) {
		return G.albuns.v[G.album].nome;
	}
	return NOMES_VISTA[G.vista];
}

static void cabecalho(void) {
	static const char *const ORDENS[] = { "Mais recentes", "Mais antigas", "Nome", "Tamanho" };
	CLAY(CLAY_ID("g-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(78) }, .padding = { 24, 24, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_ID("g-titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(titulo_da_vista()), 32, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("g-busca-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(280) } } }) {
			if (gv_campo(CLAY_ID("g-busca"), G.busca, sizeof(G.busca), "Buscar na galeria", GV_CAMPO_BUSCA) & GV_CAMPO_MUDOU) {
				refiltrar();
				gv_rolar_para(ID_ROLAGEM, 0, 0, false);
			}
		}
		CLAY(CLAY_ID("g-ordem-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(190) } } }) {
			int o = G.cfg.ordem;
			if (gv_lista_suspensa(CLAY_ID("g-ordem"), ORDENS, 4, &o)) {
				G.cfg.ordem = o;
				gravar_cfg();
				reordenar();
			}
		}
		if (gv_botao_ext(CLAY_ID("g-vista-bt"), &(gv_botao_opcoes){ .icone = G.cfg.vista == GL_VISTA_GRADE ? "grid" : "list", .estilo = GV_BOTAO_ELEVADO, .altura = 44 })) {
			gv_popup_alternar(CLAY_ID("g-vista-menu"));
		}
		if (G.vista == V_LIXEIRA) {
			if (G.lixo.n > 0 && gv_botao_ext(CLAY_ID("g-esvaziar"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Esvaziar", .estilo = GV_BOTAO_PERIGO, .altura = 44 })) {
				dlg = D_ESVAZIAR;
			}
		} else if (gv_botao_ext(CLAY_ID("g-importar"), &(gv_botao_opcoes){ .icone = "upload", .texto = "Importar", .estilo = GV_BOTAO_ELEVADO, .altura = 44 })) {
			abrir_dialogo(D_IMPORTAR, "");
		}
	}
	GV_POPUP(CLAY_ID("g-vista-menu"), CLAY_ID("g-vista-bt"), .alinhar = GV_ALINHAR_FIM, .largura = 200) {
		CLAY(CLAY_ID("g-vista-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			if (gv_menu_item_ext(CLAY_ID("g-vm-grade"), "grid", gv_t.texto_secundario, "Grade", NULL, G.cfg.vista == GL_VISTA_GRADE)) {
				G.cfg.vista = GL_VISTA_GRADE;
				gravar_cfg();
				gv_popup_fechar(CLAY_ID("g-vista-menu"));
			}
			if (gv_menu_item_ext(CLAY_ID("g-vm-lista"), "list", gv_t.texto_secundario, "Lista", NULL, G.cfg.vista == GL_VISTA_LISTA)) {
				G.cfg.vista = GL_VISTA_LISTA;
				gravar_cfg();
				gv_popup_fechar(CLAY_ID("g-vista-menu"));
			}
		}
	}
}

static void barra_de_selecao(void) {
	int n = sel_n();
	if (n == 0) {
		return;
	}
	bool lixeira = G.vista == V_LIXEIRA;
	CLAY(CLAY_ID("g-sel"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(52) }, .padding = { 14, 8, 0, 0 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		Clay_ElementId todas = CLAY_ID("g-sel-todas");
		bool tudo = n >= G.nvis;
		CLAY(todas, { .layout = { .sizing = { CLAY_SIZING_FIXED(26), CLAY_SIZING_FIXED(26) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(7) }) {
			gv_icone(tudo ? "check" : "minus", 16, gv_t.sobre_acento);
		}
		if (gv_tocado(todas)) {
			if (tudo) {
				sel_limpar();
			} else {
				sel_intervalo(0, G.nvis - 1);
			}
		}
		CLAY(CLAY_ID("g-sel-txt"), { .layout = { .padding = { 8, 18, 0, 0 } } }) {
			gv_texto(gv_fmt("%d %s", n, n == 1 ? "item selecionado" : "itens selecionados"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("g-sel-sep"), { .layout = { .sizing = { CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(26) } }, .backgroundColor = gv_t.borda }) {}
		if (lixeira) {
			if (gv_botao_ext(CLAY_ID("g-sel-restaurar"), &(gv_botao_opcoes){ .icone = "restore", .texto = "Restaurar", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
				restaurar_selecao();
			}
			if (gv_botao_ext(CLAY_ID("g-sel-apagar"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Excluir para sempre", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
				pedir_apagar_para_sempre();
			}
		} else {
			if (gv_botao_ext(CLAY_ID("g-sel-album"), &(gv_botao_opcoes){ .icone = "folder-plus", .texto = "Adicionar ao álbum", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
				gv_popup_alternar(CLAY_ID("g-album-menu"));
			}
			GPtrArray *s = selecionados();
			bool todas_fav = s->len > 0;
			for (guint i = 0; i < s->len; i++) {
				todas_fav &= gl_fav_tem(s->pdata[i]);
			}
			g_ptr_array_free(s, TRUE);
			if (gv_botao_ext(CLAY_ID("g-sel-fav"), &(gv_botao_opcoes){ .icone = "star", .texto = todas_fav ? "Desfavoritar" : "Favoritar", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
				favoritar_selecao();
			}
			if (gv_botao_ext(CLAY_ID("g-sel-comp"), &(gv_botao_opcoes){ .icone = "share", .texto = "Compartilhar", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
				procurar_celulares();
				gv_popup_alternar(CLAY_ID("g-comp-menu"));
			}
		}
		CLAY(CLAY_ID("g-sel-folga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		if (gv_botao_redondo(CLAY_ID("g-sel-fechar"), "close", 36, false, false)) {
			sel_limpar();
		}
	}
	GV_POPUP(CLAY_ID("g-album-menu"), CLAY_ID("g-sel-album"), .alinhar = GV_ALINHAR_INICIO, .largura = 280) {
		CLAY(CLAY_ID("g-album-menu-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			for (int i = 0; i < G.albuns.n; i++) {
				if (gv_menu_item(CLAY_IDI("g-am-i", (uint32_t)i), "folder", G.albuns.v[i].nome, NULL)) {
					adicionar_ao_album(G.albuns.v[i].nome);
					gv_popup_fechar(CLAY_ID("g-album-menu"));
				}
			}
			if (gv_menu_item(CLAY_ID("g-am-novo"), "plus", "Novo álbum…", NULL)) {
				dlg_levar_selecao = true;
				abrir_dialogo(D_NOVO_ALBUM, "");
				gv_popup_fechar(CLAY_ID("g-album-menu"));
			}
		}
	}
	GV_POPUP(CLAY_ID("g-comp-menu"), CLAY_ID("g-sel-comp"), .alinhar = GV_ALINHAR_INICIO, .largura = 300) {
		CLAY(CLAY_ID("g-comp-menu-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			if (celulares_lendo) {
				CLAY_AUTO_ID({ .layout = { .padding = { 14, 14, 12, 12 } } }) {
					gv_texto(CLAY_STRING("Procurando celulares…"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			for (int c = 0; c < n_celulares && !celulares_lendo; c++) {
				if (gv_menu_item(CLAY_IDI("g-cm-c", (uint32_t)c), "phone", celulares[c].nome, NULL)) {
					compartilhar_com(c);
					gv_popup_fechar(CLAY_ID("g-comp-menu"));
				}
			}
			if (!n_celulares && !celulares_lendo) {
				CLAY_AUTO_ID({ .layout = { .padding = { 14, 14, 12, 4 } } }) {
					gv_texto(CLAY_STRING("Nenhum celular conectado"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			if (gv_menu_item(CLAY_ID("g-cm-copiar"), "copy", "Copiar o caminho", NULL)) {
				GPtrArray *s = selecionados();
				GString *t = g_string_new(NULL);
				for (guint i = 0; i < s->len; i++) {
					g_string_append_printf(t, "%s%s", i ? "\n" : "", (char *)s->pdata[i]);
				}
				gv_copiar(t->str);
				g_string_free(t, TRUE);
				g_ptr_array_free(s, TRUE);
				aviso("Caminho copiado");
				gv_popup_fechar(CLAY_ID("g-comp-menu"));
			}
		}
	}
}

static void rodape(void) {
	CLAY(CLAY_ID("g-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(58) }, .padding = { 28, 24, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		int n = sel_n();
		CLAY(CLAY_ID("g-rod-txt"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .clip = { .horizontal = true } }) {
			const char *t = n > 0 ? gv_fmt("%d %s  ·  %d %s", G.nvis, G.nvis == 1 ? "item" : "itens", n, n == 1 ? "selecionado" : "selecionados").chars
					      : gv_fmt("%d %s", G.nvis, G.nvis == 1 ? "item" : "itens").chars;
			gv_texto(gv_str(t), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			if (aviso_txt[0] && gv_agora() < aviso_ate) {
				gv_texto(gv_str(aviso_txt), gv_t.tamanho_corpo - 1, gv_t.acento, GV_PESO_NORMAL);
			}
			if (op) {
				gv_texto(gv_str(op_st.descricao), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (gv_agora() < desfazer_ate && ar_desfazer_ha(NULL, 0) && !op) {
				if (gv_botao_ext(CLAY_ID("g-desfazer"), &(gv_botao_opcoes){ .icone = "restore", .texto = "Desfazer", .estilo = GV_BOTAO_ELEVADO, .altura = 34 })) {
					char *erro = NULL;
					if (!ar_desfazer(&erro)) {
						aviso("%s", erro ? erro : "Não deu para desfazer");
					} else {
						aviso("Desfeito");
					}
					g_free(erro);
					desfazer_ate = 0;
					recarregar_biblioteca();
				}
			}
		}
		gv_icone("minus", 18, gv_t.texto_suave);
		CLAY(CLAY_ID("g-zoom-cx"), { .layout = { .sizing = { CLAY_SIZING_FIXED(130) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			float v01 = (float)(G.cfg.tam - 96) / 164.0f;
			if (gv_deslizante(CLAY_ID("g-zoom"), &v01, 5, 16)) {
				G.cfg.tam = 96 + (int)(v01 * 164.0f + 0.5f);
				gv_acordar_em(0.5);
			}
		}
		gv_icone("plus", 18, gv_t.texto_suave);
		if (gv_botao_ext(CLAY_ID("g-rod-vista"), &(gv_botao_opcoes){ .icone = G.cfg.vista == GL_VISTA_GRADE ? "grid" : "list", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
			G.cfg.vista = G.cfg.vista == GL_VISTA_GRADE ? GL_VISTA_LISTA : GL_VISTA_GRADE;
			gravar_cfg();
		}
	}
}

/* ---------------- menus de contexto ---------------- */

void menu_da_midia(int k) {
	const gl_midia *m = midia_vis(k);
	g_strlcpy(menu_caminho, m->caminho, sizeof(menu_caminho));
	if (!g_hash_table_contains(G.sel, m->caminho)) { /* o botão direito num item que não estava selecionado o seleciona sozinho */
		sel_limpar();
		g_hash_table_add(G.sel, g_strdup(m->caminho));
	}
}

static void menu_midia_corpo(void) {
	gl_midia *m = achar_na_biblioteca(menu_caminho);
	if (!m && G.vista == V_LIXEIRA) {
		for (int i = 0; i < G.lixo.n; i++) {
			if (!strcmp(G.lixo.v[i].caminho, menu_caminho)) {
				m = &G.lixo.v[i];
			}
		}
	}
	if (!m) {
		return;
	}
	if (G.vista == V_LIXEIRA) {
		if (gv_menu_item(CLAY_ID("mm-restaurar"), "restore", "Restaurar", NULL)) {
			restaurar_selecao();
			gv_popup_fechar(CLAY_ID("menu-midia"));
		}
		if (gv_menu_item(CLAY_ID("mm-apagar"), "trash", "Excluir para sempre", NULL)) {
			pedir_apagar_para_sempre();
			gv_popup_fechar(CLAY_ID("menu-midia"));
		}
		return;
	}
	if (gv_menu_item(CLAY_ID("mm-abrir"), m->tipo == GL_VIDEO ? "play" : "image", m->tipo == GL_VIDEO ? "Reproduzir" : "Abrir", NULL)) {
		for (int k = 0; k < G.nvis; k++) {
			if (!strcmp(midia_vis(k)->caminho, menu_caminho)) {
				abrir_midia(k);
				break;
			}
		}
		gv_popup_fechar(CLAY_ID("menu-midia"));
	}
	if (gv_menu_item(CLAY_ID("mm-fav"), "star", m->favorita ? "Tirar dos favoritos" : "Favoritar", NULL)) {
		favoritar_selecao();
		gv_popup_fechar(CLAY_ID("menu-midia"));
	}
	if (gv_menu_item(CLAY_ID("mm-album"), "folder-plus", "Adicionar ao álbum…", NULL)) {
		gv_popup_fechar(CLAY_ID("menu-midia"));
		gv_popup_abrir(CLAY_ID("g-album-menu"));
	}
	if (gv_menu_item(CLAY_ID("mm-comp"), "share", "Compartilhar…", NULL)) {
		procurar_celulares();
		gv_popup_fechar(CLAY_ID("menu-midia"));
		gv_popup_abrir(CLAY_ID("g-comp-menu"));
	}
	if (G.vista == V_ALBUM && G.album >= 0 && G.album < G.albuns.n && gv_menu_item(CLAY_ID("mm-tirar"), "close", "Tirar deste álbum", NULL)) {
		GPtrArray *s = selecionados();
		for (guint i = 0; i < s->len; i++) {
			gl_album_remover(G.albuns.v[G.album].nome, s->pdata[i]);
		}
		g_ptr_array_free(s, TRUE);
		sel_limpar();
		gl_albuns_liberar(&G.albuns);
		gl_albuns_ler(&G.albuns);
		refiltrar();
		gv_popup_fechar(CLAY_ID("menu-midia"));
	}
	if (gv_menu_item(CLAY_ID("mm-atlas"), "folder", "Mostrar no Atlas", NULL)) {
		char *pasta = g_path_get_dirname(menu_caminho);
		char *argv[] = { "lumna-atlas", pasta, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
		g_free(pasta);
		gv_popup_fechar(CLAY_ID("menu-midia"));
	}
	if (gv_menu_item(CLAY_ID("mm-lixeira"), "trash", "Mover para a lixeira", "Del")) {
		GPtrArray *s = selecionados();
		mover_para_lixeira((char *const *)s->pdata, (int)s->len);
		g_ptr_array_free(s, TRUE);
		gv_popup_fechar(CLAY_ID("menu-midia"));
	}
}

static void menus_de_contexto(void) {
	Clay_ElementId anc = gv_menu_ancora(CLAY_ID("menu-midia"));
	float tela = gv_app_altura_tela(G.app);
	GV_POPUP(CLAY_ID("menu-midia"), anc, .lado = GV_POPUP_ABAIXO, .alinhar = GV_ALINHAR_INICIO, .largura = 280, .distancia = 2, .altura_max = tela > 0 ? fmaxf(tela - 96, 480) : 0) {
		CLAY(CLAY_ID("mm-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			menu_midia_corpo();
		}
	}
	Clay_ElementId anc_a = gv_menu_ancora(CLAY_ID("menu-album"));
	GV_POPUP(CLAY_ID("menu-album"), anc_a, .lado = GV_POPUP_ABAIXO, .alinhar = GV_ALINHAR_INICIO, .largura = 240, .distancia = 2) {
		CLAY(CLAY_ID("ma-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			if (gv_menu_item(CLAY_ID("ma-renomear"), "edit", "Renomear", NULL)) {
				abrir_dialogo(D_RENOMEAR_ALBUM, album_menu >= 0 && album_menu < G.albuns.n ? G.albuns.v[album_menu].nome : "");
				gv_popup_fechar(CLAY_ID("menu-album"));
			}
			if (gv_menu_item(CLAY_ID("ma-apagar"), "trash", "Apagar o álbum", NULL)) {
				if (album_menu >= 0 && album_menu < G.albuns.n) {
					bool era = G.vista == V_ALBUM && G.album == album_menu;
					gl_album_apagar(G.albuns.v[album_menu].nome);
					gl_albuns_liberar(&G.albuns);
					gl_albuns_ler(&G.albuns);
					aviso("Álbum apagado (as fotos continuam onde estão)");
					if (era) {
						mudar_vista(V_TODAS, -1);
					} else {
						refiltrar();
					}
				}
				gv_popup_fechar(CLAY_ID("menu-album"));
			}
		}
	}
}

/* ---------------- o teclado da biblioteca ---------------- */

static void teclado_da_biblioteca(void) {
	if (dlg != D_NENHUM || gv_focado(CLAY_ID("g-busca")) || gv_popup_aberto(CLAY_ID("menu-midia"))) {
		return;
	}
	for (int i = 0;; i++) {
		uint32_t t = gv_tecla_n(i);
		if (!t) {
			break;
		}
		uint32_t mods = gv_modificadores();
		if (t == 0xff1b) {
			sel_limpar();
		} else if ((mods & GV_MOD_CTRL) && (t == 'a' || t == 'A')) {
			sel_intervalo(0, G.nvis - 1);
		} else if (t == 0xffff && sel_n() > 0) {
			if (G.vista == V_LIXEIRA) {
				pedir_apagar_para_sempre();
			} else {
				GPtrArray *s = selecionados();
				mover_para_lixeira((char *const *)s->pdata, (int)s->len);
				g_ptr_array_free(s, TRUE);
			}
		} else if ((t == 'e' || t == 'E') && !(mods & GV_MOD_CTRL) && sel_n() == 1 && G.vista != V_LIXEIRA) { /* editar a mídia selecionada */
			for (int k = 0; k < G.nvis; k++) {
				if (g_hash_table_contains(G.sel, midia_vis(k)->caminho)) {
					if (midia_vis(k)->tipo == GL_FOTO) {
						editor_abrir(midia_vis(k), T_BIBLIOTECA);
					} else {
						editor_video_abrir(midia_vis(k));
					}
					break;
				}
			}
		} else if (t == 0xff0d && sel_n() == 1) {
			for (int k = 0; k < G.nvis; k++) {
				if (g_hash_table_contains(G.sel, midia_vis(k)->caminho)) {
					abrir_midia(k);
					break;
				}
			}
		}
	}
}

/* ---------------- o quadro ---------------- */

static void biblioteca(void) {
	CLAY(CLAY_ID("g-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		barra_lateral();
		CLAY(CLAY_ID("g-principal"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			cabecalho();
			CLAY(CLAY_ID("g-sel-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 0, sel_n() > 0 ? 8 : 0 } } }) {
				barra_de_selecao();
			}
			Clay_ElementId rol = ID_ROLAGEM;
			CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.padding = { 24, 24, 0, 18 } } })) {
				grade_desenhar(rol);
			}
			rodape();
		}
	}
	menus_de_contexto();
}

static void depurar(void); /* em teste */

static void desenhar(gv_app *a, void *dados) {
	aplicar_cargas();
	ino_rever();
	op_rever();
	soltar_rever();
	CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("barra"), "image", "LUMNA Galeria");
		switch (G.tela) {
		case T_VISUALIZADOR: visualizador_desenhar(); break;
		case T_AJUSTES: ajustes_desenhar(); break;
		case T_EDITOR: editor_desenhar(); break;
		case T_EDITOR_VIDEO: editor_video_desenhar(); break;
		case T_PLAYER: player_desenhar(); break;
		default:
			teclado_da_biblioteca();
			biblioteca();
			break;
		}
	}
	if (!ajustes_dialogo()) {
		dialogos();
	}
	if (G.carregando) {
		gv_acordar_em(0.5);
	}
	depurar();
}

/* ---------------- depuração para os testes (LUMNA_GALERIA_DEBUG) ---------------- */

static void depurar(void) {
	static int ligado = -1;
	if (ligado < 0) {
		ligado = getenv("LUMNA_GALERIA_DEBUG") != NULL;
	}
	if (!ligado) {
		return;
	}
	static GHashTable *antes;
	static char *estado_antes;
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	bool mudou = false;
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	GArray *ids = g_array_new(FALSE, FALSE, sizeof(uint32_t));
#define ADD(nome_, id_) do { g_ptr_array_add(nomes, g_strdup(nome_)); Clay_ElementId e_ = (id_); g_array_append_val(ids, e_.id); } while (0)
#define SERIE(base, n) for (int i_ = 0; i_ < (n); i_++) { g_ptr_array_add(nomes, g_strdup_printf("%s/%d", base, i_)); Clay_ElementId e_ = CLAY_IDI(base, (uint32_t)i_); g_array_append_val(ids, e_.id); }
#define F(x) ADD(x, CLAY_ID(x))
	F("g-busca"); F("g-importar"); F("g-vista-bt"); F("g-sel-album"); F("g-sel-fav"); F("g-sel-comp"); F("g-sel-fechar"); F("g-sel-todas"); F("g-config"); F("g-alb-novo");
	F("g-rod-vista"); F("g-esvaziar"); F("g-sel-restaurar"); F("g-sel-apagar"); F("g-desfazer"); F("g-ordem"); F("g-zoom"); F("g-rolagem");
	F("vz-voltar"); F("vz-editar"); F("vz-ant"); F("vz-prox"); F("vz-fav"); F("vz-info"); F("vz-lixeira"); F("vz-mais"); F("vz-menos"); F("vz-foto"); F("vz-area");
	F("cfg-voltar"); F("cfg-aplicar"); F("cfg-cancelar"); F("cfg-restaurar"); F("aj-add"); F("aj-limpar"); F("aj-add-campo"); F("aj-dlg-ok"); F("aj-dlg-cancelar");
	F("dlg-ok"); F("dlg-cancelar"); F("dlg-campo");
	F("menu-midia"); F("mm-editar"); F("mm-abrir"); F("mm-fav"); F("mm-lixeira"); F("mm-album"); F("mm-comp"); F("mm-tirar"); F("mm-restaurar"); F("mm-apagar"); F("mm-atlas");
	F("g-am-novo"); F("g-cm-copiar"); F("ma-renomear"); F("ma-apagar"); F("g-vm-grade"); F("g-vm-lista"); F("g-album-menu"); F("g-comp-menu");
	SERIE("g-vista", 6); SERIE("cfg-pg", 5); SERIE("g-album", 12); SERIE("g-cel", 160); SERIE("g-chk", 160); SERIE("g-lin", 100); SERIE("g-lchk", 100);
	F("ed-voltar"); F("ed-cancelar"); F("ed-salvar"); F("ed-undo"); F("ed-redo"); F("ed-comparar"); F("ed-zoom-menos"); F("ed-zoom-mais"); F("ed-zoom-lista"); F("ed-canvas");
	F("ed-restaurar"); F("ed-recorte-restaurar"); F("ed-girar-restaurar"); F("ed-inverter"); F("ed-girar-e"); F("ed-girar-d"); F("ed-esp-h"); F("ed-esp-v");
	F("ed-dlg-continuar"); F("ed-dlg-descartar"); F("ed-sec-luz"); F("ed-sec-cor"); F("ed-painel-rol");
	F("vd-voltar"); F("vd-cancelar"); F("vd-exportar"); F("vd-play"); F("vd-faixa"); F("vd-previa"); F("vd-mudo"); F("vd-vol"); F("vd-girar"); F("vd-dlg-continuar"); F("vd-dlg-descartar");
	F("pl-voltar"); F("pl-editar"); F("pl-fav"); F("pl-mais"); F("pl-area"); F("pl-central"); F("pl-ant"); F("pl-play"); F("pl-prox"); F("pl-vol-icone"); F("pl-vol"); F("pl-seek"); F("pl-vel"); F("pl-faixas"); F("pl-repetir"); F("pl-tela"); F("pl-erro-mpv"); F("pl-m-mpv"); F("pl-m-lixeira");
	SERIE("pl-vel-i", 6); SERIE("pl-leg-i", 4); SERIE("pl-aud-i", 4);
	SERIE("vd-mais", 2); SERIE("vd-menos", 2);
	SERIE("ed-aba", 4); SERIE("ed-sl", 8); SERIE("ed-chip", 8); SERIE("ed-prop", 9); SERIE("ed-filtro", 9);
	SERIE("aj-pasta", 8); SERIE("aj-pasta-mais", 8); SERIE("g-am-i", 8); SERIE("g-cm-c", 4);
#undef F
#undef SERIE
#undef ADD
	for (guint i = 0; i < nomes->len; i++) {
		Clay_BoundingBox b = gv_caixa((Clay_ElementId){ .id = g_array_index(ids, uint32_t, i) });
		if (b.width <= 0) {
			continue;
		}
		char *linha = g_strdup_printf("%.0f %.0f %.0f %.0f", b.x, b.y, b.width, b.height);
		char *ant = g_hash_table_lookup(antes, nomes->pdata[i]);
		if (!ant || strcmp(ant, linha)) {
			printf("pos %s %s\n", (char *)nomes->pdata[i], linha);
			g_hash_table_insert(antes, g_strdup(nomes->pdata[i]), linha);
			mudou = true;
		} else {
			g_free(linha);
		}
	}
	GString *ordem = g_string_new(NULL);
	for (int k = 0; k < G.nvis && k < 40; k++) {
		g_string_append_printf(ordem, "%s%s", k ? "," : "", midia_vis(k)->nome);
	}
	GString *ed = g_string_new(NULL);
	editor_estado(ed);
	editor_video_estado(ed);
	player_estado(ed);
	char *estado = g_strdup_printf("tela=%d vista=%d album=%d nvis=%d nsel=%d busca=[%s] ordem=%d vista_cfg=%d agrupar=%d ngrupos=%d carregando=%d aberta=%d tam=%d albuns=%d cache=%d primeiro_grupo=[%s] itens=[%s]%s",
		G.tela, G.vista, G.album, G.nvis, sel_n(), G.busca, G.cfg.ordem, G.cfg.vista, G.cfg.agrupar, G.ngrupos, G.carregando, G.aberta, G.cfg.tam, G.albuns.n, G.cfg.cache,
		G.ngrupos ? G.grupos[0].rotulo : "", ordem->str, ed->str);
	g_string_free(ordem, TRUE);
	g_string_free(ed, TRUE);
	if (!estado_antes || strcmp(estado_antes, estado)) {
		printf("estado %s\n", estado);
		g_free(estado_antes);
		estado_antes = estado;
		mudou = true;
	} else {
		g_free(estado);
	}
	fflush(stdout);
	g_ptr_array_free(nomes, TRUE);
	g_array_free(ids, TRUE);
	static int extras;
	if (mudou) {
		extras = 2; /* as caixas são as do quadro anterior (e a troca de tela leva dois quadros): mais quadros e o log fica com o layout de agora */
	}
	if (extras > 0) {
		extras--;
		gv_app_redesenhar(G.app);
	}
}

/* ---------------- main ---------------- */

int main(int argc, char **argv) {
	if (getenv("LUMNA_GALERIA_DEBUG")) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	signal(SIGPIPE, SIG_IGN);
	gl_cfg_ler(&G.cfg);
	gl_cache_usar_disco(G.cfg.cache);
	G.sel = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	G.ancora = -1;
	G.aberta = -1;
	G.album = -1;
	G.vista = V_TODAS;
	gl_albuns_ler(&G.albuns);
	G.app = gv_app_criar(&(gv_opcoes){ .titulo = "LUMNA Galeria", .app_id = "lumna-galeria", .largura = 1240, .altura = 760 });
	if (!G.app) {
		return 1;
	}
	gv_arraste_aceitar(MIME_URIS);
	ino_wds = g_array_new(FALSE, FALSE, sizeof(int));
	ino_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ino_fd >= 0) {
		gv_app_vigiar(G.app, ino_fd, ino_pronto, NULL);
	}
	vigiar_pastas();
	refiltrar();
	recarregar_biblioteca();
	(void)argc;
	(void)argv;
	int r = gv_app_correr(G.app, desenhar, NULL);
	player_encerrar();
	return r;
}
