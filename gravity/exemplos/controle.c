/*
 * lumna-controle: o controle do LUMNA pelo celular (o app LUMNA Controle). Um serviço da sessão, à
 * parte do shell e dos apps de Mídia: roda o tempo todo, então o controle não para quando um serviço
 * (Netflix, YouTube...) abre por cima, que era o defeito da primeira versão.
 *
 * Protocolo: o mesmo de antes (WebSocket na porta 47800, um JSON por quadro, anunciado por mDNS
 * como _lumna-remote._tcp). Do celular: pair, hello e, depois de entrar, pad, text, key, media,
 * volume, mode, monitors e home. Pareamento: um código de 6 dígitos que vale 5 minutos e uma vez,
 * pedido pelo shell da Mídia (ou pelo `lumna-controle parear`); o celular recebe um token, guardado
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
 * Os outros programas pedem coisas a ele pelo socket $XDG_RUNTIME_DIR/lumna-controle.sock, uma
 * linha por pedido e um JSON de resposta; `lumna-controle <pedido>` faz o pedido na linha de comando:
 *   estado                     porta, celulares pareados e quais estão conectados
 *   parear                     gera um código novo (o anterior deixa de valer)
 *   parear_cancelar            apaga o código
 *   esquecer ID | todos        tira um celular (ou todos) e derruba a conexão
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <libsoup/soup.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include "emmac.h"
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
	return g_str_has_prefix(app, "lumna-svc-") || strcasestr(app, "chrom") || strcasestr(app, "firefox") || strcasestr(app, "epiphany");
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
	if (modos_em_foco(&e) != MODO_MIDIA) {
		modos_definir(MODO_MIDIA, NULL);
	}
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

static void comando(const struct json *msg, GString *r) {
	const char *tipo = json_chave_texto(msg, "type");
	const char *valor = json_chave_texto(msg, "value");
	const char *acao = json_chave_texto(msg, "action");
	if (!strcmp(tipo, "pad")) {
		if (!strcmp(acao, "up")) tecla("Up");
		else if (!strcmp(acao, "down")) tecla("Down");
		else if (!strcmp(acao, "left")) tecla("Left");
		else if (!strcmp(acao, "right")) tecla("Right");
		else if (!strcmp(acao, "accept")) tecla("Return");
		else if (!strcmp(acao, "back")) {
			/* num site, voltar é voltar a página (a tecla do navegador: Alt+Esquerda é do Orbit); no resto, Esc */
			char *app = app_em_foco();
			if (app_e_navegador(app)) {
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
	} else {
		comando(msg, r);
	}
	enviar(ws, r->str);
	g_string_free(r, TRUE);
	json_liberar(msg);
}

static void ao_fechar(SoupWebsocketConnection *ws, gpointer dados) {
	g_ptr_array_remove(clientes, dados);
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
	soup_websocket_connection_set_max_incoming_payload_size(ws, 64 * 1024);
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
	} else if (!strcmp(cmd, "estado")) {
		g_string_printf(r, "{\"ok\":true,\"porta\":%d,\"pareando\":%s,\"dispositivos\":[", PORTA, codigo[0] && g_get_monotonic_time() <= codigo_ate ? "true" : "false");
		for (guint i = 0; i < dispositivos->len; i++) {
			struct dispositivo *d = dispositivos->pdata[i];
			g_string_append_printf(r, "%s{\"id\":\"%s\",\"nome\":", i ? "," : "", d->id);
			json_escrever_texto(r, d->nome);
			g_string_append_printf(r, ",\"conectado\":%s}", conectado(d->id) ? "true" : "false");
		}
		g_string_append(r, "]}");
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
	return g_build_filename(run, "lumna-controle.sock", NULL);
}

/* `lumna-controle parear`: faz o pedido ao serviço que está rodando */
static int cliente_cli(int argc, char **argv) {
	char *caminho = caminho_socket();
	GSocketClient *cli = g_socket_client_new();
	GSocketAddress *end = g_unix_socket_address_new(caminho);
	GSocketConnection *con = g_socket_client_connect(cli, G_SOCKET_CONNECTABLE(end), NULL, NULL);
	if (!con) {
		fprintf(stderr, "lumna-controle: o serviço não está rodando\n");
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
	clientes = g_ptr_array_new_with_free_func(cliente_liberar);

	GError *err = NULL;
	SoupServer *servidor = soup_server_new("server-header", "lumna-controle", NULL);
	soup_server_add_websocket_handler(servidor, "/", NULL, NULL, ao_conectar, NULL, NULL);
	if (!soup_server_listen_all(servidor, PORTA, 0, &err)) {
		fprintf(stderr, "lumna-controle: porta %d: %s\n", PORTA, err->message);
		return 1;
	}
	if (!mdns_anunciar(SERVICO, PORTA)) {
		fprintf(stderr, "lumna-controle: sem mDNS: o celular precisa do endereço digitado\n");
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
		fprintf(stderr, "lumna-controle: socket %s: %s\n", caminho, err->message);
	}

	/* o MPRIS e o Orbit podem demorar a aparecer: religa até conseguir */
	mpris_iniciar();
	orbit_ligar(NULL);
	g_timeout_add_seconds(3, orbit_ligar, NULL);

	GMainLoop *laco = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGINT, encerrar, laco);
	g_unix_signal_add(SIGTERM, encerrar, laco);
	g_main_loop_run(laco);
	unlink(caminho);
	return 0;
}
