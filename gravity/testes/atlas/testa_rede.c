/* Rede e nuvem do Atlas (arquivos_rede.c): o endereço, a lista salva, conectar com o gvfs falso (a senha pela interface) e o rclone falso. */
#include "arquivos_rede.h"

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int falhas;
#define CONFERE(c, ...) do { if (!(c)) { fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); falhas++; } } while (0)

static void esperar_fase(ar_conexao *c, int fase, int ms, ar_conexao_estado *e) {
	for (int i = 0; i < ms / 10; i++) {
		ar_conexao_estado_ler(c, e);
		if (e->fase == fase) {
			return;
		}
		g_usleep(10000);
	}
	ar_conexao_estado_ler(c, e);
}

static bool montar_falso(ar_conexao *c, const char *uri, char *erro, size_t tam) {
	if (strstr(uri, "livre")) {
		return true;
	}
	char usuario[96], senha[256];
	if (!ar_conexao_pedir_senha(c, "Senha do servidor", usuario, senha, sizeof(senha))) {
		snprintf(erro, tam, "Operação cancelada");
		return false;
	}
	if (!strcmp(usuario, "eric") && !strcmp(senha, "certa")) {
		return true;
	}
	snprintf(erro, tam, "Senha incorreta");
	return false;
}

int main(void) {
	char uri[512], nome[96], erro[256];
	int proto;

	/* ---- o endereço ---- */
	CONFERE(ar_rede_normalizar(AR_P_SMB, "servidor/pasta", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)) && !strcmp(uri, "smb://servidor/pasta") && proto == AR_P_SMB && !strcmp(nome, "servidor / pasta"), "smb sem esquema (%s, %s)", uri, nome);
	CONFERE(ar_rede_normalizar(AR_P_SMB, "\\\\nas\\Fotos\\2024", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)) && !strcmp(uri, "smb://nas/Fotos/2024"), "caminho do Windows (%s)", uri);
	CONFERE(ar_rede_normalizar(AR_P_SMB, "sftp://eric@192.168.0.5/home/eric", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)) && proto == AR_P_SFTP && !strcmp(uri, "sftp://eric@192.168.0.5/home/eric"), "um esquema digitado manda sobre o menu");
	CONFERE(ar_rede_normalizar(AR_P_WEBDAV, "https://nuvem.exemplo.com/dav/", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)) && !strcmp(uri, "davs://nuvem.exemplo.com/dav") && proto == AR_P_WEBDAV, "https vira davs, sem a barra do fim (%s)", uri);
	CONFERE(ar_rede_normalizar(AR_P_SFTP, "  meu-servidor  ", &proto, uri, sizeof(uri), NULL, 0, erro, sizeof(erro)) && !strcmp(uri, "sftp://meu-servidor"), "só o nome do servidor, com espaços em volta");
	CONFERE(!ar_rede_normalizar(AR_P_SMB, "file:///etc/passwd", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)) && erro[0], "file:// não passa");
	CONFERE(!ar_rede_normalizar(AR_P_SMB, "javascript://x", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)), "esquema desconhecido não passa");
	CONFERE(!ar_rede_normalizar(AR_P_SMB, "", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)) && !ar_rede_normalizar(AR_P_SMB, "   ", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)), "vazio não passa");
	CONFERE(ar_rede_normalizar(AR_P_SMB, "smb:///so-pasta", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)) && !strcmp(uri, "smb://so-pasta"), "barras sobrando no começo saem (%s)", uri);
	CONFERE(!ar_rede_normalizar(AR_P_SMB, "serv idor/pasta", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)), "espaço no endereço não passa");
	CONFERE(!ar_rede_normalizar(AR_P_SMB, "ser;vidor/pasta", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)), "caractere esquisito no nome do servidor não passa");
	CONFERE(ar_rede_normalizar(AR_P_NFS, "10.0.0.2:2049/dados", &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro)) && !strcmp(uri, "nfs://10.0.0.2:2049/dados"), "porta no endereço (%s)", uri);

	/* ---- a lista salva ---- */
	char *casa = g_dir_make_tmp("atlas-rede-XXXXXX", NULL);
	char *cfg = g_build_filename(casa, "cfg", NULL);
	g_setenv("XDG_CONFIG_HOME", cfg, TRUE);
	g_setenv("HOME", casa, TRUE);
	ar_rede r;
	ar_rede_ler(&r);
	CONFERE(r.n_shares == 0 && r.n_contas == 0, "começa vazia");
	int a = ar_rede_adicionar_share(&r, "NAS da casa", AR_P_SMB, "smb://nas/casa");
	int b = ar_rede_adicionar_share(&r, "Projetos", AR_P_SFTP, "sftp://srv/projetos");
	CONFERE(a == 0 && b == 1 && ar_rede_adicionar_share(&r, "outro nome", AR_P_SMB, "smb://nas/casa") == 0 && r.n_shares == 2, "o mesmo endereço não repete");
	r.shares[0].conectado_antes = true;
	CONFERE(ar_rede_gravar(&r), "gravar");
	ar_rede r2;
	ar_rede_ler(&r2);
	CONFERE(r2.n_shares == 2 && !strcmp(r2.shares[0].nome, "NAS da casa") && r2.shares[0].conectado_antes && !r2.shares[1].conectado_antes && r2.shares[1].proto == AR_P_SFTP, "ler o que gravou");
	char *ini = g_build_filename(cfg, "lumna", "atlas", "rede.ini", NULL);
	g_file_set_contents(ini, "[share000]\nnome=Mau\nproto=0\nuri=file:///etc\n[share001]\nnome=Bom\nproto=0\nuri=smb://x/y\n", -1, NULL);
	ar_rede_ler(&r2);
	CONFERE(r2.n_shares == 1 && !strcmp(r2.shares[0].nome, "Bom"), "um endereço editado à mão que não vale fica de fora");
	ar_rede_remover_share(&r, 0);
	CONFERE(r.n_shares == 1 && !strcmp(r.shares[0].nome, "Projetos"), "remover");
	ar_rede_remover_share(&r, 9);
	CONFERE(r.n_shares == 1, "remover um índice inválido não faz nada");

	/* ---- conectar (o gvfs falso), com a senha pela interface ---- */
	ar_rede_usar_montagem_falsa(montar_falso);
	ar_conexao_estado e;
	ar_conexao *c = ar_share_conectar("smb://servidor/livre", false, NULL, NULL);
	esperar_fase(c, AR_CX_OK, 2000, &e);
	CONFERE(e.fase == AR_CX_OK, "conectar sem senha (fase %d)", e.fase);
	ar_conexao_liberar(c);
	c = ar_share_conectar("smb://servidor/seguro", false, NULL, NULL);
	esperar_fase(c, AR_CX_PEDINDO_SENHA, 2000, &e);
	CONFERE(e.fase == AR_CX_PEDINDO_SENHA && e.mensagem[0], "pede a senha (fase %d)", e.fase);
	ar_conexao_responder(c, true, "eric", "certa", "", false, false);
	esperar_fase(c, AR_CX_OK, 2000, &e);
	CONFERE(e.fase == AR_CX_OK, "senha certa conecta (fase %d, %s)", e.fase, e.mensagem);
	ar_conexao_liberar(c);
	c = ar_share_conectar("smb://servidor/seguro", false, NULL, NULL);
	esperar_fase(c, AR_CX_PEDINDO_SENHA, 2000, &e);
	ar_conexao_responder(c, true, "eric", "errada", "", false, false);
	esperar_fase(c, AR_CX_ERRO, 2000, &e);
	CONFERE(e.fase == AR_CX_ERRO && !strcmp(e.mensagem, "Senha incorreta"), "senha errada dá erro com a razão (%d, %s)", e.fase, e.mensagem);
	ar_conexao_liberar(c);
	c = ar_share_conectar("smb://servidor/seguro", false, NULL, NULL);
	esperar_fase(c, AR_CX_PEDINDO_SENHA, 2000, &e);
	ar_conexao_responder(c, false, "", "", "", false, false);
	esperar_fase(c, AR_CX_CANCELADA, 2000, &e);
	CONFERE(e.fase == AR_CX_CANCELADA, "cancelar o diálogo cancela a conexão (fase %d)", e.fase);
	ar_conexao_liberar(c);
	c = ar_share_conectar("smb://servidor/seguro", true, NULL, NULL);
	esperar_fase(c, AR_CX_ERRO, 2000, &e);
	CONFERE(e.fase == AR_CX_ERRO || e.fase == AR_CX_CANCELADA, "o modo silencioso nunca pede senha (fase %d)", e.fase);
	ar_conexao_liberar(c);
	c = ar_share_conectar("smb://servidor/seguro", false, NULL, NULL);
	esperar_fase(c, AR_CX_PEDINDO_SENHA, 2000, &e);
	ar_conexao_liberar(c); /* soltar uma conexão que espera a pessoa não trava */
	CONFERE(true, "liberar no meio do diálogo");
	ar_rede_usar_montagem_falsa(NULL);

	/* ---- o gvfs de verdade (aqui sem o serviço): tem de acabar com um erro e não pendurar ---- */
	c = ar_share_conectar("smb://127.0.0.1/inexistente", true, NULL, NULL);
	esperar_fase(c, AR_CX_ERRO, 25000, &e);
	CONFERE(e.fase == AR_CX_ERRO || e.fase == AR_CX_CANCELADA || e.fase == AR_CX_OK, "o gvfs sem serviço termina (fase %d: %s)", e.fase, e.mensagem);
	ar_conexao_liberar(c);
	char caminho[512];
	CONFERE(!ar_share_montado("smb://127.0.0.1/inexistente", caminho, sizeof(caminho)), "não está montado");

	/* ---- o rclone falso ---- */
	char *bin = g_build_filename(casa, "bin", NULL);
	g_mkdir_with_parents(bin, 0755);
	char *falso = g_build_filename(bin, "rclone", NULL);
	char *log = g_build_filename(casa, "rclone.log", NULL);
	char *script = g_strdup_printf("#!/bin/sh\n"
		"if [ \"$1\" = obscure ]; then read s; echo \"OBSC-$s\" | sed 's/certa/X/'; exit 0; fi\n"
		"echo \"$@\" >> %s\n"
		"if [ -n \"$FAKE_FALHA\" ]; then echo 'linha antes'; echo 'ERROR : bisync deu errado'; exit 1; fi\n"
		"exit 0\n", log);
	g_file_set_contents(falso, script, -1, NULL);
	chmod(falso, 0755);
	char *path = g_strdup_printf("%s:%s", bin, g_getenv("PATH"));
	g_setenv("PATH", path, TRUE);
	CONFERE(ar_nuvem_disponivel(), "acha o rclone no PATH");
	int ic = ar_nuvem_criar_webdav(&r, "Nextcloud do Eric", "nextcloud", "https://nuvem.exemplo.com/", "eric", "certa", erro, sizeof(erro));
	CONFERE(ic == 0 && r.n_contas == 1 && !strcmp(r.contas[0].tipo, "nextcloud") && g_str_has_prefix(r.contas[0].remote, "atlas-nextcloud"), "criar a conta Nextcloud (%d %s)", ic, erro);
	char *conf = ar_nuvem_conf();
	char *txt = NULL;
	g_file_get_contents(conf, &txt, NULL, NULL);
	CONFERE(txt && strstr(txt, "type=webdav") && strstr(txt, "vendor=nextcloud") && strstr(txt, "url=https://nuvem.exemplo.com/remote.php/dav/files/eric/") && strstr(txt, "user=eric"), "o rclone.conf tem a conta (%s)", txt ? txt : "?");
	CONFERE(txt && strstr(txt, "pass=OBSC-X") && !strstr(txt, "certa"), "a senha vai ofuscada, nunca em claro");
	struct stat st;
	CONFERE(stat(conf, &st) == 0 && (st.st_mode & 0777) == 0600, "o rclone.conf é só da pessoa (%o)", (unsigned)(st.st_mode & 0777));
	g_free(txt);
	CONFERE(ar_nuvem_criar_webdav(&r, "", "webdav", "https://x", "u", "s", erro, sizeof(erro)) < 0 && erro[0], "sem nome, não cria");
	CONFERE(ar_nuvem_criar_webdav(&r, "Ruim", "webdav", "ftp://x", "u", "s", erro, sizeof(erro)) < 0, "endereço que não é http(s) não vale");
	int i2 = ar_nuvem_criar_webdav(&r, "Nextcloud do Eric", "webdav", "https://outro.exemplo.com/dav/", "ana", "s", erro, sizeof(erro));
	CONFERE(i2 == 1 && strcmp(r.contas[0].remote, r.contas[1].remote) != 0, "o mesmo nome de conta não repete o nome no rclone (%s, %s)", r.contas[0].remote, r.contas[1].remote);
	CONFERE(strstr(r.contas[0].pasta_local, "/Nuvem/Nextcloud do Eric") != NULL, "a pasta local padrão (%s)", r.contas[0].pasta_local);

	/* sincronizar: a primeira com --resync, depois sem; a falha traz a última linha */
	ar_sync_estado se;
	ar_sync *s = ar_nuvem_sincronizar(&r.contas[0], NULL, NULL);
	for (int i = 0; i < 300; i++) {
		ar_sync_estado_ler(s, &se);
		if (se.fase != AR_SY_ANDANDO) {
			break;
		}
		g_usleep(10000);
	}
	CONFERE(se.fase == AR_SY_OK, "sincronizar (fase %d, %s)", se.fase, se.mensagem);
	ar_sync_liberar(s);
	r.contas[0].sincronizada = true;
	s = ar_nuvem_sincronizar(&r.contas[0], NULL, NULL);
	for (int i = 0; i < 300; i++) {
		ar_sync_estado_ler(s, &se);
		if (se.fase != AR_SY_ANDANDO) {
			break;
		}
		g_usleep(10000);
	}
	ar_sync_liberar(s);
	char *lg = NULL;
	g_file_get_contents(log, &lg, NULL, NULL);
	char **linhas = g_strsplit(lg ? lg : "", "\n", -1);
	CONFERE(linhas[0] && strstr(linhas[0], "bisync") && strstr(linhas[0], "--resync") && strstr(linhas[0], "--config") && linhas[1] && strstr(linhas[1], "bisync") && !strstr(linhas[1], "--resync"), "a primeira sincronização usa --resync e a segunda não (%s)", lg ? lg : "?");
	CONFERE(lg && !strstr(lg, "certa"), "a senha não aparece em nenhuma linha de comando");
	CONFERE(g_file_test(r.contas[0].pasta_local, G_FILE_TEST_IS_DIR), "a pasta local é criada");
	g_strfreev(linhas);
	g_free(lg);
	g_setenv("FAKE_FALHA", "1", TRUE);
	s = ar_nuvem_sincronizar(&r.contas[0], NULL, NULL);
	for (int i = 0; i < 300; i++) {
		ar_sync_estado_ler(s, &se);
		if (se.fase != AR_SY_ANDANDO) {
			break;
		}
		g_usleep(10000);
	}
	CONFERE(se.fase == AR_SY_ERRO && strstr(se.mensagem, "bisync deu errado"), "a falha traz a última linha da saída (%s)", se.mensagem);
	ar_sync_liberar(s);
	g_unsetenv("FAKE_FALHA");
	s = ar_nuvem_entrar("atlas-drive", "drive", NULL, NULL);
	for (int i = 0; i < 300; i++) {
		ar_sync_estado_ler(s, &se);
		if (se.fase != AR_SY_ANDANDO) {
			break;
		}
		g_usleep(10000);
	}
	CONFERE(se.fase == AR_SY_OK, "entrar numa conta OAuth chama o rclone config create");
	ar_sync_liberar(s);
	g_file_get_contents(log, &lg, NULL, NULL);
	CONFERE(lg && strstr(lg, "config create --config") && strstr(lg, "atlas-drive drive"), "com o nome e o tipo (%s)", lg ? lg : "?");
	g_free(lg);
	CONFERE(ar_nuvem_adicionar_conta(&r, "Drive", "drive", "atlas-drive") >= 0 && r.n_contas == 3, "a conta OAuth entra na lista");

	/* remover */
	char *remote0 = g_strdup(r.contas[0].remote);
	ar_nuvem_remover(&r, 0);
	g_file_get_contents(conf, &txt, NULL, NULL);
	char *grupo0 = g_strdup_printf("[%s]", remote0);
	char *grupo1 = g_strdup_printf("[%s]", r.contas[0].remote); /* a segunda conta subiu para o lugar da primeira */
	CONFERE(r.n_contas == 2 && txt && !strstr(txt, grupo0) && strstr(txt, grupo1), "remover tira do rclone.conf e da lista, sem mexer nas outras");
	g_free(grupo0);
	g_free(grupo1);
	g_free(txt);
	g_free(remote0);
	ar_rede_gravar(&r);
	ar_rede_ler(&r2);
	CONFERE(r2.n_contas == 2 && r2.contas[0].segundo_plano, "as contas ficam gravadas");

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("rede e nuvem: ok\n");
	return 0;
}
