#include "appspadrao.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#include <string.h>

struct def {
	const char *id, *nome, *sub, *icone;
	const char *tipos[10]; /* o primeiro é o que lista os apps */
};

static const struct def defs[] = {
	{ "navegador", "Navegador", "Abre os sites e os links", "globe",
		{ "x-scheme-handler/http", "x-scheme-handler/https", "text/html", "application/xhtml+xml", NULL } },
	{ "email", "E-mail", "Abre os links de e-mail (mailto)", "mail", { "x-scheme-handler/mailto", NULL } },
	{ "musica", "Música", "Abre as músicas", "music",
		{ "audio/mpeg", "audio/flac", "audio/ogg", "audio/x-wav", "audio/mp4", "audio/x-vorbis+ogg", "audio/opus", NULL } },
	{ "video", "Vídeo", "Abre os filmes e vídeos", "play",
		{ "video/mp4", "video/x-matroska", "video/webm", "video/x-msvideo", "video/quicktime", "video/mpeg", NULL } },
	{ "imagens", "Imagens", "Abre as fotos e figuras", "image",
		{ "image/jpeg", "image/png", "image/gif", "image/webp", "image/bmp", "image/svg+xml", NULL } },
	{ "pdf", "Documentos PDF", "Abre os PDF", "text", { "application/pdf", NULL } },
	{ "texto", "Texto", "Abre arquivos de texto", "text", { "text/plain", "text/markdown", NULL } },
	{ "pastas", "Pastas", "Abre as pastas (o gerenciador de arquivos)", "folder", { "inode/directory", NULL } },
};

int padrao_n(void) {
	return (int)(sizeof(defs) / sizeof(defs[0]));
}

void padrao_ler(int i, struct padrao_categoria *c) {
	memset(c, 0, sizeof(*c));
	if (i < 0 || i >= padrao_n()) {
		return;
	}
	const struct def *d = &defs[i];
	c->id = d->id;
	c->nome = d->nome;
	c->sub = d->sub;
	c->icone = d->icone;
	GAppInfo *atual = g_app_info_get_default_for_type(d->tipos[0], FALSE);
	if (atual && g_app_info_get_id(atual)) {
		g_strlcpy(c->atual, g_app_info_get_id(atual), sizeof(c->atual));
	}
	if (atual) {
		g_object_unref(atual);
	}
	GList *todos = g_app_info_get_all_for_type(d->tipos[0]);
	for (GList *l = todos; l && c->nopcoes < PADRAO_MAX_OPCOES; l = l->next) {
		GAppInfo *ai = l->data;
		if (!g_app_info_get_id(ai) || !g_app_info_should_show(ai)) {
			continue;
		}
		bool repetido = false;
		for (int k = 0; k < c->nopcoes; k++) {
			repetido |= !strcmp(c->opcoes[k].id, g_app_info_get_id(ai));
		}
		if (!repetido) {
			struct padrao_opcao *o = &c->opcoes[c->nopcoes++];
			g_strlcpy(o->id, g_app_info_get_id(ai), sizeof(o->id));
			g_strlcpy(o->nome, g_app_info_get_name(ai), sizeof(o->nome));
		}
	}
	g_list_free_full(todos, g_object_unref);
}

bool padrao_definir(int i, const char *desktop_id) {
	if (i < 0 || i >= padrao_n() || !desktop_id || !*desktop_id) {
		return false;
	}
	GDesktopAppInfo *info = g_desktop_app_info_new(desktop_id);
	if (!info) {
		return false;
	}
	bool ok = true;
	for (int k = 0; defs[i].tipos[k]; k++) {
		GError *erro = NULL;
		ok &= g_app_info_set_as_default_for_type(G_APP_INFO(info), defs[i].tipos[k], &erro);
		g_clear_error(&erro);
	}
	g_object_unref(info);
	return ok;
}
