#include "agenda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>

#include "json.h"

#define DIA 86400
#define ATRASO_MAX 3600
#define TEXTO_MAX 2000

static const char *ACOES[] = { "poweroff", "reboot", "suspend", "lock", "remind", "emma", NULL };

void agenda_liberar(gpointer p) {
	struct agenda_tarefa *t = p;
	g_free(t->texto);
	g_free(t);
}

gint64 agenda_agora(void) {
	const char *fixa = g_getenv("LUMNA_AGORA");
	return fixa && *fixa ? g_ascii_strtoll(fixa, NULL, 10) : (gint64)time(NULL);
}

static char *caminho(void) {
	const char *cfg = g_getenv("XDG_CONFIG_HOME");
	return cfg && *cfg ? g_build_filename(cfg, "lumna", "schedule.json", NULL) : g_build_filename(g_get_home_dir(), ".config", "lumna", "schedule.json", NULL);
}

static gint por_hora(gconstpointer a, gconstpointer b) {
	const struct agenda_tarefa *x = *(struct agenda_tarefa *const *)a, *y = *(struct agenda_tarefa *const *)b;
	return x->em < y->em ? -1 : x->em > y->em;
}

static GPtrArray *carregar(void) {
	GPtrArray *v = g_ptr_array_new_with_free_func(agenda_liberar);
	char *c = caminho(), *texto = NULL;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		struct json *j = json_ler(texto);
		for (int i = 0; j && j->tipo == JSON_LISTA && i < j->nfilhos; i++) {
			const struct json *o = j->filhos[i];
			const char *acao = json_chave_texto(o, "action");
			bool conhecida = false;
			for (int a = 0; ACOES[a]; a++) {
				conhecida = conhecida || !strcmp(ACOES[a], acao);
			}
			if (!conhecida || !*json_chave_texto(o, "id")) {
				continue;
			}
			struct agenda_tarefa *t = g_new0(struct agenda_tarefa, 1);
			g_strlcpy(t->id, json_chave_texto(o, "id"), sizeof(t->id));
			t->em = (gint64)json_numero(json_chave(o, "at"), 0);
			g_strlcpy(t->acao, acao, sizeof(t->acao));
			t->texto = g_strdup(json_chave_texto(o, "text"));
			t->diaria = json_bool(json_chave(o, "daily"), false);
			g_strlcpy(t->por, json_chave_texto(o, "by"), sizeof(t->por));
			g_ptr_array_add(v, t);
		}
		json_liberar(j);
	}
	g_free(texto);
	g_free(c);
	return v;
}

static void tarefas_json(GString *s, GPtrArray *v) {
	g_string_append_c(s, '[');
	for (guint i = 0; i < v->len; i++) {
		struct agenda_tarefa *t = v->pdata[i];
		g_string_append_printf(s, "%s{\"id\":\"%s\",\"at\":%" G_GINT64_FORMAT ",\"action\":\"%s\",\"text\":", i ? "," : "", t->id, t->em, t->acao);
		json_escrever_texto(s, t->texto);
		g_string_append_printf(s, ",\"daily\":%s,\"by\":", t->diaria ? "true" : "false");
		json_escrever_texto(s, t->por);
		g_string_append_c(s, '}');
	}
	g_string_append_c(s, ']');
}

static bool guardar(GPtrArray *v) {
	char *c = caminho();
	char *dir = g_path_get_dirname(c);
	g_mkdir_with_parents(dir, 0700);
	GString *s = g_string_new(NULL);
	tarefas_json(s, v);
	/* escreve num arquivo ao lado e troca: um corte de luz no meio não deixa a lista pela metade */
	char *tmp = g_strconcat(c, ".tmp", NULL);
	bool ok = g_file_set_contents(tmp, s->str, -1, NULL) && rename(tmp, c) == 0;
	g_free(tmp);
	g_string_free(s, TRUE);
	g_free(dir);
	g_free(c);
	return ok;
}

GPtrArray *agenda_listar(void) {
	GPtrArray *v = carregar();
	g_ptr_array_sort(v, por_hora);
	return v;
}

bool agenda_adicionar(const char *acao, gint64 em, const char *texto, bool diaria, const char *por, char *erro, size_t tam) {
	bool conhecida = false;
	for (int a = 0; ACOES[a]; a++) {
		conhecida = conhecida || !strcmp(ACOES[a], acao);
	}
	if (!conhecida) {
		g_strlcpy(erro, "ação desconhecida", tam);
		return false;
	}
	bool com_texto = !strcmp(acao, "remind") || !strcmp(acao, "emma");
	char *aparado = g_strstrip(g_strdup(texto));
	bool vazio = !*aparado;
	g_free(aparado);
	if (com_texto && vazio) {
		g_strlcpy(erro, !strcmp(acao, "emma") ? "diga o que a Emma deve fazer" : "escreva o lembrete", tam);
		return false;
	}
	if (strlen(texto) > TEXTO_MAX) {
		g_strlcpy(erro, "texto longo demais", tam);
		return false;
	}
	gint64 agora = agenda_agora();
	if (em + 60 < agora) {
		if (!diaria) {
			g_strlcpy(erro, "esse horário já passou", tam);
			return false;
		}
		while (em <= agora) {
			em += DIA;
		}
	}
	struct agenda_tarefa *t = g_new0(struct agenda_tarefa, 1);
	unsigned char b[6];
	if (getrandom(b, sizeof(b), 0) != (ssize_t)sizeof(b)) {
		memset(b, 7, sizeof(b));
	}
	for (int i = 0; i < 6; i++) {
		g_snprintf(t->id + i * 2, 3, "%02x", b[i]);
	}
	t->em = em;
	g_strlcpy(t->acao, acao, sizeof(t->acao));
	t->texto = g_strdup(com_texto ? texto : "");
	t->diaria = diaria;
	g_strlcpy(t->por, por, sizeof(t->por));
	GPtrArray *v = carregar();
	g_ptr_array_add(v, t);
	bool ok = guardar(v);
	g_ptr_array_unref(v);
	if (!ok) {
		g_strlcpy(erro, "não consegui guardar o agendamento", tam);
	}
	return ok;
}

bool agenda_remover(const char *id) {
	GPtrArray *v = carregar();
	for (guint i = 0; i < v->len; i++) {
		if (!strcmp(((struct agenda_tarefa *)v->pdata[i])->id, id)) {
			g_ptr_array_remove_index(v, i);
			break;
		}
	}
	bool ok = guardar(v);
	g_ptr_array_unref(v);
	return ok;
}

GPtrArray *agenda_vencidas(void) {
	GPtrArray *devolve = g_ptr_array_new_with_free_func(agenda_liberar);
	GPtrArray *v = carregar();
	gint64 agora = agenda_agora();
	bool mudou = false;
	for (guint i = 0; i < v->len;) {
		struct agenda_tarefa *t = v->pdata[i];
		if (t->em > agora) {
			i++;
			continue;
		}
		mudou = true;
		if (agora - t->em <= ATRASO_MAX) {
			struct agenda_tarefa *c = g_memdup2(t, sizeof(*t));
			c->texto = g_strdup(t->texto);
			g_ptr_array_add(devolve, c);
		}
		if (t->diaria) {
			while (t->em <= agora) {
				t->em += DIA;
			}
			i++;
		} else {
			g_ptr_array_remove_index(v, i);
		}
	}
	if (mudou) {
		guardar(v);
	}
	g_ptr_array_unref(v);
	return devolve;
}

void agenda_json(GString *s) {
	GPtrArray *v = agenda_listar();
	g_string_append(s, "{\"type\":\"schedule\",\"tasks\":");
	tarefas_json(s, v);
	g_string_append_c(s, '}');
	g_ptr_array_unref(v);
}
