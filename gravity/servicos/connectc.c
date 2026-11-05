#include "connectc.h"

#include <gio/gio.h>
#include <string.h>

bool connectc_pedido(const char *pedido, char *resposta, size_t tam) {
	const char *run = g_get_user_runtime_dir();
	if (!run || !tam) {
		return false;
	}
	char *caminho = g_build_filename(run, "lumna-connect.sock", NULL);
	GSocketClient *cli = g_socket_client_new();
	g_socket_client_set_timeout(cli, 2);
	GSocketAddress *end = g_unix_socket_address_new(caminho);
	GSocketConnection *con = g_socket_client_connect(cli, G_SOCKET_CONNECTABLE(end), NULL, NULL);
	bool ok = false;
	if (con) {
		g_socket_set_timeout(g_socket_connection_get_socket(con), 2);
		char *linha = g_strdup_printf("%s\n", pedido);
		GOutputStream *saida = g_io_stream_get_output_stream(G_IO_STREAM(con));
		if (g_output_stream_write_all(saida, linha, strlen(linha), NULL, NULL, NULL)) {
			GDataInputStream *entrada = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(con)));
			char *r = g_data_input_stream_read_line(entrada, NULL, NULL, NULL);
			if (r) {
				g_strlcpy(resposta, r, tam);
				ok = true;
			}
			g_free(r);
			g_object_unref(entrada);
		}
		g_free(linha);
		g_io_stream_close(G_IO_STREAM(con), NULL, NULL);
		g_object_unref(con);
	}
	g_object_unref(end);
	g_object_unref(cli);
	g_free(caminho);
	return ok;
}
