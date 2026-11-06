/*
 * Painel > Emma > Privacidade e Automações. Tudo isto mora no servidor da Emma (na sua máquina), então o Painel só pergunta e pede
 * pelo serviço lumna-emma (emmar_chamar):
 *   Privacidade: quanta coisa ela guarda (conversas do Chat, conversas da popup, memórias dos projetos) e apagar cada uma.
 *   Automações: os lembretes que ela agenda quando você pede ("me lembra amanhã às 9..."); aqui se vê e se cancela.
 */
#include "painel.h"

#include <glib.h>
#include <string.h>

#include "emmar.h"
#include "json.h"

/* ---- Privacidade ---- */

struct dados {
	bool lido, servidor;
	int chat, popup, memorias, projetos;
};

static struct dados dados;
static bool priv_ocupado, priv_ler_ainda;
static char priv_aviso[200];

struct priv_job {
	int acao; /* 0 ler, 1 apagar as do Chat, 2 apagar as da popup, 3 apagar as memórias */
	struct dados d;
	char aviso[200];
};

static int tamanho_lista(const char *caminho, bool *servico) {
	struct emmar r;
	int n = -1;
	if (emmar_chamar("GET", caminho, NULL, 0, &r)) {
		n = r.corpo ? r.corpo->nfilhos : 0;
	}
	*servico = *servico && r.servico;
	emmar_liberar(&r);
	return n;
}

/* apaga cada item de uma lista com DELETE prefixo/<id>; devolve quantos foram */
static int apagar_lista(const char *lista, const char *prefixo) {
	struct emmar r;
	int feitos = 0;
	if (!emmar_chamar("GET", lista, NULL, 0, &r) || !r.corpo) {
		emmar_liberar(&r);
		return 0;
	}
	GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; i < r.corpo->nfilhos; i++) {
		const char *id = json_chave_texto(json_item(r.corpo, i), "id");
		if (id && *id) {
			g_ptr_array_add(ids, g_strdup(id));
		}
	}
	emmar_liberar(&r);
	for (guint i = 0; i < ids->len; i++) {
		char *c = g_strdup_printf("%s/%s", prefixo, (char *)ids->pdata[i]);
		struct emmar d;
		if (emmar_chamar("DELETE", c, NULL, 0, &d)) {
			feitos++;
		}
		emmar_liberar(&d);
		g_free(c);
	}
	g_ptr_array_free(ids, TRUE);
	return feitos;
}

static void priv_ler(struct dados *d) {
	bool serv = true;
	d->chat = tamanho_lista("/api/sessions", &serv);
	d->popup = tamanho_lista("/api/sessions?origin=assistente", &serv);
	d->memorias = d->projetos = 0;
	struct emmar r;
	if (emmar_chamar("GET", "/api/projects", NULL, 0, &r) && r.corpo) {
		d->projetos = r.corpo->nfilhos;
		for (int i = 0; i < r.corpo->nfilhos; i++) {
			char *c = g_strdup_printf("/api/projects/%s/memories", json_chave_texto(json_item(r.corpo, i), "id"));
			int n = tamanho_lista(c, &serv);
			d->memorias += n > 0 ? n : 0;
			g_free(c);
		}
	}
	emmar_liberar(&r);
	d->servidor = serv && d->chat >= 0;
	d->lido = true;
}

static void priv_trabalho(void *p) {
	struct priv_job *j = p;
	if (j->acao == 1) {
		int n = apagar_lista("/api/sessions", "/api/sessions");
		g_snprintf(j->aviso, sizeof(j->aviso), "%d conversa(s) do Chat apagada(s).", n);
	} else if (j->acao == 2) {
		int n = apagar_lista("/api/sessions?origin=assistente", "/api/sessions");
		g_snprintf(j->aviso, sizeof(j->aviso), "%d conversa(s) do assistente apagada(s).", n);
	} else if (j->acao == 3) {
		struct emmar r;
		int n = 0;
		if (emmar_chamar("GET", "/api/projects", NULL, 0, &r) && r.corpo) {
			for (int i = 0; i < r.corpo->nfilhos; i++) {
				char *c = g_strdup_printf("/api/projects/%s/memories", json_chave_texto(json_item(r.corpo, i), "id"));
				n += apagar_lista(c, "/api/memories");
				g_free(c);
			}
		}
		emmar_liberar(&r);
		g_snprintf(j->aviso, sizeof(j->aviso), "%d memória(s) apagada(s).", n);
	}
	priv_ler(&j->d);
}

static void priv_pronto(void *p) {
	struct priv_job *j = p;
	dados = j->d;
	priv_ocupado = false;
	if (j->acao) {
		g_strlcpy(priv_aviso, j->aviso, sizeof(priv_aviso));
	}
	g_free(j);
}

static void priv_pedir(int acao) {
	if (priv_ocupado) {
		return;
	}
	priv_ocupado = true;
	struct priv_job *j = g_new0(struct priv_job, 1);
	j->acao = acao;
	pn_tarefa(priv_trabalho, priv_pronto, j);
}

void pn_emma_priv_abrir(void) {
	dados = (struct dados){ 0 };
	priv_aviso[0] = '\0';
	priv_ler_ainda = true; /* a leitura sai no primeiro desenho: a janela já existe para vigiar o fim da tarefa */
}

static void apagar_linha(const char *icone, const char *nome, int n, int acao) {
	pn_linha_comecar(CLAY_IDI("ep-lin", (uint32_t)acao), icone, nome, n > 0 ? gv_fmt("%d guardada(s)", n).chars : "Nada guardado");
	if (n > 0 && !priv_ocupado && pn_confirmar(CLAY_IDI("ep-ap", (uint32_t)acao), "trash", "Apagar tudo", "Apagar de vez?", true)) {
		priv_pedir(acao);
	}
	pn_linha_acabar();
}

void pn_emma_priv_desenhar(void) {
	if (priv_ler_ainda && !priv_ocupado) {
		priv_ler_ainda = false;
		priv_pedir(0);
	}
	CLAY(CLAY_ID("ep-raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
		CLAY(CLAY_ID("ep-nota"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 0, 10 } } }) {
			gv_texto(CLAY_STRING("A Emma guarda as conversas e as memórias no servidor dela, na sua máquina. O que você escreve numa conversa vai ao provedor "
					     "de IA que você escolheu para ela responder; segredos (senhas, chaves) ela nunca recebe."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (!dados.lido) {
			gv_texto(CLAY_STRING("Consultando…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		} else if (!dados.servidor) {
			gv_texto(CLAY_STRING("O servidor da Emma não está respondendo."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		} else {
			pn_subtitulo(CLAY_ID("ep-s0"), "O que ela guarda");
			apagar_linha("chat", "Conversas do Chat", dados.chat, 1);
			apagar_linha("mic", "Conversas do assistente (a popup rápida)", dados.popup, 2);
			apagar_linha("sparkle", "Memórias dos projetos", dados.memorias, 3);
		}
		if (priv_aviso[0]) {
			CLAY(CLAY_ID("ep-aviso"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 10, 4 } } }) {
				gv_texto(gv_str(priv_aviso), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_ID("ep-nota2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 10, 4 } } }) {
			gv_texto(CLAY_STRING("As conversas dentro de cada projeto ficam no próprio projeto, no app da Emma. Apagar é de vez: não dá para desfazer."),
				gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
}

/* ---- Automações (lembretes) ---- */

#define MAX_LEMB 80

struct lembrete {
	char id[40], texto[200], quando[48], estado[12];
};

static struct {
	bool lido, servidor;
	int n;
	struct lembrete l[MAX_LEMB];
} lemb;
static bool lemb_ocupado;
static double lemb_proxima;
static char lemb_aviso[200];

struct lemb_job {
	char cancelar[40];
	bool servidor;
	int n;
	struct lembrete l[MAX_LEMB];
	char aviso[200];
};

static void lemb_trabalho(void *p) {
	struct lemb_job *j = p;
	struct emmar r;
	if (j->cancelar[0]) {
		char *c = g_strdup_printf("/api/reminders/%s", j->cancelar);
		bool ok = emmar_chamar("DELETE", c, NULL, 0, &r);
		g_snprintf(j->aviso, sizeof(j->aviso), "%s", ok ? "Lembrete cancelado." : r.msg);
		emmar_liberar(&r);
		g_free(c);
	}
	j->servidor = emmar_chamar("GET", "/api/reminders", NULL, 0, &r) && r.ok;
	const struct json *itens = j->servidor ? json_chave(r.corpo, "items") : NULL;
	for (int i = 0; itens && i < itens->nfilhos && j->n < MAX_LEMB; i++) {
		const struct json *it = json_item(itens, i);
		struct lembrete *l = &j->l[j->n++];
		g_strlcpy(l->id, json_chave_texto(it, "id"), sizeof(l->id));
		g_strlcpy(l->texto, json_chave_texto(it, "text"), sizeof(l->texto));
		g_strlcpy(l->quando, json_chave_texto(it, "label"), sizeof(l->quando));
		g_strlcpy(l->estado, json_chave_texto(it, "status"), sizeof(l->estado));
	}
	emmar_liberar(&r);
}

static void lemb_pronto(void *p) {
	struct lemb_job *j = p;
	lemb.lido = true;
	lemb.servidor = j->servidor;
	lemb.n = j->n;
	memcpy(lemb.l, j->l, sizeof(struct lembrete) * (size_t)j->n);
	if (j->aviso[0]) {
		g_strlcpy(lemb_aviso, j->aviso, sizeof(lemb_aviso));
	}
	lemb_ocupado = false;
	g_free(j);
}

static void lemb_pedir(const char *cancelar) {
	if (lemb_ocupado) {
		return;
	}
	lemb_ocupado = true;
	struct lemb_job *j = g_new0(struct lemb_job, 1);
	if (cancelar) {
		g_strlcpy(j->cancelar, cancelar, sizeof(j->cancelar));
	}
	pn_tarefa(lemb_trabalho, lemb_pronto, j);
}

void pn_emma_auto_abrir(void) {
	lemb.lido = false;
	lemb_aviso[0] = '\0';
	lemb_proxima = 0;
}

void pn_emma_auto_desenhar(void) {
	double agora = gv_agora();
	if (!lemb_ocupado && (!lemb.lido || agora >= lemb_proxima)) {
		lemb_proxima = agora + 5.0; /* aberta, acompanha: ela cria lembretes pelo chat e pelo celular */
		lemb_pedir(NULL);
	}
	gv_acordar_em(5.0);
	CLAY(CLAY_ID("ea-raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
		CLAY(CLAY_ID("ea-nota"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 0, 10 } } }) {
			gv_texto(CLAY_STRING("A Emma agenda sozinha o que você pede: “me lembra amanhã às 9 de ligar pro dentista”. Os lembretes aparecem aqui; no horário, ela avisa."),
				gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		int pendentes = 0;
		for (int i = 0; i < lemb.n; i++) {
			pendentes += !strcmp(lemb.l[i].estado, "pending");
		}
		if (!lemb.lido) {
			gv_texto(CLAY_STRING("Consultando…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		} else if (!lemb.servidor) {
			gv_texto(CLAY_STRING("O servidor da Emma não está respondendo."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		} else {
			pn_subtitulo(CLAY_ID("ea-s0"), "Agendados");
			if (pendentes == 0) {
				CLAY(CLAY_ID("ea-vazio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 6, 6 } } }) {
					gv_texto(CLAY_STRING("Nenhum lembrete agendado."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			for (int i = 0; i < lemb.n; i++) {
				if (!strcmp(lemb.l[i].estado, "pending")) {
					pn_linha_comecar(CLAY_IDI("ea-l", (uint32_t)i), "bell", lemb.l[i].texto, lemb.l[i].quando);
					if (!lemb_ocupado && pn_confirmar(CLAY_IDI("ea-x", (uint32_t)i), "trash", "Cancelar", "Cancelar o lembrete?", true)) {
						lemb_pedir(lemb.l[i].id);
					}
					pn_linha_acabar();
				}
			}
			if (pendentes < lemb.n) {
				pn_subtitulo(CLAY_ID("ea-s1"), "Já passaram ou foram cancelados");
				for (int i = 0; i < lemb.n; i++) {
					if (strcmp(lemb.l[i].estado, "pending")) {
						pn_linha_comecar(CLAY_IDI("ea-p", (uint32_t)i), "check", lemb.l[i].texto,
							gv_fmt("%s · %s", lemb.l[i].quando, !strcmp(lemb.l[i].estado, "cancelled") ? "cancelado" : "feito").chars);
						pn_linha_acabar();
					}
				}
			}
		}
		if (lemb_aviso[0]) {
			CLAY(CLAY_ID("ea-aviso"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 10, 4 } } }) {
				gv_texto(gv_str(lemb_aviso), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
}
