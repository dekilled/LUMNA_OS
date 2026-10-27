#define _DEFAULT_SOURCE
#include "pacotes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const struct pacote *app_de(const struct pacotes_bd *bd, const char *nome) {
	const struct pacote **l;
	int n = pacotes_apps(bd, &l);
	for (int i = 0; i < n; i++) {
		if (!strcmp(l[i]->nome, nome)) {
			return l[i];
		}
	}
	return NULL;
}

static bool tem(GPtrArray *a, const char *nome) {
	for (guint i = 0; i < a->len; i++) {
		if (!strcmp(a->pdata[i], nome)) {
			return true;
		}
	}
	return false;
}

int main(void) {
	struct pacotes_bd *bd = pacotes_carregar();
	assert(bd);
	const struct pacote **l;
	int n = pacotes_apps(bd, &l);
	assert(n == 4); /* aurapp, Foo App, MyApp (AppImage), optapp: em ordem de nome; hiddenapp e libbar não são apps */
	assert(!strcmp(l[0]->app_nome, "AUR App") && !strcmp(l[1]->app_nome, "Foo App") && !strcmp(l[2]->app_nome, "My App") && !strcmp(l[3]->app_nome, "Opt App"));

	const struct pacote *foo = app_de(bd, "foo");
	assert(foo && !foo->dependencia && foo->tamanho == 5000000 && foo->origem == PAC_PACMAN && !strcmp(foo->fonte, "pacman"));
	assert(foo->instalado_em == 1700000000 && foo->compilado_em == 1690000000 && foo->modificado_em == 1750000000);
	assert(!strcmp(foo->dir, "/usr/lib/foo") && !strcmp(foo->app_id, "foo.desktop") && !strcmp(foo->icone, "foo-icon") && !strcmp(foo->licenca, "GPL, MIT"));
	assert(!strcmp(foo->url, "https://foo.example") && !foo->desativado);

	const struct pacote *aur = app_de(bd, "aurapp");
	assert(aur->origem == PAC_AUR && !strcmp(aur->fonte, "AUR (yay)"));
	const struct pacote *opt = app_de(bd, "optapp");
	assert(!strcmp(opt->dir, "/opt/optapp") && opt->origem == PAC_PACMAN);
	const struct pacote *img = l[2];
	assert(img->origem == PAC_APPIMAGE && !strcmp(img->fonte, "AppImage") && img->tamanho == 12345 && !strcmp(img->app_id, "myapp.desktop"));
	assert(strstr(img->caminho, "Applications/MyApp-1.0.AppImage") && strstr(img->dir, "Applications"));
	assert(pacotes_apps_tamanho(bd) == 5000000 + 2000000 + 12345 + 7000000);

	/* o grafo */
	const struct pacote *libbar = pacotes_achar(bd, "libbar");
	assert(libbar && libbar->dependencia);
	GPtrArray *a = pacotes_depende_de(bd, foo);
	assert(a->len == 2 && tem(a, "libbar") && tem(a, "libonly"));
	g_ptr_array_free(a, TRUE);
	a = pacotes_necessario_por(bd, libbar);
	assert(a->len == 2 && tem(a, "foo") && tem(a, "baz"));
	g_ptr_array_free(a, TRUE);
	a = pacotes_opcional_para(bd, foo);
	assert(a->len == 1 && tem(a, "baz"));
	g_ptr_array_free(a, TRUE);

	/* a remoção: o foo leva o libonly (só ele o usa) e deixa o libbar (o baz ainda usa) */
	struct pacotes_remocao r;
	pacotes_previa_remocao(bd, foo, false, &r);
	assert(r.removidos->len == 2 && tem(r.removidos, "foo") && tem(r.removidos, "libonly") && r.quebrariam->len == 0);
	assert(r.liberado == 5000000 + 400000);
	pacotes_remocao_liberar(&r);
	/* o libbar: quebraria foo e baz; em cascata sai tudo, com o libonly */
	pacotes_previa_remocao(bd, libbar, false, &r);
	assert(r.quebrariam->len == 2 && r.removidos->len == 1);
	pacotes_remocao_liberar(&r);
	pacotes_previa_remocao(bd, libbar, true, &r);
	assert(tem(r.removidos, "libbar") && tem(r.removidos, "foo") && tem(r.removidos, "baz") && tem(r.removidos, "libonly") && r.removidos->len == 4);
	pacotes_remocao_liberar(&r);

	/* atualização marcada */
	pacotes_marcar_atualizacao(bd, "foo", "1.1-1");
	assert(foo->tem_atualizacao && !strcmp(foo->nova_versao, "1.1-1"));

	/* desativar e ativar: um .desktop do usuário cobre o do sistema */
	assert(pacotes_definir_ativo(foo, false));
	char *dir = g_build_filename(g_get_user_data_dir(), "applications", "foo.desktop", NULL);
	char *t = NULL;
	assert(g_file_get_contents(dir, &t, NULL, NULL) && strstr(t, "Hidden=true") && strstr(t, "X-LUMNA-Desativado=true"));
	g_free(t);
	struct pacotes_bd *bd2 = pacotes_carregar();
	assert(app_de(bd2, "foo")->desativado);
	pacotes_liberar(bd2);
	assert(pacotes_definir_ativo(foo, true) && !g_file_test(dir, G_FILE_TEST_EXISTS));
	/* o AppImage integrado: o .desktop é da pessoa, só ganha e perde as linhas do Painel */
	assert(pacotes_definir_ativo(img, false));
	char *dimg = g_build_filename(g_get_user_data_dir(), "applications", "myapp.desktop", NULL);
	assert(g_file_get_contents(dimg, &t, NULL, NULL) && strstr(t, "Hidden=true") && strstr(t, "Name=My App") && !strstr(t, "X-LUMNA-Criado"));
	g_free(t);
	assert(pacotes_definir_ativo(img, true) && g_file_get_contents(dimg, &t, NULL, NULL) && !strstr(t, "Hidden") && strstr(t, "Name=My App") && strstr(t, "Exec="));
	g_free(t);

	/* desinstalar: o pacman falso anota o que recebeu */
	char erro[400];
	assert(pacotes_remover(foo, false, erro, sizeof(erro)) == 0);
	char *args = NULL;
	assert(g_file_get_contents(g_getenv("PACMAN_FALSO_LOG"), &args, NULL, NULL) && strstr(args, "-Rns --noconfirm foo"));
	g_free(args);
	assert(pacotes_remover(libbar, true, erro, sizeof(erro)) != 0 && strstr(erro, "breaks dependency")); /* o falso recusa o libbar */
	assert(g_file_get_contents(g_getenv("PACMAN_FALSO_LOG"), &args, NULL, NULL) && strstr(args, "-Rcns --noconfirm libbar"));
	g_free(args);

	/* apagar o AppImage */
	assert(pacotes_remover_appimage(img, erro, sizeof(erro)) == 0 && access(img->caminho, F_OK) != 0 && access(dimg, F_OK) != 0);
	assert(pacotes_remover_appimage(foo, erro, sizeof(erro)) != 0);

	g_free(dir);
	g_free(dimg);
	pacotes_liberar(bd);
	puts("pacotes ok");
	return 0;
}
