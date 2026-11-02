/* A política de arquivos e comandos da Emma (servicos/emmaq.c), num HOME de mentira. */
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "emmaq.h"

static int falhas;
static void confere(bool cond, const char *msg) {
	if (!cond) {
		falhas++;
		printf("FALHOU: %s\n", msg);
	}
}

static enum emmaq_veredito v(enum emmaq_op op, const char *caminho) {
	struct emmaq_juizo j = emmaq_julgar(op, caminho);
	enum emmaq_veredito r = j.veredito;
	emmaq_juizo_limpar(&j);
	return r;
}

static void escrever(const char *caminho, const char *texto) {
	char *pai = g_path_get_dirname(caminho);
	g_mkdir_with_parents(pai, 0755);
	g_free(pai);
	g_file_set_contents(caminho, texto, -1, NULL);
}

int main(void) {
	char modelo[] = "/tmp/emma-politica-XXXXXX";
	char *casa = mkdtemp(modelo);
	setenv("HOME", casa, 1);
	char *p;

	/* ---- a política ---- */
	confere(v(EMMAQ_LER, "~/Documentos/nota.txt") == EMMAQ_LIVRE, "ler na pasta pessoal é livre");
	confere(v(EMMAQ_ESCREVER, "Histórias/a.md") == EMMAQ_LIVRE, "escrever na pasta da Emma (relativo) é livre");
	confere(v(EMMAQ_ESCREVER, "~/Emma/Scripts/x.sh") == EMMAQ_LIVRE, "escrever em ~/Emma/Scripts é livre");
	confere(v(EMMAQ_ESCREVER, "~/Documentos/novo.txt") == EMMAQ_LIVRE, "criar um arquivo novo na pasta pessoal é livre");
	p = g_build_filename(casa, "Documentos", "meu.txt", NULL);
	escrever(p, "meu");
	confere(v(EMMAQ_ESCREVER, "~/Documentos/meu.txt") == EMMAQ_APROVAR, "sobrescrever um arquivo seu pede aprovação");
	confere(v(EMMAQ_ANEXAR, "~/Documentos/meu.txt") == EMMAQ_APROVAR, "acrescentar a um arquivo seu pede aprovação");
	confere(v(EMMAQ_LER, "~/Documentos/meu.txt") == EMMAQ_LIVRE, "ler um arquivo seu é livre");
	g_free(p);
	p = g_build_filename(casa, "Emma", "dela.txt", NULL);
	escrever(p, "dela");
	confere(v(EMMAQ_ESCREVER, "~/Emma/dela.txt") == EMMAQ_LIVRE, "sobrescrever um arquivo dela é livre (com cópia)");
	g_free(p);
	confere(v(EMMAQ_ESCREVER, "~/.bashrc") == EMMAQ_APROVAR, "arquivo oculto pede aprovação");
	confere(v(EMMAQ_LER, "~/.config/qualquer/coisa") == EMMAQ_APROVAR, "pasta oculta pede aprovação");
	confere(v(EMMAQ_ESCREVER, "/etc/hosts") == EMMAQ_APROVAR, "escrever fora da pasta pessoal pede aprovação");
	confere(v(EMMAQ_LER, "/etc/os-release") == EMMAQ_APROVAR, "ler fora da pasta pessoal pede aprovação");
	confere(v(EMMAQ_ESCREVER, "~/Emma/../../../../etc/passwd") == EMMAQ_APROVAR, "subir com .. não escapa da política");
	confere(v(EMMAQ_APAGAR, "~/Documentos/novo.txt") == EMMAQ_APROVAR, "apagar sempre pede aprovação");
	confere(v(EMMAQ_EXECUTAR, "~/Emma") == EMMAQ_APROVAR, "comando sempre pede aprovação");
	/* o que nunca se toca */
	confere(v(EMMAQ_LER, "~/.ssh/id_rsa") == EMMAQ_NEGAR, "chave SSH é recusada");
	confere(v(EMMAQ_LER, "~/.gnupg/private-keys-v1.d/a.key") == EMMAQ_NEGAR, "chave GPG é recusada");
	confere(v(EMMAQ_LER, "~/.config/lumna/emma/chave") == EMMAQ_NEGAR, "a chave da Emma é recusada");
	confere(v(EMMAQ_ESCREVER, "~/.config/lumna/emma/servidor.env") == EMMAQ_NEGAR, "os segredos do servidor são recusados");
	confere(v(EMMAQ_LER, "~/.local/share/lumna/emma-servidor/emma.db") == EMMAQ_NEGAR, "o banco do servidor é recusado");
	confere(v(EMMAQ_LER, "/etc/sudoers") == EMMAQ_NEGAR, "sudoers é recusado");
	/* um link para fora não escapa */
	p = g_build_filename(casa, "Documentos", "atalho", NULL);
	symlink("/etc", p);
	confere(v(EMMAQ_ESCREVER, "~/Documentos/atalho/novo.conf") == EMMAQ_APROVAR, "um link para /etc é tratado como /etc");
	g_free(p);
	p = g_build_filename(casa, "Documentos", "chavinha", NULL);
	char *alvo = g_build_filename(casa, ".ssh", "id_rsa", NULL);
	symlink(alvo, p);
	confere(v(EMMAQ_LER, "~/Documentos/chavinha") == EMMAQ_NEGAR, "um link para uma chave SSH é recusado");
	g_free(alvo);
	g_free(p);

	/* criar pasta, mover */
	confere(v(EMMAQ_CRIAR_PASTA, "~/Downloads/Prints do LUMNA") == EMMAQ_LIVRE, "criar uma pasta na pasta pessoal é livre");
	confere(v(EMMAQ_CRIAR_PASTA, "~/.config/qualquer") == EMMAQ_APROVAR, "criar uma pasta oculta pede aprovação");
	confere(v(EMMAQ_CRIAR_PASTA, "/opt/nova") == EMMAQ_APROVAR, "criar uma pasta fora da pasta pessoal pede aprovação");
	confere(v(EMMAQ_CRIAR_PASTA, "~/.ssh/nova") == EMMAQ_NEGAR, "criar dentro de ~/.ssh é recusado");
	confere(v(EMMAQ_MOVER, "~/Downloads/a.png") == EMMAQ_APROVAR, "mover arquivo seu pede aprovação");
	confere(v(EMMAQ_MOVER, "~/Emma/Histórias/x.md") == EMMAQ_LIVRE, "mover dentro da pasta da Emma é livre");
	confere(v(EMMAQ_MOVER, "~/.ssh/id_rsa") == EMMAQ_NEGAR, "mover uma chave é recusado");
	{
		char *erro = NULL, *msg;
		char *pasta = g_build_filename(casa, "Downloads", "Prints", NULL);
		msg = emmaq_criar_pasta(pasta, &erro);
		confere(msg && strstr(msg, "Criei") && g_file_test(pasta, G_FILE_TEST_IS_DIR), "criou a pasta");
		g_free(msg);
		msg = emmaq_criar_pasta(pasta, &erro);
		confere(msg && strstr(msg, "já existia"), "criar de novo diz que já existia");
		g_free(msg);
		char *a = g_build_filename(casa, "Downloads", "a.png", NULL), *b = g_build_filename(casa, "Downloads", "b.png", NULL);
		escrever(a, "aaa");
		escrever(b, "bbb");
		char *dest = emmaq_destino(a, pasta);
		confere(g_str_has_suffix(dest, "/Downloads/Prints/a.png"), "o destino numa pasta que existe vai para dentro dela");
		msg = emmaq_mover(a, dest, false, &erro);
		confere(msg && g_file_test(dest, G_FILE_TEST_EXISTS) && !g_file_test(a, G_FILE_TEST_EXISTS), "moveu o arquivo");
		g_free(msg);
		escrever(a, "novo a");
		msg = emmaq_mover(a, dest, false, &erro);
		confere(!msg && erro && strstr(erro, "já existe"), "não sobrescreve um destino que existe");
		g_free(erro);
		erro = NULL;
		char *copia = g_build_filename(pasta, "b-copia.png", NULL);
		msg = emmaq_mover(b, copia, true, &erro);
		confere(msg && g_file_test(copia, G_FILE_TEST_EXISTS) && g_file_test(b, G_FILE_TEST_EXISTS), "copiou e o original ficou");
		g_free(msg);
		msg = emmaq_mover(pasta, g_build_filename(pasta, "dentro", NULL), false, &erro);
		confere(!msg && erro && strstr(erro, "dentro de si mesma"), "não põe uma pasta dentro dela mesma");
		g_free(erro);
		g_free(copia);
		g_free(dest);
		g_free(a);
		g_free(b);
		g_free(pasta);
	}

	/* a pasta dela é criada antes de precisar */
	emmaq_preparar();
	p = g_build_filename(casa, "Emma", "Histórias", NULL);
	confere(g_file_test(p, G_FILE_TEST_IS_DIR), "emmaq_preparar não criou ~/Emma/Histórias");
	g_free(p);
	p = g_build_filename(casa, "Emma", "Scripts", NULL);
	confere(g_file_test(p, G_FILE_TEST_IS_DIR), "emmaq_preparar não criou ~/Emma/Scripts");
	g_free(p);
	/* os erros dizem a verdade */
	char *e0 = NULL;
	char *l0 = emmaq_listar("/tmp/nao-existe-emma-xyz", &e0);
	confere(!l0 && e0 && strstr(e0, "não existe"), "listar uma pasta que não existe devia dizer que não existe");
	g_free(e0);

	/* ---- as execuções ---- */
	char *erro = NULL, *r;
	struct emmaq_juizo j = emmaq_julgar(EMMAQ_ESCREVER, "Histórias/conto.md");
	r = emmaq_escrever(j.real, "Era uma vez.\n", false, &erro);
	confere(r && g_str_has_prefix(r, "Criei"), "criou o arquivo");
	g_free(r);
	r = emmaq_escrever(j.real, "Fim.\n", true, &erro);
	confere(r && g_str_has_prefix(r, "Acrescentei"), "acrescentou");
	g_free(r);
	r = emmaq_ler(j.real, &erro);
	confere(r && !strcmp(r, "Era uma vez.\nFim.\n"), "leu o que escreveu e acrescentou");
	g_free(r);
	r = emmaq_escrever(j.real, "Outra versão.\n", false, &erro);
	confere(r && g_str_has_prefix(r, "Substituí"), "substituiu");
	g_free(r);
	char *copias = g_build_filename(casa, "Emma", ".copias", NULL);
	GDir *d = g_dir_open(copias, 0, NULL);
	int ncopias = 0;
	while (d && g_dir_read_name(d)) {
		ncopias++;
	}
	if (d) {
		g_dir_close(d);
	}
	confere(ncopias == 2, "guardou uma cópia antes de cada mudança (acrescentar e substituir)");
	g_free(copias);
	emmaq_juizo_limpar(&j);

	j = emmaq_julgar(EMMAQ_LISTAR, "~/Emma");
	r = emmaq_listar(j.real, &erro);
	confere(r && strstr(r, "Histórias/") && strstr(r, "dela.txt"), "listou a pasta");
	g_free(r);
	emmaq_juizo_limpar(&j);

	p = g_build_filename(casa, "binario.bin", NULL);
	g_file_set_contents(p, "ab\0cd", 5, NULL);
	r = emmaq_ler(p, &erro);
	confere(!r && erro, "recusou ler um binário");
	g_free(erro);
	erro = NULL;
	g_free(p);

	bool ok;
	r = emmaq_executar("echo oi; echo erro >&2", NULL, 5, &ok, &erro);
	confere(ok && r && strstr(r, "oi") && strstr(r, "erro") && strstr(r, "código 0"), "executou e juntou a saída");
	g_free(r);
	r = emmaq_executar("exit 3", NULL, 5, &ok, &erro);
	confere(!ok && r && strstr(r, "código 3"), "devolveu o código de saída");
	g_free(r);
	r = emmaq_executar("pwd", NULL, 5, &ok, &erro);
	char *esperado = g_build_filename(casa, "Emma", NULL);
	confere(r && strstr(r, esperado), "o comando roda em ~/Emma");
	g_free(esperado);
	g_free(r);
	r = emmaq_executar("sleep 10", NULL, 1, &ok, &erro);
	confere(!ok && r && strstr(r, "parei o comando"), "parou um comando que passou do prazo");
	g_free(r);

	/* o detalhe para quem aprova inclui o código do script que ela escreveu */
	p = g_build_filename(casa, "Emma", "Scripts", "faz.sh", NULL);
	escrever(p, "#!/bin/sh\necho CODIGO-DO-SCRIPT\n");
	char *cmd = g_strdup_printf("sh %s", p);
	char *det = emmaq_detalhe_comando(cmd, NULL);
	confere(strstr(det, "$ sh ") && strstr(det, "CODIGO-DO-SCRIPT"), "o detalhe do comando mostra o código do script");
	g_free(det);
	g_free(cmd);
	g_free(p);

	printf(falhas ? "politica: %d falha(s)\n" : "politica ok\n", falhas);
	return falhas ? 1 : 0;
}
