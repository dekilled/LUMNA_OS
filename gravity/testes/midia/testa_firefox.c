#define _DEFAULT_SOURCE
#include "firefox.h"
#include "midia.h"
#include <glib.h>
#include <assert.h>
#include <poll.h>
#include <stdio.h>
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
static bool tem_netflix_carregado(void) { for (int i = 0; i < e.n; i++) if (!strcmp(e.abas[i].servico, "netflix") && !e.abas[i].carregando && e.abas[i].url[0]) return true; return false; }
static bool tem_dois(void) { return e.n == 2 && !e.abas[0].carregando && !e.abas[1].carregando; }
static bool youtube_ativo(void) { return !strcmp(e.ativo, "youtube"); }
static bool netflix_ativo(void) { return !strcmp(e.ativo, "netflix"); }
static bool em_login(void) { return e.login && !e.ligado && e.lancado; }
static bool ligado_sem_login(void) { return e.ligado && !e.login && !strcmp(e.ativo, "youtube"); }
static bool nao_lancado(void) { return !e.lancado && !e.login; }
static bool tem_um(void) { return e.n == 1; }
int main(void) {
	int fd = ff_iniciar();
	assert(fd >= 0);
	ff_ler(&e);
	assert(!e.lancado && !e.ligado);
	ff_ativar("netflix");  /* sem Firefox: nada acontece */
	ff_abrir("netflix", "https://www.netflix.com/browse");
	assert(espera(tem_netflix_carregado, 15000));
	printf("netflix carregou: lancado=%d ligado=%d url=%s ativo=%s\n", e.lancado, e.ligado, e.abas[0].url, e.ativo);
	assert(e.lancado && e.ligado && !strcmp(e.ativo, "netflix"));
	ff_abrir("youtube", "https://www.youtube.com/tv");
	assert(espera(tem_dois, 5000) && espera(youtube_ativo, 2000));
	ff_abrir("netflix", "https://naoimporta.com"); /* já tem aba: só ativa, não recarrega */
	assert(espera(netflix_ativo, 2000));
	for (int i = 0; i < e.n; i++) if (!strcmp(e.abas[i].servico, "netflix")) assert(!strcmp(e.abas[i].url, "https://www.netflix.com/browse"));
	ff_salvar("netflix", true);
	usleep(800000);
	struct midia_recente r[MIDIA_MAX_RECENTES];
	int n = midia_recentes_ler(r, MIDIA_MAX_RECENTES);
	assert(n == 1 && !strcmp(r[0].servico, "netflix") && !strcmp(r[0].url, "https://www.netflix.com/browse"));
	printf("recente: %s | %s | %s\n", r[0].servico, r[0].titulo, r[0].miniatura);
	assert(strstr(r[0].titulo, "Título de") && r[0].miniatura[0] && access(r[0].miniatura, R_OK) == 0);
	ff_fechar("youtube");
	assert(espera(tem_um, 3000));
	puts("firefox ok");
	/* entrar numa conta: um Firefox comum, sem o controle remoto */
	char args[1024];
	snprintf(args, sizeof(args), "%s/args.txt", getenv("HOME"));
	setenv("LUMNA_FF_ARGS", args, 1);
	ff_entrar("https://accounts.exemplo.com/entrar");
	assert(espera(em_login, 15000));
	usleep(300000);
	char *conteudo = NULL;
	assert(g_file_get_contents(args, &conteudo, NULL, NULL));
	assert(strstr(conteudo, "accounts.exemplo.com/entrar") && !strstr(conteudo, "remote-debugging-port") && strstr(conteudo, "--kiosk"));
	g_free(conteudo);
	puts("login ok (sem controle remoto)");
	/* terminar o login sem abrir nada */
	ff_sair_login();
	assert(espera(nao_lancado, 8000));
	puts("sair do login ok");
	ff_entrar("https://accounts.exemplo.com/entrar");
	assert(espera(em_login, 15000));
	/* abrir um serviço fecha o de login e volta ao controlado */
	ff_abrir("youtube", "https://www.youtube.com/tv");
	assert(espera(ligado_sem_login, 20000));
	assert(g_file_get_contents(args, &conteudo, NULL, NULL) && strstr(conteudo, "remote-debugging-port"));
	g_free(conteudo);
	puts("volta ao controlado ok");
	ff_encerrar();
	usleep(300000);
	ff_ler(&e);
	assert(!e.lancado && !e.ligado && e.n == 0);
	puts("encerrou ok");
	return 0;
}
