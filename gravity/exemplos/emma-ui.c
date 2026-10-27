/* ver emma-ui.h */
#define _GNU_SOURCE
#include "emma-ui.h"

#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

const Clay_Color EU_VIOLETA = { 110, 78, 232, 255 };
char *eu_avatar_caminho;

void eu_iniciar(void) {
	if (!eu_avatar_caminho) {
		eu_avatar_caminho = eu_achar_asset("avatar.jpg");
	}
}

/* ---- as imagens ---- */

char *eu_achar_asset(const char *nome) {
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

void eu_ler_resposta(const char *texto, struct eu_resposta *r) {
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

/* ---- as mensagens ---- */

void eu_msg_liberar(gpointer p) {
	struct eu_msg *m = p;
	g_free(m->texto);
	if (m->ferramentas) {
		g_array_free(m->ferramentas, TRUE);
	}
	g_free(m);
}

struct eu_msg *eu_msg_nova(GPtrArray *msgs, bool usuario, const char *texto, const char *hora) {
	struct eu_msg *m = g_new0(struct eu_msg, 1);
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
	m->ferramentas = g_array_new(FALSE, TRUE, sizeof(struct eu_ferramenta));
	g_ptr_array_add(msgs, m);
	return m;
}

struct eu_msg *eu_msg_parcial(GPtrArray *msgs) {
	if (msgs->len) {
		struct eu_msg *m = msgs->pdata[msgs->len - 1];
		if (!m->usuario && m->parcial) {
			return m;
		}
	}
	return NULL;
}

void eu_juntar_ferramenta(struct eu_msg *m, const struct json *t) {
	struct eu_ferramenta f = { 0 };
	g_strlcpy(f.nome, json_chave_texto(t, "nome"), sizeof(f.nome));
	g_strlcpy(f.resumo, json_chave_texto(t, "resumo"), sizeof(f.resumo));
	f.ok = json_bool(json_chave(t, "ok"), false);
	g_strlcpy(f.secao, json_chave_texto(json_chave(t, "args"), "secao"), sizeof(f.secao));
	g_array_append_val(m->ferramentas, f);
}


/* ---- Markdown ---- */

float eu_largura_balao = 340;

/* o texto de uma linha em marcação do pango: **negrito**, *itálico*, `código`, ~~riscado~~, [texto](endereço) */
static char *md_inline(const char *t, Clay_Color acento) {
	GString *o = g_string_new(NULL);
	const char *p = t;
	while (*p) {
		const char *fim = NULL;
		if (!strncmp(p, "**", 2) || !strncmp(p, "__", 2)) {
			fim = strstr(p + 2, !strncmp(p, "**", 2) ? "**" : "__");
			if (fim && fim > p + 2) {
				char *m = g_strndup(p + 2, (gsize)(fim - p - 2)), *i = md_inline(m, acento);
				g_string_append_printf(o, "<b>%s</b>", i);
				g_free(i);
				g_free(m);
				p = fim + 2;
				continue;
			}
		} else if (!strncmp(p, "~~", 2)) {
			fim = strstr(p + 2, "~~");
			if (fim && fim > p + 2) {
				char *m = g_strndup(p + 2, (gsize)(fim - p - 2)), *i = md_inline(m, acento);
				g_string_append_printf(o, "<s>%s</s>", i);
				g_free(i);
				g_free(m);
				p = fim + 2;
				continue;
			}
		} else if (*p == '`') {
			fim = strchr(p + 1, '`');
			if (fim && fim > p + 1) {
				char *m = g_strndup(p + 1, (gsize)(fim - p - 1)), *e = g_markup_escape_text(m, -1);
				g_string_append_printf(o, "<span font_family=\"monospace\" background=\"#000000\" bgalpha=\"28%%\"> %s </span>", e);
				g_free(e);
				g_free(m);
				p = fim + 1;
				continue;
			}
		} else if ((*p == '*' || *p == '_') && p[1] && p[1] != ' ' && (p == t || !g_ascii_isalnum(p[-1]) || *p == '*')) {
			fim = strchr(p + 1, *p);
			if (fim && fim > p + 1 && fim[-1] != ' ' && (*p == '*' || !g_ascii_isalnum(fim[1]))) {
				char *m = g_strndup(p + 1, (gsize)(fim - p - 1)), *i = md_inline(m, acento);
				g_string_append_printf(o, "<i>%s</i>", i);
				g_free(i);
				g_free(m);
				p = fim + 1;
				continue;
			}
		} else if (*p == '[') {
			const char *fecha = strchr(p, ']');
			if (fecha && fecha[1] == '(') {
				const char *par = strchr(fecha, ')');
				if (par) {
					char *m = g_strndup(p + 1, (gsize)(fecha - p - 1)), *e = g_markup_escape_text(m, -1);
					g_string_append_printf(o, "<span foreground=\"#%02x%02x%02x\" underline=\"single\">%s</span>", (unsigned)acento.r, (unsigned)acento.g, (unsigned)acento.b, e);
					g_free(e);
					g_free(m);
					p = par + 1;
					continue;
				}
			}
		}
		const char *prox = g_utf8_next_char(p);
		char *c = g_strndup(p, (gsize)(prox - p)), *e = g_markup_escape_text(c, -1);
		g_string_append(o, e);
		g_free(e);
		g_free(c);
		p = prox;
	}
	return g_string_free(o, FALSE);
}

static bool md_comeca(const char *l, const char *prefixo) {
	return g_str_has_prefix(l, prefixo);
}

/* a linha, sem os espaços da frente, começa assim? */
static bool md_lin(const char *l, const char *prefixo) {
	while (*l == ' ') {
		l++;
	}
	return g_str_has_prefix(l, prefixo);
}

/* "- item", "* item", "1. item": devolve onde o texto começa (NULL: não é item) e se é numerado */
static const char *md_item(const char *l, bool *numerado, char *numero, size_t tn) {
	*numerado = false;
	if ((l[0] == '-' || l[0] == '*' || l[0] == '+') && l[1] == ' ') {
		return l + 2;
	}
	const char *q = l;
	while (g_ascii_isdigit(*q)) {
		q++;
	}
	if (q > l && q - l < 5 && (*q == '.' || *q == ')') && q[1] == ' ') {
		*numerado = true;
		g_strlcpy(numero, l, (gsize)(q - l + 2) < tn ? (gsize)(q - l + 2) : tn);
		return q + 2;
	}
	return NULL;
}

/* o texto tem algo de Markdown que justifique o desenho rico? (senão fica o balão enxuto de sempre) */
bool eu_tem_markdown(const char *t) {
	if (strstr(t, "```") || strstr(t, "**") || strstr(t, "~~") || strchr(t, '`') || strstr(t, "](") || strstr(t, "__")) {
		return true;
	}
	bool inicio = true;
	for (const char *p = t; *p; p++) {
		if (inicio) {
			const char *l = p;
			while (*l == ' ') {
				l++;
			}
			bool n;
			char num[8];
			if (*l == '#' || *l == '>' || *l == '|' || md_item(l, &n, num, sizeof(num)) || !strncmp(l, "---", 3)) {
				return true;
			}
		}
		inicio = *p == '\n';
	}
	return false;
}

static void md_codigo(uint32_t k, int *n, const char *lingua, GString *codigo, float tam) {
	int idx = (*n)++;
	CLAY(CLAY_SID(gv_fmt("md-cod-%u-%d", k, idx)), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 6, 12 }, .childGap = 2 },
		.backgroundColor = (Clay_Color){ 0, 0, 0, 90 }, .cornerRadius = CLAY_CORNER_RADIUS(10), .border = { .color = gv_alfa(gv_t.borda, 0.7f), .width = GV_BORDA(1) } }) {
		CLAY(CLAY_SID(gv_fmt("md-codh-%u-%d", k, idx)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(lingua[0] ? lingua : "código"), tam - 3, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			if (gv_botao_ext(CLAY_SID(gv_fmt("md-cp-%u-%d", k, idx)), &(gv_botao_opcoes){ .icone = "copy", .texto = "Copiar", .estilo = GV_BOTAO_SIMPLES, .altura = 28 })) {
				gv_copiar(codigo->str);
			}
		}
		char *esc = g_markup_escape_text(codigo->str, -1);
		gv_texto_rico(CLAY_SID(gv_fmt("md-cd-%u-%d", k, idx)), esc, tam - 1, (Clay_Color){ 235, 235, 245, 255 }, true);
		g_free(esc);
	}
}

void eu_markdown(uint32_t k, const char *texto, float tam, Clay_Color cor) {
	char **ls = g_strsplit(texto, "\n", -1);
	int n = 0, nl = (int)g_strv_length(ls);
	CLAY(CLAY_SID(gv_fmt("md-%u", k)), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
		for (int i = 0; i < nl; i++) {
			const char *l = ls[i];
			const char *sem = l;
			while (*sem == ' ') {
				sem++;
			}
			int recuo = (int)(sem - l);
			if (md_comeca(sem, "```")) {
				char lingua[32];
				const char *lg = sem + 3;
				while (*lg == ' ') {
					lg++;
				}
				g_strlcpy(lingua, lg, sizeof(lingua));
				g_strchomp(lingua);
				GString *c = g_string_new(NULL);
				for (i++; i < nl && !md_lin(ls[i], "```"); i++) {
					g_string_append_printf(c, "%s%s", c->len ? "\n" : "", ls[i]);
				}
				md_codigo(k, &n, lingua, c, tam);
				g_string_free(c, TRUE);
			} else if (!*sem) {
				continue;
			} else if (*sem == '#') {
				int nivel = 0;
				while (sem[nivel] == '#') {
					nivel++;
				}
				if (nivel <= 6 && sem[nivel] == ' ') {
					char *in = md_inline(sem + nivel + 1, gv_t.acento), *m = g_strdup_printf("<b>%s</b>", in);
					gv_texto_rico(CLAY_SID(gv_fmt("md-t-%u-%d", k, n++)), m, tam + (nivel == 1 ? 7 : nivel == 2 ? 4 : nivel == 3 ? 2 : 0), cor, false);
					g_free(m);
					g_free(in);
					continue;
				}
				goto paragrafo;
			} else if (!strncmp(sem, "---", 3) || !strncmp(sem, "***", 3)) {
				gv_separador(false, 0);
			} else if (*sem == '>') {
				GString *q = g_string_new(NULL);
				for (; i < nl && md_lin(ls[i], ">"); i++) {
					const char *t = ls[i];
					while (*t == ' ') {
						t++;
					}
					t++;
					if (*t == ' ') {
						t++;
					}
					g_string_append_printf(q, "%s%s", q->len ? "\n" : "", t);
				}
				i--;
				char *in = md_inline(q->str, gv_t.acento);
				CLAY(CLAY_SID(gv_fmt("md-q-%u-%d", k, n)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 0, 2, 2 }, .childGap = 10 },
					.border = { .color = gv_t.acento, .width = { .left = 3 } } }) {
					gv_texto_rico(CLAY_SID(gv_fmt("md-qt-%u-%d", k, n)), in, tam, gv_t.texto_secundario, false);
				}
				n++;
				g_free(in);
				g_string_free(q, TRUE);
			} else if (*sem == '|') {
				/* uma tabela: as linhas | a | b |, sem a de traços */
				GPtrArray *linhas = g_ptr_array_new();
				for (; i < nl && md_lin(ls[i], "|"); i++) {
					char *copia = g_strdup(ls[i]);
					if (!strpbrk(copia, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")) {
						g_free(copia);
						continue;
					}
					g_ptr_array_add(linhas, copia);
				}
				i--;
				for (guint r = 0; r < linhas->len; r++) {
					char **cel = g_strsplit(g_strstrip(linhas->pdata[r]), "|", -1);
					CLAY(CLAY_SID(gv_fmt("md-tl-%u-%d-%u", k, n, r)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 10, 10, 6, 6 }, .childGap = 12 },
						.backgroundColor = r == 0 ? gv_alfa(gv_t.acento, 0.14f) : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(6) }) {
						int c = 0;
						for (int j = 0; cel[j]; j++) {
							if (!*g_strstrip(cel[j]) && (j == 0 || !cel[j + 1])) {
								continue;
							}
							char *in = md_inline(cel[j], gv_t.acento), *m = r == 0 ? g_strdup_printf("<b>%s</b>", in) : g_strdup(in);
							gv_texto_rico(CLAY_SID(gv_fmt("md-tc-%u-%d-%u-%d", k, n, r, c++)), m, tam - 1, cor, false);
							g_free(m);
							g_free(in);
						}
					}
					g_strfreev(cel);
				}
				n++;
				g_ptr_array_free(linhas, TRUE);
			} else {
				bool numerado;
				char numero[8];
				const char *item = md_item(sem, &numerado, numero, sizeof(numero));
				if (item) {
					char *in = md_inline(item, gv_t.acento);
					CLAY(CLAY_SID(gv_fmt("md-li-%u-%d", k, n)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { (uint16_t)(6 + recuo * 7), 0, 0, 0 }, .childGap = 8 } }) {
						CLAY(CLAY_SID(gv_fmt("md-lm-%u-%d", k, n)), { .layout = { .sizing = { CLAY_SIZING_FIXED(numerado ? 26 : 14) } } }) {
							gv_texto(gv_str(numerado ? numero : "•"), tam, gv_t.acento, GV_PESO_MEDIO);
						}
						gv_texto_rico(CLAY_SID(gv_fmt("md-lt-%u-%d", k, n)), in, tam, cor, false);
					}
					n++;
					g_free(in);
					continue;
				}
			paragrafo:;
				GString *pr = g_string_new(NULL);
				for (; i < nl; i++) {
					const char *t = ls[i];
					while (*t == ' ') {
						t++;
					}
					bool nn;
					char nm[8];
					if (pr->len && (!*t || md_comeca(t, "```") || *t == '#' || *t == '>' || *t == '|' || md_item(t, &nn, nm, sizeof(nm)))) {
						break;
					}
					g_string_append_printf(pr, "%s%s", pr->len ? "\n" : "", ls[i]);
				}
				i--;
				char *in = md_inline(pr->str, gv_t.acento);
				gv_texto_rico(CLAY_SID(gv_fmt("md-p-%u-%d", k, n++)), in, tam, cor, false);
				g_free(in);
				g_string_free(pr, TRUE);
			}
		}
	}
	g_strfreev(ls);
}

/* ---- o desenho ---- */

bool eu_botao_icone(Clay_ElementId id, const char *icone, bool ligado) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ligado ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		tocado |= gv_focavel(id, gv_t.raio_medio);
		gv_icone(icone, 22, ligado ? gv_t.acento : gv_t.texto_secundario);
	}
	return tocado;
}

void eu_avatar(Clay_ElementId id, float tam) {
	if (eu_avatar_caminho) {
		CLAY(id, gv_imagem(id, eu_avatar_caminho, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) } }, .cornerRadius = CLAY_CORNER_RADIUS(tam / 2) })) {}
	} else {
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.acento, 0.2f), .cornerRadius = CLAY_CORNER_RADIUS(tam / 2) }) {
			gv_icone("sparkle", tam * 0.5f, gv_t.acento);
		}
	}
}

bool eu_chip(Clay_ElementId id, const char *texto, bool principal) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .padding = { 14, 14, 8, 8 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = principal ? gv_alfa(gv_t.acento, 0.16f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(18), .border = { .color = principal ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		tocado |= gv_focavel(id, 18);
		gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
	}
	return tocado;
}

/* o nome de uma seção do Painel para o cartão: "teclado/teclado" -> ("Layouts de teclado", "Teclado e mouse") */
void eu_nome_da_secao(const char *secao, char *titulo, size_t tt, char *sub, size_t ts) {
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

void eu_cartao_secao(uint32_t k, const struct eu_ferramenta *f) {
	Clay_ElementId id = CLAY_IDI("em-cartao", k);
	bool tocado = gv_tocado(id);
	const char *icone = strstr(f->secao, "teclado") ? "keyboard" : strstr(f->secao, "telas") ? "desktop" : strstr(f->secao, "rede") ? "wifi" : "settings";
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0, 380) }, .padding = CLAY_PADDING_ALL(14), .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		tocado |= gv_focavel(id, gv_t.raio_medio);
		gv_icone(icone, 36, gv_t.texto);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			char titulo[96], sub[96];
			eu_nome_da_secao(f->secao, titulo, sizeof(titulo), sub, sizeof(sub));
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_fmt("Painel de controle · %s", sub), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(CLAY_STRING("Abrir configuração"), gv_t.tamanho_corpo - 1, gv_t.acento, GV_PESO_NORMAL);
				gv_icone("forward", 14, gv_t.acento);
			}
		}
	}
	if (tocado) {
		eu_abrir_secao(f->secao);
	}
}

void eu_bolha(uint32_t k, const struct eu_msg *m, void (*enviar)(const char *)) {
	if (m->usuario) {
		CLAY(CLAY_IDI("em-u", k), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			bool rico = eu_tem_markdown(m->texto);
			CLAY(CLAY_IDI("em-ub", k), { .layout = { .sizing = { rico ? CLAY_SIZING_GROW(0, eu_largura_balao) : CLAY_SIZING_FIT(0, eu_largura_balao) }, .padding = { 16, 16, 12, 12 } },
				.backgroundColor = EU_VIOLETA, .cornerRadius = CLAY_CORNER_RADIUS(18) }) {
				if (rico) {
					eu_markdown(k * 2 + 1, m->texto, gv_t.tamanho_corpo + 1, (Clay_Color){ 255, 255, 255, 255 });
				} else {
					gv_texto(gv_str(m->texto), gv_t.tamanho_corpo + 1, (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_NORMAL);
				}
			}
			gv_texto(gv_fmt("%s ✓✓", m->hora), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		return;
	}
	struct eu_resposta r;
	eu_ler_resposta(m->texto, &r);
	CLAY(CLAY_IDI("em-e", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		eu_avatar(CLAY_IDI("em-ea", k), 34);
		CLAY(CLAY_IDI("em-ec", k), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
			if (r.corpo[0] || m->parcial) {
				bool rico = eu_tem_markdown(r.corpo);
				CLAY(CLAY_IDI("em-eb", k), { .layout = { .sizing = { rico ? CLAY_SIZING_GROW(0, eu_largura_balao) : CLAY_SIZING_FIT(0, eu_largura_balao) }, .padding = { 16, 16, 12, 12 } },
					.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(18) }) {
					if (rico) {
						eu_markdown(k * 2, r.corpo, gv_t.tamanho_corpo + 1, gv_t.texto);
					} else {
						gv_texto(gv_str(r.corpo[0] ? r.corpo : "…"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
					}
				}
			}
			for (guint i = 0; i < m->ferramentas->len; i++) {
				const struct eu_ferramenta *f = &g_array_index(m->ferramentas, struct eu_ferramenta, i);
				if (!strcmp(f->nome, "painel.abrir") || !strcmp(f->nome, "lumna_painel_abrir")) {
					if (f->secao[0]) {
						eu_cartao_secao(k * 16 + i, f);
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
							if (eu_chip(CLAY_IDI("em-chip", k * 8 + j), rot[j], prin[j]) && enviar) {
								enviar(rot[j]);
							}
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

/* ---- abrir outros programas ---- */

static void lancar(const char *programa, const char *a1, const char *a2) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			setsid();
			execlp(programa, programa, a1, a2, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

void eu_abrir_secao(const char *secao) {
	lancar("lumna-painel", "--secao", secao);
}

void eu_lancar(const char *programa) {
	lancar(programa, NULL, NULL);
}
