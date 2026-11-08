#define _GNU_SOURCE
#include "arquivos_op.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- a lixeira ---- */

char *ar_lixeira_pasta(void) {
	return g_build_filename(g_get_user_data_dir(), "Trash", "files", NULL);
}
static char *lixeira_info(void) {
	return g_build_filename(g_get_user_data_dir(), "Trash", "info", NULL);
}

static bool garantir_lixeira(void) {
	char *f = ar_lixeira_pasta(), *i = lixeira_info();
	bool ok = g_mkdir_with_parents(f, 0700) == 0 && g_mkdir_with_parents(i, 0700) == 0;
	g_free(f);
	g_free(i);
	return ok;
}

char *ar_nome_livre(const char *pasta, const char *nome) {
	char *c = g_build_filename(pasta, nome, NULL);
	if (!g_file_test(c, G_FILE_TEST_EXISTS) && !g_file_test(c, G_FILE_TEST_IS_SYMLINK)) {
		g_free(c);
		return g_strdup(nome);
	}
	g_free(c);
	const char *ponto = strrchr(nome, '.');
	/* ".tar.gz" é uma extensão só */
	if (ponto && ponto != nome) {
		const char *ant = ponto - 1;
		while (ant > nome && *ant != '.') {
			ant--;
		}
		if (ant > nome && !strcmp(ant, ".tar.gz")) {
			ponto = ant;
		} else if (ant > nome && (g_str_has_suffix(nome, ".tar.xz") || g_str_has_suffix(nome, ".tar.bz2") || g_str_has_suffix(nome, ".tar.zst"))) {
			ponto = ant;
		}
	}
	for (int k = 2; k < 10000; k++) {
		char *cand = ponto && ponto != nome ? g_strdup_printf("%.*s (%d)%s", (int)(ponto - nome), nome, k, ponto) : g_strdup_printf("%s (%d)", nome, k);
		c = g_build_filename(pasta, cand, NULL);
		bool existe = g_file_test(c, G_FILE_TEST_EXISTS) || g_file_test(c, G_FILE_TEST_IS_SYMLINK);
		g_free(c);
		if (!existe) {
			return cand;
		}
		g_free(cand);
	}
	return g_strdup(nome);
}

static char *uri_do_caminho(const char *caminho) {
	char *u = g_filename_to_uri(caminho, NULL, NULL);
	char *r = u ? g_strdup(u + 7) : g_strdup(caminho);
	g_free(u);
	return r;
}

/* manda para a lixeira do usuário; *nome: o nome lá dentro (g_free; NULL se foi pela lixeira de outro disco, sem como restaurar daqui) */
static bool lixeira_enviar(const char *caminho, char **nome, char **erro) {
	*nome = NULL;
	if (!garantir_lixeira()) {
		*erro = g_strdup("Não consegui criar a lixeira");
		return false;
	}
	char *files = ar_lixeira_pasta(), *info = lixeira_info();
	struct stat a, b;
	bool mesmo_disco = lstat(caminho, &a) == 0 && stat(files, &b) == 0 && a.st_dev == b.st_dev;
	bool ok = false;
	if (!mesmo_disco) {
		GFile *f = g_file_new_for_path(caminho);
		GError *err = NULL;
		ok = g_file_trash(f, NULL, &err);
		if (!ok) {
			*erro = g_strdup(err && err->message ? err->message : "não deu para mandar para a lixeira");
		}
		g_clear_error(&err);
		g_object_unref(f);
	} else {
		char *base = g_path_get_basename(caminho);
		char *unico = g_strdup(base);
		for (int k = 2; k < 10000; k++) {
			char *p1 = g_build_filename(files, unico, NULL), *p2 = g_strdup_printf("%s/%s.trashinfo", info, unico);
			bool livre = !g_file_test(p1, G_FILE_TEST_EXISTS) && !g_file_test(p1, G_FILE_TEST_IS_SYMLINK) && !g_file_test(p2, G_FILE_TEST_EXISTS);
			g_free(p1);
			g_free(p2);
			if (livre) {
				break;
			}
			g_free(unico);
			unico = g_strdup_printf("%s.%d", base, k);
		}
		char *abs = g_canonicalize_filename(caminho, NULL);
		char *cod = uri_do_caminho(abs);
		time_t agora = time(NULL);
		struct tm tm;
		localtime_r(&agora, &tm);
		char data[32];
		strftime(data, sizeof(data), "%Y-%m-%dT%H:%M:%S", &tm);
		char *conteudo = g_strdup_printf("[Trash Info]\nPath=%s\nDeletionDate=%s\n", cod, data);
		char *arq_info = g_strdup_printf("%s/%s.trashinfo", info, unico), *destino = g_build_filename(files, unico, NULL);
		if (g_file_set_contents(arq_info, conteudo, -1, NULL) && rename(caminho, destino) == 0) {
			ok = true;
			*nome = g_strdup(unico);
		} else {
			*erro = g_strdup(g_strerror(errno));
			g_unlink(arq_info);
		}
		g_free(destino);
		g_free(arq_info);
		g_free(conteudo);
		g_free(cod);
		g_free(abs);
		g_free(unico);
		g_free(base);
	}
	g_free(files);
	g_free(info);
	return ok;
}

char *ar_lixeira_origem(const char *nome) {
	char *info = lixeira_info();
	char *arq = g_strdup_printf("%s/%s.trashinfo", info, nome), *txt = NULL;
	char *origem = NULL;
	if (g_file_get_contents(arq, &txt, NULL, NULL)) {
		char *p = strstr(txt, "Path=");
		if (p) {
			p += 5;
			char *fim = strchr(p, '\n');
			char *valor = fim ? g_strndup(p, (gsize)(fim - p)) : g_strdup(p);
			char *uri = g_strconcat("file://", valor, NULL);
			origem = g_filename_from_uri(uri, NULL, NULL);
			if (!origem && valor[0] == '/') {
				origem = g_strdup(valor);
			}
			g_free(uri);
			g_free(valor);
		}
	}
	g_free(txt);
	g_free(arq);
	g_free(info);
	return origem;
}

bool ar_lixeira_restaurar(const char *nome, char **destino, char **erro) {
	char *origem = ar_lixeira_origem(nome);
	if (!origem) {
		*erro = g_strdup("Não sei de onde este item veio");
		return false;
	}
	char *files = ar_lixeira_pasta(), *info = lixeira_info();
	char *de = g_build_filename(files, nome, NULL);
	char *pasta = g_path_get_dirname(origem);
	g_mkdir_with_parents(pasta, 0755);
	bool ok = false;
	char *alvo = origem;
	if (g_file_test(origem, G_FILE_TEST_EXISTS) || g_file_test(origem, G_FILE_TEST_IS_SYMLINK)) {
		char *base = g_path_get_basename(origem);
		char *livre = ar_nome_livre(pasta, base);
		alvo = g_build_filename(pasta, livre, NULL); /* já existe um com esse nome lá: volta com (2) */
		g_free(livre);
		g_free(base);
		g_free(origem);
	}
	if (rename(de, alvo) == 0) {
		char *arq = g_strdup_printf("%s/%s.trashinfo", info, nome);
		g_unlink(arq);
		g_free(arq);
		ok = true;
		if (destino) {
			*destino = g_strdup(alvo);
		}
	} else {
		*erro = g_strdup(g_strerror(errno));
	}
	g_free(alvo);
	g_free(pasta);
	g_free(de);
	g_free(files);
	g_free(info);
	return ok;
}

/* ---- apagar para valer ---- */

static bool remover_arvore(const char *caminho) {
	struct stat st;
	if (lstat(caminho, &st) != 0) {
		return errno == ENOENT;
	}
	if (S_ISDIR(st.st_mode)) {
		GDir *d = g_dir_open(caminho, 0, NULL);
		const char *n;
		bool ok = true;
		while (d && (n = g_dir_read_name(d))) {
			char *f = g_build_filename(caminho, n, NULL);
			ok = remover_arvore(f) && ok;
			g_free(f);
		}
		if (d) {
			g_dir_close(d);
		}
		return rmdir(caminho) == 0 && ok;
	}
	return unlink(caminho) == 0;
}

bool ar_lixeira_esvaziar(char **erro) {
	char *files = ar_lixeira_pasta(), *info = lixeira_info();
	bool ok = true;
	char *pastas[2] = { files, info };
	for (int k = 0; k < 2; k++) {
		GDir *d = g_dir_open(pastas[k], 0, NULL);
		const char *n;
		while (d && (n = g_dir_read_name(d))) {
			char *f = g_build_filename(pastas[k], n, NULL);
			if (!remover_arvore(f)) {
				ok = false;
			}
			g_free(f);
		}
		if (d) {
			g_dir_close(d);
		}
	}
	if (!ok) {
		*erro = g_strdup("Alguns itens não saíram da lixeira");
	}
	g_free(files);
	g_free(info);
	return ok;
}

/* ---- nomes ---- */

bool ar_nome_valido(const char *nome, char **motivo) {
	if (!nome || !*nome) {
		*motivo = g_strdup("O nome não pode ficar vazio");
		return false;
	}
	if (strchr(nome, '/')) {
		*motivo = g_strdup("O nome não pode ter uma barra (/)");
		return false;
	}
	if (!strcmp(nome, ".") || !strcmp(nome, "..")) {
		*motivo = g_strdup("Esse nome não vale");
		return false;
	}
	if (strlen(nome) > 250) {
		*motivo = g_strdup("O nome é comprido demais");
		return false;
	}
	return true;
}

/* ---- desfazer ---- */

enum { DESF_MOVER, DESF_LIXEIRA, DESF_CRIADOS };
typedef struct desfazer {
	int tipo;
	char descricao[96];
	GPtrArray *a, *b;
} desfazer;
static GMutex undo_mx;
static GPtrArray *undo_pilha;

static void undo_liberar(desfazer *d) {
	g_ptr_array_free(d->a, TRUE);
	g_ptr_array_free(d->b, TRUE);
	g_free(d);
}

static void undo_empilhar(int tipo, const char *descricao, GPtrArray *a, GPtrArray *b) {
	if (!a->len) {
		g_ptr_array_free(a, TRUE);
		g_ptr_array_free(b, TRUE);
		return;
	}
	desfazer *d = g_new0(desfazer, 1);
	d->tipo = tipo;
	g_strlcpy(d->descricao, descricao, sizeof(d->descricao));
	d->a = a;
	d->b = b;
	g_mutex_lock(&undo_mx);
	if (!undo_pilha) {
		undo_pilha = g_ptr_array_new();
	}
	g_ptr_array_add(undo_pilha, d);
	while (undo_pilha->len > 20) {
		undo_liberar(undo_pilha->pdata[0]);
		g_ptr_array_remove_index(undo_pilha, 0);
	}
	g_mutex_unlock(&undo_mx);
}

bool ar_desfazer_ha(char *descricao, int tam) {
	g_mutex_lock(&undo_mx);
	bool ha = undo_pilha && undo_pilha->len > 0;
	if (ha && descricao) {
		g_strlcpy(descricao, ((desfazer *)undo_pilha->pdata[undo_pilha->len - 1])->descricao, (gsize)tam);
	}
	g_mutex_unlock(&undo_mx);
	return ha;
}

void ar_desfazer_limpar(void) {
	g_mutex_lock(&undo_mx);
	while (undo_pilha && undo_pilha->len) {
		undo_liberar(undo_pilha->pdata[undo_pilha->len - 1]);
		g_ptr_array_remove_index(undo_pilha, undo_pilha->len - 1);
	}
	g_mutex_unlock(&undo_mx);
}

bool ar_desfazer(char **erro) {
	g_mutex_lock(&undo_mx);
	desfazer *d = undo_pilha && undo_pilha->len ? g_ptr_array_steal_index(undo_pilha, undo_pilha->len - 1) : NULL;
	g_mutex_unlock(&undo_mx);
	if (!d) {
		*erro = g_strdup("Não há nada para desfazer");
		return false;
	}
	bool ok = true;
	for (int i = (int)d->a->len - 1; i >= 0; i--) {
		const char *a = d->a->pdata[i], *b = d->b->pdata[i];
		char *e = NULL;
		if (d->tipo == DESF_MOVER) { /* a: de onde veio, b: para onde foi */
			if (g_file_test(a, G_FILE_TEST_EXISTS) || g_file_test(a, G_FILE_TEST_IS_SYMLINK)) {
				e = g_strdup_printf("Já existe %s", a);
			} else if (rename(b, a) != 0) {
				e = g_strdup(g_strerror(errno));
			}
		} else if (d->tipo == DESF_LIXEIRA) { /* a: de onde veio, b: o nome na lixeira */
			char *dest = NULL;
			ar_lixeira_restaurar(b, &dest, &e);
			g_free(dest);
		} else { /* DESF_CRIADOS: manda os criados para a lixeira */
			char *nome = NULL;
			lixeira_enviar(a, &nome, &e);
			g_free(nome);
		}
		if (e) {
			if (ok && erro) {
				*erro = e;
			} else {
				g_free(e);
			}
			ok = false;
		}
	}
	undo_liberar(d);
	return ok;
}

/* ---- renomear e criar ---- */

bool ar_renomear(const char *caminho, const char *novo_nome, char **novo, char **erro) {
	*novo = NULL;
	if (!ar_nome_valido(novo_nome, erro)) {
		return false;
	}
	char *pasta = g_path_get_dirname(caminho), *base = g_path_get_basename(caminho);
	char *alvo = g_build_filename(pasta, novo_nome, NULL);
	bool ok = false;
	if (!strcmp(base, novo_nome)) {
		*novo = g_strdup(caminho);
		ok = true;
	} else if (g_file_test(alvo, G_FILE_TEST_EXISTS) || g_file_test(alvo, G_FILE_TEST_IS_SYMLINK)) {
		*erro = g_strdup_printf("Já existe um item chamado \"%s\" aqui", novo_nome);
	} else if (rename(caminho, alvo) != 0) {
		*erro = g_strdup(g_strerror(errno));
	} else {
		GPtrArray *a = g_ptr_array_new_with_free_func(g_free), *b = g_ptr_array_new_with_free_func(g_free);
		g_ptr_array_add(a, g_strdup(caminho));
		g_ptr_array_add(b, g_strdup(alvo));
		undo_empilhar(DESF_MOVER, "Renomear", a, b);
		*novo = g_strdup(alvo);
		ok = true;
	}
	g_free(alvo);
	g_free(base);
	g_free(pasta);
	return ok;
}

bool ar_criar(const char *pasta, const char *nome, bool eh_pasta, char **criado, char **erro) {
	*criado = NULL;
	if (!ar_nome_valido(nome, erro)) {
		return false;
	}
	char *livre = ar_nome_livre(pasta, nome);
	char *alvo = g_build_filename(pasta, livre, NULL);
	bool ok;
	if (eh_pasta) {
		ok = g_mkdir(alvo, 0755) == 0;
	} else {
		int fd = open(alvo, O_WRONLY | O_CREAT | O_EXCL, 0644);
		ok = fd >= 0;
		if (ok) {
			close(fd);
		}
	}
	if (!ok) {
		*erro = g_strdup(g_strerror(errno));
	} else {
		GPtrArray *a = g_ptr_array_new_with_free_func(g_free), *b = g_ptr_array_new_with_free_func(g_free);
		g_ptr_array_add(a, g_strdup(alvo));
		g_ptr_array_add(b, g_strdup(""));
		undo_empilhar(DESF_CRIADOS, eh_pasta ? "Nova pasta" : "Novo arquivo", a, b);
		*criado = g_strdup(alvo);
	}
	g_free(alvo);
	g_free(livre);
	return ok;
}

/* ---- as operações com thread ---- */

struct ar_op {
	enum ar_op_tipo tipo;
	GPtrArray *origens;
	char *destino;
	GThread *thread;
	GMutex mx;
	GCond cond;
	ar_op_estado est;
	enum ar_conflito politica;
	bool resposta_pronta;
	enum ar_conflito resposta;
	GCancellable *canc;
	void (*acordar)(void *);
	void *dados;
	GPtrArray *pares_a, *pares_b; /* para o desfazer */
	int desf_tipo;
};

static void op_acordar(ar_op *op) {
	if (op->acordar) {
		op->acordar(op->dados);
	}
}

static void op_atual(ar_op *op, const char *nome) {
	g_mutex_lock(&op->mx);
	g_strlcpy(op->est.atual, nome, sizeof(op->est.atual));
	g_mutex_unlock(&op->mx);
	op_acordar(op);
}

static void op_erro(ar_op *op, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static void op_erro(ar_op *op, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char *msg = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	g_mutex_lock(&op->mx);
	if (!op->est.erros) {
		g_strlcpy(op->est.erro, msg, sizeof(op->est.erro));
	}
	op->est.erros++;
	g_mutex_unlock(&op->mx);
	g_free(msg);
}

static bool op_cancelada(ar_op *op) {
	return g_cancellable_is_cancelled(op->canc);
}

static void op_criou(ar_op *op, const char *caminho) {
	g_mutex_lock(&op->mx);
	if (!op->est.criado[0]) {
		g_strlcpy(op->est.criado, caminho, sizeof(op->est.criado));
	}
	g_mutex_unlock(&op->mx);
}

static void op_item_feito(ar_op *op) {
	g_mutex_lock(&op->mx);
	op->est.itens_feitos++;
	g_mutex_unlock(&op->mx);
	op_acordar(op);
}

/* a pergunta de conflito: bloqueia a thread até a tela responder (ou cancelar) */
static enum ar_conflito perguntar(ar_op *op, const char *nome, bool pasta_destino) {
	g_mutex_lock(&op->mx);
	if (op->politica != AR_C_PERGUNTAR) {
		enum ar_conflito r = op->politica;
		g_mutex_unlock(&op->mx);
		return r;
	}
	op->est.pergunta = true;
	op->est.pergunta_pasta_destino = pasta_destino;
	g_strlcpy(op->est.pergunta_nome, nome, sizeof(op->est.pergunta_nome));
	op->resposta_pronta = false;
	g_mutex_unlock(&op->mx);
	op_acordar(op);
	g_mutex_lock(&op->mx);
	while (!op->resposta_pronta && !op_cancelada(op)) {
		g_cond_wait(&op->cond, &op->mx);
	}
	enum ar_conflito r = op_cancelada(op) ? AR_C_PULAR : op->resposta;
	op->est.pergunta = false;
	op->resposta_pronta = false;
	g_mutex_unlock(&op->mx);
	return r;
}

void ar_op_responder(ar_op *op, enum ar_conflito escolha, bool para_todos) {
	g_mutex_lock(&op->mx);
	op->resposta = escolha;
	if (para_todos) {
		op->politica = escolha;
	}
	op->resposta_pronta = true;
	g_cond_broadcast(&op->cond);
	g_mutex_unlock(&op->mx);
}

void ar_op_cancelar(ar_op *op) {
	g_cancellable_cancel(op->canc);
	g_mutex_lock(&op->mx);
	op->est.cancelado = true;
	g_cond_broadcast(&op->cond);
	g_mutex_unlock(&op->mx);
}

static void contar(const char *caminho, int64_t *bytes, int *itens) {
	struct stat st;
	if (lstat(caminho, &st) != 0) {
		return;
	}
	(*itens)++;
	if (S_ISDIR(st.st_mode)) {
		GDir *d = g_dir_open(caminho, 0, NULL);
		const char *n;
		while (d && (n = g_dir_read_name(d))) {
			char *f = g_build_filename(caminho, n, NULL);
			contar(f, bytes, itens);
			g_free(f);
		}
		if (d) {
			g_dir_close(d);
		}
	} else {
		*bytes += st.st_size;
	}
}

typedef struct progresso {
	ar_op *op;
	int64_t ultimo;
} progresso;

static void ao_progresso(goffset atual, goffset total, gpointer p) {
	progresso *pr = p;
	g_mutex_lock(&pr->op->mx);
	pr->op->est.feito += atual - pr->ultimo;
	pr->ultimo = atual;
	g_mutex_unlock(&pr->op->mx);
	op_acordar(pr->op);
}

static bool copiar_arvore(ar_op *op, const char *src, const char *dst, bool substituir) {
	struct stat st;
	if (lstat(src, &st) != 0) {
		op_erro(op, "Não achei %s", src);
		return false;
	}
	char *base = g_path_get_basename(src);
	op_atual(op, base);
	g_free(base);
	if (op_cancelada(op)) {
		return false;
	}
	if (S_ISDIR(st.st_mode)) {
		struct stat dt;
		bool existe = lstat(dst, &dt) == 0;
		if (existe && !S_ISDIR(dt.st_mode)) {
			op_erro(op, "Já existe um arquivo (não pasta) em %s", dst);
			return false;
		}
		if (!existe && g_mkdir(dst, st.st_mode & 07777 ? (st.st_mode & 0777) | 0700 : 0755) != 0) {
			op_erro(op, "Não consegui criar %s: %s", dst, g_strerror(errno));
			return false;
		}
		GDir *d = g_dir_open(src, 0, NULL);
		const char *n;
		bool ok = true;
		while (d && (n = g_dir_read_name(d)) && !op_cancelada(op)) {
			char *s2 = g_build_filename(src, n, NULL), *d2 = g_build_filename(dst, n, NULL);
			struct stat t2, s2s;
			char *alvo = d2;
			bool pula = false, sub = false;
			if (lstat(d2, &t2) == 0) {
				bool src_dir = lstat(s2, &s2s) == 0 && S_ISDIR(s2s.st_mode);
				if (!(src_dir && S_ISDIR(t2.st_mode))) { /* pasta com pasta: junta, sem perguntar */
					enum ar_conflito r = perguntar(op, n, S_ISDIR(t2.st_mode));
					if (r == AR_C_PULAR) {
						pula = true;
					} else if (r == AR_C_MANTER_AMBOS) {
						char *livre = ar_nome_livre(dst, n);
						alvo = g_build_filename(dst, livre, NULL);
						g_free(livre);
					} else {
						sub = true;
					}
				}
			}
			if (pula) {
				op_item_feito(op);
			} else {
				ok = copiar_arvore(op, s2, alvo, sub) && ok;
			}
			if (alvo != d2) {
				g_free(alvo);
			}
			g_free(s2);
			g_free(d2);
		}
		if (d) {
			g_dir_close(d);
		}
		op_item_feito(op);
		return ok;
	}
	GFile *a = g_file_new_for_path(src), *b = g_file_new_for_path(dst);
	GError *err = NULL;
	progresso pr = { op, 0 };
	GFileCopyFlags fl = G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA | (substituir ? G_FILE_COPY_OVERWRITE : 0);
	if (substituir) {
		/* o que vai ser substituído não se perde: vai antes para a lixeira */
		char *nome = NULL, *e = NULL;
		lixeira_enviar(dst, &nome, &e);
		g_free(nome);
		g_free(e);
	}
	bool ok = g_file_copy(a, b, fl, op->canc, ao_progresso, &pr, &err);
	if (!ok && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		op_erro(op, "%s", err && err->message ? err->message : "Não deu para copiar");
	}
	g_mutex_lock(&op->mx);
	op->est.feito += st.st_size - pr.ultimo; /* arquivos pequenos e links não passam pelo progresso */
	g_mutex_unlock(&op->mx);
	g_clear_error(&err);
	g_object_unref(a);
	g_object_unref(b);
	op_item_feito(op);
	return ok;
}

static bool dentro_de(const char *filho, const char *pai) {
	size_t n = strlen(pai);
	return !strncmp(filho, pai, n) && (filho[n] == '/' || filho[n] == '\0');
}

static void registrar_criado(ar_op *op, const char *caminho) {
	op_criou(op, caminho);
	g_ptr_array_add(op->pares_a, g_strdup(caminho));
	g_ptr_array_add(op->pares_b, g_strdup(""));
}

static void rodar_copiar_mover(ar_op *op, bool mover) {
	for (guint i = 0; i < op->origens->len && !op_cancelada(op); i++) {
		const char *src = op->origens->pdata[i];
		char *base = g_path_get_basename(src), *pai = g_path_get_dirname(src);
		char *dst = g_build_filename(op->destino, base, NULL);
		bool mesma_pasta = !strcmp(pai, op->destino);
		struct stat st;
		bool src_dir = lstat(src, &st) == 0 && S_ISDIR(st.st_mode);
		if (src_dir && dentro_de(op->destino, src)) {
			op_erro(op, "Não dá para colocar uma pasta dentro dela mesma (%s)", base);
		} else if (mover && mesma_pasta) {
			op_item_feito(op); /* já está aqui */
		} else {
			bool sub = false, pula = false;
			char *alvo = dst;
			struct stat dt;
			if (!mover && mesma_pasta) { /* colar no mesmo lugar: uma cópia com (2) */
				char *livre = ar_nome_livre(op->destino, base);
				alvo = g_build_filename(op->destino, livre, NULL);
				g_free(livre);
			} else if (lstat(dst, &dt) == 0) {
				if (src_dir && S_ISDIR(dt.st_mode)) {
					sub = false; /* pasta com pasta: junta */
				} else {
					enum ar_conflito r = perguntar(op, base, S_ISDIR(dt.st_mode));
					if (r == AR_C_PULAR) {
						pula = true;
					} else if (r == AR_C_MANTER_AMBOS) {
						char *livre = ar_nome_livre(op->destino, base);
						alvo = g_build_filename(op->destino, livre, NULL);
						g_free(livre);
					} else {
						sub = true;
					}
				}
			}
			if (pula) {
				op_item_feito(op);
			} else if (!mover) {
				bool eh_nova = lstat(alvo, &dt) != 0;
				if (copiar_arvore(op, src, alvo, sub) && eh_nova) {
					registrar_criado(op, alvo);
				} else {
					op_criou(op, alvo);
				}
			} else {
				bool existia = lstat(alvo, &dt) == 0;
				if (sub) {
					char *n2 = NULL, *e2 = NULL;
					lixeira_enviar(alvo, &n2, &e2); /* o que vai ser substituído vai para a lixeira */
					g_free(n2);
					g_free(e2);
					existia = false;
				}
				op_atual(op, base);
				if (!existia && rename(src, alvo) == 0) {
					g_ptr_array_add(op->pares_a, g_strdup(src));
					g_ptr_array_add(op->pares_b, g_strdup(alvo));
					op_criou(op, alvo);
					int64_t b = 0;
					int it = 0;
					contar(alvo, &b, &it);
					g_mutex_lock(&op->mx);
					op->est.feito += b;
					g_mutex_unlock(&op->mx);
					op_item_feito(op);
				} else if (!existia && errno != EXDEV) {
					op_erro(op, "%s: %s", base, g_strerror(errno));
				} else {
					/* outro disco (ou pasta que se junta a outra): copia e, se deu tudo certo, apaga a origem */
					int erros_antes = op->est.erros;
					bool ok = copiar_arvore(op, src, alvo, false);
					if (ok && op->est.erros == erros_antes && !op_cancelada(op)) {
						remover_arvore(src);
						op_criou(op, alvo);
					}
				}
			}
			if (alvo != dst) {
				g_free(alvo);
			}
		}
		g_free(dst);
		g_free(base);
		g_free(pai);
	}
}

static char *nome_sem_extensao(const char *nome) {
	static const char *const exts[] = { ".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst", ".tgz", ".zip", ".tar", ".7z", ".rar", ".gz", ".xz", ".bz2", ".zst", NULL };
	for (int i = 0; exts[i]; i++) {
		if (g_str_has_suffix(nome, exts[i]) && strlen(nome) > strlen(exts[i])) {
			return g_strndup(nome, strlen(nome) - strlen(exts[i]));
		}
	}
	return g_strdup(nome);
}

static bool rodar_programa(char **argv, char **erro) {
	gint status = 0;
	char *saida = NULL, *err = NULL;
	GError *e = NULL;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &saida, &err, &status, &e)) {
		*erro = g_strdup(e && e->message ? e->message : "não consegui rodar o programa");
		g_clear_error(&e);
		return false;
	}
	bool ok = g_spawn_check_wait_status(status, NULL);
	if (!ok) {
		char *quebra = err ? strchr(err, '\n') : NULL;
		if (quebra) {
			*quebra = '\0';
		}
		*erro = g_strdup(err && *err ? err : "o programa falhou");
	}
	g_free(saida);
	g_free(err);
	return ok;
}

static void rodar_compactar(ar_op *op) {
	if (!op->origens->len) {
		return;
	}
	const char *primeiro = op->origens->pdata[0];
	char *pasta = g_path_get_dirname(primeiro), *base = g_path_get_basename(primeiro);
	char *sem = op->origens->len == 1 ? nome_sem_extensao(base) : g_strdup("Arquivos");
	/* um arquivo "a.txt" compactado vira "a.zip" */
	char *sem2 = sem;
	if (op->origens->len == 1 && strrchr(sem, '.') && strrchr(sem, '.') != sem) {
		sem2 = g_strndup(sem, (gsize)(strrchr(sem, '.') - sem));
	}
	char *zip = g_strconcat(sem2, ".zip", NULL);
	char *livre = ar_nome_livre(op->destino, zip);
	char *saida = g_build_filename(op->destino, livre, NULL);
	op_atual(op, livre);
	GPtrArray *args = g_ptr_array_new();
	g_ptr_array_add(args, "bsdtar");
	g_ptr_array_add(args, "-a");
	g_ptr_array_add(args, "-cf");
	g_ptr_array_add(args, saida);
	g_ptr_array_add(args, "-C");
	g_ptr_array_add(args, pasta);
	for (guint i = 0; i < op->origens->len; i++) {
		char *b = g_path_get_basename(op->origens->pdata[i]);
		g_ptr_array_add(args, b);
	}
	g_ptr_array_add(args, NULL);
	char *erro = NULL;
	if (rodar_programa((char **)args->pdata, &erro)) {
		registrar_criado(op, saida);
	} else {
		op_erro(op, "Não consegui compactar: %s", erro ? erro : "?");
		g_unlink(saida);
	}
	for (guint i = 6; i + 1 < args->len; i++) {
		g_free(args->pdata[i]);
	}
	g_ptr_array_free(args, TRUE);
	g_free(erro);
	g_free(saida);
	g_free(livre);
	g_free(zip);
	if (sem2 != sem) {
		g_free(sem2);
	}
	g_free(sem);
	g_free(base);
	g_free(pasta);
	op_item_feito(op);
}

static void rodar_extrair(ar_op *op) {
	for (guint i = 0; i < op->origens->len && !op_cancelada(op); i++) {
		const char *arq = op->origens->pdata[i];
		char *base = g_path_get_basename(arq), *sem = nome_sem_extensao(base);
		op_atual(op, base);
		/* extrai para uma pasta de trabalho: se o pacote já vem dentro de uma pasta só, é ela que fica (sem "Projetos/Projetos") */
		char *molde = g_build_filename(op->destino, ".extraindo-XXXXXX", NULL);
		char *tmp = g_mkdtemp(molde);
		char *erro = NULL;
		if (!tmp) {
			op_erro(op, "Não consegui extrair aqui: %s", g_strerror(errno));
		} else {
			char *argv[] = { "bsdtar", "-xf", (char *)arq, "-C", tmp, NULL };
			if (!rodar_programa(argv, &erro)) {
				op_erro(op, "Não consegui extrair %s: %s", base, erro ? erro : "?");
				remover_arvore(tmp);
			} else {
				GDir *d = g_dir_open(tmp, 0, NULL);
				const char *unico = NULL, *outro = NULL;
				int n = 0;
				char *primeiro = NULL;
				while (d && (outro = g_dir_read_name(d))) {
					if (!n++) {
						primeiro = g_strdup(outro);
					}
				}
				if (d) {
					g_dir_close(d);
				}
				unico = primeiro;
				char *conteudo_unico = unico && n == 1 ? g_build_filename(tmp, unico, NULL) : NULL;
				bool so_uma_pasta = conteudo_unico && g_file_test(conteudo_unico, G_FILE_TEST_IS_DIR) && !g_file_test(conteudo_unico, G_FILE_TEST_IS_SYMLINK);
				char *nome_final = ar_nome_livre(op->destino, so_uma_pasta ? unico : sem);
				char *final = g_build_filename(op->destino, nome_final, NULL);
				bool ok = so_uma_pasta ? rename(conteudo_unico, final) == 0 : rename(tmp, final) == 0;
				if (ok) {
					if (so_uma_pasta) {
						rmdir(tmp);
					}
					registrar_criado(op, final);
				} else {
					op_erro(op, "Não consegui colocar o resultado em %s: %s", nome_final, g_strerror(errno));
					remover_arvore(tmp);
				}
				g_free(final);
				g_free(nome_final);
				g_free(conteudo_unico);
				g_free(primeiro);
			}
		}
		g_free(erro);
		g_free(molde);
		g_free(sem);
		g_free(base);
		op_item_feito(op);
	}
}

static gpointer op_thread(gpointer p) {
	ar_op *op = p;
	int64_t bytes = 0;
	int itens = 0;
	if (op->tipo == AR_OP_COPIAR || op->tipo == AR_OP_MOVER) {
		for (guint i = 0; i < op->origens->len; i++) {
			contar(op->origens->pdata[i], &bytes, &itens);
		}
	} else {
		itens = (int)op->origens->len;
	}
	g_mutex_lock(&op->mx);
	op->est.total = bytes;
	op->est.itens_total = itens;
	g_mutex_unlock(&op->mx);
	switch (op->tipo) {
	case AR_OP_COPIAR: rodar_copiar_mover(op, false); break;
	case AR_OP_MOVER: rodar_copiar_mover(op, true); break;
	case AR_OP_LIXEIRA:
		for (guint i = 0; i < op->origens->len && !op_cancelada(op); i++) {
			char *nome = NULL, *erro = NULL;
			const char *src = op->origens->pdata[i];
			char *base = g_path_get_basename(src);
			op_atual(op, base);
			if (!lixeira_enviar(src, &nome, &erro)) {
				op_erro(op, "%s: %s", base, erro ? erro : "?");
			} else if (nome) {
				g_ptr_array_add(op->pares_a, g_strdup(src));
				g_ptr_array_add(op->pares_b, nome);
			}
			g_free(erro);
			g_free(base);
			op_item_feito(op);
		}
		break;
	case AR_OP_APAGAR:
		for (guint i = 0; i < op->origens->len && !op_cancelada(op); i++) {
			char *base = g_path_get_basename(op->origens->pdata[i]);
			op_atual(op, base);
			if (!remover_arvore(op->origens->pdata[i])) {
				op_erro(op, "Não consegui apagar %s", base);
			}
			g_free(base);
			op_item_feito(op);
		}
		break;
	case AR_OP_COMPACTAR: rodar_compactar(op); break;
	case AR_OP_EXTRAIR: rodar_extrair(op); break;
	case AR_OP_RESTAURAR:
		for (guint i = 0; i < op->origens->len && !op_cancelada(op); i++) {
			char *destino = NULL, *erro = NULL;
			if (!ar_lixeira_restaurar(op->origens->pdata[i], &destino, &erro)) {
				op_erro(op, "%s: %s", (char *)op->origens->pdata[i], erro ? erro : "?");
			} else {
				op_criou(op, destino);
			}
			g_free(destino);
			g_free(erro);
			op_item_feito(op);
		}
		break;
	}
	/* o que desfaz esta operação */
	char desc[96];
	int n = (int)op->pares_a->len;
	const char *quando = n == 1 ? "" : "s";
	if (op->tipo == AR_OP_MOVER) {
		g_snprintf(desc, sizeof(desc), "Mover %d item%s", n, n == 1 ? "" : "s");
		undo_empilhar(DESF_MOVER, desc, op->pares_a, op->pares_b);
	} else if (op->tipo == AR_OP_LIXEIRA) {
		g_snprintf(desc, sizeof(desc), "Mover para a lixeira (%d item%s)", n, quando);
		undo_empilhar(DESF_LIXEIRA, desc, op->pares_a, op->pares_b);
	} else if (op->tipo == AR_OP_COPIAR || op->tipo == AR_OP_COMPACTAR || op->tipo == AR_OP_EXTRAIR) {
		g_snprintf(desc, sizeof(desc), "%s (%d item%s)", op->tipo == AR_OP_COPIAR ? "Copiar" : op->tipo == AR_OP_COMPACTAR ? "Compactar" : "Extrair", n, quando);
		undo_empilhar(DESF_CRIADOS, desc, op->pares_a, op->pares_b);
	} else {
		g_ptr_array_free(op->pares_a, TRUE);
		g_ptr_array_free(op->pares_b, TRUE);
	}
	op->pares_a = op->pares_b = NULL;
	g_mutex_lock(&op->mx);
	op->est.terminou = true;
	g_mutex_unlock(&op->mx);
	op_acordar(op);
	return NULL;
}

ar_op *ar_op_iniciar(enum ar_op_tipo tipo, char *const *origens, int n, const char *destino, void (*acordar)(void *), void *dados) {
	if (n <= 0) {
		return NULL;
	}
	ar_op *op = g_new0(ar_op, 1);
	op->tipo = tipo;
	op->origens = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; i < n; i++) {
		g_ptr_array_add(op->origens, g_strdup(origens[i]));
	}
	op->destino = g_strdup(destino ? destino : "");
	op->acordar = acordar;
	op->dados = dados;
	op->canc = g_cancellable_new();
	op->pares_a = g_ptr_array_new_with_free_func(g_free);
	op->pares_b = g_ptr_array_new_with_free_func(g_free);
	g_mutex_init(&op->mx);
	g_cond_init(&op->cond);
	static const char *const verbos[] = { "Copiando", "Movendo", "Mandando para a lixeira", "Apagando", "Compactando", "Extraindo", "Restaurando" };
	g_snprintf(op->est.descricao, sizeof(op->est.descricao), "%s %d %s", verbos[tipo], n, n == 1 ? "item" : "itens");
	op->thread = g_thread_new("arquivos-op", op_thread, op);
	return op;
}

void ar_op_estado_ler(ar_op *op, ar_op_estado *saida) {
	g_mutex_lock(&op->mx);
	*saida = op->est;
	g_mutex_unlock(&op->mx);
}

void ar_op_liberar(ar_op *op) {
	if (!op) {
		return;
	}
	g_thread_join(op->thread);
	g_ptr_array_free(op->origens, TRUE);
	g_free(op->destino);
	g_object_unref(op->canc);
	g_mutex_clear(&op->mx);
	g_cond_clear(&op->cond);
	g_free(op);
}

/* ---- a área de transferência de arquivos ---- */

static char *arq_area(void) {
	const char *rt = g_get_user_runtime_dir();
	return g_build_filename(rt && *rt ? rt : g_get_tmp_dir(), "lumna-arquivos-area", NULL);
}

char *ar_area_gravar(bool recortar, char *const *caminhos, int n) {
	GString *texto = g_string_new(NULL);
	for (int i = 0; i < n; i++) {
		g_string_append_printf(texto, "%s%s", i ? "\n" : "", caminhos[i]);
	}
	char *arq = arq_area();
	char *conteudo = g_strdup_printf("%s\n%s\n", recortar ? "recortar" : "copiar", texto->str);
	g_file_set_contents(arq, conteudo, -1, NULL);
	g_free(conteudo);
	g_free(arq);
	return g_string_free(texto, FALSE);
}

char **ar_area_ler(const char *texto, bool *recortar) {
	*recortar = false;
	if (!texto || !*texto) {
		return NULL;
	}
	char *arq = arq_area(), *conteudo = NULL;
	char **resultado = NULL;
	if (g_file_get_contents(arq, &conteudo, NULL, NULL)) {
		char *quebra = strchr(conteudo, '\n');
		if (quebra) {
			*quebra = '\0';
			char *lista = quebra + 1;
			size_t n = strlen(lista);
			while (n && lista[n - 1] == '\n') {
				lista[--n] = '\0';
			}
			if (!strcmp(lista, texto)) { /* a área de transferência ainda é a nossa */
				*recortar = !strcmp(conteudo, "recortar");
				resultado = g_strsplit(lista, "\n", -1);
			}
		}
	}
	g_free(conteudo);
	g_free(arq);
	if (resultado) {
		return resultado;
	}
	/* caminhos soltos copiados como texto (de outro app): valem como copiar, se existem */
	char **linhas = g_strsplit(texto, "\n", -1);
	bool todos = linhas[0] != NULL;
	for (int i = 0; linhas[i]; i++) {
		if (linhas[i][0] == '\0' && !linhas[i + 1]) {
			continue;
		}
		if (linhas[i][0] != '/' || (!g_file_test(linhas[i], G_FILE_TEST_EXISTS) && !g_file_test(linhas[i], G_FILE_TEST_IS_SYMLINK))) {
			todos = false;
		}
	}
	if (!todos) {
		g_strfreev(linhas);
		return NULL;
	}
	return linhas;
}
