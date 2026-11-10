#include "musica_mpris.h"

#include <gio/gio.h>
#include <math.h>
#include <string.h>

static const char *XML =
	"<node>"
	" <interface name='org.mpris.MediaPlayer2'>"
	"  <method name='Raise'/><method name='Quit'/>"
	"  <property name='CanQuit' type='b' access='read'/><property name='CanRaise' type='b' access='read'/>"
	"  <property name='HasTrackList' type='b' access='read'/><property name='Identity' type='s' access='read'/>"
	"  <property name='DesktopEntry' type='s' access='read'/>"
	"  <property name='SupportedUriSchemes' type='as' access='read'/><property name='SupportedMimeTypes' type='as' access='read'/>"
	" </interface>"
	" <interface name='org.mpris.MediaPlayer2.Player'>"
	"  <method name='Next'/><method name='Previous'/><method name='Pause'/><method name='PlayPause'/><method name='Stop'/><method name='Play'/>"
	"  <method name='Seek'><arg type='x' name='Offset' direction='in'/></method>"
	"  <method name='SetPosition'><arg type='o' name='TrackId' direction='in'/><arg type='x' name='Position' direction='in'/></method>"
	"  <method name='OpenUri'><arg type='s' name='Uri' direction='in'/></method>"
	"  <signal name='Seeked'><arg type='x' name='Position'/></signal>"
	"  <property name='PlaybackStatus' type='s' access='read'/><property name='LoopStatus' type='s' access='readwrite'/>"
	"  <property name='Rate' type='d' access='read'/><property name='Shuffle' type='b' access='readwrite'/>"
	"  <property name='Metadata' type='a{sv}' access='read'/><property name='Volume' type='d' access='readwrite'/>"
	"  <property name='Position' type='x' access='read'/><property name='MinimumRate' type='d' access='read'/><property name='MaximumRate' type='d' access='read'/>"
	"  <property name='CanGoNext' type='b' access='read'/><property name='CanGoPrevious' type='b' access='read'/><property name='CanPlay' type='b' access='read'/>"
	"  <property name='CanPause' type='b' access='read'/><property name='CanSeek' type='b' access='read'/><property name='CanControl' type='b' access='read'/>"
	" </interface>"
	"</node>";

struct mm_mpris {
	mm_acoes acoes;
	char nome[128];
	GThread *thread;
	GMainLoop *laco;
	GMainContext *ctx;
	GMutex m;
	GDBusConnection *con;
	GDBusNodeInfo *info;
	guint dono, reg_raiz, reg_player;
	mm_estado e;
	gint64 e_em;      /* g_get_monotonic_time quando o estado chegou */
	bool pronto;
	GCond c;
};

static const char *OBJ = "/org/mpris/MediaPlayer2";

static gint64 posicao_agora(mm_mpris *m) {
	gint64 p = m->e.pos_us;
	if (m->e.tocando) {
		p += g_get_monotonic_time() - m->e_em;
	}
	if (m->e.duracao_us > 0 && p > m->e.duracao_us) {
		p = m->e.duracao_us;
	}
	return p < 0 ? 0 : p;
}

static GVariant *metadata(const mm_estado *e) {
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
	if (e->tem_faixa) {
		char *tid = g_strdup_printf("/org/lumna/music/track/%s", e->id[0] ? e->id : "0");
		g_variant_builder_add(&b, "{sv}", "mpris:trackid", g_variant_new_object_path(tid));
		g_free(tid);
		g_variant_builder_add(&b, "{sv}", "mpris:length", g_variant_new_int64(e->duracao_us));
		g_variant_builder_add(&b, "{sv}", "xesam:title", g_variant_new_string(e->titulo));
		const char *ar[] = { e->artista, NULL };
		g_variant_builder_add(&b, "{sv}", "xesam:artist", g_variant_new_strv(ar, 1));
		g_variant_builder_add(&b, "{sv}", "xesam:album", g_variant_new_string(e->album));
		if (e->capa[0]) {
			char *u = g_filename_to_uri(e->capa, NULL, NULL);
			if (u) {
				g_variant_builder_add(&b, "{sv}", "mpris:artUrl", g_variant_new_string(u));
				g_free(u);
			}
		}
	} else {
		g_variant_builder_add(&b, "{sv}", "mpris:trackid", g_variant_new_object_path("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
	}
	return g_variant_builder_end(&b);
}

static const char *loop_texto(int r) {
	return r == 2 ? "Track" : r == 1 ? "Playlist" : "None";
}

static GVariant *propriedade(mm_mpris *m, const char *iface, const char *nome) {
	const mm_estado *e = &m->e;
	if (!strcmp(iface, "org.mpris.MediaPlayer2")) {
		if (!strcmp(nome, "CanQuit")) return g_variant_new_boolean(FALSE);
		if (!strcmp(nome, "CanRaise")) return g_variant_new_boolean(TRUE);
		if (!strcmp(nome, "HasTrackList")) return g_variant_new_boolean(FALSE);
		if (!strcmp(nome, "Identity")) return g_variant_new_string("LUMNA Music");
		if (!strcmp(nome, "DesktopEntry")) return g_variant_new_string("lumna-music");
		if (!strcmp(nome, "SupportedUriSchemes")) { const char *s[] = { "file", NULL }; return g_variant_new_strv(s, 1); }
		if (!strcmp(nome, "SupportedMimeTypes")) { const char *s[] = { "audio/mpeg", "audio/flac", "audio/ogg", "audio/mp4", "audio/x-wav", "audio/opus", NULL }; return g_variant_new_strv(s, 6); }
		return NULL;
	}
	if (!strcmp(nome, "PlaybackStatus")) return g_variant_new_string(!e->tem_faixa ? "Stopped" : e->tocando ? "Playing" : "Paused");
	if (!strcmp(nome, "LoopStatus")) return g_variant_new_string(loop_texto(e->repetir));
	if (!strcmp(nome, "Rate") || !strcmp(nome, "MinimumRate") || !strcmp(nome, "MaximumRate")) return g_variant_new_double(1.0);
	if (!strcmp(nome, "Shuffle")) return g_variant_new_boolean(e->aleatorio);
	if (!strcmp(nome, "Metadata")) return metadata(e);
	if (!strcmp(nome, "Volume")) return g_variant_new_double(e->volume);
	if (!strcmp(nome, "Position")) return g_variant_new_int64(posicao_agora(m));
	if (!strcmp(nome, "CanGoNext")) return g_variant_new_boolean(e->pode_proxima);
	if (!strcmp(nome, "CanGoPrevious")) return g_variant_new_boolean(e->pode_anterior);
	if (!strcmp(nome, "CanPlay") || !strcmp(nome, "CanPause") || !strcmp(nome, "CanSeek")) return g_variant_new_boolean(e->tem_faixa);
	if (!strcmp(nome, "CanControl")) return g_variant_new_boolean(TRUE);
	return NULL;
}

static GVariant *ao_ler(GDBusConnection *c, const char *rem, const char *obj, const char *iface, const char *nome, GError **erro, gpointer dados) {
	mm_mpris *m = dados;
	g_mutex_lock(&m->m);
	GVariant *v = propriedade(m, iface, nome);
	g_mutex_unlock(&m->m);
	if (!v) {
		g_set_error(erro, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "propriedade desconhecida: %s", nome);
	}
	return v;
}

static gboolean ao_escrever(GDBusConnection *c, const char *rem, const char *obj, const char *iface, const char *nome, GVariant *v, GError **erro, gpointer dados) {
	mm_mpris *m = dados;
	const mm_acoes *a = &m->acoes;
	if (!strcmp(nome, "Volume") && a->volume) {
		a->volume(a->dados, CLAMP(g_variant_get_double(v), 0.0, 1.0));
	} else if (!strcmp(nome, "Shuffle") && a->aleatorio) {
		a->aleatorio(a->dados, g_variant_get_boolean(v));
	} else if (!strcmp(nome, "LoopStatus") && a->repetir) {
		const char *s = g_variant_get_string(v, NULL);
		a->repetir(a->dados, !strcmp(s, "Track") ? 2 : !strcmp(s, "Playlist") ? 1 : 0);
	} else {
		return FALSE;
	}
	return TRUE;
}

static void ao_metodo(GDBusConnection *c, const char *rem, const char *obj, const char *iface, const char *metodo, GVariant *par, GDBusMethodInvocation *inv, gpointer dados) {
	mm_mpris *m = dados;
	const mm_acoes *a = &m->acoes;
	if (!strcmp(iface, "org.mpris.MediaPlayer2")) {
		if (!strcmp(metodo, "Raise") && a->abrir_janela) {
			a->abrir_janela(a->dados);
		}
	} else if (!strcmp(metodo, "Next") && a->proxima) {
		a->proxima(a->dados);
	} else if (!strcmp(metodo, "Previous") && a->anterior) {
		a->anterior(a->dados);
	} else if (!strcmp(metodo, "Pause") && a->pausar) {
		a->pausar(a->dados);
	} else if (!strcmp(metodo, "PlayPause") && a->alternar) {
		a->alternar(a->dados);
	} else if (!strcmp(metodo, "Play") && a->tocar) {
		a->tocar(a->dados);
	} else if (!strcmp(metodo, "Stop") && a->parar) {
		a->parar(a->dados);
	} else if (!strcmp(metodo, "Seek") && a->buscar) {
		gint64 d;
		g_variant_get(par, "(x)", &d);
		a->buscar(a->dados, d);
	} else if (!strcmp(metodo, "SetPosition") && a->posicionar) {
		const char *tid;
		gint64 p;
		g_variant_get(par, "(&ox)", &tid, &p);
		g_mutex_lock(&m->m);
		char *atual = g_strdup_printf("/org/lumna/music/track/%s", m->e.id[0] ? m->e.id : "0");
		bool igual = !strcmp(tid, atual);
		g_mutex_unlock(&m->m);
		g_free(atual);
		if (igual) { /* só vale para a faixa de agora (a regra do MPRIS) */
			a->posicionar(a->dados, p);
		}
	} else if (!strcmp(metodo, "OpenUri") && a->abrir_uri) {
		const char *u;
		g_variant_get(par, "(&s)", &u);
		a->abrir_uri(a->dados, u);
	}
	g_dbus_method_invocation_return_value(inv, NULL);
}

static const GDBusInterfaceVTable VT = { ao_metodo, ao_ler, ao_escrever, { 0 } };

static void ao_conectar(GDBusConnection *c, const char *nome, gpointer dados) {
	mm_mpris *m = dados;
	GError *e = NULL;
	m->reg_raiz = g_dbus_connection_register_object(c, OBJ, m->info->interfaces[0], &VT, m, NULL, &e);
	g_clear_error(&e);
	m->reg_player = g_dbus_connection_register_object(c, OBJ, m->info->interfaces[1], &VT, m, NULL, &e);
	g_clear_error(&e);
	g_mutex_lock(&m->m);
	m->con = c;
	g_mutex_unlock(&m->m);
}

static void ao_nome(GDBusConnection *c, const char *nome, gpointer dados) {
	mm_mpris *m = dados;
	g_mutex_lock(&m->m);
	m->pronto = true;
	g_cond_broadcast(&m->c);
	g_mutex_unlock(&m->m);
}

static void ao_nome_perdido(GDBusConnection *c, const char *nome, gpointer dados) {
	mm_mpris *m = dados;
	g_mutex_lock(&m->m);
	m->pronto = true; /* sem D-Bus (ou nome tomado): quem espera acorda e vê que não conectou */
	g_cond_broadcast(&m->c);
	g_mutex_unlock(&m->m);
}

static gpointer mpris_thread(gpointer dados) {
	mm_mpris *m = dados;
	g_main_context_push_thread_default(m->ctx);
	m->dono = g_bus_own_name(G_BUS_TYPE_SESSION, m->nome, G_BUS_NAME_OWNER_FLAGS_NONE, ao_conectar, ao_nome, ao_nome_perdido, m, NULL);
	g_main_loop_run(m->laco);
	g_bus_unown_name(m->dono);
	if (m->con) {
		g_dbus_connection_unregister_object(m->con, m->reg_raiz);
		g_dbus_connection_unregister_object(m->con, m->reg_player);
	}
	g_main_context_pop_thread_default(m->ctx);
	return NULL;
}

mm_mpris *mm_iniciar(const mm_acoes *acoes, const char *sufixo) {
	mm_mpris *m = g_new0(mm_mpris, 1);
	m->acoes = *acoes;
	g_snprintf(m->nome, sizeof(m->nome), "org.mpris.MediaPlayer2.%s", sufixo && *sufixo ? sufixo : "lumna_music");
	g_mutex_init(&m->m);
	g_cond_init(&m->c);
	m->info = g_dbus_node_info_new_for_xml(XML, NULL);
	m->ctx = g_main_context_new();
	m->laco = g_main_loop_new(m->ctx, FALSE);
	m->e.volume = 1;
	m->thread = g_thread_new("musica-mpris", mpris_thread, m);
	gint64 fim = g_get_monotonic_time() + 3 * G_USEC_PER_SEC;
	g_mutex_lock(&m->m);
	while (!m->pronto && g_cond_wait_until(&m->c, &m->m, fim)) {
	}
	bool ok = m->con != NULL;
	g_mutex_unlock(&m->m);
	if (!ok) {
		mm_parar(m);
		return NULL;
	}
	return m;
}

static void emitir(mm_mpris *m, const char *iface, GVariantBuilder *mudou) {
	GDBusConnection *c = m->con;
	if (!c) {
		return;
	}
	g_dbus_connection_emit_signal(c, NULL, OBJ, "org.freedesktop.DBus.Properties", "PropertiesChanged",
		g_variant_new("(sa{sv}as)", iface, mudou, NULL), NULL);
}

void mm_atualizar(mm_mpris *m, const mm_estado *e) {
	g_mutex_lock(&m->m);
	mm_estado antes = m->e;
	m->e = *e;
	m->e_em = g_get_monotonic_time();
	mm_estado *n = &m->e;
	GVariantBuilder b;
	g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
	bool algo = false;
#define MUDOU(cond, nome) if (cond) { GVariant *v = propriedade(m, "org.mpris.MediaPlayer2.Player", nome); g_variant_builder_add(&b, "{sv}", nome, v); algo = true; }
	MUDOU(antes.tocando != n->tocando || antes.tem_faixa != n->tem_faixa, "PlaybackStatus");
	MUDOU(strcmp(antes.id, n->id) || strcmp(antes.titulo, n->titulo) || strcmp(antes.artista, n->artista) || strcmp(antes.album, n->album) || strcmp(antes.capa, n->capa) || antes.duracao_us != n->duracao_us || antes.tem_faixa != n->tem_faixa, "Metadata");
	MUDOU(fabs(antes.volume - n->volume) > 0.001, "Volume");
	MUDOU(antes.aleatorio != n->aleatorio, "Shuffle");
	MUDOU(antes.repetir != n->repetir, "LoopStatus");
	MUDOU(antes.pode_proxima != n->pode_proxima, "CanGoNext");
	MUDOU(antes.pode_anterior != n->pode_anterior, "CanGoPrevious");
#undef MUDOU
	GVariant *fim = algo ? g_variant_builder_end(&b) : (g_variant_builder_clear(&b), NULL);
	g_mutex_unlock(&m->m);
	if (fim) {
		GVariantBuilder b2;
		g_variant_builder_init(&b2, G_VARIANT_TYPE("a{sv}"));
		GVariantIter it;
		g_variant_iter_init(&it, fim);
		GVariant *item;
		while ((item = g_variant_iter_next_value(&it))) {
			g_variant_builder_add_value(&b2, item);
			g_variant_unref(item);
		}
		emitir(m, "org.mpris.MediaPlayer2.Player", &b2);
		g_variant_unref(fim);
	}
}

void mm_pulou(mm_mpris *m, int64_t pos_us) {
	if (m->con) {
		g_dbus_connection_emit_signal(m->con, NULL, OBJ, "org.mpris.MediaPlayer2.Player", "Seeked", g_variant_new("(x)", (gint64)pos_us), NULL);
	}
}

void mm_parar(mm_mpris *m) {
	if (!m) {
		return;
	}
	g_main_loop_quit(m->laco);
	g_thread_join(m->thread);
	g_main_loop_unref(m->laco);
	g_main_context_unref(m->ctx);
	g_dbus_node_info_unref(m->info);
	g_mutex_clear(&m->m);
	g_cond_clear(&m->c);
	g_free(m);
}
