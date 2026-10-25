/* O que está tocando, por MPRIS (mpris.h): uma linha pergunta aos players de segundo a segundo. */
#include "mpris.h"

#include <gio/gio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define PREFIXO "org.mpris.MediaPlayer2."
#define CAMINHO "/org/mpris/MediaPlayer2"

static GDBusConnection *ligacao;
static GMutex trinco;
static struct mpris_estado estado;
static char nome_atual[256];
static char ultimo_tocando[256];
static int fd_aviso = -1;

static void avisar(void) {
	uint64_t um = 1;
	if (write(fd_aviso, &um, sizeof(um)) < 0) {
	}
}

static GVariant *prop(const char *nome, const char *iface, const char *p) {
	GVariant *r = g_dbus_connection_call_sync(ligacao, nome, CAMINHO, "org.freedesktop.DBus.Properties", "Get",
		g_variant_new("(ss)", iface, p), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
	if (!r) {
		return NULL;
	}
	GVariant *v;
	g_variant_get(r, "(v)", &v);
	g_variant_unref(r);
	return v;
}

static bool tocando(const char *nome) {
	GVariant *v = prop(nome, "org.mpris.MediaPlayer2.Player", "PlaybackStatus");
	bool sim = v && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) && !strcmp(g_variant_get_string(v, NULL), "Playing");
	if (v) {
		g_variant_unref(v);
	}
	return sim;
}

static void ler_player(const char *nome, struct mpris_estado *e) {
	memset(e, 0, sizeof(*e));
	e->tem = true;
	e->tocando = tocando(nome);
	GVariant *meta = prop(nome, "org.mpris.MediaPlayer2.Player", "Metadata");
	if (meta && g_variant_is_of_type(meta, G_VARIANT_TYPE_VARDICT)) {
		const char *s;
		if (g_variant_lookup(meta, "xesam:title", "&s", &s)) {
			snprintf(e->titulo, sizeof(e->titulo), "%s", s);
		}
		GVariant *artistas = g_variant_lookup_value(meta, "xesam:artist", G_VARIANT_TYPE_STRING_ARRAY);
		if (artistas) {
			gsize n;
			const char **lista = g_variant_get_strv(artistas, &n);
			if (n > 0) {
				snprintf(e->artista, sizeof(e->artista), "%s", lista[0]);
			}
			g_free(lista);
			g_variant_unref(artistas);
		}
		if (g_variant_lookup(meta, "mpris:artUrl", "&s", &s) && g_str_has_prefix(s, "file://")) {
			char *c = g_filename_from_uri(s, NULL, NULL);
			if (c) {
				snprintf(e->capa, sizeof(e->capa), "%s", c);
				g_free(c);
			}
		}
		GVariant *d = g_variant_lookup_value(meta, "mpris:length", NULL);
		if (d) {
			e->duracao_us = g_variant_is_of_type(d, G_VARIANT_TYPE_INT64) ? g_variant_get_int64(d)
				: g_variant_is_of_type(d, G_VARIANT_TYPE_UINT64) ? (int64_t)g_variant_get_uint64(d) : 0;
			g_variant_unref(d);
		}
	}
	if (meta) {
		g_variant_unref(meta);
	}
	GVariant *pos = prop(nome, "org.mpris.MediaPlayer2.Player", "Position");
	if (pos && g_variant_is_of_type(pos, G_VARIANT_TYPE_INT64)) {
		e->posicao_us = g_variant_get_int64(pos);
	}
	if (pos) {
		g_variant_unref(pos);
	}
	e->lida_us = g_get_monotonic_time();
	GVariant *v = prop(nome, "org.mpris.MediaPlayer2.Player", "CanGoNext");
	e->pode_proxima = v && g_variant_get_boolean(v);
	if (v) {
		g_variant_unref(v);
	}
	v = prop(nome, "org.mpris.MediaPlayer2.Player", "CanGoPrevious");
	e->pode_anterior = v && g_variant_get_boolean(v);
	if (v) {
		g_variant_unref(v);
	}
}

/* de segundo a segundo: que players há, qual escolher, e o que ele diz */
static gboolean rever(gpointer dados) {
	GVariant *r = g_dbus_connection_call_sync(ligacao, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
		"ListNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
	char escolhido[256] = "";
	if (r) {
		GVariantIter *it;
		const char *nome;
		char primeiro[256] = "";
		bool achou_ultimo = false;
		g_variant_get(r, "(as)", &it);
		while (g_variant_iter_next(it, "&s", &nome)) {
			if (!g_str_has_prefix(nome, PREFIXO)) {
				continue;
			}
			if (!primeiro[0]) {
				snprintf(primeiro, sizeof(primeiro), "%s", nome);
			}
			achou_ultimo |= !strcmp(nome, ultimo_tocando);
			if (!escolhido[0] && tocando(nome)) {
				snprintf(escolhido, sizeof(escolhido), "%s", nome);
			}
		}
		g_variant_iter_free(it);
		g_variant_unref(r);
		if (escolhido[0]) {
			snprintf(ultimo_tocando, sizeof(ultimo_tocando), "%s", escolhido);
		} else if (achou_ultimo) {
			snprintf(escolhido, sizeof(escolhido), "%s", ultimo_tocando);
		} else {
			snprintf(escolhido, sizeof(escolhido), "%s", primeiro);
		}
	}
	struct mpris_estado novo = { 0 };
	if (escolhido[0]) {
		ler_player(escolhido, &novo);
	}
	g_mutex_lock(&trinco);
	/* avisa quando muda o que se vê (a posição anda sozinha; um salto de mais de 2 s também conta) */
	int64_t esperada = estado.posicao_us + (estado.tocando ? novo.lida_us - estado.lida_us : 0);
	bool mudou = novo.tem != estado.tem || novo.tocando != estado.tocando || strcmp(novo.titulo, estado.titulo) ||
		strcmp(novo.artista, estado.artista) || strcmp(novo.capa, estado.capa) || novo.duracao_us != estado.duracao_us ||
		llabs(novo.posicao_us - esperada) > 2000000;
	estado = novo;
	snprintf(nome_atual, sizeof(nome_atual), "%s", escolhido);
	g_mutex_unlock(&trinco);
	if (mudou) {
		avisar();
	}
	return G_SOURCE_CONTINUE;
}

static gpointer linha(gpointer d) {
	GMainContext *ctx = g_main_context_new();
	g_main_context_push_thread_default(ctx);
	ligacao = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	if (!ligacao) {
		return NULL;
	}
	rever(NULL);
	GSource *s = g_timeout_source_new(1000);
	g_source_set_callback(s, rever, NULL, NULL);
	g_source_attach(s, ctx);
	g_main_loop_run(g_main_loop_new(ctx, FALSE));
	return NULL;
}

int mpris_iniciar(void) {
	fd_aviso = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (fd_aviso < 0) {
		return -1;
	}
	g_thread_unref(g_thread_new("mpris", linha, NULL));
	return fd_aviso;
}

void mpris_ler(struct mpris_estado *e) {
	uint64_t n;
	if (read(fd_aviso, &n, sizeof(n)) < 0) {
	}
	g_mutex_lock(&trinco);
	*e = estado;
	g_mutex_unlock(&trinco);
}

int64_t mpris_posicao(const struct mpris_estado *e) {
	int64_t p = e->posicao_us + (e->tocando ? g_get_monotonic_time() - e->lida_us : 0);
	return e->duracao_us > 0 && p > e->duracao_us ? e->duracao_us : p;
}

static void chamar(const char *metodo) {
	char nome[256];
	g_mutex_lock(&trinco);
	snprintf(nome, sizeof(nome), "%s", nome_atual);
	g_mutex_unlock(&trinco);
	if (ligacao && nome[0]) {
		g_dbus_connection_call(ligacao, nome, CAMINHO, "org.mpris.MediaPlayer2.Player", metodo, NULL, NULL,
			G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL, NULL);
	}
}

void mpris_tocar_pausar(void) {
	chamar("PlayPause");
}

void mpris_proxima(void) {
	chamar("Next");
}

void mpris_anterior(void) {
	chamar("Previous");
}
