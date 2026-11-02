#define _DEFAULT_SOURCE /* realpath (o Makefile compila com -std=c11 -D_POSIX_C_SOURCE=200809L) */
#include "emmaq.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LER_MAX (64 * 1024)
#define SAIDA_MAX (12 * 1024)
#define LISTA_MAX 200

static const char *casa(void) {
	const char *h = g_get_home_dir();
	return h && *h ? h : "/";
}

char *emmaq_pasta_dela(void) {
	return g_build_filename(casa(), "Emma", NULL);
}

void emmaq_preparar(void) {
	char *dela = emmaq_pasta_dela();
	const char *subs[] = { "Histórias", "Scripts" };
	for (guint i = 0; i < G_N_ELEMENTS(subs); i++) {
		char *p = g_build_filename(dela, subs[i], NULL);
		g_mkdir_with_parents(p, 0755);
		g_free(p);
	}
	g_free(dela);
}

/* tira os ".", ".." e as barras repetidas, sem tocar no disco */
static char *limpar(const char *absoluto) {
	char **partes = g_strsplit(absoluto, "/", -1);
	GPtrArray *pilha = g_ptr_array_new();
	for (int i = 0; partes[i]; i++) {
		if (!*partes[i] || !strcmp(partes[i], ".")) {
			continue;
		}
		if (!strcmp(partes[i], "..")) {
			if (pilha->len) {
				g_ptr_array_remove_index(pilha, pilha->len - 1);
			}
			continue;
		}
		g_ptr_array_add(pilha, partes[i]);
	}
	GString *g = g_string_new(NULL);
	for (guint i = 0; i < pilha->len; i++) {
		g_string_append_c(g, '/');
		g_string_append(g, g_ptr_array_index(pilha, i));
	}
	if (!g->len) {
		g_string_append_c(g, '/');
	}
	g_ptr_array_free(pilha, TRUE);
	g_strfreev(partes);
	return g_string_free(g, FALSE);
}

/* o caminho absoluto e limpo; os links são seguidos até onde o caminho existe (o que ainda não existe vem do último que existe) */
static char *resolver_n(const char *entrada, int fundo);
static char *resolver(const char *entrada) {
	return resolver_n(entrada, 0);
}

static char *resolver_n(const char *entrada, int fundo) {
	char *base;
	if (!entrada || !*entrada) {
		base = emmaq_pasta_dela();
	} else if (entrada[0] == '~' && (!entrada[1] || entrada[1] == '/')) {
		base = g_build_filename(casa(), entrada + (entrada[1] ? 2 : 1), NULL);
	} else if (entrada[0] == '/') {
		base = g_strdup(entrada);
	} else {
		char *dela = emmaq_pasta_dela();
		base = g_build_filename(dela, entrada, NULL);
		g_free(dela);
	}
	char *lim = limpar(base);
	g_free(base);
	/* o trecho que existe passa por realpath (um link para fora não escapa da política) */
	char *existente = g_strdup(lim), *resto = g_strdup("");
	while (strcmp(existente, "/") && !g_file_test(existente, G_FILE_TEST_EXISTS) && !g_file_test(existente, G_FILE_TEST_IS_SYMLINK)) {
		char *sep = strrchr(existente, '/');
		char *novo_resto = g_strconcat(sep, resto, NULL);
		g_free(resto);
		resto = novo_resto;
		if (sep == existente) {
			existente[1] = '\0';
		} else {
			*sep = '\0';
		}
	}
	char *real = realpath(existente, NULL);
	char *r;
	if (!real && fundo < 8 && g_file_test(existente, G_FILE_TEST_IS_SYMLINK)) {
		/* um link que aponta para algo que não existe: o destino dele é que conta (uma chave que ainda não existe, por exemplo) */
		char *alvo = g_file_read_link(existente, NULL);
		if (alvo) {
			char *pai_do_link = g_path_get_dirname(existente);
			char *abs = g_path_is_absolute(alvo) ? g_strdup(alvo) : g_build_filename(pai_do_link, alvo, NULL);
			g_free(pai_do_link);
			char *com_resto = g_strconcat(abs, resto, NULL);
			r = resolver_n(com_resto, fundo + 1);
			g_free(com_resto);
			g_free(abs);
			g_free(alvo);
			g_free(existente);
			g_free(resto);
			g_free(lim);
			return r;
		}
	}
	if (real) {
		r = g_strconcat(strcmp(real, "/") ? real : "", resto, NULL);
		if (!*r) {
			g_free(r);
			r = g_strdup("/");
		}
		free(real);
	} else {
		r = g_strconcat(existente, resto, NULL);
	}
	g_free(existente);
	g_free(resto);
	g_free(lim);
	return r;
}

static bool dentro(const char *caminho, const char *pasta) {
	size_t n = strlen(pasta);
	return !strncmp(caminho, pasta, n) && (caminho[n] == '\0' || caminho[n] == '/' || !strcmp(pasta, "/"));
}

/* o que ela nunca toca */
static const char *segredo(const char *real) {
	char *h = g_strdup(casa());
	const char *r = NULL;
	char *p;
	struct { const char *rel; const char *razao; } proibidos[] = {
		{ ".config/lumna/emma", "são as chaves e as configurações privadas da própria Emma" },
		{ ".local/share/lumna/emma-servidor", "é o banco do servidor da Emma, com as chaves dos provedores" },
		{ ".ssh", "são as suas chaves de acesso (SSH)" },
		{ ".gnupg", "são as suas chaves de criptografia (GPG)" },
		{ ".local/share/keyrings", "é o cofre de senhas do sistema" },
		{ ".password-store", "é o seu cofre de senhas" },
		{ ".aws", "são credenciais de nuvem" },
	};
	for (guint i = 0; i < G_N_ELEMENTS(proibidos) && !r; i++) {
		p = g_build_filename(h, proibidos[i].rel, NULL);
		if (dentro(real, p)) {
			r = proibidos[i].razao;
		}
		g_free(p);
	}
	g_free(h);
	if (!r && (g_str_has_suffix(real, "/shadow") || !strcmp(real, "/etc/sudoers") || g_str_has_prefix(real, "/etc/sudoers.d"))) {
		r = "são arquivos de senhas e permissões do sistema";
	}
	return r;
}

static bool tem_oculto(const char *real) {
	const char *h = casa();
	if (!dentro(real, h)) {
		return false;
	}
	const char *resto = real + strlen(h);
	char **ps = g_strsplit(resto, "/", -1);
	bool oculto = false;
	for (int i = 0; ps[i]; i++) {
		if (ps[i][0] == '.' && ps[i][1]) {
			oculto = true;
		}
	}
	g_strfreev(ps);
	return oculto;
}

struct emmaq_juizo emmaq_julgar(enum emmaq_op op, const char *caminho) {
	struct emmaq_juizo j = { .veredito = EMMAQ_LIVRE };
	j.real = resolver(caminho);
	const char *seg = segredo(j.real);
	if (seg) {
		j.veredito = EMMAQ_NEGAR;
		j.porque = g_strdup_printf("A Emma não mexe em %s: %s.", j.real, seg);
		return j;
	}
	char *dela = emmaq_pasta_dela();
	bool na_dela = dentro(j.real, dela);
	g_free(dela);
	bool na_casa = dentro(j.real, casa());
	bool existe = g_file_test(j.real, G_FILE_TEST_EXISTS);
	if (op == EMMAQ_EXECUTAR) {
		j.veredito = EMMAQ_APROVAR;
		j.porque = g_strdup("Todo comando passa pela sua aprovação.");
	} else if (op == EMMAQ_APAGAR) {
		j.veredito = EMMAQ_APROVAR;
		j.porque = g_strdup("Apagar sempre pede a sua aprovação (o arquivo vai para a lixeira, não some).");
	} else if (!na_casa) {
		j.veredito = EMMAQ_APROVAR;
		j.porque = g_strdup_printf("%s fica fora da sua pasta pessoal.", j.real);
	} else if (tem_oculto(j.real)) {
		j.veredito = EMMAQ_APROVAR;
		j.porque = g_strdup_printf("%s é uma pasta ou arquivo oculto, de configuração de programas.", j.real);
	} else if (op == EMMAQ_MOVER && !na_dela) {
		j.veredito = EMMAQ_APROVAR;
		j.porque = g_strdup("Mover, copiar ou renomear arquivos seus pede a sua aprovação (com a lista inteira).");
	} else if ((op == EMMAQ_ESCREVER || op == EMMAQ_ANEXAR) && existe && !na_dela) {
		j.veredito = EMMAQ_APROVAR;
		j.porque = g_strdup_printf("%s já existe e não é um arquivo da Emma: mudar ele altera algo seu.", j.real);
	}
	return j;
}

void emmaq_juizo_limpar(struct emmaq_juizo *j) {
	g_free(j->real);
	g_free(j->porque);
	j->real = j->porque = NULL;
}

/* ---- as execuções ---- */

static char *erro_de(const char *fmt, const char *a, const char *b) {
	return g_strdup_printf(fmt, a, b ? b : "");
}

char *emmaq_listar(const char *real, char **erro) {
	GError *e = NULL;
	GDir *d = g_dir_open(real, 0, &e);
	if (!d) {
		*erro = g_error_matches(e, G_FILE_ERROR, G_FILE_ERROR_NOENT) ? erro_de("A pasta %s não existe.", real, NULL)
			: g_error_matches(e, G_FILE_ERROR, G_FILE_ERROR_ACCES) ? erro_de("O sistema não deixa abrir a pasta %s (sem permissão).", real, NULL)
			: erro_de("Não consegui abrir a pasta %s.", real, NULL);
		g_clear_error(&e);
		return NULL;
	}
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	const char *n;
	while ((n = g_dir_read_name(d))) {
		g_ptr_array_add(nomes, g_strdup(n));
	}
	g_dir_close(d);
	g_ptr_array_sort(nomes, (GCompareFunc)g_strcmp0);
	GString *g = g_string_new(NULL);
	g_string_append_printf(g, "%s (%u itens)\n", real, nomes->len);
	for (guint i = 0; i < nomes->len && i < LISTA_MAX; i++) {
		char *caminho = g_build_filename(real, g_ptr_array_index(nomes, i), NULL);
		GStatBuf st;
		if (g_lstat(caminho, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				g_string_append_printf(g, "%s/\n", (char *)g_ptr_array_index(nomes, i));
			} else {
				g_string_append_printf(g, "%s (%ld bytes)\n", (char *)g_ptr_array_index(nomes, i), (long)st.st_size);
			}
		}
		g_free(caminho);
	}
	if (nomes->len > LISTA_MAX) {
		g_string_append_printf(g, "… e mais %u.\n", nomes->len - LISTA_MAX);
	}
	g_ptr_array_free(nomes, TRUE);
	return g_string_free(g, FALSE);
}

char *emmaq_ler(const char *real, char **erro) {
	if (!g_file_test(real, G_FILE_TEST_EXISTS)) {
		*erro = erro_de("%s não existe.", real, NULL);
		return NULL;
	}
	if (!g_file_test(real, G_FILE_TEST_IS_REGULAR)) {
		*erro = erro_de("%s não é um arquivo (é uma pasta ou algo especial).", real, NULL);
		return NULL;
	}
	FILE *f = fopen(real, "rb");
	if (!f) {
		*erro = errno == EACCES ? erro_de("O sistema não deixa ler %s (sem permissão).", real, NULL) : erro_de("Não consegui abrir %s: %s.", real, g_strerror(errno));
		return NULL;
	}
	char *buf = g_malloc(LER_MAX + 1);
	size_t n = fread(buf, 1, LER_MAX, f);
	bool mais = !feof(f) && fgetc(f) != EOF;
	fclose(f);
	buf[n] = '\0';
	if (memchr(buf, '\0', n) || !g_utf8_validate(buf, (gssize)n, NULL)) {
		/* um corte no meio de um caractere não é binário: aceita se o resto é UTF-8 */
		const char *fim;
		bool ok = g_utf8_validate(buf, (gssize)n, &fim) || (n - (size_t)(fim - buf) < 4 && !memchr(buf, '\0', n));
		if (!ok) {
			g_free(buf);
			*erro = erro_de("%s não parece um arquivo de texto.", real, NULL);
			return NULL;
		}
		buf[fim - buf] = '\0';
	}
	if (mais) {
		char *r = g_strdup_printf("%s\n[…o arquivo continua; só os primeiros %d KB foram lidos]", buf, LER_MAX / 1024);
		g_free(buf);
		return r;
	}
	return buf;
}

/* a cópia de um arquivo que vai ser sobrescrito: ~/Emma/.copias/ANO-MES-DIA_HORA_nome */
static void copiar_antes(const char *real) {
	if (!g_file_test(real, G_FILE_TEST_IS_REGULAR)) {
		return;
	}
	char *dela = emmaq_pasta_dela(), *pasta = g_build_filename(dela, ".copias", NULL);
	g_mkdir_with_parents(pasta, 0700);
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	char quando[32];
	strftime(quando, sizeof(quando), "%Y-%m-%d_%H%M%S", &tm);
	char *base = g_path_get_basename(real), *nome = g_strdup_printf("%s_%06d_%s", quando, (int)(g_get_real_time() % 1000000), base),
		*destino = g_build_filename(pasta, nome, NULL);
	GFile *a = g_file_new_for_path(real), *b = g_file_new_for_path(destino);
	g_file_copy(a, b, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
	g_object_unref(a);
	g_object_unref(b);
	g_free(destino);
	g_free(nome);
	g_free(base);
	g_free(pasta);
	g_free(dela);
}

char *emmaq_escrever(const char *real, const char *conteudo, bool anexar, char **erro) {
	if (g_file_test(real, G_FILE_TEST_IS_DIR)) {
		*erro = erro_de("%s é uma pasta.", real, NULL);
		return NULL;
	}
	char *pai = g_path_get_dirname(real);
	if (g_mkdir_with_parents(pai, 0755) != 0) {
		*erro = erro_de("Não consegui criar a pasta %s.", pai, NULL);
		g_free(pai);
		return NULL;
	}
	g_free(pai);
	bool existia = g_file_test(real, G_FILE_TEST_IS_REGULAR);
	copiar_antes(real);
	char *final = NULL;
	if (anexar && existia) {
		char *atual = NULL;
		gsize n = 0;
		if (!g_file_get_contents(real, &atual, &n, NULL)) {
			*erro = erro_de("Não consegui ler %s para acrescentar.", real, NULL);
			return NULL;
		}
		final = g_strconcat(atual, conteudo, NULL);
		g_free(atual);
	} else {
		final = g_strdup(conteudo);
	}
	char *tmp = g_strconcat(real, ".emma-tmp", NULL);
	GStatBuf st;
	bool tem_modo = existia && g_stat(real, &st) == 0;
	GError *e = NULL;
	bool ok = g_file_set_contents(tmp, final, -1, &e);
	if (ok) {
		if (tem_modo) {
			g_chmod(tmp, st.st_mode & 0777);
		}
		ok = g_rename(tmp, real) == 0;
	}
	char *r = NULL;
	if (ok) {
		r = g_strdup_printf("%s %s (%d bytes).", anexar && existia ? "Acrescentei a" : existia ? "Substituí" : "Criei", real, (int)strlen(final));
	} else {
		*erro = erro_de("Não consegui escrever em %s: %s.", real, e ? e->message : g_strerror(errno));
		g_unlink(tmp);
	}
	g_clear_error(&e);
	g_free(tmp);
	g_free(final);
	return r;
}

char *emmaq_apagar(const char *real, char **erro) {
	if (!g_file_test(real, G_FILE_TEST_EXISTS) && !g_file_test(real, G_FILE_TEST_IS_SYMLINK)) {
		*erro = erro_de("%s não existe.", real, NULL);
		return NULL;
	}
	GFile *f = g_file_new_for_path(real);
	GError *e = NULL;
	bool ok = g_file_trash(f, NULL, &e);
	g_object_unref(f);
	if (!ok) {
		*erro = erro_de("Não consegui mandar %s para a lixeira: %s.", real, e ? e->message : "");
		g_clear_error(&e);
		return NULL;
	}
	return g_strdup_printf("%s foi para a lixeira.", real);
}

char *emmaq_executar(const char *comando, const char *pasta, int timeout, bool *ok, char **erro) {
	char *dela = emmaq_pasta_dela();
	char *cwd = pasta && *pasta ? resolver(pasta) : g_strdup(dela);
	g_mkdir_with_parents(dela, 0755);
	g_free(dela);
	if (!g_file_test(cwd, G_FILE_TEST_IS_DIR)) {
		*erro = erro_de("A pasta %s não existe.", cwd, NULL);
		g_free(cwd);
		return NULL;
	}
	GSubprocessLauncher *l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
	g_subprocess_launcher_set_cwd(l, cwd);
	GError *e = NULL;
	if (timeout <= 0) {
		timeout = 60;
	}
	/* o prazo é do "timeout" (coreutils): para o comando e o que ele abriu, sem depender de um laço principal */
	char *tmo = g_find_program_in_path("timeout"), *segundos = g_strdup_printf("%d", timeout);
	GSubprocess *p = tmo ? g_subprocess_launcher_spawn(l, &e, tmo, "-k", "2", segundos, "/bin/sh", "-c", comando, NULL)
	                     : g_subprocess_launcher_spawn(l, &e, "/bin/sh", "-c", comando, NULL);
	g_free(tmo);
	g_free(segundos);
	g_object_unref(l);
	g_free(cwd);
	if (!p) {
		*erro = erro_de("Não consegui executar: %s.", e ? e->message : "", NULL);
		g_clear_error(&e);
		return NULL;
	}
	GBytes *bytes = NULL;
	g_subprocess_communicate(p, NULL, NULL, &bytes, NULL, NULL);
	gsize n = 0;
	const char *dados = bytes ? g_bytes_get_data(bytes, &n) : "";
	GString *g = g_string_new(NULL);
	char *validos = g_utf8_make_valid(dados ? dados : "", (gssize)(n > SAIDA_MAX ? SAIDA_MAX : n));
	g_string_append(g, validos);
	g_free(validos);
	if (n > SAIDA_MAX) {
		g_string_append_printf(g, "\n[…a saída continuou: só os primeiros %d KB]", SAIDA_MAX / 1024);
	}
	int status = g_subprocess_get_if_exited(p) ? g_subprocess_get_exit_status(p) : -1;
	bool estourou = status == 124 || status == 137 || (!g_subprocess_get_if_exited(p) && g_subprocess_get_term_sig(p) == 9);
	*ok = !estourou && status == 0;
	if (estourou) {
		g_string_append_printf(g, "\n[parei o comando: passou de %d segundos]", timeout);
	} else {
		g_string_append_printf(g, "\n[terminou com código %d]", status);
	}
	if (bytes) {
		g_bytes_unref(bytes);
	}
	g_object_unref(p);
	return g_string_free(g, FALSE);
}

char *emmaq_detalhe_comando(const char *comando, const char *pasta) {
	GString *g = g_string_new(NULL);
	char *dela = emmaq_pasta_dela();
	g_string_append_printf(g, "$ %s\n", comando);
	g_string_append_printf(g, "(na pasta %s)", pasta && *pasta ? pasta : dela);
	g_free(dela);
	/* os arquivos pequenos de texto que o comando chama (um script que ela escreveu): o conteúdo vai junto, para ver o código */
	char **toks = g_strsplit_set(comando, " \t;&|<>()\"'", -1);
	GHashTable *vistos = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	int mostrados = 0;
	for (int i = 0; toks[i] && mostrados < 3; i++) {
		if (!*toks[i] || toks[i][0] == '-') {
			continue;
		}
		char *real = resolver(toks[i]);
		if (!g_hash_table_contains(vistos, real) && g_file_test(real, G_FILE_TEST_IS_REGULAR) && !segredo(real)) {
			GStatBuf st;
			if (g_stat(real, &st) == 0 && st.st_size > 0 && st.st_size <= 8192) {
				char *conteudo = NULL;
				gsize tam = 0;
				if (g_file_get_contents(real, &conteudo, &tam, NULL) && !memchr(conteudo, '\0', tam) && g_utf8_validate(conteudo, (gssize)tam, NULL)) {
					g_string_append_printf(g, "\n\n--- conteúdo de %s ---\n%s", real, conteudo);
					mostrados++;
				}
				g_free(conteudo);
			}
		}
		g_hash_table_add(vistos, real);
	}
	g_hash_table_destroy(vistos);
	g_strfreev(toks);
	return g_string_free(g, FALSE);
}

char *emmaq_criar_pasta(const char *real, char **erro) {
	if (g_file_test(real, G_FILE_TEST_IS_DIR)) {
		return g_strdup_printf("A pasta %s já existia.", real);
	}
	if (g_file_test(real, G_FILE_TEST_EXISTS)) {
		*erro = erro_de("%s já existe e não é uma pasta.", real, NULL);
		return NULL;
	}
	if (g_mkdir_with_parents(real, 0755) != 0) {
		*erro = errno == EACCES ? erro_de("O sistema não deixa criar a pasta %s (sem permissão).", real, NULL) : erro_de("Não consegui criar a pasta %s.", real, NULL);
		return NULL;
	}
	return g_strdup_printf("Criei a pasta %s.", real);
}

char *emmaq_destino(const char *de_real, const char *para_real) {
	if (g_file_test(para_real, G_FILE_TEST_IS_DIR)) {
		char *base = g_path_get_basename(de_real), *r = g_build_filename(para_real, base, NULL);
		g_free(base);
		return r;
	}
	return g_strdup(para_real);
}

char *emmaq_mover(const char *de, const char *para, bool copiar, char **erro) {
	if (!g_file_test(de, G_FILE_TEST_EXISTS) && !g_file_test(de, G_FILE_TEST_IS_SYMLINK)) {
		*erro = erro_de("%s não existe.", de, NULL);
		return NULL;
	}
	if (g_file_test(para, G_FILE_TEST_EXISTS) || g_file_test(para, G_FILE_TEST_IS_SYMLINK)) {
		*erro = erro_de("O destino %s já existe (não sobrescrevo).", para, NULL);
		return NULL;
	}
	if (dentro(para, de)) {
		*erro = erro_de("Não dá para pôr %s dentro de si mesma.", de, NULL);
		return NULL;
	}
	char *pai = g_path_get_dirname(para);
	g_mkdir_with_parents(pai, 0755);
	g_free(pai);
	GFile *a = g_file_new_for_path(de), *b = g_file_new_for_path(para);
	GError *e = NULL;
	bool ok = copiar ? g_file_copy(a, b, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, &e) : g_file_move(a, b, G_FILE_COPY_NOFOLLOW_SYMLINKS, NULL, NULL, NULL, &e);
	g_object_unref(a);
	g_object_unref(b);
	if (!ok) {
		*erro = g_strdup_printf("Não consegui %s %s: %s.", copiar ? "copiar" : "mover", de, e ? e->message : "");
		g_clear_error(&e);
		return NULL;
	}
	return g_strdup_printf("%s %s → %s", copiar ? "Copiei" : "Movi", de, para);
}
