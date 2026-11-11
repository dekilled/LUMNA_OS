/* Os serviços do pré-setup (servicos/setup.c) num HOME de mentira, com timedatectl, rclone, pkill e lumna-emma de mentira no PATH. */
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emma_ativa.h"
#include "json.h"
#include "modos.h"
#include "setup.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

static char *le(const char *p) {
	char *t = NULL;
	return g_file_get_contents(p, &t, NULL, NULL) ? t : g_strdup("");
}

int main(void) {
	const char *casa = g_getenv("HOME");
	char erro[300];
	struct setup_estado e;

	/* o estado e o que o login faz */
	setup_ler(&e);
	CONFERE(!e.concluido && !e.nao_mostrar && e.aberturas == 0, "sem arquivo: nada feito");
	CONFERE(setup_no_login(&e) == 1, "primeira vez: abre");
	e.aberturas = 1;
	CONFERE(setup_no_login(&e) == 2, "já abriu e não terminou: só lembra");
	e.nao_mostrar = true;
	CONFERE(setup_no_login(&e) == 0, "não mostrar mais: nada");
	e.nao_mostrar = false;
	e.concluido = true;
	CONFERE(setup_no_login(&e) == 0, "concluído: nada");
	e.concluido = false;
	g_strlcpy(e.perfil, "gaming", sizeof(e.perfil));
	CONFERE(setup_gravar(&e), "grava o estado");
	struct setup_estado l;
	setup_ler(&l);
	CONFERE(l.aberturas == 1 && !strcmp(l.perfil, "gaming") && !l.concluido && !l.nao_mostrar, "o estado volta do arquivo");

	/* o perfil: modo inicial + sugestões, e não instala nada */
	CONFERE(setup_perfil_de_id("midia") == PERFIL_MIDIA && setup_perfil_de_id("xyz") == -1, "perfis por id");
	CONFERE(setup_perfil_aplicar(PERFIL_GAMING, erro, sizeof(erro)), "aplica o perfil gaming");
	struct modos_cfg mc;
	modos_cfg_ler(&mc);
	CONFERE(mc.ao_ligar == MODO_GAMING, "gaming começa no modo Gaming");
	CONFERE(setup_perfil_aplicar(PERFIL_MIDIA, erro, sizeof(erro)), "aplica o perfil mídia");
	modos_cfg_ler(&mc);
	CONFERE(mc.ao_ligar == MODO_MIDIA, "mídia começa no modo Mídia");
	CONFERE(setup_perfil_aplicar(PERFIL_DEV, erro, sizeof(erro)), "aplica o perfil dev");
	modos_cfg_ler(&mc);
	CONFERE(mc.ao_ligar == MODO_DESKTOP, "dev começa no Desktop");
	char *sug = setup_sugestoes_ler();
	CONFERE(strstr(sug, "git") != NULL, "as sugestões do perfil ficam gravadas");
	g_free(sug);
	CONFERE(!setup_perfil_aplicar(99, erro, sizeof(erro)), "perfil inexistente é recusado");
	setup_ler(&l);
	CONFERE(!strcmp(l.perfil, "dev"), "o perfil escolhido fica no setup.ini");

	/* hora, teclado, fuso */
	CONFERE(setup_hora_formato(false), "grava 12 horas");
	char *h = g_strdup_printf("%s/.config/lumna/hora.ini", casa), *t = le(h);
	CONFERE(strstr(t, "formato = 12") != NULL, "hora.ini com 12");
	g_free(t);
	g_free(h);
	CONFERE(setup_teclado("us", "intl", erro, sizeof(erro)), "teclado us intl");
	char *en = g_strdup_printf("%s/.config/lumna/entrada.ini", casa);
	t = le(en);
	CONFERE(strstr(t, "layout=us") != NULL && strstr(t, "variant=intl") != NULL, "entrada.ini com o layout");
	g_free(t);
	g_free(en);
	CONFERE(!setup_teclado("us; rm -rf", "", erro, sizeof(erro)), "layout com lixo é recusado");
	CONFERE(setup_fuso("America/Sao_Paulo", erro, sizeof(erro)), "fuso válido chega ao timedatectl");
	char *lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "timedatectl set-timezone America/Sao_Paulo") != NULL, "o timedatectl foi chamado com o fuso");
	g_free(lg);
	CONFERE(!setup_fuso("../../etc/passwd", erro, sizeof(erro)), "fuso com .. é recusado");
	CONFERE(!setup_fuso("-h", erro, sizeof(erro)), "fuso começando com - é recusado");
	CONFERE(!setup_fuso("a b", erro, sizeof(erro)), "fuso com espaço é recusado");

	/* e-mail */
	CONFERE(setup_email_valido("eu@exemplo.com") && setup_email_valido("a.b+c@sub.exemplo.com.br"), "e-mails bons");
	CONFERE(!setup_email_valido("") && !setup_email_valido("sem-arroba") && !setup_email_valido("a@b") && !setup_email_valido("a@@b.com") && !setup_email_valido("a b@c.com")
		&& !setup_email_valido("@c.com") && !setup_email_valido("a@.com") && !setup_email_valido("a@c.com."), "e-mails ruins");
	CONFERE(!setup_email_guardar("ruim", "", 0, "", 0, erro, sizeof(erro)) && *erro, "e-mail ruim: erro com texto");
	CONFERE(setup_email_guardar("eu@exemplo.com", "imap.exemplo.com", 993, "smtp.exemplo.com", 587, erro, sizeof(erro)), "guarda o e-mail com servidor manual");
	char end[200];
	setup_email_ler(end, sizeof(end));
	CONFERE(!strcmp(end, "eu@exemplo.com"), "o e-mail volta do arquivo");

	/* nuvem (rclone de mentira) */
	CONFERE(setup_rclone_existe(), "achou o rclone de mentira");
	CONFERE(!setup_nuvem_conectar(7, "~/Nuvem", erro, sizeof(erro)), "provedor inexistente é recusado");
	CONFERE(setup_nuvem_conectar(0, "~/Nuvem", erro, sizeof(erro)), "conecta o Google Drive");
	lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "rclone config create lumna-drive drive") != NULL, "rclone config create chamado");
	g_free(lg);
	char *pasta = g_strdup_printf("%s/Nuvem", casa);
	CONFERE(g_file_test(pasta, G_FILE_TEST_IS_DIR), "a pasta ~/Nuvem foi criada");
	g_free(pasta);
	CONFERE(setup_nuvem_aplicar(0, "~/Nuvem", true, erro, sizeof(erro)), "aplica a nuvem em segundo plano");
	char p2[200];
	bool bg = false;
	CONFERE(setup_nuvem_ler(p2, sizeof(p2), &bg) == 0 && bg && !strcmp(p2, "~/Nuvem"), "a nuvem volta do arquivo");
	char *auto_dir = g_strdup_printf("%s/.config/autostart", casa);
	GDir *d = g_dir_open(auto_dir, 0, NULL);
	const char *nome;
	bool achou = false;
	while (d && (nome = g_dir_read_name(d))) {
		char *c = g_build_filename(auto_dir, nome, NULL), *tx = le(c);
		if (strstr(tx, "rclone mount lumna-drive:")) {
			achou = true;
		}
		g_free(tx);
		g_free(c);
	}
	if (d) {
		g_dir_close(d);
	}
	CONFERE(achou, "o rclone mount entrou no autostart");
	CONFERE(setup_nuvem_aplicar(0, "~/Nuvem", false, erro, sizeof(erro)), "desliga a nuvem em segundo plano");
	d = g_dir_open(auto_dir, 0, NULL);
	achou = false;
	while (d && (nome = g_dir_read_name(d))) {
		char *c = g_build_filename(auto_dir, nome, NULL), *tx = le(c);
		if (strstr(tx, "rclone mount")) {
			achou = true;
		}
		g_free(tx);
		g_free(c);
	}
	if (d) {
		g_dir_close(d);
	}
	CONFERE(!achou, "desligar tira o rclone mount do autostart");
	g_free(auto_dir);
	char *exp = setup_pasta_expandir("~/Nuvem");
	CONFERE(g_str_has_suffix(exp, "/Nuvem") && exp[0] == '/', "~ vira o caminho de verdade");
	g_free(exp);

	/* a Emma */
	CONFERE(emma_ativa(), "a Emma começa ligada");
	CONFERE(setup_emma_definir(false, erro, sizeof(erro)), "desliga a Emma");
	CONFERE(!emma_ativa(), "desligada no ini");
	lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "pkill -x lumna-emma") != NULL && strstr(lg, "pkill -x lumna-emma-chat") != NULL, "parou o serviço e o chat");
	g_free(lg);
	CONFERE(setup_emma_definir(true, erro, sizeof(erro)), "liga a Emma");
	CONFERE(emma_ativa(), "ligada no ini");
	g_usleep(300000);
	lg = le(g_getenv("FALSO_LOG"));
	CONFERE(strstr(lg, "lumna-emma subiu") != NULL, "subiu o serviço");
	g_free(lg);

	/* o provedor da Emma */
	CONFERE(!setup_emma_provedor("custom", "X", "ftp://x", "k", "m", erro, sizeof(erro)), "URL que não é http é recusada");
	CONFERE(setup_emma_provedor("openai", "OpenAI", "https://api.openai.com/v1", " sk-teste \n", "gpt-5.4-mini, gpt-5.5", erro, sizeof(erro)), "grava o provedor");
	char *pj = g_strdup_printf("%s/.config/lumna/emma/provedores.json", casa);
	t = le(pj);
	struct json *j = json_ler(t);
	const struct json *ps = json_chave(j, "provedores"), *rs = json_chave(j, "rotas");
	CONFERE(ps && ps->nfilhos == 1 && !strcmp(json_chave_texto(json_item(ps, 0), "url"), "https://api.openai.com/v1"), "o provedor está no arquivo");
	CONFERE(!strcmp(json_chave_texto(json_item(ps, 0), "chave"), "sk-teste"), "a chave sai sem espaços nem quebra de linha");
	CONFERE(rs && rs->nfilhos == 4 && !strcmp(json_chave_texto(json_item(rs, 0), "modelo"), "gpt-5.4-mini"), "as quatro rotas usam o primeiro modelo");
	CONFERE(!json_bool(json_chave(json_item(ps, 0), "sinc"), true), "fica pendente de sincronizar (o Painel manda ao servidor)");
	json_liberar(j);
	g_free(t);
	GStatBuf st;
	CONFERE(g_stat(pj, &st) == 0 && (st.st_mode & 0777) == 0600, "o arquivo com a chave é 0600");
	CONFERE(!setup_emma_provedor("openai", "Outro", "https://outro.com/v1", "k2", "m", erro, sizeof(erro)) && strstr(erro, "já tem"), "não passa por cima de um provedor que já existe");
	t = le(pj);
	CONFERE(strstr(t, "outro.com") == NULL, "o provedor existente continua intacto");
	g_free(t);
	g_free(pj);

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("setup (serviços): ok\n");
	return 0;
}
