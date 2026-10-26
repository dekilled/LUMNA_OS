#include "midia.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
int main(void) {
	struct midia_servico s[MIDIA_MAX_SERVICOS];
	int n = midia_servicos_ler(s, MIDIA_MAX_SERVICOS);
	assert(n == 6 && s[0].ativo && !strcmp(s[1].id, "netflix"));
	char id[32];
	assert(!midia_servico_adicionar("Ruim", "ftp://x", id, sizeof(id)));
	assert(midia_servico_adicionar("Meu Site Legal!", "https://exemplo.com/tv", id, sizeof(id)));
	assert(!strcmp(id, "meu-site-legal"));
	assert(midia_servico_adicionar("Meu Site Legal", "https://outro.com", id, sizeof(id)) && !strcmp(id, "meu-site-legal-2"));
	assert(midia_servico_ativar("netflix", false));
	n = midia_servicos_ler(s, MIDIA_MAX_SERVICOS);
	assert(n == 8 && !s[1].ativo && s[0].ativo && !strcmp(s[6].nome, "Meu Site Legal!") && s[6].ativo);
	assert(!midia_servico_remover("youtube"));
	assert(midia_servico_remover("meu-site-legal"));
	n = midia_servicos_ler(s, MIDIA_MAX_SERVICOS);
	assert(n == 7 && !strcmp(s[6].id, "meu-site-legal-2"));
	struct midia_recente r = { .servico = "youtube", .url = "https://www.youtube.com/watch?v=abc&t=84s", .titulo = "Paisagens \"do\" mundo" };
	midia_recente_gravar(&r);
	snprintf(r.servico, sizeof(r.servico), "netflix"); snprintf(r.url, sizeof(r.url), "https://www.netflix.com/watch/1"); snprintf(r.titulo, sizeof(r.titulo), "Minha lista");
	midia_recente_gravar(&r);
	snprintf(r.servico, sizeof(r.servico), "youtube"); snprintf(r.url, sizeof(r.url), "https://www.youtube.com/watch?v=zzz");
	midia_recente_gravar(&r);
	struct midia_recente rs[MIDIA_MAX_RECENTES];
	n = midia_recentes_ler(rs, MIDIA_MAX_RECENTES);
	assert(n == 2 && !strcmp(rs[0].servico, "youtube") && strstr(rs[0].url, "zzz") && !strcmp(rs[1].servico, "netflix"));
	midia_recente_apagar("netflix");
	assert(midia_recentes_ler(rs, MIDIA_MAX_RECENTES) == 1);
	puts("midia ok");
	return 0;
}
