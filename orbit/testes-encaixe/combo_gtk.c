/*
 * Cliente de teste: uma janela GTK3 com a "barra lateral direita" de um app de impressão 3D: vários GtkComboBoxText (dropdowns) numa coluna, encostados na borda direita.
 * Escreve no arquivo dado (argv[1]) cada dropdown que abriu ("abriu N X Y W H": a caixa do popup em relação à janela) ou fechou ("fechou N").
 *   combo_gtk ARQUIVO [LARGURA] [ALTURA]
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

static const char *saida;
static void escreve(const char *t) { FILE *f = fopen(saida, "a"); if (f) { fputs(t, f); fclose(f); } }

static void notificou(GObject *o, GParamSpec *p, gpointer n) {
	gboolean aberto;
	g_object_get(o, "popup-shown", &aberto, NULL);
	char b[64];
	snprintf(b, sizeof b, "%s %d\n", aberto ? "abriu" : "fechou", GPOINTER_TO_INT(n));
	escreve(b);
}

static void mudou(GtkComboBox *c, gpointer n) {
	char b[64];
	snprintf(b, sizeof b, "escolheu %d %d\n", GPOINTER_TO_INT(n), gtk_combo_box_get_active(c));
	escreve(b);
}

int main(int argc, char **argv) {
	saida = argc > 1 ? argv[1] : "/dev/null";
	int w = argc > 2 ? atoi(argv[2]) : 900, h = argc > 3 ? atoi(argv[3]) : 600;
	gtk_init(&argc, &argv);
	GtkWidget *j = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(j), w, h);
	gtk_window_set_title(GTK_WINDOW(j), "combo-teste");
	GtkWidget *caixa = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *vazio = gtk_label_new("area 3D");
	gtk_box_pack_start(GTK_BOX(caixa), vazio, TRUE, TRUE, 0);
	GtkWidget *lateral = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_size_request(lateral, 260, -1);
	for (int n = 1; n <= 4; n++) {
		GtkWidget *c = gtk_combo_box_text_new();
		for (int i = 0; i < (n == 4 ? 60 : 6); i++) {
			char t[64];
			snprintf(t, sizeof t, "Perfil %d item %d", n, i + 1);
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), t);
		}
		gtk_combo_box_set_active(GTK_COMBO_BOX(c), 0);
		g_signal_connect(c, "changed", G_CALLBACK(mudou), GINT_TO_POINTER(n));
		g_signal_connect(c, "notify::popup-shown", G_CALLBACK(notificou), GINT_TO_POINTER(n));
		gtk_box_pack_start(GTK_BOX(lateral), c, FALSE, FALSE, 0);
	}
	gtk_box_pack_end(GTK_BOX(caixa), lateral, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(j), caixa);
	g_signal_connect(j, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(j);
	gtk_main();
	return 0;
}
