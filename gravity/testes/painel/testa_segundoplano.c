/* Os serviços do usuário (servicos/segundoplano.c), com o systemctl falso de testes/painel/bin. */
#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "segundoplano.h"

static const struct fundo_servico *achar(const struct fundo_servico *l, int n, const char *u) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(l[i].unidade, u)) {
			return &l[i];
		}
	}
	return NULL;
}

int main(void) {
	struct fundo_servico l[SEGUNDOPLANO_MAX];
	int n = fundo_listar(l, SEGUNDOPLANO_MAX);
	assert(n == 3); /* os static, generated e os de modelo (@) não entram */
	assert(!strcmp(l[0].unidade, "backup.service") && !strcmp(l[2].unidade, "syncthing.service")); /* em ordem */
	assert(achar(l, n, "syncthing.service")->ligado && achar(l, n, "syncthing.service")->rodando);
	assert(!strcmp(achar(l, n, "syncthing.service")->descricao, "Syncthing - Open Source Continuous File Synchronization"));
	assert(!achar(l, n, "backup.service")->ligado && !achar(l, n, "backup.service")->rodando);
	puts("lista ok");
	char erro[200] = "";
	assert(fundo_ligar("backup.service", true, erro, sizeof(erro)));
	n = fundo_listar(l, SEGUNDOPLANO_MAX);
	assert(achar(l, n, "backup.service")->ligado && achar(l, n, "backup.service")->rodando);
	assert(fundo_rodar("backup.service", false, erro, sizeof(erro)));
	n = fundo_listar(l, SEGUNDOPLANO_MAX);
	assert(achar(l, n, "backup.service")->ligado && !achar(l, n, "backup.service")->rodando);
	assert(fundo_ligar("backup.service", false, erro, sizeof(erro)));
	n = fundo_listar(l, SEGUNDOPLANO_MAX);
	assert(!achar(l, n, "backup.service")->ligado);
	puts("ligar e rodar ok");
	assert(!fundo_ligar("nao-existe.service", true, erro, sizeof(erro)) && strstr(erro, "does not exist"));
	assert(!fundo_ligar("../x.service", true, erro, sizeof(erro)) && !fundo_ligar("-h", true, erro, sizeof(erro)) && !fundo_ligar("a b.service", true, erro, sizeof(erro)));
	puts("erros ok");
	puts("segundoplano ok");
	return 0;
}
