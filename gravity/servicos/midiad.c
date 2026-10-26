#define _GNU_SOURCE
#include "midiad.h"

#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "firefox.h"
#include "json.h"
#include "midia.h"
#include "modos.h"

#define CAPA_ATE_MS 25000       /* a página que nunca termina de carregar não prende a capa */
#define CAPA_SAIDA_MS 350       /* a janela troca por baixo da capa; só então ela começa a sumir */

static const char *este_programa;
static struct ff_estado fe;

struct filho {
	GPid pid;
};
static struct filho tela_inicio, tela_seletor, tela_capa;

static struct {
	char servico[32];
	bool visivel;
	guint prazo;
} capa;

static GPtrArray *assinantes; /* GOutputStream* */

/* ---- as telas ---- */

static void filho_saiu(GPid pid, gint estado, gpointer dados) {
	struct filho *f = dados;
	g_spawn_close_pid(pid);
	f->pid = 0;
}

static void abrir_tela(struct filho *f, const char *papel, const char *extra) {
	if (f->pid) {
		return;
	}
	char *argv[] = { (char *)este_programa, "tela", (char *)papel, (char *)extra, NULL };
	GError *erro = NULL;
	if (g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, &f->pid, &erro)) {
		g_child_watch_add(f->pid, filho_saiu, f);
	} else {
		f->pid = 0;
	}
	g_clear_error(&erro);
}

/* ---- as janelas no Orbit ---- */

static bool trazer_janela(const char *app_id) {
	char *r = g_malloc(32768);
	bool achou = false;
	if (orbit_pedido("janelas", r, 32768)) {
		struct json *j = json_ler(r);
		const struct json *lista = json_chave(j, "janelas");
		for (int i = 0; lista && i < lista->nfilhos && !achou; i++) {
			if (!strcmp(json_chave_texto(lista->filhos[i], "app_id"), app_id)) {
				char pedido[40], resp[256];
				snprintf(pedido, sizeof(pedido), "foco %d", (int)json_numero(json_chave(lista->filhos[i], "id"), 0));
				achou = orbit_pedido(pedido, resp, sizeof(resp));
			}
		}
		json_liberar(j);
	}
	g_free(r);
	return achou;
}

static void garantir_modo_midia(void) {
	struct modos_estado e;
	if (modos_ler(&e) && modos_em_foco(&e) != MODO_MIDIA) {
		modos_definir(MODO_MIDIA, NULL);
	}
}

/* ---- o estado, para as telas ---- */

static void difundir(void) {
	static const char aviso[] = "{\"evento\":\"estado\"}\n";
	for (guint i = assinantes->len; i-- > 0;) {
		GOutputStream *o = assinantes->pdata[i];
		GError *erro = NULL;
		if (g_output_stream_write(o, aviso, sizeof(aviso) - 1, NULL, &erro) < 0) {
			g_ptr_array_remove_index(assinantes, i);
		}
		g_clear_error(&erro);
	}
}

static void estado_json(GString *s) {
	struct midia_servico serv[MIDIA_MAX_SERVICOS];
	int n = midia_servicos_ler(serv, MIDIA_MAX_SERVICOS);
	struct midia_recente rec[MIDIA_MAX_RECENTES];
	int nr = midia_recentes_ler(rec, MIDIA_MAX_RECENTES);
	g_string_append_printf(s, "{\"ok\":true,\"firefox\":%s,\"login\":%s,\"ativo\":", fe.ligado ? "true" : "false", fe.login ? "true" : "false");
	json_escrever_texto(s, fe.ativo);
	g_string_append(s, ",\"servicos\":[");
	for (int i = 0; i < n; i++) {
		bool aberto = false, carregando = false;
		for (int a = 0; a < fe.n; a++) {
			if (!strcmp(fe.abas[a].servico, serv[i].id)) {
				aberto = true;
				carregando = fe.abas[a].carregando;
			}
		}
		g_string_append(s, i ? ",{\"id\":" : "{\"id\":");
		json_escrever_texto(s, serv[i].id);
		g_string_append(s, ",\"nome\":");
		json_escrever_texto(s, serv[i].nome);
		g_string_append(s, ",\"url\":");
		json_escrever_texto(s, serv[i].url);
		g_string_append(s, ",\"cor\":");
		json_escrever_texto(s, serv[i].cor);
		g_string_append_printf(s, ",\"padrao\":%s,\"ativo\":%s,\"aberto\":%s,\"carregando\":%s}", serv[i].padrao ? "true" : "false",
			serv[i].ativo ? "true" : "false", aberto ? "true" : "false", carregando ? "true" : "false");
	}
	g_string_append(s, "],\"recentes\":[");
	for (int i = 0; i < nr; i++) {
		g_string_append(s, i ? ",{\"servico\":" : "{\"servico\":");
		json_escrever_texto(s, rec[i].servico);
		g_string_append(s, ",\"url\":");
		json_escrever_texto(s, rec[i].url);
		g_string_append(s, ",\"titulo\":");
		json_escrever_texto(s, rec[i].titulo);
		g_string_append(s, ",\"miniatura\":");
		json_escrever_texto(s, rec[i].miniatura);
		g_string_append_printf(s, ",\"quando\":%" G_GINT64_FORMAT "}", rec[i].quando);
	}
	g_string_append(s, "],\"capa\":{\"servico\":");
	json_escrever_texto(s, capa.servico);
	g_string_append_printf(s, ",\"visivel\":%s}}", capa.visivel ? "true" : "false");
}

/* ---- a capa de loading ---- */

static gboolean capa_esconder(gpointer dados) {
	capa.prazo = 0;
	capa.visivel = false;
	difundir();
	return G_SOURCE_REMOVE;
}

static void capa_agendar_saida(guint ms) {
	if (capa.prazo) {
		g_source_remove(capa.prazo);
	}
	capa.prazo = g_timeout_add(ms, capa_esconder, NULL);
}

static gboolean capa_trazer_firefox(gpointer dados) {
	static int tentativas;
	if (!capa.visivel) {
		tentativas = 0;
		return G_SOURCE_REMOVE;
	}
	if (trazer_janela(FF_APP_ID) || ++tentativas > 15) {
		tentativas = 0;
		capa_agendar_saida(CAPA_SAIDA_MS);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE; /* a janela do Firefox ainda não apareceu */
}

static bool capa_concluindo;

static void capa_mostrar(const char *servico) {
	g_strlcpy(capa.servico, servico, sizeof(capa.servico));
	capa.visivel = true;
	capa_concluindo = false;
	capa_agendar_saida(CAPA_ATE_MS);
	abrir_tela(&tela_capa, "capa", capa.servico);
	difundir();
}

/* o Firefox mudou: a página da capa carregou? */
static void verificar_capa(void) {
	if (!capa.visivel || capa_concluindo) {
		return;
	}
	for (int i = 0; i < fe.n; i++) {
		if (!strcmp(fe.abas[i].servico, capa.servico) && !fe.abas[i].carregando && !strcmp(fe.ativo, capa.servico)) {
			capa_concluindo = true;
			if (capa.prazo) {
				g_source_remove(capa.prazo);
				capa.prazo = 0;
			}
			g_timeout_add(150, capa_trazer_firefox, NULL);
			return;
		}
	}
}

static gboolean ff_acordou(gint fd, GIOCondition cond, gpointer dados) {
	ff_ler(&fe);
	verificar_capa();
	difundir();
	return G_SOURCE_CONTINUE;
}

/* ---- os pedidos ---- */

static const char *url_para_abrir(const char *id, bool novo, const struct midia_servico *s) {
	static char url[1024];
	if (!novo) {
		struct midia_recente rec[MIDIA_MAX_RECENTES];
		int n = midia_recentes_ler(rec, MIDIA_MAX_RECENTES);
		for (int i = 0; i < n; i++) {
			if (!strcmp(rec[i].servico, id)) {
				g_strlcpy(url, rec[i].url, sizeof(url));
				return url;
			}
		}
	}
	g_strlcpy(url, s->url, sizeof(url));
	return url;
}

static void guardar_o_que_passava(void) {
	if (fe.ativo[0]) {
		ff_salvar(fe.ativo, true); /* a miniatura de quem sai */
	}
}

/* o Firefox de login abre devagar: tenta trazê-lo para a frente até a janela existir */
static gboolean login_trazer_firefox(gpointer dados) {
	static int tentativas;
	if (trazer_janela(FF_APP_ID) || ++tentativas > 25) {
		tentativas = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void pedido(const char *linha, GString *r) {
	char **p = g_strsplit(g_strstrip(g_strdup(linha)), " ", 3);
	const char *cmd = p[0] ? p[0] : "";
	if (!strcmp(cmd, "estado")) {
		estado_json(r);
	} else if (!strcmp(cmd, "abrir") && p[1]) {
		struct midia_servico serv[MIDIA_MAX_SERVICOS];
		int n = midia_servicos_ler(serv, MIDIA_MAX_SERVICOS);
		const struct midia_servico *s = midia_servico_achar(serv, n, p[1]);
		if (!s || !s->ativo) {
			g_string_assign(r, "{\"ok\":false,\"erro\":\"serviço desconhecido ou desligado\"}");
		} else {
			bool vivo = false;
			for (int i = 0; i < fe.n; i++) {
				vivo = vivo || !strcmp(fe.abas[i].servico, s->id);
			}
			if (strcmp(fe.ativo, s->id)) {
				guardar_o_que_passava();
			}
			garantir_modo_midia();
			ff_abrir(s->id, url_para_abrir(s->id, p[2] && !strcmp(p[2], "novo"), s));
			if (vivo) {
				trazer_janela(FF_APP_ID); /* já carregado: sem capa */
			} else {
				capa_mostrar(s->id);
			}
			g_string_assign(r, "{\"ok\":true}");
		}
	} else if (!strcmp(cmd, "entrar") && p[1]) {
		/* um Firefox comum, sem controle, na página de entrada do serviço (o Google não deixa entrar no controlado) */
		struct midia_servico serv[MIDIA_MAX_SERVICOS];
		int n = midia_servicos_ler(serv, MIDIA_MAX_SERVICOS);
		const struct midia_servico *s = midia_servico_achar(serv, n, p[1]);
		if (!s || !s->ativo) {
			g_string_assign(r, "{\"ok\":false,\"erro\":\"serviço desconhecido ou desligado\"}");
		} else {
			guardar_o_que_passava();
			garantir_modo_midia();
			fe.ativo[0] = '\0';
			ff_entrar(s->login);
			g_timeout_add(400, login_trazer_firefox, NULL);
			g_string_assign(r, "{\"ok\":true}");
		}
	} else if (!strcmp(cmd, "inicio")) {
		if (fe.login) {
			ff_sair_login(); /* acabou de entrar: o Firefox comum fecha, o controlado volta quando abrir um serviço */
		}
		guardar_o_que_passava();
		garantir_modo_midia();
		if (!trazer_janela(MIDIA_APP_INICIO)) {
			abrir_tela(&tela_inicio, "inicio", NULL);
		}
		fe.ativo[0] = '\0';
		g_string_assign(r, "{\"ok\":true}");
	} else if (!strcmp(cmd, "seletor")) {
		abrir_tela(&tela_seletor, "seletor", NULL);
		g_string_assign(r, "{\"ok\":true}");
	} else if (!strcmp(cmd, "fechar") && p[1]) {
		ff_fechar(p[1]);
		g_string_assign(r, "{\"ok\":true}");
	} else if (!strcmp(cmd, "servico_adicionar") && p[1] && p[2]) {
		char id[32];
		if (midia_servico_adicionar(p[2], p[1], id, sizeof(id))) {
			g_string_printf(r, "{\"ok\":true,\"id\":\"%s\"}", id);
			difundir();
		} else {
			g_string_assign(r, "{\"ok\":false,\"erro\":\"endereço ou nome inválido\"}");
		}
	} else if (!strcmp(cmd, "servico_remover") && p[1]) {
		ff_fechar(p[1]);
		midia_servico_remover(p[1]) ? g_string_assign(r, "{\"ok\":true}") : g_string_assign(r, "{\"ok\":false,\"erro\":\"só os adicionados saem\"}");
		difundir();
	} else if (!strcmp(cmd, "servico_ativar") && p[1] && p[2]) {
		bool sim = !strcmp(p[2], "sim");
		if (!sim) {
			ff_fechar(p[1]);
		}
		midia_servico_ativar(p[1], sim) ? g_string_assign(r, "{\"ok\":true}") : g_string_assign(r, "{\"ok\":false,\"erro\":\"serviço desconhecido\"}");
		difundir();
	} else {
		g_string_assign(r, "{\"ok\":false,\"erro\":\"pedido desconhecido (estado, abrir ID, inicio, seletor, fechar ID, servico_*, assinar)\"}");
	}
	g_strfreev(p);
}

static gboolean chegou(GSocketService *servico, GSocketConnection *con, GObject *origem, gpointer dados) {
	g_socket_set_timeout(g_socket_connection_get_socket(con), 2);
	GDataInputStream *entrada = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(con)));
	char *linha = g_data_input_stream_read_line(entrada, NULL, NULL, NULL);
	if (linha && !strcmp(g_strstrip(linha), "assinar")) {
		/* fica aberta: cada mudança vira uma linha */
		g_socket_set_timeout(g_socket_connection_get_socket(con), 0);
		g_socket_set_blocking(g_socket_connection_get_socket(con), FALSE);
		g_ptr_array_add(assinantes, g_object_ref(g_io_stream_get_output_stream(G_IO_STREAM(con))));
		g_object_ref(con); /* a conexão vive enquanto o fluxo dela estiver na lista */
	} else if (linha) {
		GString *r = g_string_new(NULL);
		pedido(linha, r);
		g_string_append_c(r, '\n');
		g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(con)), r->str, r->len, NULL, NULL, NULL);
		g_string_free(r, TRUE);
	}
	g_free(linha);
	g_object_unref(entrada);
	return TRUE;
}

/* ---- o Orbit mudou de modo: quem entra na Mídia vê o Início ---- */

static bool algum_em_midia;

static gboolean orbit_mexeu(gint fd, GIOCondition cond, gpointer dados) {
	modos_assinar_ler(fd);
	struct modos_estado e;
	bool agora = false;
	if (modos_ler(&e)) {
		for (int i = 0; i < e.n; i++) {
			agora = agora || e.saidas[i].modo == MODO_MIDIA;
		}
	}
	if (agora && !algum_em_midia && !fe.ativo[0] && !trazer_janela(MIDIA_APP_INICIO)) {
		abrir_tela(&tela_inicio, "inicio", NULL);
	}
	algum_em_midia = agora;
	return G_SOURCE_CONTINUE;
}

static gboolean orbit_ligar(gpointer dados) {
	static guint fonte;
	if (fonte) {
		return G_SOURCE_REMOVE;
	}
	int fd = modos_assinar();
	if (fd < 0) {
		return G_SOURCE_CONTINUE;
	}
	fonte = g_unix_fd_add(fd, G_IO_IN | G_IO_HUP, orbit_mexeu, NULL);
	return G_SOURCE_REMOVE;
}

/* os recentes mudaram em disco (a miniatura chegou): as telas redesenham */
static void recentes_mudaram(GFileMonitor *m, GFile *a, GFile *b, GFileMonitorEvent ev, gpointer dados) {
	if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_CREATED) {
		difundir();
	}
}

static gboolean encerrar(gpointer dados) {
	ff_encerrar();
	g_main_loop_quit(dados);
	return G_SOURCE_REMOVE;
}

int midiad_correr(const char *programa) {
	signal(SIGPIPE, SIG_IGN);
	este_programa = programa;
	assinantes = g_ptr_array_new_with_free_func(g_object_unref);
	int fd = ff_iniciar();
	if (fd < 0) {
		return 1;
	}
	g_unix_fd_add(fd, G_IO_IN, ff_acordou, NULL);
	ff_ler(&fe);

	char *caminho = g_build_filename(g_get_user_runtime_dir(), "lumna-midia.sock", NULL);
	unlink(caminho);
	GError *erro = NULL;
	GSocketService *servico = g_socket_service_new();
	GSocketAddress *end = g_unix_socket_address_new(caminho);
	if (!g_socket_listener_add_address(G_SOCKET_LISTENER(servico), end, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL, &erro)) {
		fprintf(stderr, "lumna-midia: socket %s: %s\n", caminho, erro->message);
		return 1;
	}
	chmod(caminho, 0600);
	g_signal_connect(servico, "incoming", G_CALLBACK(chegou), NULL);
	g_socket_service_start(servico);

	char *dados = midia_pasta_dados();
	GFile *pasta = g_file_new_for_path(dados);
	GFileMonitor *mon = g_file_monitor_directory(pasta, G_FILE_MONITOR_NONE, NULL, NULL);
	if (mon) {
		g_signal_connect(mon, "changed", G_CALLBACK(recentes_mudaram), NULL);
	}
	g_free(dados);

	orbit_ligar(NULL);
	g_timeout_add_seconds(3, orbit_ligar, NULL);

	GMainLoop *laco = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGINT, encerrar, laco);
	g_unix_signal_add(SIGTERM, encerrar, laco);
	g_main_loop_run(laco);
	unlink(caminho);
	return 0;
}
