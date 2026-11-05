/*
 * Os modelos de IA escrevem setas e símbolos em LaTeX ($\rightarrow$, \times) mesmo em Markdown simples, e na tela aparece o código.
 * latex_simples() troca, no próprio texto, o que é fácil de trocar pelo símbolo (→ ← ⇒ × ≈ ≠ ≤ ≥ … ✓ ° ±): tanto "$\rightarrow$" quanto
 * "\rightarrow" solto. Só mexe em "$...$" que tem um comando conhecido dentro (então "R$ 5 e R$ 10" fica como está) e nunca dentro de
 * código (`assim` ou ```blocos```). O resultado nunca é maior que o original, por isso vale no mesmo buffer.
 */
#ifndef LUMNA_LATEX_H
#define LUMNA_LATEX_H

#include <string.h>

static inline const char *latex_simbolo(const char *nome, size_t n) {
	static const struct { const char *cmd, *sim; } tabela[] = {
		{ "rightarrow", "→" }, { "to", "→" }, { "longrightarrow", "→" }, { "leftarrow", "←" }, { "gets", "←" }, { "Rightarrow", "⇒" },
		{ "implies", "⇒" }, { "Leftarrow", "⇐" }, { "leftrightarrow", "↔" }, { "Leftrightarrow", "⇔" }, { "uparrow", "↑" }, { "downarrow", "↓" },
		{ "times", "×" }, { "cdot", "·" }, { "approx", "≈" }, { "neq", "≠" }, { "ne", "≠" }, { "leq", "≤" }, { "le", "≤" }, { "geq", "≥" },
		{ "ge", "≥" }, { "ldots", "…" }, { "dots", "…" }, { "cdots", "…" }, { "checkmark", "✓" }, { "degree", "°" }, { "pm", "±" },
		{ "infty", "∞" }, { "bullet", "•" },
	};
	for (size_t i = 0; i < sizeof(tabela) / sizeof(tabela[0]); i++) {
		if (strlen(tabela[i].cmd) == n && !strncmp(tabela[i].cmd, nome, n)) {
			return tabela[i].sim;
		}
	}
	return NULL;
}

/* o comando que começa em t[0] == '\\': devolve o símbolo e quanto do texto ele ocupa (0: não é um comando conhecido) */
static inline const char *latex_comando(const char *t, size_t *ocupa) {
	size_t n = 0;
	while ((t[1 + n] >= 'a' && t[1 + n] <= 'z') || (t[1 + n] >= 'A' && t[1 + n] <= 'Z')) {
		n++;
	}
	const char *s = n ? latex_simbolo(t + 1, n) : NULL;
	*ocupa = s ? n + 1 : 0;
	return s;
}

static inline void latex_simples(char *t) {
	char *w = t;
	bool crase = false;
	for (const char *r = t; *r;) {
		if (*r == '`') {
			crase = !crase;
			*w++ = *r++;
			continue;
		}
		if (crase) {
			*w++ = *r++;
			continue;
		}
		if (*r == '$') {
			/* "$ ... $" na mesma linha, curto, com ao menos um comando conhecido e nada desconhecido */
			const char *f = r + 1;
			while (*f && *f != '$' && *f != '\n' && f - r < 48) {
				f++;
			}
			if (*f == '$') {
				char tmp[64];
				size_t m = 0;
				bool achou = false, estranho = false;
				for (const char *p = r + 1; p < f && m + 4 < sizeof(tmp); p++) {
					if (*p == '\\') {
						size_t oc;
						const char *s = latex_comando(p, &oc);
						if (s) {
							size_t l = strlen(s);
							memcpy(tmp + m, s, l);
							m += l;
							p += oc - 1;
							achou = true;
						} else {
							estranho = true;
							break;
						}
					} else {
						tmp[m++] = *p;
					}
				}
				if (achou && !estranho && m < sizeof(tmp)) {
					while (m > 0 && tmp[m - 1] == ' ') {
						m--;
					}
					size_t ini = 0;
					while (ini < m && tmp[ini] == ' ') {
						ini++;
					}
					memcpy(w, tmp + ini, m - ini);
					w += m - ini;
					r = f + 1;
					continue;
				}
			}
		}
		if (*r == '\\') {
			size_t oc;
			const char *s = latex_comando(r, &oc);
			if (s) {
				size_t l = strlen(s);
				memcpy(w, s, l);
				w += l;
				r += oc;
				continue;
			}
		}
		*w++ = *r++;
	}
	*w = '\0';
}

#endif
