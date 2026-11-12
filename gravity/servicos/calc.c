#define _GNU_SOURCE
#include "calc.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef long double num;

/* ---- as unidades ---- */

enum dim { D_ESC, D_COMP, D_MASSA, D_TEMPO, D_VOL, D_TEMP, D_VEL, D_DADOS };

struct un {
	const char *nomes; /* os nomes aceitos, sem acento e em minúsculas, separados por espaço */
	enum dim dim;
	num fator;         /* valor na base = x × fator + desloc */
	num desloc;
	const char *rotulo;
};

/* bases: m, g, s, L, K, m/s, byte */
static const struct un UN[] = {
	{ "mm milimetro milimetros", D_COMP, 0.001L, 0, "mm" },
	{ "cm centimetro centimetros", D_COMP, 0.01L, 0, "cm" },
	{ "m metro metros", D_COMP, 1, 0, "m" },
	{ "km quilometro quilometros kilometro kilometros", D_COMP, 1000, 0, "km" },
	{ "pol polegada polegadas in", D_COMP, 0.0254L, 0, "pol" },
	{ "ft pe pes", D_COMP, 0.3048L, 0, "pés" },
	{ "jd jarda jardas yd", D_COMP, 0.9144L, 0, "jd" },
	{ "mi milha milhas", D_COMP, 1609.344L, 0, "mi" },

	{ "mg miligrama miligramas", D_MASSA, 0.001L, 0, "mg" },
	{ "g grama gramas", D_MASSA, 1, 0, "g" },
	{ "kg quilo quilos quilograma quilogramas kilo kilos", D_MASSA, 1000, 0, "kg" },
	{ "t tonelada toneladas", D_MASSA, 1000000, 0, "t" },
	{ "lb libra libras", D_MASSA, 453.59237L, 0, "lb" },
	{ "oz onca oncas", D_MASSA, 28.349523125L, 0, "oz" },

	{ "ms milissegundo milissegundos", D_TEMPO, 0.001L, 0, "ms" },
	{ "s seg segundo segundos", D_TEMPO, 1, 0, "s" },
	{ "min minuto minutos", D_TEMPO, 60, 0, "min" },
	{ "h hr hora horas", D_TEMPO, 3600, 0, "h" },
	{ "d dia dias", D_TEMPO, 86400, 0, "d" },
	{ "sem semana semanas", D_TEMPO, 604800, 0, "sem" },

	{ "ml mililitro mililitros", D_VOL, 0.001L, 0, "ml" },
	{ "cl centilitro centilitros", D_VOL, 0.01L, 0, "cl" },
	{ "l litro litros", D_VOL, 1, 0, "L" },
	{ "gal galao galoes", D_VOL, 3.785411784L, 0, "gal" },

	{ "\xC2\xB0" "c celsius", D_TEMP, 1, 273.15L, "°C" },
	{ "\xC2\xB0" "f fahrenheit", D_TEMP, 5.0L / 9.0L, 459.67L * 5.0L / 9.0L, "°F" },
	{ "k kelvin \xC2\xB0" "k", D_TEMP, 1, 0, "K" },

	{ "km/h", D_VEL, 1000.0L / 3600.0L, 0, "km/h" },
	{ "m/s", D_VEL, 1, 0, "m/s" },
	{ "mph", D_VEL, 0.44704L, 0, "mph" },
	{ "nos kn", D_VEL, 1852.0L / 3600.0L, 0, "nós" },

	{ "b byte bytes", D_DADOS, 1, 0, "B" },
	{ "kb", D_DADOS, 1e3L, 0, "KB" },
	{ "mb", D_DADOS, 1e6L, 0, "MB" },
	{ "gb", D_DADOS, 1e9L, 0, "GB" },
	{ "tb", D_DADOS, 1e12L, 0, "TB" },
	{ "kib", D_DADOS, 1024.0L, 0, "KiB" },
	{ "mib", D_DADOS, 1048576.0L, 0, "MiB" },
	{ "gib", D_DADOS, 1073741824.0L, 0, "GiB" },
	{ "tib", D_DADOS, 1099511627776.0L, 0, "TiB" },
};
#define N_UN ((int)(sizeof(UN) / sizeof(UN[0])))

static int unidade_por_nome(const char *nome) {
	size_t ln = strlen(nome);
	for (int i = 0; i < N_UN; i++) {
		const char *p = UN[i].nomes;
		while (*p) {
			const char *fim = strchr(p, ' ');
			size_t l = fim ? (size_t)(fim - p) : strlen(p);
			if (l == ln && !memcmp(p, nome, l)) {
				return i;
			}
			p += l + (fim ? 1 : 0);
		}
	}
	return -1;
}

static int unidade_por_rotulo(enum dim d, const char *rot) {
	for (int i = 0; i < N_UN; i++) {
		if (UN[i].dim == d && !strcmp(UN[i].rotulo, rot)) {
			return i;
		}
	}
	return -1;
}

/* ---- as palavras: sem acento e em minúsculas ---- */

static void dobrar(const char *s, size_t n, char *saida, size_t tam) {
	size_t o = 0;
	for (size_t i = 0; i < n && o + 4 < tam;) {
		unsigned char c = (unsigned char)s[i];
		if (c < 0x80) {
			saida[o++] = (char)tolower(c);
			i++;
		} else if (c == 0xC3 && i + 1 < n) {
			unsigned char d = (unsigned char)s[i + 1];
			char r = 0;
			switch (d) {
			case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: r = 'a'; break;
			case 0xA9: case 0xAA: case 0xA8: case 0x89: case 0x8A: case 0x88: r = 'e'; break;
			case 0xAD: case 0xAC: case 0x8D: case 0x8C: r = 'i'; break;
			case 0xB3: case 0xB4: case 0xB5: case 0xB2: case 0x93: case 0x94: case 0x95: case 0x92: r = 'o'; break;
			case 0xBA: case 0xBC: case 0xB9: case 0x9A: case 0x9C: case 0x99: r = 'u'; break;
			case 0xA7: case 0x87: r = 'c'; break;
			default: break;
			}
			if (r) {
				saida[o++] = r;
			} else {
				saida[o++] = (char)c;
				saida[o++] = (char)d;
			}
			i += 2;
		} else {
			saida[o++] = (char)c;
			i++;
		}
	}
	saida[o] = '\0';
}

/* ---- as fichas ---- */

enum tok { T_FIM, T_NUM, T_ID, T_OP };

struct ficha {
	enum tok t;
	num v;          /* T_NUM */
	int casas;      /* T_NUM: quantas casas decimais foram digitadas */
	char s[40];     /* T_ID: a palavra dobrada */
	char op;        /* T_OP: + - * / ^ ( ) % */
	bool espaco_antes;
};

#define MAX_FICHAS 256

struct ctx {
	struct ficha f[MAX_FICHAS];
	int n, i;
	char erro[160];
	bool incompleto;  /* o erro foi por faltar o resto */
	int casas_min;    /* o maior número de casas digitadas */
};

static bool erro(struct ctx *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static bool erro(struct ctx *c, const char *fmt, ...) {
	if (!c->erro[0]) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(c->erro, sizeof(c->erro), fmt, ap);
		va_end(ap);
	}
	return false;
}

static bool falta(struct ctx *c, const char *o_que) {
	c->incompleto = true;
	return erro(c, "falta %s", o_que);
}

/* um número digitado: "1.234,56", "0,5", "12.5", ",5" */
static bool ler_numero(struct ctx *c, const char *s, size_t n, num *v, int *casas) {
	char buf[64];
	if (n >= sizeof(buf)) {
		return erro(c, "número grande demais");
	}
	memcpy(buf, s, n);
	buf[n] = '\0';
	int virgulas = 0, pontos = 0;
	for (size_t i = 0; i < n; i++) {
		virgulas += buf[i] == ',';
		pontos += buf[i] == '.';
	}
	if (virgulas > 1) {
		return erro(c, "número com mais de uma vírgula: %s", buf);
	}
	char limpo[64];
	size_t o = 0;
	if (virgulas) {
		/* pontos antes da vírgula são de milhar */
		char *v_ = strchr(buf, ',');
		for (char *p = buf; p < v_; p++) {
			if (*p != '.') {
				limpo[o++] = *p;
			}
		}
		limpo[o++] = '.';
		for (char *p = v_ + 1; *p; p++) {
			if (*p == '.') {
				return erro(c, "número estranho: %s", buf);
			}
			limpo[o++] = *p;
		}
	} else if (pontos) {
		/* "1.234" e "1.234.567" são milhar; "1.5" e "0.25" são decimais */
		bool milhar = buf[0] != '.';
		const char *p = buf;
		int grupo = 0;
		bool primeiro = true;
		for (; *p; p++) {
			if (*p == '.') {
				if (primeiro ? (grupo < 1 || grupo > 3) : grupo != 3) {
					milhar = false;
				}
				primeiro = false;
				grupo = 0;
			} else {
				grupo++;
			}
		}
		if (grupo != 3) {
			milhar = false;
		}
		if (!milhar && pontos > 1) {
			return erro(c, "número estranho: %s", buf);
		}
		for (p = buf; *p; p++) {
			if (*p != '.' || !milhar) {
				limpo[o++] = *p;
			}
		}
	} else {
		memcpy(limpo, buf, n);
		o = n;
	}
	limpo[o] = '\0';
	char *fim;
	*v = strtold(limpo, &fim);
	if (*fim) {
		return erro(c, "número estranho: %s", buf);
	}
	char *pt = strchr(limpo, '.');
	*casas = pt ? (int)strlen(pt + 1) : 0;
	return true;
}

static bool eh_letra(unsigned char ch) {
	return isalpha(ch) || ch >= 0x80;
}

static bool fichas(struct ctx *c, const char *e) {
	size_t i = 0, len = strlen(e);
	bool espaco = false;
	while (i < len) {
		unsigned char ch = (unsigned char)e[i];
		if (ch == ' ' || ch == '\t' || ch == '\n') {
			espaco = true;
			i++;
			continue;
		}
		if (c->n >= MAX_FICHAS - 1) {
			return erro(c, "conta comprida demais");
		}
		struct ficha *f = &c->f[c->n];
		memset(f, 0, sizeof(*f));
		f->espaco_antes = espaco;
		espaco = false;
		if (isdigit(ch) || (ch == ',' && i + 1 < len && isdigit((unsigned char)e[i + 1]))) {
			size_t j = i;
			while (j < len && (isdigit((unsigned char)e[j]) || e[j] == '.' || e[j] == ',')) {
				/* um ponto ou vírgula sem dígito depois não faz parte do número ("3." ou "3,") */
				if ((e[j] == '.' || e[j] == ',') && !(j + 1 < len && isdigit((unsigned char)e[j + 1]))) {
					break;
				}
				j++;
			}
			f->t = T_NUM;
			if (!ler_numero(c, e + i, j - i, &f->v, &f->casas)) {
				return false;
			}
			if (f->casas > c->casas_min) {
				c->casas_min = f->casas > 6 ? 6 : f->casas;
			}
			i = j;
		} else if (strchr("+-*/^()%", ch)) {
			f->t = T_OP;
			f->op = (char)ch;
			i++;
		} else if (!strncmp(e + i, "\xC3\x97", 2)) { /* × */
			f->t = T_OP;
			f->op = '*';
			i += 2;
		} else if (!strncmp(e + i, "\xC3\xB7", 2)) { /* ÷ */
			f->t = T_OP;
			f->op = '/';
			i += 2;
		} else if (!strncmp(e + i, "\xE2\x88\x92", 3)) { /* − */
			f->t = T_OP;
			f->op = '-';
			i += 3;
		} else if (!strncmp(e + i, "\xE2\x88\x9A", 3)) { /* √ */
			f->t = T_ID;
			strcpy(f->s, "raiz");
			i += 3;
		} else if (!strncmp(e + i, "\xE2\x86\x92", 3)) { /* → */
			f->t = T_ID;
			strcpy(f->s, "em");
			i += 3;
		} else if (!strncmp(e + i, "\xCF\x80", 2)) { /* π */
			f->t = T_ID;
			strcpy(f->s, "pi");
			i += 2;
		} else if (ch == '=') {
			i++; /* "2 + 2 =" */
		} else if (eh_letra(ch) || !strncmp(e + i, "\xC2\xB0", 2) || !strncmp(e + i, "\xC2\xBA", 2)) {
			size_t j = i;
			/* o grau (° ou º) é o começo de "°C" */
			if (!strncmp(e + j, "\xC2\xB0", 2) || !strncmp(e + j, "\xC2\xBA", 2)) {
				j += 2;
			}
			while (j < len && eh_letra((unsigned char)e[j])) {
				/* × ÷ e outros símbolos de 2 bytes não são letra */
				if (!strncmp(e + j, "\xC3\x97", 2) || !strncmp(e + j, "\xC3\xB7", 2)) {
					break;
				}
				if ((unsigned char)e[j] >= 0x80 && !strncmp(e + j, "\xE2", 1)) {
					break;
				}
				j++;
			}
			/* "km/h" e "m/s" são uma unidade só */
			if (j + 1 < len && e[j] == '/' && ((j - i == 2 && !strncasecmp(e + i, "km", 2) && tolower((unsigned char)e[j + 1]) == 'h') || (j - i == 1 && tolower((unsigned char)e[i]) == 'm' && tolower((unsigned char)e[j + 1]) == 's'))
				&& !(j + 2 < len && eh_letra((unsigned char)e[j + 2]))) {
				j += 2;
			}
			char tmp[40];
			size_t l = j - i;
			if (l >= sizeof(tmp) / 2) {
				return erro(c, "palavra comprida demais");
			}
			dobrar(e + i, l, tmp, sizeof(tmp));
			/* o º ordinal vira o grau */
			if (!strncmp(tmp, "\xC2\xBA", 2)) {
				memmove(tmp + 2, tmp + 2, strlen(tmp + 2) + 1);
				tmp[1] = (char)0xB0;
			}
			f->t = T_ID;
			snprintf(f->s, sizeof(f->s), "%s", tmp);
			i = j;
		} else {
			char sim[8];
			size_t l = 1;
			if (ch >= 0x80) {
				l = (ch >= 0xF0) ? 4 : (ch >= 0xE0) ? 3 : 2;
			}
			snprintf(sim, sizeof(sim), "%.*s", (int)l, e + i);
			return erro(c, "não entendi “%s”", sim);
		}
		c->n++;
	}
	c->f[c->n].t = T_FIM;
	return true;
}

/* ---- o valor: um número, com unidade ou não ---- */

struct val {
	num v;            /* na base da grandeza (m, g, s...) ou o próprio número */
	enum dim dim;
	int un;           /* a unidade em que se mostra (-1: nenhuma) */
	bool pct;         /* acabou de ser "N%" */
	bool composto;    /* digitado como "3h20": mostra em horas e minutos */
};

static const struct ficha *olhar(struct ctx *c) {
	return &c->f[c->i];
}

static bool eh_op(struct ctx *c, char op) {
	const struct ficha *f = olhar(c);
	return f->t == T_OP && f->op == op;
}

static bool eh_id(struct ctx *c, const char *s) {
	const struct ficha *f = olhar(c);
	return f->t == T_ID && !strcmp(f->s, s);
}

static bool expr(struct ctx *c, struct val *r);

/* a unidade que vem a seguir (uma ou duas palavras: "graus celsius"); consome só se achar */
static int ler_unidade(struct ctx *c) {
	const struct ficha *f = olhar(c);
	if (f->t != T_ID) {
		return -1;
	}
	if ((!strcmp(f->s, "grau") || !strcmp(f->s, "graus")) && c->f[c->i + 1].t == T_ID) {
		char nome[48];
		snprintf(nome, sizeof(nome), "\xC2\xB0%s", c->f[c->i + 1].s);
		int u = unidade_por_nome(nome);
		if (u < 0) {
			u = unidade_por_nome(c->f[c->i + 1].s);
		}
		if (u >= 0) {
			c->i += 2;
			return u;
		}
	}
	int u = unidade_por_nome(f->s);
	if (u >= 0) {
		c->i++;
	}
	return u;
}

/* a unidade de baixo ("3h20" = 3 h e 20 min) */
static int unidade_menor(int u) {
	if (UN[u].dim != D_TEMPO) {
		return -1;
	}
	if (!strcmp(UN[u].rotulo, "sem")) return unidade_por_rotulo(D_TEMPO, "d");
	if (!strcmp(UN[u].rotulo, "d")) return unidade_por_rotulo(D_TEMPO, "h");
	if (!strcmp(UN[u].rotulo, "h")) return unidade_por_rotulo(D_TEMPO, "min");
	if (!strcmp(UN[u].rotulo, "min")) return unidade_por_rotulo(D_TEMPO, "s");
	return -1;
}

/* número, com unidade se tiver: "5 km", "3h20", "3 horas e 20 min" */
static bool numero(struct ctx *c, struct val *r) {
	const struct ficha *f = olhar(c);
	*r = (struct val){ .dim = D_ESC, .un = -1 };
	r->v = f->v;
	c->i++;
	int u = ler_unidade(c);
	if (u < 0) {
		return true;
	}
	r->dim = UN[u].dim;
	r->un = u;
	r->v = r->v * UN[u].fator + UN[u].desloc;
	/* mais pedaços da mesma grandeza: "3h20", "3 h 20 min", "5 km e 300 m" */
	while (UN[r->un].dim != D_TEMP) {
		bool com_e = eh_id(c, "e") && c->f[c->i + 1].t == T_NUM;
		if (!(com_e || olhar(c)->t == T_NUM)) {
			break;
		}
		int salvo = c->i;
		if (com_e) {
			c->i++;
		}
		num n = olhar(c)->v;
		c->i++;
		int u2 = ler_unidade(c);
		if (u2 < 0) {
			/* sem unidade: só vale para o tempo, na unidade logo abaixo ("3h20" = 3 h 20 min) e sem operador no meio */
			u2 = unidade_menor(u);
			if (u2 < 0 || com_e) {
				c->i = salvo;
				break;
			}
			r->composto = true;
		} else if (UN[u2].dim != r->dim) {
			c->i = salvo;
			break;
		} else {
			r->composto = r->composto || r->dim == D_TEMPO;
		}
		r->v += n * UN[u2].fator;
		u = u2;
	}
	return true;
}

static bool primario(struct ctx *c, struct val *r) {
	const struct ficha *f = olhar(c);
	if (f->t == T_FIM) {
		return falta(c, "um número");
	}
	if (f->t == T_NUM) {
		return numero(c, r);
	}
	if (f->t == T_OP && f->op == '(') {
		c->i++;
		if (!expr(c, r)) {
			return false;
		}
		if (!eh_op(c, ')')) {
			return olhar(c)->t == T_FIM ? falta(c, "fechar o parêntese") : erro(c, "esperava “)”");
		}
		c->i++;
		r->pct = false;
		return true;
	}
	if (f->t == T_ID) {
		if (!strcmp(f->s, "pi")) {
			c->i++;
			*r = (struct val){ .v = 3.14159265358979323846264338327950288L, .dim = D_ESC, .un = -1 };
			return true;
		}
		if (!strcmp(f->s, "raiz") || !strcmp(f->s, "sqrt")) {
			c->i++;
			struct val x;
			bool par = eh_op(c, '(');
			if (par) {
				c->i++;
				if (!expr(c, &x)) {
					return false;
				}
				if (!eh_op(c, ')')) {
					return olhar(c)->t == T_FIM ? falta(c, "fechar o parêntese") : erro(c, "esperava “)”");
				}
				c->i++;
			} else {
				/* "√81": o número logo depois, sem conta no meio */
				if (olhar(c)->t != T_NUM && !(olhar(c)->t == T_ID && !strcmp(olhar(c)->s, "pi"))) {
					return olhar(c)->t == T_FIM ? falta(c, "o número da raiz") : erro(c, "a raiz pede um número");
				}
				if (!primario(c, &x)) {
					return false;
				}
			}
			if (x.dim != D_ESC) {
				return erro(c, "a raiz é de um número, sem unidade");
			}
			if (x.v < 0) {
				return erro(c, "raiz de número negativo");
			}
			*r = (struct val){ .v = sqrtl(x.v), .dim = D_ESC, .un = -1 };
			return true;
		}
		return erro(c, "não entendi “%s”", f->s);
	}
	if (f->t == T_OP) {
		return erro(c, "“%c” não cabe aqui", f->op);
	}
	return erro(c, "não entendi a conta");
}

static bool pos_fixo(struct ctx *c, struct val *r) {
	if (!primario(c, r)) {
		return false;
	}
	if (eh_op(c, '%')) {
		if (r->dim != D_ESC) {
			return erro(c, "porcentagem de um valor com unidade: escreva “N%% de ...”");
		}
		c->i++;
		r->v /= 100;
		r->pct = true;
	}
	return true;
}

static bool unario(struct ctx *c, struct val *r);

static bool potencia(struct ctx *c, struct val *r) {
	if (!pos_fixo(c, r)) {
		return false;
	}
	if (eh_op(c, '^')) {
		c->i++;
		struct val e;
		if (!unario(c, &e)) {
			return false;
		}
		if (r->dim != D_ESC || e.dim != D_ESC) {
			return erro(c, "potência só de números sem unidade");
		}
		num p = powl(r->v, e.v);
		if (isnan(p)) {
			return erro(c, "essa potência não dá um número real");
		}
		*r = (struct val){ .v = p, .dim = D_ESC, .un = -1 };
	}
	return true;
}

static bool unario(struct ctx *c, struct val *r) {
	if (eh_op(c, '-') || eh_op(c, '+')) {
		bool neg = eh_op(c, '-');
		c->i++;
		if (!unario(c, r)) {
			return false;
		}
		if (neg) {
			if (r->dim == D_TEMP) { /* o sinal vale para o número que se vê (-40 °C), não para o kelvin */
				num x = (r->v - UN[r->un].desloc) / UN[r->un].fator;
				r->v = -x * UN[r->un].fator + UN[r->un].desloc;
			} else {
				r->v = -r->v;
			}
		}
		return true;
	}
	return potencia(c, r);
}

static bool inicio_de_operando(struct ctx *c) {
	const struct ficha *f = olhar(c);
	return f->t == T_NUM || (f->t == T_OP && f->op == '(') || (f->t == T_ID && (!strcmp(f->s, "pi") || !strcmp(f->s, "raiz") || !strcmp(f->s, "sqrt")));
}

static bool termo(struct ctx *c, struct val *r) {
	if (!unario(c, r)) {
		return false;
	}
	for (;;) {
		const struct ficha *f = olhar(c);
		char op = 0;
		if (f->t == T_OP && (f->op == '*' || f->op == '/')) {
			op = f->op;
			c->i++;
		} else if (f->t == T_ID && (!strcmp(f->s, "de") || !strcmp(f->s, "x") || !strcmp(f->s, "vezes"))) {
			/* "15% de 89,90", "3 x 4", "3 vezes 4" */
			op = '*';
			c->i++;
		} else if (inicio_de_operando(c) && f->t == T_OP && f->op == '(') {
			op = '*'; /* 2(3 + 4) */
		} else if (f->t == T_ID && !strcmp(f->s, "mod")) {
			c->i++;
			struct val d;
			if (!unario(c, &d)) {
				return false;
			}
			if (r->dim != D_ESC || d.dim != D_ESC) {
				return erro(c, "resto só entre números sem unidade");
			}
			if (d.v == 0) {
				return erro(c, "divisão por zero");
			}
			*r = (struct val){ .v = fmodl(r->v, d.v), .dim = D_ESC, .un = -1 };
			continue;
		} else {
			break;
		}
		struct val d;
		if (!unario(c, &d)) {
			return false;
		}
		if (op == '*') {
			if (r->dim != D_ESC && d.dim != D_ESC) {
				return erro(c, "não sei multiplicar duas grandezas com unidade");
			}
			if (r->dim == D_TEMP || d.dim == D_TEMP) {
				return erro(c, "temperatura só converte, não multiplica");
			}
			bool composto = r->composto || d.composto;
			if (r->dim == D_ESC) {
				struct val t = d;
				t.v = r->v * d.v;
				t.pct = false;
				*r = t;
			} else {
				r->v *= d.v;
			}
			r->composto = composto;
		} else {
			if (d.dim != D_ESC && r->dim == D_ESC) {
				return erro(c, "não dá para dividir um número por uma grandeza com unidade");
			}
			if (d.v == 0) {
				return erro(c, "divisão por zero");
			}
			if (r->dim == D_TEMP || d.dim == D_TEMP) {
				return erro(c, "temperatura só converte, não divide");
			}
			if (r->dim != D_ESC && d.dim != D_ESC) {
				if (r->dim != d.dim) {
					return erro(c, "não sei dividir %s por %s", UN[r->un].rotulo, UN[d.un].rotulo);
				}
				*r = (struct val){ .v = r->v / d.v, .dim = D_ESC, .un = -1 };
			} else {
				r->v /= d.v;
			}
		}
		r->pct = false;
	}
	return true;
}

static bool expr(struct ctx *c, struct val *r) {
	if (!termo(c, r)) {
		return false;
	}
	for (;;) {
		if (!(eh_op(c, '+') || eh_op(c, '-'))) {
			break;
		}
		char op = olhar(c)->op;
		c->i++;
		struct val d;
		if (!termo(c, &d)) {
			return false;
		}
		if (r->dim == D_TEMP || d.dim == D_TEMP) {
			return erro(c, "temperatura só converte, não soma");
		}
		if (d.pct && r->pct) {
			/* "50% + 25%" é 75%: as duas são porcentagens, soma direta */
			r->v = op == '+' ? r->v + d.v : r->v - d.v;
			continue;
		}
		if (d.pct) {
			/* "200 + 10%" é 200 + 10% de 200 */
			if (r->dim != D_ESC && d.dim != D_ESC) {
				return erro(c, "conta estranha com porcentagem");
			}
			d.v = r->v * d.v;
			d.dim = r->dim;
			d.un = r->un;
		}
		if (r->dim != d.dim) {
			if (r->dim == D_ESC || d.dim == D_ESC) {
				return erro(c, "não dá para %s um número sem unidade e outro com unidade", op == '+' ? "somar" : "subtrair");
			}
			return erro(c, "não dá para %s %s e %s", op == '+' ? "somar" : "subtrair", UN[r->un].rotulo, UN[d.un].rotulo);
		}
		r->v = op == '+' ? r->v + d.v : r->v - d.v;
		r->pct = false;
		r->composto = r->composto || d.composto;
	}
	return true;
}

/* ---- o resultado como texto ---- */

static void agrupar_milhar(const char *inteiro, char *saida, size_t tam) {
	size_t n = strlen(inteiro), o = 0;
	for (size_t i = 0; i < n && o + 2 < tam; i++) {
		if (i && (n - i) % 3 == 0) {
			saida[o++] = '.';
		}
		saida[o++] = inteiro[i];
	}
	saida[o] = '\0';
}

/* 12 algarismos úteis no máximo; com pelo menos `casas_min` casas ("89,90 + 39,90" = "129,80"); vírgula decimal e ponto de milhar */
static void formatar(num x, int casas_min, char *saida, size_t tam) {
	if (fabsl(x) < 1e-15L) {
		x = 0;
	}
	char buf[128];
	if (fabsl(x) >= 1e15L || (x != 0 && fabsl(x) < 1e-9L)) {
		snprintf(buf, sizeof(buf), "%.9Le", x);
		/* "1.500000000e+20" → "1,5e+20" */
		char *e = strchr(buf, 'e');
		char mant[128], exp[32];
		snprintf(exp, sizeof(exp), "%s", e);
		*e = '\0';
		snprintf(mant, sizeof(mant), "%s", buf);
		if (strchr(mant, '.')) {
			char *p = mant + strlen(mant) - 1;
			while (*p == '0') {
				*p-- = '\0';
			}
			if (*p == '.') {
				*p = '\0';
			}
		}
		for (char *p = mant; *p; p++) {
			if (*p == '.') {
				*p = ',';
			}
		}
		snprintf(saida, tam, "%se%s", mant, exp + 1);
		return;
	}
	snprintf(buf, sizeof(buf), "%.10Lf", x);
	char *ponto = strchr(buf, '.');
	if (ponto) {
		char *fim = buf + strlen(buf) - 1;
		int casas = (int)(fim - ponto);
		while (casas > (casas_min >= 2 ? casas_min : 0) && *fim == '0') {
			*fim-- = '\0';
			casas--;
		}
		if (casas == 0) {
			*ponto = '\0';
		}
	}
	bool neg = buf[0] == '-';
	char *corpo = buf + (neg ? 1 : 0);
	char decimais[64] = "", inteiro[64];
	char *pt = strchr(corpo, '.');
	if (pt) {
		snprintf(decimais, sizeof(decimais), ",%s", pt + 1);
		*pt = '\0';
	}
	snprintf(inteiro, sizeof(inteiro), "%s", corpo);
	char agrup[96];
	agrupar_milhar(inteiro, agrup, sizeof(agrup));
	bool zero = !strcmp(inteiro, "0") && (!decimais[0] || strspn(decimais + 1, "0") == strlen(decimais + 1));
	snprintf(saida, tam, "%s%s%s", neg && !zero ? "-" : "", agrup, decimais);
}

/* "13 h 20 min": o tempo em partes (d, h, min, s) */
static void formatar_composto(num seg, char *saida, size_t tam) {
	bool neg = seg < 0;
	if (neg) {
		seg = -seg;
	}
	seg = roundl(seg * 1000) / 1000;
	static const struct { const char *r; num s; } P[] = { { "d", 86400 }, { "h", 3600 }, { "min", 60 }, { "s", 1 } };
	char o[256] = "";
	for (int i = 0; i < 4; i++) {
		num q = floorl(seg / P[i].s);
		if (i == 3) {
			q = seg;
		}
		if (q > 0) {
			char n[48];
			formatar(q, 0, n, sizeof(n));
			size_t l = strlen(o);
			snprintf(o + l, sizeof(o) - l, "%s%s %s", l ? " " : "", n, P[i].r);
		}
		seg -= floorl(seg / P[i].s) * P[i].s;
		if (i == 3) {
			break;
		}
	}
	if (!o[0]) {
		strcpy(o, "0 s");
	}
	snprintf(saida, tam, "%s%s", neg ? "-" : "", o);
}

/* ---- a conta inteira ---- */

enum calc_estado calc_avaliar(const char *entrada, struct calc_res *saida) {
	memset(saida, 0, sizeof(*saida));
	struct ctx *c = calloc(1, sizeof(*c));
	if (!c) {
		snprintf(saida->erro, sizeof(saida->erro), "sem memória");
		return CALC_ERRO;
	}
	enum calc_estado estado = CALC_ERRO;
	if (!fichas(c, entrada)) {
		goto fim;
	}
	if (c->n == 0) {
		estado = CALC_VAZIO;
		goto fim;
	}
	struct val r;
	if (!expr(c, &r)) {
		goto fim;
	}
	int alvo = -1;
	if (eh_id(c, "em") || eh_id(c, "para") || eh_id(c, "to")) {
		c->i++;
		if (r.dim == D_ESC) {
			erro(c, "para converter, o número precisa de uma unidade");
			goto fim;
		}
		if (olhar(c)->t == T_FIM) {
			falta(c, "a unidade");
			goto fim;
		}
		alvo = ler_unidade(c);
		if (alvo < 0) {
			erro(c, "não conheço a unidade “%s”", olhar(c)->s);
			goto fim;
		}
		if (UN[alvo].dim != r.dim) {
			erro(c, "não converte %s em %s", UN[r.un].rotulo, UN[alvo].rotulo);
			goto fim;
		}
		r.composto = false;
	}
	if (olhar(c)->t != T_FIM) {
		const struct ficha *f = olhar(c);
		if (f->t == T_OP) {
			erro(c, "“%c” não cabe aqui", f->op);
		} else if (f->t == T_ID) {
			erro(c, "não entendi “%s”", f->s);
		} else {
			erro(c, "falta um operador antes de %s", "outro número");
		}
		goto fim;
	}
	if (!isfinite((double)r.v)) {
		erro(c, "resultado grande demais");
		goto fim;
	}
	if (r.dim == D_ESC) {
		if (r.pct) {
			char n[96];
			formatar(r.v * 100, c->casas_min > 2 ? c->casas_min - 2 : 0, n, sizeof(n));
			snprintf(saida->numero, sizeof(saida->numero), "%s", n);
			snprintf(saida->texto, sizeof(saida->texto), "%s%%", n);
			saida->valor = (double)(r.v * 100);
		} else {
			formatar(r.v, c->casas_min, saida->numero, sizeof(saida->numero));
			snprintf(saida->texto, sizeof(saida->texto), "%s", saida->numero);
			saida->valor = (double)r.v;
		}
	} else {
		int u = alvo >= 0 ? alvo : r.un;
		if (r.dim == D_TEMPO && r.composto && alvo < 0) {
			formatar_composto(r.v, saida->texto, sizeof(saida->texto));
			formatar(r.v / UN[u].fator, 0, saida->numero, sizeof(saida->numero));
			saida->valor = (double)(r.v / UN[u].fator);
		} else {
			num x = (r.v - UN[u].desloc) / UN[u].fator;
			formatar(x, c->casas_min, saida->numero, sizeof(saida->numero));
			snprintf(saida->texto, sizeof(saida->texto), "%s %s", saida->numero, UN[u].rotulo);
			saida->valor = (double)x;
		}
	}
	estado = CALC_OK;
fim:
	if (estado == CALC_ERRO) {
		snprintf(saida->erro, sizeof(saida->erro), "%s", c->erro[0] ? c->erro : "não entendi a conta");
		if (c->incompleto) {
			estado = CALC_INCOMPLETO;
		}
	}
	free(c);
	return estado;
}

void calc_embelezar(const char *entrada, char *saida, size_t tam) {
	size_t o = 0, n = strlen(entrada);
	for (size_t i = 0; i < n && o + 8 < tam; i++) {
		char ch = entrada[i];
		bool mult = ch == '*';
		bool div = ch == '/' && !(i > 0 && eh_letra((unsigned char)entrada[i - 1])) && !(i + 1 < n && eh_letra((unsigned char)entrada[i + 1]));
		if (mult || div) {
			/* " × " e " ÷ " com um espaço de cada lado (sem dobrar os que já existem) */
			if (o > 0 && saida[o - 1] != ' ') {
				saida[o++] = ' ';
			}
			memcpy(saida + o, mult ? "\xC3\x97" : "\xC3\xB7", 2);
			o += 2;
			if (i + 1 < n && entrada[i + 1] != ' ') {
				saida[o++] = ' ';
			}
		} else {
			saida[o++] = ch;
		}
	}
	saida[o] = '\0';
}
