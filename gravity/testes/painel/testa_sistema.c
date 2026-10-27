#include "sistema.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
	struct sistema_info s;
	sistema_ler(&s);
	assert(!strcmp(s.cpu, "Intel Core i7-9750H @ 2.60GHz"));
	assert(s.nucleos == 2 && s.memoria_total == 16384000LL * 1024 && s.memoria_livre == 8000000LL * 1024);
	assert(!strcmp(s.modelo, "LENOVO ThinkPad T14") && !strcmp(s.distro, "Arch Linux") && !strcmp(s.lumna, "1.2.3"));
	assert(!strcmp(s.gpu, "AMD (amdgpu)") && s.ligado_ha_s == 3600 && s.disco_total > 0);
	char resumo[1024];
	sistema_resumo(&s, resumo, sizeof(resumo));
	assert(strstr(resumo, "LUMNA OS 1.2.3") && strstr(resumo, "ThinkPad"));
	struct sistema_atualizacao a[16];
	int n = sistema_atualizacoes(a, 16);
	assert(n == 2 && !strcmp(a[0].nome, "firefox") && !strcmp(a[0].antiga, "130.0-1") && !strcmp(a[0].nova, "131.0-1") && !strcmp(a[1].nome, "linux"));
	struct sistema_evento h[16];
	n = sistema_historico(h, 16);
	assert(n == 3 && !strcmp(h[0].acao, "removido") && !strcmp(h[0].nome, "velho") && !strcmp(h[1].acao, "atualizado") && !strcmp(h[1].de, "1.0-1") && !strcmp(h[1].para, "1.1-1"));
	assert(!strcmp(h[2].acao, "instalado") && !strcmp(h[2].para, "2.0-1") && h[2].quando > 1700000000);
	puts("sistema ok");
	return 0;
}
