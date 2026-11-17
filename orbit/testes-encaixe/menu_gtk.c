/*
 * Cliente de teste: uma janela GTK3 que abre um menu de contexto (xdg_popup) ao clicar com o botão direito, e escreve no arquivo dado
 * (argv[1]) onde o menu apareceu em relação à janela, para o teste conferir que ele nasce junto do cursor.
 */
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <stdio.h>

static const char *saida;

static void escreve(const char *txt) {
	FILE *f = fopen(saida, "a");
	if (f) {
		fputs(txt, f);
		fclose(f);
	}
}

static gboolean botao(GtkWidget *w, GdkEventButton *e, gpointer d) {
	if (e->button != 3) {
		return FALSE;
	}
	char b[128];
	snprintf(b, sizeof b, "clique %d %d\n", (int)e->x, (int)e->y);
	escreve(b);
	GtkWidget *menu = gtk_menu_new();
	for (int i = 0; i < 4; i++) {
		char t[32];
		snprintf(t, sizeof t, "Item %d", i + 1);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_menu_item_new_with_label(t));
	}
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)e);
	return TRUE;
}

int main(int argc, char **argv) {
	saida = argc > 1 ? argv[1] : "/dev/null";
	gtk_init(&argc, &argv);
	GtkWidget *j = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(j), 700, 500);
	gtk_window_set_title(GTK_WINDOW(j), "menu-teste");
	GtkWidget *ev = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(j), ev);
	gtk_container_add(GTK_CONTAINER(ev), gtk_label_new("clique com o botão direito"));
	gtk_widget_add_events(ev, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(ev, "button-press-event", G_CALLBACK(botao), NULL);
	g_signal_connect(j, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(j);
	gtk_main();
	return 0;
}
