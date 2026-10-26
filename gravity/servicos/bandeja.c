/* A bandeja do sistema pelo StatusNotifierItem (bandeja.h). */
#include "bandeja.h"

#include <cairo.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#define WATCHER "org.kde.StatusNotifierWatcher"
#define WATCHER_OBJ "/StatusNotifierWatcher"
#define ITEM_IFACE "org.kde.StatusNotifierItem"
#define MENU_IFACE "com.canonical.dbusmenu"

static GDBusConnection *bus;
static GMutex trinco;
static struct bandeja_estado estado;
static struct bandeja_menu menu;
static int fd_aviso = -1;
static GMainContext *contexto;

struct no {
	struct bandeja_item item;
	char servico[96], caminho[96], menu_caminho[96], tema[160];
	guint vigia, assinatura;
};
static GPtrArray *nos; /* struct no *, só na linha do serviço */

static void avisar(void) {
	uint64_t um = 1;
	if (write(fd_aviso, &um, sizeof(um)) < 0) {
	}
}

static void copiar(char *dest, size_t tam, const char *src) {
	g_strlcpy(dest, src ? src : "", tam);
}

static GVariant *chamar(const char *servico, const char *caminho, const char *iface, const char *metodo, GVariant *args,
		const GVariantType *tipo) {
	return g_dbus_connection_call_sync(bus, servico, caminho, iface, metodo, args, tipo, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
}

/* ---- o estado, publicado a partir dos nós ---- */

static void publicar(void) {
	g_mutex_lock(&trinco);
	memset(&estado.itens, 0, sizeof(estado.itens));
	estado.n = 0;
	for (guint i = 0; nos && i < nos->len && estado.n < BANDEJA_MAX; i++) {
		struct no *n = g_ptr_array_index(nos, i);
		estado.itens[estado.n++] = n->item;
	}
	estado.geracao++;
	g_mutex_unlock(&trinco);
	avisar();
}

/* ---- o ícone: o nome no tema, um arquivo, ou os pixels que o app mandou ---- */

static bool existe(const char *c) {
	return access(c, R_OK) == 0;
}

static void pixmap_para_arquivo(GVariant *pixmaps, const char *chave, char *dest, size_t tam) {
	dest[0] = '\0';
	if (!pixmaps || g_variant_n_children(pixmaps) == 0) {
		return;
	}
	/* o maior */
	int melhor = -1, area = 0;
	for (gsize i = 0; i < g_variant_n_children(pixmaps); i++) {
		GVariant *p = g_variant_get_child_value(pixmaps, i);
		int w, h;
		g_variant_get_child(p, 0, "i", &w);
		g_variant_get_child(p, 1, "i", &h);
		if (w * h > area) {
			area = w * h;
			melhor = (int)i;
		}
		g_variant_unref(p);
	}
	if (melhor < 0 || area <= 0) {
		return;
	}
	GVariant *p = g_variant_get_child_value(pixmaps, melhor);
	int w, h;
	g_variant_get_child(p, 0, "i", &w);
	g_variant_get_child(p, 1, "i", &h);
	GVariant *dados = g_variant_get_child_value(p, 2);
	gsize n;
	const guchar *b = g_variant_get_fixed_array(dados, &n, 1);
	if (n >= (gsize)w * h * 4) {
		cairo_surface_t *sup = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		unsigned char *d = cairo_image_surface_get_data(sup);
		int passo = cairo_image_surface_get_stride(sup);
		cairo_surface_flush(sup);
		for (int y = 0; y < h; y++) {
			uint32_t *linha = (uint32_t *)(d + (size_t)y * passo);
			for (int x = 0; x < w; x++) {
				const guchar *s = b + ((size_t)y * w + x) * 4; /* ARGB, a ordem da rede */
				linha[x] = (uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3];
			}
		}
		cairo_surface_mark_dirty(sup);
		const char *runtime = g_get_user_runtime_dir();
		char *pasta = g_build_filename(runtime, "lumna-bandeja", NULL);
		g_mkdir_with_parents(pasta, 0700);
		char *hash = g_compute_checksum_for_string(G_CHECKSUM_MD5, chave, -1);
		/* o conteúdo muda o nome (o Gravity guarda as imagens pelo caminho) */
		char *conteudo = g_compute_checksum_for_data(G_CHECKSUM_MD5, b, n > 4096 ? 4096 : n);
		char *arq = g_strdup_printf("%s/%s-%.8s.png", pasta, hash, conteudo);
		if (cairo_surface_write_to_png(sup, arq) == CAIRO_STATUS_SUCCESS) {
			copiar(dest, tam, arq);
		}
		g_free(arq);
		g_free(conteudo);
		g_free(hash);
		g_free(pasta);
		cairo_surface_destroy(sup);
	}
	g_variant_unref(dados);
	g_variant_unref(p);
}

static void ler_item(struct no *n) {
	GError *erro = NULL;
	GVariant *r = g_dbus_connection_call_sync(bus, n->servico, n->caminho, "org.freedesktop.DBus.Properties", "GetAll",
		g_variant_new("(s)", ITEM_IFACE), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &erro);
	if (!r) {
		g_printerr("bandeja: não li %s: %s\n", n->item.chave, erro ? erro->message : "?");
		g_clear_error(&erro);
		return;
	}
	GVariant *d = g_variant_get_child_value(r, 0);
	const char *s;
	if (g_variant_lookup(d, "Id", "&s", &s)) {
		copiar(n->item.id, sizeof(n->item.id), s);
	}
	copiar(n->item.titulo, sizeof(n->item.titulo), n->item.id);
	if (g_variant_lookup(d, "Title", "&s", &s) && s[0]) {
		copiar(n->item.titulo, sizeof(n->item.titulo), s);
	}
	n->item.estado = BANDEJA_ATIVO;
	if (g_variant_lookup(d, "Status", "&s", &s)) {
		n->item.estado = !strcmp(s, "Passive") ? BANDEJA_PASSIVO : !strcmp(s, "NeedsAttention") ? BANDEJA_ATENCAO : BANDEJA_ATIVO;
	}
	const char *tema = "";
	g_variant_lookup(d, "IconThemePath", "&s", &tema);
	copiar(n->tema, sizeof(n->tema), tema);
	const char *nome = "";
	if (n->item.estado == BANDEJA_ATENCAO && g_variant_lookup(d, "AttentionIconName", "&s", &s) && s[0]) {
		nome = s;
	} else if (g_variant_lookup(d, "IconName", "&s", &s)) {
		nome = s;
	}
	n->item.icone[0] = '\0';
	if (nome[0] == '/' && existe(nome)) {
		copiar(n->item.icone, sizeof(n->item.icone), nome);
	} else if (nome[0] && n->tema[0]) {
		/* o app traz os seus próprios ícones */
		const char *ext[] = { "png", "svg" };
		for (int i = 0; i < 2 && !n->item.icone[0]; i++) {
			char *c = g_strdup_printf("%s/%s.%s", n->tema, nome, ext[i]);
			if (existe(c)) {
				copiar(n->item.icone, sizeof(n->item.icone), c);
			}
			g_free(c);
		}
	}
	if (!n->item.icone[0] && nome[0]) {
		copiar(n->item.icone, sizeof(n->item.icone), nome);
	}
	if (!nome[0] || n->item.icone[0] == '\0') {
		GVariant *px = g_variant_lookup_value(d, "IconPixmap", G_VARIANT_TYPE("a(iiay)"));
		pixmap_para_arquivo(px, n->item.chave, n->item.icone, sizeof(n->item.icone));
		if (px) {
			g_variant_unref(px);
		}
	}
	const char *m = NULL;
	n->menu_caminho[0] = '\0';
	if (g_variant_lookup(d, "Menu", "&o", &m) && m && strcmp(m, "/")) {
		copiar(n->menu_caminho, sizeof(n->menu_caminho), m);
	}
	n->item.tem_menu = n->menu_caminho[0] != '\0';
	gboolean so_menu = FALSE;
	g_variant_lookup(d, "ItemIsMenu", "b", &so_menu);
	n->item.so_menu = so_menu;
	g_variant_unref(d);
	g_variant_unref(r);
}

static struct no *achar(const char *chave) {
	for (guint i = 0; nos && i < nos->len; i++) {
		struct no *n = g_ptr_array_index(nos, i);
		if (!strcmp(n->item.chave, chave)) {
			return n;
		}
	}
	return NULL;
}

/* ---- o watcher ---- */

static void item_sinal(GDBusConnection *c, const gchar *remetente, const gchar *caminho, const gchar *iface, const gchar *sinal,
		GVariant *params, gpointer dados) {
	struct no *n = dados;
	ler_item(n);
	publicar();
}

static void item_foi(GDBusConnection *c, const gchar *nome, gpointer dados) {
	struct no *n = dados;
	g_dbus_connection_signal_unsubscribe(bus, n->assinatura);
	g_bus_unwatch_name(n->vigia);
	g_ptr_array_remove(nos, n);
	g_free(n);
	publicar();
	if (bus) {
		g_dbus_connection_emit_signal(bus, NULL, WATCHER_OBJ, WATCHER, "StatusNotifierItemUnregistered", g_variant_new("(s)", ""), NULL);
	}
}

static void registrar(const char *remetente, const char *arg) {
	char servico[96], caminho[96];
	if (arg[0] == '/') {
		copiar(servico, sizeof(servico), remetente);
		copiar(caminho, sizeof(caminho), arg);
	} else {
		const char *barra = strchr(arg, '/');
		if (barra) {
			g_snprintf(servico, sizeof(servico), "%.*s", (int)(barra - arg), arg);
			copiar(caminho, sizeof(caminho), barra);
		} else {
			copiar(servico, sizeof(servico), arg);
			copiar(caminho, sizeof(caminho), "/StatusNotifierItem");
		}
	}
	char chave[192];
	g_snprintf(chave, sizeof(chave), "%s%s", servico, caminho);
	if (achar(chave)) {
		return;
	}
	struct no *n = g_new0(struct no, 1);
	n->item.estado = BANDEJA_ATIVO;
	copiar(n->item.chave, sizeof(n->item.chave), chave);
	copiar(n->servico, sizeof(n->servico), servico);
	copiar(n->caminho, sizeof(n->caminho), caminho);
	ler_item(n);
	if (!n->item.id[0] && !n->item.titulo[0]) {
		copiar(n->item.titulo, sizeof(n->item.titulo), servico);
	}
	n->vigia = g_bus_watch_name_on_connection(bus, servico, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, item_foi, n, NULL);
	n->assinatura = g_dbus_connection_signal_subscribe(bus, servico, ITEM_IFACE, NULL, caminho, NULL, G_DBUS_SIGNAL_FLAGS_NONE, item_sinal, n, NULL);
	g_ptr_array_add(nos, n);
	publicar();
	g_dbus_connection_emit_signal(bus, NULL, WATCHER_OBJ, WATCHER, "StatusNotifierItemRegistered", g_variant_new("(s)", chave), NULL);
}

static gboolean registrar_depois(gpointer dados) {
	char **par = dados;
	registrar(par[0], par[1]);
	g_free(par[0]);
	g_free(par[1]);
	g_free(par);
	return G_SOURCE_REMOVE;
}

static const char *watcher_xml =
	"<node><interface name='org.kde.StatusNotifierWatcher'>"
	"<method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>"
	"<method name='RegisterStatusNotifierHost'><arg type='s' direction='in'/></method>"
	"<property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
	"<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
	"<property name='ProtocolVersion' type='i' access='read'/>"
	"<signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>"
	"<signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>"
	"<signal name='StatusNotifierHostRegistered'/>"
	"</interface></node>";

static void watcher_chamada(GDBusConnection *c, const gchar *remetente, const gchar *caminho, const gchar *iface, const gchar *metodo,
		GVariant *args, GDBusMethodInvocation *inv, gpointer d) {
	g_dbus_method_invocation_return_value(inv, NULL);
	if (!strcmp(metodo, "RegisterStatusNotifierItem")) {
		/* responde antes de ler o item: o app espera a resposta e só atende o D-Bus depois dela */
		const char *arg;
		g_variant_get(args, "(&s)", &arg);
		char **par = g_new0(char *, 3);
		par[0] = g_strdup(remetente);
		par[1] = g_strdup(arg);
		g_main_context_invoke(contexto, registrar_depois, par);
	}
}

static GVariant *watcher_propriedade(GDBusConnection *c, const gchar *remetente, const gchar *caminho, const gchar *iface, const gchar *nome,
		GError **erro, gpointer d) {
	if (!strcmp(nome, "RegisteredStatusNotifierItems")) {
		GVariantBuilder b;
		g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
		for (guint i = 0; nos && i < nos->len; i++) {
			g_variant_builder_add(&b, "s", ((struct no *)g_ptr_array_index(nos, i))->item.chave);
		}
		return g_variant_builder_end(&b);
	}
	if (!strcmp(nome, "IsStatusNotifierHostRegistered")) {
		return g_variant_new_boolean(TRUE);
	}
	return g_variant_new_int32(0);
}

static const GDBusInterfaceVTable watcher_vtable = { .method_call = watcher_chamada, .get_property = watcher_propriedade };

static void nome_obtido(GDBusConnection *c, const gchar *nome, gpointer d) {
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(watcher_xml, NULL);
	g_dbus_connection_register_object(c, WATCHER_OBJ, info->interfaces[0], &watcher_vtable, NULL, NULL, NULL);
	g_dbus_node_info_unref(info);
	g_dbus_connection_emit_signal(c, NULL, WATCHER_OBJ, WATCHER, "StatusNotifierHostRegistered", NULL, NULL);
}

/* ---- o menu (dbusmenu) ---- */

static void menu_texto(const char *label, char *dest, size_t tam) {
	/* "_Arquivo" é a tecla de atalho: tira o sublinhado ("__" é um sublinhado de verdade) */
	size_t j = 0;
	for (const char *p = label; *p && j + 1 < tam; p++) {
		if (*p == '_') {
			if (p[1] == '_') {
				dest[j++] = '_';
				p++;
			}
			continue;
		}
		dest[j++] = *p;
	}
	dest[j] = '\0';
}

static void menu_ler_no(GVariant *no, int nivel, struct bandeja_menu *m) {
	if (m->n >= BANDEJA_MENU_MAX) {
		return;
	}
	gint32 id;
	GVariant *props, *filhos;
	g_variant_get(no, "(i@a{sv}@av)", &id, &props, &filhos);
	const char *s;
	gboolean b;
	bool visivel = !g_variant_lookup(props, "visible", "b", &b) || b;
	bool separador = g_variant_lookup(props, "type", "&s", &s) && !strcmp(s, "separator");
	if (visivel && nivel >= 0) {
		struct bandeja_entrada *e = &m->entradas[m->n++];
		memset(e, 0, sizeof(*e));
		e->id = id;
		e->nivel = nivel;
		e->separador = separador;
		e->habilitado = !g_variant_lookup(props, "enabled", "b", &b) || b;
		if (g_variant_lookup(props, "label", "&s", &s)) {
			menu_texto(s, e->texto, sizeof(e->texto));
		}
		if (g_variant_lookup(props, "toggle-type", "&s", &s) && s[0]) {
			e->marcavel = true;
			gint32 t = 0;
			g_variant_lookup(props, "toggle-state", "i", &t);
			e->marcado = t == 1;
		}
		e->submenu = g_variant_lookup(props, "children-display", "&s", &s) && !strcmp(s, "submenu");
	}
	for (gsize i = 0; i < g_variant_n_children(filhos); i++) {
		GVariant *f = g_variant_get_child_value(filhos, i);
		GVariant *interno = g_variant_get_variant(f);
		menu_ler_no(interno, nivel + 1, m);
		g_variant_unref(interno);
		g_variant_unref(f);
	}
	g_variant_unref(props);
	g_variant_unref(filhos);
}

struct pedido {
	char chave[192];
	int x, y, id;
	int tipo; /* 0 ativar, 1 secundário, 2 menu, 3 clicar, 4 fechar */
};

static gboolean executar(gpointer dados) {
	struct pedido *p = dados;
	struct no *n = achar(p->chave);
	if (p->tipo == 4) {
		g_mutex_lock(&trinco);
		memset(&menu, 0, sizeof(menu));
		menu.geracao++;
		g_mutex_unlock(&trinco);
		avisar();
	} else if (n) {
		if (p->tipo == 0) {
			GVariant *r = n->item.so_menu ? NULL : chamar(n->servico, n->caminho, ITEM_IFACE, "Activate", g_variant_new("(ii)", p->x, p->y), NULL);
			if (r) {
				g_variant_unref(r);
			} else if (n->item.tem_menu) {
				p->tipo = 2; /* o app não ativa (ou só tem menu): abre o menu */
			}
		} else if (p->tipo == 1) {
			GVariant *r = chamar(n->servico, n->caminho, ITEM_IFACE, "SecondaryActivate", g_variant_new("(ii)", p->x, p->y), NULL);
			if (r) {
				g_variant_unref(r);
			}
		}
		if (p->tipo == 2) {
			if (!n->item.tem_menu) {
				GVariant *r = chamar(n->servico, n->caminho, ITEM_IFACE, "ContextMenu", g_variant_new("(ii)", p->x, p->y), NULL);
				if (r) {
					g_variant_unref(r);
				}
			} else {
				GVariant *r = chamar(n->servico, n->menu_caminho, MENU_IFACE, "AboutToShow", g_variant_new("(i)", 0), NULL);
				if (r) {
					g_variant_unref(r);
				}
				GError *erro = NULL;
				r = g_dbus_connection_call_sync(bus, n->servico, n->menu_caminho, MENU_IFACE, "GetLayout", g_variant_new("(iias)", 0, -1, NULL),
					G_VARIANT_TYPE("(u(ia{sv}av))"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &erro);
				if (!r) {
					g_printerr("bandeja: menu de %s: %s\n", p->chave, erro ? erro->message : "?");
					g_clear_error(&erro);
				}
				if (r) {
					struct bandeja_menu *m = g_new0(struct bandeja_menu, 1);
					GVariant *raiz = g_variant_get_child_value(r, 1);
					copiar(m->chave, sizeof(m->chave), p->chave);
					menu_ler_no(raiz, -1, m); /* a raiz não se mostra; os filhos entram no nível 0 */
					m->pronto = true;
					g_mutex_lock(&trinco);
					m->geracao = menu.geracao + 1;
					memcpy(&menu, m, sizeof(menu));
					g_mutex_unlock(&trinco);
					avisar();
					g_variant_unref(raiz);
					g_free(m);
					g_variant_unref(r);
				}
			}
		} else if (p->tipo == 3 && n->item.tem_menu) {
			GVariant *r = chamar(n->servico, n->menu_caminho, MENU_IFACE, "Event",
				g_variant_new("(isvu)", p->id, "clicked", g_variant_new_int32(0), (guint32)(g_get_monotonic_time() / 1000)), NULL);
			if (r) {
				g_variant_unref(r);
			}
		}
	}
	g_free(p);
	return G_SOURCE_REMOVE;
}

static void enviar(int tipo, const char *chave, int x, int y, int id) {
	if (!contexto) {
		return;
	}
	struct pedido *p = g_new0(struct pedido, 1);
	p->tipo = tipo;
	copiar(p->chave, sizeof(p->chave), chave);
	p->x = x;
	p->y = y;
	p->id = id;
	g_main_context_invoke(contexto, executar, p);
}

void bandeja_ativar(const char *chave, int x, int y) { enviar(0, chave, x, y, 0); }
void bandeja_secundario(const char *chave, int x, int y) { enviar(1, chave, x, y, 0); }
void bandeja_menu_pedir(const char *chave, int x, int y) { enviar(2, chave, x, y, 0); }
void bandeja_menu_clicar(const char *chave, int id) { enviar(3, chave, 0, 0, id); }
void bandeja_menu_fechar(void) { enviar(4, "", 0, 0, 0); }

static gpointer linha(gpointer d) {
	GMainContext *ctx = g_main_context_new();
	g_main_context_push_thread_default(ctx);
	nos = g_ptr_array_new();
	GError *e = NULL;
	bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &e);
	if (!bus) {
		g_clear_error(&e);
		return NULL;
	}
	g_atomic_pointer_set(&contexto, ctx);
	g_bus_own_name_on_connection(bus, WATCHER, G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT | G_BUS_NAME_OWNER_FLAGS_REPLACE, nome_obtido, NULL, NULL, NULL);
	g_main_loop_run(g_main_loop_new(ctx, FALSE));
	return NULL;
}

int bandeja_iniciar(void) {
	if (fd_aviso >= 0) {
		return fd_aviso;
	}
	fd_aviso = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	g_thread_new("lumna-bandeja", linha, NULL);
	for (int i = 0; i < 100 && !g_atomic_pointer_get(&contexto); i++) {
		g_usleep(2000);
	}
	return fd_aviso;
}

void bandeja_estado_copiar(struct bandeja_estado *dest) {
	g_mutex_lock(&trinco);
	memcpy(dest, &estado, sizeof(*dest));
	g_mutex_unlock(&trinco);
}

void bandeja_menu_copiar(struct bandeja_menu *dest) {
	g_mutex_lock(&trinco);
	memcpy(dest, &menu, sizeof(*dest));
	g_mutex_unlock(&trinco);
}
