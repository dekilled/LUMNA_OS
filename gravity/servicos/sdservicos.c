#define _GNU_SOURCE
#include "sdservicos.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* roda `timeout 15 systemctl [--user] ARGS...` e devolve a saída (NULL se falhou; erro recebe o motivo) */
static char *rodar(bool usuario, char **args, char *erro, int tam_erro) {
	GPtrArray *a = g_ptr_array_new();
	g_ptr_array_add(a, (gpointer) "timeout");
	g_ptr_array_add(a, (gpointer) "15");
	g_ptr_array_add(a, (gpointer) "systemctl");
	if (usuario) {
		g_ptr_array_add(a, (gpointer) "--user");
	}
	g_ptr_array_add(a, (gpointer) "--no-pager");
	g_ptr_array_add(a, (gpointer) "--plain");
	for (char **p = args; *p; p++) {
		g_ptr_array_add(a, *p);
	}
	g_ptr_array_add(a, NULL);
	char *saida = NULL, *err = NULL;
	int estado = 0;
	GError *ge = NULL;
	bool ok = g_spawn_sync(NULL, (char **)a->pdata, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &saida, &err, &estado, &ge);
	g_ptr_array_free(a, TRUE);
	if (!ok) {
		if (erro && tam_erro > 0) {
			g_strlcpy(erro, ge ? ge->message : "não consegui rodar o systemctl", (gsize)tam_erro);
		}
		g_clear_error(&ge);
		g_free(saida);
		g_free(err);
		return NULL;
	}
	int codigo = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
	if (codigo == 124 || codigo == 127 || (codigo != 0 && (!saida || !*saida))) {
		if (erro && tam_erro > 0) {
			if (codigo == 124) {
				g_strlcpy(erro, "o systemd demorou demais para responder", (gsize)tam_erro);
			} else if (codigo == 127) {
				g_strlcpy(erro, "o systemctl não está instalado", (gsize)tam_erro);
			} else {
				char *e = err ? g_strstrip(err) : NULL;
				g_strlcpy(erro, e && *e ? e : "o systemd não respondeu", (gsize)tam_erro);
			}
		}
		g_free(saida);
		g_free(err);
		return NULL;
	}
	g_free(err);
	return saida;
}

static struct sv_item *novo(struct sv_lista *l, int *cap) {
	if (l->n >= *cap) {
		*cap = *cap ? *cap * 2 : 128;
		l->v = g_realloc(l->v, sizeof(struct sv_item) * (gsize)*cap);
	}
	struct sv_item *s = &l->v[l->n++];
	memset(s, 0, sizeof(*s));
	s->memoria = -1;
	return s;
}

static struct sv_item *achar(struct sv_lista *l, const char *unidade) {
	for (int i = 0; i < l->n; i++) {
		if (!strcmp(l->v[i].unidade, unidade)) {
			return &l->v[i];
		}
	}
	return NULL;
}

/* UNIT LOAD ACTIVE SUB DESCRIPTION (com --plain --no-legend não há marcador na frente; a descrição pode ter espaços) */
static void ler_unidades(struct sv_lista *l, int *cap, const char *texto, bool timer) {
	char **linhas = g_strsplit(texto, "\n", -1);
	for (char **ln = linhas; *ln; ln++) {
		char *p = *ln;
		while (*p == ' ' || *p == '\t' || (unsigned char)*p == 0xe2) { /* o "●" do systemd (UTF-8 e2 97 8f), se vier */
			if ((unsigned char)*p == 0xe2 && p[1] && p[2]) {
				p += 3;
			} else {
				p++;
			}
		}
		char unidade[112], carga[24], ativo[24], sub[32];
		int n = 0;
		if (sscanf(p, "%111s %23s %23s %31s %n", unidade, carga, ativo, sub, &n) < 4) {
			continue;
		}
		if (!strcmp(carga, "not-found") || !strcmp(carga, "masked")) {
			continue; /* sem arquivo de unidade: nada para mostrar */
		}
		size_t lu = strlen(unidade);
		if (lu < 6 || strcmp(unidade + lu - (timer ? 6 : 8), timer ? ".timer" : ".service")) {
			continue;
		}
		struct sv_item *s = novo(l, cap);
		g_strlcpy(s->unidade, unidade, sizeof(s->unidade));
		g_strlcpy(s->ativo, ativo, sizeof(s->ativo));
		g_strlcpy(s->sub, sub, sizeof(s->sub));
		if (n > 0) {
			g_strlcpy(s->descricao, p + n, sizeof(s->descricao));
			g_strstrip(s->descricao);
		}
		s->falhou = !strcmp(ativo, "failed");
		if (timer) {
			s->estado = !strcmp(ativo, "active") ? SV_PROGRAMADO : SV_PARADO;
			g_strlcpy(s->ativa, unidade, sizeof(s->ativa));
			char *ponto = strrchr(s->ativa, '.');
			if (ponto) {
				strcpy(ponto, ".service");
			}
		} else if (!strcmp(ativo, "active") || !strcmp(ativo, "activating") || !strcmp(ativo, "reloading")) {
			s->estado = SV_EXECUCAO;
		} else {
			s->estado = SV_PARADO;
		}
	}
	g_strfreev(linhas);
}

static int64_t numero(const char *v) {
	if (!v || !*v || !strcmp(v, "[not set]") || !strcmp(v, "infinity") || !strcmp(v, "n/a")) {
		return -1;
	}
	if (*v == '@') {
		v++;
	}
	char *fim;
	unsigned long long x = strtoull(v, &fim, 10);
	if (fim == v || x >= 0x7ffffffffffffff0ULL) {
		return -1; /* 18446744073709551615 é o "não medido" do systemd */
	}
	return (int64_t)x;
}

/* `systemctl show` de várias unidades: blocos KEY=VALUE separados por linha em branco */
static void ler_propriedades(bool usuario, struct sv_lista *l) {
	if (l->n == 0) {
		return;
	}
	GPtrArray *a = g_ptr_array_new();
	g_ptr_array_add(a, (gpointer) "show");
	g_ptr_array_add(a, (gpointer) "--timestamp=unix");
	g_ptr_array_add(a, (gpointer) "-p");
	g_ptr_array_add(a, (gpointer) "Id,MainPID,MemoryCurrent,FreezerState,NextElapseUSecRealtime,LastTriggerUSec");
	for (int i = 0; i < l->n; i++) {
		g_ptr_array_add(a, l->v[i].unidade);
	}
	g_ptr_array_add(a, NULL);
	char *saida = rodar(usuario, (char **)a->pdata, NULL, 0);
	g_ptr_array_free(a, TRUE);
	if (!saida) {
		return;
	}
	char **linhas = g_strsplit(saida, "\n", -1);
	struct sv_item *atual = NULL;
	for (char **ln = linhas; *ln; ln++) {
		char *igual = strchr(*ln, '=');
		if (!igual) {
			continue;
		}
		*igual = '\0';
		const char *k = *ln, *v = igual + 1;
		if (!strcmp(k, "Id")) {
			atual = achar(l, v);
		} else if (!atual) {
			continue;
		} else if (!strcmp(k, "MainPID")) {
			atual->pid = (int)numero(v) > 0 ? (int)numero(v) : 0;
		} else if (!strcmp(k, "MemoryCurrent")) {
			atual->memoria = numero(v);
		} else if (!strcmp(k, "FreezerState")) {
			if (!strcmp(v, "frozen") || !strcmp(v, "freezing")) {
				atual->estado = SV_PAUSA;
			}
		} else if (!strcmp(k, "NextElapseUSecRealtime")) {
			int64_t t = numero(v);
			atual->proximo = t > 0 ? t : 0;
		} else if (!strcmp(k, "LastTriggerUSec")) {
			int64_t t = numero(v);
			atual->ultimo = t > 0 ? t : 0;
		}
	}
	g_strfreev(linhas);
	g_free(saida);
}

static int por_nome(const void *a, const void *b) {
	return g_ascii_strcasecmp(((const struct sv_item *)a)->unidade, ((const struct sv_item *)b)->unidade);
}

int sv_listar(bool usuario, struct sv_lista *l, char *erro, int tam_erro) {
	l->v = NULL;
	l->n = 0;
	int cap = 0;
	char *servicos_args[] = { "list-units", "--type=service", "--all", "--no-legend", NULL };
	char *timers_args[] = { "list-units", "--type=timer", "--all", "--no-legend", NULL };
	char *s = rodar(usuario, servicos_args, erro, tam_erro);
	if (!s) {
		return -1;
	}
	ler_unidades(l, &cap, s, false);
	g_free(s);
	char *t = rodar(usuario, timers_args, NULL, 0);
	if (t) {
		ler_unidades(l, &cap, t, true);
		g_free(t);
	}
	ler_propriedades(usuario, l);
	qsort(l->v, (size_t)l->n, sizeof(struct sv_item), por_nome);
	return 0;
}

void sv_liberar(struct sv_lista *l) {
	g_free(l->v);
	l->v = NULL;
	l->n = 0;
}

const char *sv_estado_nome(enum sv_estado e) {
	static const char *const n[SV_ESTADOS] = { "Em execução", "Parados", "Programados", "Em pausa" };
	return e >= 0 && e < SV_ESTADOS ? n[e] : "?";
}

const char *sv_sub_texto(const struct sv_item *s) {
	if (s->falhou) {
		return "falhou";
	}
	if (s->estado == SV_PAUSA) {
		return "em pausa";
	}
	static const struct { const char *sub, *texto; } t[] = { { "running", "rodando" }, { "exited", "concluído" }, { "dead", "parado" }, { "waiting", "esperando" }, { "listening", "escutando" },
		{ "start", "iniciando" }, { "start-pre", "iniciando" }, { "start-post", "iniciando" }, { "stop", "parando" }, { "stop-sigterm", "parando" }, { "reload", "recarregando" },
		{ "auto-restart", "reiniciando" }, { "activating", "iniciando" }, { "elapsed", "disparou" }, { "failed", "falhou" }, { NULL, NULL } };
	for (int i = 0; t[i].sub; i++) {
		if (!strcmp(s->sub, t[i].sub)) {
			return t[i].texto;
		}
	}
	return s->sub;
}
