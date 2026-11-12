#define _GNU_SOURCE
#include "sgbuffer.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LIMITE_ARQUIVO (64 * 1024 * 1024)
#define LIMITE_DESFAZER 50000 /* operações guardadas */

struct linha {
	char *s;
	size_t len, cap;
};

struct op {
	bool ins;                      /* inseriu texto (senão: apagou) */
	struct sg_pos pos;             /* onde o texto está/estava */
	char *texto;
	size_t len;
	struct sg_pos cur_desfeito;    /* o cursor depois de desfazer esta operação */
	struct sg_pos cur_refeito;     /* e depois de refazê-la */
	int grupo;
};

struct sg_buffer {
	struct linha *l;
	int n, cap;
	char *caminho;
	enum sg_fim_linha fim;
	enum sg_codificacao cod;
	bool bom, misto;
	struct timespec mtime;
	off_t tamanho_disco;
	bool tem_disco;
	/* desfazer */
	struct op *ops;
	int n_ops, cap_ops;
	int topo;                      /* ops[0..topo) estão aplicadas; o resto é o que dá para refazer */
	int salvo_topo;                /* o topo no último salvamento (-1: não volta mais) */
	int prox_grupo, lote;
	bool grupo_aberto;
	bool mod_extra;                /* mudou algo que não está no desfazer (a codificação, o BOM, o fim de linha) */
};

static mode_t umask_atual(void) {
	mode_t m = umask(0);
	umask(m);
	return m;
}

/* ---- linhas ---- */

static void linha_garantir(struct linha *x, size_t cap) {
	if (cap + 1 > x->cap) {
		size_t n = x->cap ? x->cap : 16;
		while (n < cap + 1) {
			n *= 2;
		}
		x->s = g_realloc(x->s, n);
		x->cap = n;
	}
}

static void linha_definir(struct linha *x, const char *s, size_t len) {
	linha_garantir(x, len);
	if (len) {
		memcpy(x->s, s, len);
	}
	x->len = len;
	x->s[len] = '\0';
}

static void linhas_garantir(sg_buffer *b, int cap) {
	if (cap > b->cap) {
		int n = b->cap ? b->cap : 64;
		while (n < cap) {
			n *= 2;
		}
		b->l = g_realloc(b->l, sizeof(struct linha) * (size_t)n);
		b->cap = n;
	}
}

static void linhas_inserir_vazias(sg_buffer *b, int em, int quantas) {
	linhas_garantir(b, b->n + quantas);
	memmove(&b->l[em + quantas], &b->l[em], sizeof(struct linha) * (size_t)(b->n - em));
	memset(&b->l[em], 0, sizeof(struct linha) * (size_t)quantas);
	b->n += quantas;
}

static void linhas_remover(sg_buffer *b, int de, int quantas) {
	for (int i = de; i < de + quantas; i++) {
		g_free(b->l[i].s);
	}
	memmove(&b->l[de], &b->l[de + quantas], sizeof(struct linha) * (size_t)(b->n - de - quantas));
	b->n -= quantas;
}

static void linhas_limpar(sg_buffer *b) {
	for (int i = 0; i < b->n; i++) {
		g_free(b->l[i].s);
	}
	b->n = 0;
	linhas_inserir_vazias(b, 0, 1);
	linha_definir(&b->l[0], "", 0);
}

/* ---- posições ---- */

int sg_pos_cmp(struct sg_pos a, struct sg_pos b) {
	return a.linha != b.linha ? (a.linha < b.linha ? -1 : 1) : a.col != b.col ? (a.col < b.col ? -1 : 1) : 0;
}

struct sg_pos sg_pos_valida(const sg_buffer *b, struct sg_pos p) {
	if (p.linha < 0) {
		return (struct sg_pos){ 0, 0 };
	}
	if (p.linha >= b->n) {
		return sg_pos_fim(b);
	}
	const struct linha *x = &b->l[p.linha];
	if (p.col < 0) {
		p.col = 0;
	}
	if ((size_t)p.col > x->len) {
		p.col = (int)x->len;
	}
	while (p.col > 0 && ((unsigned char)x->s[p.col] & 0xC0) == 0x80) {
		p.col--;
	}
	return p;
}

struct sg_pos sg_pos_fim(const sg_buffer *b) {
	return (struct sg_pos){ b->n - 1, (int)b->l[b->n - 1].len };
}

struct sg_pos sg_pos_proximo(const sg_buffer *b, struct sg_pos p) {
	p = sg_pos_valida(b, p);
	const struct linha *x = &b->l[p.linha];
	if ((size_t)p.col < x->len) {
		p.col = (int)(g_utf8_next_char(x->s + p.col) - x->s);
		if ((size_t)p.col > x->len) {
			p.col = (int)x->len;
		}
	} else if (p.linha < b->n - 1) {
		p.linha++;
		p.col = 0;
	}
	return p;
}

struct sg_pos sg_pos_anterior(const sg_buffer *b, struct sg_pos p) {
	p = sg_pos_valida(b, p);
	if (p.col > 0) {
		const struct linha *x = &b->l[p.linha];
		p.col = (int)(g_utf8_prev_char(x->s + p.col) - x->s);
	} else if (p.linha > 0) {
		p.linha--;
		p.col = (int)b->l[p.linha].len;
	}
	return p;
}

/* 0 espaço, 1 palavra, 2 pontuação */
static int classe(gunichar c) {
	if (g_unichar_isspace(c)) {
		return 0;
	}
	return g_unichar_isalnum(c) || c == '_' ? 1 : 2;
}

static gunichar char_em(const struct linha *x, int col) {
	return (size_t)col < x->len ? g_utf8_get_char_validated(x->s + col, (gssize)(x->len - (size_t)col)) : 0;
}

struct sg_pos sg_pos_palavra_proxima(const sg_buffer *b, struct sg_pos p) {
	p = sg_pos_valida(b, p);
	const struct linha *x = &b->l[p.linha];
	if ((size_t)p.col >= x->len) {
		return sg_pos_proximo(b, p);
	}
	int c = classe(char_em(x, p.col));
	if (c != 0) {
		while ((size_t)p.col < x->len && classe(char_em(x, p.col)) == c) {
			p.col = (int)(g_utf8_next_char(x->s + p.col) - x->s);
		}
	}
	while ((size_t)p.col < x->len && classe(char_em(x, p.col)) == 0) {
		p.col = (int)(g_utf8_next_char(x->s + p.col) - x->s);
	}
	if ((size_t)p.col > x->len) {
		p.col = (int)x->len;
	}
	return p;
}

struct sg_pos sg_pos_palavra_anterior(const sg_buffer *b, struct sg_pos p) {
	p = sg_pos_valida(b, p);
	if (p.col == 0) {
		return sg_pos_anterior(b, p);
	}
	const struct linha *x = &b->l[p.linha];
	while (p.col > 0 && classe(char_em(x, (int)(g_utf8_prev_char(x->s + p.col) - x->s))) == 0) {
		p.col = (int)(g_utf8_prev_char(x->s + p.col) - x->s);
	}
	if (p.col == 0) {
		return p;
	}
	int c = classe(char_em(x, (int)(g_utf8_prev_char(x->s + p.col) - x->s)));
	while (p.col > 0 && classe(char_em(x, (int)(g_utf8_prev_char(x->s + p.col) - x->s))) == c) {
		p.col = (int)(g_utf8_prev_char(x->s + p.col) - x->s);
	}
	return p;
}

void sg_palavra_em(const sg_buffer *b, struct sg_pos p, struct sg_pos *ini, struct sg_pos *fim) {
	p = sg_pos_valida(b, p);
	const struct linha *x = &b->l[p.linha];
	struct sg_pos a = p, z = p;
	if (x->len > 0) {
		/* sobre o fim da linha, vale o caractere anterior */
		int col = (size_t)p.col >= x->len ? (int)(g_utf8_prev_char(x->s + x->len) - x->s) : p.col;
		int c = classe(char_em(x, col));
		int i = col, j = col;
		while (i > 0 && classe(char_em(x, (int)(g_utf8_prev_char(x->s + i) - x->s))) == c) {
			i = (int)(g_utf8_prev_char(x->s + i) - x->s);
		}
		while ((size_t)j < x->len && classe(char_em(x, j)) == c) {
			j = (int)(g_utf8_next_char(x->s + j) - x->s);
		}
		a.col = i;
		z.col = j > (int)x->len ? (int)x->len : j;
	}
	*ini = a;
	*fim = z;
}

/* ---- leitura ---- */

int sg_linhas(const sg_buffer *b) {
	return b->n;
}

const char *sg_linha(const sg_buffer *b, int i, size_t *len) {
	if (i < 0 || i >= b->n) {
		if (len) {
			*len = 0;
		}
		return "";
	}
	if (len) {
		*len = b->l[i].len;
	}
	return b->l[i].s ? b->l[i].s : "";
}

size_t sg_bytes(const sg_buffer *b) {
	size_t t = 0;
	for (int i = 0; i < b->n; i++) {
		t += b->l[i].len + 1;
	}
	return t ? t - 1 : 0;
}

char *sg_texto(const sg_buffer *b, struct sg_pos a, struct sg_pos z) {
	a = sg_pos_valida(b, a);
	z = sg_pos_valida(b, z);
	if (sg_pos_cmp(a, z) > 0) {
		struct sg_pos t = a;
		a = z;
		z = t;
	}
	GString *s = g_string_new(NULL);
	for (int i = a.linha; i <= z.linha; i++) {
		const struct linha *x = &b->l[i];
		size_t de = i == a.linha ? (size_t)a.col : 0, ate = i == z.linha ? (size_t)z.col : x->len;
		if (ate > de) {
			g_string_append_len(s, x->s + de, (gssize)(ate - de));
		}
		if (i < z.linha) {
			g_string_append_c(s, '\n');
		}
	}
	return g_string_free(s, FALSE);
}

char *sg_texto_todo(const sg_buffer *b) {
	return sg_texto(b, (struct sg_pos){ 0, 0 }, sg_pos_fim(b));
}

/* ---- edição sem desfazer ---- */

static struct sg_pos cru_inserir(sg_buffer *b, struct sg_pos p, const char *t, size_t n) {
	struct linha *x = &b->l[p.linha];
	const char *nl = memchr(t, '\n', n);
	if (!nl) {
		linha_garantir(x, x->len + n);
		memmove(x->s + p.col + n, x->s + p.col, x->len - (size_t)p.col + 1);
		memcpy(x->s + p.col, t, n);
		x->len += n;
		return (struct sg_pos){ p.linha, p.col + (int)n };
	}
	/* o resto da linha depois do ponto vai para o fim da última linha nova */
	size_t cauda_len = x->len - (size_t)p.col;
	char *cauda = g_strndup(x->s + p.col, cauda_len);
	int nlinhas = 1;
	for (size_t i = 0; i < n; i++) {
		if (t[i] == '\n') {
			nlinhas++;
		}
	}
	linhas_inserir_vazias(b, p.linha + 1, nlinhas - 1);
	x = &b->l[p.linha];
	const char *seg = t;
	int li = p.linha;
	size_t ult_len = 0;
	for (int k = 0; k < nlinhas; k++) {
		const char *fim = k < nlinhas - 1 ? memchr(seg, '\n', (size_t)(t + n - seg)) : t + n;
		size_t sl = (size_t)(fim - seg);
		struct linha *y = &b->l[li + k];
		if (k == 0) {
			linha_definir(y, x->s, (size_t)p.col);
			linha_garantir(y, y->len + sl);
			memcpy(y->s + y->len, seg, sl);
			y->len += sl;
			y->s[y->len] = '\0';
		} else {
			linha_definir(y, seg, sl);
		}
		ult_len = sl;
		seg = fim + 1;
	}
	struct linha *ultima = &b->l[li + nlinhas - 1];
	linha_garantir(ultima, ultima->len + cauda_len);
	memcpy(ultima->s + ultima->len, cauda, cauda_len);
	ultima->len += cauda_len;
	ultima->s[ultima->len] = '\0';
	g_free(cauda);
	return (struct sg_pos){ li + nlinhas - 1, (int)ult_len };
}

static void cru_apagar(sg_buffer *b, struct sg_pos a, struct sg_pos z) {
	if (a.linha == z.linha) {
		struct linha *x = &b->l[a.linha];
		memmove(x->s + a.col, x->s + z.col, x->len - (size_t)z.col + 1);
		x->len -= (size_t)(z.col - a.col);
		return;
	}
	struct linha *x = &b->l[a.linha], *y = &b->l[z.linha];
	size_t cauda = y->len - (size_t)z.col;
	linha_garantir(x, (size_t)a.col + cauda);
	memcpy(x->s + a.col, y->s + z.col, cauda);
	x->len = (size_t)a.col + cauda;
	x->s[x->len] = '\0';
	linhas_remover(b, a.linha + 1, z.linha - a.linha);
}

/* ---- desfazer ---- */

static void op_liberar(struct op *o) {
	g_free(o->texto);
	o->texto = NULL;
}

/* joga fora o que dava para refazer (uma edição nova) */
static void cortar_refazer(sg_buffer *b) {
	for (int i = b->topo; i < b->n_ops; i++) {
		op_liberar(&b->ops[i]);
	}
	b->n_ops = b->topo;
	if (b->salvo_topo > b->topo) {
		b->salvo_topo = -1;
	}
}

/* o limite de operações: joga fora o grupo mais antigo */
static void limitar(sg_buffer *b) {
	while (b->n_ops > LIMITE_DESFAZER && b->n_ops > 1) {
		int g = b->ops[0].grupo, k = 0;
		while (k < b->n_ops && b->ops[k].grupo == g) {
			op_liberar(&b->ops[k]);
			k++;
		}
		memmove(&b->ops[0], &b->ops[k], sizeof(struct op) * (size_t)(b->n_ops - k));
		b->n_ops -= k;
		b->topo -= k;
		if (b->topo < 0) {
			b->topo = 0;
		}
		if (b->salvo_topo >= 0) {
			b->salvo_topo -= k;
			if (b->salvo_topo < 0) {
				b->salvo_topo = -1;
			}
		}
	}
}

static int escolher_grupo(sg_buffer *b, bool ins, struct sg_pos a, struct sg_pos z, bool junta, bool simples) {
	if (b->lote) {
		return b->lote;
	}
	if (junta && simples && b->grupo_aberto && b->topo > 0 && b->topo == b->n_ops) {
		const struct op *p = &b->ops[b->topo - 1];
		bool adj = false;
		if (ins && p->ins) {
			/* continua de onde a anterior parou; depois de uma quebra de linha começa outro grupo (a quebra desfaz sozinha) */
			adj = sg_pos_cmp(p->cur_refeito, a) == 0 && !memchr(p->texto, '\n', p->len);
		} else if (!ins && !p->ins) {
			adj = sg_pos_cmp(p->pos, z) == 0 /* Backspace repetido */ || sg_pos_cmp(p->pos, a) == 0 /* Delete repetido */;
		}
		if (adj) {
			return p->grupo;
		}
	}
	b->grupo_aberto = true;
	return ++b->prox_grupo;
}

static void empilhar(sg_buffer *b, struct op o) {
	cortar_refazer(b);
	if (b->n_ops >= b->cap_ops) {
		b->cap_ops = b->cap_ops ? b->cap_ops * 2 : 64;
		b->ops = g_realloc(b->ops, sizeof(struct op) * (size_t)b->cap_ops);
	}
	b->ops[b->n_ops++] = o;
	b->topo = b->n_ops;
	limitar(b);
}

static char *sem_cr(const char *t, size_t n, size_t *novo) {
	char *r = g_malloc(n + 1);
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		if (t[i] != '\r') {
			r[k++] = t[i];
		}
	}
	r[k] = '\0';
	*novo = k;
	return r;
}

static struct sg_pos fazer_inserir(sg_buffer *b, struct sg_pos p, char *t, size_t n, bool junta, bool simples) {
	p = sg_pos_valida(b, p);
	if (n == 0) {
		g_free(t);
		return p;
	}
	int grupo = escolher_grupo(b, true, p, p, junta, simples && !memchr(t, '\n', n));
	struct sg_pos fim = cru_inserir(b, p, t, n);
	struct op o = { .ins = true, .pos = p, .texto = t, .len = n, .cur_desfeito = p, .cur_refeito = fim, .grupo = grupo };
	empilhar(b, o);
	return fim;
}

static struct sg_pos fazer_apagar(sg_buffer *b, struct sg_pos a, struct sg_pos z, bool junta, bool simples) {
	a = sg_pos_valida(b, a);
	z = sg_pos_valida(b, z);
	if (sg_pos_cmp(a, z) > 0) {
		struct sg_pos t = a;
		a = z;
		z = t;
	}
	if (sg_pos_cmp(a, z) == 0) {
		return a;
	}
	char *t = sg_texto(b, a, z);
	size_t n = strlen(t);
	int grupo = escolher_grupo(b, false, a, z, junta, simples && !memchr(t, '\n', n));
	cru_apagar(b, a, z);
	struct op o = { .ins = false, .pos = a, .texto = t, .len = n, .cur_desfeito = z, .cur_refeito = a, .grupo = grupo };
	empilhar(b, o);
	return a;
}

struct sg_pos sg_inserir(sg_buffer *b, struct sg_pos p, const char *texto, size_t len, bool junta) {
	size_t n;
	char *t = sem_cr(texto, len, &n);
	return fazer_inserir(b, p, t, n, junta, true);
}

struct sg_pos sg_apagar(sg_buffer *b, struct sg_pos a, struct sg_pos z, bool junta) {
	return fazer_apagar(b, a, z, junta, true);
}

struct sg_pos sg_trocar(sg_buffer *b, struct sg_pos a, struct sg_pos z, const char *texto, size_t len) {
	bool eu_abri = b->lote == 0;
	if (eu_abri) {
		b->lote = ++b->prox_grupo;
	}
	struct sg_pos ini = fazer_apagar(b, a, z, false, false);
	size_t n;
	char *t = sem_cr(texto, len, &n);
	struct sg_pos fim = fazer_inserir(b, ini, t, n, false, false);
	if (eu_abri) {
		b->lote = 0;
		b->grupo_aberto = false;
	}
	return fim;
}

void sg_fechar_grupo(sg_buffer *b) {
	b->grupo_aberto = false;
}

bool sg_pode_desfazer(const sg_buffer *b) {
	return b->topo > 0;
}

bool sg_pode_refazer(const sg_buffer *b) {
	return b->topo < b->n_ops;
}

static void aplicar_inverso(sg_buffer *b, const struct op *o) {
	if (o->ins) {
		/* desfazer uma inserção: apagar o que ela pôs */
		struct sg_pos fim = o->cur_refeito;
		cru_apagar(b, o->pos, fim);
	} else {
		cru_inserir(b, o->pos, o->texto, o->len);
	}
}

static void aplicar_direto(sg_buffer *b, const struct op *o) {
	if (o->ins) {
		cru_inserir(b, o->pos, o->texto, o->len);
	} else {
		struct sg_pos z = o->cur_desfeito;
		cru_apagar(b, o->pos, z);
	}
}

bool sg_desfazer(sg_buffer *b, struct sg_pos *cursor) {
	if (b->topo == 0) {
		return false;
	}
	b->grupo_aberto = false;
	int g = b->ops[b->topo - 1].grupo;
	while (b->topo > 0 && b->ops[b->topo - 1].grupo == g) {
		const struct op *o = &b->ops[--b->topo];
		aplicar_inverso(b, o);
		if (cursor) {
			*cursor = o->cur_desfeito;
		}
	}
	return true;
}

bool sg_refazer(sg_buffer *b, struct sg_pos *cursor) {
	if (b->topo >= b->n_ops) {
		return false;
	}
	b->grupo_aberto = false;
	int g = b->ops[b->topo].grupo;
	while (b->topo < b->n_ops && b->ops[b->topo].grupo == g) {
		const struct op *o = &b->ops[b->topo++];
		aplicar_direto(b, o);
		if (cursor) {
			*cursor = o->cur_refeito;
		}
	}
	return true;
}

void sg_marcar_salvo(sg_buffer *b) {
	b->salvo_topo = b->topo;
	b->mod_extra = false;
}

bool sg_modificado(const sg_buffer *b) {
	return b->mod_extra || b->salvo_topo != b->topo;
}

/* ---- ciclo de vida e propriedades ---- */

sg_buffer *sg_novo(void) {
	sg_buffer *b = g_new0(sg_buffer, 1);
	linhas_limpar(b);
	b->fim = SG_LF;
	b->cod = SG_UTF8;
	b->salvo_topo = 0;
	return b;
}

static void desfazer_limpar(sg_buffer *b) {
	for (int i = 0; i < b->n_ops; i++) {
		op_liberar(&b->ops[i]);
	}
	b->n_ops = b->topo = 0;
	b->salvo_topo = 0;
	b->grupo_aberto = false;
	b->lote = 0;
}

void sg_liberar(sg_buffer *b) {
	if (!b) {
		return;
	}
	for (int i = 0; i < b->n; i++) {
		g_free(b->l[i].s);
	}
	g_free(b->l);
	desfazer_limpar(b);
	g_free(b->ops);
	g_free(b->caminho);
	g_free(b);
}

const char *sg_caminho(const sg_buffer *b) {
	return b->caminho;
}

enum sg_fim_linha sg_fim_de_linha(const sg_buffer *b) {
	return b->fim;
}

void sg_definir_fim_de_linha(sg_buffer *b, enum sg_fim_linha f) {
	if (b->fim != f) {
		b->fim = f;
		b->mod_extra = true;
	}
}

enum sg_codificacao sg_codificacao(const sg_buffer *b) {
	return b->cod;
}

void sg_definir_codificacao(sg_buffer *b, enum sg_codificacao c) {
	if (b->cod != c) {
		b->cod = c;
		b->mod_extra = true;
	}
}

bool sg_tem_bom(const sg_buffer *b) {
	return b->bom;
}

void sg_definir_bom(sg_buffer *b, bool bom) {
	if (b->bom != bom) {
		b->bom = bom;
		b->mod_extra = true;
	}
}

bool sg_fim_misto(const sg_buffer *b) {
	return b->misto;
}

/* ---- arquivo ---- */

static void erro_msg(char *erro, size_t tam, const char *fmt, ...) G_GNUC_PRINTF(3, 4);
static void erro_msg(char *erro, size_t tam, const char *fmt, ...) {
	if (!erro || !tam) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(erro, tam, fmt, ap);
	va_end(ap);
}

static void guardar_disco(sg_buffer *b, const char *caminho) {
	struct stat st;
	if (caminho && stat(caminho, &st) == 0) {
		b->mtime = st.st_mtim;
		b->tamanho_disco = st.st_size;
		b->tem_disco = true;
	} else {
		b->tem_disco = false;
	}
}

bool sg_mudou_no_disco(const sg_buffer *b) {
	if (!b->caminho || !b->tem_disco) {
		return false;
	}
	struct stat st;
	if (stat(b->caminho, &st) != 0) {
		return true; /* sumiu */
	}
	return st.st_mtim.tv_sec != b->mtime.tv_sec || st.st_mtim.tv_nsec != b->mtime.tv_nsec || st.st_size != b->tamanho_disco;
}

int sg_abrir(sg_buffer *b, const char *caminho, char *erro, size_t tam) {
	struct stat st;
	if (stat(caminho, &st) != 0) {
		erro_msg(erro, tam, "não consegui abrir: %s", strerror(errno));
		return -1;
	}
	if (S_ISDIR(st.st_mode)) {
		erro_msg(erro, tam, "isto é uma pasta, não um arquivo");
		return -1;
	}
	if (st.st_size > LIMITE_ARQUIVO) {
		erro_msg(erro, tam, "o arquivo é grande demais (mais de %d MB)", LIMITE_ARQUIVO / (1024 * 1024));
		return -1;
	}
	char *dados = NULL;
	gsize n = 0;
	GError *ge = NULL;
	if (!g_file_get_contents(caminho, &dados, &n, &ge)) {
		erro_msg(erro, tam, "não consegui ler: %s", ge ? ge->message : "erro");
		g_clear_error(&ge);
		return -1;
	}
	if ((n >= 2 && (unsigned char)dados[0] == 0xFF && (unsigned char)dados[1] == 0xFE) || (n >= 2 && (unsigned char)dados[0] == 0xFE && (unsigned char)dados[1] == 0xFF)) {
		erro_msg(erro, tam, "o arquivo está em UTF-16 (ainda não abro)");
		g_free(dados);
		return -1;
	}
	for (gsize i = 0; i < n && i < 8192; i++) {
		if (dados[i] == '\0') {
			erro_msg(erro, tam, "parece um arquivo binário, não de texto");
			g_free(dados);
			return -1;
		}
	}
	const char *t = dados;
	gsize tn = n;
	bool bom = false;
	if (tn >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) {
		bom = true;
		t += 3;
		tn -= 3;
	}
	enum sg_codificacao cod = SG_UTF8;
	char *convertido = NULL;
	if (!g_utf8_validate(t, (gssize)tn, NULL)) {
		/* não é UTF-8: é Latin-1 (a codificação antiga mais comum); cada byte é o ponto de código */
		cod = SG_LATIN1;
		GString *u = g_string_sized_new(tn + tn / 4);
		for (gsize i = 0; i < tn; i++) {
			g_string_append_unichar(u, (gunichar)(unsigned char)t[i]);
		}
		tn = u->len;
		convertido = g_string_free(u, FALSE);
		t = convertido;
		bom = false;
	}
	/* o fim de linha predominante; o \r só sai quando faz parte de \r\n */
	size_t crlf = 0, lf = 0;
	for (gsize i = 0; i < tn; i++) {
		if (t[i] == '\n') {
			if (i > 0 && t[i - 1] == '\r') {
				crlf++;
			} else {
				lf++;
			}
		}
	}
	desfazer_limpar(b);
	for (int i = 0; i < b->n; i++) {
		g_free(b->l[i].s);
	}
	b->n = 0;
	gsize ini = 0;
	int cont = 0;
	for (gsize i = 0; i <= tn; i++) {
		if (i == tn || t[i] == '\n') {
			gsize fim = i;
			if (i < tn && fim > ini && t[fim - 1] == '\r') {
				fim--;
			}
			linhas_inserir_vazias(b, b->n, 1);
			linha_definir(&b->l[b->n - 1], t + ini, fim - ini);
			ini = i + 1;
			cont++;
		}
	}
	b->fim = crlf > lf ? SG_CRLF : SG_LF;
	b->misto = crlf > 0 && lf > 0;
	b->cod = cod;
	b->bom = bom;
	b->mod_extra = false;
	g_free(b->caminho);
	b->caminho = g_strdup(caminho);
	guardar_disco(b, caminho);
	g_free(convertido);
	g_free(dados);
	return 0;
}

/* o documento como vai para o disco: o fim de linha, a codificação e o BOM; NULL com o motivo se não cabe */
static char *serializar(const sg_buffer *b, size_t *tam, char *erro, size_t tam_erro) {
	GString *s = g_string_sized_new(sg_bytes(b) + 16);
	if (b->bom && b->cod == SG_UTF8) {
		g_string_append(s, "\xEF\xBB\xBF");
	}
	for (int i = 0; i < b->n; i++) {
		const struct linha *x = &b->l[i];
		if (b->cod == SG_LATIN1) {
			const char *p = x->s;
			while (*p) {
				gunichar c = g_utf8_get_char(p);
				if (c > 0xFF) {
					erro_msg(erro, tam_erro, "o texto tem caracteres que não existem em Latin-1 (linha %d); salve em UTF-8", i + 1);
					g_string_free(s, TRUE);
					return NULL;
				}
				g_string_append_c(s, (char)c);
				p = g_utf8_next_char(p);
			}
		} else {
			g_string_append_len(s, x->s, (gssize)x->len);
		}
		if (i < b->n - 1) {
			g_string_append(s, b->fim == SG_CRLF ? "\r\n" : "\n");
		}
	}
	*tam = s->len;
	return g_string_free(s, FALSE);
}

static int gravar_tudo(int fd, const char *d, size_t n) {
	while (n > 0) {
		ssize_t w = write(fd, d, n);
		if (w < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		d += w;
		n -= (size_t)w;
	}
	return 0;
}

int sg_salvar(sg_buffer *b, const char *caminho, char *erro, size_t tam) {
	const char *destino = caminho ? caminho : b->caminho;
	if (!destino || !*destino) {
		erro_msg(erro, tam, "o documento ainda não tem um nome de arquivo");
		return -1;
	}
	size_t n = 0;
	char *dados = serializar(b, &n, erro, tam);
	if (!dados) {
		return -1;
	}
	/* um link simbólico: grava no arquivo para onde ele aponta, sem trocar o link por um arquivo */
	char *real = realpath(destino, NULL);
	const char *alvo = real ? real : destino;
	mode_t modo = 0666 & ~umask_atual();
	struct stat st;
	if (stat(alvo, &st) == 0) {
		modo = st.st_mode & 07777;
	}
	char *dir = g_path_get_dirname(alvo), *base = g_path_get_basename(alvo);
	char *tmp = g_strdup_printf("%s/.%s.signals-XXXXXX", dir, base);
	int fd = mkstemp(tmp);
	int rc = -1;
	if (fd >= 0) {
		if (gravar_tudo(fd, dados, n) == 0 && fchmod(fd, modo) == 0 && fsync(fd) == 0) {
			close(fd);
			fd = -1;
			if (rename(tmp, alvo) == 0) {
				rc = 0;
			} else {
				erro_msg(erro, tam, "não consegui gravar: %s", strerror(errno));
				unlink(tmp);
			}
		} else {
			erro_msg(erro, tam, "não consegui gravar: %s", strerror(errno));
			close(fd);
			fd = -1;
			unlink(tmp);
		}
	} else if (errno == EACCES || errno == EPERM || errno == EROFS) {
		/* a pasta não deixa criar arquivos, mas o arquivo pode ser gravado (um arquivo de outro dono com permissão): grava direto nele */
		int d = open(alvo, O_WRONLY | O_TRUNC | O_CREAT, modo);
		if (d >= 0 && gravar_tudo(d, dados, n) == 0 && fsync(d) == 0) {
			rc = 0;
		} else {
			erro_msg(erro, tam, "não consegui gravar: %s", strerror(errno));
		}
		if (d >= 0) {
			close(d);
		}
	} else {
		erro_msg(erro, tam, "não consegui gravar: %s", strerror(errno));
	}
	g_free(tmp);
	g_free(dir);
	g_free(base);
	g_free(dados);
	if (rc == 0) {
		if (!b->caminho || strcmp(b->caminho, destino)) {
			g_free(b->caminho);
			b->caminho = g_strdup(destino);
		}
		guardar_disco(b, alvo);
		sg_marcar_salvo(b);
	}
	free(real);
	return rc;
}

/* ---- busca ---- */

static bool ehpalavra(gunichar c) {
	return g_unichar_isalnum(c) || c == '_';
}

/* a agulha casa na linha em i? devolve quantos bytes da linha usa, ou -1 */
static int casa_em(const char *s, size_t len, size_t i, const char *ag, size_t agn, bool maius, bool palavra) {
	const char *p = s + i, *fim = s + len, *q = ag, *qf = ag + agn;
	while (q < qf) {
		if (p >= fim) {
			return -1;
		}
		gunichar a = g_utf8_get_char(p), c = g_utf8_get_char(q);
		if (maius ? a != c : g_unichar_tolower(a) != g_unichar_tolower(c)) {
			return -1;
		}
		p = g_utf8_next_char(p);
		q = g_utf8_next_char(q);
	}
	size_t usados = (size_t)(p - (s + i));
	if (palavra) {
		if (i > 0 && ehpalavra(g_utf8_get_char(g_utf8_prev_char(s + i)))) {
			return -1;
		}
		if (p < fim && ehpalavra(g_utf8_get_char(p))) {
			return -1;
		}
	}
	return (int)usados;
}

/* o primeiro achado na linha a partir de col (bytes); -1 se não tem */
static int achar_na_linha(const struct linha *x, const struct sg_busca *q, size_t agn, int col, int *tam) {
	for (size_t i = (size_t)col; i + (agn ? 0 : 1) <= x->len; ) {
		int m = casa_em(x->s, x->len, i, q->agulha, agn, q->maiusculas, q->palavra);
		if (m >= 0) {
			*tam = m;
			return (int)i;
		}
		if (i >= x->len) {
			break;
		}
		i = (size_t)(g_utf8_next_char(x->s + i) - x->s);
	}
	return -1;
}

/* o último achado na linha que começa antes de limite (bytes) */
static int ultimo_na_linha(const struct linha *x, const struct sg_busca *q, size_t agn, int limite, int minimo, int *tam) {
	int melhor = -1, mt = 0, col = 0;
	for (;;) {
		int t = 0;
		int i = achar_na_linha(x, q, agn, col, &t);
		if (i < 0 || i >= limite) {
			break;
		}
		if (i >= minimo) {
			melhor = i;
			mt = t;
		}
		col = (int)(g_utf8_next_char(x->s + i) - x->s);
		if ((size_t)col > x->len) {
			break;
		}
	}
	*tam = mt;
	return melhor;
}

bool sg_buscar(const sg_buffer *b, const struct sg_busca *q, struct sg_pos de, bool frente, struct sg_pos *ini, struct sg_pos *fim, bool *voltou) {
	if (voltou) {
		*voltou = false;
	}
	if (!q || !q->agulha || !*q->agulha || strchr(q->agulha, '\n')) {
		return false;
	}
	size_t agn = strlen(q->agulha);
	de = sg_pos_valida(b, de);
	int tam = 0;
	if (frente) {
		for (int passo = 0; passo < 2; passo++) {
			int primeira = passo == 0 ? de.linha : 0, ultima = passo == 0 ? b->n - 1 : de.linha;
			for (int i = primeira; i <= ultima; i++) {
				int col = passo == 0 && i == de.linha ? de.col : 0;
				int a = achar_na_linha(&b->l[i], q, agn, col, &tam);
				if (a >= 0) {
					if (passo == 1 && i == de.linha && a >= de.col) {
						continue; /* já foi olhado na primeira passada: aqui só o que ficou antes */
					}
					*ini = (struct sg_pos){ i, a };
					*fim = (struct sg_pos){ i, a + tam };
					if (voltou) {
						*voltou = passo == 1;
					}
					return true;
				}
			}
		}
		return false;
	}
	for (int passo = 0; passo < 2; passo++) {
		int primeira = passo == 0 ? de.linha : b->n - 1, ultima = passo == 0 ? 0 : de.linha;
		for (int i = primeira; i >= ultima; i--) {
			int limite = passo == 0 && i == de.linha ? de.col : (int)b->l[i].len + 1;
			int minimo = passo == 1 && i == de.linha ? de.col : 0;
			int a = ultimo_na_linha(&b->l[i], q, agn, limite, minimo, &tam);
			if (a >= 0) {
				*ini = (struct sg_pos){ i, a };
				*fim = (struct sg_pos){ i, a + tam };
				if (voltou) {
					*voltou = passo == 1;
				}
				return true;
			}
		}
	}
	return false;
}

int sg_contar(const sg_buffer *b, const struct sg_busca *q) {
	if (!q || !q->agulha || !*q->agulha || strchr(q->agulha, '\n')) {
		return 0;
	}
	size_t agn = strlen(q->agulha);
	int total = 0;
	for (int i = 0; i < b->n; i++) {
		int col = 0, tam;
		for (;;) {
			int a = achar_na_linha(&b->l[i], q, agn, col, &tam);
			if (a < 0) {
				break;
			}
			total++;
			col = a + (tam > 0 ? tam : 1);
			if ((size_t)col > b->l[i].len) {
				break;
			}
		}
	}
	return total;
}

int sg_substituir_tudo(sg_buffer *b, const struct sg_busca *q, const char *por) {
	if (!q || !q->agulha || !*q->agulha || strchr(q->agulha, '\n')) {
		return 0;
	}
	size_t agn = strlen(q->agulha), pn = strlen(por ? por : "");
	int total = 0;
	bool eu_abri = b->lote == 0;
	if (eu_abri) {
		b->lote = ++b->prox_grupo;
	}
	/* de baixo para cima: as posições que faltam trocar não mudam */
	for (int i = b->n - 1; i >= 0; i--) {
		GArray *achados = g_array_new(FALSE, FALSE, sizeof(int) * 2);
		int col = 0, tam;
		for (;;) {
			int a = achar_na_linha(&b->l[i], q, agn, col, &tam);
			if (a < 0) {
				break;
			}
			int par[2] = { a, tam };
			g_array_append_val(achados, par);
			col = a + (tam > 0 ? tam : 1);
			if ((size_t)col > b->l[i].len) {
				break;
			}
		}
		for (int k = (int)achados->len - 1; k >= 0; k--) {
			int *par = &g_array_index(achados, int, k * 2);
			sg_trocar(b, (struct sg_pos){ i, par[0] }, (struct sg_pos){ i, par[0] + par[1] }, por ? por : "", pn);
			total++;
		}
		g_array_free(achados, TRUE);
	}
	if (eu_abri) {
		b->lote = 0;
		b->grupo_aberto = false;
	}
	return total;
}
