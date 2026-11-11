/*
 * A Loja do Gaming (incluída pelo jogos.c: usa as peças e o estado dele). Catálogo da Steam (servicos/loja_steam.h): Destaques, Categorias, Ofertas,
 * Lista de desejos (local), Buscar (filtros), a página do jogo (Visão geral, Sobre, Requisitos) e a janela Instalar. Instalar, atualizar e
 * desinstalar são feitos pela Steam escondida (servicos/jogos.h); só Comprar abre a loja da Steam, na hora.
 */
#include "loja_steam.h"

enum { LJ_ABA_GERAL, LJ_ABA_SOBRE, LJ_ABA_REQ, LJ_ABA_N };

static int l_aba, l_sel, l_hero_gen;
static struct ls_item l_dest[LS_MAX], l_ofertas[LS_MAX], l_res[LS_MAX];
static int l_ndest, l_nofertas, l_nres, l_total;
static bool l_carregando, l_carregou, l_busca_ocupada, l_det_ocupado;
static char l_erro[200];
static struct ls_filtro l_filtro;
static char l_busca_txt[96];
static struct ls_detalhe l_det;
static bool l_det_ok;
static char l_det_erro[200], l_proton[16];
static int l_appid;                      /* o jogo aberto na Loja */
static int *l_possui;                    /* os appids da conta (com a chave) */
static int l_npossui;
static char l_possui_msg[160];
static bool l_instalando_dialogo;
static int l_lib_sel;
static struct steam_lib l_libs[8];
static int l_nlibs;
static struct { int appid; char nome[128], capa[400]; } l_desejos[200];
static int l_ndesejos;
static struct steam_prog l_prog;
static double l_prog_em;
static int l_baixando[8], l_nbaixando;
static char l_chave_txt[80], l_id_txt[32];
static bool l_chave_ok;

/* ---------------- tarefas e imagens (a rede nunca bloqueia o desenho) ---------------- */

struct l_tarefa {
	void (*trabalho)(void *);
	void (*pronto)(void *);
	void *dados;
};

static void l_tarefa_pronta(void *d) {
	struct l_tarefa *t = d;
	if (t->pronto) {
		t->pronto(t->dados);
	}
	gv_app_redesenhar(app);
	g_free(t);
}

static gpointer l_tarefa_linha(gpointer d) {
	struct l_tarefa *t = d;
	t->trabalho(t->dados);
	gv_na_linha_principal(l_tarefa_pronta, t);
	return NULL;
}

static void l_em_segundo_plano(void (*trabalho)(void *), void (*pronto)(void *), void *dados) {
	struct l_tarefa *t = g_new0(struct l_tarefa, 1);
	t->trabalho = trabalho;
	t->pronto = pronto;
	t->dados = dados;
	g_thread_unref(g_thread_new("loja", l_tarefa_linha, t));
}

static GHashTable *l_imgs; /* url → arquivo local ("" : baixando, "!" : não deu) */
static int l_img_ativas;

struct l_img_job {
	char url[400], *arq;
};

static void l_img_trabalho(void *d) {
	struct l_img_job *j = d;
	j->arq = ls_imagem(j->url);
}

static void l_img_pronto(void *d) {
	struct l_img_job *j = d;
	l_img_ativas--;
	g_hash_table_insert(l_imgs, g_strdup(j->url), g_strdup(j->arq ? j->arq : "!"));
	g_free(j->arq);
	g_free(j);
}

/* o arquivo da imagem, se já veio; senão pede e devolve NULL (o quadro seguinte a encontra) */
static const char *l_img(const char *url) {
	if (!url || !*url) {
		return NULL;
	}
	if (!l_imgs) {
		l_imgs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	const char *v = g_hash_table_lookup(l_imgs, url);
	if (v) {
		return v[0] && strcmp(v, "!") ? v : NULL;
	}
	if (l_img_ativas >= 4) {
		return NULL; /* no máximo 4 ao mesmo tempo: as outras esperam o quadro seguinte */
	}
	g_hash_table_insert(l_imgs, g_strdup(url), g_strdup(""));
	struct l_img_job *j = g_new0(struct l_img_job, 1);
	g_strlcpy(j->url, url, sizeof(j->url));
	l_img_ativas++;
	l_em_segundo_plano(l_img_trabalho, l_img_pronto, j);
	return NULL;
}

/* ---------------- dados ---------------- */

static bool l_tem(const struct ls_item *it) {
	for (int i = 0; i < l_npossui; i++) {
		if (l_possui[i] == it->appid) {
			return true;
		}
	}
	for (int i = 0; i < njogos; i++) {
		if (lista[i].origem == ORIGEM_STEAM && atoi(lista[i].ref) == it->appid) {
			return true;
		}
	}
	return false;
}

static bool l_appid_tem(int appid) {
	struct ls_item it = { .appid = appid };
	return l_tem(&it);
}

static char *l_arq_desejos(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "desejos.json", NULL);
}

static void l_desejos_ler(void) {
	char *a = l_arq_desejos(), *t = NULL;
	l_ndesejos = 0;
	if (g_file_get_contents(a, &t, NULL, NULL)) {
		struct json *j = json_ler(t);
		const struct json *l = json_chave(j, "jogos");
		for (int i = 0; l && i < l->nfilhos && l_ndesejos < 200; i++) {
			const struct json *x = json_item(l, i);
			l_desejos[l_ndesejos].appid = (int)json_numero(json_chave(x, "id"), 0);
			g_strlcpy(l_desejos[l_ndesejos].nome, json_chave_texto(x, "nome"), sizeof(l_desejos[0].nome));
			g_strlcpy(l_desejos[l_ndesejos].capa, json_chave_texto(x, "capa"), sizeof(l_desejos[0].capa));
			if (l_desejos[l_ndesejos].appid > 0) {
				l_ndesejos++;
			}
		}
		json_liberar(j);
		g_free(t);
	}
	g_free(a);
}

static void l_desejos_gravar(void) {
	GString *s = g_string_new("{\"jogos\":[");
	for (int i = 0; i < l_ndesejos; i++) {
		g_string_append_printf(s, "%s{\"id\":%d,\"nome\":", i ? "," : "", l_desejos[i].appid);
		json_escrever_texto(s, l_desejos[i].nome);
		g_string_append(s, ",\"capa\":");
		json_escrever_texto(s, l_desejos[i].capa);
		g_string_append_c(s, '}');
	}
	g_string_append(s, "]}");
	char *a = l_arq_desejos(), *dir = g_path_get_dirname(a);
	g_mkdir_with_parents(dir, 0700);
	g_file_set_contents(a, s->str, -1, NULL);
	g_free(dir);
	g_free(a);
	g_string_free(s, TRUE);
}

static int l_desejo_indice(int appid) {
	for (int i = 0; i < l_ndesejos; i++) {
		if (l_desejos[i].appid == appid) {
			return i;
		}
	}
	return -1;
}

static void l_desejo_alternar(int appid, const char *nome, const char *capa) {
	int i = l_desejo_indice(appid);
	if (i >= 0) {
		memmove(&l_desejos[i], &l_desejos[i + 1], (size_t)(l_ndesejos - i - 1) * sizeof(l_desejos[0]));
		l_ndesejos--;
	} else if (l_ndesejos < 200) {
		l_desejos[l_ndesejos].appid = appid;
		g_strlcpy(l_desejos[l_ndesejos].nome, nome, sizeof(l_desejos[0].nome));
		g_strlcpy(l_desejos[l_ndesejos].capa, capa, sizeof(l_desejos[0].capa));
		l_ndesejos++;
	}
	l_desejos_gravar();
}

struct l_dest_job {
	struct ls_item dest[LS_MAX], ofertas[LS_MAX];
	int ndest, nofertas;
	char erro[200];
	int genero;
};

static void l_dest_trabalho(void *d) {
	struct l_dest_job *j = d;
	if (j->genero > 0) {
		struct ls_filtro f = { .genero = j->genero, .ordem = 1 };
		int total;
		j->ndest = ls_buscar(&f, j->dest, 12, &total, j->erro, sizeof(j->erro));
	} else {
		j->ndest = ls_mais_vendidos(j->dest, 12, j->erro, sizeof(j->erro));
	}
	if (!j->nofertas && !j->genero) {
		j->nofertas = ls_ofertas(j->ofertas, LS_MAX, j->erro, sizeof(j->erro));
	}
}

static void l_dest_pronto(void *d) {
	struct l_dest_job *j = d;
	l_carregando = false;
	l_carregou = true;
	memcpy(l_dest, j->dest, sizeof(l_dest));
	l_ndest = j->ndest;
	if (!j->genero) {
		memcpy(l_ofertas, j->ofertas, sizeof(l_ofertas));
		l_nofertas = j->nofertas;
	}
	g_strlcpy(l_erro, l_ndest || l_nofertas ? "" : j->erro, sizeof(l_erro));
	if (l_sel >= l_ndest) {
		l_sel = 0;
	}
	g_free(j);
}

static void l_carregar_destaques(int genero) {
	if (l_carregando) {
		return;
	}
	l_carregando = true;
	struct l_dest_job *j = g_new0(struct l_dest_job, 1);
	j->genero = genero;
	l_hero_gen = genero;
	l_em_segundo_plano(l_dest_trabalho, l_dest_pronto, j);
}

struct l_busca_job {
	struct ls_filtro f;
	struct ls_item res[LS_MAX];
	int n, total;
	char erro[200];
};

static void l_busca_trabalho(void *d) {
	struct l_busca_job *j = d;
	j->n = ls_buscar(&j->f, j->res, 30, &j->total, j->erro, sizeof(j->erro));
}

static void l_busca_pronto(void *d) {
	struct l_busca_job *j = d;
	l_busca_ocupada = false;
	memcpy(l_res, j->res, sizeof(l_res));
	l_nres = j->n;
	l_total = j->total;
	g_strlcpy(l_erro, j->n ? "" : j->erro, sizeof(l_erro));
	l_sel = 0;
	g_free(j);
}

static void l_buscar(void) {
	if (l_busca_ocupada) {
		return;
	}
	l_busca_ocupada = true;
	struct l_busca_job *j = g_new0(struct l_busca_job, 1);
	j->f = l_filtro;
	g_strlcpy(j->f.termo, l_busca_txt, sizeof(j->f.termo));
	l_em_segundo_plano(l_busca_trabalho, l_busca_pronto, j);
}

struct l_det_job {
	int appid;
	struct ls_detalhe d;
	char proton[16], erro[200];
	bool ok;
};

static void l_det_trabalho(void *d) {
	struct l_det_job *j = d;
	j->ok = ls_detalhes(j->appid, &j->d, j->erro, sizeof(j->erro));
	if (j->ok && j->d.windows) {
		ls_proton(j->appid, j->proton, sizeof(j->proton));
	}
}

static void l_det_pronto(void *d) {
	struct l_det_job *j = d;
	l_det_ocupado = false;
	if (j->appid == l_appid) {
		l_det_ok = j->ok;
		l_det = j->d;
		g_strlcpy(l_proton, j->proton, sizeof(l_proton));
		g_strlcpy(l_det_erro, j->ok ? "" : j->erro, sizeof(l_det_erro));
	}
	g_free(j);
}

static void l_abrir_jogo(int appid) {
	l_appid = appid;
	tela = T_LOJA_JOGO;
	l_aba = LJ_ABA_GERAL;
	l_det_ok = false;
	l_det_erro[0] = '\0';
	l_proton[0] = '\0';
	l_instalando_dialogo = false;
	l_prog_em = 0;
	struct l_det_job *j = g_new0(struct l_det_job, 1);
	j->appid = appid;
	l_det_ocupado = true;
	l_em_segundo_plano(l_det_trabalho, l_det_pronto, j);
}

struct l_possui_job {
	int ids[4000], n;
	char erro[160];
};

static void l_possui_trabalho(void *d) {
	struct l_possui_job *j = d;
	j->n = ls_possuidos(j->ids, 4000, j->erro, sizeof(j->erro));
}

static void l_possui_pronto(void *d) {
	struct l_possui_job *j = d;
	g_free(l_possui);
	l_possui = j->n > 0 ? g_memdup2(j->ids, (gsize)j->n * sizeof(int)) : NULL;
	l_npossui = j->n > 0 ? j->n : 0;
	g_strlcpy(l_possui_msg, j->n >= 0 ? "" : j->erro, sizeof(l_possui_msg));
	g_free(j);
}

static void l_abrir_loja(void) {
	tela = T_LOJA;
	confirmando = false;
	adicionando = false;
	l_desejos_ler();
	l_chave_ok = ls_chave_existe();
	if (!l_carregou) {
		l_carregar_destaques(0);
		if (l_chave_ok) {
			l_em_segundo_plano(l_possui_trabalho, l_possui_pronto, g_new0(struct l_possui_job, 1));
		}
	}
}

/* ---------------- instalar ---------------- */

static int64_t l_espaco_necessario(const struct ls_detalhe *d) {
	/* os requisitos trazem "Armazenamento: 68 GB de espaço disponível": o maior número seguido de GB/MB depois dessa palavra */
	const char *p = strstr(d->requisitos, "Armazenamento");
	if (!p) {
		p = strstr(d->requisitos, "Storage");
	}
	if (!p) {
		p = strstr(d->requisitos, "Espaço");
	}
	if (!p) {
		return 0;
	}
	for (const char *c = p; *c && c < p + 80; c++) {
		if (g_ascii_isdigit(*c)) {
			char *fim;
			double v = g_strtod(c, &fim);
			while (*fim == ' ') {
				fim++;
			}
			if (!g_ascii_strncasecmp(fim, "GB", 2)) {
				return (int64_t)(v * 1e9);
			}
			if (!g_ascii_strncasecmp(fim, "MB", 2)) {
				return (int64_t)(v * 1e6);
			}
		}
	}
	return 0;
}

static void l_baixando_adicionar(int appid) {
	for (int i = 0; i < l_nbaixando; i++) {
		if (l_baixando[i] == appid) {
			return;
		}
	}
	if (l_nbaixando < 8) {
		l_baixando[l_nbaixando++] = appid;
	}
}

static void l_instalar_agora(void) {
	char erro[200] = "";
	const char *lib = l_lib_sel > 0 && l_lib_sel < l_nlibs ? l_libs[l_lib_sel].caminho : NULL;
	if (jogos_steam_instalar(l_appid, l_det.nome, lib, erro, sizeof(erro))) {
		l_baixando_adicionar(l_appid);
		l_instalando_dialogo = false;
		l_prog_em = 0;
		avisar("A instalação começou.");
	} else {
		avisar(erro);
	}
}

static void l_dialogo_instalar(void) {
	int64_t nec = l_espaco_necessario(&l_det);
	if (!l_nlibs) {
		l_nlibs = jogos_steam_bibliotecas(l_libs, 8);
	}
	CLAY(CLAY_ID("li-fundo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER }, .zIndex = 50, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE },
		.backgroundColor = (Clay_Color){ 0, 0, 0, 170 } }) {
		CLAY(CLAY_ID("li-caixa"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(760)) }, .padding = CLAY_PADDING_ALL(U(28)), .childGap = U(14) },
			.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(U(14)), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(gv_fmt("Instalar %s", l_det.nome), U(30), gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(l_appid_tem(l_appid) || l_det.gratis ? "Jogo adquirido" : "Ainda não adquirido: a Steam vai pedir a compra."), U(18), l_appid_tem(l_appid) || l_det.gratis ? gv_t.sucesso : gv_t.aviso, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Local de instalação (uma biblioteca da Steam)"), U(17), gv_t.texto_secundario, GV_PESO_MEDIO);
			const char *nomes[8];
			char rot[8][480];
			for (int i = 0; i < l_nlibs; i++) {
				g_snprintf(rot[i], sizeof(rot[i]), "%s%s · %.0f GB livres", l_libs[i].caminho, l_libs[i].padrao ? " (principal)" : "", (double)l_libs[i].livre / 1e9);
				nomes[i] = rot[i];
			}
			if (l_nlibs) {
				gv_lista_suspensa(CLAY_ID("li-local"), nomes, l_nlibs, &l_lib_sel);
			} else {
				gv_texto(CLAY_STRING("Nenhuma biblioteca da Steam encontrada (a Steam está instalada?)."), U(17), gv_t.aviso, GV_PESO_NORMAL);
			}
			int64_t livre = l_nlibs ? l_libs[l_lib_sel < l_nlibs ? l_lib_sel : 0].livre : 0, total = l_nlibs ? l_libs[l_lib_sel < l_nlibs ? l_lib_sel : 0].total : 0;
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(24), .padding = { 0, 0, U(8), 0 } } }) {
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
					gv_texto(CLAY_STRING("Espaço necessário"), U(15), gv_t.texto_secundario, GV_PESO_NORMAL);
					gv_texto(gv_str(nec ? gv_fmt("%.1f GB", (double)nec / 1e9).chars : "não informado"), U(26), gv_t.texto, GV_PESO_MEDIO);
				}
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
					gv_texto(CLAY_STRING("Livre depois de instalar"), U(15), gv_t.texto_secundario, GV_PESO_NORMAL);
					gv_texto(gv_str(l_nlibs ? gv_fmt("%.1f GB", (double)(livre - nec) / 1e9).chars : "—"), U(26), nec && livre < nec ? gv_t.perigo : gv_t.texto, GV_PESO_MEDIO);
				}
			}
			if (total > 0) {
				float usado = (float)(total - livre) / (float)total, nova = nec > 0 ? (float)nec / (float)total : 0;
				CLAY(CLAY_ID("li-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(10)) } }, .backgroundColor = (Clay_Color){ 255, 255, 255, 25 }, .cornerRadius = CLAY_CORNER_RADIUS(U(5)) }) {
					CLAY(CLAY_ID("li-usado"), { .layout = { .sizing = { CLAY_SIZING_PERCENT(usado > 1 ? 1 : usado), CLAY_SIZING_GROW(0) } }, .backgroundColor = roxo(1), .cornerRadius = CLAY_CORNER_RADIUS(U(5)) }) {}
					CLAY(CLAY_ID("li-nova"), { .layout = { .sizing = { CLAY_SIZING_PERCENT(nova + usado > 1 ? 1 - usado : nova), CLAY_SIZING_GROW(0) } }, .backgroundColor = roxo(0.5f) }) {}
				}
			}
			if (nec && livre < nec) {
				gv_texto(CLAY_STRING("Não cabe nessa biblioteca: escolha outra."), U(17), gv_t.perigo, GV_PESO_NORMAL);
			}
			gv_texto(CLAY_STRING("O download começa em segundo plano, sem abrir a janela da Steam. O tamanho do download a Steam só informa depois de começar."), U(15), gv_t.texto_suave, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12), .padding = { 0, 0, U(6), 0 } } }) {
				if (gv_botao_ext(CLAY_ID("li-nao"), &(gv_botao_opcoes){ .texto = "Agora não", .estilo = GV_BOTAO_ELEVADO, .crescer = true, .altura = U(54) }) || tecla(XKB_KEY_Escape)) {
					l_instalando_dialogo = false;
				}
				if (gv_botao_ext(CLAY_ID("li-ok"), &(gv_botao_opcoes){ .icone = "download", .texto = "Instalar", .estilo = GV_BOTAO_CHEIO, .crescer = true, .altura = U(54), .desativado = !l_nlibs || (nec && livre < nec) })) {
					l_instalar_agora();
				}
			}
		}
	}
}

/* ---------------- peças ---------------- */

static void l_capa(Clay_ElementId id, const char *url, float larg, float alt, bool foco, const char *nome) {
	const char *arq = l_img(url);
	Clay_ElementDeclaration d = { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(larg), CLAY_SIZING_FIXED(alt) }, .padding = CLAY_PADDING_ALL(U(10)), .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } },
		.backgroundColor = (Clay_Color){ 40, 30, 70, 255 }, .cornerRadius = CLAY_CORNER_RADIUS(U(8)), .border = { .color = foco ? roxo(1) : (Clay_Color){ 255, 255, 255, 30 }, .width = GV_BORDA(foco ? 3 : 1) } };
	if (arq) {
		CLAY(id, gv_imagem(id, arq, GV_AJUSTE_COBRIR, d)) {}
	} else {
		CLAY(id, d) {
			if (nome) {
				gv_texto(gv_str(nome), U(14), gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
}

static const char *l_preco_txt(const struct ls_item *it, bool *possui) {
	*possui = l_tem(it);
	return it->gratis ? "Grátis" : it->preco;
}

static void l_card(const char *base, int idx, const struct ls_item *it, float larg, float alt, bool foco) {
	Clay_ElementId id = CLAY_SID(gv_fmt("%s-%d", base, idx));
	CLAY(CLAY_SID(gv_fmt("%s-c%d", base, idx)), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = U(6) } }) {
		bool ativou = gv_focavel(id, U(8));
		l_capa(id, it->capa, larg, alt, foco, it->nome);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(larg) } } }) {
			gv_texto(gv_str(it->nome), U(18), foco ? gv_t.texto : gv_t.texto_secundario, GV_PESO_MEDIO);
		}
		bool possui;
		const char *preco = l_preco_txt(it, &possui);
		if (possui) {
			gv_texto(CLAY_STRING("Na biblioteca"), U(16), gv_t.acento, GV_PESO_MEDIO);
		} else {
			CLAY_AUTO_ID({ .layout = { .childGap = U(8), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (it->desconto > 0) {
					CLAY_AUTO_ID({ .layout = { .padding = { U(6), U(6), U(2), U(2) } }, .backgroundColor = (Clay_Color){ 76, 175, 80, 255 }, .cornerRadius = CLAY_CORNER_RADIUS(U(4)) }) {
						gv_texto(gv_fmt("-%d%%", it->desconto), U(14), (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_MEDIO);
					}
				}
				gv_texto(gv_str(preco), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		if (gv_tocado(id) || ativou) {
			l_abrir_jogo(it->appid);
		}
	}
}

/* ---------------- Destaques ---------------- */

static void l_chips_genero(void) {
	CLAY(CLAY_ID("l-chips"), { .layout = { .childGap = U(10), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		for (int g = 0; g < LS_GEN_N; g++) {
			if (gv_botao_ext(CLAY_IDI("l-chip", (uint32_t)g), &(gv_botao_opcoes){ .texto = ls_gen_nome(g), .estilo = l_hero_gen == g ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO, .altura = U(42) })) {
				l_sel = 0;
				l_carregar_destaques(g);
			}
		}
	}
}

static void l_sub_destaques(void) {
	if (l_sel >= l_ndest) {
		l_sel = l_ndest ? l_ndest - 1 : 0;
	}
	const struct ls_item *h = l_ndest ? &l_dest[l_sel] : NULL;
	CLAY(CLAY_ID("l-dest"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("l-hero"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(72), U(72), U(20), U(10) }, .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (h) {
				CLAY(CLAY_ID("l-hero-t"), { .layout = { .sizing = { CLAY_SIZING_FIXED(U(900)) } } }) {
					gv_texto(gv_str(h->nome), U(64), gv_t.texto, GV_PESO_SEMINEGRITO);
				}
				bool possui = l_tem(h);
				gv_texto(gv_str(h->desconto > 0 ? gv_fmt("Oferta: -%d%%", h->desconto).chars : possui ? "Na sua biblioteca" : h->gratis ? "Grátis para jogar" : "Em destaque na Steam"), U(20), gv_t.texto_secundario, GV_PESO_NORMAL);
				CLAY_AUTO_ID({ .layout = { .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					if (botao_u(CLAY_ID("l-ver"), "play", "Ver jogo", GV_BOTAO_CHEIO, false)) {
						l_abrir_jogo(h->appid);
					}
					if (!possui && botao_u(CLAY_ID("l-preco"), NULL, h->gratis ? "Grátis" : h->preco, GV_BOTAO_CONTORNO, false)) {
						l_abrir_jogo(h->appid);
					}
				}
			} else if (l_carregando) {
				gv_texto(CLAY_STRING("Carregando a loja…"), U(30), gv_t.texto_secundario, GV_PESO_NORMAL);
			} else {
				gv_texto(gv_str(l_erro[0] ? l_erro : "A loja não respondeu."), U(24), gv_t.aviso, GV_PESO_NORMAL);
				if (botao_u(CLAY_ID("l-tentar"), "refresh", "Tentar de novo", GV_BOTAO_CHEIO, false)) {
					l_carregar_destaques(0);
				}
			}
		}
		CLAY(CLAY_ID("l-desc"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), 0, U(14) }, .childGap = U(12) } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(CLAY_STRING("Descubra sua próxima aventura"), U(30), gv_t.texto, GV_PESO_MEDIO);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
				l_chips_genero();
			}
			CLAY(CLAY_ID("l-fila"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(16) } }) {
				for (int i = 0; i < l_ndest && i < 5; i++) {
					l_card("l-c", i, &l_dest[i], U(300), U(138), i == l_sel);
				}
			}
		}
	}
}

static void l_grade(const char *base, struct ls_item *itens, int n, int colunas, float larg, float alt) {
	for (int linha = 0; linha * colunas < n; linha++) {
		CLAY(CLAY_IDI("lg", (uint32_t)(linha + (int)base[0] * 100)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(18) } }) {
			for (int c = 0; c < colunas && linha * colunas + c < n; c++) {
				int i = linha * colunas + c;
				l_card(base, i, &itens[i], larg, alt, i == l_sel);
			}
		}
	}
}

static void l_sub_ofertas(void) {
	CLAY(CLAY_ID("l-of"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), U(18), U(10) }, .childGap = U(16) } }) {
		gv_texto(CLAY_STRING("Ofertas"), U(36), gv_t.texto, GV_PESO_SEMINEGRITO);
		Clay_ElementId rol = CLAY_ID("l-of-rol");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(18) } })) {
			if (!l_nofertas) {
				gv_texto(gv_str(l_carregando ? "Carregando…" : "Nenhuma oferta agora."), U(20), gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			l_grade("l-o", l_ofertas, l_nofertas, 4, U(330), U(155));
		}
	}
}

static void l_abrir_busca(int genero);

static void l_sub_categorias(void) {
	CLAY(CLAY_ID("l-cat"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), U(18), U(10) }, .childGap = U(18) } }) {
		gv_texto(CLAY_STRING("Categorias"), U(36), gv_t.texto, GV_PESO_SEMINEGRITO);
		CLAY_AUTO_ID({ .layout = { .childGap = U(16) } }) {
			for (int g = 1; g < LS_GEN_N; g++) {
				if (botao_u(CLAY_IDI("l-cat-b", (uint32_t)g), NULL, ls_gen_nome(g), GV_BOTAO_ELEVADO, false)) {
					l_abrir_busca(g);
				}
			}
		}
		gv_texto(CLAY_STRING("Toque numa categoria para ver os jogos dela, com filtros de preço, compatibilidade e jogadores."), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void l_sub_desejos(void) {
	CLAY(CLAY_ID("l-des"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), U(18), U(10) }, .childGap = U(16) } }) {
		gv_texto(CLAY_STRING("Lista de desejos"), U(36), gv_t.texto, GV_PESO_SEMINEGRITO);
		gv_texto(CLAY_STRING("A lista fica só neste computador (a da Steam só se altera pelo site dela)."), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		if (!l_ndesejos) {
			gv_texto(CLAY_STRING("Nenhum jogo na lista ainda. Na página de um jogo, toque em \"Lista de desejos\"."), U(20), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		static struct ls_item itens[200];
		for (int i = 0; i < l_ndesejos; i++) {
			memset(&itens[i], 0, sizeof(itens[i]));
			itens[i].appid = l_desejos[i].appid;
			g_strlcpy(itens[i].nome, l_desejos[i].nome, sizeof(itens[i].nome));
			g_strlcpy(itens[i].capa, l_desejos[i].capa, sizeof(itens[i].capa));
			g_strlcpy(itens[i].preco, "—", sizeof(itens[i].preco));
		}
		Clay_ElementId rol = CLAY_ID("l-des-rol");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(18) } })) {
			l_grade("l-w", itens, l_ndesejos, 4, U(330), U(155));
		}
	}
}

static void t_loja(void) {
	static const char *const subs[LS_SUB_N] = { "Destaques", "Categorias", "Ofertas", "Lista de desejos" };
	CLAY(CLAY_ID("loja"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("l-sub"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), 0, 0 }, .childGap = U(18), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = (Clay_Color){ 8, 6, 20, 120 } }) {
			int s = l_sub;
			if (gv_abas(CLAY_ID("l-subabas"), subs, LS_SUB_N, &s)) {
				l_sub = s;
				l_sel = 0;
				if (s == LS_SUB_DESEJOS) {
					l_desejos_ler();
				}
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			CLAY(CLAY_ID("l-busca-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(U(420)) } } }) {
				int r = gv_campo(CLAY_ID("l-busca"), l_busca_txt, sizeof(l_busca_txt), "Buscar jogos", GV_CAMPO_BUSCA);
				if (r & GV_CAMPO_ENTER) {
					l_abrir_busca(0);
				}
			}
		}
		switch (l_sub) {
		case LS_SUB_DEST: l_sub_destaques(); break;
		case LS_SUB_CATEG: l_sub_categorias(); break;
		case LS_SUB_OFERTAS: l_sub_ofertas(); break;
		default: l_sub_desejos(); break;
		}
	}
}

/* ---------------- Buscar ---------------- */

static void l_abrir_busca(int genero) {
	tela = T_BUSCA;
	l_filtro.genero = genero;
	l_sel = 0;
	l_buscar();
}

static void t_busca(void) {
	CLAY(CLAY_ID("busca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), U(12), U(10) }, .childGap = U(14) } }) {
		gv_texto(CLAY_STRING("Buscar jogos"), U(36), gv_t.texto, GV_PESO_SEMINEGRITO);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			int r = gv_campo(CLAY_ID("b-campo"), l_busca_txt, sizeof(l_busca_txt), "Buscar jogos", GV_CAMPO_BUSCA);
			if (r & GV_CAMPO_ENTER) {
				l_buscar();
			}
		}
		CLAY_AUTO_ID({ .layout = { .childGap = U(10) } }) {
			for (int g = 0; g < LS_GEN_N; g++) {
				if (gv_botao_ext(CLAY_IDI("b-gen", (uint32_t)g), &(gv_botao_opcoes){ .texto = ls_gen_nome(g), .estilo = l_filtro.genero == g ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO, .altura = U(40) })) {
					l_filtro.genero = g;
					l_buscar();
				}
			}
		}
		CLAY(CLAY_ID("b-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(26) } }) {
			CLAY(CLAY_ID("b-filtros"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(330)) }, .padding = CLAY_PADDING_ALL(U(16)), .childGap = U(6) },
				.backgroundColor = (Clay_Color){ 255, 255, 255, 10 }, .cornerRadius = CLAY_CORNER_RADIUS(U(10)) }) {
				gv_texto(CLAY_STRING("Filtros"), U(26), gv_t.texto, GV_PESO_MEDIO);
				bool mudou = false;
				mudou |= gv_linha(CLAY_ID("b-gratis"), NULL, "Só grátis", NULL, &l_filtro.gratis);
				mudou |= gv_linha(CLAY_ID("b-linux"), NULL, "Linux nativo", NULL, &l_filtro.linux_nativo);
				mudou |= gv_linha(CLAY_ID("b-um"), NULL, "Um jogador", NULL, &l_filtro.um_jogador);
				mudou |= gv_linha(CLAY_ID("b-coop"), NULL, "Cooperativo", NULL, &l_filtro.cooperativo);
				if (mudou) {
					l_buscar();
				}
			}
			CLAY(CLAY_ID("b-res"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(12) } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_texto(gv_str(l_busca_ocupada ? "Buscando…" : gv_fmt("%d resultado%s", l_total, l_total == 1 ? "" : "s").chars), U(26), gv_t.texto, GV_PESO_MEDIO);
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
					static const char *const ordens[4] = { "Ordenar: Relevância", "Ordenar: Mais vendidos", "Ordenar: Lançamento", "Ordenar: Menor preço" };
					CLAY(CLAY_ID("b-ordem-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(U(320)) } } }) {
						if (gv_lista_suspensa(CLAY_ID("b-ordem"), ordens, 4, &l_filtro.ordem)) {
							l_buscar();
						}
					}
				}
				Clay_ElementId rol = CLAY_ID("b-rol");
				CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(16) } })) {
					if (!l_nres && !l_busca_ocupada) {
						gv_texto(gv_str(l_erro[0] ? l_erro : "Nenhum jogo com esses filtros."), U(20), gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					l_grade("b-c", l_res, l_nres, 3, U(360), U(168));
				}
			}
		}
	}
}

/* ---------------- a página do jogo ---------------- */

static void l_painel_compra(void) {
	bool tem = l_appid_tem(l_appid);
	struct steam_prog *pr = &l_prog;
	bool instalado = pr->instalado && !pr->baixando;
	CLAY(CLAY_ID("lj-lado"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(U(22)), .childGap = U(10) },
		.backgroundColor = (Clay_Color){ 255, 255, 255, 12 }, .cornerRadius = CLAY_CORNER_RADIUS(U(10)), .border = { .color = (Clay_Color){ 255, 255, 255, 30 }, .width = GV_BORDA(1) } }) {
		if (pr->baixando) {
			gv_texto(CLAY_STRING("Baixando"), U(26), gv_t.texto, GV_PESO_MEDIO);
			CLAY(CLAY_ID("lj-pbar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(12)) } }, .backgroundColor = (Clay_Color){ 255, 255, 255, 25 }, .cornerRadius = CLAY_CORNER_RADIUS(U(6)) }) {
				CLAY(CLAY_ID("lj-pfill"), { .layout = { .sizing = { CLAY_SIZING_PERCENT(pr->pct / 100.0f), CLAY_SIZING_GROW(0) } }, .backgroundColor = roxo(1), .cornerRadius = CLAY_CORNER_RADIUS(U(6)) }) {}
			}
			gv_texto(gv_fmt("%d%%%s", pr->pct, pr->total > 0 ? gv_fmt(" · %.1f de %.1f GB", (double)pr->baixado / 1e9, (double)pr->total / 1e9).chars : " · preparando…"), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		} else if (instalado) {
			CLAY_AUTO_ID({ .layout = { .childGap = U(10), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(12)), CLAY_SIZING_FIXED(U(12)) } }, .backgroundColor = gv_t.sucesso, .cornerRadius = CLAY_CORNER_RADIUS(U(6)) }) {}
				gv_texto(CLAY_STRING("Instalado"), U(26), gv_t.texto, GV_PESO_MEDIO);
			}
			if (pr->atualizacao && botao_u(CLAY_ID("lj-atualizar"), "download", "Atualizar", GV_BOTAO_CHEIO, false)) {
				char e[160] = "";
				jogos_steam_instalar(l_appid, l_det.nome, NULL, e, sizeof(e));
				l_baixando_adicionar(l_appid);
			}
			if (botao_u(CLAY_ID("lj-jogar"), "play", "Ver na biblioteca", GV_BOTAO_CHEIO, false)) {
				char id[32];
				g_snprintf(id, sizeof(id), "steam:%d", l_appid);
				struct jogo *j = por_id(id);
				if (!j) {
					relistar();
					j = por_id(id);
				}
				if (j) {
					abrir_detalhes(j);
				}
			}
			if (botao_u(CLAY_ID("lj-desinstalar"), "trash", "Desinstalar", GV_BOTAO_CONTORNO, false)) {
				char id[32], e[160] = "";
				bool saiu;
				g_snprintf(id, sizeof(id), "steam:%d", l_appid);
				struct jogo *j = por_id(id);
				if (!j) {
					relistar(); /* acabou de instalar: a biblioteca ainda não leu o manifesto */
					j = por_id(id);
				}
				if (j && jogos_desinstalar(j, false, &saiu, e, sizeof(e))) {
					avisar("A Steam abriu para desinstalar.");
				} else if (e[0]) {
					avisar(e);
				}
			}
		} else {
			gv_texto(CLAY_STRING("Edição padrão"), U(22), gv_t.texto, GV_PESO_MEDIO);
			if (tem || l_det.gratis) {
				gv_texto(gv_str(tem ? "Na sua biblioteca" : "Grátis"), U(34), gv_t.texto, GV_PESO_SEMINEGRITO);
				if (botao_u(CLAY_ID("lj-instalar"), "download", "Instalar", GV_BOTAO_CHEIO, false)) {
					l_nlibs = jogos_steam_bibliotecas(l_libs, 8);
					l_lib_sel = 0;
					l_instalando_dialogo = true;
				}
			} else {
				CLAY_AUTO_ID({ .layout = { .childGap = U(10), .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
					gv_texto(gv_str(l_det.preco[0] ? l_det.preco : "—"), U(40), gv_t.texto, GV_PESO_SEMINEGRITO);
					if (l_det.desconto > 0) {
						gv_texto(gv_fmt("-%d%% (era %s)", l_det.desconto, l_det.preco_antes), U(17), gv_t.sucesso, GV_PESO_MEDIO);
					}
				}
				if (botao_u(CLAY_ID("lj-comprar"), "bag", "Comprar", GV_BOTAO_CHEIO, false)) {
					char e[160] = "";
					if (jogos_steam_comprar(l_appid, e, sizeof(e))) {
						avisar("A loja da Steam abriu para a compra.");
					} else {
						avisar(e);
					}
				}
				bool na_lista = l_desejo_indice(l_appid) >= 0;
				if (gv_botao_ext(CLAY_ID("lj-desejo"), &(gv_botao_opcoes){ .icone = "heart", .texto = na_lista ? "Na lista de desejos" : "Lista de desejos", .estilo = na_lista ? GV_BOTAO_CHEIO : GV_BOTAO_CONTORNO, .altura = U(52) })) {
					l_desejo_alternar(l_appid, l_det.nome, l_det.capa);
				}
				gv_texto(CLAY_STRING("Ainda não adquirido"), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_ID("lj-sep"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
		if (l_det.um_jogador) {
			CLAY_AUTO_ID({ .layout = { .childGap = U(12) } }) { gv_icone("user", U(24), gv_t.icone); gv_texto(CLAY_STRING("Um jogador"), U(19), gv_t.texto, GV_PESO_NORMAL); }
		}
		if (l_det.cooperativo) {
			CLAY_AUTO_ID({ .layout = { .childGap = U(12) } }) { gv_icone("users", U(24), gv_t.icone); gv_texto(CLAY_STRING("Cooperativo"), U(19), gv_t.texto, GV_PESO_NORMAL); }
		}
		if (l_det.controle) {
			CLAY_AUTO_ID({ .layout = { .childGap = U(12) } }) { gv_icone("gamepad", U(24), gv_t.icone); gv_texto(CLAY_STRING("Controle compatível"), U(19), gv_t.texto, GV_PESO_NORMAL); }
		}
		CLAY_AUTO_ID({ .layout = { .childGap = U(12) } }) {
			gv_icone("stack", U(24), gv_t.icone);
			gv_texto(gv_str(l_det.linux_nativo ? "Linux nativo" : l_proton[0] ? ls_proton_texto(l_proton) : l_det.windows ? "Via Proton" : "Sem versão para Linux"), U(19), gv_t.texto, GV_PESO_NORMAL);
		}
	}
}

static void t_loja_jogo(void) {
	if (gv_agora() - l_prog_em > 1.0) {
		jogos_steam_progresso(l_appid, &l_prog);
		l_prog_em = gv_agora();
		if (l_prog.baixando) {
			l_baixando_adicionar(l_appid);
		}
	}
	if (l_prog.baixando) {
		gv_acordar_em(1.0);
	}
	CLAY(CLAY_ID("lj"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("lj-hero"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(230)) }, .padding = { U(64), U(64), U(10), U(12) }, .childGap = U(8), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao_ext(CLAY_ID("lj-voltar"), &(gv_botao_opcoes){ .icone = "arrow-left", .texto = "Loja", .estilo = GV_BOTAO_SIMPLES, .altura = U(36) })) {
				tela = T_LOJA;
			}
			gv_texto(gv_str(l_det_ok ? l_det.nome : l_det_ocupado ? "Carregando…" : "Jogo não encontrado"), U(60), gv_t.texto, GV_PESO_SEMINEGRITO);
			gv_texto(gv_str(l_det.generos), U(21), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("lj-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = (Clay_Color){ 12, 9, 26, 235 } }) {
			static const char *const abas[LJ_ABA_N] = { "Visão geral", "Sobre", "Requisitos" };
			CLAY(CLAY_ID("lj-abas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { U(64), U(64), U(8), 0 } } }) {
				gv_abas(CLAY_ID("lj-aba"), abas, LJ_ABA_N, &l_aba);
			}
			CLAY(CLAY_ID("lj-dois"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(64), U(64), U(14), U(10) }, .childGap = U(34) } }) {
				Clay_ElementId rol = CLAY_ID("lj-rol");
				CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(14) } })) {
					if (!l_det_ok) {
						gv_texto(gv_str(l_det_ocupado ? "Buscando na loja…" : l_det_erro[0] ? l_det_erro : "A loja não tem esse jogo."), U(22), gv_t.texto_secundario, GV_PESO_NORMAL);
					} else if (l_aba == LJ_ABA_GERAL) {
						CLAY(CLAY_ID("lj-resumo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
							gv_texto(gv_str(l_det.resumo), U(22), gv_t.texto, GV_PESO_NORMAL);
						}
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(14) } }) {
							for (int i = 0; i < l_det.ntelas && i < 3; i++) {
								Clay_ElementId id = CLAY_IDI("lj-tela", (uint32_t)i);
								const char *arq = l_img(l_det.telas[i]);
								Clay_ElementDeclaration d = { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(170)) } }, .backgroundColor = (Clay_Color){ 40, 30, 70, 255 }, .cornerRadius = CLAY_CORNER_RADIUS(U(8)) };
								if (arq) {
									CLAY(id, gv_imagem(id, arq, GV_AJUSTE_COBRIR, d)) {}
								} else {
									CLAY(id, d) {}
								}
							}
						}
						if (l_det.trailer[0] && botao_u(CLAY_ID("lj-trailer"), "play", "Ver o trailer", GV_BOTAO_CONTORNO, false)) {
							char *a[] = { "mpv", "--force-window=yes", "--no-terminal", l_det.trailer, NULL };
							g_spawn_async(NULL, a, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
						}
					} else if (l_aba == LJ_ABA_SOBRE) {
						gv_texto(gv_fmt("Desenvolvedora: %s", l_det.desenvolvedor[0] ? l_det.desenvolvedor : "—"), U(19), gv_t.texto_secundario, GV_PESO_NORMAL);
						gv_texto(gv_fmt("Lançamento: %s%s", l_det.lancamento[0] ? l_det.lancamento : "—", l_det.metacritic ? gv_fmt(" · Metacritic %d", l_det.metacritic).chars : ""), U(19), gv_t.texto_secundario, GV_PESO_NORMAL);
						CLAY(CLAY_ID("lj-sobre"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
							gv_texto(gv_str(l_det.sobre[0] ? l_det.sobre : l_det.resumo), U(20), gv_t.texto, GV_PESO_NORMAL);
						}
					} else {
						gv_texto(CLAY_STRING("Requisitos mínimos"), U(26), gv_t.texto, GV_PESO_MEDIO);
						CLAY(CLAY_ID("lj-req"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
							gv_texto(gv_str(l_det.requisitos[0] ? l_det.requisitos : "A loja não informa os requisitos."), U(19), gv_t.texto, GV_PESO_NORMAL);
						}
					}
				}
				CLAY(CLAY_ID("lj-lado-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(U(500)), CLAY_SIZING_GROW(0) } } }) {
					Clay_ElementId rol2 = CLAY_ID("lj-rol2");
					CLAY(rol2, gv_rolagem(rol2, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
						if (l_det_ok) {
							l_painel_compra();
						}
					}
				}
			}
		}
	}
	if (l_instalando_dialogo && l_det_ok) {
		l_dialogo_instalar();
	}
}

/* ---------------- a chave da Steam (Configurações) ---------------- */

static void cfg_steam(void) {
	CLAY(CLAY_ID("cfg-st"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12) } }) {
		gv_texto(CLAY_STRING("Steam"), U(28), gv_t.texto, GV_PESO_MEDIO);
		gv_texto(CLAY_STRING("Com a chave de API, a Loja mostra o que a sua conta já tem (\"Na biblioteca\"). Sem ela, só o que está instalado."), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(CLAY_STRING("Pegue a chave em steamcommunity.com/dev/apikey (conta com ao menos US$ 5 gastos). O perfil precisa estar com \"Detalhes do jogo\" público."), U(16), gv_t.texto_suave, GV_PESO_NORMAL);
		bool tem = ls_chave_existe();
		gv_texto(gv_str(tem ? "Uma chave já está guardada (ela não aparece aqui). Cole outra para trocar." : "Nenhuma chave guardada."), U(18), tem ? gv_t.sucesso : gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(CLAY_STRING("Chave da API"), U(16), gv_t.texto_secundario, GV_PESO_MEDIO);
		gv_campo(CLAY_ID("st-chave"), l_chave_txt, sizeof(l_chave_txt), "32 letras e números", GV_CAMPO_SENHA);
		gv_texto(CLAY_STRING("Id da Steam (64 bits, 17 dígitos; vazio: o do usuário logado no cliente)"), U(16), gv_t.texto_secundario, GV_PESO_MEDIO);
		gv_campo(CLAY_ID("st-id"), l_id_txt, sizeof(l_id_txt), "7656…", GV_CAMPO_TEXTO);
		if (botao_u(CLAY_ID("st-salvar"), "save", "Salvar", GV_BOTAO_CHEIO, !l_chave_txt[0])) {
			if (ls_chave_gravar(l_chave_txt, l_id_txt)) {
				l_chave_txt[0] = '\0';
				l_carregou = false;
				avisar("Chave guardada.");
				if (l_possui) {
					g_free(l_possui);
					l_possui = NULL;
					l_npossui = 0;
				}
			} else {
				avisar("A chave são 32 letras e números, e o id são 17 dígitos.");
			}
		}
		if (l_possui_msg[0]) {
			gv_texto(gv_str(l_possui_msg), U(16), gv_t.aviso, GV_PESO_NORMAL);
		} else if (l_npossui) {
			gv_texto(gv_fmt("A conta tem %d jogos.", l_npossui), U(17), gv_t.sucesso, GV_PESO_NORMAL);
		}
	}
}
