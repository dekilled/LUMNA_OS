/* As ajudas que as seções do Painel partilham: os arquivos de config e as linhas de ajuste. */
#include "painel.h"

#include "papelini.h"

#include <glib.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void pn_caminho(char *buf, size_t tam, const char *nome) {
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *casa = getenv("HOME");
	if (config && *config) {
		snprintf(buf, tam, "%s/lumna/%s", config, nome);
	} else {
		snprintf(buf, tam, "%s/.config/lumna/%s", casa ? casa : "", nome);
	}
}

bool pn_gravar(const char *nome, const char *conteudo) {
	char caminho[1024], temp[1100];
	pn_caminho(caminho, sizeof(caminho), nome);
	char *pasta = g_path_get_dirname(caminho);
	g_mkdir_with_parents(pasta, 0755);
	g_free(pasta);
	snprintf(temp, sizeof(temp), "%s.novo", caminho);
	if (!g_file_set_contents(temp, conteudo, -1, NULL)) {
		return false;
	}
	return rename(temp, caminho) == 0;
}

void pn_subtitulo(Clay_ElementId id, const char *texto) {
	CLAY(id, { .layout = { .padding = { 0, 0, 14, 8 } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
	}
}

void pn_linha_comecar(Clay_ElementId id, const char *icone, const char *nome, const char *sub) {
	Clay__OpenElementWithId(id);
	Clay__ConfigureOpenElement((Clay_ElementDeclaration){
		.layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 2, 12, 12 }, .childGap = 16,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } },
	});
	if (icone) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(30) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
			gv_icone(icone, 24, gv_t.icone);
		}
	}
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
		gv_texto(gv_str(nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
		if (sub) {
			gv_texto(gv_str(sub), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

void pn_linha_acabar(void) {
	Clay__CloseElement();
}

bool pn_ajuste_lista(Clay_ElementId id, const char *icone, const char *nome, const char *sub, const char *const *opcoes,
	int n, int *escolha) {
	bool mudou = false;
	pn_linha_comecar(id, icone, nome, sub);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(260) } } }) {
		mudou = gv_lista_suspensa(Clay__HashStringWithOffset(id.stringId, 9000001u, id.baseId), opcoes, n, escolha);
	}
	pn_linha_acabar();
	return mudou;
}

bool pn_segmentos(Clay_ElementId id, const char *const *opcoes, int n, int *escolha) {
	bool mudou = false;
	CLAY(id, { .layout = { .childGap = 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		for (int i = 0; i < n; i++) {
			Clay_ElementId s = Clay__HashStringWithOffset(id.stringId, 7000000u + (uint32_t)i, id.baseId);
			bool esta = *escolha == i;
			bool tocado = gv_tocado(s);
			Clay_CornerRadius r = { i == 0 ? gv_t.raio_pequeno : 0, i == n - 1 ? gv_t.raio_pequeno : 0,
				i == 0 ? gv_t.raio_pequeno : 0, i == n - 1 ? gv_t.raio_pequeno : 0 };
			CLAY(s, { .layout = { .sizing = { CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(34) },
					.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = esta ? gv_t.selecionado : gv_por_cima(s) ? gv_t.sob_cursor : (Clay_Color){ 0 },
				.cornerRadius = r,
				.border = { .color = esta ? gv_alfa(gv_t.acento, 0.7f) : gv_t.borda_controle,
					.width = { .left = i > 0 ? 1 : 0 } } }) {
				tocado |= gv_focavel(s, gv_t.raio_pequeno);
				gv_texto(gv_str(opcoes[i]), gv_t.tamanho_corpo, esta ? gv_t.acento : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (tocado && !esta) {
				*escolha = i;
				mudou = true;
			}
		}
	}
	return mudou;
}

void pn_executar(const char *comando) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			setsid();
			execl("/bin/sh", "sh", "-c", comando, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

/* ---- as imagens da pessoa (Imagens e Downloads), todas, por ordem de nome ---- */

static GPtrArray *suas;
#define nsuas ((int)suas->len)
#define SUA(i) ((const char *)g_ptr_array_index(suas, (i)))

static void juntar_suas(const char *pasta) {
	GDir *d = g_dir_open(pasta, 0, NULL);
	if (!d) {
		return;
	}
	const char *nome;
	while ((nome = g_dir_read_name(d))) {
		const char *ponto = strrchr(nome, '.');
		if (ponto && (!strcasecmp(ponto, ".jpg") || !strcasecmp(ponto, ".jpeg") || !strcasecmp(ponto, ".png") ||
			!strcasecmp(ponto, ".webp"))) {
			g_ptr_array_add(suas, g_build_filename(pasta, nome, NULL));
		}
	}
	g_dir_close(d);
}

static gint comparar_nomes(gconstpointer a, gconstpointer b) {
	return g_utf8_collate(*(const char *const *)a, *(const char *const *)b);
}

void pn_suas_carregar(void) {
	if (suas) {
		g_ptr_array_free(suas, TRUE);
	}
	suas = g_ptr_array_new_with_free_func(g_free);
	const char *imagens = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	if (imagens) {
		juntar_suas(imagens);
	}
	char *downloads = g_build_filename(g_get_home_dir(), "Downloads", NULL);
	juntar_suas(downloads);
	g_free(downloads);
	g_ptr_array_sort(suas, comparar_nomes);
}

bool pn_miniatura(Clay_ElementId id, const char *caminho, float largura, float altura, bool escolhida, const char *legenda) {
	bool tocado = gv_tocado(id);
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(largura) }, .childGap = 8,
		.childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIXED(altura) }, .padding = CLAY_PADDING_ALL(2) },
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio + 2),
			.border = { .color = escolhida ? gv_t.acento : gv_por_cima(id) ? gv_t.borda_controle : gv_t.borda,
				.width = GV_BORDA(escolhida ? 2 : 1) } }) {
			tocado |= gv_focavel(id, gv_t.raio_medio + 2);
			Clay_ElementId img = Clay__HashStringWithOffset(id.stringId, id.offset + 500000u, id.baseId);
			CLAY(img, gv_imagem(img, caminho, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
				.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6),
					.childAlignment = { .x = CLAY_ALIGN_X_RIGHT } },
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) })) {
				if (escolhida) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) },
						.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
						.backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(11) }) {
						gv_icone("check", 14, gv_t.sobre_acento);
					}
				}
			}
		}
		if (legenda) {
			gv_texto(gv_str(legenda), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	return tocado;
}

const char *pn_suas_popup(Clay_ElementId popup, Clay_ElementId ancora, const char *atual) {
	const char *escolhida = NULL;
	if (!suas) {
		pn_suas_carregar();
	}
	GV_POPUP(popup, ancora, .largura = 520) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = CLAY_PADDING_ALL(8), .childGap = 10 } }) {
			gv_texto(CLAY_STRING("Suas imagens (Imagens e Downloads)"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			if (!nsuas) {
				gv_texto(CLAY_STRING("Nenhuma imagem encontrada."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
			} else {
				gv_texto(gv_fmt("%d imagens", nsuas), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			/* todas, numa grade que rola (as miniaturas descodificam pequenas, só as que aparecem) */
			Clay_ElementId grade = CLAY_ID("suas-grade");
			CLAY(grade, gv_rolagem(grade, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
				.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0, 430) },
					.childGap = 10 } })) {
				for (int linha = 0; linha * 3 < nsuas; linha++) {
					CLAY(CLAY_IDI("suas-linha", (uint32_t)linha), { .layout = { .childGap = 10 } }) {
						for (int i = linha * 3; i < nsuas && i < linha * 3 + 3; i++) {
							char n[128];
							papel_nome(SUA(i), n, sizeof(n));
							if (pn_miniatura(CLAY_IDI("sua", (uint32_t)i), SUA(i), 156, 90, atual && !strcmp(SUA(i), atual), n)) {
								escolhida = SUA(i);
								gv_popup_fechar(popup);
							}
						}
					}
				}
			}
		}
	}
	return escolhida;
}

/* ---- terminal, tarefas, confirmação, formatos ---- */

void pn_terminal(const char *comando) {
	/* cada terminal pede o comando de um jeito; o primeiro que existir leva */
	static const struct { const char *programa, *formato; } terminais[] = {
		{ "foot", "foot sh -c %s" },
		{ "kitty", "kitty sh -c %s" },
		{ "alacritty", "alacritty -e sh -c %s" },
		{ "konsole", "konsole -e sh -c %s" },
		{ "gnome-terminal", "gnome-terminal -- sh -c %s" },
		{ "xterm", "xterm -e sh -c %s" },
	};
	char *com_espera = g_strdup_printf("%s; echo; printf 'Pronto. Aperte Enter para fechar. '; read _", comando);
	char *quoted = g_shell_quote(com_espera);
	const char *escolhido = g_getenv("LUMNA_TERMINAL");
	for (size_t i = 0; i < sizeof(terminais) / sizeof(terminais[0]); i++) {
		if (escolhido && *escolhido && strcmp(escolhido, terminais[i].programa) != 0) {
			continue;
		}
		char *caminho = g_find_program_in_path(terminais[i].programa);
		if (caminho) {
			g_free(caminho);
			char *linha = g_strdup_printf(terminais[i].formato, quoted);
			pn_executar(linha);
			g_free(linha);
			break;
		}
	}
	g_free(quoted);
	g_free(com_espera);
}

struct tarefa {
	void (*trabalho)(void *);
	void (*pronto)(void *);
	void *dados;
};

static GMutex tarefas_trinco;
static GQueue tarefas_prontas = G_QUEUE_INIT;
static int tarefas_fd = -1;

static void tarefas_acordou(gv_app *app, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	for (;;) {
		g_mutex_lock(&tarefas_trinco);
		struct tarefa *t = g_queue_pop_head(&tarefas_prontas);
		g_mutex_unlock(&tarefas_trinco);
		if (!t) {
			break;
		}
		if (t->pronto) {
			t->pronto(t->dados);
		}
		g_free(t);
	}
	gv_app_redesenhar(app);
}

static gpointer tarefa_linha(gpointer p) {
	struct tarefa *t = p;
	t->trabalho(t->dados);
	g_mutex_lock(&tarefas_trinco);
	g_queue_push_tail(&tarefas_prontas, t);
	g_mutex_unlock(&tarefas_trinco);
	uint64_t um = 1;
	if (write(tarefas_fd, &um, sizeof(um)) < 0) {
	}
	return NULL;
}

void pn_tarefa(void (*trabalho)(void *), void (*pronto)(void *), void *dados) {
	static bool vigiado;
	if (tarefas_fd < 0) {
		tarefas_fd = eventfd(0, EFD_CLOEXEC);
	}
	/* o painel pode pedir uma tarefa antes de a janela existir (abrir direto numa seção): vigia quando puder */
	if (!vigiado && tarefas_fd >= 0 && pn_app) {
		gv_app_vigiar(pn_app, tarefas_fd, tarefas_acordou, NULL);
		vigiado = true;
	}
	struct tarefa *t = g_new0(struct tarefa, 1);
	t->trabalho = trabalho;
	t->pronto = pronto;
	t->dados = dados;
	g_thread_unref(g_thread_new("painel-tarefa", tarefa_linha, t));
}

/* quem está pedindo confirmação agora (um por vez) e até quando */
static uint32_t confirmando_id;
static double confirmando_ate;

bool pn_confirmar(Clay_ElementId id, const char *icone, const char *texto, const char *aviso, bool perigo) {
	bool aberto = confirmando_id == id.id && gv_agora() < confirmando_ate;
	if (!aberto) {
		if (gv_botao(id, icone, texto, perigo ? GV_BOTAO_PERIGO : GV_BOTAO_ELEVADO)) {
			confirmando_id = id.id;
			confirmando_ate = gv_agora() + 8;
		}
		return false;
	}
	gv_acordar_em(confirmando_ate - gv_agora() + 0.05);
	bool sim = false;
	CLAY(CLAY_IDI("confirmar-caixa", id.id), { .layout = { .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (gv_botao(CLAY_IDI("confirmar-sim", id.id), perigo ? "trash" : "check", "Confirmar", perigo ? GV_BOTAO_PERIGO : GV_BOTAO_CHEIO)) {
			sim = true;
			confirmando_id = 0;
		}
		if (gv_botao(CLAY_IDI("confirmar-nao", id.id), NULL, "Cancelar", GV_BOTAO_ELEVADO)) {
			confirmando_id = 0;
		}
	}
	return sim;
}

const char *pn_tamanho(int64_t bytes) {
	static char buf[8][32];
	static int i;
	char *saida = buf[i++ % 8];
	static const char *const un[] = { "B", "KB", "MB", "GB", "TB" };
	double v = (double)(bytes < 0 ? 0 : bytes);
	int k = 0;
	while (v >= 1000.0 && k < 4) {
		v /= 1000.0;
		k++;
	}
	if (k == 0) {
		snprintf(saida, 32, "%d B", (int)v);
	} else {
		snprintf(saida, 32, v >= 100 ? "%.0f %s" : "%.1f %s", v, un[k]);
		for (char *p = saida; *p; p++) {
			if (*p == '.') {
				*p = ','; /* à brasileira */
			}
		}
	}
	return saida;
}

const char *pn_dia(int64_t unix_s) {
	static char buf[8][24];
	static int i;
	char *saida = buf[i++ % 8];
	if (unix_s <= 0) {
		return "—";
	}
	time_t t = (time_t)unix_s;
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(saida, 24, "%d/%m/%Y", &tm);
	return saida;
}
