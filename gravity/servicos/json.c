#include "json.h"

#include <stdlib.h>
#include <string.h>

#define PROFUNDIDADE_MAX 32

static void espaco(const char **p) {
	while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') {
		(*p)++;
	}
}

static struct json *novo(enum json_tipo tipo) {
	struct json *j = g_new0(struct json, 1);
	j->tipo = tipo;
	return j;
}

static void filho(struct json *pai, struct json *f) {
	pai->filhos = g_renew(struct json *, pai->filhos, pai->nfilhos + 1);
	pai->filhos[pai->nfilhos++] = f;
}

static int hex4(const char *p) {
	int v = 0;
	for (int i = 0; i < 4; i++) {
		int c = p[i];
		v <<= 4;
		if (c >= '0' && c <= '9') {
			v |= c - '0';
		} else if (c >= 'a' && c <= 'f') {
			v |= c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			v |= c - 'A' + 10;
		} else {
			return -1;
		}
	}
	return v;
}

/* um texto entre aspas; devolve g_malloc, ou NULL */
static char *ler_texto(const char **p) {
	if (**p != '"') {
		return NULL;
	}
	(*p)++;
	GString *s = g_string_new(NULL);
	while (**p && **p != '"') {
		unsigned char c = (unsigned char)**p;
		(*p)++;
		if (c != '\\') {
			g_string_append_c(s, (char)c);
			continue;
		}
		char e = **p;
		if (!e) {
			break;
		}
		(*p)++;
		switch (e) {
		case 'n': g_string_append_c(s, '\n'); break;
		case 't': g_string_append_c(s, '\t'); break;
		case 'r': g_string_append_c(s, '\r'); break;
		case 'b': g_string_append_c(s, '\b'); break;
		case 'f': g_string_append_c(s, '\f'); break;
		case 'u': {
			int u = hex4(*p);
			if (u < 0) {
				g_string_free(s, TRUE);
				return NULL;
			}
			*p += 4;
			if (u >= 0xd800 && u < 0xdc00 && (*p)[0] == '\\' && (*p)[1] == 'u') {
				int baixo = hex4(*p + 2);
				if (baixo >= 0xdc00 && baixo < 0xe000) {
					u = 0x10000 + ((u - 0xd800) << 10) + (baixo - 0xdc00);
					*p += 6;
				}
			}
			g_string_append_unichar(s, (gunichar)u);
			break;
		}
		default: g_string_append_c(s, e); break;
		}
	}
	if (**p != '"') {
		g_string_free(s, TRUE);
		return NULL;
	}
	(*p)++;
	return g_string_free(s, FALSE);
}

static struct json *ler_valor(const char **p, int nivel) {
	if (nivel > PROFUNDIDADE_MAX) {
		return NULL;
	}
	espaco(p);
	char c = **p;
	if (c == '"') {
		char *t = ler_texto(p);
		if (!t) {
			return NULL;
		}
		struct json *j = novo(JSON_TEXTO);
		j->s = t;
		return j;
	}
	if (c == '{' || c == '[') {
		bool obj = c == '{';
		struct json *j = novo(obj ? JSON_OBJETO : JSON_LISTA);
		(*p)++;
		espaco(p);
		if (**p == (obj ? '}' : ']')) {
			(*p)++;
			return j;
		}
		for (;;) {
			espaco(p);
			char *chave = NULL;
			if (obj) {
				chave = ler_texto(p);
				espaco(p);
				if (!chave || **p != ':') {
					g_free(chave);
					json_liberar(j);
					return NULL;
				}
				(*p)++;
			}
			struct json *f = ler_valor(p, nivel + 1);
			if (!f) {
				g_free(chave);
				json_liberar(j);
				return NULL;
			}
			f->chave = chave;
			filho(j, f);
			espaco(p);
			if (**p == ',') {
				(*p)++;
				continue;
			}
			if (**p == (obj ? '}' : ']')) {
				(*p)++;
				return j;
			}
			json_liberar(j);
			return NULL;
		}
	}
	if (!strncmp(*p, "true", 4)) {
		*p += 4;
		struct json *j = novo(JSON_BOOL);
		j->b = true;
		return j;
	}
	if (!strncmp(*p, "false", 5)) {
		*p += 5;
		return novo(JSON_BOOL);
	}
	if (!strncmp(*p, "null", 4)) {
		*p += 4;
		return novo(JSON_NULO);
	}
	char *fim = NULL;
	double n = strtod(*p, &fim);
	if (fim == *p) {
		return NULL;
	}
	*p = fim;
	struct json *j = novo(JSON_NUMERO);
	j->n = n;
	return j;
}

struct json *json_ler(const char *texto) {
	if (!texto) {
		return NULL;
	}
	const char *p = texto;
	struct json *j = ler_valor(&p, 0);
	if (!j) {
		return NULL;
	}
	espaco(&p);
	if (*p) {
		json_liberar(j);
		return NULL;
	}
	return j;
}

void json_liberar(struct json *j) {
	if (!j) {
		return;
	}
	for (int i = 0; i < j->nfilhos; i++) {
		json_liberar(j->filhos[i]);
	}
	g_free(j->filhos);
	g_free(j->s);
	g_free(j->chave);
	g_free(j);
}

const struct json *json_chave(const struct json *obj, const char *chave) {
	if (!obj || obj->tipo != JSON_OBJETO) {
		return NULL;
	}
	for (int i = 0; i < obj->nfilhos; i++) {
		if (obj->filhos[i]->chave && !strcmp(obj->filhos[i]->chave, chave)) {
			return obj->filhos[i];
		}
	}
	return NULL;
}

const struct json *json_item(const struct json *lista, int i) {
	return lista && lista->tipo == JSON_LISTA && i >= 0 && i < lista->nfilhos ? lista->filhos[i] : NULL;
}

const char *json_texto(const struct json *j) {
	return j && j->tipo == JSON_TEXTO ? j->s : NULL;
}

double json_numero(const struct json *j, double padrao) {
	return j && j->tipo == JSON_NUMERO ? j->n : padrao;
}

bool json_bool(const struct json *j, bool padrao) {
	return j && j->tipo == JSON_BOOL ? j->b : padrao;
}

const char *json_chave_texto(const struct json *obj, const char *chave) {
	const char *t = json_texto(json_chave(obj, chave));
	return t ? t : "";
}

void json_escrever_texto(GString *s, const char *texto) {
	g_string_append_c(s, '"');
	for (const char *p = texto ? texto : ""; *p; p++) {
		unsigned char c = (unsigned char)*p;
		switch (c) {
		case '"': g_string_append(s, "\\\""); break;
		case '\\': g_string_append(s, "\\\\"); break;
		case '\n': g_string_append(s, "\\n"); break;
		case '\r': g_string_append(s, "\\r"); break;
		case '\t': g_string_append(s, "\\t"); break;
		default:
			if (c < 0x20) {
				g_string_append_printf(s, "\\u%04x", c);
			} else {
				g_string_append_c(s, (char)c);
			}
		}
	}
	g_string_append_c(s, '"');
}

void json_escrever(GString *s, const struct json *j) {
	if (!j) {
		g_string_append(s, "null");
		return;
	}
	switch (j->tipo) {
	case JSON_NULO: g_string_append(s, "null"); break;
	case JSON_BOOL: g_string_append(s, j->b ? "true" : "false"); break;
	case JSON_NUMERO: {
		char buf[40];
		g_ascii_formatd(buf, sizeof(buf), "%.15g", j->n);
		g_string_append(s, buf);
		break;
	}
	case JSON_TEXTO: json_escrever_texto(s, j->s); break;
	case JSON_LISTA:
	case JSON_OBJETO: {
		bool obj = j->tipo == JSON_OBJETO;
		g_string_append_c(s, obj ? '{' : '[');
		for (int i = 0; i < j->nfilhos; i++) {
			if (i) {
				g_string_append_c(s, ',');
			}
			if (obj) {
				json_escrever_texto(s, j->filhos[i]->chave);
				g_string_append_c(s, ':');
			}
			json_escrever(s, j->filhos[i]);
		}
		g_string_append_c(s, obj ? '}' : ']');
		break;
	}
	}
}
