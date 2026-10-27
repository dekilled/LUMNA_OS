/*
 * lumna-emma-app: o app completo da Emma, em janela. À esquerda a barra com as conversas e as rotinas; no meio o chat
 * (a mesma conversa do painel flutuante, só que por sessão), a Memória de cada rotina, as Skills e as próprias Rotinas.
 *
 * Quem pensa e age é o serviço lumna-emma (servicos/emmac.h), que por baixo fala com o servidor da Emma (o backend):
 * as listas e os cadastros vão por "rest" (o serviço cuida do login), a conversa por "conversar" e chega por eventos
 * com canal "app". Provedores, voz e permissões não ficam aqui: ficam no Painel de controle.
 */
#define _GNU_SOURCE
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "emma-ui.h"
#include "emmac.h"
#include "gravity.h"
#include "json.h"

enum vista { V_CHAT, V_MEMORIA, V_SKILLS, V_ROTINAS };

struct sessao {
	char id[40];
	char titulo[120];
};

struct rotina {
	char id[40];
	char nome[96];
	char descricao[200];
};

struct memoria {
	char id[40];
	char tipo[16];
	char texto[400];
};

struct skill {
	char id[40];
	char nome[96];
	char descricao[200];
	char icone[32];
};

#define LIM_RESP (8 * 1024 * 1024)

static gv_app *app;
static int emma_fd = -1;
static char estado[16] = "offline";
static bool servidor;
static enum vista vista = V_CHAT;

static GArray *sessoes;   /* struct sessao, da rotina escolhida */
static GArray *rotinas;   /* struct rotina */
static GArray *memorias;  /* struct memoria */
static GArray *skills;    /* struct skill */
static int rotina_atual = -1;       /* -1: o chat livre */
static char sid[40];                /* a sessão aberta ("" : a conversa ainda não existe) */
static char titulo[120] = "Nova conversa";
static GPtrArray *msgs;
static char skill_id[40], skill_nome[96];
static char campo[2000], busca[96], novo_nome[96], novo_desc[200], nova_memoria[400], nome_edicao[120];
static int tipo_memoria;
static bool renomeando, excluindo, rolar_fim = true, ctx_ativo = true;
static char erro[300];
static char conf_id[24], conf_nome[96], conf_desc[256];
static char *dir_fundo;
static char rotina_excluir[40];

/* ---- o servidor: tudo passa pelo serviço ---- */

struct resposta {
	struct json *raiz;
	const struct json *corpo;
	bool ok;
	char msg[200];
};

static bool rest(const char *metodo, const char *caminho, const char *corpo, struct resposta *r) {
	static char *buf;
	if (!buf) {
		buf = g_malloc(LIM_RESP);
	}
	memset(r, 0, sizeof(*r));
	char *p = g_strdup_printf("rest %s %s %s", metodo, caminho, corpo ? corpo : "");
	bool falou = emmac_pedido(p, buf, LIM_RESP);
	g_free(p);
	if (!falou) {
		g_strlcpy(r->msg, "O serviço da Emma não respondeu.", sizeof(r->msg));
		return false;
	}
	r->raiz = json_ler(buf);
	r->corpo = json_chave(r->raiz, "corpo");
	r->ok = json_bool(json_chave(r->raiz, "ok"), false);
	if (!r->ok) {
		const char *e = json_chave_texto(r->raiz, "erro"), *d = json_chave_texto(r->corpo, "detail");
		g_strlcpy(r->msg, *d ? d : *e ? e : "O servidor da Emma não respondeu.", sizeof(r->msg));
	}
	return r->ok;
}

static void liberar(struct resposta *r) {
	json_liberar(r->raiz);
	r->raiz = NULL;
}

static void pedir(const char *p) {
	char r[256];
	emmac_pedido(p, r, sizeof(r));
}

static char *hora_local(const char *iso) {
	GTimeZone *utc = g_time_zone_new_utc();
	GDateTime *t = *iso ? g_date_time_new_from_iso8601(iso, utc) : NULL;
	char *r = NULL;
	if (t) {
		GDateTime *l = g_date_time_to_local(t);
		r = g_date_time_format(l, "%H:%M");
		g_date_time_unref(l);
		g_date_time_unref(t);
	}
	g_time_zone_unref(utc);
	return r ? r : g_strdup("");
}

/* ---- as listas ---- */

static void carregar_sessoes(void) {
	g_array_set_size(sessoes, 0);
	char *c = g_strdup_printf("/api/sessions?project_id=%s", rotina_atual >= 0 ? g_array_index(rotinas, struct rotina, rotina_atual).id : "none");
	struct resposta r;
	if (rest("GET", c, NULL, &r) && r.corpo) {
		for (int i = 0; i < r.corpo->nfilhos; i++) {
			const struct json *s = json_item(r.corpo, i);
			struct sessao n = { 0 };
			g_strlcpy(n.id, json_chave_texto(s, "id"), sizeof(n.id));
			g_strlcpy(n.titulo, json_chave_texto(s, "title"), sizeof(n.titulo));
			g_array_append_val(sessoes, n);
		}
	}
	liberar(&r);
	g_free(c);
}

static void carregar_rotinas(void) {
	g_array_set_size(rotinas, 0);
	struct resposta r;
	if (rest("GET", "/api/projects", NULL, &r) && r.corpo) {
		for (int i = 0; i < r.corpo->nfilhos; i++) {
			const struct json *s = json_item(r.corpo, i);
			struct rotina n = { 0 };
			g_strlcpy(n.id, json_chave_texto(s, "id"), sizeof(n.id));
			g_strlcpy(n.nome, json_chave_texto(s, "name"), sizeof(n.nome));
			g_strlcpy(n.descricao, json_chave_texto(s, "description"), sizeof(n.descricao));
			g_array_append_val(rotinas, n);
		}
	}
	liberar(&r);
	if (rotina_atual >= (int)rotinas->len) {
		rotina_atual = -1;
	}
}

static void carregar_memorias(void) {
	g_array_set_size(memorias, 0);
	if (rotina_atual < 0) {
		return;
	}
	char *c = g_strdup_printf("/api/projects/%s/memories", g_array_index(rotinas, struct rotina, rotina_atual).id);
	struct resposta r;
	if (rest("GET", c, NULL, &r) && r.corpo) {
		for (int i = 0; i < r.corpo->nfilhos; i++) {
			const struct json *s = json_item(r.corpo, i);
			struct memoria n = { 0 };
			g_strlcpy(n.id, json_chave_texto(s, "id"), sizeof(n.id));
			g_strlcpy(n.tipo, json_chave_texto(s, "kind"), sizeof(n.tipo));
			g_strlcpy(n.texto, json_chave_texto(s, "content"), sizeof(n.texto));
			g_array_append_val(memorias, n);
		}
	}
	liberar(&r);
	g_free(c);
}

static void carregar_skills(void) {
	g_array_set_size(skills, 0);
	struct resposta r;
	if (rest("GET", "/api/skills", NULL, &r) && r.corpo) {
		for (int i = 0; i < r.corpo->nfilhos; i++) {
			const struct json *s = json_item(r.corpo, i);
			struct skill n = { 0 };
			g_strlcpy(n.id, json_chave_texto(s, "id"), sizeof(n.id));
			g_strlcpy(n.nome, json_chave_texto(s, "name"), sizeof(n.nome));
			g_strlcpy(n.descricao, json_chave_texto(s, "description"), sizeof(n.descricao));
			g_strlcpy(n.icone, json_chave_texto(s, "icon"), sizeof(n.icone));
			g_array_append_val(skills, n);
		}
	}
	liberar(&r);
}

/* ---- a conversa aberta ---- */

static void zerar_conversa(void) {
	sid[0] = '\0';
	g_strlcpy(titulo, "Nova conversa", sizeof(titulo));
	g_ptr_array_set_size(msgs, 0);
	conf_id[0] = erro[0] = '\0';
	renomeando = excluindo = false;
	rolar_fim = true;
}

static void abrir_sessao(const char *id, const char *tit) {
	zerar_conversa();
	g_strlcpy(sid, id, sizeof(sid));
	g_strlcpy(titulo, tit, sizeof(titulo));
	char *c = g_strdup_printf("/api/sessions/%s/messages", id);
	struct resposta r;
	if (rest("GET", c, NULL, &r) && r.corpo) {
		for (int i = 0; i < r.corpo->nfilhos; i++) {
			const struct json *m = json_item(r.corpo, i);
			if (*json_chave_texto(m, "marker")) {
				continue; /* um passo que o app mandou (plano/construir), não uma fala */
			}
			char *h = hora_local(json_chave_texto(m, "created_at"));
			struct eu_msg *n = eu_msg_nova(msgs, !strcmp(json_chave_texto(m, "role"), "user"), json_chave_texto(m, "content"), h);
			g_free(h);
			const struct json *ts = json_chave(m, "tools");
			for (int k = 0; ts && k < ts->nfilhos; k++) {
				const struct json *t = json_item(ts, k);
				struct eu_ferramenta f = { 0 };
				g_strlcpy(f.nome, json_chave_texto(t, "name"), sizeof(f.nome));
				g_strlcpy(f.resumo, json_chave_texto(t, "summary"), sizeof(f.resumo));
				f.ok = !strcmp(json_chave_texto(t, "status"), "ok");
				g_strlcpy(f.secao, json_chave_texto(json_chave(t, "args"), "secao"), sizeof(f.secao));
				g_array_append_val(n->ferramentas, f);
			}
		}
	} else if (r.msg[0]) {
		g_strlcpy(erro, r.msg, sizeof(erro));
	}
	liberar(&r);
	g_free(c);
	vista = V_CHAT;
}

static void escolher_rotina(int i) {
	rotina_atual = i;
	zerar_conversa();
	busca[0] = '\0';
	carregar_sessoes();
	if (vista == V_MEMORIA) {
		carregar_memorias();
	} else {
		vista = V_CHAT;
	}
}

static bool garantir_sessao(void) {
	if (sid[0]) {
		return true;
	}
	GString *c = g_string_new("{\"title\":\"Nova conversa\"");
	if (rotina_atual >= 0) {
		g_string_append(c, ",\"project_id\":");
		json_escrever_texto(c, g_array_index(rotinas, struct rotina, rotina_atual).id);
	}
	g_string_append_c(c, '}');
	struct resposta r;
	bool ok = rest("POST", "/api/sessions", c->str, &r) && *json_chave_texto(r.corpo, "id");
	if (ok) {
		g_strlcpy(sid, json_chave_texto(r.corpo, "id"), sizeof(sid));
	} else {
		g_strlcpy(erro, r.msg[0] ? r.msg : "Não consegui criar a conversa.", sizeof(erro));
	}
	liberar(&r);
	g_string_free(c, TRUE);
	return ok;
}

static void enviar(const char *texto) {
	if (!*texto || !garantir_sessao()) {
		return;
	}
	if (!msgs->len) {
		/* o título vem da primeira fala */
		char *t = g_strndup(texto, 48);
		char *ult = g_utf8_make_valid(t, -1);
		g_free(t);
		GString *c = g_string_new("{\"title\":");
		json_escrever_texto(c, ult);
		g_string_append_c(c, '}');
		char *caminho = g_strdup_printf("/api/sessions/%s", sid);
		struct resposta r;
		if (rest("PATCH", caminho, c->str, &r)) {
			g_strlcpy(titulo, ult, sizeof(titulo));
		}
		liberar(&r);
		g_free(caminho);
		g_string_free(c, TRUE);
		g_free(ult);
		carregar_sessoes();
	}
	GString *c = g_string_new("{\"sid\":");
	json_escrever_texto(c, sid);
	g_string_append(c, ",\"texto\":");
	json_escrever_texto(c, texto);
	if (skill_id[0]) {
		g_string_append(c, ",\"skill\":");
		json_escrever_texto(c, skill_id);
	}
	g_string_append_c(c, '}');
	char *p = g_strdup_printf("conversar %s", c->str);
	pedir(p);
	g_free(p);
	g_string_free(c, TRUE);
	skill_id[0] = skill_nome[0] = erro[0] = '\0';
}

static void aplicar_evento(const struct json *j) {
	const char *ev = json_chave_texto(j, "evento");
	if (!strcmp(ev, "estado")) {
		g_strlcpy(estado, json_chave_texto(j, "estado"), sizeof(estado));
		servidor = json_bool(json_chave(j, "online"), false);
		return;
	}
	if (strcmp(json_chave_texto(j, "canal"), "app") || !sid[0] || strcmp(json_chave_texto(j, "sessao"), sid)) {
		return; /* de outra conversa, ou do painel flutuante */
	}
	if (!strcmp(ev, "ouvido")) {
		erro[0] = '\0';
		eu_msg_nova(msgs, true, json_chave_texto(j, "texto"), NULL);
		rolar_fim = true;
	} else if (!strcmp(ev, "delta")) {
		struct eu_msg *m = eu_msg_parcial(msgs);
		if (!m) {
			m = eu_msg_nova(msgs, false, "", NULL);
			m->parcial = true;
		}
		char *t = g_strconcat(m->texto, json_chave_texto(j, "texto"), NULL);
		g_free(m->texto);
		m->texto = t;
		rolar_fim = true;
	} else if (!strcmp(ev, "ferramenta")) {
		struct eu_msg *m = eu_msg_parcial(msgs);
		if (!m) {
			m = eu_msg_nova(msgs, false, "", NULL);
			m->parcial = true;
		}
		eu_juntar_ferramenta(m, j);
		conf_id[0] = '\0';
	} else if (!strcmp(ev, "resposta")) {
		struct eu_msg *m = eu_msg_parcial(msgs);
		const char *t = json_chave_texto(j, "texto");
		if (!m && *t) {
			m = eu_msg_nova(msgs, false, "", NULL);
		}
		if (m) {
			if (*t) {
				g_free(m->texto);
				m->texto = g_strdup(t);
			}
			m->parcial = false;
		}
		rolar_fim = true;
	} else if (!strcmp(ev, "confirmar")) {
		g_strlcpy(conf_id, json_chave_texto(j, "id"), sizeof(conf_id));
		g_strlcpy(conf_nome, json_chave_texto(j, "nome"), sizeof(conf_nome));
		g_strlcpy(conf_desc, json_chave_texto(j, "descricao"), sizeof(conf_desc));
		rolar_fim = true;
	} else if (!strcmp(ev, "erro")) {
		g_strlcpy(erro, json_chave_texto(j, "mensagem"), sizeof(erro));
	}
}

static void emma_mudou(gv_app *a, int fd, void *dados) {
	(void)dados;
	bool caiu = false;
	char *linha;
	while ((linha = emmac_assinar_ler(fd, &caiu))) {
		struct json *j = json_ler(linha);
		if (j) {
			aplicar_evento(j);
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

/* ---- as ações sobre o servidor ---- */

static void nova_conversa(void) {
	zerar_conversa();
	vista = V_CHAT;
	gv_focar(CLAY_ID("ap-campo"));
}

static void renomear_conversa(const char *novo) {
	if (!sid[0] || !*novo) {
		return;
	}
	GString *c = g_string_new("{\"title\":");
	json_escrever_texto(c, novo);
	g_string_append_c(c, '}');
	char *caminho = g_strdup_printf("/api/sessions/%s", sid);
	struct resposta r;
	if (rest("PATCH", caminho, c->str, &r)) {
		g_strlcpy(titulo, novo, sizeof(titulo));
		carregar_sessoes();
	} else {
		g_strlcpy(erro, r.msg, sizeof(erro));
	}
	liberar(&r);
	g_free(caminho);
	g_string_free(c, TRUE);
}

static void excluir_conversa(void) {
	char *caminho = g_strdup_printf("/api/sessions/%s", sid);
	struct resposta r;
	rest("DELETE", caminho, NULL, &r);
	liberar(&r);
	g_free(caminho);
	zerar_conversa();
	carregar_sessoes();
}

static void criar_rotina(void) {
	GString *c = g_string_new("{\"name\":");
	json_escrever_texto(c, novo_nome);
	g_string_append(c, ",\"description\":");
	json_escrever_texto(c, novo_desc);
	g_string_append_c(c, '}');
	struct resposta r;
	if (rest("POST", "/api/projects", c->str, &r)) {
		novo_nome[0] = novo_desc[0] = '\0';
		carregar_rotinas();
	} else {
		g_strlcpy(erro, r.msg, sizeof(erro));
	}
	liberar(&r);
	g_string_free(c, TRUE);
}

static void excluir_rotina(const char *id) {
	char *caminho = g_strdup_printf("/api/projects/%s", id);
	struct resposta r;
	rest("DELETE", caminho, NULL, &r);
	liberar(&r);
	g_free(caminho);
	if (rotina_atual >= 0 && !strcmp(g_array_index(rotinas, struct rotina, rotina_atual).id, id)) {
		rotina_atual = -1;
		zerar_conversa();
	}
	carregar_rotinas();
	carregar_sessoes();
}

static void adicionar_memoria(void) {
	static const char *tipos[] = { "preference", "decision", "fact" };
	GString *c = g_string_new("{\"kind\":");
	json_escrever_texto(c, tipos[tipo_memoria]);
	g_string_append(c, ",\"content\":");
	json_escrever_texto(c, nova_memoria);
	g_string_append_c(c, '}');
	char *caminho = g_strdup_printf("/api/projects/%s/memories", g_array_index(rotinas, struct rotina, rotina_atual).id);
	struct resposta r;
	if (rest("POST", caminho, c->str, &r)) {
		nova_memoria[0] = '\0';
		carregar_memorias();
	} else {
		g_strlcpy(erro, r.msg, sizeof(erro));
	}
	liberar(&r);
	g_free(caminho);
	g_string_free(c, TRUE);
}

static void esquecer_memoria(const char *id) {
	char *caminho = g_strdup_printf("/api/memories/%s", id);
	struct resposta r;
	rest("DELETE", caminho, NULL, &r);
	liberar(&r);
	g_free(caminho);
	carregar_memorias();
}

/* ---- o desenho ---- */

static const char *frase_estado(void) {
	return !strcmp(estado, "offline") ? "Serviço parado" : !strcmp(estado, "ouvindo") ? "Ouvindo…" : !strcmp(estado, "transcrevendo") ? "Entendendo…" :
		!strcmp(estado, "pensando") ? "Pensando…" : !strcmp(estado, "falando") ? "Falando…" : servidor ? "Disponível" : "Só comandos curtos";
}

static Clay_Color cor_estado(void) {
	return !strcmp(estado, "offline") || (!servidor && !strcmp(estado, "ocioso")) ? gv_t.texto_suave : !strcmp(estado, "ouvindo") ? gv_t.perigo :
		!strcmp(estado, "ocioso") ? gv_t.sucesso : gv_t.acento;
}

static bool item_lateral(Clay_ElementId id, const char *icone, const char *texto, bool ativo) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 9, 9 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativo ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		tocado |= gv_focavel(id, gv_t.raio_medio);
		if (icone) {
			gv_icone(icone, 20, ativo ? gv_t.acento : gv_t.texto_secundario);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo, ativo ? gv_t.texto : gv_t.texto_secundario, ativo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
		}
	}
	return tocado;
}

static void titulo_secao(const char *texto) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 14, 4 } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_MEDIO);
	}
}

static void barra_lateral(void) {
	CLAY(CLAY_ID("ap-lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(280), CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 14, 14 }, .childGap = 2 },
		.backgroundColor = gv_t.elevada }) {
		CLAY(CLAY_ID("ap-id"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 14 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			eu_avatar(CLAY_ID("ap-av"), 44);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 2 } }) {
				gv_texto(CLAY_STRING("Emma"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
				CLAY_AUTO_ID({ .layout = { .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY(CLAY_ID("ap-ponto"), { .layout = { .sizing = { CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8) } }, .backgroundColor = cor_estado(), .cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
					gv_texto(gv_str(frase_estado()), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
		}
		if (gv_botao_ext(CLAY_ID("ap-nova"), &(gv_botao_opcoes){ .icone = "plus-circle", .texto = "Nova conversa", .estilo = GV_BOTAO_ELEVADO, .crescer = true })) {
			nova_conversa();
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(8) } } }) {}
		if (item_lateral(CLAY_ID("ap-nav-chat"), "chat", "Conversas", vista == V_CHAT)) {
			vista = V_CHAT;
		}
		if (item_lateral(CLAY_ID("ap-nav-mem"), "memory", "Memória", vista == V_MEMORIA)) {
			vista = V_MEMORIA;
			carregar_memorias();
		}
		if (item_lateral(CLAY_ID("ap-nav-skills"), "sparkle", "Skills", vista == V_SKILLS)) {
			vista = V_SKILLS;
			carregar_skills();
		}
		titulo_secao("ROTINAS");
		if (item_lateral(CLAY_ID("ap-rot-livre"), "chat", "Chat livre", rotina_atual < 0 && vista != V_ROTINAS)) {
			escolher_rotina(-1);
		}
		for (guint i = 0; i < rotinas->len; i++) {
			const struct rotina *r = &g_array_index(rotinas, struct rotina, i);
			if (item_lateral(CLAY_IDI("ap-rot", i), "folder", r->nome, rotina_atual == (int)i && vista != V_ROTINAS)) {
				escolher_rotina((int)i);
			}
		}
		if (item_lateral(CLAY_ID("ap-rot-gerir"), "settings", "Gerenciar rotinas", vista == V_ROTINAS)) {
			vista = V_ROTINAS;
		}
		titulo_secao("SESSÕES");
		Clay_ElementId lista = CLAY_ID("ap-sessoes");
		CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 2 } })) {
			char *filtro = busca[0] ? g_utf8_casefold(busca, -1) : NULL;
			int mostradas = 0;
			for (guint i = 0; i < sessoes->len; i++) {
				const struct sessao *s = &g_array_index(sessoes, struct sessao, i);
				if (filtro) {
					char *t = g_utf8_casefold(s->titulo, -1);
					bool tem = strstr(t, filtro) != NULL;
					g_free(t);
					if (!tem) {
						continue;
					}
				}
				mostradas++;
				if (item_lateral(CLAY_IDI("ap-ses", i), NULL, s->titulo, !strcmp(s->id, sid) && vista == V_CHAT)) {
					abrir_sessao(s->id, s->titulo);
				}
			}
			if (!mostradas) {
				CLAY_AUTO_ID({ .layout = { .padding = { 12, 12, 6, 6 } } }) {
					gv_texto(gv_str(busca[0] ? "Nada com esse nome." : "Nenhuma conversa ainda."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			g_free(filtro);
		}
		if (item_lateral(CLAY_ID("ap-config"), "settings", "Configurações", false)) {
			eu_abrir_secao("emma/geral");
		}
	}
}

static void cabecalho(void) {
	CLAY(CLAY_ID("ap-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 20, 14, 14 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		const char *nome = vista == V_MEMORIA ? "Memória" : vista == V_SKILLS ? "Skills" : vista == V_ROTINAS ? "Rotinas" : titulo;
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .clip = { .horizontal = true } }) {
			if (renomeando && vista == V_CHAT) {
				int r = gv_campo(CLAY_ID("ap-renomear"), nome_edicao, sizeof(nome_edicao), "Nome da conversa", GV_CAMPO_TEXTO);
				if ((r & GV_CAMPO_ENTER) && nome_edicao[0]) {
					renomear_conversa(nome_edicao);
					renomeando = false;
				} else if (gv_tecla() == XKB_KEY_Escape) {
					renomeando = false;
				}
			} else {
				gv_texto(gv_str(nome), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			}
		}
		/* o modelo: "Automático" escolhe pelo roteamento que está no Painel */
		Clay_ElementId modelo = CLAY_ID("ap-modelo");
		bool t = gv_tocado(modelo);
		CLAY(modelo, { .layout = { .padding = { 14, 12, 8, 8 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(modelo) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			t |= gv_focavel(modelo, gv_t.raio_pequeno);
			gv_icone("sparkle", 16, gv_t.acento);
			gv_texto(CLAY_STRING("Automático"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			gv_icone("chevron-down", 14, gv_t.texto_secundario);
		}
		if (t) {
			eu_abrir_secao("emma/provedores");
		}
		CLAY(CLAY_ID("ap-busca-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(220) } } }) {
			gv_campo(CLAY_ID("ap-busca"), busca, sizeof(busca), "Buscar conversas", GV_CAMPO_BUSCA);
		}
		bool mais = eu_botao_icone(CLAY_ID("ap-mais"), "dots", gv_popup_aberto(CLAY_ID("ap-menu")));
		if (mais) {
			gv_popup_alternar(CLAY_ID("ap-menu"));
		}
		GV_POPUP(CLAY_ID("ap-menu"), CLAY_ID("ap-mais"), .alinhar = GV_ALINHAR_FIM, .largura = 240) {
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6) } }) {
				if (gv_menu_item(CLAY_ID("ap-m-nova"), "plus-circle", "Nova conversa", NULL)) {
					nova_conversa();
					gv_popup_fechar(CLAY_ID("ap-menu"));
				}
				if (sid[0]) {
					if (gv_menu_item(CLAY_ID("ap-m-ren"), "edit", "Renomear", NULL)) {
						g_strlcpy(nome_edicao, titulo, sizeof(nome_edicao));
						renomeando = true;
						vista = V_CHAT;
						gv_popup_fechar(CLAY_ID("ap-menu"));
						gv_focar(CLAY_ID("ap-renomear"));
					}
					if (gv_menu_item(CLAY_ID("ap-m-exc"), "trash", "Excluir conversa", NULL)) {
						excluindo = true;
						gv_popup_fechar(CLAY_ID("ap-menu"));
					}
				}
				if (gv_menu_item(CLAY_ID("ap-m-conf"), "settings", "Configurações da Emma", NULL)) {
					eu_abrir_secao("emma/geral");
					gv_popup_fechar(CLAY_ID("ap-menu"));
				}
			}
		}
	}
}

static void faixa_excluir(void) {
	CLAY(CLAY_ID("ap-exc"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 12, 12 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_alfa(gv_t.perigo, 0.12f) }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_fmt("Excluir \"%s\" e tudo o que foi dito nela?", titulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_botao(CLAY_ID("ap-exc-nao"), NULL, "Cancelar", GV_BOTAO_ELEVADO)) {
			excluindo = false;
		}
		if (gv_botao(CLAY_ID("ap-exc-sim"), "trash", "Excluir", GV_BOTAO_PERIGO)) {
			excluindo = false;
			excluir_conversa();
		}
	}
}

static bool sugestao(Clay_ElementId id, int n, const char *texto) {
	bool t = gv_tocado(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 12, 12 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? gv_alfa(gv_t.elevada, 0.95f) : gv_alfa(gv_t.elevada, 0.78f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		t |= gv_focavel(id, gv_t.raio_medio);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(28), CLAY_SIZING_FIXED(28) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.acento, 0.2f), .cornerRadius = CLAY_CORNER_RADIUS(14) }) {
			gv_texto(gv_fmt("%d", n), gv_t.tamanho_corpo - 1, gv_t.acento, GV_PESO_MEDIO);
		}
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
	}
	return t;
}

static void apresentacao(void) {
	CLAY(CLAY_ID("ap-hero"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 56, 56, 0, 0 }, .childGap = 18,
		.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Converse com a Emma."), gv_t.tamanho_destaque + 10, (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_MEDIO);
		gv_texto(CLAY_STRING("Peça algo, tire uma dúvida ou deixe que ela cuide do sistema."), gv_t.tamanho_corpo + 1, (Clay_Color){ 220, 220, 235, 255 }, GV_PESO_NORMAL);
		CLAY(CLAY_ID("ap-modos"), { .layout = { .childGap = 10, .padding = { 0, 0, 6, 6 } } }) {
			Clay_ElementId livre = CLAY_ID("ap-m-livre"), sis = CLAY_ID("ap-m-sis");
			bool tl = gv_tocado(livre), ts = gv_tocado(sis);
			CLAY(livre, { .layout = { .padding = { 16, 16, 9, 9 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = !ctx_ativo ? gv_alfa(gv_t.acento, 0.25f) : gv_alfa(gv_t.elevada, 0.7f),
				.cornerRadius = CLAY_CORNER_RADIUS(20), .border = { .color = !ctx_ativo ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				tl |= gv_focavel(livre, 20);
				gv_icone("chat", 16, gv_t.texto);
				gv_texto(CLAY_STRING("Conversa livre"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			CLAY(sis, { .layout = { .padding = { 16, 16, 9, 9 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = ctx_ativo ? gv_alfa(gv_t.acento, 0.25f) : gv_alfa(gv_t.elevada, 0.7f),
				.cornerRadius = CLAY_CORNER_RADIUS(20), .border = { .color = ctx_ativo ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				ts |= gv_focavel(sis, 20);
				gv_icone("desktop", 16, gv_t.texto);
				gv_texto(CLAY_STRING("Contexto do sistema"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (tl && ctx_ativo) {
				ctx_ativo = false;
				pedir("contexto nao");
			}
			if (ts && !ctx_ativo) {
				ctx_ativo = true;
				pedir("contexto sim");
			}
		}
		CLAY(CLAY_ID("ap-sugs"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(520) }, .childGap = 10 } }) {
			static const char *ideias[] = { "Abre a Netflix na TV", "Como adiciono outro layout de teclado?", "Troca esta tela para o modo Mídia" };
			for (int i = 0; i < 3; i++) {
				if (sugestao(CLAY_IDI("ap-sug", i), i + 1, ideias[i])) {
					enviar(ideias[i]);
				}
			}
		}
	}
}

static void conversa(void) {
	Clay_ElementId lista = CLAY_ID("ap-lista");
	CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 18, .padding = { 56, 56, 16, 16 } } })) {
		for (guint i = 0; i < msgs->len; i++) {
			eu_bolha(i, msgs->pdata[i], enviar);
		}
		if (conf_id[0]) {
			CLAY(CLAY_ID("ap-lic"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIT(0, 520) }, .padding = CLAY_PADDING_ALL(14), .childGap = 8 },
				.backgroundColor = gv_alfa(gv_t.aviso, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_alfa(gv_t.aviso, 0.5f), .width = GV_BORDA(1) } }) {
				gv_texto(gv_fmt("A Emma quer: %s", conf_nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_str(conf_desc), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				CLAY(CLAY_ID("ap-lic-b"), { .layout = { .childGap = 10 } }) {
					bool nao = gv_botao(CLAY_ID("ap-lic-nao"), NULL, "Agora não", GV_BOTAO_ELEVADO), sim = gv_botao(CLAY_ID("ap-lic-sim"), "check", "Pode", GV_BOTAO_CHEIO);
					if (nao || sim) {
						char p[64];
						snprintf(p, sizeof(p), "confirmar %s %s", conf_id, sim ? "sim" : "nao");
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
}

static void compositor_de_mensagem(void) {
	bool ouvindo = !strcmp(estado, "ouvindo");
	CLAY(CLAY_ID("ap-comp"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 56, 56, 6, 20 }, .childGap = 8 } }) {
		if (skill_id[0]) {
			CLAY_AUTO_ID({ .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("sparkle", 16, gv_t.acento);
				gv_texto(gv_fmt("Skill: %s", skill_nome), gv_t.tamanho_corpo - 1, gv_t.acento, GV_PESO_NORMAL);
				if (eu_botao_icone(CLAY_ID("ap-skill-x"), "close", false)) {
					skill_id[0] = skill_nome[0] = '\0';
				}
			}
		}
		CLAY(CLAY_ID("ap-entrada"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 8, 8 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(28), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			if (eu_botao_icone(CLAY_ID("ap-skills"), "sparkle", vista == V_SKILLS)) {
				vista = V_SKILLS;
				carregar_skills();
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				if ((gv_campo(CLAY_ID("ap-campo"), campo, sizeof(campo), ouvindo ? "Ouvindo…" : "Pergunte ou peça algo à Emma…", GV_CAMPO_TEXTO) & GV_CAMPO_ENTER) && campo[0]) {
					enviar(campo);
					campo[0] = '\0';
				}
			}
			if (eu_botao_icone(CLAY_ID("ap-mic"), "mic", ouvindo)) {
				pedir("ouvir");
			}
			Clay_ElementId env = CLAY_ID("ap-enviar");
			bool t = gv_tocado(env);
			CLAY(env, { .layout = { .sizing = { CLAY_SIZING_FIXED(44), CLAY_SIZING_FIXED(44) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(env) ? gv_alfa(EU_VIOLETA, 0.85f) : EU_VIOLETA, .cornerRadius = CLAY_CORNER_RADIUS(22) }) {
				t |= gv_focavel(env, 22);
				gv_icone("send", 20, (Clay_Color){ 255, 255, 255, 255 });
			}
			if (t && campo[0]) {
				enviar(campo);
				campo[0] = '\0';
			}
		}
	}
}

static void vista_chat(void) {
	bool vazio = !msgs->len && !conf_id[0];
	if (vazio) {
		apresentacao();
	} else {
		conversa();
	}
	compositor_de_mensagem();
}

static void cartao_lista(Clay_ElementId id, uint32_t k, const char *icone, const char *nome, const char *desc, bool *acao, const char *rotulo_acao, enum gv_estilo_botao estilo) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_icone(icone, 28, gv_t.acento);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			gv_texto(gv_str(nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			if (desc[0]) {
				gv_texto(gv_str(desc), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		if (rotulo_acao && gv_botao(CLAY_IDI("ap-cl-b", k), NULL, rotulo_acao, estilo)) {
			*acao = true;
		}
	}
}

static void vista_memoria(void) {
	static const char *tipos[] = { "Preferência", "Decisão", "Fato" };
	Clay_ElementId lista = CLAY_ID("ap-mem-l");
	CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 10, .padding = { 24, 24, 8, 24 } } })) {
		if (rotina_atual < 0) {
			gv_texto(CLAY_STRING("A memória fica dentro de cada rotina."), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Escolha uma rotina na barra à esquerda (ou crie uma em Gerenciar rotinas) para ver e guardar o que a Emma deve lembrar."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		} else {
			gv_texto(gv_fmt("O que a Emma lembra em \"%s\"", g_array_index(rotinas, struct rotina, rotina_atual).nome), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			CLAY(CLAY_ID("ap-mem-novo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY(CLAY_ID("ap-mem-tipo-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
					gv_lista_suspensa(CLAY_ID("ap-mem-tipo"), tipos, 3, &tipo_memoria);
				}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					if ((gv_campo(CLAY_ID("ap-mem-campo"), nova_memoria, sizeof(nova_memoria), "Algo para lembrar…", GV_CAMPO_TEXTO) & GV_CAMPO_ENTER) && nova_memoria[0]) {
						adicionar_memoria();
					}
				}
				if (gv_botao(CLAY_ID("ap-mem-add"), "plus-circle", "Guardar", GV_BOTAO_CHEIO) && nova_memoria[0]) {
					adicionar_memoria();
				}
			}
			if (!memorias->len) {
				gv_texto(CLAY_STRING("Ainda não há nada guardado aqui."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			char apagar[40] = "";
			for (guint i = 0; i < memorias->len; i++) {
				const struct memoria *m = &g_array_index(memorias, struct memoria, i);
				bool esquecer = false;
				cartao_lista(CLAY_IDI("ap-mem", i), i, !strcmp(m->tipo, "preference") ? "heart" : !strcmp(m->tipo, "decision") ? "check" : "info", m->texto,
					!strcmp(m->tipo, "preference") ? "Preferência" : !strcmp(m->tipo, "decision") ? "Decisão" : "Fato", &esquecer, "Esquecer", GV_BOTAO_SIMPLES);
				if (esquecer) {
					g_strlcpy(apagar, m->id, sizeof(apagar));
				}
			}
			if (apagar[0]) {
				esquecer_memoria(apagar);
			}
		}
	}
}

static void vista_skills(void) {
	Clay_ElementId lista = CLAY_ID("ap-sk-l");
	CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 10, .padding = { 24, 24, 8, 24 } } })) {
		gv_texto(CLAY_STRING("Receitas prontas: escolha uma e a Emma segue o jeito dela na próxima mensagem."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (!skills->len) {
			gv_texto(CLAY_STRING("Nenhuma skill encontrada."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		for (guint i = 0; i < skills->len; i++) {
			const struct skill *s = &g_array_index(skills, struct skill, i);
			bool usar = false;
			cartao_lista(CLAY_IDI("ap-sk", i), i, "sparkle", s->nome, s->descricao, &usar, skill_id[0] && !strcmp(skill_id, s->id) ? "Escolhida" : "Usar", GV_BOTAO_ELEVADO);
			if (usar) {
				g_strlcpy(skill_id, s->id, sizeof(skill_id));
				g_strlcpy(skill_nome, s->nome, sizeof(skill_nome));
				vista = V_CHAT;
				gv_focar(CLAY_ID("ap-campo"));
			}
		}
	}
}

static void vista_rotinas(void) {
	Clay_ElementId lista = CLAY_ID("ap-ro-l");
	CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 10, .padding = { 24, 24, 8, 24 } } })) {
		gv_texto(CLAY_STRING("Uma rotina junta conversas, memórias e (em breve) as macros e automações de um assunto."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		CLAY(CLAY_ID("ap-ro-novo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("ap-ro-n-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(240) } } }) {
				gv_campo(CLAY_ID("ap-ro-nome"), novo_nome, sizeof(novo_nome), "Nome da rotina", GV_CAMPO_TEXTO);
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_campo(CLAY_ID("ap-ro-desc"), novo_desc, sizeof(novo_desc), "Para que serve (opcional)", GV_CAMPO_TEXTO);
			}
			if (gv_botao(CLAY_ID("ap-ro-add"), "plus-circle", "Criar", GV_BOTAO_CHEIO) && novo_nome[0]) {
				criar_rotina();
			}
		}
		char apagar[40] = "";
		for (guint i = 0; i < rotinas->len; i++) {
			const struct rotina *r = &g_array_index(rotinas, struct rotina, i);
			bool abrir = false, tirar = false;
			bool confirmando = !strcmp(rotina_excluir, r->id);
			CLAY(CLAY_IDI("ap-ro", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
				gv_icone("folder", 28, gv_t.acento);
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
					gv_texto(gv_str(r->nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
					if (r->descricao[0]) {
						gv_texto(gv_str(r->descricao), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
				}
				abrir = gv_botao(CLAY_IDI("ap-ro-ab", i), NULL, "Abrir", GV_BOTAO_ELEVADO);
				if (confirmando) {
					if (gv_botao(CLAY_IDI("ap-ro-no", i), NULL, "Não", GV_BOTAO_SIMPLES)) {
						rotina_excluir[0] = '\0';
					}
					tirar = gv_botao(CLAY_IDI("ap-ro-ok", i), "trash", "Excluir mesmo", GV_BOTAO_PERIGO);
				} else if (gv_botao(CLAY_IDI("ap-ro-ex", i), "trash", NULL, GV_BOTAO_SIMPLES)) {
					g_strlcpy(rotina_excluir, r->id, sizeof(rotina_excluir));
				}
			}
			if (abrir) {
				escolher_rotina((int)i);
			}
			if (tirar) {
				g_strlcpy(apagar, r->id, sizeof(apagar));
			}
		}
		if (apagar[0]) {
			rotina_excluir[0] = '\0';
			excluir_rotina(apagar);
		}
		if (erro[0]) {
			gv_texto(gv_str(erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
		}
	}
}

static void desenhar(gv_app *a, void *dados) {
	(void)dados;
	if (emma_fd < 0) {
		emma_fd = emmac_assinar();
		if (emma_fd >= 0) {
			gv_app_vigiar(a, emma_fd, emma_mudou, NULL);
			g_strlcpy(estado, "ocioso", sizeof(estado));
		} else {
			gv_acordar_em(3);
		}
	}
	CLAY(CLAY_ID("ap-janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("ap-barra"), "sparkle", "Emma");
		CLAY(CLAY_ID("ap-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			barra_lateral();
			bool hero = vista == V_CHAT && !msgs->len && !conf_id[0] && dir_fundo;
			Clay_ElementDeclaration miolo = { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } };
			CLAY(CLAY_ID("ap-miolo"), hero ? gv_imagem(CLAY_ID("ap-miolo"), dir_fundo, GV_AJUSTE_COBRIR, miolo) : miolo) {
				cabecalho();
				if (excluindo) {
					faixa_excluir();
				}
				if (vista == V_CHAT) {
					vista_chat();
				} else if (vista == V_MEMORIA) {
					vista_memoria();
				} else if (vista == V_SKILLS) {
					vista_skills();
				} else {
					vista_rotinas();
				}
			}
		}
	}
}

int main(void) {
	msgs = g_ptr_array_new_with_free_func(eu_msg_liberar);
	sessoes = g_array_new(FALSE, TRUE, sizeof(struct sessao));
	rotinas = g_array_new(FALSE, TRUE, sizeof(struct rotina));
	memorias = g_array_new(FALSE, TRUE, sizeof(struct memoria));
	skills = g_array_new(FALSE, TRUE, sizeof(struct skill));
	eu_iniciar();
	dir_fundo = eu_achar_asset("fundo.jpg");
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Emma", .app_id = "lumna-emma-app", .largura = 1180, .altura = 760 });
	if (!app) {
		return 1;
	}
	carregar_rotinas();
	carregar_sessoes();
	gv_focar(CLAY_ID("ap-campo"));
	return gv_app_correr(app, desenhar, NULL);
}
