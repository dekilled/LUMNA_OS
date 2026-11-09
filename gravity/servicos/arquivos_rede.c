#define _DEFAULT_SOURCE
#include "arquivos_rede.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static ar_montar_falso montagem_falsa;
const char *const AR_PROTO_NOMES[AR_PROTOS_N] = { "SMB", "SFTP", "FTP", "WebDAV", "NFS" };
const char *const AR_PROTO_DICAS[AR_PROTOS_N] = { "smb://servidor/pasta", "sftp://usuario@servidor/pasta", "ftp://servidor/pasta", "davs://servidor/pasta", "nfs://servidor/exportacao" };
static const char *const ESQUEMAS[AR_PROTOS_N] = { "smb", "sftp", "ftp", "davs", "nfs" };

static void texto_erro(char *erro, size_t tam, const char *msg) {
	if (erro && tam) {
		snprintf(erro, tam, "%s", msg);
	}
}

/* ---- o endereço ---- */

static int proto_do_esquema(const char *e, const char **canonico) {
	static const struct { const char *esquema; int proto; const char *canon; } M[] = {
		{ "smb", AR_P_SMB, "smb" }, { "cifs", AR_P_SMB, "smb" }, { "sftp", AR_P_SFTP, "sftp" }, { "ssh", AR_P_SFTP, "sftp" }, { "ftp", AR_P_FTP, "ftp" },
		{ "davs", AR_P_WEBDAV, "davs" }, { "https", AR_P_WEBDAV, "davs" }, { "webdavs", AR_P_WEBDAV, "davs" },
		{ "dav", AR_P_WEBDAV, "dav" }, { "http", AR_P_WEBDAV, "dav" }, { "webdav", AR_P_WEBDAV, "dav" }, { "nfs", AR_P_NFS, "nfs" },
	};
	for (size_t i = 0; i < sizeof(M) / sizeof(M[0]); i++) {
		if (!g_ascii_strcasecmp(e, M[i].esquema)) {
			*canonico = M[i].canon;
			return M[i].proto;
		}
	}
	return -1;
}

bool ar_rede_normalizar(int proto_escolhido, const char *texto, int *proto, char *uri, size_t tam, char *nome, size_t tam_nome, char *erro, size_t tam_erro) {
	if (!texto) {
		texto_erro(erro, tam_erro, "Digite o endereço do servidor");
		return false;
	}
	char *t = g_strstrip(g_strdup(texto));
	if (!t[0]) {
		g_free(t);
		texto_erro(erro, tam_erro, "Digite o endereço do servidor");
		return false;
	}
	for (char *c = t; *c; c++) {
		if ((unsigned char)*c < 0x20 || *c == 0x7f || *c == ' ' || *c == '\t') {
			g_free(t);
			texto_erro(erro, tam_erro, "O endereço não pode ter espaços nem caracteres de controle");
			return false;
		}
	}
	/* \\servidor\pasta (o jeito do Windows) vira smb://servidor/pasta */
	if (t[0] == '\\' && t[1] == '\\') {
		char *resto = g_strdup(t + 2);
		for (char *c = resto; *c; c++) {
			if (*c == '\\') {
				*c = '/';
			}
		}
		g_free(t);
		t = g_strdup_printf("smb://%s", resto);
		g_free(resto);
	}
	const char *canon = NULL;
	int p = proto_escolhido >= 0 && proto_escolhido < AR_PROTOS_N ? proto_escolhido : AR_P_SMB;
	char *resto;
	char *sep = strstr(t, "://");
	if (sep) {
		char *esq = g_strndup(t, (gsize)(sep - t));
		int pp = proto_do_esquema(esq, &canon);
		if (pp < 0) {
			char *msg = g_strdup_printf("O esquema \"%s\" não é aceito (use smb, sftp, ftp, davs ou nfs)", esq);
			texto_erro(erro, tam_erro, msg);
			g_free(msg);
			g_free(esq);
			g_free(t);
			return false;
		}
		g_free(esq);
		p = pp;
		resto = g_strdup(sep + 3);
	} else {
		canon = ESQUEMAS[p];
		resto = g_strdup(t);
	}
	for (char *c = resto; *c; c++) {
		if (*c == '\\') {
			*c = '/';
		}
	}
	while (*resto == '/') { /* smb:///x não é um servidor */
		memmove(resto, resto + 1, strlen(resto));
	}
	/* o servidor: depois do usuário@ e antes do primeiro / */
	char *barra = strchr(resto, '/');
	char *autor = barra ? g_strndup(resto, (gsize)(barra - resto)) : g_strdup(resto);
	char *host = strrchr(autor, '@') ? strrchr(autor, '@') + 1 : autor;
	char *dp = strrchr(host, ':');
	if (dp && host[0] != '[') {
		*dp = '\0';
	}
	bool host_ok = host[0] != '\0';
	for (const char *c = host; *c && host_ok; c++) {
		host_ok = isalnum((unsigned char)*c) || *c == '.' || *c == '-' || *c == '_' || *c == '[' || *c == ']' || *c == ':';
	}
	if (!host_ok) {
		texto_erro(erro, tam_erro, "O nome do servidor está vazio ou tem caracteres que não valem");
		g_free(autor);
		g_free(resto);
		g_free(t);
		return false;
	}
	char *final = g_strdup_printf("%s://%s", canon, resto);
	size_t n = strlen(final);
	while (n > 1 && final[n - 1] == '/' && strlen(final) > strlen(canon) + 4) {
		final[--n] = '\0';
	}
	if (n >= tam) {
		texto_erro(erro, tam_erro, "O endereço é comprido demais");
		g_free(final);
		g_free(autor);
		g_free(resto);
		g_free(t);
		return false;
	}
	snprintf(uri, tam, "%s", final);
	if (proto) {
		*proto = p;
	}
	if (nome && tam_nome) {
		char *rot = g_strdup(barra && barra[1] ? barra + 1 : "");
		char *fim = rot + strlen(rot);
		while (fim > rot && fim[-1] == '/') {
			*--fim = '\0';
		}
		if (rot[0]) {
			snprintf(nome, tam_nome, "%s / %s", host, rot);
		} else {
			snprintf(nome, tam_nome, "%s", host);
		}
		g_free(rot);
	}
	g_free(final);
	g_free(autor);
	g_free(resto);
	g_free(t);
	return true;
}

/* ---- a lista salva ---- */

static char *arq_rede(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "atlas", "rede.ini", NULL);
}

void ar_rede_ler(ar_rede *r) {
	memset(r, 0, sizeof(*r));
	GKeyFile *kf = g_key_file_new();
	char *a = arq_rede();
	g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL);
	g_free(a);
	gsize ng = 0;
	char **grupos = g_key_file_get_groups(kf, &ng);
	for (gsize i = 0; i < ng; i++) {
		const char *g = grupos[i];
		if (g_str_has_prefix(g, "share") && r->n_shares < AR_SHARES_MAX) {
			char *uri = g_key_file_get_string(kf, g, "uri", NULL);
			char *nome = g_key_file_get_string(kf, g, "nome", NULL);
			int proto = g_key_file_get_integer(kf, g, "proto", NULL);
			char nome2[96], uri2[512];
			int proto2 = AR_P_SMB;
			/* o arquivo é editável à mão: o que não passa pela normalização fica de fora */
			if (uri && ar_rede_normalizar(proto, uri, &proto2, uri2, sizeof(uri2), nome2, sizeof(nome2), NULL, 0)) {
				ar_share *s = &r->shares[r->n_shares++];
				snprintf(s->nome, sizeof(s->nome), "%s", nome && nome[0] ? nome : nome2);
				s->proto = proto2;
				snprintf(s->uri, sizeof(s->uri), "%s", uri2);
				s->conectado_antes = g_key_file_get_boolean(kf, g, "conectado", NULL);
			}
			g_free(uri);
			g_free(nome);
		} else if (g_str_has_prefix(g, "conta") && r->n_contas < AR_CONTAS_MAX) {
			char *remote = g_key_file_get_string(kf, g, "remote", NULL);
			if (remote && remote[0]) {
				ar_conta *c = &r->contas[r->n_contas++];
				char *v;
				v = g_key_file_get_string(kf, g, "nome", NULL);
				snprintf(c->nome, sizeof(c->nome), "%s", v ? v : remote);
				g_free(v);
				v = g_key_file_get_string(kf, g, "tipo", NULL);
				snprintf(c->tipo, sizeof(c->tipo), "%s", v ? v : "webdav");
				g_free(v);
				snprintf(c->remote, sizeof(c->remote), "%s", remote);
				v = g_key_file_get_string(kf, g, "pasta", NULL);
				snprintf(c->pasta_local, sizeof(c->pasta_local), "%s", v ? v : "");
				g_free(v);
				c->segundo_plano = g_key_file_get_boolean(kf, g, "segundo_plano", NULL);
				c->sincronizada = g_key_file_get_boolean(kf, g, "sincronizada", NULL);
				c->ultima_sync = (time_t)g_key_file_get_int64(kf, g, "ultima_sync", NULL);
				v = g_key_file_get_string(kf, g, "erro", NULL);
				snprintf(c->ultimo_erro, sizeof(c->ultimo_erro), "%s", v ? v : "");
				g_free(v);
			}
			g_free(remote);
		}
	}
	g_strfreev(grupos);
	g_key_file_free(kf);
}

bool ar_rede_gravar(const ar_rede *r) {
	GKeyFile *kf = g_key_file_new();
	for (int i = 0; i < r->n_shares; i++) {
		char g[24];
		snprintf(g, sizeof(g), "share%03d", i);
		g_key_file_set_string(kf, g, "nome", r->shares[i].nome);
		g_key_file_set_integer(kf, g, "proto", r->shares[i].proto);
		g_key_file_set_string(kf, g, "uri", r->shares[i].uri);
		g_key_file_set_boolean(kf, g, "conectado", r->shares[i].conectado_antes);
	}
	for (int i = 0; i < r->n_contas; i++) {
		char g[24];
		snprintf(g, sizeof(g), "conta%03d", i);
		const ar_conta *c = &r->contas[i];
		g_key_file_set_string(kf, g, "nome", c->nome);
		g_key_file_set_string(kf, g, "tipo", c->tipo);
		g_key_file_set_string(kf, g, "remote", c->remote);
		g_key_file_set_string(kf, g, "pasta", c->pasta_local);
		g_key_file_set_boolean(kf, g, "segundo_plano", c->segundo_plano);
		g_key_file_set_boolean(kf, g, "sincronizada", c->sincronizada);
		g_key_file_set_int64(kf, g, "ultima_sync", (gint64)c->ultima_sync);
		g_key_file_set_string(kf, g, "erro", c->ultimo_erro);
	}
	char *a = arq_rede();
	char *pasta = g_path_get_dirname(a);
	g_mkdir_with_parents(pasta, 0700);
	bool ok = g_key_file_save_to_file(kf, a, NULL);
	g_free(pasta);
	g_free(a);
	g_key_file_free(kf);
	return ok;
}

int ar_rede_adicionar_share(ar_rede *r, const char *nome, int proto, const char *uri) {
	for (int i = 0; i < r->n_shares; i++) {
		if (!strcmp(r->shares[i].uri, uri)) {
			return i;
		}
	}
	if (r->n_shares >= AR_SHARES_MAX) {
		return -1;
	}
	ar_share *s = &r->shares[r->n_shares];
	memset(s, 0, sizeof(*s));
	snprintf(s->nome, sizeof(s->nome), "%s", nome && nome[0] ? nome : uri);
	s->proto = proto;
	snprintf(s->uri, sizeof(s->uri), "%s", uri);
	return r->n_shares++;
}

void ar_rede_remover_share(ar_rede *r, int i) {
	if (i < 0 || i >= r->n_shares) {
		return;
	}
	memmove(&r->shares[i], &r->shares[i + 1], sizeof(ar_share) * (size_t)(r->n_shares - i - 1));
	r->n_shares--;
}

/* ---- montado? ---- */

/* a montagem falsa (testes) lembra o que "montou", para o resto do Atlas ver o compartilhamento como conectado */
static GHashTable *falsos_montados;
static GMutex falsos_trinco;

static void falso_marcar(const char *uri, bool montado) {
	g_mutex_lock(&falsos_trinco);
	if (!falsos_montados) {
		falsos_montados = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	}
	if (montado) {
		g_hash_table_add(falsos_montados, g_strdup(uri));
	} else {
		g_hash_table_remove(falsos_montados, uri);
	}
	g_mutex_unlock(&falsos_trinco);
}

bool ar_share_montado(const char *uri, char *caminho, size_t tam) {
	if (montagem_falsa) {
		g_mutex_lock(&falsos_trinco);
		bool m = falsos_montados && g_hash_table_contains(falsos_montados, uri);
		g_mutex_unlock(&falsos_trinco);
		if (m && caminho && tam) {
			const char *d = g_getenv("LUMNA_ATLAS_FALSO_DIR");
			snprintf(caminho, tam, "%s", d && d[0] ? d : g_get_tmp_dir());
		}
		return m;
	}
	GFile *f = g_file_new_for_uri(uri);
	GMount *m = g_file_find_enclosing_mount(f, NULL, NULL);
	bool montado = m != NULL;
	if (montado && caminho && tam) {
		char *p = g_file_get_path(f);
		snprintf(caminho, tam, "%s", p ? p : "");
		g_free(p);
		if (!caminho[0]) { /* sem gvfs-fuse: o ponto de montagem não tem caminho local */
			GFile *raiz = g_mount_get_root(m);
			char *pr = g_file_get_path(raiz);
			snprintf(caminho, tam, "%s", pr ? pr : "");
			g_free(pr);
			g_object_unref(raiz);
		}
	}
	if (m) {
		g_object_unref(m);
	}
	g_object_unref(f);
	return montado;
}

/* ---- conectar e desconectar ---- */

struct ar_conexao {
	GMutex trinco;
	GCond cond;
	ar_conexao_estado est;
	bool respondido;
	bool resp_ok;
	int resp_opcao;
	char resp_usuario[96], resp_senha[256], resp_dominio[96];
	bool resp_lembrar, resp_anonimo;
	char *uri;
	bool silencioso, desconectar;
	void (*acordar)(void *);
	void *dados;
	GThread *thread;
	bool acabou;
};

void ar_rede_usar_montagem_falsa(ar_montar_falso f) {
	montagem_falsa = f;
}

static void cx_acordar(ar_conexao *c) {
	if (c->acordar) {
		c->acordar(c->dados);
	}
}

/* a thread pede à interface e espera a resposta */
static bool cx_esperar_resposta(ar_conexao *c) {
	g_mutex_lock(&c->trinco);
	c->respondido = false;
	while (!c->respondido) {
		g_cond_wait(&c->cond, &c->trinco);
	}
	bool ok = c->resp_ok;
	g_mutex_unlock(&c->trinco);
	return ok;
}

bool ar_conexao_pedir_senha(ar_conexao *c, const char *mensagem, char *usuario_saida, char *senha_saida, size_t tam) {
	if (c->silencioso) {
		return false;
	}
	g_mutex_lock(&c->trinco);
	c->est.fase = AR_CX_PEDINDO_SENHA;
	snprintf(c->est.mensagem, sizeof(c->est.mensagem), "%s", mensagem ? mensagem : "");
	c->est.quer_usuario = true;
	c->est.pode_lembrar = true;
	g_mutex_unlock(&c->trinco);
	cx_acordar(c);
	bool ok = cx_esperar_resposta(c);
	g_mutex_lock(&c->trinco);
	if (ok) {
		snprintf(usuario_saida, tam, "%s", c->resp_usuario);
		snprintf(senha_saida, tam, "%s", c->resp_senha);
		c->est.fase = AR_CX_CONECTANDO;
	}
	g_mutex_unlock(&c->trinco);
	explicit_bzero(c->resp_senha, sizeof(c->resp_senha));
	return ok;
}

void ar_conexao_estado_ler(ar_conexao *c, ar_conexao_estado *e) {
	g_mutex_lock(&c->trinco);
	*e = c->est;
	g_mutex_unlock(&c->trinco);
}

void ar_conexao_responder(ar_conexao *c, bool ok, const char *usuario, const char *senha, const char *dominio, bool lembrar, bool anonimo) {
	g_mutex_lock(&c->trinco);
	c->resp_ok = ok;
	snprintf(c->resp_usuario, sizeof(c->resp_usuario), "%s", usuario ? usuario : "");
	snprintf(c->resp_senha, sizeof(c->resp_senha), "%s", senha ? senha : "");
	snprintf(c->resp_dominio, sizeof(c->resp_dominio), "%s", dominio ? dominio : "");
	c->resp_lembrar = lembrar;
	c->resp_anonimo = anonimo;
	c->respondido = true;
	g_cond_signal(&c->cond);
	g_mutex_unlock(&c->trinco);
}

void ar_conexao_escolher(ar_conexao *c, int opcao) {
	g_mutex_lock(&c->trinco);
	c->resp_ok = opcao >= 0;
	c->resp_opcao = opcao;
	c->respondido = true;
	g_cond_signal(&c->cond);
	g_mutex_unlock(&c->trinco);
}

/* --- o gvfs de verdade --- */

typedef struct gio_ctx {
	ar_conexao *c;
	GMainLoop *loop;
	bool ok;
	char erro[256];
} gio_ctx;

static void gio_fim(gio_ctx *g, GError *err, bool ja_montado_vale) {
	if (!err) {
		g->ok = true;
	} else if (ja_montado_vale && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED)) {
		g->ok = true;
	} else {
		g->ok = false;
		snprintf(g->erro, sizeof(g->erro), "%s", err->message);
	}
	g_main_loop_quit(g->loop);
}

static void gio_montou(GObject *origem, GAsyncResult *res, gpointer dados) {
	gio_ctx *g = dados;
	GError *err = NULL;
	g_file_mount_enclosing_volume_finish(G_FILE(origem), res, &err);
	gio_fim(g, err, true);
	g_clear_error(&err);
}

static void gio_desmontou(GObject *origem, GAsyncResult *res, gpointer dados) {
	gio_ctx *g = dados;
	GError *err = NULL;
	g_mount_unmount_with_operation_finish(G_MOUNT(origem), res, &err);
	gio_fim(g, err, false);
	g_clear_error(&err);
}

static void gio_pede_senha(GMountOperation *op, const char *mensagem, const char *usuario, const char *dominio, GAskPasswordFlags flags, gpointer dados) {
	gio_ctx *g = dados;
	ar_conexao *c = g->c;
	if (c->silencioso) {
		g_mount_operation_reply(op, G_MOUNT_OPERATION_ABORTED);
		return;
	}
	g_mutex_lock(&c->trinco);
	c->est.fase = AR_CX_PEDINDO_SENHA;
	snprintf(c->est.mensagem, sizeof(c->est.mensagem), "%s", mensagem ? mensagem : "");
	snprintf(c->est.usuario, sizeof(c->est.usuario), "%s", usuario ? usuario : "");
	snprintf(c->est.dominio, sizeof(c->est.dominio), "%s", dominio ? dominio : "");
	c->est.quer_usuario = (flags & G_ASK_PASSWORD_NEED_USERNAME) != 0;
	c->est.quer_dominio = (flags & G_ASK_PASSWORD_NEED_DOMAIN) != 0;
	c->est.pode_lembrar = (flags & G_ASK_PASSWORD_SAVING_SUPPORTED) != 0;
	c->est.anonimo_possivel = (flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED) != 0;
	g_mutex_unlock(&c->trinco);
	cx_acordar(c);
	bool ok = cx_esperar_resposta(c);
	g_mutex_lock(&c->trinco);
	c->est.fase = AR_CX_CONECTANDO;
	if (ok) {
		if (c->resp_anonimo) {
			g_mount_operation_set_anonymous(op, TRUE);
		} else {
			if (flags & G_ASK_PASSWORD_NEED_USERNAME) {
				g_mount_operation_set_username(op, c->resp_usuario);
			}
			if (flags & G_ASK_PASSWORD_NEED_DOMAIN) {
				g_mount_operation_set_domain(op, c->resp_dominio);
			}
			g_mount_operation_set_password(op, c->resp_senha);
		}
		g_mount_operation_set_password_save(op, c->resp_lembrar ? G_PASSWORD_SAVE_PERMANENTLY : G_PASSWORD_SAVE_NEVER);
	}
	g_mutex_unlock(&c->trinco);
	explicit_bzero(c->resp_senha, sizeof(c->resp_senha));
	g_mount_operation_reply(op, ok ? G_MOUNT_OPERATION_HANDLED : G_MOUNT_OPERATION_ABORTED);
}

static void gio_pergunta(GMountOperation *op, const char *mensagem, const char **escolhas, gpointer dados) {
	gio_ctx *g = dados;
	ar_conexao *c = g->c;
	if (c->silencioso) {
		g_mount_operation_reply(op, G_MOUNT_OPERATION_ABORTED);
		return;
	}
	g_mutex_lock(&c->trinco);
	c->est.fase = AR_CX_PERGUNTANDO;
	snprintf(c->est.mensagem, sizeof(c->est.mensagem), "%s", mensagem ? mensagem : "");
	c->est.n_opcoes = 0;
	for (int i = 0; escolhas && escolhas[i] && i < 4; i++) {
		snprintf(c->est.opcoes[c->est.n_opcoes++], sizeof(c->est.opcoes[0]), "%s", escolhas[i]);
	}
	g_mutex_unlock(&c->trinco);
	cx_acordar(c);
	bool ok = cx_esperar_resposta(c);
	g_mutex_lock(&c->trinco);
	c->est.fase = AR_CX_CONECTANDO;
	int opcao = c->resp_opcao;
	g_mutex_unlock(&c->trinco);
	if (ok) {
		g_mount_operation_set_choice(op, opcao);
	}
	g_mount_operation_reply(op, ok ? G_MOUNT_OPERATION_HANDLED : G_MOUNT_OPERATION_ABORTED);
}

static void gio_aborto(GMountOperation *op, gpointer dados) {
	gio_ctx *g = dados;
	(void)op;
	g_main_loop_quit(g->loop);
}

static gpointer cx_thread(gpointer d) {
	ar_conexao *c = d;
	char erro[256] = "";
	bool ok = false;
	if (montagem_falsa) {
		if (c->desconectar) {
			falso_marcar(c->uri, false);
			ok = true;
		} else {
			ok = montagem_falsa(c, c->uri, erro, sizeof(erro));
			if (ok) {
				falso_marcar(c->uri, true);
			}
		}
	} else {
		GMainContext *ctx = g_main_context_new();
		g_main_context_push_thread_default(ctx);
		gio_ctx g = { .c = c, .loop = g_main_loop_new(ctx, FALSE), .ok = false };
		GMountOperation *op = g_mount_operation_new();
		g_signal_connect(op, "ask-password", G_CALLBACK(gio_pede_senha), &g);
		g_signal_connect(op, "ask-question", G_CALLBACK(gio_pergunta), &g);
		g_signal_connect(op, "aborted", G_CALLBACK(gio_aborto), &g);
		GFile *f = g_file_new_for_uri(c->uri);
		if (c->desconectar) {
			GMount *m = g_file_find_enclosing_mount(f, NULL, NULL);
			if (!m) {
				g.ok = true; /* já estava desconectado */
			} else {
				g_mount_unmount_with_operation(m, G_MOUNT_UNMOUNT_NONE, op, NULL, gio_desmontou, &g);
				g_main_loop_run(g.loop);
				g_object_unref(m);
			}
		} else {
			g_file_mount_enclosing_volume(f, G_MOUNT_MOUNT_NONE, op, NULL, gio_montou, &g);
			g_main_loop_run(g.loop);
		}
		ok = g.ok;
		snprintf(erro, sizeof(erro), "%s", g.erro);
		g_object_unref(f);
		g_object_unref(op);
		g_main_loop_unref(g.loop);
		g_main_context_pop_thread_default(ctx);
		g_main_context_unref(ctx);
	}
	g_mutex_lock(&c->trinco);
	bool cancelada = !ok && c->respondido && !c->resp_ok;
	c->est.fase = ok ? AR_CX_OK : cancelada ? AR_CX_CANCELADA : AR_CX_ERRO;
	if (!ok && erro[0]) {
		snprintf(c->est.mensagem, sizeof(c->est.mensagem), "%s", erro);
	} else if (!ok && !cancelada) {
		snprintf(c->est.mensagem, sizeof(c->est.mensagem), "Não consegui conectar");
	}
	c->acabou = true;
	g_mutex_unlock(&c->trinco);
	cx_acordar(c);
	return NULL;
}

static ar_conexao *cx_iniciar(const char *uri, bool silencioso, bool desconectar, void (*acordar)(void *), void *dados) {
	ar_conexao *c = g_new0(ar_conexao, 1);
	g_mutex_init(&c->trinco);
	g_cond_init(&c->cond);
	c->uri = g_strdup(uri);
	c->silencioso = silencioso;
	c->desconectar = desconectar;
	c->acordar = acordar;
	c->dados = dados;
	c->est.fase = AR_CX_CONECTANDO;
	c->thread = g_thread_new("atlas-conexao", cx_thread, c);
	return c;
}

ar_conexao *ar_share_conectar(const char *uri, bool silencioso, void (*acordar)(void *), void *dados) {
	return cx_iniciar(uri, silencioso, false, acordar, dados);
}

ar_conexao *ar_share_desconectar(const char *uri, void (*acordar)(void *), void *dados) {
	return cx_iniciar(uri, true, true, acordar, dados);
}

void ar_conexao_liberar(ar_conexao *c) {
	if (!c) {
		return;
	}
	g_mutex_lock(&c->trinco);
	if (!c->acabou) { /* pedido para soltar antes da hora: cancela o que estiver esperando a pessoa */
		c->respondido = true;
		c->resp_ok = false;
		g_cond_signal(&c->cond);
	}
	g_mutex_unlock(&c->trinco);
	g_thread_join(c->thread);
	g_free(c->uri);
	g_mutex_clear(&c->trinco);
	g_cond_clear(&c->cond);
	explicit_bzero(c->resp_senha, sizeof(c->resp_senha));
	g_free(c);
}

/* ---- nuvem (rclone) ---- */

const char *const AR_NUVEM_TIPOS[5] = { "nextcloud", "webdav", "drive", "dropbox", "onedrive" };
const char *const AR_NUVEM_NOMES[5] = { "Nextcloud", "WebDAV", "Google Drive", "Dropbox", "OneDrive" };

bool ar_nuvem_disponivel(void) {
	char *p = g_find_program_in_path("rclone");
	bool ok = p != NULL;
	g_free(p);
	return ok;
}

char *ar_nuvem_conf(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "atlas", "rclone.conf", NULL);
}

/* grava um arquivo que só a pessoa lê, de uma vez (nunca existe com o modo aberto) */
static bool gravar_privado(const char *caminho, const char *conteudo) {
	char *tmp = g_strdup_printf("%s.%d.tmp", caminho, (int)getpid());
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		g_free(tmp);
		return false;
	}
	size_t n = strlen(conteudo);
	bool ok = write(fd, conteudo, n) == (ssize_t)n;
	ok = close(fd) == 0 && ok;
	ok = ok && rename(tmp, caminho) == 0;
	if (!ok) {
		unlink(tmp);
	}
	g_free(tmp);
	return ok;
}

static GKeyFile *conf_ler(void) {
	GKeyFile *kf = g_key_file_new();
	char *a = ar_nuvem_conf();
	g_key_file_load_from_file(kf, a, G_KEY_FILE_KEEP_COMMENTS, NULL);
	g_free(a);
	return kf;
}

static bool conf_gravar(GKeyFile *kf) {
	char *a = ar_nuvem_conf();
	char *pasta = g_path_get_dirname(a);
	g_mkdir_with_parents(pasta, 0700);
	gsize n;
	char *dados = g_key_file_to_data(kf, &n, NULL);
	bool ok = gravar_privado(a, dados);
	g_free(dados);
	g_free(pasta);
	g_free(a);
	return ok;
}

/* roda o rclone: saida recebe stdout+stderr (g_free) */
static bool rclone_rodar(char **argv, const char *entrada, char **saida) {
	GError *err = NULL;
	GSubprocessLauncher *l = g_subprocess_launcher_new(entrada ? G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE
		: G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
	GSubprocess *p = g_subprocess_launcher_spawnv(l, (const char *const *)argv, &err);
	g_object_unref(l);
	if (!p) {
		if (saida) {
			*saida = g_strdup(err ? err->message : "não consegui rodar o rclone");
		}
		g_clear_error(&err);
		return false;
	}
	char *out = NULL;
	g_subprocess_communicate_utf8(p, entrada, NULL, &out, NULL, &err);
	g_clear_error(&err);
	bool ok = g_subprocess_get_if_exited(p) && g_subprocess_get_exit_status(p) == 0;
	g_object_unref(p);
	if (saida) {
		*saida = out ? out : g_strdup("");
	} else {
		g_free(out);
	}
	return ok;
}

void ar_nuvem_nome_remote(const ar_rede *r, const char *nome, char *saida, size_t tam) {
	char slug[32] = "";
	size_t k = 0;
	for (const char *c = nome; *c && k < 24; c++) {
		if (isalnum((unsigned char)*c)) {
			slug[k++] = (char)tolower((unsigned char)*c);
		}
	}
	slug[k] = '\0';
	if (!slug[0]) {
		snprintf(slug, sizeof(slug), "conta");
	}
	for (int n = 0;; n++) {
		if (n) {
			snprintf(saida, tam, "atlas-%s%d", slug, n + 1);
		} else {
			snprintf(saida, tam, "atlas-%s", slug);
		}
		bool existe = false;
		for (int i = 0; i < r->n_contas; i++) {
			existe |= !strcmp(r->contas[i].remote, saida);
		}
		if (!existe) {
			return;
		}
	}
}

int ar_nuvem_adicionar_conta(ar_rede *r, const char *nome, const char *tipo, const char *remote) {
	if (r->n_contas >= AR_CONTAS_MAX) {
		return -1;
	}
	ar_conta *c = &r->contas[r->n_contas];
	memset(c, 0, sizeof(*c));
	snprintf(c->nome, sizeof(c->nome), "%s", nome);
	snprintf(c->tipo, sizeof(c->tipo), "%s", tipo);
	snprintf(c->remote, sizeof(c->remote), "%s", remote);
	char *slug = g_strdup(nome);
	for (char *p = slug; *p; p++) {
		if (*p == '/' || *p == '\\') {
			*p = '_';
		}
	}
	char *pasta = g_build_filename(g_get_home_dir(), "Nuvem", slug, NULL);
	snprintf(c->pasta_local, sizeof(c->pasta_local), "%s", pasta);
	g_free(pasta);
	g_free(slug);
	c->segundo_plano = true;
	return r->n_contas++;
}

int ar_nuvem_criar_webdav(ar_rede *r, const char *nome, const char *tipo, const char *url, const char *usuario, const char *senha, char *erro, size_t tam_erro) {
	if (!ar_nuvem_disponivel()) {
		texto_erro(erro, tam_erro, "O rclone não está instalado (as contas na nuvem dependem dele)");
		return -1;
	}
	if (!nome || !nome[0] || !url || !usuario || !usuario[0] || !senha || !senha[0]) {
		texto_erro(erro, tam_erro, "Preencha o nome, o endereço, o usuário e a senha");
		return -1;
	}
	char *u = g_strstrip(g_strdup(url));
	if (!g_str_has_prefix(u, "https://") && !g_str_has_prefix(u, "http://")) {
		g_free(u);
		texto_erro(erro, tam_erro, "O endereço da nuvem começa com https://");
		return -1;
	}
	bool nextcloud = !strcmp(tipo, "nextcloud");
	char *url_final;
	if (nextcloud && !strstr(u, "remote.php")) { /* só o endereço do servidor: o caminho do WebDAV do Nextcloud é conhecido */
		size_t n = strlen(u);
		while (n > 0 && u[n - 1] == '/') {
			u[--n] = '\0';
		}
		url_final = g_strdup_printf("%s/remote.php/dav/files/%s/", u, usuario);
	} else {
		url_final = g_strdup(u);
	}
	g_free(u);
	char *obs = NULL;
	char *argv[] = { "rclone", "obscure", "-", NULL };
	if (!rclone_rodar(argv, senha, &obs) || !obs || !g_strstrip(obs)[0]) {
		texto_erro(erro, tam_erro, "O rclone não conseguiu ofuscar a senha");
		g_free(obs);
		g_free(url_final);
		return -1;
	}
	char remote[48];
	ar_nuvem_nome_remote(r, nome, remote, sizeof(remote));
	GKeyFile *kf = conf_ler();
	g_key_file_set_string(kf, remote, "type", "webdav");
	g_key_file_set_string(kf, remote, "url", url_final);
	g_key_file_set_string(kf, remote, "vendor", nextcloud ? "nextcloud" : "other");
	g_key_file_set_string(kf, remote, "user", usuario);
	g_key_file_set_string(kf, remote, "pass", obs);
	bool ok = conf_gravar(kf);
	g_key_file_free(kf);
	explicit_bzero(obs, strlen(obs));
	g_free(obs);
	g_free(url_final);
	if (!ok) {
		texto_erro(erro, tam_erro, "Não consegui gravar o rclone.conf");
		return -1;
	}
	int i = ar_nuvem_adicionar_conta(r, nome, tipo, remote);
	if (i < 0) {
		texto_erro(erro, tam_erro, "Já são contas demais");
	}
	return i;
}

void ar_nuvem_remover(ar_rede *r, int i) {
	if (i < 0 || i >= r->n_contas) {
		return;
	}
	GKeyFile *kf = conf_ler();
	if (g_key_file_remove_group(kf, r->contas[i].remote, NULL)) {
		conf_gravar(kf);
	}
	g_key_file_free(kf);
	memmove(&r->contas[i], &r->contas[i + 1], sizeof(ar_conta) * (size_t)(r->n_contas - i - 1));
	r->n_contas--;
}

/* --- as threads do rclone --- */

struct ar_sync {
	GMutex trinco;
	ar_sync_estado est;
	char **argv;
	void (*acordar)(void *);
	void *dados;
	GThread *thread;
	char *pasta_local;
};

static gpointer sync_thread(gpointer d) {
	ar_sync *s = d;
	if (s->pasta_local) {
		g_mkdir_with_parents(s->pasta_local, 0700);
	}
	char *saida = NULL;
	bool ok = rclone_rodar(s->argv, NULL, &saida);
	g_mutex_lock(&s->trinco);
	s->est.fase = ok ? AR_SY_OK : AR_SY_ERRO;
	if (!ok) {
		/* a última linha com texto da saída é a razão */
		char *fim = saida ? g_strstrip(saida) : NULL;
		char *ultima = fim ? strrchr(fim, '\n') : NULL;
		snprintf(s->est.mensagem, sizeof(s->est.mensagem), "%s", ultima ? g_strstrip(ultima) : fim && fim[0] ? fim : "o rclone falhou");
	}
	g_mutex_unlock(&s->trinco);
	g_free(saida);
	if (s->acordar) {
		s->acordar(s->dados);
	}
	return NULL;
}

static ar_sync *sync_iniciar(char **argv, const char *pasta_local, void (*acordar)(void *), void *dados) {
	ar_sync *s = g_new0(ar_sync, 1);
	g_mutex_init(&s->trinco);
	s->argv = argv;
	s->acordar = acordar;
	s->dados = dados;
	s->pasta_local = g_strdup(pasta_local);
	s->est.fase = AR_SY_ANDANDO;
	s->thread = g_thread_new("atlas-rclone", sync_thread, s);
	return s;
}

ar_sync *ar_nuvem_entrar(const char *remote, const char *tipo, void (*acordar)(void *), void *dados) {
	char *conf = ar_nuvem_conf();
	char *pasta = g_path_get_dirname(conf);
	g_mkdir_with_parents(pasta, 0700);
	g_free(pasta);
	char **argv = g_new0(char *, 8);
	argv[0] = g_strdup("rclone");
	argv[1] = g_strdup("config");
	argv[2] = g_strdup("create");
	argv[3] = g_strdup("--config");
	argv[4] = conf;
	argv[5] = g_strdup(remote);
	argv[6] = g_strdup(tipo);
	return sync_iniciar(argv, NULL, acordar, dados);
}

ar_sync *ar_nuvem_sincronizar(const ar_conta *c, void (*acordar)(void *), void *dados) {
	char **argv = g_new0(char *, 10);
	int k = 0;
	argv[k++] = g_strdup("rclone");
	argv[k++] = g_strdup("bisync");
	argv[k++] = g_strdup_printf("%s:", c->remote);
	argv[k++] = g_strdup(c->pasta_local);
	argv[k++] = g_strdup("--config");
	argv[k++] = ar_nuvem_conf();
	if (!c->sincronizada) {
		argv[k++] = g_strdup("--resync"); /* a primeira vez junta os dois lados sem apagar nada */
	}
	return sync_iniciar(argv, c->pasta_local, acordar, dados);
}

void ar_sync_estado_ler(ar_sync *s, ar_sync_estado *e) {
	g_mutex_lock(&s->trinco);
	*e = s->est;
	g_mutex_unlock(&s->trinco);
}

void ar_sync_liberar(ar_sync *s) {
	if (!s) {
		return;
	}
	g_thread_join(s->thread);
	g_strfreev(s->argv);
	g_free(s->pasta_local);
	g_mutex_clear(&s->trinco);
	g_free(s);
}
