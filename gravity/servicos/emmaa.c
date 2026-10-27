#include "emmaa.h"

#include <gio/gio.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emmab.h"
#include "midia.h"
#include "midiac.h"
#include "modos.h"
#include "mpris.h"

/* ---- o catálogo ---- */

static const struct emmaa_def catalogo[] = {
	{ "midia.estado", "O que está na Mídia", "Diz que serviço da Mídia está na frente (Netflix, YouTube...), os serviços disponíveis e o que está tocando.", "{}", "", EMMAA_LER, "auto" },
	{ "midia.abrir", "Abrir um serviço da Mídia",
		"Abre um serviço da Mídia (Netflix, YouTube, Prime Video, Disney+...) pelo nome, na TV ou no monitor do modo Mídia. Se já está aberto, só traz para a frente.",
		"{\"servico\":{\"type\":\"string\",\"description\":\"O nome do serviço, como a pessoa disse (netflix, youtube, prime video)\"},"
		"\"do_comeco\":{\"type\":\"boolean\",\"description\":\"Abrir do começo, sem retomar de onde parou\"}}",
		"servico", EMMAA_AGIR, "auto" },
	{ "midia.fechar", "Fechar um serviço da Mídia", "Fecha um serviço da Mídia que está aberto (o endereço fica nos recentes).",
		"{\"servico\":{\"type\":\"string\",\"description\":\"O nome do serviço\"}}", "servico", EMMAA_AGIR, "auto" },
	{ "midia.inicio", "Início da Mídia", "Vai para a tela de Início da Mídia (os serviços e o que estava vendo).", "{}", "", EMMAA_AGIR, "auto" },
	{ "midia.seletor", "Seletor de serviços", "Abre o seletor de serviços da Mídia por cima do que está passando.", "{}", "", EMMAA_AGIR, "auto" },
	{ "midia.controle", "Controlar o que toca",
		"Controla o que está tocando: tocar, pausar, alternar, próxima, anterior, avançar ou voltar alguns segundos.",
		"{\"acao\":{\"type\":\"string\",\"enum\":[\"tocar\",\"pausar\",\"alternar\",\"proxima\",\"anterior\",\"avancar\",\"voltar\"]},"
		"\"segundos\":{\"type\":\"number\",\"description\":\"Quantos segundos avançar ou voltar (padrão 10)\"}}",
		"acao", EMMAA_AGIR, "auto" },
	{ "midia.navegar", "Mexer na tela da Mídia",
		"Aperta uma tecla de controle remoto na janela da frente: setas, OK, voltar ou o menu de serviços.",
		"{\"direcao\":{\"type\":\"string\",\"enum\":[\"cima\",\"baixo\",\"esquerda\",\"direita\",\"ok\",\"voltar\",\"menu\"]}}", "direcao", EMMAA_AGIR, "auto" },
	{ "midia.digitar", "Digitar na Mídia", "Digita um texto na janela da frente (uma busca no YouTube, por exemplo). Não aperta Enter sozinho.",
		"{\"texto\":{\"type\":\"string\"},\"enter\":{\"type\":\"boolean\",\"description\":\"Apertar Enter no fim\"}}", "texto", EMMAA_AGIR, "auto" },
	{ "audio.volume", "Volume", "Muda o volume do computador: um valor de 0 a 100, mais ou menos em relação ao de agora, ou mudo.",
		"{\"percentual\":{\"type\":\"number\",\"description\":\"O volume de 0 a 100\"},\"delta\":{\"type\":\"number\",\"description\":\"Quanto subir (positivo) ou descer (negativo)\"},"
		"\"mudo\":{\"type\":\"boolean\",\"description\":\"true muda, false tira o mudo\"}}",
		"", EMMAA_AGIR, "auto" },
	{ "mode.atual", "Modo atual", "Diz em que modo cada tela está (Desktop, Gaming ou Mídia).", "{}", "", EMMAA_LER, "auto" },
	{ "mode.trocar", "Trocar de modo", "Troca o modo da tela (desktop, gaming ou midia). Sem monitor, vale para o da frente.",
		"{\"modo\":{\"type\":\"string\",\"enum\":[\"desktop\",\"gaming\",\"midia\"]},\"monitor\":{\"type\":\"string\",\"description\":\"O nome da saída (opcional)\"}}",
		"modo", EMMAA_AGIR, "auto" },
	{ "mode.trocar_telas", "Trocar as telas", "O que está num monitor vai para o outro (a TV em Mídia e o monitor trocam de lugar).", "{}", "", EMMAA_AGIR, "auto" },
	{ "apps.listar", "Listar programas", "Lista os programas instalados. Filtre com query.",
		"{\"query\":{\"type\":\"string\",\"description\":\"Parte do nome ou do tipo\"}}", "", EMMAA_LER, "auto" },
	{ "apps.abrir", "Abrir programa", "Abre um programa instalado pelo nome aproximado (\"firefox\", \"steam\", \"terminal\").",
		"{\"nome\":{\"type\":\"string\"}}", "nome", EMMAA_AGIR, "auto" },
	{ "painel.abrir", "Abrir uma configuração",
		"Abre o Painel de controle numa seção (o jeito de mostrar onde mudar algo). Seções: aparencia/tema, aparencia/papel, aparencia/barra, rede/wifi, bluetooth/dispositivos, "
		"aplicativos/instalados, aplicativos/atualizacoes, telas/monitores, telas/modos, teclado/teclado, teclado/atalhos, teclado/mouse, data/hora, data/fuso, "
		"sobre/computador, sobre/atualizacoes, armazenamento/uso, armazenamento/limpeza, armazenamento/discos, energia/perfil.",
		"{\"secao\":{\"type\":\"string\",\"description\":\"categoria/seção, como teclado/mouse\"}}", "secao", EMMAA_AGIR, "auto" },
	{ "notificar", "Notificar", "Mostra um aviso na tela.", "{\"titulo\":{\"type\":\"string\"},\"texto\":{\"type\":\"string\"}}", "titulo", EMMAA_AGIR, "auto" },
};

const struct emmaa_def *emmaa_catalogo(int *n) {
	*n = (int)G_N_ELEMENTS(catalogo);
	return catalogo;
}

const struct emmaa_def *emmaa_achar(const char *nome) {
	for (guint i = 0; i < G_N_ELEMENTS(catalogo); i++) {
		if (!strcmp(catalogo[i].nome, nome)) {
			return &catalogo[i];
		}
	}
	return NULL;
}

static char *ferramenta_nome(const char *nome) {
	char *s = g_strconcat("lumna_", nome, NULL);
	for (char *p = s; *p; p++) {
		if (*p == '.') {
			*p = '_';
		}
	}
	return s;
}

const struct emmaa_def *emmaa_achar_ferramenta(const char *ferramenta) {
	for (guint i = 0; i < G_N_ELEMENTS(catalogo); i++) {
		char *n = ferramenta_nome(catalogo[i].nome);
		bool igual = !strcmp(n, ferramenta);
		g_free(n);
		if (igual) {
			return &catalogo[i];
		}
	}
	return NULL;
}

bool emmaa_seguro_bloqueado(const struct emmaa_def *d) {
	/* com a tela bloqueada quem fala pode não ser o dono: só volume e o que toca */
	return !strcmp(d->nome, "audio.volume") || !strcmp(d->nome, "midia.controle") || !strcmp(d->nome, "midia.estado");
}

const char *emmaa_permissao(const struct emmaa_def *d) {
	GHashTable *p = emmab_permissoes();
	const char *v = g_hash_table_lookup(p, d->nome);
	static char escolhida[8];
	g_strlcpy(escolhida, v && (!strcmp(v, "auto") || !strcmp(v, "ask") || !strcmp(v, "off")) ? v : d->padrao, sizeof(escolhida));
	g_hash_table_destroy(p);
	if (d->risco == EMMAA_PERIGO && !strcmp(escolhida, "auto")) {
		g_strlcpy(escolhida, "ask", sizeof(escolhida));
	}
	return escolhida;
}

char *emmaa_ferramentas_json(bool bloqueado) {
	GString *s = g_string_new("[");
	bool primeiro = true;
	for (guint i = 0; i < G_N_ELEMENTS(catalogo); i++) {
		const struct emmaa_def *d = &catalogo[i];
		if (bloqueado && !emmaa_seguro_bloqueado(d)) {
			continue;
		}
		char *nome = ferramenta_nome(d->nome);
		g_string_append_printf(s, "%s{\"name\":", primeiro ? "" : ",");
		json_escrever_texto(s, nome);
		g_string_append(s, ",\"label\":");
		json_escrever_texto(s, d->rotulo);
		g_string_append(s, ",\"description\":");
		json_escrever_texto(s, d->descricao);
		g_string_append_printf(s, ",\"params\":{\"type\":\"object\",\"properties\":%s,\"required\":[", d->params);
		char **req = g_strsplit(d->obrigatorios, ",", -1);
		for (int k = 0; req[k]; k++) {
			if (*req[k]) {
				g_string_append_printf(s, "%s\"%s\"", k ? "," : "", req[k]);
			}
		}
		g_strfreev(req);
		g_string_append_printf(s, "]},\"risk\":\"%s\",\"default\":\"%s\"}", d->risco == EMMAA_LER ? "read" : "act", emmaa_permissao(d));
		g_free(nome);
		primeiro = false;
	}
	g_string_append_c(s, ']');
	return g_string_free(s, FALSE);
}

void emmaa_res_limpar(struct emmaa_res *r) {
	g_free(r->resumo);
	g_free(r->conteudo);
	r->resumo = r->conteudo = NULL;
}

static void ok_(struct emmaa_res *r, const char *resumo) {
	r->ok = true;
	g_free(r->resumo);
	r->resumo = g_strdup(resumo);
}

static void falha(struct emmaa_res *r, const char *resumo) {
	r->ok = false;
	g_free(r->resumo);
	r->resumo = g_strdup(resumo);
}

/* ---- ajudas ---- */

char *emmaa_normalizar(const char *texto) {
	char *a = g_str_to_ascii(texto, "pt_BR");
	GString *s = g_string_new(NULL);
	bool espaco = true;
	for (const char *p = a; *p; p++) {
		if (g_ascii_isalnum(*p)) {
			g_string_append_c(s, g_ascii_tolower(*p));
			espaco = false;
		} else if (!espaco) {
			g_string_append_c(s, ' ');
			espaco = true;
		}
	}
	if (s->len && s->str[s->len - 1] == ' ') {
		g_string_truncate(s, s->len - 1);
	}
	g_free(a);
	return g_string_free(s, FALSE);
}

static bool tem_palavra(const char *normal, const char *palavra) {
	char *p = g_strdup_printf(" %s ", palavra), *n = g_strdup_printf(" %s ", normal);
	bool r = strstr(n, p) != NULL;
	g_free(p);
	g_free(n);
	return r;
}

static bool comeca_com(const char *normal, const char *prefixo) {
	size_t n = strlen(prefixo);
	return !strncmp(normal, prefixo, n) && (normal[n] == '\0' || normal[n] == ' ');
}

static bool executar_simples(char **argv) {
	GError *e = NULL;
	bool ok = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, &e);
	g_clear_error(&e);
	return ok;
}

static bool executar_esperando(char **argv, char **saida) {
	gint estado = 0;
	GError *e = NULL;
	bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, saida, NULL, &estado, &e);
	g_clear_error(&e);
	return ok && g_spawn_check_wait_status(estado, NULL);
}

static bool tecla(const char *nome) {
	char *argv[] = { "wtype", "-k", (char *)nome, NULL };
	return executar_esperando(argv, NULL);
}

/* ---- o que a Mídia tem ---- */

static const struct midia_servico *servico_por_nome(const char *dito, struct midia_servico *lista, int n) {
	char *d = emmaa_normalizar(dito);
	const struct midia_servico *melhor = NULL;
	int melhor_pontos = 0;
	for (int i = 0; i < n; i++) {
		if (!lista[i].ativo) {
			continue;
		}
		char *nome = emmaa_normalizar(lista[i].nome), *id = emmaa_normalizar(lista[i].id);
		int pontos = 0;
		if (!strcmp(d, nome) || !strcmp(d, id)) {
			pontos = 100;
		} else if (*nome && (tem_palavra(d, nome) || strstr(d, nome))) {
			pontos = 80 + (int)strlen(nome);
		} else if (*id && tem_palavra(d, id)) {
			pontos = 70;
		} else if (*d && strstr(nome, d) && strlen(d) >= 3) {
			pontos = 50;
		}
		if (pontos > melhor_pontos) {
			melhor_pontos = pontos;
			melhor = &lista[i];
		}
		g_free(nome);
		g_free(id);
	}
	g_free(d);
	return melhor;
}

/* ---- as ações ---- */

static void midia_estado(struct emmaa_res *r) {
	struct midia_servico lista[MIDIA_MAX_SERVICOS];
	int n = midia_servicos_ler(lista, MIDIA_MAX_SERVICOS);
	struct midia_estado e;
	bool vivo = midiac_estado(&e);
	struct mpris_estado m;
	mpris_ler(&m);
	GString *s = g_string_new(NULL);
	if (vivo && e.ativo[0]) {
		const struct midia_vivo *v = midiac_achar(&e, e.ativo);
		g_string_append_printf(s, "Na frente: %s.\n", v ? v->s.nome : e.ativo);
	} else {
		g_string_append(s, vivo ? "Nenhum serviço na frente (Início).\n" : "O serviço da Mídia não respondeu.\n");
	}
	g_string_append(s, "Serviços:");
	for (int i = 0; i < n; i++) {
		if (lista[i].ativo) {
			const struct midia_vivo *v = vivo ? midiac_achar(&e, lista[i].id) : NULL;
			g_string_append_printf(s, " %s%s;", lista[i].nome, v && v->aberto ? " (aberto)" : "");
		}
	}
	if (m.tem) {
		g_string_append_printf(s, "\n%s: %s%s%s.", m.tocando ? "Tocando" : "Pausado", m.titulo, m.artista[0] ? " — " : "", m.artista);
	}
	r->ok = true;
	r->conteudo = g_string_free(s, FALSE);
	r->resumo = g_strdup(vivo && e.ativo[0] ? "Aqui está o que há na Mídia." : "Sem serviço na frente.");
}

static void midia_abrir(const char *dito, bool do_comeco, struct emmaa_res *r) {
	struct midia_servico lista[MIDIA_MAX_SERVICOS];
	int n = midia_servicos_ler(lista, MIDIA_MAX_SERVICOS);
	const struct midia_servico *s = servico_por_nome(dito, lista, n);
	if (!s) {
		char *m = g_strdup_printf("Não achei \"%s\" entre os serviços da Mídia.", dito);
		falha(r, m);
		g_free(m);
		return;
	}
	char *pedido = g_strdup_printf("abrir %s%s", s->id, do_comeco ? " novo" : ""), resposta[512];
	bool ok = midiac_pedido(pedido, resposta, sizeof(resposta)) && strstr(resposta, "\"ok\":true");
	g_free(pedido);
	if (ok) {
		char *m = g_strdup_printf("Abrindo %s.", s->nome);
		ok_(r, m);
		g_free(m);
	} else {
		falha(r, "O serviço da Mídia não respondeu (o lumna-midia está rodando?).");
	}
}

static void midia_fechar(const char *dito, struct emmaa_res *r) {
	struct midia_servico lista[MIDIA_MAX_SERVICOS];
	int n = midia_servicos_ler(lista, MIDIA_MAX_SERVICOS);
	const struct midia_servico *s = servico_por_nome(dito, lista, n);
	if (!s) {
		falha(r, "Não achei esse serviço.");
		return;
	}
	char *pedido = g_strdup_printf("fechar %s", s->id), resposta[512];
	bool ok = midiac_pedido(pedido, resposta, sizeof(resposta)) && strstr(resposta, "\"ok\":true");
	g_free(pedido);
	if (ok) {
		char *m = g_strdup_printf("Fechei %s.", s->nome);
		ok_(r, m);
		g_free(m);
	} else {
		falha(r, "Não consegui fechar.");
	}
}

static void midia_pedido_simples(const char *pedido, const char *dito, struct emmaa_res *r) {
	char resposta[512];
	if (midiac_pedido(pedido, resposta, sizeof(resposta)) && strstr(resposta, "\"ok\":true")) {
		ok_(r, dito);
	} else {
		falha(r, "O serviço da Mídia não respondeu.");
	}
}

static void midia_controle(const char *acao, double segundos, struct emmaa_res *r) {
	struct mpris_estado m;
	mpris_ler(&m);
	if (segundos <= 0) {
		segundos = 10;
	}
	if (!strcmp(acao, "alternar")) {
		m.tem ? mpris_tocar_pausar() : (void)tecla("XF86AudioPlay");
		ok_(r, m.tem && m.tocando ? "Pausado." : "Tocando.");
	} else if (!strcmp(acao, "tocar")) {
		if (m.tem && !m.tocando) {
			mpris_tocar_pausar();
		} else if (!m.tem) {
			tecla("XF86AudioPlay");
		}
		ok_(r, "Tocando.");
	} else if (!strcmp(acao, "pausar")) {
		if (m.tem && m.tocando) {
			mpris_tocar_pausar();
		} else if (!m.tem) {
			tecla("XF86AudioPlay");
		}
		ok_(r, "Pausado.");
	} else if (!strcmp(acao, "proxima")) {
		m.tem ? mpris_proxima() : (void)tecla("XF86AudioNext");
		ok_(r, "Próxima.");
	} else if (!strcmp(acao, "anterior")) {
		m.tem ? mpris_anterior() : (void)tecla("XF86AudioPrev");
		ok_(r, "Anterior.");
	} else if (!strcmp(acao, "avancar") || !strcmp(acao, "voltar")) {
		bool frente = !strcmp(acao, "avancar");
		if (m.tem) {
			mpris_buscar((int64_t)((frente ? 1 : -1) * segundos * G_USEC_PER_SEC));
		} else {
			for (int i = 0; i < (int)(segundos / 5 + 0.5); i++) {
				tecla(frente ? "Right" : "Left"); /* o player da frente anda 5 s com a seta */
			}
		}
		char *t = g_strdup_printf("%s %d segundos.", frente ? "Avancei" : "Voltei", (int)segundos);
		ok_(r, t);
		g_free(t);
	} else {
		falha(r, "Não conheço essa ação de mídia.");
	}
}

static void midia_navegar(const char *dir, struct emmaa_res *r) {
	static const struct { const char *nome, *tecla; } mapa[] = { { "cima", "Up" }, { "baixo", "Down" }, { "esquerda", "Left" }, { "direita", "Right" },
		{ "ok", "Return" }, { "voltar", "Escape" } };
	if (!strcmp(dir, "menu")) {
		char *argv[] = { "wtype", "-M", "logo", "-k", "m", "-m", "logo", NULL }; /* o seletor de serviços */
		executar_esperando(argv, NULL) ? ok_(r, "Abri o seletor.") : falha(r, "O wtype não está instalado.");
		return;
	}
	for (guint i = 0; i < G_N_ELEMENTS(mapa); i++) {
		if (!strcmp(dir, mapa[i].nome)) {
			tecla(mapa[i].tecla) ? ok_(r, "Feito.") : falha(r, "O wtype não está instalado.");
			return;
		}
	}
	falha(r, "Direção desconhecida.");
}

static void audio_volume(const struct json *args, struct emmaa_res *r) {
	const struct json *mudo = json_chave(args, "mudo"), *pct = json_chave(args, "percentual"), *delta = json_chave(args, "delta");
	if (mudo && mudo->tipo == JSON_BOOL) {
		char *argv[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", mudo->b ? "1" : "0", NULL };
		executar_esperando(argv, NULL) ? ok_(r, mudo->b ? "Mudo." : "Som de volta.") : falha(r, "O wpctl não respondeu.");
		return;
	}
	char valor[24];
	if (pct && pct->tipo == JSON_NUMERO) {
		snprintf(valor, sizeof(valor), "%d%%", (int)CLAMP(pct->n, 0, 150));
	} else if (delta && delta->tipo == JSON_NUMERO) {
		snprintf(valor, sizeof(valor), "%d%%%c", (int)fabs(CLAMP(delta->n, -100, 100)), delta->n < 0 ? '-' : '+');
	} else {
		falha(r, "Diga o volume (0 a 100) ou quanto subir ou descer.");
		return;
	}
	char *argv[] = { "wpctl", "set-volume", "-l", "1.5", "@DEFAULT_AUDIO_SINK@", valor, NULL };
	if (!executar_esperando(argv, NULL)) {
		falha(r, "O wpctl não respondeu (o PipeWire está rodando?).");
		return;
	}
	char *saida = NULL, *argv2[] = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };
	int atual = -1;
	if (executar_esperando(argv2, &saida) && saida && g_str_has_prefix(saida, "Volume:")) {
		atual = (int)lround(atof(saida + 7) * 100);
	}
	g_free(saida);
	char *m = atual >= 0 ? g_strdup_printf("Volume em %d%%.", atual) : g_strdup("Volume ajustado.");
	ok_(r, m);
	g_free(m);
}

static void mode_atual(struct emmaa_res *r) {
	struct modos_estado e;
	if (!modos_ler(&e) || !e.orbit) {
		falha(r, "O Orbit não respondeu.");
		return;
	}
	GString *s = g_string_new(NULL);
	for (int i = 0; i < e.n; i++) {
		g_string_append_printf(s, "%s: %s%s\n", e.saidas[i].nome, modos_nome(e.saidas[i].modo), e.saidas[i].ponteiro ? " (onde está o ponteiro)" : "");
	}
	r->ok = true;
	r->conteudo = g_string_free(s, FALSE);
	r->resumo = g_strdup("Aqui estão os modos.");
}

static void mode_trocar(const char *modo, const char *monitor, struct emmaa_res *r) {
	int m = modos_de_id(modo);
	if (m < 0) {
		falha(r, "Modo desconhecido (desktop, gaming ou midia).");
		return;
	}
	if (modos_definir(m, monitor && *monitor ? monitor : NULL)) {
		char *t = g_strdup_printf("Modo %s.", modos_nome(m));
		ok_(r, t);
		g_free(t);
	} else {
		falha(r, "O Orbit não respondeu.");
	}
}

/* ---- programas ---- */

static int pontos_app(GAppInfo *a, const char *q) {
	if (!*q) {
		return 1;
	}
	char *nome = emmaa_normalizar(g_app_info_get_display_name(a)), *exe = emmaa_normalizar(g_app_info_get_executable(a) ? g_app_info_get_executable(a) : "");
	int p = 0;
	if (!strcmp(nome, q) || !strcmp(exe, q)) {
		p = 100;
	} else if (comeca_com(nome, q)) {
		p = 80;
	} else if (strstr(nome, q)) {
		p = 60;
	} else if (strstr(exe, q)) {
		p = 40;
	}
	g_free(nome);
	g_free(exe);
	return p;
}

static void apps_listar(const char *query, struct emmaa_res *r) {
	char *q = emmaa_normalizar(query ? query : "");
	GList *todos = g_app_info_get_all();
	GString *s = g_string_new(NULL);
	int n = 0;
	for (GList *l = todos; l && n < 60; l = l->next) {
		if (g_app_info_should_show(l->data) && pontos_app(l->data, q) > 0) {
			g_string_append_printf(s, "%s\n", g_app_info_get_display_name(l->data));
			n++;
		}
	}
	g_list_free_full(todos, g_object_unref);
	g_free(q);
	r->ok = true;
	r->conteudo = g_string_free(s, FALSE);
	r->resumo = g_strdup_printf("%d programas.", n);
}

static void apps_abrir(const char *nome, struct emmaa_res *r) {
	char *q = emmaa_normalizar(nome);
	GList *todos = g_app_info_get_all();
	GAppInfo *melhor = NULL;
	int melhor_p = 0;
	for (GList *l = todos; l; l = l->next) {
		if (!g_app_info_should_show(l->data)) {
			continue;
		}
		int p = pontos_app(l->data, q);
		if (p > melhor_p) {
			melhor_p = p;
			melhor = l->data;
		}
	}
	if (melhor && melhor_p >= 40) {
		GError *e = NULL;
		if (g_app_info_launch(melhor, NULL, NULL, &e)) {
			char *m = g_strdup_printf("Abrindo %s.", g_app_info_get_display_name(melhor));
			ok_(r, m);
			g_free(m);
		} else {
			falha(r, "Não consegui abrir.");
		}
		g_clear_error(&e);
	} else {
		char *m = g_strdup_printf("Não achei um programa chamado \"%s\".", nome);
		falha(r, m);
		g_free(m);
	}
	g_list_free_full(todos, g_object_unref);
	g_free(q);
}

void emmaa_iniciar(void) {
	mpris_iniciar();
}

void emmaa_executar(const struct emmaa_def *d, const struct json *args, struct emmaa_res *r) {
	const char *n = d->nome;
	if (!strcmp(n, "midia.estado")) {
		midia_estado(r);
	} else if (!strcmp(n, "midia.abrir")) {
		midia_abrir(json_chave_texto(args, "servico"), json_bool(json_chave(args, "do_comeco"), false), r);
	} else if (!strcmp(n, "midia.fechar")) {
		midia_fechar(json_chave_texto(args, "servico"), r);
	} else if (!strcmp(n, "midia.inicio")) {
		midia_pedido_simples("inicio", "Início da Mídia.", r);
	} else if (!strcmp(n, "midia.seletor")) {
		midia_pedido_simples("seletor", "Seletor aberto.", r);
	} else if (!strcmp(n, "midia.controle")) {
		midia_controle(json_chave_texto(args, "acao"), json_numero(json_chave(args, "segundos"), 10), r);
	} else if (!strcmp(n, "midia.navegar")) {
		midia_navegar(json_chave_texto(args, "direcao"), r);
	} else if (!strcmp(n, "midia.digitar")) {
		const char *t = json_chave_texto(args, "texto");
		if (!*t || strlen(t) > 500) {
			falha(r, "Texto vazio ou longo demais.");
		} else {
			char *argv[] = { "wtype", "--", (char *)t, NULL };
			bool bom = executar_esperando(argv, NULL);
			if (bom && json_bool(json_chave(args, "enter"), false)) {
				tecla("Return");
			}
			bom ? ok_(r, "Digitado.") : falha(r, "O wtype não está instalado.");
		}
	} else if (!strcmp(n, "audio.volume")) {
		audio_volume(args, r);
	} else if (!strcmp(n, "mode.atual")) {
		mode_atual(r);
	} else if (!strcmp(n, "mode.trocar")) {
		mode_trocar(json_chave_texto(args, "modo"), json_chave_texto(args, "monitor"), r);
	} else if (!strcmp(n, "mode.trocar_telas")) {
		modos_trocar_telas() ? ok_(r, "Troquei as telas.") : falha(r, "Não há outra tela para trocar.");
	} else if (!strcmp(n, "apps.listar")) {
		apps_listar(json_chave_texto(args, "query"), r);
	} else if (!strcmp(n, "apps.abrir")) {
		apps_abrir(json_chave_texto(args, "nome"), r);
	} else if (!strcmp(n, "painel.abrir")) {
		const char *sec = json_chave_texto(args, "secao");
		bool bom = *sec && strlen(sec) < 64;
		for (const char *c = sec; bom && *c; c++) {
			bom = g_ascii_islower(*c) || g_ascii_isdigit(*c) || *c == '/' || *c == '-' || *c == '_';
		}
		if (!bom) {
			falha(r, "Seção inválida.");
		} else {
			char *argv[] = { "lumna-painel", "--secao", (char *)sec, NULL };
			executar_simples(argv) ? ok_(r, "Abri a configuração.") : falha(r, "Não consegui abrir o Painel de controle.");
		}
	} else if (!strcmp(n, "notificar")) {
		char *argv[] = { "notify-send", "-a", "Emma", (char *)json_chave_texto(args, "titulo"), (char *)json_chave_texto(args, "texto"), NULL };
		executar_simples(argv) ? ok_(r, "Avisei.") : falha(r, "O notify-send não está instalado.");
	} else {
		falha(r, "Ação desconhecida.");
	}
}

/* ---- os comandos curtos ---- */

static int numero_pt(char **toks, int n, int *usados) {
	static const struct { const char *p; int v; } un[] = { { "zero", 0 }, { "um", 1 }, { "uma", 1 }, { "dois", 2 }, { "duas", 2 }, { "tres", 3 }, { "quatro", 4 },
		{ "cinco", 5 }, { "seis", 6 }, { "sete", 7 }, { "oito", 8 }, { "nove", 9 }, { "dez", 10 }, { "onze", 11 }, { "doze", 12 }, { "treze", 13 },
		{ "catorze", 14 }, { "quatorze", 14 }, { "quinze", 15 }, { "dezesseis", 16 }, { "dezessete", 17 }, { "dezoito", 18 }, { "dezenove", 19 },
		{ "vinte", 20 }, { "trinta", 30 }, { "quarenta", 40 }, { "cinquenta", 50 }, { "sessenta", 60 }, { "setenta", 70 }, { "oitenta", 80 },
		{ "noventa", 90 }, { "cem", 100 }, { "cento", 100 } };
	*usados = 0;
	if (n <= 0) {
		return -1;
	}
	if (g_ascii_isdigit(toks[0][0])) {
		*usados = 1;
		return atoi(toks[0]);
	}
	int total = -1, i = 0;
	for (; i < n; i++) {
		int v = -1;
		for (guint k = 0; k < G_N_ELEMENTS(un); k++) {
			if (!strcmp(toks[i], un[k].p)) {
				v = un[k].v;
			}
		}
		if (v >= 0) {
			total = total < 0 ? v : total + v;
		} else if (!strcmp(toks[i], "e") && total >= 20 && i + 1 < n) {
			continue;
		} else {
			break;
		}
	}
	*usados = i;
	return total;
}

static void acao_curta(const char *nome, const char *args_json, struct emmaa_res *r) {
	const struct emmaa_def *d = emmaa_achar(nome);
	/* a pessoa pediu na voz dela: vale mesmo com "perguntar", mas uma ação desligada no Painel continua desligada */
	if (!strcmp(emmaa_permissao(d), "off")) {
		falha(r, "Essa ação está desligada nas permissões da Emma.");
		return;
	}
	struct json *a = json_ler(args_json);
	emmaa_executar(d, a, r);
	json_liberar(a);
}

bool emmaa_intencao(const char *texto, struct emmaa_res *r) {
	char *n = emmaa_normalizar(texto);
	if (!*n || strlen(n) > 80) {
		g_free(n);
		return false;
	}
	/* tira "emma", "por favor" e as palavras de cortesia do começo e do fim */
	char *limpo = g_strdup(n);
	static const char *const enfeites[] = { "emma ", "oi emma ", "ei emma ", "ok emma ", "por favor ", "pode ", "podia ", "quero que voce ", "voce pode ", "da pra ", "da para " };
	for (int volta = 0; volta < 3; volta++) {
		for (guint i = 0; i < G_N_ELEMENTS(enfeites); i++) {
			if (g_str_has_prefix(limpo, enfeites[i])) {
				char *t = g_strdup(limpo + strlen(enfeites[i]));
				g_free(limpo);
				limpo = t;
			}
		}
	}
	const char *fins[] = { " por favor", " emma", " pra mim", " agora" };
	for (guint i = 0; i < G_N_ELEMENTS(fins); i++) {
		size_t ln = strlen(limpo), lf = strlen(fins[i]);
		if (ln > lf && !strcmp(limpo + ln - lf, fins[i])) {
			limpo[ln - lf] = '\0';
		}
	}
	g_free(n);
	n = limpo;
	char **toks = g_strsplit(n, " ", -1);
	int nt = (int)g_strv_length(toks);
	bool tratado = true;

	/* o que está tocando */
	static const char *const objetos[] = { "", " o video", " a musica", " o filme", " isso", " a serie", " o episodio", " o som", " a reproducao", " a faixa", " o que ta passando" };
	bool so_objeto = false;
	const char *verbo = NULL;
	static const struct { const char *palavra; const char *acao; } verbos[] = { { "pausa", "pausar" }, { "pausar", "pausar" }, { "pause", "pausar" }, { "pare", "pausar" },
		{ "para", "pausar" }, { "parar", "pausar" }, { "segura", "pausar" }, { "continua", "tocar" }, { "continuar", "tocar" }, { "continue", "tocar" },
		{ "retoma", "tocar" }, { "retomar", "tocar" }, { "toca", "tocar" }, { "tocar", "tocar" }, { "play", "tocar" }, { "despausa", "tocar" },
		{ "solta", "tocar" }, { "da play", "tocar" }, { "proxima", "proxima" }, { "proximo", "proxima" }, { "pula", "proxima" }, { "pular", "proxima" },
		{ "next", "proxima" }, { "anterior", "anterior" }, { "volta a anterior", "anterior" } };
	for (guint i = 0; i < G_N_ELEMENTS(verbos) && !verbo; i++) {
		if (comeca_com(n, verbos[i].palavra)) {
			const char *resto = n + strlen(verbos[i].palavra);
			so_objeto = !*resto;
			for (guint k = 0; k < G_N_ELEMENTS(objetos) && !so_objeto; k++) {
				so_objeto = !strcmp(resto, objetos[k]);
			}
			if (so_objeto) {
				verbo = verbos[i].acao;
			}
		}
	}
	if (verbo) {
		char *j = g_strdup_printf("{\"acao\":\"%s\"}", verbo);
		acao_curta("midia.controle", j, r);
		g_free(j);
	} else if ((comeca_com(n, "avanca") || comeca_com(n, "adianta") || comeca_com(n, "volta") || comeca_com(n, "atrasa") || comeca_com(n, "recua")) && nt >= 2) {
		bool frente = comeca_com(n, "avanca") || comeca_com(n, "adianta");
		int usados;
		int qtd = numero_pt(toks + 1, nt - 1, &usados);
		int unidade = 1;
		if (usados > 0 && 1 + usados < nt) {
			unidade = g_str_has_prefix(toks[1 + usados], "min") ? 60 : g_str_has_prefix(toks[1 + usados], "seg") ? 1 : g_str_has_prefix(toks[1 + usados], "hora") ? 3600 : 0;
		}
		if (qtd > 0 && unidade > 0) {
			char *j = g_strdup_printf("{\"acao\":\"%s\",\"segundos\":%d}", frente ? "avancar" : "voltar", qtd * unidade);
			acao_curta("midia.controle", j, r);
			g_free(j);
		} else {
			tratado = false;
		}
	} else if (comeca_com(n, "volume") || ((comeca_com(n, "coloca") || comeca_com(n, "bota") || comeca_com(n, "poe") || comeca_com(n, "deixa") || comeca_com(n, "ajusta")) && strstr(n, "volume"))) {
		int usados = 0, qtd = -1;
		for (int i = 0; i < nt && qtd < 0; i++) {
			qtd = numero_pt(toks + i, nt - i, &usados);
			if (usados == 0) {
				qtd = -1;
			}
		}
		if (qtd >= 0 && qtd <= 150) {
			char *j = g_strdup_printf("{\"percentual\":%d}", qtd);
			acao_curta("audio.volume", j, r);
			g_free(j);
		} else {
			tratado = false;
		}
	} else if ((tem_palavra(n, "aumenta") || tem_palavra(n, "sobe") || tem_palavra(n, "aumentar") || tem_palavra(n, "subir") || tem_palavra(n, "mais")) && tem_palavra(n, "volume")) {
		acao_curta("audio.volume", "{\"delta\":10}", r);
	} else if ((tem_palavra(n, "abaixa") || tem_palavra(n, "diminui") || tem_palavra(n, "desce") || tem_palavra(n, "baixa") || tem_palavra(n, "abaixar") || tem_palavra(n, "menos")) &&
		tem_palavra(n, "volume")) {
		acao_curta("audio.volume", "{\"delta\":-10}", r);
	} else if (!strcmp(n, "mudo") || !strcmp(n, "muda") || !strcmp(n, "silencio") || !strcmp(n, "silencia") || !strcmp(n, "muta") || !strcmp(n, "coloca no mudo")) {
		acao_curta("audio.volume", "{\"mudo\":true}", r);
	} else if (!strcmp(n, "desmuta") || !strcmp(n, "tira o mudo") || !strcmp(n, "volta o som") || !strcmp(n, "liga o som")) {
		acao_curta("audio.volume", "{\"mudo\":false}", r);
	} else if (!strcmp(n, "inicio") || !strcmp(n, "tela inicial") || !strcmp(n, "home") || !strcmp(n, "vai para o inicio") || !strcmp(n, "volta para o inicio") ||
		!strcmp(n, "volta pro inicio") || !strcmp(n, "vai pro inicio")) {
		acao_curta("midia.inicio", "{}", r);
	} else if (!strcmp(n, "menu") || !strcmp(n, "seletor") || !strcmp(n, "trocar de servico") || !strcmp(n, "muda de servico")) {
		acao_curta("midia.seletor", "{}", r);
	} else if (!strcmp(n, "troca as telas") || !strcmp(n, "inverte as telas") || !strcmp(n, "troca de tela") || !strcmp(n, "manda para a tv") || !strcmp(n, "manda pra tv")) {
		acao_curta("mode.trocar_telas", "{}", r);
	} else if (strstr(n, "modo ") && (comeca_com(n, "modo") || comeca_com(n, "muda") || comeca_com(n, "troca") || comeca_com(n, "vai") || comeca_com(n, "poe") || comeca_com(n, "coloca"))) {
		const char *modo = tem_palavra(n, "midia") ? "midia" : tem_palavra(n, "gaming") || tem_palavra(n, "jogos") || tem_palavra(n, "jogo") ? "gaming" :
			tem_palavra(n, "desktop") || tem_palavra(n, "normal") || tem_palavra(n, "trabalho") ? "desktop" : NULL;
		if (modo) {
			char *j = g_strdup_printf("{\"modo\":\"%s\"}", modo);
			acao_curta("mode.trocar", j, r);
			g_free(j);
		} else {
			tratado = false;
		}
	} else if (comeca_com(n, "abre") || comeca_com(n, "abrir") || comeca_com(n, "abra") || comeca_com(n, "vai para") || comeca_com(n, "vai pro") || comeca_com(n, "vai pra") ||
		comeca_com(n, "coloca") || comeca_com(n, "bota") || comeca_com(n, "poe") || comeca_com(n, "liga") || comeca_com(n, "quero ver") || comeca_com(n, "quero assistir") ||
		comeca_com(n, "assistir") || comeca_com(n, "me leva para") || comeca_com(n, "muda para") || comeca_com(n, "muda pro") || comeca_com(n, "troca para")) {
		/* só vale se for um serviço da Mídia: o resto (um programa) é com a Emma */
		struct midia_servico lista[MIDIA_MAX_SERVICOS];
		int ns = midia_servicos_ler(lista, MIDIA_MAX_SERVICOS);
		const struct midia_servico *s = servico_por_nome(n, lista, ns);
		if (s) {
			char *j = g_strdup_printf("{\"servico\":\"%s\"}", s->id);
			acao_curta("midia.abrir", j, r);
			g_free(j);
		} else {
			tratado = false;
		}
	} else {
		tratado = false;
	}
	g_strfreev(toks);
	g_free(n);
	return tratado;
}
