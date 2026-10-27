#include "emmas.h"

#include <glib/gstdio.h>
#include <string.h>

#include "json.h"

static GMutex trinco;

/* ---- caminhos e arquivos ---- */

static char *pasta(void) {
	char *p = g_build_filename(g_get_user_data_dir(), "lumna", "emma", NULL);
	return p;
}

static char *arq_sessao(const char *sid) {
	char *d = pasta(), *r = NULL;
	/* o id só tem letras e números: nada de ../ vindo de fora */
	for (const char *c = sid; *c; c++) {
		if (!g_ascii_isalnum(*c)) {
			g_free(d);
			return NULL;
		}
	}
	r = g_strdup_printf("%s/sessoes/%s.json", d, sid);
	g_free(d);
	return r;
}

static bool gravar(const char *caminho, const char *conteudo) {
	char *dir = g_path_get_dirname(caminho);
	g_mkdir_with_parents(dir, 0700);
	g_free(dir);
	char *tmp = g_strconcat(caminho, ".tmp", NULL);
	bool ok = g_file_set_contents(tmp, conteudo, -1, NULL);
	if (ok) {
		g_chmod(tmp, 0600);
		ok = g_rename(tmp, caminho) == 0;
	}
	g_free(tmp);
	return ok;
}

static char *agora_iso(void) {
	GDateTime *t = g_date_time_new_now_utc();
	char *s = g_date_time_format(t, "%Y-%m-%dT%H:%M:%S+00:00");
	g_date_time_unref(t);
	return s;
}

static char *novo_id(void) {
	return g_strdup_printf("%08x%08x", g_random_int(), g_random_int());
}

/* ---- as conversas ---- */

struct sessao {
	char *id, *titulo, *projeto, *criada, *atualizada;
	bool oculta;
	GString *msgs; /* os itens, já em JSON, separados por vírgula */
};

static void sessao_liberar(struct sessao *s) {
	g_free(s->id);
	g_free(s->titulo);
	g_free(s->projeto);
	g_free(s->criada);
	g_free(s->atualizada);
	if (s->msgs) {
		g_string_free(s->msgs, TRUE);
	}
}

static bool sessao_ler(const char *sid, struct sessao *s) {
	memset(s, 0, sizeof(*s));
	char *caminho = arq_sessao(sid), *txt = NULL;
	if (!caminho || !g_file_get_contents(caminho, &txt, NULL, NULL)) {
		g_free(caminho);
		return false;
	}
	g_free(caminho);
	struct json *j = json_ler(txt);
	g_free(txt);
	if (!j) {
		return false;
	}
	s->id = g_strdup(json_chave_texto(j, "id"));
	s->titulo = g_strdup(json_chave_texto(j, "title"));
	const char *p = json_texto(json_chave(j, "project_id"));
	s->projeto = p && *p ? g_strdup(p) : NULL;
	s->criada = g_strdup(json_chave_texto(j, "created_at"));
	s->atualizada = g_strdup(json_chave_texto(j, "updated_at"));
	s->oculta = json_bool(json_chave(j, "oculta"), false);
	s->msgs = g_string_new(NULL);
	const struct json *ms = json_chave(j, "messages");
	for (int i = 0; ms && i < ms->nfilhos; i++) {
		if (i) {
			g_string_append_c(s->msgs, ',');
		}
		json_escrever(s->msgs, json_item(ms, i));
	}
	json_liberar(j);
	return true;
}

static bool sessao_gravar(const struct sessao *s) {
	GString *g = g_string_new("{\"id\":");
	json_escrever_texto(g, s->id);
	g_string_append(g, ",\"title\":");
	json_escrever_texto(g, s->titulo);
	g_string_append(g, ",\"project_id\":");
	if (s->projeto) {
		json_escrever_texto(g, s->projeto);
	} else {
		g_string_append(g, "null");
	}
	g_string_append(g, ",\"mode\":\"chat\",\"created_at\":");
	json_escrever_texto(g, s->criada);
	g_string_append(g, ",\"updated_at\":");
	json_escrever_texto(g, s->atualizada);
	g_string_append_printf(g, ",\"oculta\":%s,\"messages\":[%s]}", s->oculta ? "true" : "false", s->msgs ? s->msgs->str : "");
	char *caminho = arq_sessao(s->id);
	bool ok = caminho && gravar(caminho, g->str);
	g_free(caminho);
	g_string_free(g, TRUE);
	return ok;
}

static void sessao_cabecalho(GString *g, const struct sessao *s) {
	g_string_append(g, "{\"id\":");
	json_escrever_texto(g, s->id);
	g_string_append(g, ",\"title\":");
	json_escrever_texto(g, s->titulo);
	g_string_append(g, ",\"project_id\":");
	if (s->projeto) {
		json_escrever_texto(g, s->projeto);
	} else {
		g_string_append(g, "null");
	}
	g_string_append(g, ",\"mode\":\"chat\",\"created_at\":");
	json_escrever_texto(g, s->criada);
	g_string_append(g, ",\"updated_at\":");
	json_escrever_texto(g, s->atualizada);
	g_string_append_c(g, '}');
}

static bool sessao_nova(const char *titulo, const char *projeto, bool oculta, char *sid_out, size_t n) {
	struct sessao s = { 0 };
	s.id = novo_id();
	s.titulo = g_strdup(titulo && *titulo ? titulo : "Nova conversa");
	s.projeto = projeto && *projeto ? g_strdup(projeto) : NULL;
	s.criada = s.atualizada = agora_iso();
	s.atualizada = g_strdup(s.criada);
	s.oculta = oculta;
	s.msgs = g_string_new(NULL);
	bool ok = sessao_gravar(&s);
	if (ok && sid_out) {
		g_strlcpy(sid_out, s.id, n);
	}
	sessao_liberar(&s);
	return ok;
}

bool emmas_existe(const char *sid) {
	char *caminho = arq_sessao(sid);
	bool ok = caminho && g_file_test(caminho, G_FILE_TEST_EXISTS);
	g_free(caminho);
	return ok;
}

bool emmas_anexar(const char *sid, const char *role, const char *texto, const char *tools_json) {
	g_mutex_lock(&trinco);
	struct sessao s;
	bool ok = sessao_ler(sid, &s);
	if (ok) {
		char *id = novo_id(), *quando = agora_iso();
		if (s.msgs->len) {
			g_string_append_c(s.msgs, ',');
		}
		g_string_append(s.msgs, "{\"id\":");
		json_escrever_texto(s.msgs, id);
		g_string_append_printf(s.msgs, ",\"session_id\":");
		json_escrever_texto(s.msgs, sid);
		g_string_append_printf(s.msgs, ",\"role\":");
		json_escrever_texto(s.msgs, role);
		g_string_append(s.msgs, ",\"content\":");
		json_escrever_texto(s.msgs, texto);
		g_string_append(s.msgs, ",\"created_at\":");
		json_escrever_texto(s.msgs, quando);
		g_string_append_printf(s.msgs, ",\"tools\":%s}", tools_json && *tools_json ? tools_json : "[]");
		g_free(s.atualizada);
		s.atualizada = quando;
		g_free(id);
		ok = sessao_gravar(&s);
		sessao_liberar(&s);
	}
	g_mutex_unlock(&trinco);
	return ok;
}

char *emmas_historico(const char *sid, int max) {
	g_mutex_lock(&trinco);
	struct sessao s;
	GString *g = g_string_new("[");
	if (sessao_ler(sid, &s)) {
		char *txt = g_strdup_printf("[%s]", s.msgs->str);
		struct json *j = json_ler(txt);
		g_free(txt);
		int ini = j && j->nfilhos > max ? j->nfilhos - max : 0;
		bool primeiro = true;
		for (int i = ini; j && i < j->nfilhos; i++) {
			const struct json *m = json_item(j, i);
			const char *papel = json_chave_texto(m, "role");
			if (strcmp(papel, "user") && strcmp(papel, "assistant")) {
				continue;
			}
			if (*json_chave_texto(m, "marker")) {
				continue;
			}
			g_string_append_printf(g, "%s{\"role\":\"%s\",\"content\":", primeiro ? "" : ",", papel);
			json_escrever_texto(g, json_chave_texto(m, "content"));
			g_string_append_c(g, '}');
			primeiro = false;
		}
		json_liberar(j);
		sessao_liberar(&s);
	}
	g_mutex_unlock(&trinco);
	g_string_append_c(g, ']');
	return g_string_free(g, FALSE);
}

/* ---- o canal do painel flutuante ---- */

static char *arq_canais(void) {
	char *d = pasta(), *r = g_build_filename(d, "canais.json", NULL);
	g_free(d);
	return r;
}

static char *canal_ler(const char *canal) {
	char *p = arq_canais(), *txt = NULL, *r = NULL;
	if (g_file_get_contents(p, &txt, NULL, NULL)) {
		struct json *j = json_ler(txt);
		const char *s = json_texto(json_chave(j, canal));
		r = s && *s ? g_strdup(s) : NULL;
		json_liberar(j);
	}
	g_free(txt);
	g_free(p);
	return r;
}

static void canal_gravar(const char *canal, const char *sid) {
	char *p = arq_canais();
	GString *g = g_string_new("{");
	json_escrever_texto(g, canal);
	g_string_append(g, ":");
	json_escrever_texto(g, sid);
	g_string_append_c(g, '}');
	gravar(p, g->str);
	g_string_free(g, TRUE);
	g_free(p);
}

bool emmas_sessao_canal(const char *canal, char *sid, size_t n) {
	g_mutex_lock(&trinco);
	char *s = canal_ler(canal);
	bool ok = s && emmas_existe(s);
	if (ok) {
		g_strlcpy(sid, s, n);
	} else if (sessao_nova("Painel da Emma", NULL, true, sid, n)) {
		canal_gravar(canal, sid);
		ok = true;
	}
	g_free(s);
	g_mutex_unlock(&trinco);
	return ok;
}

void emmas_nova_sessao_canal(const char *canal) {
	g_mutex_lock(&trinco);
	char sid[40];
	if (sessao_nova("Painel da Emma", NULL, true, sid, sizeof(sid))) {
		canal_gravar(canal, sid);
	}
	g_mutex_unlock(&trinco);
}

/* ---- as rotinas e as memórias ---- */

static char *arq_rotinas(void) {
	char *d = pasta(), *r = g_build_filename(d, "rotinas.json", NULL);
	g_free(d);
	return r;
}

static struct json *rotinas_ler(void) {
	char *p = arq_rotinas(), *txt = NULL;
	struct json *j = NULL;
	if (g_file_get_contents(p, &txt, NULL, NULL)) {
		j = json_ler(txt);
	}
	g_free(txt);
	g_free(p);
	return j;
}

static void rotinas_gravar(const struct json *antes, const char *tira_id, const char *proj_novo_json, const char *mem_novo_json, const char *muda_id,
	const char *muda_nome, const char *muda_desc) {
	GString *g = g_string_new("{\"projetos\":[");
	bool primeiro = true;
	const struct json *ps = json_chave(antes, "projetos"), *ms = json_chave(antes, "memorias");
	for (int i = 0; ps && i < ps->nfilhos; i++) {
		const struct json *p = json_item(ps, i);
		if (tira_id && !strcmp(json_chave_texto(p, "id"), tira_id)) {
			continue;
		}
		g_string_append_printf(g, "%s", primeiro ? "" : ",");
		primeiro = false;
		if (muda_id && !strcmp(json_chave_texto(p, "id"), muda_id)) {
			g_string_append(g, "{\"id\":");
			json_escrever_texto(g, muda_id);
			g_string_append(g, ",\"name\":");
			json_escrever_texto(g, muda_nome ? muda_nome : json_chave_texto(p, "name"));
			g_string_append(g, ",\"description\":");
			json_escrever_texto(g, muda_desc ? muda_desc : json_chave_texto(p, "description"));
			g_string_append(g, ",\"created_at\":");
			json_escrever_texto(g, json_chave_texto(p, "created_at"));
			g_string_append_c(g, '}');
		} else {
			json_escrever(g, p);
		}
	}
	if (proj_novo_json) {
		g_string_append_printf(g, "%s%s", primeiro ? "" : ",", proj_novo_json);
	}
	g_string_append(g, "],\"memorias\":[");
	primeiro = true;
	for (int i = 0; ms && i < ms->nfilhos; i++) {
		const struct json *m = json_item(ms, i);
		if (tira_id && (!strcmp(json_chave_texto(m, "project_id"), tira_id) || !strcmp(json_chave_texto(m, "id"), tira_id))) {
			continue;
		}
		g_string_append_printf(g, "%s", primeiro ? "" : ",");
		primeiro = false;
		json_escrever(g, m);
	}
	if (mem_novo_json) {
		g_string_append_printf(g, "%s%s", primeiro ? "" : ",", mem_novo_json);
	}
	g_string_append(g, "]}");
	char *p = arq_rotinas();
	gravar(p, g->str);
	g_free(p);
	g_string_free(g, TRUE);
}

/* ---- as rotas ---- */

static char *erro_json(int *status, int st, const char *detalhe) {
	*status = st;
	GString *g = g_string_new("{\"detail\":");
	json_escrever_texto(g, detalhe);
	g_string_append_c(g, '}');
	return g_string_free(g, FALSE);
}

static int cmp_atualizada(gconstpointer a, gconstpointer b) {
	const struct sessao *x = *(struct sessao *const *)a, *y = *(struct sessao *const *)b;
	return strcmp(y->atualizada, x->atualizada);
}

static char *rota_sessoes(const char *metodo, const char *rest, const char *query, const char *corpo, int *status) {
	*status = 200;
	struct json *c = corpo && *corpo ? json_ler(corpo) : NULL;
	char *r = NULL;
	if (!*rest) { /* /api/sessions */
		if (!strcmp(metodo, "GET")) {
			char *d = pasta(), *dir = g_build_filename(d, "sessoes", NULL);
			GPtrArray *lista = g_ptr_array_new();
			GDir *gd = g_dir_open(dir, 0, NULL);
			const char *nome;
			while (gd && (nome = g_dir_read_name(gd))) {
				if (!g_str_has_suffix(nome, ".json")) {
					continue;
				}
				char *sid = g_strndup(nome, strlen(nome) - 5);
				struct sessao *s = g_new0(struct sessao, 1);
				if (!sessao_ler(sid, s) || s->oculta) {
					sessao_liberar(s);
					g_free(s);
				} else {
					g_ptr_array_add(lista, s);
				}
				g_free(sid);
			}
			if (gd) {
				g_dir_close(gd);
			}
			g_free(dir);
			g_free(d);
			const char *filtro = NULL;
			char *pid = NULL;
			if (query && g_str_has_prefix(query, "project_id=")) {
				pid = g_uri_unescape_string(query + 11, NULL);
				filtro = pid;
			}
			g_ptr_array_sort(lista, cmp_atualizada);
			GString *g = g_string_new("[");
			bool primeiro = true;
			for (guint i = 0; i < lista->len; i++) {
				struct sessao *s = lista->pdata[i];
				bool entra = !filtro ? !s->projeto : !strcmp(filtro, "none") ? !s->projeto : (s->projeto && !strcmp(s->projeto, filtro));
				if (entra) {
					g_string_append_printf(g, "%s", primeiro ? "" : ",");
					sessao_cabecalho(g, s);
					primeiro = false;
				}
				sessao_liberar(s);
				g_free(s);
			}
			g_ptr_array_free(lista, TRUE);
			g_string_append_c(g, ']');
			g_free(pid);
			r = g_string_free(g, FALSE);
		} else if (!strcmp(metodo, "POST")) {
			char sid[40];
			if (sessao_nova(json_chave_texto(c, "title"), json_texto(json_chave(c, "project_id")), false, sid, sizeof(sid))) {
				struct sessao s;
				sessao_ler(sid, &s);
				GString *g = g_string_new(NULL);
				sessao_cabecalho(g, &s);
				sessao_liberar(&s);
				r = g_string_free(g, FALSE);
			} else {
				r = erro_json(status, 500, "Não consegui guardar a conversa.");
			}
		}
	} else if (!strchr(rest, '/')) { /* /api/sessions/{id} */
		struct sessao s;
		if (!sessao_ler(rest, &s)) {
			r = erro_json(status, 404, "Sessão não encontrada");
		} else if (!strcmp(metodo, "PATCH")) {
			const char *t = json_chave_texto(c, "title");
			if (*t) {
				g_free(s.titulo);
				s.titulo = g_strdup(t);
				sessao_gravar(&s);
			}
			GString *g = g_string_new(NULL);
			sessao_cabecalho(g, &s);
			r = g_string_free(g, FALSE);
			sessao_liberar(&s);
		} else if (!strcmp(metodo, "DELETE")) {
			char *caminho = arq_sessao(rest);
			g_unlink(caminho);
			g_free(caminho);
			sessao_liberar(&s);
			r = g_strdup("{\"ok\":true}");
		} else {
			GString *g = g_string_new(NULL);
			sessao_cabecalho(g, &s);
			r = g_string_free(g, FALSE);
			sessao_liberar(&s);
		}
	} else if (g_str_has_suffix(rest, "/messages")) { /* /api/sessions/{id}/messages */
		char *sid = g_strndup(rest, strlen(rest) - 9);
		struct sessao s;
		if (sessao_ler(sid, &s)) {
			r = g_strdup_printf("[%s]", s.msgs->str);
			sessao_liberar(&s);
		} else {
			r = erro_json(status, 404, "Sessão não encontrada");
		}
		g_free(sid);
	}
	json_liberar(c);
	return r;
}

static char *rota_projetos(const char *metodo, const char *rest, const char *corpo, int *status) {
	*status = 200;
	struct json *c = corpo && *corpo ? json_ler(corpo) : NULL;
	struct json *rot = rotinas_ler();
	const struct json *ps = json_chave(rot, "projetos"), *ms = json_chave(rot, "memorias");
	char *r = NULL;
	if (!*rest) {
		if (!strcmp(metodo, "GET")) {
			GString *g = g_string_new("[");
			for (int i = 0; ps && i < ps->nfilhos; i++) {
				g_string_append_printf(g, "%s", i ? "," : "");
				json_escrever(g, json_item(ps, i));
			}
			g_string_append_c(g, ']');
			r = g_string_free(g, FALSE);
		} else if (!strcmp(metodo, "POST")) {
			const char *nome = json_chave_texto(c, "name");
			if (!*nome) {
				r = erro_json(status, 400, "Dê um nome à rotina.");
			} else {
				char *id = novo_id(), *quando = agora_iso();
				GString *g = g_string_new("{\"id\":");
				json_escrever_texto(g, id);
				g_string_append(g, ",\"name\":");
				json_escrever_texto(g, nome);
				g_string_append(g, ",\"description\":");
				json_escrever_texto(g, json_chave_texto(c, "description"));
				g_string_append(g, ",\"created_at\":");
				json_escrever_texto(g, quando);
				g_string_append_c(g, '}');
				rotinas_gravar(rot, NULL, g->str, NULL, NULL, NULL, NULL);
				r = g_string_free(g, FALSE);
				g_free(id);
				g_free(quando);
			}
		}
	} else if (g_str_has_suffix(rest, "/memories")) {
		char *pid = g_strndup(rest, strlen(rest) - 9);
		if (!strcmp(metodo, "GET")) {
			GString *g = g_string_new("[");
			bool primeiro = true;
			for (int i = ms ? ms->nfilhos - 1 : -1; i >= 0; i--) {
				const struct json *m = json_item(ms, i);
				if (!strcmp(json_chave_texto(m, "project_id"), pid)) {
					g_string_append_printf(g, "%s", primeiro ? "" : ",");
					json_escrever(g, m);
					primeiro = false;
				}
			}
			g_string_append_c(g, ']');
			r = g_string_free(g, FALSE);
		} else if (!strcmp(metodo, "POST")) {
			const char *tipo = json_chave_texto(c, "kind"), *texto = json_chave_texto(c, "content");
			if (!*texto || (strcmp(tipo, "preference") && strcmp(tipo, "decision") && strcmp(tipo, "fact"))) {
				r = erro_json(status, 400, "Tipo ou texto inválido.");
			} else {
				char *id = novo_id(), *quando = agora_iso();
				GString *g = g_string_new("{\"id\":");
				json_escrever_texto(g, id);
				g_string_append(g, ",\"project_id\":");
				json_escrever_texto(g, pid);
				g_string_append(g, ",\"kind\":");
				json_escrever_texto(g, tipo);
				g_string_append(g, ",\"content\":");
				json_escrever_texto(g, texto);
				g_string_append(g, ",\"created_at\":");
				json_escrever_texto(g, quando);
				g_string_append_c(g, '}');
				rotinas_gravar(rot, NULL, NULL, g->str, NULL, NULL, NULL);
				r = g_string_free(g, FALSE);
				g_free(id);
				g_free(quando);
			}
		}
		g_free(pid);
	} else if (!strchr(rest, '/')) { /* /api/projects/{id} */
		if (!strcmp(metodo, "DELETE")) {
			rotinas_gravar(rot, rest, NULL, NULL, NULL, NULL, NULL);
			/* as conversas dela voltam para o chat livre */
			char *d = pasta(), *dir = g_build_filename(d, "sessoes", NULL);
			GDir *gd = g_dir_open(dir, 0, NULL);
			const char *nome;
			while (gd && (nome = g_dir_read_name(gd))) {
				if (!g_str_has_suffix(nome, ".json")) {
					continue;
				}
				char *sid = g_strndup(nome, strlen(nome) - 5);
				struct sessao s;
				if (sessao_ler(sid, &s)) {
					if (s.projeto && !strcmp(s.projeto, rest)) {
						g_free(s.projeto);
						s.projeto = NULL;
						sessao_gravar(&s);
					}
					sessao_liberar(&s);
				}
				g_free(sid);
			}
			if (gd) {
				g_dir_close(gd);
			}
			g_free(dir);
			g_free(d);
			r = g_strdup("{\"ok\":true}");
		} else if (!strcmp(metodo, "PATCH")) {
			rotinas_gravar(rot, NULL, NULL, NULL, rest, *json_chave_texto(c, "name") ? json_chave_texto(c, "name") : NULL,
				json_chave(c, "description") ? json_chave_texto(c, "description") : NULL);
			r = g_strdup("{\"ok\":true}");
		}
	}
	json_liberar(rot);
	json_liberar(c);
	return r;
}

char *emmas_rest(const char *metodo, const char *caminho, const char *corpo, int *status) {
	*status = 200;
	char *copia = g_strdup(caminho), *query = strchr(copia, '?');
	if (query) {
		*query++ = '\0';
	}
	char *r = NULL;
	g_mutex_lock(&trinco);
	if (g_str_has_prefix(copia, "/api/sessions")) {
		const char *rest = copia + strlen("/api/sessions");
		if (*rest == '/') {
			rest++;
		} else if (*rest) {
			rest = NULL;
		}
		r = rest ? rota_sessoes(metodo, rest, query, corpo, status) : NULL;
	} else if (g_str_has_prefix(copia, "/api/projects")) {
		const char *rest = copia + strlen("/api/projects");
		if (*rest == '/') {
			rest++;
		} else if (*rest) {
			rest = NULL;
		}
		r = rest ? rota_projetos(metodo, rest, corpo, status) : NULL;
	} else if (g_str_has_prefix(copia, "/api/memories/") && !strcmp(metodo, "DELETE")) {
		struct json *rot = rotinas_ler();
		if (rot) {
			rotinas_gravar(rot, copia + strlen("/api/memories/"), NULL, NULL, NULL, NULL, NULL);
			json_liberar(rot);
		}
		r = g_strdup("{\"ok\":true}");
	} else if (!strcmp(copia, "/api/skills")) {
		r = g_strdup("[]"); /* as skills ficam para depois */
	}
	g_mutex_unlock(&trinco);
	g_free(copia);
	if (!r && *status == 200) {
		r = erro_json(status, 404, "Rota desconhecida");
	}
	return r;
}
