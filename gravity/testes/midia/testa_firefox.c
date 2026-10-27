#define _DEFAULT_SOURCE
#include "firefox.h"
#include "midia.h"
#include <assert.h>
#include <glib.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static struct ff_estado e;
static bool espera(bool (*cond)(void), int ms) {
	for (int t = 0; t < ms; t += 50) {
		ff_ler(&e);
		if (cond()) return true;
		usleep(50000);
	}
	ff_ler(&e);
	return cond();
}
static const struct ff_aba *aba(const char *s) { for (int i = 0; i < e.n; i++) if (!strcmp(e.abas[i].servico, s)) return &e.abas[i]; return NULL; }
static bool netflix_carregado(void) { const struct ff_aba *a = aba("netflix"); return a && !a->carregando && a->url[0]; }
static bool tem_dois(void) { return e.n == 2 && !e.abas[0].carregando && !e.abas[1].carregando; }
static bool youtube_ativo(void) { return !strcmp(e.ativo, "youtube"); }
static bool netflix_ativo(void) { return !strcmp(e.ativo, "netflix"); }
static bool netflix_no_login(void) { const struct ff_aba *a = aba("netflix"); return a && !a->carregando && strstr(a->url, "/login"); }
static bool tem_um(void) { return e.n == 1; }
int main(void) {
	int fd = ff_iniciar();
	assert(fd >= 0);
	ff_ler(&e);
	assert(!e.lancado && !e.ligado);
	ff_ativar("netflix");  /* sem Firefox: nada acontece */
	ff_abrir("netflix", "https://www.netflix.com/browse");
	assert(espera(netflix_carregado, 15000));
	printf("netflix carregou: lancado=%d ligado=%d sem_extensao=%d url=%s ativo=%s\n", e.lancado, e.ligado, e.sem_extensao, aba("netflix")->url, e.ativo);
	assert(e.lancado && e.ligado && !e.sem_extensao && !strcmp(e.ativo, "netflix"));
	/* o Firefox abre sem o controle remoto do WebDriver, e a extensão está no perfil */
	char args[1024], ponteiro[1024];
	snprintf(args, sizeof(args), "%s/args.txt", getenv("HOME"));
	char *conteudo = NULL;
	assert(g_file_get_contents(args, &conteudo, NULL, NULL));
	assert(strstr(conteudo, "--kiosk") && strstr(conteudo, "--name lumna-midia-web") && !strstr(conteudo, "remote-debugging") && !strstr(conteudo, "marionette"));
	g_free(conteudo);
	snprintf(ponteiro, sizeof(ponteiro), "%s/.local/share/lumna/midia/perfil/extensions/lumna-midia@lumna.os", getenv("HOME"));
	assert(g_file_get_contents(ponteiro, &conteudo, NULL, NULL) && g_str_has_suffix(conteudo, "extensao-firefox"));
	g_free(conteudo);
	puts("perfil sem WebDriver e com a extensão ok");
	/* quem não é a extensão não entra */
	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "python3 %s/origem_errada.py", getenv("LUMNA_TESTES"));
	assert(system(cmd) == 0);
	ff_ler(&e);
	assert(!aba("intruso") && e.n == 1);
	puts("origem errada barrada ok");
	ff_abrir("youtube", "https://www.youtube.com/tv");
	assert(espera(tem_dois, 5000) && espera(youtube_ativo, 2000));
	ff_abrir("netflix", "https://naoimporta.com"); /* já tem aba: só ativa, não recarrega */
	assert(espera(netflix_ativo, 2000));
	assert(!strcmp(aba("netflix")->url, "https://www.netflix.com/browse"));
	/* a página de entrada da conta: a aba que já existe vai até lá */
	ff_ir("netflix", "https://www.netflix.com/login");
	assert(espera(netflix_no_login, 5000));
	puts("ir (página de entrada) ok");
	ff_salvar("netflix", true);
	usleep(800000);
	struct midia_recente r[MIDIA_MAX_RECENTES];
	int n = midia_recentes_ler(r, MIDIA_MAX_RECENTES);
	assert(n == 1 && !strcmp(r[0].servico, "netflix") && !strcmp(r[0].url, "https://www.netflix.com/login"));
	printf("recente: %s | %s | %s\n", r[0].servico, r[0].titulo, r[0].miniatura);
	assert(strstr(r[0].titulo, "Título de") && r[0].miniatura[0] && access(r[0].miniatura, R_OK) == 0);
	ff_fechar("youtube");
	assert(espera(tem_um, 3000));
	puts("firefox ok");
	ff_encerrar();
	usleep(400000);
	ff_ler(&e);
	assert(!e.lancado && !e.ligado && e.n == 0);
	puts("encerrou ok");
	return 0;
}
