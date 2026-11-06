/*
 * lumna-connect: o controle do LUMNA pelo celular (o app LUMNA Connect). Um serviço da sessão, à
 * parte do shell e dos apps de Mídia: roda o tempo todo, então o controle não para quando um serviço
 * (Netflix, YouTube...) abre por cima, que era o defeito da primeira versão.
 *
 * Protocolo: o mesmo de antes (WebSocket na porta 47800, um JSON por quadro, anunciado por mDNS
 * como _lumna-remote._tcp). Do celular: pair, hello e, depois de entrar, pad, text, key, media,
 * volume, mode, monitors e home. Pareamento: um código de 6 dígitos que vale 5 minutos e uma vez,
 * pedido pelo shell da Mídia (ou pelo `lumna-connect parear`); o celular recebe um token, guardado
 * nos dois lados em ~/.config/lumna/remotes.json (só o dono lê).
 *
 * Voz: {"type":"voice","text":"..."} é o que o celular entendeu; vai para a Emma (lumna-emma), que age e responde
 * depois com {"type":"voice_reply","ok":..,"text":"..."}. {"type":"voice","action":"listen"} liga o microfone do PC.
 *
 * Dois caminhos, que não dependem de qual janela está na frente:
 *   Sistema   volume, modo, Home, desligar: falam com o Orbit e com o som, direto.
 *   Serviço   setas, OK, voltar, texto, play, seek e o mouse do celular: entram como teclado e
 *             ponteiro virtuais do Orbit, então valem em qualquer site ou app, sem eles saberem.
 *             O play/pausa e o avanço vão por MPRIS quando há player.
 *
 * Os outros programas pedem coisas a ele pelo socket $XDG_RUNTIME_DIR/lumna-connect.sock, uma
 * linha por pedido e um JSON de resposta; `lumna-connect <pedido>` faz o pedido na linha de comando:
 *   estado                     porta, celulares pareados e quais estão conectados
 *   parear                     gera um código novo (o anterior deixa de valer)
 *   parear_cancelar            apaga o código
 *   esquecer ID | todos        tira um celular (ou todos) e derruba a conexão
 *
 * Connect (o dia a dia entre o PC e o celular, no mesmo protocolo de antes): clipboard (o que se copia num vai pro outro), notify /
 * notification (os avisos do celular aparecem aqui e os daqui no celular), arquivos (file-begin/chunk/end do celular para cá; file-offer daqui
 * e o celular puxa com file-pull) e features (o que está ligado para cada celular, em Painel > LUMNA Connect). Pelo socket:
 *   connect                    o que o Painel mostra: ativo, pasta, celulares (com os recursos), transferências recentes
 *   connect_ativo sim|nao      liga/desliga o Connect inteiro (desligado, nada sai nem entra)
 *   connect_pasta CAMINHO      onde ficam os arquivos recebidos ("" volta a Downloads)
 *   recurso ID NOME sim|nao    liga/desliga um recurso de um celular (notifications, phone_notifications, clipboard, files, control)
 *   enviar ID CAMINHO          oferece um arquivo ao celular (ele puxa)
 *   copiar_para ID             manda ao celular o texto que está copiado
 *   aviso JSON                 {"app","summary","body","urgent"}: o lumna-avisos manda os avisos daqui para os celulares
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <errno.h>
#include <glib.h>
#include <libsoup/soup.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include "agenda.h"
#include "emmac.h"
#include "energia.h"
#include "json.h"
#include "mdns.h"
#include "modos.h"
#include "midiac.h"
#include "mpris.h"
#include "ponteiro.h"

#define PORTA 47800
#define SERVICO "_lumna-remote._tcp"
#define CODIGO_VALE_US (5 * 60 * G_USEC_PER_SEC)
#define APP_ID_SHELL "lumna-midia-inicio" /* o app_id da janela de Início da Mídia */

/* ---- celulares pareados ---- */

struct dispositivo {
	char id[40];
	char nome[48];
	char token[72];
	char *recursos; /* o objeto "features" como veio (os recursos do Connect), ou NULL */
};

static GPtrArray *dispositivos;
static char codigo[8];
static gint64 codigo_ate;

static char *caminho_dispositivos(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "remotes.json", NULL);
}

static void dispositivo_liberar(gpointer p) {
	struct dispositivo *d = p;
	g_free(d->recursos);
	g_free(d);
}

static void gerar_bytes(void *buf, size_t n) {
	if (getrandom(buf, n, 0) != (ssize_t)n) {
		g_error("sem entropia");
	}
}

static void novo_texto_aleatorio(char *saida, size_t bytes) {
	uint8_t b[32];
	gerar_bytes(b, bytes);
	for (size_t i = 0; i < bytes; i++) {
		sprintf(saida + i * 2, "%02x", b[i]);
	}
}

static void dispositivos_ler(void) {
	if (dispositivos) {
		g_ptr_array_free(dispositivos, TRUE);
	}
	dispositivos = g_ptr_array_new_with_free_func(dispositivo_liberar);
	char *caminho = caminho_dispositivos(), *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		struct json *lista = json_ler(texto);
		for (int i = 0; lista && i < lista->nfilhos; i++) {
			const struct json *j = lista->filhos[i];
			const char *token = json_chave_texto(j, "token");
			if (!*token) {
				continue;
			}
			struct dispositivo *d = g_new0(struct dispositivo, 1);
			g_strlcpy(d->token, token, sizeof(d->token));
			g_strlcpy(d->nome, json_chave_texto(j, "name"), sizeof(d->nome));
			g_strlcpy(d->id, json_chave_texto(j, "id"), sizeof(d->id));
			if (!d->id[0]) {
				novo_texto_aleatorio(d->id, 8); /* pareamentos antigos não tinham id */
			}
			const struct json *rec = json_chave(j, "features");
			if (rec && rec->tipo == JSON_OBJETO) {
				GString *s = g_string_new(NULL);
				json_escrever(s, rec);
				d->recursos = g_string_free(s, FALSE);
			}
			g_ptr_array_add(dispositivos, d);
		}
		json_liberar(lista);
		g_free(texto);
	}
	g_free(caminho);
}

/* os tokens dão controle da máquina: o arquivo nasce 0600 */
static bool dispositivos_gravar(void) {
	GString *s = g_string_new("[");
	for (guint i = 0; i < dispositivos->len; i++) {
		struct dispositivo *d = dispositivos->pdata[i];
		g_string_append(s, i ? ",\n  {" : "\n  {");
		g_string_append(s, "\"id\":");
		json_escrever_texto(s, d->id);
		g_string_append(s, ",\"name\":");
		json_escrever_texto(s, d->nome);
		g_string_append(s, ",\"token\":");
		json_escrever_texto(s, d->token);
		if (d->recursos) {
			g_string_append_printf(s, ",\"features\":%s", d->recursos);
		}
		g_string_append_c(s, '}');
	}
	g_string_append(s, dispositivos->len ? "\n]\n" : "]\n");
	char *caminho = caminho_dispositivos();
	char *dir = g_path_get_dirname(caminho);
	g_mkdir_with_parents(dir, 0700);
	char *tmp = g_strdup_printf("%s.novo", caminho);
	bool ok = false;
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd >= 0) {
		ok = write(fd, s->str, s->len) == (ssize_t)s->len;
		ok = close(fd) == 0 && ok;
		ok = ok && rename(tmp, caminho) == 0;
	}
	g_free(tmp);
	g_free(dir);
	g_free(caminho);
	g_string_free(s, TRUE);
	return ok;
}

/* em tempo constante: não vaza quantos dígitos acertou */
static bool igual(const char *a, const char *b) {
	size_t n = strlen(a);
	if (n != strlen(b)) {
		return false;
	}
	unsigned char r = 0;
	for (size_t i = 0; i < n; i++) {
		r |= (unsigned char)(a[i] ^ b[i]);
	}
	return r == 0;
}

static struct dispositivo *dispositivo_por_token(const char *token) {
	if (!*token) {
		return NULL;
	}
	for (guint i = 0; i < dispositivos->len; i++) {
		struct dispositivo *d = dispositivos->pdata[i];
		if (igual(d->token, token)) {
			return d;
		}
	}
	return NULL;
}

/* ---- conexões ---- */

struct cliente {
	SoupWebsocketConnection *ws;
	char dispositivo[40]; /* vazio até se identificar */
};

static GPtrArray *clientes;

static void enviar(SoupWebsocketConnection *ws, const char *texto) {
	if (soup_websocket_connection_get_state(ws) == SOUP_WEBSOCKET_STATE_OPEN) {
		soup_websocket_connection_send_text(ws, texto);
	}
}

static void erro(GString *r, const char *mensagem, bool sem_par) {
	g_string_assign(r, "{\"type\":\"error\",\"message\":");
	json_escrever_texto(r, mensagem);
	if (sem_par) {
		g_string_append(r, ",\"unpaired\":true");
	}
	g_string_append_c(r, '}');
}

static void ok(GString *r) {
	g_string_assign(r, "{\"type\":\"ok\"}");
}

static void transmitir(const char *texto) {
	for (guint i = 0; i < clientes->len; i++) {
		struct cliente *c = clientes->pdata[i];
		if (c->dispositivo[0]) {
			enviar(c->ws, texto);
		}
	}
}

/* ---- Connect: o dia a dia entre o PC e o celular ---- */

#define CHUNK (192 * 1024)
#define CLIP_MAX (256 * 1024)
#define MAX_ARQUIVO ((guint64)20 << 30)

static struct {
	bool ativo;
	char pasta[512];
} conf = { true, "" };

static bool conectado(const char *id);

static char *caminho_conf(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "connect.json", NULL);
}

static void conf_ler(void) {
	char *c = caminho_conf(), *t = NULL;
	if (g_file_get_contents(c, &t, NULL, NULL)) {
		struct json *j = json_ler(t);
		conf.ativo = json_bool(json_chave(j, "ativo"), true);
		g_strlcpy(conf.pasta, json_chave_texto(j, "pasta"), sizeof(conf.pasta));
		json_liberar(j);
	}
	g_free(t);
	g_free(c);
}

static void conf_gravar(void) {
	char *c = caminho_conf(), *dir = g_path_get_dirname(c);
	g_mkdir_with_parents(dir, 0700);
	GString *g = g_string_new("{\"ativo\":");
	g_string_append(g, conf.ativo ? "true" : "false");
	g_string_append(g, ",\"pasta\":");
	json_escrever_texto(g, conf.pasta);
	g_string_append_c(g, '}');
	g_file_set_contents(c, g->str, -1, NULL);
	g_string_free(g, TRUE);
	g_free(dir);
	g_free(c);
}

/* onde caem os arquivos que chegam (Downloads, ou a pasta que a pessoa escolheu; "~" vale a pessoal); g_free */
static char *pasta_receber(void) {
	if (conf.pasta[0]) {
		return conf.pasta[0] == '~' ? g_build_filename(g_get_home_dir(), conf.pasta + (conf.pasta[1] == '/' ? 2 : 1), NULL) : g_strdup(conf.pasta);
	}
	const char *d = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
	return d ? g_strdup(d) : g_build_filename(g_get_home_dir(), "Downloads", NULL);
}

static const char *RECURSOS[] = { "notifications", "phone_notifications", "clipboard", "files", "control", NULL };

static bool recurso_de(const struct dispositivo *d, const char *nome) {
	if (!conf.ativo) {
		return false; /* o interruptor geral: desligado, nada sai nem entra */
	}
	if (!d || !d->recursos) {
		return true;
	}
	struct json *j = json_ler(d->recursos);
	bool v = json_bool(json_chave(j, nome), true);
	json_liberar(j);
	return v;
}

static void recursos_escrever(const struct dispositivo *d, GString *g) {
	g_string_append_c(g, '{');
	for (int i = 0; RECURSOS[i]; i++) {
		bool v = recurso_de(d, RECURSOS[i]);
		if (!conf.ativo) { /* o que o celular vê: o do aparelho, não o do interruptor geral */
			struct json *j = d->recursos ? json_ler(d->recursos) : NULL;
			v = json_bool(json_chave(j, RECURSOS[i]), true);
			json_liberar(j);
		}
		g_string_append_printf(g, "%s\"%s\":%s", i ? "," : "", RECURSOS[i], v ? "true" : "false");
	}
	g_string_append_c(g, '}');
}

static void recurso_definir(struct dispositivo *d, const char *nome, bool valor) {
	struct json *j = d->recursos ? json_ler(d->recursos) : NULL;
	GString *g = g_string_new("{");
	for (int i = 0; RECURSOS[i]; i++) {
		bool v = !strcmp(RECURSOS[i], nome) ? valor : json_bool(json_chave(j, RECURSOS[i]), true);
		g_string_append_printf(g, "%s\"%s\":%s", i ? "," : "", RECURSOS[i], v ? "true" : "false");
	}
	g_string_append_c(g, '}');
	json_liberar(j);
	g_free(d->recursos);
	d->recursos = g_string_free(g, FALSE);
}

static bool recurso_existe(const char *nome) {
	for (int i = 0; RECURSOS[i]; i++) {
		if (!strcmp(RECURSOS[i], nome)) {
			return true;
		}
	}
	return false;
}

static struct dispositivo *dispositivo_por_id(const char *id) {
	for (guint i = 0; i < dispositivos->len; i++) {
		struct dispositivo *d = dispositivos->pdata[i];
		if (!strcmp(d->id, id)) {
			return d;
		}
	}
	return NULL;
}

/* manda a um celular (por todas as conexões dele) */
static bool enviar_ao(const char *id, const char *json) {
	bool foi = false;
	for (guint i = 0; i < clientes->len; i++) {
		struct cliente *c = clientes->pdata[i];
		if (!strcmp(c->dispositivo, id)) {
			enviar(c->ws, json);
			foi = true;
		}
	}
	return foi;
}

/* manda a todos os celulares conectados que têm este recurso ligado */
static void transmitir_recurso(const char *recurso, const char *json) {
	for (guint i = 0; i < clientes->len; i++) {
		struct cliente *c = clientes->pdata[i];
		if (c->dispositivo[0] && recurso_de(dispositivo_por_id(c->dispositivo), recurso)) {
			enviar(c->ws, json);
		}
	}
}

/* ---- transferências (a lista que o Painel mostra) ---- */

struct transf {
	char id[48], nome[200], par[48], dir[4]; /* "in" | "out" */
	guint64 feito, tamanho;
	char estado[10]; /* running | done | error */
	char erro[120];
};

static GPtrArray *transferencias;

static struct transf *transf_nova(const char *id, const char *nome, const char *par, const char *dir, guint64 tamanho) {
	if (!transferencias) {
		transferencias = g_ptr_array_new_with_free_func(g_free);
	}
	if (transferencias->len >= 20) { /* as 20 mais recentes */
		g_ptr_array_remove_index(transferencias, 0);
	}
	struct transf *t = g_new0(struct transf, 1);
	g_strlcpy(t->id, id, sizeof(t->id));
	g_strlcpy(t->nome, nome, sizeof(t->nome));
	g_strlcpy(t->par, par, sizeof(t->par));
	g_strlcpy(t->dir, dir, sizeof(t->dir));
	g_strlcpy(t->estado, "running", sizeof(t->estado));
	t->tamanho = tamanho;
	g_ptr_array_add(transferencias, t);
	return t;
}

static void transf_erro(struct transf *t, const char *motivo) {
	if (t) {
		g_strlcpy(t->estado, "error", sizeof(t->estado));
		g_strlcpy(t->erro, motivo, sizeof(t->erro));
	}
}

/* um aviso aqui no PC, marcado para o lumna-avisos não o devolver aos celulares */
static void aviso_local(const char *app, const char *titulo, const char *corpo, bool urgente) {
	char *argv[] = { "notify-send", "-a", (char *)app, "-u", urgente ? "critical" : "normal", "-h", "string:x-lumna-connect:1", (char *)titulo, (char *)corpo, NULL };
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

/* ---- a área de transferência ---- */

static char *ultimo_clip; /* o último texto que passou (daqui ou de lá): quem já foi não volta (sem eco) */
static GPid clip_pid;
static bool clip_primeiro;

static void clip_filho_saiu(GPid pid, gint st, gpointer d) {
	g_spawn_close_pid(pid);
}

static void clip_definir(const char *texto) {
	g_free(ultimo_clip);
	ultimo_clip = g_strdup(texto);
	char *argv[] = { "wl-copy", NULL };
	int entrada = -1;
	GPid pid;
	if (g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &pid, &entrada, NULL, NULL, NULL)) {
		ssize_t r = write(entrada, texto, strlen(texto));
		(void)r;
		close(entrada);
		g_child_watch_add(pid, clip_filho_saiu, NULL);
	}
}

static gboolean clip_lido(GIOChannel *ch, GIOCondition cond, gpointer d) {
	char *linha = NULL;
	gsize n = 0;
	GIOStatus st = g_io_channel_read_line(ch, &linha, &n, NULL, NULL);
	if (st != G_IO_STATUS_NORMAL) {
		g_free(linha);
		if (st == G_IO_STATUS_EOF || cond & (G_IO_HUP | G_IO_ERR)) {
			clip_pid = 0; /* o wl-paste saiu: o temporizador religa */
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}
	g_strstrip(linha);
	gsize tam = 0;
	guchar *dec = *linha ? g_base64_decode(linha, &tam) : NULL;
	g_free(linha);
	bool primeiro = clip_primeiro;
	clip_primeiro = false;
	if (dec && tam > 0 && tam <= CLIP_MAX && g_utf8_validate((char *)dec, (gssize)tam, NULL)) {
		char *texto = g_strndup((char *)dec, tam);
		if (g_strcmp0(texto, ultimo_clip)) {
			g_free(ultimo_clip);
			ultimo_clip = g_strdup(texto);
			if (!primeiro) { /* o que já estava copiado ao ligar não sai por aí */
				GString *m = g_string_new("{\"type\":\"clipboard\",\"text\":");
				json_escrever_texto(m, texto);
				g_string_append_c(m, '}');
				transmitir_recurso("clipboard", m->str);
				g_string_free(m, TRUE);
			}
		}
		g_free(texto);
	}
	g_free(dec);
	return G_SOURCE_CONTINUE;
}

static gboolean clip_ligar(gpointer d) {
	if (clip_pid || !g_getenv("WAYLAND_DISPLAY")) {
		return G_SOURCE_CONTINUE;
	}
	char *argv[] = { "wl-paste", "--type", "text", "--watch", "sh", "-c", "base64 -w0; echo", NULL };
	int saida = -1;
	if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &clip_pid, NULL, &saida, NULL, NULL)) {
		clip_pid = 0;
		return G_SOURCE_CONTINUE;
	}
	clip_primeiro = true;
	GIOChannel *ch = g_io_channel_unix_new(saida);
	g_io_channel_set_encoding(ch, NULL, NULL);
	g_io_channel_set_close_on_unref(ch, TRUE);
	g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, clip_lido, NULL);
	g_io_channel_unref(ch);
	return G_SOURCE_CONTINUE;
}

/* ---- arquivos que chegam do celular ---- */

struct recebendo {
	char chave[100]; /* dispositivo:id */
	char dispositivo[40], tid[48];
	char *parcial, *alvo;
	FILE *f;
	struct transf *t;
};

static GHashTable *recebendo;

static char *nome_seguro(const char *nome) {
	const char *b = strrchr(nome, '/');
	b = b ? b + 1 : nome;
	const char *c = strrchr(b, '\\');
	b = c ? c + 1 : b;
	GString *g = g_string_new(NULL);
	for (const char *p = b; *p && g->len < 200; p++) {
		if ((unsigned char)*p >= 32 && *p != 127) {
			g_string_append_c(g, *p);
		}
	}
	char *r = g_strstrip(g_string_free(g, FALSE));
	char *ini = r;
	while (*ini == '.') {
		ini++;
	}
	char *saida = *ini ? g_strdup(ini) : g_strdup("arquivo");
	g_free(r);
	return saida;
}

/* "foto.jpg" que já existe vira "foto (2).jpg" */
static char *caminho_livre(const char *pasta, const char *nome) {
	char *base = g_strdup(nome), *ponto = strrchr(base, '.');
	char *ext = ponto && ponto != base ? g_strdup(ponto) : g_strdup("");
	if (ponto && ponto != base) {
		*ponto = '\0';
	}
	char *r = NULL;
	for (int n = 1; n < 10000 && !r; n++) {
		char *nm = n == 1 ? g_strdup_printf("%s%s", base, ext) : g_strdup_printf("%s (%d)%s", base, n, ext);
		char *c = g_build_filename(pasta, nm, NULL), *p = g_strdup_printf("%s.part", c);
		if (!g_file_test(c, G_FILE_TEST_EXISTS) && !g_file_test(p, G_FILE_TEST_EXISTS)) {
			r = c;
			c = NULL;
		}
		g_free(c);
		g_free(p);
		g_free(nm);
	}
	g_free(base);
	g_free(ext);
	return r;
}

static void recebendo_liberar(gpointer p) {
	struct recebendo *r = p;
	if (r->f) {
		fclose(r->f);
	}
	g_free(r->parcial);
	g_free(r->alvo);
	g_free(r);
}

static void recebimento_cancelar(const char *chave, const char *motivo) {
	struct recebendo *r = recebendo ? g_hash_table_lookup(recebendo, chave) : NULL;
	if (r) {
		if (r->f) {
			fclose(r->f);
			r->f = NULL;
		}
		g_unlink(r->parcial);
		transf_erro(r->t, motivo);
		g_hash_table_remove(recebendo, chave);
	}
}

static void recebimentos_do_celular_cancelar(const char *dispositivo, const char *motivo) {
	if (!recebendo) {
		return;
	}
	GList *chaves = g_hash_table_get_keys(recebendo), *l;
	for (l = chaves; l; l = l->next) {
		struct recebendo *r = g_hash_table_lookup(recebendo, l->data);
		if (r && !strcmp(r->dispositivo, dispositivo)) {
			char *k = g_strdup(l->data);
			recebimento_cancelar(k, motivo);
			g_free(k);
		}
	}
	g_list_free(chaves);
}

/* ---- arquivos daqui para o celular (ele puxa) ---- */

struct oferta {
	char id[24], dispositivo[40];
	char *caminho;
	struct transf *t;
};

static GHashTable *ofertas;

static void oferta_liberar(gpointer p) {
	struct oferta *o = p;
	g_free(o->caminho);
	g_free(o);
}

static void ofertas_do_celular_falhar(const char *dispositivo, const char *motivo) {
	if (!ofertas) {
		return;
	}
	GHashTableIter it;
	gpointer k, v;
	g_hash_table_iter_init(&it, ofertas);
	while (g_hash_table_iter_next(&it, &k, &v)) {
		struct oferta *o = v;
		if (!strcmp(o->dispositivo, dispositivo)) {
			transf_erro(o->t, motivo);
			g_hash_table_iter_remove(&it);
		}
	}
}

/* o celular puxa um pedaço: {"type":"file-pull","id":..,"offset":N} -> file-data */
static void oferta_puxar(const struct json *msg, GString *r) {
	const char *id = json_chave_texto(msg, "id");
	struct oferta *o = ofertas ? g_hash_table_lookup(ofertas, id) : NULL;
	guint64 off = (guint64)json_numero(json_chave(msg, "offset"), 0);
	if (!o) {
		erro(r, "esse arquivo não está mais sendo oferecido", false);
		return;
	}
	FILE *f = fopen(o->caminho, "rb");
	if (!f) {
		transf_erro(o->t, "não consegui ler o arquivo");
		g_hash_table_remove(ofertas, id);
		erro(r, "não consegui ler o arquivo", false);
		return;
	}
	fseeko(f, 0, SEEK_END);
	guint64 tam = (guint64)ftello(f);
	fseeko(f, (off_t)off, SEEK_SET);
	guchar *buf = g_malloc(CHUNK);
	size_t n = off < tam ? fread(buf, 1, CHUNK, f) : 0;
	fclose(f);
	bool eof = off + n >= tam;
	char *b64 = g_base64_encode(buf, n);
	g_string_printf(r, "{\"type\":\"file-data\",\"data\":\"%s\",\"len\":%zu,\"eof\":%s}", b64, n, eof ? "true" : "false");
	g_free(b64);
	g_free(buf);
	o->t->feito = off + n;
	if (eof) {
		g_strlcpy(o->t->estado, "done", sizeof(o->t->estado));
		g_hash_table_remove(ofertas, id); /* o ponteiro o->t segue na lista de transferências */
	}
}

/* oferece um arquivo a um celular (o pedido "enviar ID CAMINHO"); devolve o erro ou NULL */
static const char *oferta_criar(const char *dispositivo, const char *caminho) {
	struct dispositivo *d = dispositivo_por_id(dispositivo);
	if (!d) {
		return "celular não pareado";
	}
	if (!conf.ativo || !recurso_de(d, "files")) {
		return "o envio de arquivos está desligado para este celular";
	}
	if (!conectado(dispositivo)) {
		return "o celular não está conectado";
	}
	GStatBuf st;
	if (g_stat(caminho, &st) != 0 || !S_ISREG(st.st_mode)) {
		return "só arquivos comuns vão pelo Connect (pastas não)";
	}
	if ((guint64)st.st_size > MAX_ARQUIVO) {
		return "arquivo grande demais";
	}
	if (!ofertas) {
		ofertas = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, oferta_liberar);
	}
	struct oferta *o = g_new0(struct oferta, 1);
	char aleatorio[17];
	novo_texto_aleatorio(aleatorio, 8);
	g_snprintf(o->id, sizeof(o->id), "%s", aleatorio);
	g_strlcpy(o->dispositivo, dispositivo, sizeof(o->dispositivo));
	o->caminho = g_strdup(caminho);
	char *nome = g_path_get_basename(caminho);
	o->t = transf_nova(o->id, nome, d->nome, "out", (guint64)st.st_size);
	g_hash_table_insert(ofertas, g_strdup(o->id), o);
	GString *m = g_string_new("{\"type\":\"file-offer\",\"id\":");
	json_escrever_texto(m, o->id);
	g_string_append(m, ",\"name\":");
	json_escrever_texto(m, nome);
	g_string_append_printf(m, ",\"size\":%" G_GUINT64_FORMAT "}", (guint64)st.st_size);
	enviar_ao(dispositivo, m->str);
	g_string_free(m, TRUE);
	g_free(nome);
	return NULL;
}

/* ---- as mensagens do Connect que o celular manda ---- */

#define PRECISA(on, o_que) \
	do { \
		if (!(on)) { \
			erro(r, o_que " está desligado para este celular no PC", false); \
			return true; \
		} \
	} while (0)

static bool connect_msg(struct cliente *c, const struct json *msg, GString *r) {
	const char *tipo = json_chave_texto(msg, "type");
	struct dispositivo *d = dispositivo_por_id(c->dispositivo);
	if (!d) {
		return false;
	}
	char chave[100];
	g_snprintf(chave, sizeof(chave), "%s:%s", d->id, json_chave_texto(msg, "id"));
	if (!strcmp(tipo, "features")) {
		g_string_assign(r, "{\"type\":\"features\",\"features\":");
		recursos_escrever(d, r);
		g_string_append_c(r, '}');
	} else if (!strcmp(tipo, "clipboard")) {
		PRECISA(recurso_de(d, "clipboard"), "a área de transferência");
		const char *texto = json_chave_texto(msg, "text");
		if (!*texto || strlen(texto) > CLIP_MAX) {
			erro(r, "texto vazio ou grande demais", false);
		} else {
			clip_definir(texto);
			ok(r);
		}
	} else if (!strcmp(tipo, "notify")) {
		PRECISA(recurso_de(d, "phone_notifications"), "mostrar avisos do celular");
		char *de = *json_chave_texto(msg, "app") ? g_strdup_printf("%s · %s", d->nome, json_chave_texto(msg, "app")) : g_strdup(d->nome);
		aviso_local(de, json_chave_texto(msg, "summary"), json_chave_texto(msg, "body"), json_bool(json_chave(msg, "urgent"), false));
		g_free(de);
		ok(r);
	} else if (!strcmp(tipo, "file-begin")) {
		PRECISA(recurso_de(d, "files"), "receber arquivos");
		guint64 tam = (guint64)json_numero(json_chave(msg, "size"), -1);
		if (!*json_chave_texto(msg, "id") || json_chave(msg, "size") == NULL || tam > MAX_ARQUIVO) {
			erro(r, "tamanho faltando ou grande demais", false);
			return true;
		}
		if (!recebendo) {
			recebendo = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, recebendo_liberar);
		}
		recebimento_cancelar(chave, "recomeçou");
		char *pasta = pasta_receber(), *nome = nome_seguro(json_chave_texto(msg, "name"));
		g_mkdir_with_parents(pasta, 0755);
		struct recebendo *rc = g_new0(struct recebendo, 1);
		rc->alvo = caminho_livre(pasta, nome);
		rc->parcial = g_strdup_printf("%s.part", rc->alvo);
		rc->f = fopen(rc->parcial, "wb");
		if (!rc->alvo || !rc->f) {
			erro(r, "não consegui criar o arquivo na pasta de recebidos", false);
			recebendo_liberar(rc);
		} else {
			g_strlcpy(rc->chave, chave, sizeof(rc->chave));
			g_strlcpy(rc->dispositivo, d->id, sizeof(rc->dispositivo));
			char *tid = g_strdup_printf("in:%s", chave);
			g_strlcpy(rc->tid, tid, sizeof(rc->tid));
			rc->t = transf_nova(tid, nome, d->nome, "in", tam);
			g_free(tid);
			g_hash_table_insert(recebendo, g_strdup(chave), rc);
			ok(r);
		}
		g_free(pasta);
		g_free(nome);
	} else if (!strcmp(tipo, "file-chunk")) {
		PRECISA(recurso_de(d, "files"), "receber arquivos");
		struct recebendo *rc = recebendo ? g_hash_table_lookup(recebendo, chave) : NULL;
		if (!rc) {
			erro(r, "transferência desconhecida", false);
			return true;
		}
		gsize n = 0;
		guchar *dec = g_base64_decode(json_chave_texto(msg, "data"), &n);
		if (!dec || rc->t->feito + n > rc->t->tamanho) {
			g_free(dec);
			recebimento_cancelar(chave, "dado inválido ou maior que o anunciado");
			erro(r, "o arquivo veio maior que o anunciado", false);
			return true;
		}
		if (fwrite(dec, 1, n, rc->f) != n) {
			g_free(dec);
			recebimento_cancelar(chave, "sem espaço ou erro de disco");
			erro(r, "não consegui gravar (disco cheio?)", false);
			return true;
		}
		rc->t->feito += n;
		g_free(dec);
		ok(r);
	} else if (!strcmp(tipo, "file-end")) {
		PRECISA(recurso_de(d, "files"), "receber arquivos");
		struct recebendo *rc = recebendo ? g_hash_table_lookup(recebendo, chave) : NULL;
		if (!rc) {
			erro(r, "transferência desconhecida", false);
			return true;
		}
		fclose(rc->f);
		rc->f = NULL;
		if (rc->t->feito != rc->t->tamanho) {
			recebimento_cancelar(chave, "o arquivo chegou incompleto");
			erro(r, "o arquivo chegou incompleto", false);
			return true;
		}
		if (rename(rc->parcial, rc->alvo) != 0) {
			recebimento_cancelar(chave, "não consegui renomear");
			erro(r, "não consegui guardar o arquivo", false);
			return true;
		}
		g_strlcpy(rc->t->estado, "done", sizeof(rc->t->estado));
		char *nome = g_path_get_basename(rc->alvo), *pasta = g_path_get_dirname(rc->alvo);
		char *tit = g_strdup_printf("Recebido: %s", nome), *corpo = g_strdup_printf("Salvo em %s", pasta);
		aviso_local(d->nome, tit, corpo, false);
		g_free(tit);
		g_free(corpo);
		g_free(nome);
		g_free(pasta);
		g_hash_table_remove(recebendo, chave);
		ok(r);
	} else if (!strcmp(tipo, "file-cancel")) {
		recebimento_cancelar(chave, "o celular cancelou");
		ok(r);
	} else if (!strcmp(tipo, "file-pull")) {
		oferta_puxar(msg, r);
	} else if (!strcmp(tipo, "file-reject")) {
		struct oferta *o = ofertas ? g_hash_table_lookup(ofertas, json_chave_texto(msg, "id")) : NULL;
		if (o) {
			transf_erro(o->t, *json_chave_texto(msg, "reason") ? json_chave_texto(msg, "reason") : "o celular recusou");
			g_hash_table_remove(ofertas, json_chave_texto(msg, "id"));
		}
		ok(r);
	} else {
		return false;
	}
	return true;
}

/* ---- o caminho do serviço: teclado e ponteiro virtuais ---- */

static bool wtype(const char *a, const char *b, const char *c, const char *d, const char *e, const char *f) {
	char *argv[] = { "wtype", (char *)a, (char *)b, (char *)c, (char *)d, (char *)e, (char *)f, NULL };
	return g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

static bool tecla(const char *nome) {
	return wtype("-k", nome, NULL, NULL, NULL, NULL);
}

static bool tecla_com(const char *modificador, const char *nome) {
	return wtype("-M", modificador, "-k", nome, "-m", modificador);
}

/* o app_id da janela em foco ("" se o Orbit não responde ou não há) */
static char *app_em_foco(void) {
	char *r = g_malloc(32768);
	char *app = g_strdup("");
	if (orbit_pedido("janelas", r, 32768)) {
		struct json *j = json_ler(r);
		const struct json *lista = json_chave(j, "janelas");
		for (int i = 0; lista && i < lista->nfilhos; i++) {
			if (json_bool(json_chave(lista->filhos[i], "foco"), false)) {
				g_free(app);
				app = g_strdup(json_chave_texto(lista->filhos[i], "app_id"));
				break;
			}
		}
		json_liberar(j);
	}
	g_free(r);
	return app;
}

static bool app_e_navegador(const char *app) {
	return g_str_has_prefix(app, "lumna-svc-") || g_str_has_prefix(app, "lumna-midia-web") || strcasestr(app, "chrom") || strcasestr(app, "firefox") || strcasestr(app, "epiphany");
}

/* ---- o caminho do sistema ---- */

static const char *modo_do_celular(const char *valor) {
	return !strcmp(valor, "media") ? "midia" : valor;
}

static int volume_atual(bool *mudo) {
	char *saida = NULL;
	int v = -1;
	char *vol[] = { "pactl", "get-sink-volume", "@DEFAULT_SINK@", NULL };
	if (g_spawn_sync(NULL, vol, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL) && saida) {
		char *p = strchr(saida, '/');
		if (p) {
			v = atoi(p + 1);
		}
	}
	g_free(saida);
	saida = NULL;
	char *mut[] = { "pactl", "get-sink-mute", "@DEFAULT_SINK@", NULL };
	*mudo = false;
	if (g_spawn_sync(NULL, mut, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL) && saida) {
		*mudo = strstr(saida, "yes") != NULL;
	}
	g_free(saida);
	return v;
}

static void volume_mudar(const char *acao, GString *r) {
	bool mudo;
	int v = volume_atual(&mudo);
	if (v < 0) {
		erro(r, "sem controle de volume (pactl)", false);
		return;
	}
	char *argv_vol[] = { "pactl", "set-sink-volume", "@DEFAULT_SINK@", NULL, NULL };
	char *argv_mudo[] = { "pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle", NULL };
	char valor[16];
	if (!strcmp(acao, "up") || !strcmp(acao, "down")) {
		v = CLAMP(v + (acao[0] == 'u' ? 5 : -5), 0, 100); /* sem passar de 100%: acima disso distorce */
		snprintf(valor, sizeof(valor), "%d%%", v);
		argv_vol[3] = valor;
		g_spawn_sync(NULL, argv_vol, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	} else if (!strcmp(acao, "mute")) {
		g_spawn_sync(NULL, argv_mudo, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	} else {
		erro(r, "comando de volume desconhecido", false);
		return;
	}
	v = volume_atual(&mudo);
	g_string_printf(r, "{\"type\":\"volume\",\"value\":%d,\"muted\":%s}", v, mudo ? "true" : "false");
}

/* Home: a tela de Início da Mídia na frente, sem fechar o que estava aberto (o serviço da Mídia cuida) */
static void ir_para_inicio(GString *r) {
	char resp[256];
	if (midiac_pedido("inicio", resp, sizeof(resp)) && strstr(resp, "\"ok\":true")) {
		ok(r);
		return;
	}
	/* sem o serviço da Mídia: ao menos o modo Mídia e a janela do Início, se houver */
	struct modos_estado e;
	if (!modos_ler(&e)) {
		erro(r, "o Orbit não respondeu", false);
		return;
	}
	modos_garantir(MODO_MIDIA, NULL); /* já numa tela: fica nela; senão, na do cursor */
	char *lista = g_malloc(32768);
	if (orbit_pedido("janelas", lista, 32768)) {
		struct json *j = json_ler(lista);
		const struct json *js = json_chave(j, "janelas");
		for (int i = 0; js && i < js->nfilhos; i++) {
			if (!strcmp(json_chave_texto(js->filhos[i], "app_id"), APP_ID_SHELL)) {
				char pedido[40], resp[256];
				snprintf(pedido, sizeof(pedido), "foco %d", (int)json_numero(json_chave(js->filhos[i], "id"), 0));
				orbit_pedido(pedido, resp, sizeof(resp));
				break;
			}
		}
		json_liberar(j);
	}
	g_free(lista);
	ok(r);
}

static void listar_monitores(GString *r) {
	char *saidas = g_malloc(8192);
	struct modos_estado e;
	if (!orbit_pedido("saidas", saidas, 8192) || !modos_ler(&e)) {
		g_free(saidas);
		erro(r, "o Orbit não respondeu", false);
		return;
	}
	struct json *j = json_ler(saidas);
	const struct json *lista = json_chave(j, "saidas");
	g_string_assign(r, "{\"type\":\"monitors\",\"monitors\":[");
	bool primeiro = true;
	for (int i = 0; lista && i < lista->nfilhos; i++) {
		const struct json *s = lista->filhos[i];
		if (json_bool(json_chave(s, "telemovel"), false)) {
			continue; /* a tela do celular-PC não é lugar de Mídia */
		}
		const char *nome = json_chave_texto(s, "nome");
		g_string_append(r, primeiro ? "{\"name\":" : ",{\"name\":");
		primeiro = false;
		json_escrever_texto(r, nome);
		g_string_append(r, ",\"label\":");
		json_escrever_texto(r, nome);
		g_string_append_printf(r, ",\"width\":%d,\"height\":%d,\"focused\":%s,\"media\":%s}", (int)json_numero(json_chave(s, "largura"), 0),
			(int)json_numero(json_chave(s, "altura"), 0), json_bool(json_chave(s, "ponteiro"), false) ? "true" : "false",
			modos_da_saida(&e, nome) == MODO_MIDIA ? "true" : "false");
	}
	g_string_append(r, "]}");
	json_liberar(j);
	g_free(saidas);
}

/* quem está no modo Mídia agora, para avisar os celulares quando muda */
static char *midia_agora(void) {
	struct modos_estado e;
	GString *s = g_string_new("[");
	if (modos_ler(&e)) {
		bool primeiro = true;
		for (int i = 0; i < e.n; i++) {
			if (e.saidas[i].modo == MODO_MIDIA) {
				g_string_append(s, primeiro ? "" : ",");
				json_escrever_texto(s, e.saidas[i].nome);
				primeiro = false;
			}
		}
	}
	g_string_append_c(s, ']');
	return g_string_free(s, FALSE);
}

static char *midia_anterior;

/* ---- um comando de um celular já pareado ---- */

/* o direcional, o OK, o voltar, o texto e as teclas apertam teclas pelo wtype; sem ele nada acontece, e o celular não deve ouvir "ok" */
static bool wtype_instalado(void) {
	char *caminho = g_find_program_in_path("wtype");
	bool tem = caminho != NULL;
	g_free(caminho);
	return tem;
}

static void comando(const struct json *msg, GString *r) {
	const char *tipo = json_chave_texto(msg, "type");
	const char *valor = json_chave_texto(msg, "value");
	const char *acao = json_chave_texto(msg, "action");
	if ((!strcmp(tipo, "pad") || !strcmp(tipo, "text") || !strcmp(tipo, "key")) && !wtype_instalado()) {
		static bool avisado;
		if (!avisado) {
			avisado = true;
			fprintf(stderr, "lumna-connect: o wtype não está instalado: o direcional, o texto e as teclas do celular não funcionam (no Arch: sudo pacman -S wtype)\n");
		}
		erro(r, "o wtype não está instalado neste PC (no Arch: sudo pacman -S wtype): sem ele o controle não aperta teclas", false);
		return;
	}
	if (!strcmp(tipo, "pad")) {
		if (!strcmp(acao, "up")) tecla("Up");
		else if (!strcmp(acao, "down")) tecla("Down");
		else if (!strcmp(acao, "left")) tecla("Left");
		else if (!strcmp(acao, "right")) tecla("Right");
		else if (!strcmp(acao, "accept")) tecla("Return");
		else if (!strcmp(acao, "back")) {
			/* num site, voltar é voltar a página (a tecla do navegador: Alt+Esquerda é do Orbit); no resto, Esc */
			char *app = app_em_foco();
			char resp[256];
			if (g_str_has_prefix(app, "lumna-midia-web") && midiac_pedido("voltar", resp, sizeof(resp)) && strstr(resp, "\"ok\":true")) {
				/* o Firefox da Mídia: o serviço volta uma página e, na primeira, sai do serviço (e pausa) */
			} else if (app_e_navegador(app)) {
				tecla("XF86Back");
			} else {
				tecla("Escape");
			}
			g_free(app);
		} else if (!strcmp(acao, "menu")) {
			tecla_com("logo", "m"); /* o seletor de serviços da Mídia */
		} else if (!strcmp(acao, "guide")) {
			ir_para_inicio(r);
			return;
		} else {
			erro(r, "ação desconhecida", false);
			return;
		}
		ok(r);
	} else if (!strcmp(tipo, "text")) {
		if (!*valor || strlen(valor) > 500) {
			erro(r, "texto vazio ou longo demais", false);
			return;
		}
		wtype("--", valor, NULL, NULL, NULL, NULL) ? ok(r) : erro(r, "o wtype não está instalado", false);
	} else if (!strcmp(tipo, "key")) {
		const char *k = !strcmp(valor, "backspace") ? "BackSpace" : !strcmp(valor, "enter") ? "Return" :
			!strcmp(valor, "escape") ? "Escape" : !strcmp(valor, "space") ? "space" : NULL;
		if (!k) {
			erro(r, "tecla desconhecida", false);
			return;
		}
		tecla(k);
		ok(r);
	} else if (!strcmp(tipo, "media")) {
		struct mpris_estado m;
		mpris_ler(&m);
		if (!strcmp(acao, "play-pause")) {
			m.tem ? mpris_tocar_pausar() : (void)tecla("XF86AudioPlay");
		} else if (!strcmp(acao, "next")) {
			m.tem ? mpris_proxima() : (void)tecla("XF86AudioNext");
		} else if (!strcmp(acao, "previous")) {
			m.tem ? mpris_anterior() : (void)tecla("XF86AudioPrev");
		} else if (!strcmp(acao, "forward") || !strcmp(acao, "back")) {
			int64_t d = (acao[0] == 'f' ? 10 : -10) * G_USEC_PER_SEC;
			if (m.tem) {
				mpris_buscar(d);
			} else {
				tecla(d > 0 ? "Right" : "Left"); /* o player na frente costuma andar 5 s com a seta */
			}
		} else {
			erro(r, "comando de mídia desconhecido", false);
			return;
		}
		ok(r);
	} else if (!strcmp(tipo, "volume")) {
		volume_mudar(acao, r);
	} else if (!strcmp(tipo, "mode")) {
		int modo = modos_de_id(modo_do_celular(valor));
		if (modo < 0) {
			erro(r, "modo desconhecido", false);
			return;
		}
		const char *monitor = json_chave_texto(msg, "monitor");
		modos_definir(modo, *monitor ? monitor : NULL) ? ok(r) : erro(r, "o Orbit não respondeu", false);
	} else if (!strcmp(tipo, "monitors")) {
		listar_monitores(r);
	} else if (!strcmp(tipo, "swap-screens")) {
		/* a TV em Mídia e o monitor trocam de espaço de trabalho (só assim o filme sai da TV) */
		modos_trocar_telas() ? ok(r) : erro(r, "não há outra tela para trocar", false);
	} else if (!strcmp(tipo, "home")) {
		ir_para_inicio(r);
	} else if (!strcmp(tipo, "pointer")) {
		/* o celular como touchpad: {"type":"pointer","dx":..,"dy":..} */
		ponteiro_mover(json_numero(json_chave(msg, "dx"), 0), json_numero(json_chave(msg, "dy"), 0)) ? ok(r) : erro(r, "o Orbit não respondeu", false);
	} else if (!strcmp(tipo, "click")) {
		const char *b = json_chave_texto(msg, "button");
		int botao = !strcmp(b, "right") ? PONTEIRO_DIREITO : !strcmp(b, "middle") ? PONTEIRO_MEIO : PONTEIRO_ESQUERDO;
		bool bom = ponteiro_botao(botao, true);
		bom = ponteiro_botao(botao, false) && bom;
		bom ? ok(r) : erro(r, "o Orbit não respondeu", false);
	} else if (!strcmp(tipo, "scroll")) {
		ponteiro_rolar(json_numero(json_chave(msg, "dx"), 0), json_numero(json_chave(msg, "dy"), 0)) ? ok(r) : erro(r, "o Orbit não respondeu", false);
	} else {
		erro(r, "comando desconhecido", false);
	}
}

/* ---- conversa com o celular ---- */

static void pareamento(struct cliente *c, const struct json *msg, GString *r) {
	const char *enviado = json_chave_texto(msg, "code");
	if (!codigo[0]) {
		erro(r, "abra o pareamento na tela de Mídia da TV", false);
	} else if (g_get_monotonic_time() > codigo_ate || !igual(codigo, enviado)) {
		erro(r, "código inválido ou expirado", false);
	} else {
		codigo[0] = '\0'; /* gasto */
		struct dispositivo *d = g_new0(struct dispositivo, 1);
		novo_texto_aleatorio(d->id, 8);
		novo_texto_aleatorio(d->token, 32);
		const char *nome = json_chave_texto(msg, "name");
		g_strlcpy(d->nome, *nome ? nome : "Celular", sizeof(d->nome));
		g_ptr_array_add(dispositivos, d);
		if (!dispositivos_gravar()) {
			g_ptr_array_remove(dispositivos, d);
			erro(r, "não deu para salvar o pareamento", false);
			return;
		}
		g_strlcpy(c->dispositivo, d->id, sizeof(c->dispositivo));
		g_string_assign(r, "{\"type\":\"paired\",\"token\":");
		json_escrever_texto(r, d->token);
		g_string_append_printf(r, ",\"name\":\"LUMNA %s\"}", g_get_host_name());
	}
}

/* ---- a voz: o celular ouve e a Emma (lumna-emma) entende ---- */

struct voz_espera {
	SoupWebsocketConnection *ws;
};

static void voz_respondeu(const char *resposta, void *dados) {
	struct voz_espera *v = dados;
	if (soup_websocket_connection_get_state(v->ws) == SOUP_WEBSOCKET_STATE_OPEN) {
		GString *r = g_string_new("{\"type\":\"voice_reply\"");
		struct json *j = resposta ? json_ler(resposta) : NULL;
		if (j) {
			g_string_append_printf(r, ",\"ok\":%s,\"local\":%s,\"text\":", json_bool(json_chave(j, "ok"), false) ? "true" : "false",
				json_bool(json_chave(j, "local"), false) ? "true" : "false");
			json_escrever_texto(r, json_chave_texto(j, "resposta"));
			if (*json_chave_texto(j, "erro")) {
				g_string_append(r, ",\"error\":");
				json_escrever_texto(r, json_chave_texto(j, "erro"));
			}
		} else {
			g_string_append(r, ",\"ok\":false,\"error\":\"a Emma não está rodando\"");
		}
		g_string_append_c(r, '}');
		enviar(v->ws, r->str);
		g_string_free(r, TRUE);
		json_liberar(j);
	}
	g_object_unref(v->ws);
	g_free(v);
}

/* {"type":"voice","text":"abre a netflix"}: o que o celular transcreveu. {"type":"voice","action":"listen"}: liga (ou
 * encerra) o microfone do computador. A resposta chega depois, como voice_reply */
static void voz(SoupWebsocketConnection *ws, const struct json *msg, GString *r) {
	const char *texto = json_chave_texto(msg, "text"), *acao = json_chave_texto(msg, "action");
	struct voz_espera *v = g_new0(struct voz_espera, 1);
	v->ws = g_object_ref(ws);
	if (!strcmp(acao, "listen")) {
		emmac_pedido_async("ouvir", voz_respondeu, v);
	} else if (*texto && strlen(texto) <= 500) {
		char *limpo = g_strdup(texto);
		for (char *p = limpo; *p; p++) {
			if (*p == '\n' || *p == '\r') {
				*p = ' ';
			}
		}
		char *pedido = g_strdup_printf("celular %s", limpo);
		emmac_pedido_async(pedido, voz_respondeu, v);
		g_free(pedido);
		g_free(limpo);
	} else {
		g_object_unref(v->ws);
		g_free(v);
		erro(r, "texto vazio ou longo demais", false);
		return;
	}
	ok(r);
}

/* ---- o controle de longe: informações, energia, a Emma por texto e os agendamentos ---- */

/* um aviso do agendamento: aparece aqui e nos celulares */
static void agenda_dizer(const char *resumo, const char *corpo, bool urgente) {
	aviso_local("Agendamento", resumo, corpo, urgente);
	GString *m = g_string_new("{\"type\":\"notification\",\"app\":\"Agendamento\",\"summary\":");
	json_escrever_texto(m, resumo);
	g_string_append(m, ",\"body\":");
	json_escrever_texto(m, corpo);
	g_string_append_printf(m, ",\"urgent\":%s}", urgente ? "true" : "false");
	transmitir_recurso("notifications", m->str);
	g_string_free(m, TRUE);
}

/* a resposta da Emma, sem passar de 400 letras (cabe num aviso) */
static char *resposta_curta(const char *texto) {
	if (g_utf8_strlen(texto, -1) <= 400) {
		return g_strdup(texto);
	}
	return g_strndup(texto, g_utf8_offset_to_pointer(texto, 400) - texto);
}

static void agenda_emma_terminou(const char *resposta, void *dados) {
	struct json *j = resposta ? json_ler(resposta) : NULL;
	bool bom = j && json_bool(json_chave(j, "ok"), false);
	const char *texto = !j ? "a Emma não está rodando" : bom ? json_chave_texto(j, "resposta") : json_chave_texto(j, "erro");
	char *curto = resposta_curta(*texto ? texto : bom ? "Pronto." : "algo deu errado");
	agenda_dizer(bom ? "A Emma terminou" : "A Emma não conseguiu", curto, true);
	g_free(curto);
	json_liberar(j);
}

static gboolean agenda_energia_depois(gpointer dados) {
	char *acao = dados, erro_[160];
	if (!energia_acao(acao, erro_, sizeof(erro_))) {
		agenda_dizer("O agendamento falhou", erro_, true);
	}
	g_free(acao);
	return G_SOURCE_REMOVE;
}

static void agenda_rodar(const struct agenda_tarefa *t) {
	if (!strcmp(t->acao, "remind")) {
		agenda_dizer("Lembrete", t->texto, true);
	} else if (!strcmp(t->acao, "emma")) {
		agenda_dizer("A Emma começou uma tarefa agendada", t->texto, false);
		char *limpo = g_strdup(t->texto);
		for (char *p = limpo; *p; p++) {
			if (*p == '\n' || *p == '\r') {
				*p = ' ';
			}
		}
		char *pedido = g_strdup_printf("celular %s", limpo);
		emmac_pedido_async(pedido, agenda_emma_terminou, NULL);
		g_free(pedido);
		g_free(limpo);
	} else {
		const char *rotulo = !strcmp(t->acao, "poweroff") ? "Desligando o PC" : !strcmp(t->acao, "reboot") ? "Reiniciando o PC"
			: !strcmp(t->acao, "suspend") ? "Suspendendo o PC" : "Bloqueando o PC";
		agenda_dizer(rotulo, "Como estava agendado.", false);
		/* dá tempo do aviso chegar nos celulares antes de a rede cair */
		g_timeout_add_seconds(3, agenda_energia_depois, g_strdup(t->acao));
	}
}

static void agenda_transmitir(const struct cliente *menos);

static gboolean agenda_tique(gpointer dados) {
	GPtrArray *vencidas = agenda_vencidas();
	for (guint i = 0; i < vencidas->len; i++) {
		agenda_rodar(vencidas->pdata[i]);
	}
	if (vencidas->len) {
		agenda_transmitir(NULL);
	}
	g_ptr_array_unref(vencidas);
	return G_SOURCE_CONTINUE;
}

/* a lista nova vai para os celulares que controlam o PC (outro celular mexeu, ou a hora venceu); quem pediu já recebeu a resposta */
static void agenda_transmitir(const struct cliente *menos) {
	GString *m = g_string_new(NULL);
	agenda_json(m);
	for (guint i = 0; i < clientes->len; i++) {
		struct cliente *c = clientes->pdata[i];
		if (c != menos && c->dispositivo[0] && recurso_de(dispositivo_por_id(c->dispositivo), "control")) {
			enviar(c->ws, m->str);
		}
	}
	g_string_free(m, TRUE);
}

static void emma_respondeu(const char *resposta, void *dados) {
	SoupWebsocketConnection *ws = dados;
	struct json *j = resposta ? json_ler(resposta) : NULL;
	GString *r = g_string_new(NULL);
	if (j && json_bool(json_chave(j, "ok"), false)) {
		g_string_assign(r, "{\"type\":\"emma-reply\",\"content\":");
		json_escrever_texto(r, json_chave_texto(j, "resposta"));
		g_string_append_c(r, '}');
	} else {
		erro(r, j && *json_chave_texto(j, "erro") ? json_chave_texto(j, "erro") : "a Emma não está rodando", false);
	}
	enviar(ws, r->str);
	g_string_free(r, TRUE);
	json_liberar(j);
	g_object_unref(ws);
}

static void emma_decidido(const char *resposta, void *dados) {
}

static bool id_simples(const char *id) {
	if (!*id || strlen(id) > 80) {
		return false;
	}
	for (const char *p = id; *p; p++) {
		if (!g_ascii_isalnum(*p) && *p != '-' && *p != '_' && *p != ':') {
			return false;
		}
	}
	return true;
}

/* respondida aqui (true) ou não é desta família (false) */
static bool controle_msg(struct cliente *c, const struct json *msg, GString *r) {
	const char *tipo = json_chave_texto(msg, "type");
	struct dispositivo *d = dispositivo_por_id(c->dispositivo);
	if (!d) {
		return false;
	}
	if (!strcmp(tipo, "info")) {
		g_string_truncate(r, 0);
		energia_info_json(r);
		return true;
	}
	bool agenda = g_str_has_prefix(tipo, "schedule-");
	if (strcmp(tipo, "power") && strcmp(tipo, "emma") && strcmp(tipo, "emma-decide") && !agenda) {
		return false;
	}
	PRECISA(recurso_de(d, "control"), "o controle de longe");
	if (!strcmp(tipo, "power")) {
		char motivo[160];
		energia_acao(json_chave_texto(msg, "action"), motivo, sizeof(motivo)) ? ok(r) : erro(r, motivo, false);
	} else if (!strcmp(tipo, "emma")) {
		const char *texto = json_chave_texto(msg, "text");
		if (!*texto || strlen(texto) > 4000) {
			erro(r, "mensagem vazia ou longa demais", false);
		} else {
			char *limpo = g_strdup(texto);
			for (char *p = limpo; *p; p++) {
				if (*p == '\n' || *p == '\r') {
					*p = ' ';
				}
			}
			char *pedido = g_strdup_printf("celular %s", limpo);
			emmac_pedido_async(pedido, emma_respondeu, g_object_ref(c->ws));
			g_free(pedido);
			g_free(limpo);
			ok(r); /* a resposta chega depois, como emma-reply */
		}
	} else if (!strcmp(tipo, "emma-decide")) {
		const char *id = json_chave_texto(msg, "call_id");
		if (!id_simples(id)) {
			erro(r, "pedido de ação inválido", false);
		} else {
			char *pedido = g_strdup_printf("confirmar %s %s", id, json_bool(json_chave(msg, "allow"), false) ? "sim" : "nao");
			emmac_pedido_async(pedido, emma_decidido, NULL);
			g_free(pedido);
			ok(r);
		}
	} else if (!strcmp(tipo, "schedule-list")) {
		g_string_truncate(r, 0);
		agenda_json(r);
	} else if (!strcmp(tipo, "schedule-add")) {
		const struct json *t = json_chave(msg, "task");
		char motivo[120];
		if (!t || !agenda_adicionar(json_chave_texto(t, "action"), (gint64)json_numero(json_chave(t, "at"), 0), json_chave_texto(t, "text"),
				json_bool(json_chave(t, "daily"), false), d->nome, motivo, sizeof(motivo))) {
			erro(r, t ? motivo : "falta a tarefa", false);
		} else {
			g_string_truncate(r, 0);
			agenda_json(r);
			agenda_transmitir(c);
		}
	} else if (!strcmp(tipo, "schedule-remove")) {
		agenda_remover(json_chave_texto(msg, "id"));
		g_string_truncate(r, 0);
		agenda_json(r);
		agenda_transmitir(c);
	} else {
		erro(r, "comando desconhecido", false);
	}
	return true;
}

static void ao_receber(SoupWebsocketConnection *ws, gint tipo_dado, GBytes *bytes, gpointer dados) {
	struct cliente *c = dados;
	if (tipo_dado != SOUP_WEBSOCKET_DATA_TEXT) {
		return;
	}
	gsize n;
	const char *texto = g_bytes_get_data(bytes, &n);
	char *copia = g_strndup(texto, n);
	struct json *msg = json_ler(copia);
	g_free(copia);
	if (!msg) {
		return;
	}
	const char *tipo = json_chave_texto(msg, "type");
	GString *r = g_string_new(NULL);
	if (!strcmp(tipo, "pair")) {
		pareamento(c, msg, r);
	} else if (!strcmp(tipo, "hello")) {
		struct dispositivo *d = dispositivo_por_token(json_chave_texto(msg, "token"));
		if (d) {
			g_strlcpy(c->dispositivo, d->id, sizeof(c->dispositivo));
			/* já está no Mídia? o celular fica sabendo de cara */
			char *agora = midia_agora();
			char *aviso = g_strdup_printf("{\"type\":\"mode\",\"media\":%s}", agora);
			enviar(ws, aviso);
			g_free(aviso);
			g_free(agora);
			g_string_printf(r, "{\"type\":\"ready\",\"name\":\"LUMNA %s\"}", g_get_host_name());
		} else {
			erro(r, "celular não pareado", true);
		}
	} else if (!c->dispositivo[0]) {
		erro(r, "pareie o celular primeiro", true);
	} else if (!strcmp(tipo, "voice")) {
		voz(ws, msg, r);
	} else if (connect_msg(c, msg, r) || controle_msg(c, msg, r)) {
		/* respondido pelo Connect */
	} else {
		comando(msg, r);
	}
	enviar(ws, r->str);
	g_string_free(r, TRUE);
	json_liberar(msg);
}

static void ao_fechar(SoupWebsocketConnection *ws, gpointer dados) {
	struct cliente *c = dados;
	char id[40];
	g_strlcpy(id, c->dispositivo, sizeof(id));
	g_ptr_array_remove(clientes, dados);
	if (id[0] && !conectado(id)) { /* a conexão caiu: o que estava a meio não termina */
		recebimentos_do_celular_cancelar(id, "a conexão caiu");
		ofertas_do_celular_falhar(id, "a conexão caiu");
	}
}

static void cliente_liberar(gpointer p) {
	struct cliente *c = p;
	g_object_unref(c->ws);
	g_free(c);
}

/* só a rede local, a própria máquina e o Tailscale (100.64.0.0/10) */
static bool endereco_local(GInetAddress *a) {
	const guint8 *b = g_inet_address_to_bytes(a);
	if (g_inet_address_get_family(a) == G_SOCKET_FAMILY_IPV6) {
		static const guint8 v4mapeado[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
		if (!memcmp(b, v4mapeado, 12)) {
			b += 12; /* ::ffff:a.b.c.d é o IPv4 a.b.c.d */
		} else {
			return g_inet_address_get_is_loopback(a) || (b[0] & 0xfe) == 0xfc || (b[0] == 0xfe && (b[1] & 0xc0) == 0x80);
		}
	}
	return b[0] == 127 || b[0] == 10 || (b[0] == 172 && (b[1] & 0xf0) == 16) || (b[0] == 192 && b[1] == 168) || (b[0] == 169 && b[1] == 254) ||
		(b[0] == 100 && (b[1] & 0xc0) == 64);
}

static void ao_conectar(SoupServer *servidor, SoupServerMessage *msg, const char *caminho, SoupWebsocketConnection *ws, gpointer dados) {
	GSocketAddress *de = soup_server_message_get_remote_address(msg);
	if (!G_IS_INET_SOCKET_ADDRESS(de) || !endereco_local(g_inet_socket_address_get_address(G_INET_SOCKET_ADDRESS(de)))) {
		soup_websocket_connection_close(ws, SOUP_WEBSOCKET_CLOSE_POLICY_VIOLATION, "só a rede local");
		return;
	}
	struct cliente *c = g_new0(struct cliente, 1);
	c->ws = g_object_ref(ws);
	g_ptr_array_add(clientes, c);
	soup_websocket_connection_set_max_incoming_payload_size(ws, 1024 * 1024); /* um pedaço de arquivo em base64 tem ~256 KB */
	soup_websocket_connection_set_keepalive_interval(ws, 20); /* celular que sumiu sem avisar cai sozinho */
	g_signal_connect(ws, "message", G_CALLBACK(ao_receber), c);
	g_signal_connect(ws, "closed", G_CALLBACK(ao_fechar), c);
}

/* ---- o socket dos outros programas ---- */

static bool conectado(const char *id) {
	for (guint i = 0; i < clientes->len; i++) {
		struct cliente *c = clientes->pdata[i];
		if (!strcmp(c->dispositivo, id)) {
			return true;
		}
	}
	return false;
}

static void derrubar(const char *id) {
	for (guint i = 0; i < clientes->len; i++) {
		struct cliente *c = clientes->pdata[i];
		if (!id || !strcmp(c->dispositivo, id)) {
			soup_websocket_connection_close(c->ws, SOUP_WEBSOCKET_CLOSE_NORMAL, "esquecido");
		}
	}
}

static void pedido_local(const char *linha, GString *r) {
	char **p = g_strsplit(g_strstrip(g_strdup(linha)), " ", 2);
	const char *cmd = p[0] ? p[0] : "";
	if (!strcmp(cmd, "parear")) {
		uint32_t n;
		gerar_bytes(&n, sizeof(n));
		snprintf(codigo, sizeof(codigo), "%06u", n % 1000000);
		codigo_ate = g_get_monotonic_time() + CODIGO_VALE_US;
		char ends[8][16];
		int ne = mdns_enderecos(ends, 8);
		g_string_printf(r, "{\"ok\":true,\"codigo\":\"%s\",\"porta\":%d,\"expira\":%d,\"enderecos\":[", codigo, PORTA, (int)(CODIGO_VALE_US / G_USEC_PER_SEC));
		for (int i = 0; i < ne; i++) {
			g_string_append_printf(r, "%s\"%s\"", i ? "," : "", ends[i]);
		}
		g_string_append(r, "],\"nome\":");
		json_escrever_texto(r, g_get_host_name());
		g_string_append_c(r, '}');
	} else if (!strcmp(cmd, "parear_cancelar")) {
		codigo[0] = '\0';
		g_string_assign(r, "{\"ok\":true}");
	} else if (!strcmp(cmd, "esquecer") && p[1]) {
		bool todos = !strcmp(p[1], "todos");
		bool achou = todos;
		for (guint i = dispositivos->len; i-- > 0;) {
			struct dispositivo *d = dispositivos->pdata[i];
			if (todos || !strcmp(d->id, p[1])) {
				derrubar(d->id);
				g_ptr_array_remove_index(dispositivos, i);
				achou = true;
			}
		}
		if (achou && dispositivos_gravar()) {
			g_string_assign(r, "{\"ok\":true}");
		} else {
			g_string_assign(r, "{\"ok\":false,\"erro\":\"celular não encontrado\"}");
		}
	} else if (!strcmp(cmd, "estado") || !strcmp(cmd, "connect")) {
		bool completo = !strcmp(cmd, "connect");
		g_string_printf(r, "{\"ok\":true,\"porta\":%d,\"pareando\":%s,\"ativo\":%s,\"dispositivos\":[", PORTA, codigo[0] && g_get_monotonic_time() <= codigo_ate ? "true" : "false",
			conf.ativo ? "true" : "false");
		for (guint i = 0; i < dispositivos->len; i++) {
			struct dispositivo *d = dispositivos->pdata[i];
			g_string_append_printf(r, "%s{\"id\":\"%s\",\"nome\":", i ? "," : "", d->id);
			json_escrever_texto(r, d->nome);
			g_string_append_printf(r, ",\"conectado\":%s,\"recursos\":", conectado(d->id) ? "true" : "false");
			recursos_escrever(d, r);
			g_string_append_c(r, '}');
		}
		g_string_append(r, "]");
		if (completo) {
			char *pasta = pasta_receber();
			g_string_append(r, ",\"pasta\":");
			json_escrever_texto(r, pasta);
			g_string_append(r, ",\"pasta_escolhida\":");
			json_escrever_texto(r, conf.pasta);
			g_free(pasta);
			g_string_append(r, ",\"transferencias\":[");
			for (guint i = 0; transferencias && i < transferencias->len; i++) {
				struct transf *t = transferencias->pdata[transferencias->len - 1 - i]; /* a mais nova primeiro */
				g_string_append_printf(r, "%s{\"id\":\"%s\",\"nome\":", i ? "," : "", t->id);
				json_escrever_texto(r, t->nome);
				g_string_append(r, ",\"par\":");
				json_escrever_texto(r, t->par);
				g_string_append_printf(r, ",\"direcao\":\"%s\",\"feito\":%" G_GUINT64_FORMAT ",\"tamanho\":%" G_GUINT64_FORMAT ",\"estado\":\"%s\",\"erro\":", t->dir, t->feito, t->tamanho, t->estado);
				json_escrever_texto(r, t->erro);
				g_string_append_c(r, '}');
			}
			g_string_append(r, "]");
		}
		g_string_append_c(r, '}');
	} else if (!strcmp(cmd, "connect_ativo") && p[1]) {
		conf.ativo = !strcmp(g_strstrip(p[1]), "sim");
		conf_gravar();
		if (!conf.ativo) { /* desligado: o que estava a meio para */
			for (guint i = 0; i < dispositivos->len; i++) {
				struct dispositivo *d = dispositivos->pdata[i];
				recebimentos_do_celular_cancelar(d->id, "o Connect foi desligado");
				ofertas_do_celular_falhar(d->id, "o Connect foi desligado");
			}
		}
		g_string_assign(r, "{\"ok\":true}");
	} else if (!strcmp(cmd, "connect_pasta")) {
		g_strlcpy(conf.pasta, p[1] ? g_strstrip(p[1]) : "", sizeof(conf.pasta));
		conf_gravar();
		g_string_assign(r, "{\"ok\":true}");
	} else if (!strcmp(cmd, "recurso") && p[1]) {
		char **q = g_strsplit(p[1], " ", 3);
		struct dispositivo *d = q[0] && q[1] && q[2] ? dispositivo_por_id(q[0]) : NULL;
		if (d && recurso_existe(q[1])) {
			recurso_definir(d, q[1], !strcmp(g_strstrip(q[2]), "sim"));
			dispositivos_gravar();
			GString *m = g_string_new("{\"type\":\"features\",\"features\":");
			recursos_escrever(d, m);
			g_string_append_c(m, '}');
			enviar_ao(d->id, m->str); /* o celular fica sabendo na hora */
			g_string_free(m, TRUE);
			g_string_assign(r, "{\"ok\":true}");
		} else {
			g_string_assign(r, "{\"ok\":false,\"erro\":\"celular ou recurso desconhecido\"}");
		}
		g_strfreev(q);
	} else if (!strcmp(cmd, "enviar") && p[1]) {
		char *sp = strchr(p[1], ' ');
		const char *e = sp ? NULL : "enviar ID CAMINHO";
		if (sp) {
			*sp++ = '\0';
			e = oferta_criar(p[1], g_strstrip(sp));
		}
		if (e) {
			g_string_assign(r, "{\"ok\":false,\"erro\":");
			json_escrever_texto(r, e);
			g_string_append_c(r, '}');
		} else {
			g_string_assign(r, "{\"ok\":true}");
		}
	} else if (!strcmp(cmd, "copiar_para") && p[1]) {
		struct dispositivo *d = dispositivo_por_id(g_strstrip(p[1]));
		char *texto = NULL, *av[] = { "wl-paste", "--no-newline", "--type", "text", NULL };
		if (!d || !recurso_de(d, "clipboard") || !conectado(d->id)) {
			g_string_assign(r, "{\"ok\":false,\"erro\":\"celular desconectado ou com a área de transferência desligada\"}");
		} else if (!g_spawn_sync(NULL, av, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &texto, NULL, NULL, NULL) || !texto || !*texto || strlen(texto) > CLIP_MAX) {
			g_string_assign(r, "{\"ok\":false,\"erro\":\"não há texto copiado\"}");
		} else {
			GString *m = g_string_new("{\"type\":\"clipboard\",\"text\":");
			json_escrever_texto(m, texto);
			g_string_append_c(m, '}');
			enviar_ao(d->id, m->str);
			g_string_free(m, TRUE);
			g_string_assign(r, "{\"ok\":true}");
		}
		g_free(texto);
	} else if (!strcmp(cmd, "aviso") && p[1]) { /* o lumna-avisos: um aviso daqui vai para os celulares */
		struct json *j = json_ler(p[1]);
		if (j) {
			GString *m = g_string_new("{\"type\":\"notification\",\"app\":");
			json_escrever_texto(m, json_chave_texto(j, "app"));
			g_string_append(m, ",\"summary\":");
			json_escrever_texto(m, json_chave_texto(j, "summary"));
			g_string_append(m, ",\"body\":");
			json_escrever_texto(m, json_chave_texto(j, "body"));
			g_string_append_printf(m, ",\"urgent\":%s}", json_bool(json_chave(j, "urgent"), false) ? "true" : "false");
			transmitir_recurso("notifications", m->str);
			g_string_free(m, TRUE);
			json_liberar(j);
		}
		g_string_assign(r, "{\"ok\":true}");
	} else {
		g_string_assign(r, "{\"ok\":false,\"erro\":\"pedido desconhecido (estado, parear, parear_cancelar, esquecer ID|todos)\"}");
	}
	g_strfreev(p);
}

static gboolean ao_chegar_pedido(GSocketService *servico, GSocketConnection *con, GObject *origem, gpointer dados) {
	g_socket_set_timeout(g_socket_connection_get_socket(con), 2);
	GDataInputStream *entrada = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(con)));
	char *linha = g_data_input_stream_read_line(entrada, NULL, NULL, NULL);
	if (linha) {
		GString *r = g_string_new(NULL);
		pedido_local(linha, r);
		g_string_append_c(r, '\n');
		g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(con)), r->str, r->len, NULL, NULL, NULL);
		g_string_free(r, TRUE);
		g_free(linha);
	}
	g_object_unref(entrada);
	return TRUE;
}

static char *caminho_socket(void) {
	const char *run = g_get_user_runtime_dir();
	return g_build_filename(run, "lumna-connect.sock", NULL);
}

/* `lumna-connect parear`: faz o pedido ao serviço que está rodando */
static int cliente_cli(int argc, char **argv) {
	char *caminho = caminho_socket();
	GSocketClient *cli = g_socket_client_new();
	GSocketAddress *end = g_unix_socket_address_new(caminho);
	GSocketConnection *con = g_socket_client_connect(cli, G_SOCKET_CONNECTABLE(end), NULL, NULL);
	if (!con) {
		fprintf(stderr, "lumna-connect: o serviço não está rodando\n");
		return 1;
	}
	char *pedido = g_strjoinv(" ", argv + 1);
	char *com_nl = g_strdup_printf("%s\n", pedido);
	g_output_stream_write_all(g_io_stream_get_output_stream(G_IO_STREAM(con)), com_nl, strlen(com_nl), NULL, NULL, NULL);
	GDataInputStream *entrada = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(con)));
	char *resp = g_data_input_stream_read_line(entrada, NULL, NULL, NULL);
	printf("%s\n", resp ? resp : "");
	(void)argc;
	return resp ? 0 : 1;
}

/* ---- o Orbit mudou de modo: os celulares ficam sabendo ---- */

static gboolean orbit_mexeu(gint fd, GIOCondition cond, gpointer dados) {
	modos_assinar_ler(fd);
	char *agora = midia_agora();
	if (g_strcmp0(agora, midia_anterior)) {
		char *aviso = g_strdup_printf("{\"type\":\"mode\",\"media\":%s}", agora);
		transmitir(aviso);
		g_free(aviso);
		g_free(midia_anterior);
		midia_anterior = agora;
	} else {
		g_free(agora);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean orbit_ligar(gpointer dados) {
	static guint fonte;
	if (fonte) {
		return G_SOURCE_REMOVE;
	}
	int fd = modos_assinar();
	if (fd < 0) {
		return G_SOURCE_CONTINUE; /* o Orbit ainda não subiu: tenta de novo */
	}
	fonte = g_unix_fd_add(fd, G_IO_IN | G_IO_HUP, orbit_mexeu, NULL);
	midia_anterior = midia_agora();
	return G_SOURCE_REMOVE;
}

static gboolean encerrar(gpointer dados) {
	g_main_loop_quit(dados);
	return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
	signal(SIGPIPE, SIG_IGN);
	if (argc > 1) {
		return cliente_cli(argc, argv);
	}
	dispositivos_ler();
	conf_ler();
	clientes = g_ptr_array_new_with_free_func(cliente_liberar);

	GError *err = NULL;
	SoupServer *servidor = soup_server_new("server-header", "lumna-connect", NULL);
	soup_server_add_websocket_handler(servidor, "/", NULL, NULL, ao_conectar, NULL, NULL);
	if (!soup_server_listen_all(servidor, PORTA, 0, &err)) {
		fprintf(stderr, "lumna-connect: porta %d: %s\n", PORTA, err->message);
		return 1;
	}
	if (!mdns_anunciar(SERVICO, PORTA)) {
		fprintf(stderr, "lumna-connect: sem mDNS: o celular precisa do endereço digitado\n");
	}

	char *caminho = caminho_socket();
	unlink(caminho);
	GSocketService *servico = g_socket_service_new();
	GSocketAddress *end = g_unix_socket_address_new(caminho);
	if (g_socket_listener_add_address(G_SOCKET_LISTENER(servico), end, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, NULL, NULL, &err)) {
		chmod(caminho, 0600);
		g_signal_connect(servico, "incoming", G_CALLBACK(ao_chegar_pedido), NULL);
		g_socket_service_start(servico);
	} else {
		fprintf(stderr, "lumna-connect: socket %s: %s\n", caminho, err->message);
	}

	/* a área de transferência (o wl-paste precisa do compositor no ar: religa até conseguir) */
	clip_ligar(NULL);
	g_timeout_add_seconds(5, clip_ligar, NULL);

	/* o MPRIS e o Orbit podem demorar a aparecer: religa até conseguir */
	mpris_iniciar();
	orbit_ligar(NULL);
	g_timeout_add_seconds(3, orbit_ligar, NULL);
	agenda_tique(NULL); /* o que venceu com o PC desligado roda já */
	g_timeout_add_seconds(15, agenda_tique, NULL);

	GMainLoop *laco = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGINT, encerrar, laco);
	g_unix_signal_add(SIGTERM, encerrar, laco);
	g_main_loop_run(laco);
	unlink(caminho);
	return 0;
}
