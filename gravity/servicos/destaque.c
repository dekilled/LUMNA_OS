#include "destaque.h"

#include <ctype.h>
#include <string.h>

enum { C_LIKE, HASH, DASH, MARCACAO, JSON_L, NADA };

struct lingua {
	const char *nomes; /* separados por espaço */
	const char *ext;
	int comentario;
	const char *palavras; /* separadas por espaço */
	const char *tipos;
};

static const struct lingua linguas[] = {
	{ "c h", "c", C_LIKE, "if else for while do switch case break continue return goto sizeof typedef struct union enum static const extern inline volatile register sizeof default #include #define #ifdef #ifndef #endif #if #else #pragma", "int char long short unsigned signed float double void size_t uint8_t uint16_t uint32_t uint64_t int32_t int64_t bool NULL true false" },
	{ "cpp c++ cc cxx hpp", "cpp", C_LIKE, "if else for while do switch case break continue return goto sizeof typedef struct union enum class public private protected static const constexpr extern inline virtual override template typename namespace using new delete try catch throw auto nullptr this operator #include #define #ifdef #ifndef #endif #if #else #pragma", "int char long short unsigned signed float double void size_t bool string vector map true false NULL" },
	{ "python py python3", "py", HASH, "def class return if elif else for while in not and or is import from as with try except finally raise pass break continue lambda yield global nonlocal assert del async await", "None True False self cls int str float bool list dict set tuple print len range" },
	{ "bash sh shell zsh console", "sh", HASH, "if then else elif fi for while do done case esac in function return exit export local readonly source echo cd set unset shift trap alias", "true false" },
	{ "javascript js jsx mjs node", "js", C_LIKE, "function return if else for while do switch case break continue new delete typeof instanceof in of var let const class extends super this import from export default async await try catch finally throw yield static get set", "null undefined true false NaN Infinity console document window" },
	{ "typescript ts tsx", "ts", C_LIKE, "function return if else for while do switch case break continue new delete typeof instanceof in of var let const class extends implements interface type enum namespace import from export default async await try catch finally throw yield static readonly public private protected abstract as is keyof", "null undefined true false any string number boolean void never unknown object" },
	{ "rust rs", "rs", C_LIKE, "fn let mut const static struct enum impl trait pub use mod crate self super as if else match for while loop in return break continue where move ref async await unsafe dyn type", "Self Option Result Some None Ok Err Vec String Box i8 i16 i32 i64 u8 u16 u32 u64 usize isize f32 f64 bool str char true false" },
	{ "go golang", "go", C_LIKE, "func package import var const type struct interface map chan go defer return if else for range switch case default break continue select fallthrough goto", "nil true false string int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 float32 float64 bool byte rune error any" },
	{ "java kotlin kt", "java", C_LIKE, "class interface enum extends implements public private protected static final abstract void return if else for while do switch case break continue new try catch finally throw throws import package this super fun val var when override", "int long double float boolean char byte short String null true false" },
	{ "json", "json", JSON_L, "", "true false null" },
	{ "yaml yml toml ini conf", "yml", HASH, "", "true false null yes no on off" },
	{ "html xml svg", "html", MARCACAO, "", "" },
	{ "css scss", "css", C_LIKE, "@media @import @font-face important", "" },
	{ "sql", "sql", DASH, "select from where insert into values update set delete create table alter drop index join left right inner outer on group by order having limit offset and or not null as distinct union primary key foreign references default", "int integer varchar text boolean date timestamp true false" },
	{ "lua", "lua", DASH, "function end if then else elseif for while do repeat until return local in break not and or", "nil true false" },
	{ "ruby rb", "rb", HASH, "def end class module if elsif else unless while until for in do begin rescue ensure return yield require include extend attr_accessor", "nil true false self" },
	{ "php", "php", C_LIKE, "function return if else elseif for foreach while do switch case break continue new class extends implements public private protected static echo require include namespace use try catch throw", "null true false array string int" },
	{ "makefile make", "mk", HASH, "ifeq ifneq else endif include define endef export", "" },
};

static const struct lingua *achar(const char *nome) {
	if (!nome || !*nome) {
		return NULL;
	}
	char *n = g_ascii_strdown(nome, -1);
	g_strstrip(n);
	const struct lingua *r = NULL;
	for (guint i = 0; i < G_N_ELEMENTS(linguas) && !r; i++) {
		char **ns = g_strsplit(linguas[i].nomes, " ", -1);
		for (int k = 0; ns[k] && !r; k++) {
			if (!strcmp(ns[k], n)) {
				r = &linguas[i];
			}
		}
		g_strfreev(ns);
	}
	g_free(n);
	return r;
}

const char *destaque_extensao(const char *lingua) {
	const struct lingua *l = achar(lingua);
	return l ? l->ext : "txt";
}

static gboolean na_lista(const char *lista, const char *p, size_t n) {
	const char *c = lista;
	while (*c) {
		while (*c == ' ') {
			c++;
		}
		const char *fim = strchr(c, ' ');
		size_t m = fim ? (size_t)(fim - c) : strlen(c);
		if (m == n && !strncmp(c, p, n)) {
			return TRUE;
		}
		if (!fim) {
			break;
		}
		c = fim;
	}
	return FALSE;
}

static void emitir(GString *g, const char *ini, size_t n, const char *cor, gboolean italico) {
	if (!n) {
		return;
	}
	char *t = g_markup_escape_text(ini, (gssize)n);
	if (cor) {
		g_string_append_printf(g, "<span foreground=\"%s\"%s>%s</span>", cor, italico ? " style=\"italic\"" : "", t);
	} else {
		g_string_append(g, t);
	}
	g_free(t);
}

static gboolean ident_ini(unsigned char c) {
	return isalpha(c) || c == '_' || c == '#' || c == '@' || c >= 0x80;
}
static gboolean ident_cor(unsigned char c) {
	return isalnum(c) || c == '_' || c >= 0x80;
}

char *destaque_markup(const char *lingua, const char *codigo, const struct destaque_cores *cores) {
	const struct lingua *l = achar(lingua);
	GString *g = g_string_new(NULL);
	size_t n = strlen(codigo);
	if (!l || n > 60000) { /* sem linguagem (ou grande demais): só o texto, escapado */
		char *e = g_markup_escape_text(codigo, -1);
		g_string_append(g, e);
		g_free(e);
		return g_string_free(g, FALSE);
	}
	size_t i = 0, solto = 0; /* solto: onde começa o texto que ainda não foi emitido */
#define SOLTAR(ate) do { emitir(g, codigo + solto, (ate) - solto, NULL, FALSE); solto = (ate); } while (0)
	while (i < n) {
		unsigned char c = (unsigned char)codigo[i];
		size_t ini = i;
		/* comentários */
		gboolean linha = FALSE, bloco = FALSE, marc = FALSE;
		if (l->comentario == C_LIKE && c == '/' && i + 1 < n && codigo[i + 1] == '/') {
			linha = TRUE;
		} else if (l->comentario == C_LIKE && c == '/' && i + 1 < n && codigo[i + 1] == '*') {
			bloco = TRUE;
		} else if (l->comentario == HASH && c == '#' && (i == 0 || (codigo[i - 1] != '$' && codigo[i - 1] != '{')) && !(l->ext[0] == 'c' && !strcmp(l->ext, "c"))) {
			linha = TRUE;
		} else if (l->comentario == DASH && c == '-' && i + 1 < n && codigo[i + 1] == '-') {
			linha = TRUE;
		} else if (l->comentario == MARCACAO && c == '<' && !strncmp(codigo + i, "<!--", 4)) {
			marc = TRUE;
		}
		if (linha || bloco || marc) {
			SOLTAR(ini);
			size_t fim = i;
			if (linha) {
				while (fim < n && codigo[fim] != '\n') {
					fim++;
				}
			} else {
				const char *f = strstr(codigo + i + 2, bloco ? "*/" : "-->");
				fim = f ? (size_t)(f - codigo) + (bloco ? 2 : 3) : n;
			}
			emitir(g, codigo + ini, fim - ini, cores->comentario, TRUE);
			i = solto = fim;
			continue;
		}
		/* textos entre aspas */
		if (c == '"' || c == '\'' || (c == '`' && l->comentario != HASH)) {
			if (c == '\'' && l->comentario == C_LIKE && l->ext[0] == 'r' && !strcmp(l->ext, "rs")) {
				i++; /* 'a lifetime em Rust: não é um texto */
				continue;
			}
			SOLTAR(ini);
			size_t fim = i + 1;
			gboolean triplo = l->comentario == HASH && i + 2 < n && codigo[i + 1] == c && codigo[i + 2] == c;
			if (triplo) {
				const char tri[4] = { (char)c, (char)c, (char)c, 0 };
				const char *f = strstr(codigo + i + 3, tri);
				fim = f ? (size_t)(f - codigo) + 3 : n;
			} else {
				while (fim < n && codigo[fim] != (char)c && !(codigo[fim] == '\n' && c != '`')) {
					if (codigo[fim] == '\\' && fim + 1 < n) {
						fim++;
					}
					fim++;
				}
				if (fim < n && codigo[fim] == (char)c) {
					fim++;
				}
			}
			emitir(g, codigo + ini, fim - ini, cores->texto, FALSE);
			i = solto = fim;
			continue;
		}
		/* números */
		if (isdigit(c) && (i == 0 || !ident_cor((unsigned char)codigo[i - 1]))) {
			SOLTAR(ini);
			size_t fim = i;
			while (fim < n && (isalnum((unsigned char)codigo[fim]) || codigo[fim] == '.' || codigo[fim] == '_')) {
				fim++;
			}
			emitir(g, codigo + ini, fim - ini, cores->numero, FALSE);
			i = solto = fim;
			continue;
		}
		/* palavras */
		if (ident_ini(c) && (i == 0 || !ident_cor((unsigned char)codigo[i - 1]))) {
			size_t fim = i + 1;
			while (fim < n && (ident_cor((unsigned char)codigo[fim]) || (c == '#' && fim == i + 1 && isalpha((unsigned char)codigo[fim])))) {
				fim++;
			}
			size_t len = fim - i;
			const char *cor = NULL;
			if (l->palavras[0] && na_lista(l->palavras, codigo + i, len)) {
				cor = cores->palavra;
			} else if (l->tipos[0] && na_lista(l->tipos, codigo + i, len)) {
				cor = cores->tipo;
			} else if (l->comentario == JSON_L) {
				cor = NULL;
			}
			if (cor) {
				SOLTAR(ini);
				emitir(g, codigo + ini, len, cor, FALSE);
				solto = fim;
			}
			i = fim;
			continue;
		}
		i++;
	}
	SOLTAR(n);
#undef SOLTAR
	return g_string_free(g, FALSE);
}

/* ---- parece código? ---- */

static gboolean linha_de_codigo(const char *l, size_t n) {
	size_t i = 0;
	while (i < n && (l[i] == ' ' || l[i] == '\t')) {
		i++;
	}
	if (i == n) {
		return FALSE;
	}
	const char *t = l + i;
	size_t m = n - i;
	if (i >= 2 && m > 1) { /* recuo */
		return TRUE;
	}
	if (t[m - 1] == ';' || t[m - 1] == '{' || t[m - 1] == '}' || t[m - 1] == ':' || t[m - 1] == ',') {
		return strchr(t, ' ') != NULL || m < 3 || t[m - 1] != ',';
	}
	static const char *marcas[] = { "#include", "#define", "def ", "class ", "import ", "from ", "function ", "fn ", "func ", "const ", "let ", "var ", "return", "if (", "for (", "while (", "=> ", "->", "::", "&&", "||", "==", "!=", "#!/" };
	for (guint k = 0; k < G_N_ELEMENTS(marcas); k++) {
		size_t ml = strlen(marcas[k]);
		if (!strncmp(t, marcas[k], ml) || (k >= 12 && g_strstr_len(t, (gssize)m, marcas[k]))) {
			return TRUE;
		}
	}
	return FALSE;
}

gboolean destaque_parece_codigo(const char *texto) {
	if (!texto || strstr(texto, "```")) {
		return FALSE; /* já tem um bloco: quem escreveu cuidou disso */
	}
	int linhas = 0, codigo = 0;
	const char *p = texto;
	while (*p) {
		const char *fim = strchr(p, '\n');
		size_t n = fim ? (size_t)(fim - p) : strlen(p);
		if (n > 0) {
			linhas++;
			codigo += linha_de_codigo(p, n);
		}
		if (!fim) {
			break;
		}
		p = fim + 1;
	}
	return linhas >= 4 && codigo * 100 >= linhas * 60;
}

const char *destaque_adivinhar(const char *t) {
	if (!t) {
		return "";
	}
	if (g_str_has_prefix(t, "#!/bin/bash") || g_str_has_prefix(t, "#!/bin/sh") || g_str_has_prefix(t, "#!/usr/bin/env bash")) {
		return "bash";
	}
	if (g_str_has_prefix(t, "#!/usr/bin/env python") || g_str_has_prefix(t, "#!/usr/bin/python") || strstr(t, "\ndef ") || g_str_has_prefix(t, "def ") ||
		(strstr(t, "import ") && strstr(t, ":\n"))) {
		return "python";
	}
	if (strstr(t, "#include")) {
		return "c";
	}
	if (strstr(t, "fn ") && (strstr(t, "let ") || strstr(t, "->"))) {
		return "rust";
	}
	if (strstr(t, "function ") || strstr(t, "const ") || strstr(t, "=> ")) {
		return "javascript";
	}
	if ((t[0] == '{' || t[0] == '[') && strstr(t, "\":")) {
		return "json";
	}
	return "";
}
