#include "orbit.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wlr/util/log.h>

/*
 * orbit-msg: o Orbit atende num socket Unix ($ORBIT_SOCK, por omissão
 * $XDG_RUNTIME_DIR/orbit.$WAYLAND_DISPLAY.sock). Cada pedido é uma linha de texto e cada
 * resposta uma linha de JSON: {"ok":true,...} ou {"ok":false,"erro":"..."}.
 *
 *   saidas | get_outputs        as saídas
 *   janelas | get_tree          as janelas (com id)
 *   espacos | get_workspaces    o espaço activo e os ocupados de cada saída
 *   foco ID                     foca a janela (e mostra o espaço dela)
 *   fechar [ID]                 fecha a janela (sem ID, a focada)
 *   espaco N [SAIDA]            mostra o espaço N (1 até `areas`) na saída (sem ela, a do ponteiro)
 *   areas [N]                   quantos espaços o Desktop usa (1-3); o dos desligados vai para o último
 *   mover ID N                  leva a janela para o espaço N
 *   ecra [ID] | flutuar [ID]    ecrã inteiro / flutuante, alterna
 *   maximizar [ID]              maximizada (a área útil, sem a barra e a dock), alterna
 *   dividir                     liga ou desliga o par no telemóvel
 *   acao NOME                   uma acção dos atalhos (terminal, captura, mestre_mais...)
 *   exec COMANDO                corre um comando
 *   toque baixo|move|cima DEDO X Y   um toque de mentira, em coordenadas do layout (testes)
 *   subscrever                  fica ligado e recebe os eventos, um JSON por linha:
 *                               janela_aberta, janela_fechada, foco, espaco, areas, modo, titulo
 */

#define IPC_MAX_LINE 1024
#define IPC_MAX_CLIENTS 32

struct ipc_client {
	int fd;
	struct wl_event_source *source;
	char buf[IPC_MAX_LINE];
	size_t len;
	bool subscribed;
};

struct ipc_buf {
	char *data;
	size_t len, cap;
};

static struct {
	struct orbit_server *server;
	int fd;
	char path[108];
	struct ipc_client *clients[IPC_MAX_CLIENTS];
	/* o último estado anunciado, para os eventos de foco e espaço */
	uint32_t focused_id;
	int tags[16];
	int modes[16];
	char tag_names[16][32];
	int ntags;
	int areas;
} ipc = { .fd = -1 };

static void put(struct ipc_buf *b, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int need = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (need < 0) {
		return;
	}
	if (b->len + need + 1 > b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 512;
		while (cap < b->len + need + 1) {
			cap *= 2;
		}
		char *data = realloc(b->data, cap);
		if (!data) {
			return;
		}
		b->data = data;
		b->cap = cap;
	}
	va_start(ap, fmt);
	vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap);
	va_end(ap);
	b->len += need;
}

static void put_str(struct ipc_buf *b, const char *s) {
	put(b, "\"");
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		if (*p == '"' || *p == '\\') {
			put(b, "\\%c", *p);
		} else if (*p == '\n') {
			put(b, "\\n");
		} else if (*p < 0x20) {
			put(b, "\\u%04x", *p);
		} else {
			put(b, "%c", *p);
		}
	}
	put(b, "\"");
}

static void client_close(struct ipc_client *client) {
	for (int i = 0; i < IPC_MAX_CLIENTS; i++) {
		if (ipc.clients[i] == client) {
			ipc.clients[i] = NULL;
		}
	}
	wl_event_source_remove(client->source);
	close(client->fd);
	free(client);
}

static bool client_send(struct ipc_client *client, struct ipc_buf *b) {
	put(b, "\n");
	size_t off = 0;
	while (off < b->len) {
		ssize_t n = write(client->fd, b->data + off, b->len - off);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			return false;
		}
		off += n;
	}
	return true;
}

static void put_view(struct ipc_buf *b, struct orbit_view *view) {
	struct orbit_server *server = view->server;
	put(b, "{\"id\":%u,\"app_id\":", view->id);
	put_str(b, view->toplevel->app_id);
	put(b, ",\"titulo\":");
	put_str(b, view->toplevel->title);
	put(b, ",\"saida\":");
	put_str(b, view->output ? view->output->wlr->name : "");
	put(b, ",\"espaco\":%d,\"foco\":%s,\"flutuante\":%s,\"ecra\":%s,\"maximizada\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
		view->tag + 1, server->focused == view ? "true" : "false", view->floating ? "true" : "false",
		view->fullscreen ? "true" : "false", view->maximized ? "true" : "false", view->x, view->y, view->w, view->h);
}

static struct orbit_view *view_by_id(uint32_t id) {
	struct orbit_view *view;
	wl_list_for_each(view, &ipc.server->views, link) {
		if (view->mapped && view->id == id) {
			return view;
		}
	}
	return NULL;
}

static struct orbit_output *output_by_name(const char *name) {
	struct orbit_output *output;
	wl_list_for_each(output, &ipc.server->outputs, link) {
		if (strcmp(output->wlr->name, name) == 0) {
			return output;
		}
	}
	return NULL;
}

/* A janela pedida (ID) ou, sem ele, a focada. */
static struct orbit_view *target(const char *arg) {
	if (arg && *arg) {
		return view_by_id((uint32_t)strtoul(arg, NULL, 10));
	}
	struct orbit_view *focused = ipc.server->focused;
	return focused && focused->mapped ? focused : NULL;
}

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void fail(struct ipc_buf *b, const char *why) {
	b->len = 0;
	put(b, "{\"ok\":false,\"erro\":");
	put_str(b, why);
	put(b, "}");
}

static void handle(struct ipc_client *client, char *line, struct ipc_buf *b) {
	struct orbit_server *server = ipc.server;
	char *rest = NULL;
	char *cmd = strtok_r(line, " \t", &rest);
	if (!cmd) {
		fail(b, "pedido vazio");
		return;
	}
	/* orbit-msg -t TIPO manda "-t TIPO": o -t não conta */
	if (strcmp(cmd, "-t") == 0) {
		cmd = strtok_r(NULL, " \t", &rest);
		if (!cmd) {
			fail(b, "falta o tipo depois de -t");
			return;
		}
	}
	while (rest && (*rest == ' ' || *rest == '\t')) {
		rest++;
	}
	char *arg1 = NULL, *arg2 = NULL;
	char args[IPC_MAX_LINE];
	snprintf(args, sizeof(args), "%s", rest ? rest : "");
	char *save = NULL;
	arg1 = strtok_r(args, " \t", &save);
	arg2 = arg1 ? strtok_r(NULL, " \t", &save) : NULL;

	if (!strcmp(cmd, "saidas") || !strcmp(cmd, "get_outputs")) {
		put(b, "{\"ok\":true,\"saidas\":[");
		struct orbit_output *output;
		bool first = true;
		struct orbit_output *under = cursor_output(server);
		wl_list_for_each_reverse(output, &server->outputs, link) {
			struct wlr_box box;
			wlr_output_layout_get_box(server->layout, output->wlr, &box);
			int hz = output->wlr->refresh > 0 ? (output->wlr->refresh + 500) / 1000 : 0;
			/* a área útil: o que sobra depois da barra e da dock (relativa à saída) */
			struct wlr_box util = orbit_layer_usable(output, &box);
			put(b, "%s{\"nome\":", first ? "" : ",");
			put_str(b, output->wlr->name);
			put(b, ",\"largura\":%d,\"altura\":%d,\"escala\":%.2f,\"hz\":%d,\"x\":%d,\"y\":%d,"
				"\"espaco\":%d,\"telemovel\":%s,\"ponteiro\":%s,"
				"\"util\":{\"x\":%d,\"y\":%d,\"largura\":%d,\"altura\":%d}}",
				output->wlr->width, output->wlr->height, output->wlr->scale, hz, box.x, box.y,
				output->tag + 1, output_is_phone(output->wlr) ? "true" : "false",
				output == under ? "true" : "false", util.x - box.x, util.y - box.y, util.width, util.height);
			first = false;
		}
		put(b, "]}");
	} else if (!strcmp(cmd, "janelas") || !strcmp(cmd, "get_tree") || !strcmp(cmd, "get_views")) {
		put(b, "{\"ok\":true,\"janelas\":[");
		struct orbit_view *view;
		bool first = true;
		wl_list_for_each(view, &server->views, link) {
			if (!view->mapped) {
				continue;
			}
			put(b, first ? "" : ",");
			put_view(b, view);
			first = false;
		}
		put(b, "]}");
	} else if (!strcmp(cmd, "espacos") || !strcmp(cmd, "get_workspaces")) {
		put(b, "{\"ok\":true,\"areas\":%d,\"espacos\":[", server->areas);
		struct orbit_output *output;
		bool first = true;
		wl_list_for_each_reverse(output, &server->outputs, link) {
			bool used[ORBIT_TAGS] = { false };
			struct orbit_view *view;
			wl_list_for_each(view, &server->views, link) {
				if (view->mapped && view->output == output && view->tag >= 0 && view->tag < ORBIT_TAGS) {
					used[view->tag] = true;
				}
			}
			put(b, "%s{\"saida\":", first ? "" : ",");
			put_str(b, output->wlr->name);
			put(b, ",\"activo\":%d,\"ocupados\":[", output->tag + 1);
			bool f = true;
			for (int t = 0; t < ORBIT_TAGS; t++) {
				if (used[t]) {
					put(b, "%s%d", f ? "" : ",", t + 1);
					f = false;
				}
			}
			put(b, "]}");
			first = false;
		}
		put(b, "]}");
	} else if (!strcmp(cmd, "foco")) {
		struct orbit_view *view = target(arg1);
		if (!view || !arg1) {
			fail(b, "janela não encontrada");
			return;
		}
		if (view->output) {
			view->output->tag = view->tag;
		}
		orbit_arrange(server);
		orbit_focus(view);
		orbit_arrange(server);
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "fechar")) {
		struct orbit_view *view = target(arg1);
		if (!view) {
			fail(b, "janela não encontrada");
			return;
		}
		wlr_xdg_toplevel_send_close(view->toplevel);
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "modo")) {
		/* modo [seguinte|anterior|desktop|gaming|midia [saida]] */
		if (arg1) {
			struct orbit_output *output = arg2 ? output_by_name(arg2) : cursor_output(server);
			if (!output) {
				fail(b, "saída não encontrada");
				return;
			}
			if (!strcmp(arg1, "seguinte") || !strcmp(arg1, "anterior")) {
				orbit_mode_set(server, output, (output->mode + (arg1[0] == 's' ? 1 : ORBIT_MODES - 1)) % ORBIT_MODES);
			} else {
				int m = orbit_mode_parse(arg1);
				if (m < 0) {
					fail(b, "uso: modo [seguinte|anterior|desktop|gaming|midia [saida]]");
					return;
				}
				orbit_mode_set(server, output, m);
			}
		}
		struct orbit_output *output;
		put(b, "{\"ok\":true,\"layout\":\"%s\",\"saidas\":[", server->mode_extend ? "estender" : "tela");
		bool first = true;
		struct orbit_output *under = cursor_output(server);
		wl_list_for_each_reverse(output, &server->outputs, link) {
			put(b, "%s{\"saida\":", first ? "" : ",");
			put_str(b, output->wlr->name);
			put(b, ",\"modo\":\"%s\",\"ponteiro\":%s}", orbit_mode_name(output->mode), output == under ? "true" : "false");
			first = false;
		}
		put(b, "]}");
	} else if (!strcmp(cmd, "modo_layout")) {
		if (!arg1 || (strcmp(arg1, "tela") && strcmp(arg1, "estender"))) {
			fail(b, "uso: modo_layout tela|estender");
			return;
		}
		orbit_mode_set_extend(server, !strcmp(arg1, "estender"));
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "areas")) {
		/* areas [1-3]: quantos espaços o Desktop usa; o que estava nos desligados passa para o último */
		if (arg1) {
			int n = atoi(arg1);
			if (n < 1 || n > ORBIT_TAGS) {
				fail(b, "uso: areas [1-3]");
				return;
			}
			orbit_areas_set(server, n);
		}
		put(b, "{\"ok\":true,\"areas\":%d,\"maximo\":%d}", server->areas, ORBIT_TAGS);
	} else if (!strcmp(cmd, "espaco")) {
		int n = arg1 ? atoi(arg1) : 0;
		struct orbit_output *output = arg2 ? output_by_name(arg2) : cursor_output(server);
		if (n < 1 || n > server->areas || !output) {
			fail(b, "uso: espaco 1-N [saida] (N = espaços ligados, ver `areas`)");
			return;
		}
		output->tag = n - 1;
		orbit_arrange(server);
		focus_top(server, output);
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "mover")) {
		struct orbit_view *view = arg1 ? view_by_id((uint32_t)strtoul(arg1, NULL, 10)) : NULL;
		int n = arg2 ? atoi(arg2) : 0;
		if (!view || n < 1 || n > server->areas) {
			fail(b, "uso: mover ID 1-N");
			return;
		}
		orbit_split_release(server, view);
		view->tag = n - 1;
		view->fullscreen = false;
		orbit_arrange(server);
		if (view->output) {
			focus_top(server, view->output);
		}
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "maximizar")) {
		struct orbit_view *view = target(arg1);
		if (!view) {
			fail(b, "janela não encontrada");
			return;
		}
		orbit_view_set_maximized(view, !view->maximized);
		put(b, "{\"ok\":true,\"maximizada\":%s}", view->maximized ? "true" : "false");
	} else if (!strcmp(cmd, "ecra") || !strcmp(cmd, "flutuar")) {
		struct orbit_view *view = target(arg1);
		if (!view) {
			fail(b, "janela não encontrada");
			return;
		}
		if (!strcmp(cmd, "ecra")) {
			view->fullscreen = !view->fullscreen;
			if (view->fullscreen) {
				view->floating = false;
			}
		} else {
			orbit_split_release(server, view);
			view->floating = !view->floating;
			view->fullscreen = false;
		}
		orbit_arrange(server);
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "dividir")) {
		orbit_action_named(server, "dividir");
		put(b, "{\"ok\":true,\"dividido\":%s}", server->phone_split ? "true" : "false");
	} else if (!strcmp(cmd, "atalhos")) {
		/* atalhos pausar|retomar: o Painel grava um atalho novo (Alt+Tab tem de chegar a ele) */
		if (!arg1 || (strcmp(arg1, "pausar") && strcmp(arg1, "retomar"))) {
			fail(b, "atalhos pausar|retomar");
			return;
		}
		orbit_binds_pause(server, !strcmp(arg1, "pausar"));
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "acao")) {
		if (!arg1 || !orbit_action_named(server, arg1)) {
			fail(b, "acção desconhecida");
			return;
		}
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "exec")) {
		if (!rest || !*rest) {
			fail(b, "falta o comando");
			return;
		}
		if (fork() == 0) {
			setsid();
			execl("/bin/sh", "/bin/sh", "-c", rest, (char *)NULL);
			_exit(1);
		}
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "toque")) {
		/* toque baixo|move|cima DEDO X Y */
		char *kind = arg1;
		char *id = arg2;
		char *x = strtok_r(NULL, " \t", &save);
		char *y = strtok_r(NULL, " \t", &save);
		if (!kind || !id || (strcmp(kind, "cima") && (!x || !y))) {
			fail(b, "uso: toque baixo|move|cima DEDO X Y");
			return;
		}
		int32_t finger = atoi(id);
		uint32_t t = now_ms();
		if (!server->virtual_touch) {
			server->virtual_touch = true;
			orbit_seat_capabilities(server);
		}
		if (!strcmp(kind, "baixo")) {
			orbit_touch_down(server, t, finger, atof(x), atof(y));
		} else if (!strcmp(kind, "move")) {
			orbit_touch_motion(server, t, finger, atof(x), atof(y));
		} else if (!strcmp(kind, "cima")) {
			orbit_touch_up(server, t, finger);
		} else {
			fail(b, "uso: toque baixo|move|cima DEDO X Y");
			return;
		}
		orbit_touch_frame(server);
		put(b, "{\"ok\":true}");
	} else if (!strcmp(cmd, "subscrever") || !strcmp(cmd, "subscribe")) {
		client->subscribed = true;
		put(b, "{\"ok\":true}");
	} else {
		fail(b, "pedido desconhecido (saidas, janelas, espacos, foco, fechar, espaco, mover, "
			"ecra, maximizar, flutuar, dividir, acao, exec, toque, subscrever)");
	}
}

static int client_readable(int fd, uint32_t mask, void *data) {
	struct ipc_client *client = data;
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		client_close(client);
		return 0;
	}
	ssize_t n = read(fd, client->buf + client->len, sizeof(client->buf) - client->len - 1);
	if (n <= 0) {
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
			return 0;
		}
		client_close(client);
		return 0;
	}
	client->len += n;
	client->buf[client->len] = '\0';
	char *start = client->buf;
	char *nl;
	while ((nl = strchr(start, '\n'))) {
		*nl = '\0';
		struct ipc_buf b = { 0 };
		handle(client, start, &b);
		bool ok = client_send(client, &b);
		free(b.data);
		start = nl + 1;
		if (!ok) {
			client_close(client);
			return 0;
		}
	}
	size_t left = client->len - (start - client->buf);
	if (left >= sizeof(client->buf) - 1) {
		wlr_log(WLR_ERROR, "orbit-msg: pedido longo demais, ligação fechada");
		client_close(client);
		return 0;
	}
	memmove(client->buf, start, left);
	client->len = left;
	return 0;
}

static int listener_readable(int fd, uint32_t mask, void *data) {
	int cfd = accept(fd, NULL, NULL);
	if (cfd < 0) {
		return 0;
	}
	fcntl(cfd, F_SETFD, FD_CLOEXEC);
	int slot = -1;
	for (int i = 0; i < IPC_MAX_CLIENTS; i++) {
		if (!ipc.clients[i]) {
			slot = i;
			break;
		}
	}
	struct ipc_client *client = slot >= 0 ? calloc(1, sizeof(*client)) : NULL;
	if (!client) {
		close(cfd);
		return 0;
	}
	client->fd = cfd;
	client->source = wl_event_loop_add_fd(wl_display_get_event_loop(ipc.server->display), cfd,
		WL_EVENT_READABLE, client_readable, client);
	if (!client->source) {
		close(cfd);
		free(client);
		return 0;
	}
	ipc.clients[slot] = client;
	return 0;
}

static void broadcast(struct ipc_buf *b) {
	put(b, "\n");
	for (int i = 0; i < IPC_MAX_CLIENTS; i++) {
		struct ipc_client *client = ipc.clients[i];
		if (!client || !client->subscribed) {
			continue;
		}
		/* send() com MSG_DONTWAIT: um assinante lento perde o evento, não pára o Orbit */
		if (send(client->fd, b->data, b->len, MSG_DONTWAIT | MSG_NOSIGNAL) < 0 && errno != EAGAIN) {
			client_close(client);
		}
	}
}

static bool any_subscriber(void) {
	for (int i = 0; i < IPC_MAX_CLIENTS; i++) {
		if (ipc.clients[i] && ipc.clients[i]->subscribed) {
			return true;
		}
	}
	return false;
}

void orbit_ipc_view_event(struct orbit_view *view, const char *event) {
	if (ipc.fd < 0 || !any_subscriber()) {
		return;
	}
	struct ipc_buf b = { 0 };
	put(&b, "{\"evento\":");
	put_str(&b, event);
	put(&b, ",\"janela\":");
	put_view(&b, view);
	put(&b, "}");
	broadcast(&b);
	free(b.data);
}

/* Depois de cada arranjo: avisa o que mudou no foco e nos espaços. */
void orbit_ipc_sync(struct orbit_server *server) {
	if (ipc.fd < 0) {
		return;
	}
	bool listening = any_subscriber();
	if (ipc.areas != server->areas) {
		ipc.areas = server->areas;
		if (listening) {
			struct ipc_buf b = { 0 };
			put(&b, "{\"evento\":\"areas\",\"areas\":%d}", server->areas);
			broadcast(&b);
			free(b.data);
		}
	}
	uint32_t focused = server->focused && server->focused->mapped ? server->focused->id : 0;
	if (focused != ipc.focused_id) {
		ipc.focused_id = focused;
		if (listening) {
			struct ipc_buf b = { 0 };
			if (focused) {
				put(&b, "{\"evento\":\"foco\",\"janela\":");
				put_view(&b, server->focused);
				put(&b, "}");
			} else {
				put(&b, "{\"evento\":\"foco\",\"janela\":null}");
			}
			broadcast(&b);
			free(b.data);
		}
	}
	orbit_mode_follow(server);
	struct orbit_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		int i;
		for (i = 0; i < ipc.ntags; i++) {
			if (!strcmp(ipc.tag_names[i], output->wlr->name)) {
				break;
			}
		}
		if (i == ipc.ntags) {
			if (ipc.ntags >= 16) {
				continue;
			}
			snprintf(ipc.tag_names[i], sizeof(ipc.tag_names[i]), "%s", output->wlr->name);
			ipc.tags[i] = -1;
			ipc.modes[i] = -1;
			ipc.ntags++;
		}
		if (ipc.modes[i] != output->mode) {
			ipc.modes[i] = output->mode;
			if (listening) {
				struct ipc_buf b = { 0 };
				put(&b, "{\"evento\":\"modo\",\"saida\":");
				put_str(&b, output->wlr->name);
				put(&b, ",\"modo\":\"%s\",\"layout\":\"%s\"}", orbit_mode_name(output->mode), server->mode_extend ? "estender" : "tela");
				broadcast(&b);
				free(b.data);
			}
		}
		if (ipc.tags[i] == output->tag) {
			continue;
		}
		ipc.tags[i] = output->tag;
		if (listening) {
			struct ipc_buf b = { 0 };
			put(&b, "{\"evento\":\"espaco\",\"saida\":");
			put_str(&b, output->wlr->name);
			put(&b, ",\"espaco\":%d}", output->tag + 1);
			broadcast(&b);
			free(b.data);
		}
	}
}

bool orbit_ipc_bind(struct orbit_server *server, const char *wayland_display) {
	ipc.server = server;
	const char *path = getenv("ORBIT_SOCK");
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (path && *path) {
		snprintf(ipc.path, sizeof(ipc.path), "%s", path);
	} else if (runtime) {
		snprintf(ipc.path, sizeof(ipc.path), "%s/orbit.%s.sock", runtime, wayland_display);
	} else {
		wlr_log(WLR_ERROR, "orbit-msg: sem XDG_RUNTIME_DIR, o socket fica desligado");
		return false;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		wlr_log(WLR_ERROR, "orbit-msg: socket falhou");
		return false;
	}
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ipc.path);
	unlink(ipc.path);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 8) < 0) {
		wlr_log(WLR_ERROR, "orbit-msg: não deu para abrir %s", ipc.path);
		close(fd);
		return false;
	}
	if (!wl_event_loop_add_fd(wl_display_get_event_loop(server->display), fd, WL_EVENT_READABLE,
			listener_readable, NULL)) {
		close(fd);
		return false;
	}
	ipc.fd = fd;
	setenv("ORBIT_SOCK", ipc.path, 1);
	wlr_log(WLR_INFO, "orbit-msg em %s", ipc.path);
	return true;
}

void orbit_ipc_finish(void) {
	if (ipc.fd >= 0) {
		close(ipc.fd);
		unlink(ipc.path);
		ipc.fd = -1;
	}
}
