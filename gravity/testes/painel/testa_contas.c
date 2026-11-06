/* servicos/contas.c com um /etc falso, e o session/lumna-usuarios em modo seco (LUMNA_USUARIOS_SECO). ./testa_contas PASTA AJUDANTE */
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "contas.h"

int main(int argc, char **argv) {
	const char *raiz = argv[1];
	char *etc = g_build_filename(raiz, "etc", NULL), *home = g_build_filename(raiz, "home", NULL);
	g_mkdir_with_parents(etc, 0755);
	g_mkdir_with_parents(home, 0755);
	setenv("LUMNA_ETC", etc, 1);
	setenv("HOME", home, 1);
	setenv("LUMNA_USUARIO", "maria", 1);
	setenv("LOGNAME", "maria", 1);
	setenv("LUMNA_USUARIOS_AJUDANTE", argv[2], 1);
	setenv("LUMNA_SEM_PKEXEC", "1", 1);
	setenv("LUMNA_USUARIOS_SECO", "1", 1);
	char *passwd = g_build_filename(etc, "passwd", NULL), *grupo = g_build_filename(etc, "group", NULL);
	g_file_set_contents(passwd, "root:x:0:0:root:/root:/bin/bash\ndaemon:x:1:1::/:/usr/bin/nologin\njoao:x:1001:1001:João Silva,,,:/home/joao:/bin/bash\n"
		"maria:x:1000:1000:Maria:/home/maria:/bin/zsh\nsistema:x:1002:1002::/var/x:/usr/bin/nologin\nnobody:x:65534:65534::/:/usr/bin/nologin\n", -1, NULL);
	g_file_set_contents(grupo, "root:x:0:\nwheel:x:998:maria\nsudo:x:27:joao,outro\n", -1, NULL);
	struct conta l[CONTAS_MAX];
	int n = contas_listar(l, CONTAS_MAX);
	assert(n == 2);
	assert(!strcmp(l[0].login, "maria") && l[0].atual && l[0].admin && !strcmp(l[0].nome, "Maria")); /* quem está logado primeiro */
	assert(!strcmp(l[1].login, "joao") && !l[1].atual && l[1].admin && !strcmp(l[1].nome, "João Silva"));
	puts("listar ok");
	/* o ajudante em modo seco (getent falso no PATH): aceita e recusa */
	char erro[300] = "";
	assert(contas_adicionar("ana", "Ana Souza", "segredo123", true, erro, sizeof(erro)));
	assert(!contas_adicionar("Ana Maiuscula", "x", "senha", false, erro, sizeof(erro)) && strstr(erro, "letras minúsculas"));
	assert(!contas_adicionar("../x", "x", "senha", false, erro, sizeof(erro)));
	assert(!contas_adicionar("joao", "x", "senha", false, erro, sizeof(erro)) && strstr(erro, "já existe"));
	assert(!contas_adicionar("bia", "Bia", "", false, erro, sizeof(erro)));
	assert(contas_admin("joao", false, erro, sizeof(erro)) && contas_nome("joao", "João S.", erro, sizeof(erro)));
	assert(contas_remover("joao", true, erro, sizeof(erro)));
	assert(!contas_remover("root", false, erro, sizeof(erro)) && strstr(erro, "não é uma conta"));
	assert(!contas_remover("sistema", false, erro, sizeof(erro)));
	assert(contas_senha("joao", "nova-senha", erro, sizeof(erro)));
	puts("ajudante ok");
	/* a foto: ~/.face quadrada */
	char *img = g_build_filename(raiz, "foto.png", NULL);
	GdkPixbuf *p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 400, 200);
	gdk_pixbuf_fill(p, 0xff8800ff);
	assert(gdk_pixbuf_save(p, img, "png", NULL, NULL));
	assert(!contas_foto());
	assert(contas_foto_definir(img, erro, sizeof(erro)) && contas_foto());
	GdkPixbuf *q = gdk_pixbuf_new_from_file(contas_foto(), NULL);
	assert(q && gdk_pixbuf_get_width(q) == gdk_pixbuf_get_height(q) && gdk_pixbuf_get_width(q) <= 256 && gdk_pixbuf_get_width(q) >= 128);
	assert(!contas_foto_definir("/nao/existe.png", erro, sizeof(erro)));
	assert(contas_foto_tirar() && !contas_foto());
	puts("foto ok");
	puts("contas ok");
	return 0;
}
