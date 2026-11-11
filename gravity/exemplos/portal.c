/*
 * lumna-portal: o que o sistema pede ao gerenciador de arquivos, respondido pelo Atlas.
 *
 *  - org.freedesktop.FileManager1 (ShowFolders, ShowItems, ShowItemProperties): é o "Mostrar na pasta" dos downloads do Firefox e do Chromium,
 *    do Steam e de qualquer app. Cada pedido abre uma janela do Atlas na pasta (e, num arquivo, com ele selecionado).
 *  - org.freedesktop.impl.portal.FileChooser (OpenFile, SaveFile, SaveFiles): o backend do xdg-desktop-portal para o seletor de arquivos (abrir, enviar, salvar,
 *    escolher pasta) de qualquer app. Cada pedido abre o Atlas no modo escolher (atlas --escolher=...) e devolve o que a pessoa escolheu.
 *
 * O serviço sobe sozinho pelo D-Bus e sai depois de um minuto sem pedidos. LUMNA_ATLAS_EXE troca o programa que abre a janela (os testes põem um de mentira).
 */
#include <gio/gio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define OCIOSO_SEG 60

static GMainLoop *laco;
static guint ocioso_id;

static GHashTable *pedidos; /* handle -> struct pedido (o seletor de arquivos abertos agora) */

static gboolean ocioso_acabou(gpointer d) {
	if (pedidos && g_hash_table_size(pedidos) > 0) { /* um seletor aberto: a pessoa pode demorar */
		return G_SOURCE_CONTINUE;
	}
	g_main_loop_quit(laco);
	return G_SOURCE_REMOVE;
}

static void ocioso_reiniciar(void) {
	if (ocioso_id) {
		g_source_remove(ocioso_id);
	}
	ocioso_id = g_timeout_add_seconds(OCIOSO_SEG, ocioso_acabou, NULL);
}

static const char *atlas_exe(void) {
	const char *e = g_getenv("LUMNA_ATLAS_EXE");
	return e && *e ? e : "lumna-atlas";
}

/* abre uma janela do Atlas em um caminho (pasta, ou arquivo: o Atlas abre a pasta dele com o arquivo selecionado) */
static void atlas_abrir(const char *caminho) {
	char *argv[] = { (char *)atlas_exe(), (char *)caminho, NULL };
	GError *err = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &err)) {
		g_printerr("lumna-portal: não abri o Atlas: %s\n", err->message);
		g_clear_error(&err);
	}
}

static const char XML[] =
	"<node><interface name='org.freedesktop.FileManager1'>"
	"<method name='ShowFolders'><arg type='as' name='URIs' direction='in'/><arg type='s' name='StartupId' direction='in'/></method>"
	"<method name='ShowItems'><arg type='as' name='URIs' direction='in'/><arg type='s' name='StartupId' direction='in'/></method>"
	"<method name='ShowItemProperties'><arg type='as' name='URIs' direction='in'/><arg type='s' name='StartupId' direction='in'/></method>"
	"</interface></node>";

static void chamada(GDBusConnection *c, const char *remetente, const char *caminho_obj, const char *interface, const char *metodo, GVariant *params,
		GDBusMethodInvocation *inv, gpointer dados) {
	ocioso_reiniciar();
	GVariantIter *it;
	const char *startup;
	g_variant_get(params, "(as&s)", &it, &startup);
	char *uri;
	int n = 0;
	while (g_variant_iter_next(it, "s", &uri)) {
		char *caminho = g_filename_from_uri(uri, NULL, NULL);
		if (caminho && g_path_is_absolute(caminho) && n < 16) { /* quantas janelas de uma vez: não abre dezenas se pedirem dezenas */
			atlas_abrir(caminho);
			n++;
		}
		g_free(caminho);
		g_free(uri);
	}
	g_variant_iter_free(it);
	g_dbus_method_invocation_return_value(inv, NULL);
}


/* ---- o seletor de arquivos (org.freedesktop.impl.portal.FileChooser) ---- */

struct pedido {
	GDBusMethodInvocation *inv;
	GDBusConnection *con;
	char *handle;
	guint registro;
	GPid pid;
	int fd_saida;
	gboolean fechado;   /* a pessoa (ou o app) cancelou pelo Request.Close */
	char **nomes_varios;  /* SaveFiles: os nomes dos arquivos; a pessoa escolhe a pasta */
};

static void pedido_liberar(struct pedido *p) {
	if (p->registro) {
		g_dbus_connection_unregister_object(p->con, p->registro);
	}
	g_hash_table_remove(pedidos, p->handle);
	g_free(p->handle);
	g_strfreev(p->nomes_varios);
	g_object_unref(p->con);
	g_free(p);
}

static void filho_acabou(GPid pid, gint estado, gpointer dados) {
	struct pedido *p = dados;
	GString *saida = g_string_new(NULL);
	char buf[4096];
	ssize_t n;
	while ((n = read(p->fd_saida, buf, sizeof(buf))) > 0) {
		g_string_append_len(saida, buf, n);
	}
	close(p->fd_saida);
	g_spawn_close_pid(pid);
	guint resposta = p->fechado ? 2 : (g_spawn_check_wait_status(estado, NULL) ? 0 : 1);
	GVariantBuilder uris, resultado;
	g_variant_builder_init(&uris, G_VARIANT_TYPE("as"));
	if (resposta == 0) {
		char **linhas = g_strsplit(saida->str, "\n", -1);
		for (int i = 0; linhas[i]; i++) {
			if (linhas[i][0] != '/') {
				continue;
			}
			if (p->nomes_varios) { /* SaveFiles: o que veio é a pasta; os arquivos ficam nela, com os nomes que o app mandou */
				for (int k = 0; p->nomes_varios[k]; k++) {
					char *alvo = g_build_filename(linhas[i], p->nomes_varios[k], NULL);
					char *uri = g_filename_to_uri(alvo, NULL, NULL);
					if (uri) {
						g_variant_builder_add(&uris, "s", uri);
					}
					g_free(uri);
					g_free(alvo);
				}
				break;
			}
			char *uri = g_filename_to_uri(linhas[i], NULL, NULL);
			if (uri) {
				g_variant_builder_add(&uris, "s", uri);
				g_free(uri);
			}
		}
		g_strfreev(linhas);
	}
	g_variant_builder_init(&resultado, G_VARIANT_TYPE("a{sv}"));
	if (resposta == 0) {
		g_variant_builder_add(&resultado, "{sv}", "uris", g_variant_builder_end(&uris));
	}
	g_dbus_method_invocation_return_value(p->inv, g_variant_new("(ua{sv})", resposta, &resultado));
	g_string_free(saida, TRUE);
	pedido_liberar(p);
	ocioso_reiniciar();
}

/* Request.Close: o app desistiu; o seletor fecha e a chamada responde 2 */
static void request_chamada(GDBusConnection *c, const char *remetente, const char *caminho, const char *interface, const char *metodo, GVariant *params,
		GDBusMethodInvocation *inv, gpointer dados) {
	struct pedido *p = dados;
	if (!p->fechado) {
		p->fechado = TRUE;
		kill(p->pid, SIGTERM);
	}
	g_dbus_method_invocation_return_value(inv, NULL);
}

static const GDBusInterfaceVTable REQUEST_VTABLE = { .method_call = request_chamada };
static const char REQUEST_XML[] = "<node><interface name='org.freedesktop.impl.portal.Request'><method name='Close'/></interface></node>";

static void acrescentar_filtros(GPtrArray *args, GVariant *filtros) {
	if (!filtros) {
		return;
	}
	GVariantIter *it;
	g_variant_get(filtros, "a(sa(us))", &it);
	const char *nome;
	GVariantIter *pads;
	while (g_variant_iter_next(it, "(&sa(us))", &nome, &pads)) {
		GString *f = g_string_new("--filtro=");
		g_string_append_printf(f, "%s::", nome);
		guint tipo;
		const char *padrao;
		gboolean primeiro = TRUE;
		while (g_variant_iter_next(pads, "(u&s)", &tipo, &padrao)) {
			g_string_append_printf(f, "%s%c=%s", primeiro ? "" : ",", tipo == 0 ? 'g' : 'm', padrao);
			primeiro = FALSE;
		}
		g_variant_iter_free(pads);
		g_ptr_array_add(args, g_string_free(f, FALSE));
	}
	g_variant_iter_free(it);
}

/* os bytes de um "ay" de caminho (terminado em NUL) */
static char *caminho_de_ay(GVariant *v) {
	if (!v) {
		return NULL;
	}
	gsize n;
	const char *b = g_variant_get_fixed_array(v, &n, 1);
	return n && b ? g_strndup(b, strnlen(b, n)) : NULL;
}

static void seletor_chamada(GDBusConnection *c, const char *remetente, const char *caminho_obj, const char *interface, const char *metodo, GVariant *params,
		GDBusMethodInvocation *inv, gpointer dados) {
	ocioso_reiniciar();
	const char *handle, *app_id, *janela, *titulo;
	GVariant *opcoes;
	g_variant_get(params, "(&o&s&s&s@a{sv})", &handle, &app_id, &janela, &titulo, &opcoes);
	gboolean salvar = !strcmp(metodo, "SaveFile"), salvar_varios = !strcmp(metodo, "SaveFiles");
	gboolean multiplo = FALSE, pasta = FALSE;
	g_variant_lookup(opcoes, "multiple", "b", &multiplo);
	g_variant_lookup(opcoes, "directory", "b", &pasta);
	const char *modo = salvar ? "salvar" : (salvar_varios || pasta) ? "pasta" : multiplo ? "abrir-varios" : "abrir";
	GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(args, g_strdup(atlas_exe()));
	g_ptr_array_add(args, g_strdup_printf("--escolher=%s", modo));
	g_ptr_array_add(args, g_strdup_printf("--titulo=%s", titulo && *titulo ? titulo : (salvar ? "Salvar" : "Abrir")));
	GVariant *v = g_variant_lookup_value(opcoes, "current_folder", G_VARIANT_TYPE("ay"));
	char *pasta_ini = caminho_de_ay(v);
	if (v) {
		g_variant_unref(v);
	}
	const char *nome = NULL;
	char *do_arquivo = NULL;
	if (g_variant_lookup(opcoes, "current_name", "&s", &nome) && *nome) {
		g_ptr_array_add(args, g_strdup_printf("--nome=%s", nome));
	}
	v = g_variant_lookup_value(opcoes, "current_file", G_VARIANT_TYPE("ay"));
	do_arquivo = caminho_de_ay(v);
	if (v) {
		g_variant_unref(v);
	}
	if (do_arquivo && !nome) { /* um arquivo que já existe: abre na pasta dele com o nome dele */
		char *b = g_path_get_basename(do_arquivo);
		g_ptr_array_add(args, g_strdup_printf("--nome=%s", b));
		g_free(b);
		if (!pasta_ini) {
			pasta_ini = g_path_get_dirname(do_arquivo);
		}
	}
	g_free(do_arquivo);
	if (pasta_ini) {
		g_ptr_array_add(args, g_strdup_printf("--pasta=%s", pasta_ini));
		g_free(pasta_ini);
	}
	const char *rotulo;
	if (g_variant_lookup(opcoes, "accept_label", "&s", &rotulo) && *rotulo) {
		GString *limpo = g_string_new(NULL); /* o "_" de atalho do GTK ("_Abrir") não vai no botão */
		for (const char *q = rotulo; *q; q++) {
			if (*q != '_') {
				g_string_append_c(limpo, *q);
			}
		}
		g_ptr_array_add(args, g_strdup_printf("--rotulo=%s", limpo->str));
		g_string_free(limpo, TRUE);
	}
	v = g_variant_lookup_value(opcoes, "filters", G_VARIANT_TYPE("a(sa(us))"));
	acrescentar_filtros(args, v);
	if (v) {
		g_variant_unref(v);
	}
	g_ptr_array_add(args, NULL);

	struct pedido *p = g_new0(struct pedido, 1);
	p->inv = inv;
	p->con = g_object_ref(c);
	p->handle = g_strdup(handle);
	if (salvar_varios) {
		GVariant *fs = g_variant_lookup_value(opcoes, "files", G_VARIANT_TYPE("aay"));
		GPtrArray *nomes = g_ptr_array_new();
		GVariantIter *fi;
		if (fs) {
			g_variant_get(fs, "aay", &fi);
			GVariant *um;
			while ((um = g_variant_iter_next_value(fi))) {
				char *n = caminho_de_ay(um);
				if (n && *n && !strchr(n, '/')) {
					g_ptr_array_add(nomes, n);
				} else {
					g_free(n);
				}
				g_variant_unref(um);
			}
			g_variant_iter_free(fi);
			g_variant_unref(fs);
		}
		g_ptr_array_add(nomes, NULL);
		p->nomes_varios = (char **)g_ptr_array_free(nomes, FALSE);
	}
	GError *err = NULL;
	gint fd = -1;
	if (!g_spawn_async_with_pipes(NULL, (char **)args->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &p->pid, NULL, &fd, NULL, &err)) {
		g_printerr("lumna-portal: não abri o seletor: %s\n", err->message);
		g_clear_error(&err);
		GVariantBuilder vazio;
		g_variant_builder_init(&vazio, G_VARIANT_TYPE("a{sv}"));
		g_dbus_method_invocation_return_value(inv, g_variant_new("(ua{sv})", 2, &vazio));
		g_ptr_array_free(args, TRUE);
		g_strfreev(p->nomes_varios);
		g_object_unref(p->con);
		g_free(p->handle);
		g_free(p);
		g_variant_unref(opcoes);
		return;
	}
	p->fd_saida = fd;
	g_ptr_array_free(args, TRUE);
	g_variant_unref(opcoes);
	if (!pedidos) {
		pedidos = g_hash_table_new(g_str_hash, g_str_equal);
	}
	g_hash_table_insert(pedidos, p->handle, p);
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(REQUEST_XML, NULL);
	p->registro = g_dbus_connection_register_object(c, handle, info->interfaces[0], &REQUEST_VTABLE, p, NULL, NULL);
	g_dbus_node_info_unref(info);
	g_child_watch_add(p->pid, filho_acabou, p);
}

static GVariant *seletor_propriedade(GDBusConnection *c, const char *remetente, const char *caminho, const char *interface, const char *nome, GError **err, gpointer dados) {
	return g_variant_new_uint32(3);
}

static const GDBusInterfaceVTable SELETOR_VTABLE = { .method_call = seletor_chamada, .get_property = seletor_propriedade };
static const char SELETOR_XML[] =
	"<node><interface name='org.freedesktop.impl.portal.FileChooser'>"
	"<method name='OpenFile'><arg type='o' name='handle' direction='in'/><arg type='s' name='app_id' direction='in'/><arg type='s' name='parent_window' direction='in'/>"
	"<arg type='s' name='title' direction='in'/><arg type='a{sv}' name='options' direction='in'/><arg type='u' name='response' direction='out'/><arg type='a{sv}' name='results' direction='out'/></method>"
	"<method name='SaveFile'><arg type='o' name='handle' direction='in'/><arg type='s' name='app_id' direction='in'/><arg type='s' name='parent_window' direction='in'/>"
	"<arg type='s' name='title' direction='in'/><arg type='a{sv}' name='options' direction='in'/><arg type='u' name='response' direction='out'/><arg type='a{sv}' name='results' direction='out'/></method>"
	"<method name='SaveFiles'><arg type='o' name='handle' direction='in'/><arg type='s' name='app_id' direction='in'/><arg type='s' name='parent_window' direction='in'/>"
	"<arg type='s' name='title' direction='in'/><arg type='a{sv}' name='options' direction='in'/><arg type='u' name='response' direction='out'/><arg type='a{sv}' name='results' direction='out'/></method>"
	"<property name='version' type='u' access='read'/></interface></node>";

static const GDBusInterfaceVTable VTABLE = { .method_call = chamada };

static void ao_ter_barramento(GDBusConnection *c, const char *nome, gpointer dados) {
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(XML, NULL);
	GError *err = NULL;
	if (!g_dbus_connection_register_object(c, "/org/freedesktop/FileManager1", info->interfaces[0], &VTABLE, NULL, NULL, &err)) {
		g_printerr("lumna-portal: %s\n", err->message);
		g_main_loop_quit(laco);
	}
	g_dbus_node_info_unref(info);
	GDBusNodeInfo *sel = g_dbus_node_info_new_for_xml(SELETOR_XML, NULL);
	if (!g_dbus_connection_register_object(c, "/org/freedesktop/portal/desktop", sel->interfaces[0], &SELETOR_VTABLE, NULL, NULL, &err)) {
		g_printerr("lumna-portal: %s\n", err->message);
		g_clear_error(&err);
	}
	g_dbus_node_info_unref(sel);
}

static gboolean nome_adquirido;

static void nome_ganho(GDBusConnection *c, const char *nome, gpointer dados) {
	nome_adquirido = TRUE;
}

static void nome_perdido(GDBusConnection *c, const char *nome, gpointer dados) {
	if (!nome_adquirido) { /* outro gerenciador de arquivos (o Thunar) já tem o FileManager1: o seletor de arquivos continua daqui assim mesmo */
		g_printerr("lumna-portal: o nome %s já tem dono (outro gerenciador de arquivos); fico só com o seletor de arquivos\n", nome);
	}
}

static void backend_perdido(GDBusConnection *c, const char *nome, gpointer dados) {
	g_main_loop_quit(laco); /* sem o nome do backend (ou sem barramento: a sessão acabou) não há o que fazer */
}

int main(void) {
	laco = g_main_loop_new(NULL, FALSE);
	guint dono = g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.FileManager1", G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT, ao_ter_barramento, nome_ganho, nome_perdido, NULL, NULL);
	/* o backend do seletor de arquivos do xdg-desktop-portal: um nome só dele (se o FileManager1 for de outro programa, este continua) */
	guint dono2 = g_bus_own_name(G_BUS_TYPE_SESSION, "org.freedesktop.impl.portal.desktop.lumna", G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, backend_perdido, NULL, NULL);
	ocioso_reiniciar();
	g_main_loop_run(laco);
	g_bus_unown_name(dono);
	g_bus_unown_name(dono2);
	return 0;
}
