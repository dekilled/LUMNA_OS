/* O servidor MPRIS (servicos/musica_mpris.c) falando com um cliente D-Bus de verdade. Rode dentro de dbus-run-session. */
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

#include "musica_mpris.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static GMutex mu;
static char registro[512];
static int64_t ultimo_i64;
static double ultimo_d;
static int ultimo_n;
static void reg(const char *s) { g_mutex_lock(&mu); g_strlcat(registro, s, sizeof(registro)); g_strlcat(registro, ",", sizeof(registro)); g_mutex_unlock(&mu); }
static void a_tocar(void *d) { reg("play"); }
static void a_pausar(void *d) { reg("pause"); }
static void a_alt(void *d) { reg("alterna"); }
static void a_parar(void *d) { reg("stop"); }
static void a_prox(void *d) { reg("next"); }
static void a_ant(void *d) { reg("prev"); }
static void a_buscar(void *d, int64_t x) { reg("seek"); ultimo_i64 = x; }
static void a_pos(void *d, int64_t x) { reg("setpos"); ultimo_i64 = x; }
static void a_vol(void *d, double v) { reg("vol"); ultimo_d = v; }
static void a_alea(void *d, bool b) { reg("shuffle"); ultimo_n = b; }
static void a_rep(void *d, int m) { reg("loop"); ultimo_n = m; }
static void a_janela(void *d) { reg("raise"); }

static int sinais;
static char mudadas[512];
static void ao_sinal(GDBusConnection *c, const char *rem, const char *obj, const char *iface, const char *sig, GVariant *par, gpointer d) {
	if (strcmp(sig, "PropertiesChanged")) {
		if (!strcmp(sig, "Seeked")) {
			g_strlcat(mudadas, "Seeked,", sizeof(mudadas));
		}
		return;
	}
	GVariant *dic = g_variant_get_child_value(par, 1);
	GVariantIter it;
	g_variant_iter_init(&it, dic);
	const char *k;
	GVariant *v;
	while (g_variant_iter_next(&it, "{&sv}", &k, &v)) {
		g_strlcat(mudadas, k, sizeof(mudadas));
		g_strlcat(mudadas, ",", sizeof(mudadas));
		g_variant_unref(v);
	}
	g_variant_unref(dic);
	sinais++;
}

static void gira(int ms) {
	gint64 fim = g_get_monotonic_time() + ms * 1000;
	while (g_get_monotonic_time() < fim) {
		while (g_main_context_iteration(NULL, FALSE)) {
		}
		g_usleep(10000);
	}
}

static GVariant *chama(GDBusConnection *c, const char *iface, const char *m, GVariant *par) {
	GError *e = NULL;
	GVariant *r = g_dbus_connection_call_sync(c, "org.mpris.MediaPlayer2.lumnateste", "/org/mpris/MediaPlayer2", iface, m, par, NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &e);
	if (e) {
		fprintf(stderr, "  (erro na chamada %s: %s)\n", m, e->message);
		g_clear_error(&e);
	}
	return r;
}

static GVariant *le(GDBusConnection *c, const char *iface, const char *prop) {
	GVariant *r = chama(c, "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", iface, prop));
	if (!r) {
		return NULL;
	}
	GVariant *in, *v;
	g_variant_get(r, "(v)", &v);
	in = v;
	g_variant_unref(r);
	return in;
}

static bool cham(GDBusConnection *c, const char *m, GVariant *par, const char *esperado) {
	g_mutex_lock(&mu);
	registro[0] = 0;
	g_mutex_unlock(&mu);
	GVariant *r = chama(c, "org.mpris.MediaPlayer2.Player", m, par);
	if (r) {
		g_variant_unref(r);
	}
	gira(100);
	g_mutex_lock(&mu);
	bool ok = !strcmp(registro, esperado);
	if (!ok) {
		fprintf(stderr, "  %s: esperava '%s', veio '%s'\n", m, esperado, registro);
	}
	g_mutex_unlock(&mu);
	return ok;
}

int main(void) {
	mm_acoes a = { a_tocar, a_pausar, a_alt, a_parar, a_prox, a_ant, a_buscar, a_pos, a_vol, a_alea, a_rep, a_janela, NULL, NULL };
	mm_mpris *m = mm_iniciar(&a, "lumnateste");
	CONFERE(m != NULL, "o serviço sobe (precisa de dbus-run-session)");
	if (!m) {
		return 1;
	}
	GError *e = NULL;
	GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &e);
	CONFERE(c != NULL, "cliente no barramento");
	g_dbus_connection_signal_subscribe(c, "org.mpris.MediaPlayer2.lumnateste", NULL, NULL, "/org/mpris/MediaPlayer2", NULL, G_DBUS_SIGNAL_FLAGS_NONE, ao_sinal, NULL, NULL);

	/* sem faixa */
	GVariant *v = le(c, "org.mpris.MediaPlayer2.Player", "PlaybackStatus");
	CONFERE(v && !strcmp(g_variant_get_string(v, NULL), "Stopped"), "sem faixa: Stopped");
	if (v) g_variant_unref(v);
	v = le(c, "org.mpris.MediaPlayer2", "Identity");
	CONFERE(v && !strcmp(g_variant_get_string(v, NULL), "LUMNA Music"), "Identity");
	if (v) g_variant_unref(v);
	v = le(c, "org.mpris.MediaPlayer2", "DesktopEntry");
	CONFERE(v && !strcmp(g_variant_get_string(v, NULL), "lumna-music"), "DesktopEntry");
	if (v) g_variant_unref(v);
	v = le(c, "org.mpris.MediaPlayer2.Player", "CanPlay");
	CONFERE(v && !g_variant_get_boolean(v), "sem faixa: não pode tocar");
	if (v) g_variant_unref(v);
	CONFERE(le(c, "org.mpris.MediaPlayer2.Player", "NaoExiste") == NULL, "propriedade desconhecida: erro");

	/* uma faixa tocando */
	mm_estado s;
	memset(&s, 0, sizeof(s));
	s.tem_faixa = true; s.tocando = true;
	g_strlcpy(s.titulo, "Entre órbitas", sizeof(s.titulo)); g_strlcpy(s.artista, "LUMNA Sessions", sizeof(s.artista)); g_strlcpy(s.album, "Horizonte violeta", sizeof(s.album));
	g_strlcpy(s.id, "abc123", sizeof(s.id)); g_strlcpy(s.capa, "/tmp/capa de teste.jpg", sizeof(s.capa));
	s.duracao_us = 248000000; s.pos_us = 84000000; s.volume = 0.8; s.pode_proxima = true; s.pode_anterior = false; s.repetir = 1;
	mudadas[0] = 0;
	mm_atualizar(m, &s);
	gira(300);
	CONFERE(strstr(mudadas, "PlaybackStatus") && strstr(mudadas, "Metadata") && strstr(mudadas, "Volume") && strstr(mudadas, "CanGoNext") && strstr(mudadas, "LoopStatus"), "PropertiesChanged com o que mudou (%s)", mudadas);
	CONFERE(!strstr(mudadas, "Shuffle") && !strstr(mudadas, "CanGoPrevious"), "e só com o que mudou");
	v = le(c, "org.mpris.MediaPlayer2.Player", "PlaybackStatus");
	CONFERE(v && !strcmp(g_variant_get_string(v, NULL), "Playing"), "Playing");
	if (v) g_variant_unref(v);
	v = le(c, "org.mpris.MediaPlayer2.Player", "Metadata");
	if (v) {
		GVariant *t = g_variant_lookup_value(v, "xesam:title", NULL), *l = g_variant_lookup_value(v, "mpris:length", NULL), *art = g_variant_lookup_value(v, "mpris:artUrl", NULL), *ar = g_variant_lookup_value(v, "xesam:artist", NULL), *tid = g_variant_lookup_value(v, "mpris:trackid", NULL);
		CONFERE(t && !strcmp(g_variant_get_string(t, NULL), "Entre órbitas"), "o título (com acento) na Metadata");
		CONFERE(l && g_variant_get_int64(l) == 248000000, "mpris:length em microssegundos");
		CONFERE(art && !strcmp(g_variant_get_string(art, NULL), "file:///tmp/capa%20de%20teste.jpg"), "a capa como file:// com o espaço escapado");
		gsize n;
		const char **av = ar ? g_variant_get_strv(ar, &n) : NULL;
		CONFERE(av && n == 1 && !strcmp(av[0], "LUMNA Sessions"), "xesam:artist é uma lista");
		g_free(av);
		CONFERE(tid && g_variant_is_object_path(g_variant_get_string(tid, NULL)), "o trackid é um caminho de objeto válido");
		if (t) g_variant_unref(t); if (l) g_variant_unref(l); if (art) g_variant_unref(art); if (ar) g_variant_unref(ar); if (tid) g_variant_unref(tid);
		g_variant_unref(v);
	} else {
		CONFERE(false, "Metadata não veio");
	}
	/* a posição anda sozinha enquanto toca */
	v = le(c, "org.mpris.MediaPlayer2.Player", "Position");
	gint64 p1 = v ? g_variant_get_int64(v) : -1;
	if (v) g_variant_unref(v);
	g_usleep(500000);
	v = le(c, "org.mpris.MediaPlayer2.Player", "Position");
	gint64 p2 = v ? g_variant_get_int64(v) : -1;
	if (v) g_variant_unref(v);
	CONFERE(p1 >= 84000000 && p2 - p1 > 400000 && p2 - p1 < 900000, "a posição anda em tempo real (%lld -> %lld)", (long long)p1, (long long)p2);
	/* pausada: não anda */
	s.tocando = false; s.pos_us = 90000000;
	mm_atualizar(m, &s);
	gira(100);
	v = le(c, "org.mpris.MediaPlayer2.Player", "Position");
	p1 = v ? g_variant_get_int64(v) : -1;
	if (v) g_variant_unref(v);
	g_usleep(300000);
	v = le(c, "org.mpris.MediaPlayer2.Player", "Position");
	p2 = v ? g_variant_get_int64(v) : -1;
	if (v) g_variant_unref(v);
	CONFERE(p1 == 90000000 && p2 == p1, "pausada, a posição fica (%lld, %lld)", (long long)p1, (long long)p2);
	v = le(c, "org.mpris.MediaPlayer2.Player", "PlaybackStatus");
	CONFERE(v && !strcmp(g_variant_get_string(v, NULL), "Paused"), "Paused");
	if (v) g_variant_unref(v);

	/* os comandos chegam como callbacks */
	CONFERE(cham(c, "PlayPause", NULL, "alterna,"), "PlayPause");
	CONFERE(cham(c, "Play", NULL, "play,"), "Play");
	CONFERE(cham(c, "Pause", NULL, "pause,"), "Pause");
	CONFERE(cham(c, "Stop", NULL, "stop,"), "Stop");
	CONFERE(cham(c, "Next", NULL, "next,"), "Next");
	CONFERE(cham(c, "Previous", NULL, "prev,"), "Previous");
	CONFERE(cham(c, "Seek", g_variant_new("(x)", (gint64)5000000), "seek,") && ultimo_i64 == 5000000, "Seek +5 s");
	CONFERE(cham(c, "Seek", g_variant_new("(x)", (gint64)-3000000), "seek,") && ultimo_i64 == -3000000, "Seek -3 s");
	CONFERE(cham(c, "SetPosition", g_variant_new("(ox)", "/org/lumna/music/track/abc123", (gint64)30000000), "setpos,") && ultimo_i64 == 30000000, "SetPosition da faixa de agora");
	CONFERE(cham(c, "SetPosition", g_variant_new("(ox)", "/org/lumna/music/track/outra", (gint64)30000000), ""), "SetPosition de outra faixa é ignorado");
	/* propriedades graváveis */
	GVariant *r = chama(c, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", "org.mpris.MediaPlayer2.Player", "Volume", g_variant_new_double(0.35)));
	if (r) g_variant_unref(r);
	gira(100);
	CONFERE(strstr(registro, "vol") && ultimo_d > 0.34 && ultimo_d < 0.36, "Volume gravado: 0,35 (%.2f)", ultimo_d);
	r = chama(c, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", "org.mpris.MediaPlayer2.Player", "Volume", g_variant_new_double(7.0)));
	if (r) g_variant_unref(r);
	gira(100);
	CONFERE(ultimo_d <= 1.0, "Volume é limitado a 1 (%.2f)", ultimo_d);
	r = chama(c, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", "org.mpris.MediaPlayer2.Player", "Shuffle", g_variant_new_boolean(TRUE)));
	if (r) g_variant_unref(r);
	gira(100);
	CONFERE(strstr(registro, "shuffle") && ultimo_n == 1, "Shuffle ligado");
	r = chama(c, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", "org.mpris.MediaPlayer2.Player", "LoopStatus", g_variant_new_string("Track")));
	if (r) g_variant_unref(r);
	gira(100);
	CONFERE(strstr(registro, "loop") && ultimo_n == 2, "LoopStatus Track vira 2");
	r = chama(c, "org.freedesktop.DBus.Properties", "Set", g_variant_new("(ssv)", "org.mpris.MediaPlayer2.Player", "PlaybackStatus", g_variant_new_string("Playing")));
	CONFERE(r == NULL, "propriedade só de leitura não grava");
	r = chama(c, "org.mpris.MediaPlayer2", "Raise", NULL);
	if (r) g_variant_unref(r);
	gira(100);
	CONFERE(strstr(registro, "raise"), "Raise abre a janela");

	/* mudar de faixa e o sinal Seeked */
	mudadas[0] = 0;
	s.tocando = true; g_strlcpy(s.titulo, "Maré noturna", sizeof(s.titulo)); g_strlcpy(s.id, "def456", sizeof(s.id)); s.pos_us = 0; s.pode_anterior = true;
	mm_atualizar(m, &s);
	mm_pulou(m, 1000000);
	gira(300);
	CONFERE(strstr(mudadas, "Metadata") && strstr(mudadas, "CanGoPrevious") && strstr(mudadas, "Seeked"), "faixa nova: Metadata, CanGoPrevious e Seeked (%s)", mudadas);
	/* sem mudança nenhuma, sem sinal */
	int antes = sinais;
	mm_atualizar(m, &s);
	gira(200);
	CONFERE(sinais == antes, "o mesmo estado não manda sinal");
	/* voltar a nada */
	memset(&s, 0, sizeof(s));
	s.volume = 0.8;
	mm_atualizar(m, &s);
	gira(200);
	v = le(c, "org.mpris.MediaPlayer2.Player", "PlaybackStatus");
	CONFERE(v && !strcmp(g_variant_get_string(v, NULL), "Stopped"), "voltou a Stopped");
	if (v) g_variant_unref(v);

	g_object_unref(c);
	mm_parar(m);
	if (falhas) {
		fprintf(stderr, "mpris: %d falha(s)\n", falhas);
		return 1;
	}
	printf("musica (MPRIS): ok\n");
	return 0;
}
