#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

estado L;

/* ---------------- ajustes ---------------- */

static char *arquivo_cfg(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "loja", "config.ini", NULL);
}

static void ler_cfg(void) {
	L.cfg.aur = true;
	L.cfg.confirmar = true;
	L.cfg.avancado = false;
	L.cfg.helper[0] = '\0';
	GKeyFile *kf = g_key_file_new();
	char *a = arquivo_cfg();
	if (g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL)) {
		L.cfg.aur = g_key_file_get_boolean(kf, "loja", "aur", NULL) || !g_key_file_has_key(kf, "loja", "aur", NULL);
		L.cfg.confirmar = g_key_file_get_boolean(kf, "loja", "confirmar", NULL) || !g_key_file_has_key(kf, "loja", "confirmar", NULL);
		L.cfg.avancado = g_key_file_get_boolean(kf, "loja", "avancado", NULL);
		char *h = g_key_file_get_string(kf, "loja", "helper", NULL);
		if (h && (!strcmp(h, "paru") || !strcmp(h, "yay"))) {
			g_strlcpy(L.cfg.helper, h, sizeof(L.cfg.helper));
		}
		g_free(h);
	}
	g_free(a);
	g_key_file_free(kf);
	L.helper = lj_aur_helper(L.cfg.helper);
}

void gravar_cfg(void) {
	GKeyFile *kf = g_key_file_new();
	g_key_file_set_boolean(kf, "loja", "aur", L.cfg.aur);
	g_key_file_set_boolean(kf, "loja", "confirmar", L.cfg.confirmar);
	g_key_file_set_boolean(kf, "loja", "avancado", L.cfg.avancado);
	g_key_file_set_string(kf, "loja", "helper", L.cfg.helper);
	char *a = arquivo_cfg(), *dir = g_path_get_dirname(a);
	g_mkdir_with_parents(dir, 0700);
	g_key_file_save_to_file(kf, a, NULL);
	g_free(dir);
	g_free(a);
	g_key_file_free(kf);
	L.helper = lj_aur_helper(L.cfg.helper);
}

/* ---------------- a lista de Pacotes ---------------- */

void marcar_sujo(void) {
	L.sujo = true;
	gv_app_redesenhar(L.app);
}

static void refazer_lista(void) {
	L.sujo = false;
	if (!L.cat) {
		L.nres = 0;
		return;
	}
	static const enum lj_buscar_por POR[] = { LJ_POR_NOME, LJ_POR_DESCRICAO, LJ_POR_AMBOS };
	static const enum lj_ordem ORDEM[] = { LJ_RELEVANCIA, LJ_NOME, LJ_TAMANHO, LJ_NOTA };
	static const double NOTA[] = { 0, 4, 3 };
	L.natualiz = lj_contar_atualizaveis(L.cat);
	drivers_atualizar();
	L.f.texto = L.busca;
	L.f.por = POR[CLAMP(L.por_idx, 0, 2)];
	L.f.cat = CLAMP(L.cat_idx, 0, LJ_NCATS - 1);
	L.f.ordem = ORDEM[CLAMP(L.ordem_idx, 0, 3)];
	L.f.nota_min = NOTA[CLAMP(L.nota_idx, 0, 2)];
	bool aur_antes = L.f.aur;
	int total = lj_n(L.cat) + 80;
	lj_filtro g = L.f;
	g.aur = L.f.aur && L.cfg.aur;
	bool lista_do_sistema = L.tela == T_INSTALADOS || L.tela == T_ATUALIZACOES;
	if (lista_do_sistema) {
		/* Instalados e Atualizações usam só a busca do alto: os filtros de Pacotes não valem aqui */
		lj_filtro_padrao(&g);
		g.texto = L.busca;
		g.so_instalados = L.tela == T_INSTALADOS;
		g.so_atualizaveis = L.tela == T_ATUALIZACOES;
		g.ordem = L.busca[0] ? LJ_RELEVANCIA : LJ_NOME;
	}
	if (L.cap_res < total) {
		L.res = g_realloc(L.res, sizeof(int) * (size_t)total);
		L.cap_res = total;
	}
	L.nres = lj_buscar(L.cat, lista_do_sistema ? &g : &L.f, L.res, L.cap_res);
	if (L.nres > L.cap_res) {
		L.nres = L.cap_res;
	}
	if (L.tela == T_INSTALADOS && !L.inst_todos) {
		int k = 0;
		for (int i = 0; i < L.nres; i++) {
			const lj_pacote *p = lj_resultado(L.cat, L.res[i]);
			if (p && p->explicito) {
				L.res[k++] = L.res[i];
			}
		}
		L.nres = k;
	}
	L.f.aur = aur_antes;
}

/* ---------------- o catálogo (thread) ---------------- */

typedef struct {
	lj_catalogo *c;
	lh_hw *hw;
} carga;

static void carga_pronta(void *d) {
	carga *k = d;
	L.cat = k->c;
	L.hw = k->hw;
	L.carregando = false;
	if (!k->c) {
		g_strlcpy(L.erro_carga, "Não consegui ler os repositórios (o pacman está instalado e a lista sincronizada?).", sizeof(L.erro_carga));
	}
	g_free(k);
	marcar_sujo();
}

static gpointer carga_thread(gpointer d) {
	carga *k = g_new0(carga, 1);
	k->c = lj_catalogo_ler();
	k->hw = lh_detectar();
	if (k->c) {
		lj_marcar_atualizacoes(k->c, L.helper);
	}
	gv_na_linha_principal(carga_pronta, k);
	return NULL;
}

void recarregar_catalogo(void) {
	if (L.carregando) {
		return;
	}
	L.carregando = true;
	L.erro_carga[0] = '\0';
	g_thread_unref(g_thread_new("loja-carga", carga_thread, NULL));
}

/* ---------------- o AUR (thread) ---------------- */

typedef struct {
	char termo[256];
	GArray *achados;
	char erro[160];
} busca_aur;

static void aur_pronto(void *d) {
	busca_aur *b = d;
	L.aur_ocupado = false;
	if (b->achados && !strcmp(b->termo, L.busca)) {
		lj_aur_usar(L.cat, b->achados);
		L.aur_erro[0] = '\0';
		marcar_sujo();
	} else if (b->achados) {
		g_array_free(b->achados, TRUE);
	}
	if (!b->achados) {
		g_strlcpy(L.aur_erro, b->erro, sizeof(L.aur_erro));
	}
	if (strcmp(b->termo, L.busca)) {
		L.aur_em = gv_agora() + 0.1;   /* o texto mudou enquanto a rede respondia */
	}
	g_free(b);
	gv_app_redesenhar(L.app);
}

static gpointer aur_thread(gpointer d) {
	busca_aur *b = d;
	b->achados = lj_aur_consultar(L.cat, b->termo, b->erro, sizeof(b->erro));
	gv_na_linha_principal(aur_pronto, b);
	return NULL;
}

void buscar_aur_agora(void) {
	L.aur_em = 0;
	if (!L.cat || !L.cfg.aur || L.aur_ocupado || L.agindo) {
		return;
	}
	if (strlen(L.busca) < 2) {
		lj_aur_limpar(L.cat);
		L.aur_erro[0] = '\0';
		marcar_sujo();
		return;
	}
	busca_aur *b = g_new0(busca_aur, 1);
	g_strlcpy(b->termo, L.busca, sizeof(b->termo));
	g_strlcpy(L.aur_termo, L.busca, sizeof(L.aur_termo));
	L.aur_ocupado = true;
	g_thread_unref(g_thread_new("loja-aur", aur_thread, b));
}

/* ---------------- as ações (thread) ---------------- */

typedef struct {
	int acao;
	char nomes[4][96];
	int nn;
	bool kernel;
	lh_checagem checks[4];
	int nchecks;
	lj_pacote p;
	bool tem_p;
	int codigo;
	char erro[512];
	lj_instalados *ins;
} trabalho;

static lj_instalados *ins_pendente;

static void aplicar_instalados(void) {
	if (ins_pendente && L.cat && !L.aur_ocupado) {
		lj_instalados_usar(L.cat, ins_pendente);
		ins_pendente = NULL;
		marcar_sujo();
	}
}

static void acao_pronta(void *d) {
	trabalho *t = d;
	g_mutex_lock(&L.ag_mutex);
	L.ag_fim = true;
	L.ag_ok = t->codigo == 0;
	g_strlcpy(L.ag_erro, t->erro, sizeof(L.ag_erro));
	g_mutex_unlock(&L.ag_mutex);
	L.agindo = false;
	L.ag_nchecks = t->nchecks;
	memcpy(L.ag_checks, t->checks, sizeof(t->checks));
	if (t->ins) {
		ins_pendente = t->ins;
		aplicar_instalados();
	}
	if (t->codigo == 0 && L.cat && t->nn == 0) {
		lj_marcar_atualizado(L.cat, t->acao == LJ_ATUALIZAR_TUDO ? NULL : t->p.nome);
	}
	g_free(t);
	marcar_sujo();
}

static void linha_da_acao(void *dados, const char *linha) {
	g_mutex_lock(&L.ag_mutex);
	g_ptr_array_add(L.ag_linhas, g_strdup(linha));
	g_mutex_unlock(&L.ag_mutex);
	gv_na_linha_principal((void (*)(void *))gv_app_redesenhar, L.app);
}

static gpointer acao_thread(gpointer d) {
	trabalho *t = d;
	if (t->nn > 0) {
		const char *nomes[4];
		for (int i = 0; i < t->nn; i++) {
			nomes[i] = t->nomes[i];
		}
		t->codigo = lj_executar_nomes((enum lj_acao)t->acao, nomes, t->nn, linha_da_acao, NULL, t->erro, sizeof(t->erro));
	} else {
		t->codigo = lj_executar((enum lj_acao)t->acao, t->tem_p ? &t->p : NULL, L.helper, linha_da_acao, NULL, t->erro, sizeof(t->erro));
	}
	t->ins = lj_instalados_ler();
	if (t->kernel && t->acao == LJ_INSTALAR && t->codigo == 0 && L.hw) {
		/* o pacman terminou: a Loja confere o que ficou em /boot e no gerenciador de boot, e avisa (não mexe no boot) */
		t->nchecks = lh_verificar_kernel(L.hw, t->nomes[0], t->checks, 4);
		for (int i = 0; i < t->nchecks; i++) {
			char *l = g_strdup_printf("%s %s", t->checks[i].ok ? "✓" : "⚠", t->checks[i].texto);
			linha_da_acao(NULL, l);
			g_free(l);
		}
	}
	gv_na_linha_principal(acao_pronta, t);
	return NULL;
}

void iniciar_acao(int acao, const char *nome) {
	if (L.agindo || !L.cat) {
		return;
	}
	trabalho *t = g_new0(trabalho, 1);
	t->acao = acao;
	const lj_pacote *p = nome && *nome ? lj_achar(L.cat, nome) : NULL;
	if (p) {
		t->p = *p;
		t->tem_p = true;
	}
	L.agindo = true;
	L.ag_acao = acao;
	g_strlcpy(L.ag_nome, p ? p->nome : "", sizeof(L.ag_nome));
	g_strlcpy(L.ag_exibicao, p ? p->exibicao : "", sizeof(L.ag_exibicao));
	g_mutex_lock(&L.ag_mutex);
	g_ptr_array_set_size(L.ag_linhas, 0);
	L.ag_fim = L.ag_ok = false;
	L.ag_erro[0] = '\0';
	g_mutex_unlock(&L.ag_mutex);
	L.ag_nchecks = 0;
	L.ag_painel = true;
	L.conf_aberta = false;
	L.det_aberto = false;
	g_thread_unref(g_thread_new("loja-acao", acao_thread, t));
	gv_app_redesenhar(L.app);
}

void iniciar_acao_nomes(int acao, const char *const *nomes, int n, const char *exibicao, bool kernel) {
	if (L.agindo || !L.cat || n <= 0 || n > 4) {
		return;
	}
	trabalho *t = g_new0(trabalho, 1);
	t->acao = acao;
	t->nn = n;
	t->kernel = kernel;
	for (int i = 0; i < n; i++) {
		g_strlcpy(t->nomes[i], nomes[i], sizeof(t->nomes[i]));
	}
	L.agindo = true;
	L.ag_acao = acao;
	g_strlcpy(L.ag_nome, nomes[0], sizeof(L.ag_nome));
	g_strlcpy(L.ag_exibicao, exibicao, sizeof(L.ag_exibicao));
	g_mutex_lock(&L.ag_mutex);
	g_ptr_array_set_size(L.ag_linhas, 0);
	L.ag_fim = L.ag_ok = false;
	L.ag_erro[0] = '\0';
	g_mutex_unlock(&L.ag_mutex);
	L.ag_nchecks = 0;
	L.ag_painel = true;
	L.d_conf_aberta = false;
	L.d_det_aberto = false;   /* o painel do andamento fica por cima de tudo, sem um detalhe aberto tapando */
	g_thread_unref(g_thread_new("loja-acao", acao_thread, t));
	gv_app_redesenhar(L.app);
}

void pedir_acao(int acao, const char *nome) {
	if (L.agindo) {
		return;
	}
	const lj_pacote *p = nome && *nome ? lj_achar(L.cat, nome) : NULL;
	bool aur = p && !strcmp(p->repo, "aur");
	/* o AUR sempre pergunta ao instalar (compila código de terceiros nesta máquina); o resto, só se os ajustes pedirem */
	bool perguntar = acao == LJ_INSTALAR ? (aur || L.cfg.confirmar) : acao == LJ_REMOVER ? L.cfg.confirmar : false;
	if (!perguntar) {
		iniciar_acao(acao, nome);
		return;
	}
	L.conf_aberta = true;
	L.conf_acao = acao;
	g_strlcpy(L.conf_nome, nome ? nome : "", sizeof(L.conf_nome));
	gv_app_redesenhar(L.app);
}

void ir_para(int tela) {
	if (L.busca[0]) {   /* a busca é da página de onde se sai: não filtra a próxima */
		L.busca[0] = '\0';
		L.aur_em = 0;
		if (L.cat) {
			lj_aur_limpar(L.cat);
		}
	}
	L.tela = tela;
	L.det_aberto = false;
	if (tela == T_PACOTES || tela == T_INSTALADOS || tela == T_ATUALIZACOES) {
		L.mostrar = 100;
	}
	gv_rolar_para(CLAY_ID("lj-rolagem"), 0, 0, false);
	marcar_sujo();
}

void abrir_detalhe(const char *nome) {
	g_strlcpy(L.det, nome, sizeof(L.det));
	L.det_aberto = true;
	gv_app_redesenhar(L.app);
}

/* ---------------- depuração (LUMNA_LOJA_DEBUG) ---------------- */

static void depurar(void) {
	static int ligado = -1;
	if (ligado < 0) {
		ligado = getenv("LUMNA_LOJA_DEBUG") != NULL;
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
	F("lj-busca"); F("lj-rolagem"); F("lj-limpar"); F("lj-ordem"); F("lj-por"); F("lj-cat"); F("lj-nota"); F("lj-sem-aval"); F("lj-repo-core"); F("lj-repo-extra"); F("lj-repo-multilib"); F("lj-repo-aur");
	F("lj-mais"); F("lj-atualizar-tudo"); F("lj-ver-deps"); F("lj-recarregar"); F("lj-aur-erro");
	F("lj-ex-ordem"); F("lj-det-fechar"); F("lj-det-acao"); F("lj-det-remover"); F("lj-det-url");
	F("lj-conf-ok"); F("lj-tentar"); F("lj-d-compat"); F("lj-d-hw"); F("lj-hw-fechar"); F("lj-dd-fechar"); F("lj-dd-instalar"); F("lj-dd-remover"); F("lj-dc-ok"); F("lj-dc-cancelar"); F("lj-cfg-avancado"); F("lj-conf-cancelar"); F("lj-ag-fechar"); F("lj-ag-painel"); F("lj-cfg-aur"); F("lj-cfg-confirmar"); F("lj-cfg-helper");
	SERIE("lj-nav", 6); SERIE("lj-d-aba", 2); SERIE("lj-d-ver", 24); SERIE("lj-d-linha", 24); SERIE("lj-tipo", 6);
	{ Clay_ElementId base_ = CLAY_ID("lj-abas"); for (int i_ = 0; i_ < 7; i_++) { g_ptr_array_add(nomes, g_strdup_printf("lj-aba/%d", i_)); Clay_ElementId e_ = Clay__HashStringWithOffset(base_.stringId, base_.offset * 64 + 3000017u + (uint32_t)i_, base_.baseId); g_array_append_val(ids, e_.id); } }
	 SERIE("lj-linha", 120); SERIE("lj-acao", 120); SERIE("lj-cartao", 60); SERIE("lj-cacao", 60);
#undef F
#undef SERIE
#undef ADD
	for (guint i = 0; i < nomes->len; i++) {
		Clay_BoundingBox b = gv_caixa((Clay_ElementId){ .id = g_array_index(ids, uint32_t, i) });
		char *linha = g_strdup_printf("%.0f %.0f %.0f %.0f", b.width > 0 ? b.x : 0, b.width > 0 ? b.y : 0, b.width > 0 ? b.width : 0, b.width > 0 ? b.height : 0);
		char *ant = g_hash_table_lookup(antes, nomes->pdata[i]);
		if ((!ant && b.width > 0) || (ant && strcmp(ant, linha))) {
			printf("pos %s %s\n", (char *)nomes->pdata[i], linha);
			g_hash_table_insert(antes, g_strdup(nomes->pdata[i]), linha);
			mudou = true;
		} else {
			g_free(linha);
		}
	}
	GString *it = g_string_new(NULL);
	int ninst = 0;
	for (int k = 0; k < L.nres; k++) {
		const lj_pacote *q = L.cat ? lj_resultado(L.cat, L.res[k]) : NULL;
		ninst += q && q->instalado;
	}
	for (int k = 0; k < L.nres && k < 30; k++) {
		const lj_pacote *p = L.cat ? lj_resultado(L.cat, L.res[k]) : NULL;
		g_string_append_printf(it, "%s%s", k ? "," : "", p ? p->nome : "?");
	}
	int natual = L.natualiz;
	int ckok = 0;
	for (int i = 0; i < L.ag_nchecks; i++) {
		ckok += L.ag_checks[i].ok;
	}
	int drn = 0;
	for (int i = 0; i < L.nitens_hw; i++) {
		const lh_item *x = &L.itens_hw[i];
		bool aba = L.d_aba == 1 ? x->grupo == LH_KERNEL : x->grupo != LH_KERNEL;
		drn += aba && (!L.d_so_compat || x->instalado || (!x->nao_aplica && x->compat != LH_INCOMPATIVEL));
	}
	GString *hwtxt = g_string_new(NULL);
	for (int i = 0; L.hw && i < L.hw->ngpu; i++) {
		g_string_append_printf(hwtxt, "%s%s/%s", i ? "," : "", L.hw->gpus[i].vendor, L.hw->gpus[i].nome);
	}
	g_mutex_lock(&L.ag_mutex);
	guint nl = L.ag_linhas->len;
	g_mutex_unlock(&L.ag_mutex);
	char *estado = g_strdup_printf("tela=%d carregando=%d npkg=%d nres=%d busca=[%s] aur_ocupado=%d aur_erro=[%s] agindo=%d ag_fim=%d ag_ok=%d ag_linhas=%u ag_nome=[%s] ag_painel=%d det=[%s] conf=%d natual=%d helper=[%s] cfg_aur=%d cfg_conf=%d cfg_helper=[%s] tipo=%d cat=%d ordem=%d ex_cat=%d inst_todos=%d mostrar=%d ninst=%d dr_n=%d dr_aba=%d dr_compat=%d dr_det=[%s] dr_conf=%d hw=[%s] avancado=%d checks=%d checks_ok=%d itens=[%s]",
		L.tela, L.carregando, L.cat ? lj_n(L.cat) : 0, L.nres, L.busca, L.aur_ocupado, L.aur_erro, L.agindo, L.ag_fim, L.ag_ok, nl, L.ag_nome, L.ag_painel, L.det_aberto ? L.det : "", L.conf_aberta, natual,
		L.helper ? L.helper : "", L.cfg.aur, L.cfg.confirmar, L.cfg.helper, L.f.tipo, L.cat_idx, L.ordem_idx, L.ex_cat, L.inst_todos, L.mostrar, ninst, drn, L.d_aba, L.d_so_compat, L.d_det_aberto ? L.d_det : "", L.d_conf_aberta, L.hw ? hwtxt->str : "", L.cfg.avancado, L.ag_nchecks, ckok, it->str);
	g_string_free(it, TRUE);
	g_string_free(hwtxt, TRUE);
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
		extras = 2;
	}
	if (extras > 0) {
		extras--;
		gv_app_redesenhar(L.app);
	}
}

/* ---------------- a janela ---------------- */

static void desenhar(gv_app *a, void *dados) {
	if (L.sujo) {
		refazer_lista();
	}
	aplicar_instalados();
	if (L.aur_em > 0 && gv_agora() >= L.aur_em) {
		buscar_aur_agora();
	}
	CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("barra"), "bag", "Loja LUMNA");
		CLAY(CLAY_ID("lj-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			barra_lateral();
			CLAY(CLAY_ID("lj-principal"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
				barra_do_alto();
				switch (L.tela) {
				case T_PACOTES: tela_pacotes(); break;
				case T_INSTALADOS: tela_instalados(); break;
				case T_ATUALIZACOES: tela_atualizacoes(); break;
				case T_DRIVERS: tela_drivers(); break;
				case T_CONFIG: tela_config(); break;
				default: tela_explorar(); break;
				}
			}
		}
	}
	painel_andamento();
	dialogos();
	if (L.carregando || L.aur_ocupado || L.agindo) {
		gv_acordar_em(0.3);
	}
	if (L.aur_em > 0) {
		gv_acordar_em(0.2);
	}
	depurar();
}

int main(int argc, char **argv) {
	if (getenv("LUMNA_LOJA_DEBUG")) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	signal(SIGPIPE, SIG_IGN);
	g_mutex_init(&L.ag_mutex);
	L.ag_linhas = g_ptr_array_new_with_free_func(g_free);
	ler_cfg();
	lj_filtro_padrao(&L.f);
	L.d_so_compat = true;   /* por padrão a página mostra só o que serve a este computador */
	L.mostrar = 100;
	L.app = gv_app_criar(&(gv_opcoes){ .titulo = "Loja LUMNA", .app_id = "lumna-loja", .largura = 1280, .altura = 800 });
	if (!L.app) {
		return 1;
	}
	recarregar_catalogo();
	(void)argc;
	(void)argv;
	return gv_app_correr(L.app, desenhar, NULL);
}
