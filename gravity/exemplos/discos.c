/*
 * lumna-discos: monta pendrives e discos externos sozinho, se a pessoa pediu (Painel > Armazenamento > Discos e pendrives).
 * Usa o monitor de volumes do GIO (gvfs + udisks2): sem sondar o disco de tempos em tempos, só reage quando um volume aparece.
 * Lê ~/.config/lumna/discos.ini a cada evento (servicos/discos_cfg.h), então ligar ou desligar no Painel vale na hora, sem reiniciar.
 *   montar_ao_conectar  um volume removível que é ligado já é montado
 *   montar_ao_entrar    os que já estão ligados quando a sessão começa
 * Só monta removíveis que o sistema deixa montar sozinhos. Não pergunta nada (um disco cifrado que pede senha fica para o Atlas ou o Painel).
 * Sobe com a sessão:  lumna-discos &
 */
#include <gio/gio.h>
#include <glib-unix.h>
#include <stdio.h>

#include "discos_cfg.h"

static GMainLoop *laco;

static void montou(GObject *origem, GAsyncResult *res, gpointer dados) {
	GError *erro = NULL;
	GVolume *v = G_VOLUME(origem);
	char *nome = g_volume_get_name(v);
	if (g_volume_mount_finish(v, res, &erro)) {
		g_message("montou %s", nome);
	} else {
		g_message("não montou %s: %s", nome, erro->message);
		g_error_free(erro);
	}
	g_free(nome);
}

/* o volume é de um aparelho removível (pendrive, disco externo, cartão)? */
static gboolean removivel(GVolume *v) {
	GDrive *d = g_volume_get_drive(v);
	if (!d) {
		return FALSE;
	}
	gboolean r = g_drive_is_removable(d) || g_drive_is_media_removable(d) || g_drive_can_eject(d);
	g_object_unref(d);
	return r;
}

static void considerar(GVolume *v, enum discos_evento ev) {
	discos_cfg c;
	discos_cfg_ler(&c);
	GMount *m = g_volume_get_mount(v);
	gboolean montado = m != NULL;
	if (m) {
		g_object_unref(m);
	}
	if (discos_deve_montar(&c, ev, removivel(v), montado, g_volume_should_automount(v) && g_volume_can_mount(v))) {
		g_volume_mount(v, G_MOUNT_MOUNT_NONE, NULL, NULL, montou, NULL);
	}
}

static void volume_ligado(GVolumeMonitor *mon, GVolume *v, gpointer dados) {
	considerar(v, DISCOS_CONECTOU);
}

/* um disco que acabou de ser ligado só ganha o volume (com o sistema de arquivos) depois de um instante: o monitor avisa quando ele existe */
static gboolean ao_entrar(gpointer dados) {
	GVolumeMonitor *mon = dados;
	for (GList *l = g_volume_monitor_get_volumes(mon); l; l = g_list_delete_link(l, l)) {
		considerar(l->data, DISCOS_ENTROU);
		g_object_unref(l->data);
	}
	return G_SOURCE_REMOVE;
}

static gboolean sair(gpointer dados) {
	g_main_loop_quit(laco);
	return G_SOURCE_REMOVE;
}

int main(void) {
	GVolumeMonitor *mon = g_volume_monitor_get();
	g_signal_connect(mon, "volume-added", G_CALLBACK(volume_ligado), NULL);
	g_timeout_add_seconds(2, ao_entrar, mon); /* dá tempo ao gvfs de listar o que já estava ligado */
	laco = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGTERM, sair, NULL);
	g_unix_signal_add(SIGINT, sair, NULL);
	g_main_loop_run(laco);
	g_object_unref(mon);
	return 0;
}
