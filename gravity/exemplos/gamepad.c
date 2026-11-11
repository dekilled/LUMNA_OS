/*
 * lumna-gamepad: o serviço dos controles do modo Gaming. Lê os controles (servicos/gamepad.h, sem capturá-los: o jogo continua lendo o dele) e
 *  - com o LUMNA Jogos em foco, traduz os botões em teclas (o direcional vira as setas, A é Enter, B é Esc, LB e RB são Page Up e Page Down) pelo wtype;
 *    com um jogo em foco, não manda tecla nenhuma;
 *  - o botão Guia (em qualquer janela) abre a biblioteca, o Início ou a Emma, conforme a configuração;
 *  - guarda os apelidos e os controles ignorados, e atende o Gaming (estado, vibrar, apelido, ignorar, recarregar, assinar: servicos/gamepadc.h).
 * Para testar: $LUMNA_GAMEPAD_FOCO é o app_id da janela "em foco" (no lugar de perguntar ao Orbit) e o PATH leva um wtype de mentira.
 */
#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emma_ativa.h"
#include "gamepad.h"
#include "gamepadc.h"
#include "json.h"
#include "modos.h"

static struct gp_disp disp[GP_MAX];
static int ndisp;
static struct gp_cfg cfg;
static GMainLoop *laco;
static GPtrArray *assinantes; /* GOutputStream* */
static char foco_app[96];
static bool segurado[GP_N];
static gint64 repete_em[GP_N];
static char lista_antes[512];

/* ---------------- avisar quem assina ---------------- */

static void difundir(const char *linha) {
	for (guint i = 0; i < assinantes->len;) {
		GError *e = NULL;
		GOutputStream *s = assinantes->pdata[i];
		if (!g_output_stream_write_all(s, linha, strlen(linha), NULL, NULL, &e)) {
			g_clear_error(&e);
			g_ptr_array_remove_index(assinantes, i);
		} else {
			i++;
		}
	}
}

/* ---------------- o foco e as ações ---------------- */

static bool jogos_em_foco(void) {
	return g_str_has_prefix(foco_app, "lumna-jogos");
}

static void foco_rever(void) {
	const char *f = g_getenv("LUMNA_GAMEPAD_FOCO");
	if (f) {
		/* um caminho: o teste troca o app_id em foco escrevendo no arquivo */
		char *t = NULL;
		if (f[0] == '/' && g_file_get_contents(f, &t, NULL, NULL)) {
			g_strlcpy(foco_app, g_strstrip(t), sizeof(foco_app));
			g_free(t);
		} else if (f[0] != '/') {
			g_strlcpy(foco_app, f, sizeof(foco_app));
		}
		return;
	}
	char *r = g_malloc(64 * 1024);
	foco_app[0] = '\0';
	if (orbit_pedido("janelas", r, 64 * 1024)) {
		struct json *j = json_ler(r);
		const struct json *l = json_chave(j, "janelas");
		for (int i = 0; l && i < l->nfilhos; i++) {
			const struct json *v = json_item(l, i);
			if (json_bool(json_chave(v, "foco"), false)) {
				g_strlcpy(foco_app, json_chave_texto(v, "app_id"), sizeof(foco_app));
			}
		}
		json_liberar(j);
	}
	g_free(r);
}

static void tecla(const char *nome) {
	char *argv[] = { "wtype", "-k", (char *)nome, NULL };
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

static const char *tecla_de(int b) {
	switch (b) {
	case GP_A: return "Return";
	case GP_B: return "Escape";
	case GP_CIMA: return "Up";
	case GP_BAIXO: return "Down";
	case GP_ESQ: return "Left";
	case GP_DIR: return "Right";
	case GP_LB: return "Prior";
	case GP_RB: return "Next";
	case GP_START: return "Menu";
	default: return NULL;
	}
}

static gboolean manda_home(gpointer dados) {
	if (jogos_em_foco()) {
		tecla("Home");
	}
	return G_SOURCE_REMOVE;
}

static void guia(void) {
	if (!strcmp(cfg.guia, "nada")) {
		return;
	}
	if (!strcmp(cfg.guia, "emma")) {
		if (emma_ativa()) {
			char *argv[] = { "sh", "-c", "pkill -x lumna-emma-chat || lumna-emma-chat", NULL };
			g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
		}
		return;
	}
	/* biblioteca ou início: traz o LUMNA Jogos (abre se não estiver aberto); "início" ainda manda a tecla Home */
	int estado = 0;
	char *pg[] = { "pgrep", "-x", "lumna-jogos", NULL };
	g_spawn_sync(NULL, pg, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &estado, NULL);
	if (estado != 0) {
		char *ab[] = { "lumna-jogos", NULL };
		g_spawn_async(NULL, ab, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
	} else {
		char *r = g_malloc(64 * 1024);
		if (orbit_pedido("janelas", r, 64 * 1024)) {
			struct json *j = json_ler(r);
			const struct json *l = json_chave(j, "janelas");
			for (int i = 0; l && i < l->nfilhos; i++) {
				const struct json *v = json_item(l, i);
				if (g_str_has_prefix(json_chave_texto(v, "app_id"), "lumna-jogos")) {
					char p[64], resp[256];
					g_snprintf(p, sizeof(p), "foco %d", (int)json_numero(json_chave(v, "id"), 0));
					orbit_pedido(p, resp, sizeof(resp));
				}
			}
			json_liberar(j);
		}
		g_free(r);
	}
	if (!strcmp(cfg.guia, "inicio")) {
		g_timeout_add(500, manda_home, NULL); /* dá tempo da janela ganhar o foco */
	}
}

/* ---------------- os botões ---------------- */

static void botao(const struct gp_disp *d, const struct gp_evento *e, void *dados) {
	if (d->ignorado) {
		return;
	}
	char *l = g_strdup_printf("{\"evento\":\"botao\",\"botao\":\"%s\",\"pressionado\":%s,\"disp\":\"%s\"}\n", gp_botao_nome(e->botao), e->pressionado ? "true" : "false", d->id);
	difundir(l);
	g_free(l);
	segurado[e->botao] = e->pressionado;
	repete_em[e->botao] = g_get_monotonic_time() + 400000;
	if (!e->pressionado) {
		return;
	}
	if (e->botao == GP_GUIA) {
		guia();
		return;
	}
	if (jogos_em_foco() && tecla_de(e->botao)) {
		tecla(tecla_de(e->botao));
	}
}

static gboolean repetir(gpointer dados) {
	gint64 agora = g_get_monotonic_time();
	for (int b = GP_CIMA; b <= GP_DIR; b++) {
		if (segurado[b] && agora >= repete_em[b]) {
			repete_em[b] = agora + 110000;
			if (jogos_em_foco()) {
				tecla(tecla_de(b));
			}
		}
	}
	return G_SOURCE_CONTINUE;
}

static gboolean controle_legivel(gint fd, GIOCondition c, gpointer dados) {
	struct gp_disp *d = dados;
	if (!gp_ler(d, &cfg, botao, NULL) || (c & (G_IO_HUP | G_IO_ERR))) {
		return G_SOURCE_REMOVE; /* some da lista na próxima varredura */
	}
	return G_SOURCE_CONTINUE;
}

/* ---------------- a lista (varrer de vez em quando) ---------------- */

static GHashTable *vigiados; /* fd → id da fonte */

static void varrer(void) {
	ndisp = gp_varrer(disp, ndisp, GP_MAX);
	/* os que acabaram de abrir passam a ser lidos pelo laço; os fechados já saíram da lista */
	for (int i = 0; i < ndisp; i++) {
		if (!g_hash_table_contains(vigiados, GINT_TO_POINTER(disp[i].fd + 1))) {
			guint id = g_unix_fd_add(disp[i].fd, G_IO_IN | G_IO_HUP | G_IO_ERR, controle_legivel, &disp[i]);
			g_hash_table_insert(vigiados, GINT_TO_POINTER(disp[i].fd + 1), GUINT_TO_POINTER(id));
		}
	}
	GString *s = g_string_new(NULL);
	for (int i = 0; i < ndisp; i++) {
		g_string_append_printf(s, "%s|%d|%d;", disp[i].id, disp[i].bateria, disp[i].ignorado);
	}
	if (strcmp(s->str, lista_antes)) {
		g_strlcpy(lista_antes, s->str, sizeof(lista_antes));
		difundir("{\"evento\":\"controles\"}\n");
	}
	g_string_free(s, TRUE);
}

static gboolean varrer_timer(gpointer dados) {
	/* um fd vigiado que o gp_varrer fechou (e que pode ser reaproveitado por outro controle) sai da tabela */
	GHashTableIter it;
	gpointer k, v;
	g_hash_table_iter_init(&it, vigiados);
	while (g_hash_table_iter_next(&it, &k, &v)) {
		bool vivo = false;
		for (int i = 0; i < ndisp; i++) {
			vivo |= disp[i].fd + 1 == GPOINTER_TO_INT(k);
		}
		if (!vivo) {
			g_source_remove(GPOINTER_TO_UINT(v));
			g_hash_table_iter_remove(&it);
		}
	}
	varrer();
	return G_SOURCE_CONTINUE;
}

static gboolean foco_timer(gpointer dados) {
	foco_rever();
	return G_SOURCE_CONTINUE;
}

/* ---------------- o socket ---------------- */

static char *estado_json(void) {
	GString *s = g_string_new("{\"ok\":true,\"controles\":[");
	for (int i = 0; i < ndisp; i++) {
		g_string_append_printf(s, "%s{\"id\":", i ? "," : "");
		json_escrever_texto(s, disp[i].id);
		g_string_append(s, ",\"nome\":");
		json_escrever_texto(s, disp[i].nome);
		g_string_append(s, ",\"apelido\":");
		json_escrever_texto(s, disp[i].apelido);
		g_string_append_printf(s, ",\"bluetooth\":%s,\"bateria\":%d,\"vibra\":%s,\"ignorado\":%s}", disp[i].bluetooth ? "true" : "false", disp[i].bateria,
			disp[i].rumble ? "true" : "false", disp[i].ignorado ? "true" : "false");
	}
	g_string_append_printf(s, "],\"cfg\":{\"zona_morta\":%d,\"vibracao\":%s,\"trocar_ab\":%s,\"guia\":\"%s\",\"energia\":\"%s\",\"fps\":%s},\"foco\":", cfg.zona_morta, cfg.vibracao ? "true" : "false",
		cfg.trocar_ab ? "true" : "false", cfg.guia, cfg.energia, cfg.fps ? "true" : "false");
	json_escrever_texto(s, foco_app);
	g_string_append(s, "}");
	return g_string_free(s, FALSE);
}

static struct gp_disp *por_id(const char *id) {
	for (int i = 0; i < ndisp; i++) {
		if (!strcmp(disp[i].id, id)) {
			return &disp[i];
		}
	}
	return NULL;
}

static char *atender(const char *linha) {
	char **p = g_strsplit(linha, " ", 3);
	char *r;
	if (!p[0]) {
		r = g_strdup("{\"ok\":false,\"erro\":\"pedido vazio\"}");
	} else if (!strcmp(p[0], "estado")) {
		r = estado_json();
	} else if (!strcmp(p[0], "recarregar")) {
		gp_cfg_ler(&cfg);
		gp_apelidos_ler(disp, ndisp);
		varrer();
		r = g_strdup("{\"ok\":true}");
	} else if (!strcmp(p[0], "vibrar") && p[1]) {
		struct gp_disp *d = por_id(p[1]);
		r = d && cfg.vibracao && gp_vibrar(d, 300) ? g_strdup("{\"ok\":true}") : g_strdup("{\"ok\":false,\"erro\":\"esse controle não vibra (ou a vibração está desligada)\"}");
	} else if (!strcmp(p[0], "apelido") && p[1]) {
		bool ok = gp_apelido_gravar(p[1], p[2] ? p[2] : "");
		gp_apelidos_ler(disp, ndisp);
		difundir("{\"evento\":\"controles\"}\n");
		r = g_strdup(ok ? "{\"ok\":true}" : "{\"ok\":false,\"erro\":\"id inválido\"}");
	} else if (!strcmp(p[0], "ignorar") && p[1] && p[2]) {
		bool ok = gp_ignorar_gravar(p[1], !strcmp(p[2], "sim"));
		gp_apelidos_ler(disp, ndisp);
		difundir("{\"evento\":\"controles\"}\n");
		r = g_strdup(ok ? "{\"ok\":true}" : "{\"ok\":false,\"erro\":\"id inválido\"}");
	} else {
		r = g_strdup("{\"ok\":false,\"erro\":\"pedido desconhecido (estado, vibrar ID, apelido ID NOME, ignorar ID sim|nao, recarregar, assinar)\"}");
	}
	g_strfreev(p);
	return r;
}

static gboolean chegou(GSocketService *s, GSocketConnection *con, GObject *fonte, gpointer dados) {
	GInputStream *in = g_io_stream_get_input_stream(G_IO_STREAM(con));
	GOutputStream *out = g_io_stream_get_output_stream(G_IO_STREAM(con));
	g_socket_set_timeout(g_socket_connection_get_socket(con), 2);
	GDataInputStream *d = g_data_input_stream_new(in);
	char *linha = g_data_input_stream_read_line(d, NULL, NULL, NULL);
	if (linha && !strcmp(linha, "assinar")) {
		g_socket_set_timeout(g_socket_connection_get_socket(con), 0);
		g_ptr_array_add(assinantes, g_object_ref(out));
		g_object_ref(con); /* a conexão fica aberta enquanto estiver na lista */
		g_object_unref(d);
		g_free(linha);
		return TRUE;
	}
	if (linha) {
		char *r = atender(linha), *l = g_strdup_printf("%s\n", r);
		g_output_stream_write_all(out, l, strlen(l), NULL, NULL, NULL);
		g_free(l);
		g_free(r);
	}
	g_free(linha);
	g_object_unref(d);
	return FALSE;
}

static gboolean ao_sair(gpointer dados) {
	g_main_loop_quit(laco);
	return G_SOURCE_REMOVE;
}

int main(void) {
	char resp[64];
	if (gamepadc_pedido("estado", resp, sizeof(resp))) {
		fprintf(stderr, "lumna-gamepad: já está rodando\n");
		return 0;
	}
	signal(SIGPIPE, SIG_IGN);
	gp_cfg_ler(&cfg);
	assinantes = g_ptr_array_new_with_free_func(g_object_unref);
	vigiados = g_hash_table_new(g_direct_hash, g_direct_equal);
	char *caminho = gamepadc_socket_caminho();
	unlink(caminho);
	GSocketService *srv = g_socket_service_new();
	GError *erro = NULL;
	GSocketAddress *end = g_unix_socket_address_new(caminho);
	if (!g_socket_listener_add_address(G_SOCKET_LISTENER(srv), end, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL, &erro)) {
		fprintf(stderr, "lumna-gamepad: não abriu %s: %s\n", caminho, erro->message);
		return 1;
	}
	g_signal_connect(srv, "incoming", G_CALLBACK(chegou), NULL);
	g_socket_service_start(srv);
	g_free(caminho);
	foco_rever();
	varrer();
	g_timeout_add(2000, varrer_timer, NULL);
	g_timeout_add(400, foco_timer, NULL);
	g_timeout_add(40, repetir, NULL);
	g_unix_signal_add(SIGTERM, ao_sair, NULL);
	g_unix_signal_add(SIGINT, ao_sair, NULL);
	laco = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(laco);
	return 0;
}
