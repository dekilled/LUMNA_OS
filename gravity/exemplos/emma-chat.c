/*
 * lumna-emma-chat: o painel flutuante da Emma, à direita da tela. Um chat rápido por cima do que a pessoa
 * está fazendo: a conversa, os botões de próximo passo que a Emma sugere, o cartão que leva a uma configuração,
 * o contexto (a janela que está em foco) e o microfone. Rodar de novo fecha (como o lançador).
 *
 * Quem pensa e age é o serviço lumna-emma (servicos/emmac.h); aqui só se mostra e se pede. Pedido rápido
 * ("pausa", "abre a Netflix"): a resposta aparece, e o painel (se não estiver fixado) fecha sozinho.
 */
#define _GNU_SOURCE
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "emmac.h"
#include "gravity.h"
#include "json.h"
#include "modos.h"

struct ferramenta {
	char nome[64];
	char resumo[160];
	char secao[64];     /* painel.abrir: a seção */
	bool ok;
};

struct msg {
	bool usuario;
	char *texto;
	char hora[8];
	bool parcial;
	GArray *ferramentas; /* struct ferramenta */
};

static GPtrArray *msgs;
static char estado[16] = "offline";
static bool servidor;
static double nivel;
static char erro[300];
static char campo[512];
static char conf_id[24], conf_nome[96], conf_desc[256];  /* uma ação que pede licença */
static char ctx_titulo[160];
static bool ctx_ativo = true;
static bool fixado;
static bool rolar_fim = true;
static double fechar_em;
static gv_app *app;
static int emma_fd = -1;
static char *dir_assets;

/* o violeta dos balões e do enviar (um tom mais fundo que o acento dos botões, como no desenho) */
static const Clay_Color VIOLETA = { 110, 78, 232, 255 };

/* ---- as imagens ---- */

static char *achar_asset(const char *nome) {
	const char *env = g_getenv("LUMNA_EMMA_ASSETS");
	char *exe = g_file_read_link("/proc/self/exe", NULL), *pasta = exe ? g_path_get_dirname(exe) : NULL;
	char *cand[] = { env && *env ? g_build_filename(env, nome, NULL) : NULL, pasta ? g_build_filename(pasta, "assets", "emma", nome, NULL) : NULL,
		g_build_filename("/usr/share/lumna/emma", nome, NULL) };
	char *r = NULL;
	for (guint i = 0; i < G_N_ELEMENTS(cand); i++) {
		if (!r && cand[i] && g_file_test(cand[i], G_FILE_TEST_EXISTS)) {
			r = g_strdup(cand[i]);
		}
		g_free(cand[i]);
	}
	g_free(exe);
	g_free(pasta);
	return r;
}

/* ---- os botões de próximo passo: [acao: ...] [link: ...] [sugestao: ...] ---- */

struct resposta_lida {
	char *corpo;
	char principal[96];
	char links[2][96];
	int nlinks;
	char sugestoes[2][96];
	int nsug;
};

static void ler_resposta(const char *texto, struct resposta_lida *r) {
	memset(r, 0, sizeof(*r));
	char **ls = g_strsplit(texto, "\n", -1);
	GString *corpo = g_string_new(NULL);
	int n = (int)g_strv_length(ls);
	for (int i = 0; i < n; i++) {
		char *l = g_strstrip(g_strdup(ls[i]));
		bool etiqueta = false;
		if (l[0] == '[') {
			char *dp = strchr(l, ':'), *fim = strrchr(l, ']');
			if (dp && fim && fim > dp) {
				char *tipo = g_strndup(l + 1, (gsize)(dp - l - 1)), *rot = g_strndup(dp + 1, (gsize)(fim - dp - 1));
				g_strstrip(rot);
				char *t = g_ascii_strdown(g_strstrip(tipo), -1);
				if (g_str_has_prefix(t, "a") && strlen(t) >= 3 && (g_str_has_prefix(t, "acao") || g_str_has_prefix(t, "ação"))) {
					etiqueta = true;
					if (!r->principal[0]) {
						g_strlcpy(r->principal, rot, sizeof(r->principal));
					} else if (r->nlinks < 2) {
						g_strlcpy(r->links[r->nlinks++], rot, 96);
					}
				} else if (!strcmp(t, "link")) {
					etiqueta = true;
					if (r->nlinks < 2) {
						g_strlcpy(r->links[r->nlinks++], rot, 96);
					}
				} else if (g_str_has_prefix(t, "sugest")) {
					etiqueta = true;
					if (r->nsug < 2) {
						g_strlcpy(r->sugestoes[r->nsug++], rot, 96);
					}
				}
				g_free(tipo);
				g_free(rot);
				g_free(t);
			} else if (i == n - 1 && !strchr(l, ']')) {
				etiqueta = true; /* um botão ainda chegando */
			}
		}
		if (!etiqueta) {
			g_string_append_printf(corpo, "%s%s", corpo->len ? "\n" : "", ls[i]);
		}
		g_free(l);
	}
	g_strfreev(ls);
	r->corpo = g_strstrip(g_string_free(corpo, FALSE));
}

/* ---- o estado ---- */

static void msg_liberar(gpointer p) {
	struct msg *m = p;
	g_free(m->texto);
	if (m->ferramentas) {
		g_array_free(m->ferramentas, TRUE);
	}
	g_free(m);
}

static struct msg *nova_msg(bool usuario, const char *texto, const char *hora) {
	struct msg *m = g_new0(struct msg, 1);
	m->usuario = usuario;
	m->texto = g_strdup(texto);
	if (hora && *hora) {
		g_strlcpy(m->hora, hora, sizeof(m->hora));
	} else {
		time_t t = time(NULL);
		struct tm tm;
		localtime_r(&t, &tm);
		strftime(m->hora, sizeof(m->hora), "%H:%M", &tm);
	}
	m->ferramentas = g_array_new(FALSE, TRUE, sizeof(struct ferramenta));
	g_ptr_array_add(msgs, m);
	rolar_fim = true;
	return m;
}

static struct msg *ultima_parcial(void) {
	if (msgs->len) {
		struct msg *m = msgs->pdata[msgs->len - 1];
		if (!m->usuario && m->parcial) {
			return m;
		}
	}
	return NULL;
}

static void juntar_ferramenta(struct msg *m, const struct json *t) {
	struct ferramenta f = { 0 };
	g_strlcpy(f.nome, json_chave_texto(t, "nome"), sizeof(f.nome));
	g_strlcpy(f.resumo, json_chave_texto(t, "resumo"), sizeof(f.resumo));
	f.ok = json_bool(json_chave(t, "ok"), false);
	g_strlcpy(f.secao, json_chave_texto(json_chave(t, "args"), "secao"), sizeof(f.secao));
	g_array_append_val(m->ferramentas, f);
}

static void atualizar_contexto(void) {
	char r[512];
	if (emmac_pedido(ctx_ativo ? "contexto" : "contexto", r, sizeof(r))) {
		struct json *j = json_ler(r);
		if (j) {
			g_strlcpy(ctx_titulo, json_chave_texto(j, "titulo"), sizeof(ctx_titulo));
			ctx_ativo = json_bool(json_chave(j, "ativo"), true);
			json_liberar(j);
		}
	}
}

static void carregar_historico(void) {
	char *r = g_malloc(256 * 1024);
	g_ptr_array_set_size(msgs, 0);
	if (emmac_pedido("historico", r, 256 * 1024)) {
		struct json *j = json_ler(r);
		const struct json *l = json_chave(j, "mensagens");
		for (int i = 0; l && i < l->nfilhos; i++) {
			const struct json *m = json_item(l, i);
			struct msg *n = nova_msg(!strcmp(json_chave_texto(m, "papel"), "user"), json_chave_texto(m, "texto"), json_chave_texto(m, "hora"));
			const struct json *ts = json_chave(m, "ferramentas");
			for (int k = 0; ts && k < ts->nfilhos; k++) {
				juntar_ferramenta(n, json_item(ts, k));
			}
		}
		json_liberar(j);
	}
	g_free(r);
	rolar_fim = true;
}

static void evento(const struct json *j) {
	const char *ev = json_chave_texto(j, "evento");
	if (!strcmp(json_chave_texto(j, "canal"), "app")) {
		return;   /* as conversas do app completo são dele, não do painel */
	}
	if (!strcmp(ev, "estado")) {
		g_strlcpy(estado, json_chave_texto(j, "estado"), sizeof(estado));
		servidor = json_bool(json_chave(j, "online"), false);
		if (strcmp(estado, "ouvindo")) {
			nivel = 0;
		}
	} else if (!strcmp(ev, "nivel")) {
		nivel = json_numero(json_chave(j, "nivel"), 0);
	} else if (!strcmp(ev, "ouvido")) {
		erro[0] = '\0';
		nova_msg(true, json_chave_texto(j, "texto"), NULL);
		fechar_em = 0;
	} else if (!strcmp(ev, "delta")) {
		struct msg *m = ultima_parcial();
		if (!m) {
			m = nova_msg(false, "", NULL);
			m->parcial = true;
		}
		char *t = g_strconcat(m->texto, json_chave_texto(j, "texto"), NULL);
		g_free(m->texto);
		m->texto = t;
		rolar_fim = true;
	} else if (!strcmp(ev, "ferramenta")) {
		struct msg *m = ultima_parcial();
		if (!m) {
			m = nova_msg(false, "", NULL);
			m->parcial = true;
		}
		juntar_ferramenta(m, j);
		conf_id[0] = '\0';
	} else if (!strcmp(ev, "resposta")) {
		struct msg *m = ultima_parcial();
		const char *t = json_chave_texto(j, "texto");
		if (!m && *t) {
			m = nova_msg(false, "", NULL);
		}
		if (m) {
			if (*t) {
				g_free(m->texto);
				m->texto = g_strdup(t);
			}
			m->parcial = false;
		}
		atualizar_contexto();
		/* um pedido rápido ("pausa", "abre a Netflix"): o painel some sozinho, a não ser que esteja fixado */
		if (!fixado && !strcmp(json_chave_texto(j, "origem"), "local")) {
			fechar_em = gv_agora() + 2.5;
		}
	} else if (!strcmp(ev, "confirmar")) {
		g_strlcpy(conf_id, json_chave_texto(j, "id"), sizeof(conf_id));
		g_strlcpy(conf_nome, json_chave_texto(j, "nome"), sizeof(conf_nome));
		g_strlcpy(conf_desc, json_chave_texto(j, "descricao"), sizeof(conf_desc));
		rolar_fim = true;
	} else if (!strcmp(ev, "erro")) {
		g_strlcpy(erro, json_chave_texto(j, "mensagem"), sizeof(erro));
	} else if (!strcmp(ev, "nova")) {
		g_ptr_array_set_size(msgs, 0);
	}
}

static void emma_mudou(gv_app *a, int fd, void *dados) {
	bool caiu = false;
	char *linha;
	while ((linha = emmac_assinar_ler(fd, &caiu))) {
		struct json *j = json_ler(linha);
		if (j) {
			evento(j);
			json_liberar(j);
		}
		g_free(linha);
	}
	if (caiu) {
		gv_app_desvigiar(a, fd);
		close(fd);
		emma_fd = -1;
		g_strlcpy(estado, "offline", sizeof(estado));
	}
	gv_app_redesenhar(a);
}

/* ---- o desenho ---- */

static void pedir(const char *p) {
	char r[256];
	emmac_pedido(p, r, sizeof(r));
}

static void enviar(const char *texto) {
	if (!*texto) {
		return;
	}
	char *p = g_strdup_printf("enviar %s", texto);
	pedir(p);
	g_free(p);
}

static void abrir_secao(const char *secao) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			setsid();
			execlp("lumna-painel", "lumna-painel", "--secao", secao, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

static void abrir_app_completo(void) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			setsid();
			execlp("lumna-emma-app", "lumna-emma-app", (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
	gv_app_sair(app);
}

static const char *frase_estado(void) {
	return !strcmp(estado, "offline") ? "Serviço parado" : !strcmp(estado, "ouvindo") ? "Ouvindo…" : !strcmp(estado, "transcrevendo") ? "Entendendo…" :
		!strcmp(estado, "pensando") ? "Pensando…" : !strcmp(estado, "falando") ? "Falando…" : servidor ? "Disponível" : "Só comandos curtos";
}

static Clay_Color cor_estado(void) {
	return !strcmp(estado, "offline") || (!servidor && !strcmp(estado, "ocioso")) ? gv_t.texto_suave : !strcmp(estado, "ouvindo") ? gv_t.perigo :
		!strcmp(estado, "ocioso") ? gv_t.sucesso : gv_t.acento;
}

static bool botao_icone(Clay_ElementId id, const char *icone, bool ligado) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ligado ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		tocado |= gv_focavel(id, gv_t.raio_medio);
		gv_icone(icone, 22, ligado ? gv_t.acento : gv_t.texto_secundario);
	}
	return tocado;
}

static void avatar(Clay_ElementId id, float tam) {
	if (dir_assets) {
		CLAY(id, gv_imagem(id, dir_assets, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) } }, .cornerRadius = CLAY_CORNER_RADIUS(tam / 2) })) {}
	} else {
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.acento, 0.2f), .cornerRadius = CLAY_CORNER_RADIUS(tam / 2) }) {
			gv_icone("sparkle", tam * 0.5f, gv_t.acento);
		}
	}
}

static void chip(Clay_ElementId id, const char *texto, bool principal) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .padding = { 14, 14, 8, 8 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = principal ? gv_alfa(gv_t.acento, 0.16f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(18), .border = { .color = principal ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		tocado |= gv_focavel(id, 18);
		gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
	}
	if (tocado) {
		enviar(texto);
	}
}

/* o nome de uma seção do Painel para o cartão: "teclado/teclado" -> ("Layouts de teclado", "Teclado e mouse") */
static void nome_da_secao(const char *secao, char *titulo, size_t tt, char *sub, size_t ts) {
	static const struct { const char *id, *titulo, *sub; } mapa[] = {
		{ "teclado/teclado", "Layouts de teclado", "Teclado e mouse" }, { "teclado/mouse", "Mouse e touchpad", "Teclado e mouse" },
		{ "teclado/atalhos", "Atalhos", "Teclado e mouse" }, { "telas/monitores", "Monitores", "Telas e modos" }, { "telas/modos", "Modos", "Telas e modos" },
		{ "rede/wifi", "Wi-Fi", "Rede" }, { "bluetooth/dispositivos", "Dispositivos Bluetooth", "Bluetooth" }, { "aplicativos/instalados", "Aplicativos instalados", "Aplicativos" },
		{ "aplicativos/atualizacoes", "Atualizações de apps", "Aplicativos" }, { "armazenamento/uso", "Uso do disco", "Armazenamento" },
		{ "armazenamento/limpeza", "Limpeza", "Armazenamento" }, { "armazenamento/discos", "Discos e pendrives", "Armazenamento" },
		{ "data/hora", "Data e hora", "Data e hora" }, { "data/fuso", "Fuso horário", "Data e hora" }, { "sobre/computador", "Este computador", "Sobre" },
		{ "sobre/atualizacoes", "Atualizações do sistema", "Sobre" }, { "aparencia/tema", "Tema", "Aparência" }, { "aparencia/papel", "Papel de parede", "Aparência" },
	};
	for (guint i = 0; i < G_N_ELEMENTS(mapa); i++) {
		if (!strcmp(secao, mapa[i].id)) {
			g_strlcpy(titulo, mapa[i].titulo, tt);
			g_strlcpy(sub, mapa[i].sub, ts);
			return;
		}
	}
	const char *barra = strchr(secao, '/');
	g_strlcpy(titulo, barra ? barra + 1 : secao, tt);
	titulo[0] = g_ascii_toupper(titulo[0]);
	g_strlcpy(sub, "Painel de controle", ts);
}

static void cartao_secao(uint32_t k, const struct ferramenta *f) {
	Clay_ElementId id = CLAY_IDI("em-cartao", k);
	bool tocado = gv_tocado(id);
	const char *icone = strstr(f->secao, "teclado") ? "keyboard" : strstr(f->secao, "telas") ? "desktop" : strstr(f->secao, "rede") ? "wifi" : "settings";
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0, 380) }, .padding = CLAY_PADDING_ALL(14), .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		tocado |= gv_focavel(id, gv_t.raio_medio);
		gv_icone(icone, 36, gv_t.texto);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			char titulo[96], sub[96];
			nome_da_secao(f->secao, titulo, sizeof(titulo), sub, sizeof(sub));
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_fmt("Painel de controle · %s", sub), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(CLAY_STRING("Abrir configuração"), gv_t.tamanho_corpo - 1, gv_t.acento, GV_PESO_NORMAL);
				gv_icone("forward", 14, gv_t.acento);
			}
		}
	}
	if (tocado) {
		abrir_secao(f->secao);
	}
}

static void bolha(uint32_t k, const struct msg *m) {
	if (m->usuario) {
		CLAY(CLAY_IDI("em-u", k), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			CLAY(CLAY_IDI("em-ub", k), { .layout = { .sizing = { CLAY_SIZING_FIT(0, 340) }, .padding = { 16, 16, 12, 12 } },
				.backgroundColor = VIOLETA, .cornerRadius = CLAY_CORNER_RADIUS(18) }) {
				gv_texto(gv_str(m->texto), gv_t.tamanho_corpo + 1, (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_NORMAL);
			}
			gv_texto(gv_fmt("%s ✓✓", m->hora), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		return;
	}
	struct resposta_lida r;
	ler_resposta(m->texto, &r);
	CLAY(CLAY_IDI("em-e", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		avatar(CLAY_IDI("em-ea", k), 34);
		CLAY(CLAY_IDI("em-ec", k), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
			if (r.corpo[0] || m->parcial) {
				CLAY(CLAY_IDI("em-eb", k), { .layout = { .sizing = { CLAY_SIZING_FIT(0, 340) }, .padding = { 16, 16, 12, 12 } },
					.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(18) }) {
					gv_texto(gv_str(r.corpo[0] ? r.corpo : "…"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
				}
			}
			for (guint i = 0; i < m->ferramentas->len; i++) {
				const struct ferramenta *f = &g_array_index(m->ferramentas, struct ferramenta, i);
				if (!strcmp(f->nome, "painel.abrir") || !strcmp(f->nome, "lumna_painel_abrir")) {
					if (f->secao[0]) {
						cartao_secao(k * 16 + i, f);
					}
				} else if (f->resumo[0] && strcmp(f->resumo, r.corpo)) {
					CLAY(CLAY_IDI("em-ft", k * 16 + i), { .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						gv_icone(f->ok ? "check" : "close", 14, f->ok ? gv_t.sucesso : gv_t.perigo);
						gv_texto(gv_str(f->resumo), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
				}
			}
			if (!m->parcial && (r.principal[0] || r.nlinks || r.nsug)) {
				/* os botões, dois por linha (o Clay não quebra linha sozinho) */
				const char *rot[5];
				bool prin[5];
				int nb = 0;
				if (r.principal[0]) {
					rot[nb] = r.principal;
					prin[nb++] = true;
				}
				for (int i = 0; i < r.nlinks; i++) {
					rot[nb] = r.links[i];
					prin[nb++] = false;
				}
				for (int i = 0; i < r.nsug; i++) {
					rot[nb] = r.sugestoes[i];
					prin[nb++] = false;
				}
				for (int i = 0; i < nb; i += 2) {
					CLAY(CLAY_IDI("em-chl", k * 4 + i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
						for (int j = i; j < nb && j < i + 2; j++) {
							chip(CLAY_IDI("em-chip", k * 8 + j), rot[j], prin[j]);
						}
					}
				}
			}
			if (!m->parcial && m->hora[0]) {
				gv_texto(gv_str(m->hora), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
	}
	g_free(r.corpo);
}

static void desenhar(gv_app *a, void *dados) {
	(void)dados;
	if (gv_tecla() == XKB_KEY_Escape) {
		gv_app_sair(a);
		return;
	}
	if (fechar_em > 0) {
		if (gv_agora() >= fechar_em) {
			gv_app_sair(a);
			return;
		}
		gv_acordar_em(fechar_em - gv_agora() + 0.05);
	}
	if (emma_fd < 0) {
		emma_fd = emmac_assinar();
		if (emma_fd >= 0) {
			gv_app_vigiar(a, emma_fd, emma_mudou, NULL);
			g_strlcpy(estado, "ocioso", sizeof(estado));
		} else {
			gv_acordar_em(3);
		}
	}
	bool ouvindo = !strcmp(estado, "ouvindo");
	CLAY(CLAY_ID("em-painel"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 16, 16, 16, 12 }, .childGap = 10 },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		/* o cabeçalho */
		CLAY(CLAY_ID("em-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			avatar(CLAY_ID("em-av"), 64);
			CLAY(CLAY_ID("em-nome"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
				gv_texto(CLAY_STRING("Emma"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
				CLAY_AUTO_ID({ .layout = { .childGap = 7, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY(CLAY_ID("em-ponto"), { .layout = { .sizing = { CLAY_SIZING_FIXED(9), CLAY_SIZING_FIXED(9) } }, .backgroundColor = cor_estado(), .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
					gv_texto(gv_str(frase_estado()), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				struct modos_estado me;
				const char *modo = modos_ler(&me) && me.orbit ? modos_nome(modos_em_foco(&me)) : "";
				gv_texto(gv_fmt("Ajudando no modo %s", modo), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			if (botao_icone(CLAY_ID("em-app"), "external", false)) {
				abrir_app_completo();
			}
			if (botao_icone(CLAY_ID("em-pin"), "pin", fixado)) {
				fixado = !fixado;
				if (fixado) {
					fechar_em = 0;
				}
			}
			if (botao_icone(CLAY_ID("em-fechar"), "close", false)) {
				gv_app_sair(a);
			}
		}
		gv_separador(false, 0);
		/* a conversa */
		Clay_ElementId lista = CLAY_ID("em-lista");
		CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
			.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 14, .padding = { 0, 6, 4, 8 } } })) {
			if (!msgs->len) {
				CLAY(CLAY_ID("em-vazio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 30, 10 }, .childGap = 8 } }) {
					gv_texto(CLAY_STRING("Oi! Peça algo ou pergunte."), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(CLAY_STRING("Ex.: \"abre a Netflix\", \"volume 30\", \"como adiciono outro layout de teclado?\""), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
			for (guint i = 0; i < msgs->len; i++) {
				bolha(i, msgs->pdata[i]);
			}
			if (conf_id[0]) {
				CLAY(CLAY_ID("em-lic"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 8 },
					.backgroundColor = gv_alfa(gv_t.aviso, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_alfa(gv_t.aviso, 0.5f), .width = GV_BORDA(1) } }) {
					gv_texto(gv_fmt("A Emma quer: %s", conf_nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(conf_desc), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					CLAY(CLAY_ID("em-lic-b"), { .layout = { .childGap = 10 } }) {
						if (gv_botao(CLAY_ID("em-lic-nao"), NULL, "Agora não", GV_BOTAO_ELEVADO)) {
							char p[64];
							snprintf(p, sizeof(p), "confirmar %s nao", conf_id);
							pedir(p);
							conf_id[0] = '\0';
						}
						if (gv_botao(CLAY_ID("em-lic-sim"), "check", "Pode", GV_BOTAO_CHEIO)) {
							char p[64];
							snprintf(p, sizeof(p), "confirmar %s sim", conf_id);
							pedir(p);
							conf_id[0] = '\0';
						}
					}
				}
			}
			if (erro[0]) {
				gv_texto(gv_str(erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
		}
		if (rolar_fim) {
			gv_rolar_para(lista, 0, 1e7f, false);
			rolar_fim = false;
		}
		/* o contexto e a conversa nova */
		CLAY(CLAY_ID("em-meio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("em-ctx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (ctx_ativo && ctx_titulo[0]) {
					gv_icone("desktop", 18, gv_t.texto_secundario);
					gv_texto(gv_fmt("Contexto: %s", ctx_titulo), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
					if (botao_icone(CLAY_ID("em-ctx-x"), "close", false)) {
						ctx_ativo = false;
						pedir("contexto nao");
					}
				} else if (!ctx_ativo) {
					if (gv_botao(CLAY_ID("em-ctx-on"), "desktop", "Usar o que estou vendo", GV_BOTAO_SIMPLES)) {
						ctx_ativo = true;
						pedir("contexto sim");
						atualizar_contexto();
					}
				}
			}
			if (gv_botao(CLAY_ID("em-nova"), "plus-circle", "Nova conversa", GV_BOTAO_ELEVADO)) {
				pedir("nova");
				g_ptr_array_set_size(msgs, 0);
			}
		}
		/* escrever */
		CLAY(CLAY_ID("em-entrada"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				if ((gv_campo(CLAY_ID("em-campo"), campo, sizeof(campo), ouvindo ? "Ouvindo…" : "Pergunte à Emma…", GV_CAMPO_TEXTO) & GV_CAMPO_ENTER) && campo[0]) {
					enviar(campo);
					campo[0] = '\0';
				}
			}
			if (botao_icone(CLAY_ID("em-mic"), ouvindo ? "mic" : "mic", ouvindo)) {
				pedir("ouvir");
			}
			Clay_ElementId env = CLAY_ID("em-enviar");
			bool tocado = gv_tocado(env);
			CLAY(env, { .layout = { .sizing = { CLAY_SIZING_FIXED(46), CLAY_SIZING_FIXED(46) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(env) ? gv_alfa(VIOLETA, 0.85f) : VIOLETA, .cornerRadius = CLAY_CORNER_RADIUS(23) }) {
				tocado |= gv_focavel(env, 23);
				gv_icone("send", 22, (Clay_Color){ 255, 255, 255, 255 });
			}
			if (tocado && campo[0]) {
				enviar(campo);
				campo[0] = '\0';
			}
		}
		CLAY(CLAY_ID("em-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			if (gv_botao(CLAY_ID("em-completo"), "external", "Abrir app completo", GV_BOTAO_SIMPLES)) {
				abrir_app_completo();
			}
		}
	}
}

int main(void) {
	msgs = g_ptr_array_new_with_free_func(msg_liberar);
	dir_assets = achar_asset("avatar.jpg");
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Emma", .app_id = "lumna-emma-chat", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_DIREITA | GV_ANCORA_CIMA | GV_ANCORA_BAIXO, .largura = 480, .teclado = true, .margens = { 52, 20, 96, 0 } });
	if (!app) {
		return 1;
	}
	carregar_historico();
	atualizar_contexto();
	gv_focar(CLAY_ID("em-campo"));
	return gv_app_correr(app, desenhar, NULL);
}
