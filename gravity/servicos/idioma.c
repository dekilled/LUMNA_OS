#define _GNU_SOURCE
#include "idioma.h"

#include <glib.h>
#include <langinfo.h>
#include <locale.h>
#include <monetary.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

static const char *etc(void) {
	const char *e = getenv("LUMNA_ETC");
	return e && *e ? e : "/etc";
}

void idioma_ler(struct idioma_cfg *c) {
	memset(c, 0, sizeof(*c));
	char *caminho = g_strdup_printf("%s/locale.conf", etc()), *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			char *k = g_strstrip(*l), *v = g_strstrip(igual + 1);
			if (*v == '"' && v[strlen(v) - 1] == '"') {
				v[strlen(v) - 1] = '\0';
				v++;
			}
			char *alvo = !strcmp(k, "LANG") ? c->lang : !strcmp(k, "LC_TIME") ? c->tempo : !strcmp(k, "LC_NUMERIC") ? c->numeros
				: !strcmp(k, "LC_MONETARY") ? c->moeda : !strcmp(k, "LC_PAPER") ? c->papel : !strcmp(k, "LC_MEASUREMENT") ? c->medidas : NULL;
			if (alvo) {
				g_strlcpy(alvo, v, 48);
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(caminho);
}

static const struct { const char *cod, *nome; } linguas[] = {
	{ "pt", "Português" }, { "en", "Inglês" }, { "es", "Espanhol" }, { "fr", "Francês" }, { "de", "Alemão" }, { "it", "Italiano" }, { "ru", "Russo" },
	{ "ja", "Japonês" }, { "zh", "Chinês" }, { "ko", "Coreano" }, { "ar", "Árabe" }, { "hi", "Hindi" }, { "nl", "Holandês" }, { "pl", "Polonês" },
	{ "tr", "Turco" }, { "sv", "Sueco" }, { "uk", "Ucraniano" }, { "cs", "Tcheco" }, { "el", "Grego" }, { "he", "Hebraico" }, { "da", "Dinamarquês" },
	{ "fi", "Finlandês" }, { "no", "Norueguês" }, { "nb", "Norueguês" }, { "hu", "Húngaro" }, { "ro", "Romeno" }, { "bg", "Búlgaro" }, { "ca", "Catalão" },
	{ "id", "Indonésio" }, { "vi", "Vietnamita" }, { "th", "Tailandês" }, { "fa", "Persa" }, { "sk", "Eslovaco" }, { "hr", "Croata" }, { "sr", "Sérvio" },
	{ "lt", "Lituano" }, { "lv", "Letão" }, { "et", "Estoniano" }, { "sl", "Esloveno" }, { "eu", "Basco" }, { "gl", "Galego" }, { "is", "Islandês" },
	{ "ga", "Irlandês" }, { "af", "Africâner" }, { "sw", "Suaíli" }, { "bn", "Bengali" }, { "ta", "Tâmil" }, { "ur", "Urdu" }, { "ms", "Malaio" },
};

static const struct { const char *cod, *nome; } paises[] = {
	{ "BR", "Brasil" }, { "PT", "Portugal" }, { "US", "Estados Unidos" }, { "GB", "Reino Unido" }, { "CA", "Canadá" }, { "AU", "Austrália" },
	{ "ES", "Espanha" }, { "MX", "México" }, { "AR", "Argentina" }, { "CL", "Chile" }, { "CO", "Colômbia" }, { "PE", "Peru" }, { "FR", "França" },
	{ "DE", "Alemanha" }, { "AT", "Áustria" }, { "CH", "Suíça" }, { "IT", "Itália" }, { "RU", "Rússia" }, { "JP", "Japão" }, { "CN", "China" },
	{ "TW", "Taiwan" }, { "HK", "Hong Kong" }, { "KR", "Coreia do Sul" }, { "IN", "Índia" }, { "NL", "Países Baixos" }, { "BE", "Bélgica" },
	{ "PL", "Polônia" }, { "TR", "Turquia" }, { "SE", "Suécia" }, { "NO", "Noruega" }, { "DK", "Dinamarca" }, { "FI", "Finlândia" }, { "UA", "Ucrânia" },
	{ "IE", "Irlanda" }, { "NZ", "Nova Zelândia" }, { "ZA", "África do Sul" }, { "AO", "Angola" }, { "MZ", "Moçambique" }, { "EG", "Egito" },
	{ "SA", "Arábia Saudita" }, { "IL", "Israel" }, { "GR", "Grécia" }, { "CZ", "Tchéquia" }, { "HU", "Hungria" }, { "RO", "Romênia" },
};

void idioma_nome(const char *locale, char *saida, size_t tam) {
	char cod[16] = "", pais[16] = "";
	const char *p = locale;
	size_t i = 0;
	while (*p && *p != '_' && *p != '.' && *p != '@' && i < sizeof(cod) - 1) {
		cod[i++] = *p++;
	}
	cod[i] = '\0';
	if (*p == '_') {
		p++;
		i = 0;
		while (*p && *p != '.' && *p != '@' && i < sizeof(pais) - 1) {
			pais[i++] = *p++;
		}
		pais[i] = '\0';
	}
	const char *ln = NULL, *pn = NULL;
	for (size_t k = 0; k < sizeof(linguas) / sizeof(linguas[0]); k++) {
		if (!strcmp(linguas[k].cod, cod)) {
			ln = linguas[k].nome;
		}
	}
	for (size_t k = 0; k < sizeof(paises) / sizeof(paises[0]); k++) {
		if (!strcmp(paises[k].cod, pais)) {
			pn = paises[k].nome;
		}
	}
	if (!ln) {
		g_strlcpy(saida, locale, tam);
	} else if (pn) {
		g_snprintf(saida, tam, "%s (%s)", ln, pn);
	} else if (*pais) {
		g_snprintf(saida, tam, "%s (%s)", ln, pais);
	} else {
		g_strlcpy(saida, ln, tam);
	}
}

static char *saida_de(char **argv) {
	char *saida = NULL;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL)) {
		g_free(saida);
		return NULL;
	}
	return saida;
}

/* "pt_BR.utf8" → "pt_BR.UTF-8" (o locale -a escreve de um jeito, o locale.conf de outro) */
static void normalizar(const char *in, char *out, size_t tam) {
	g_strlcpy(out, in, tam);
	char *p = strstr(out, ".utf8");
	if (p) {
		strcpy(p, ".UTF-8");
	}
}

static int comparar_nomes(const void *a, const void *b) {
	char na[96], nb[96];
	idioma_nome((const char *)a, na, sizeof(na));
	idioma_nome((const char *)b, nb, sizeof(nb));
	return g_utf8_collate(na, nb);
}

int idioma_geradas(char lista[][48], int max) {
	char *argv[] = { "locale", "-a", NULL };
	char *saida = saida_de(argv);
	int n = 0;
	char **linhas = g_strsplit(saida ? saida : "", "\n", -1);
	for (char **l = linhas; *l && n < max; l++) {
		char *t = g_strstrip(*l);
		if (!*t || !strcmp(t, "C") || !strcmp(t, "POSIX") || g_str_has_prefix(t, "C.") || (!strstr(t, ".utf8") && !strstr(t, ".UTF-8"))) {
			continue;
		}
		char norm[48];
		normalizar(t, norm, sizeof(norm));
		bool repetido = false;
		for (int i = 0; i < n; i++) {
			repetido |= !strcmp(lista[i], norm);
		}
		if (!repetido) {
			g_strlcpy(lista[n++], norm, 48);
		}
	}
	g_strfreev(linhas);
	g_free(saida);
	qsort(lista, (size_t)n, 48, comparar_nomes);
	return n;
}

int idioma_suportados(char lista[][48], int max) {
	const char *caminho = getenv("LUMNA_SUPPORTED") ? getenv("LUMNA_SUPPORTED") : "/usr/share/i18n/SUPPORTED";
	char *texto = NULL, (*geradas)[48] = g_malloc((size_t)IDIOMA_MAX * 48);
	int ng = idioma_geradas(geradas, IDIOMA_MAX), n = 0;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l && n < max; l++) {
			char loc[48] = "", cs[24] = "";
			if (sscanf(*l, "%47s %23s", loc, cs) == 2 && !strcmp(cs, "UTF-8") && !strchr(loc, '@')) {
				bool tem = false;
				for (int i = 0; i < ng; i++) {
					tem |= !strcmp(geradas[i], loc);
				}
				if (!tem) {
					g_strlcpy(lista[n++], loc, 48);
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(geradas);
	qsort(lista, (size_t)n, 48, comparar_nomes);
	return n;
}

static bool rodar(char **argv, char *erro, size_t tam) {
	char *err = NULL;
	int estado = 0;
	bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, NULL) &&
		WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
	if (!ok && erro && tam) {
		g_strlcpy(erro, err && *err ? g_strstrip(err) : "não deu (faltou a senha do administrador?)", tam);
	}
	g_free(err);
	return ok;
}

static bool locale_valido(const char *l) {
	if (!*l) {
		return true;
	}
	for (const char *p = l; *p; p++) {
		if (!g_ascii_isalnum(*p) && *p != '_' && *p != '.' && *p != '-' && *p != '@') {
			return false;
		}
	}
	return true;
}

bool idioma_definir(const struct idioma_cfg *c, char *erro, size_t tam) {
	const char *v[6] = { c->lang, c->tempo, c->numeros, c->moeda, c->papel, c->medidas };
	for (int i = 0; i < 6; i++) {
		if (!locale_valido(v[i])) {
			return false;
		}
	}
	char *args[10] = { "localectl", "set-locale", NULL };
	char *itens[6];
	int n = 2;
	static const char *const chaves[6] = { "LANG", "LC_TIME", "LC_NUMERIC", "LC_MONETARY", "LC_PAPER", "LC_MEASUREMENT" };
	for (int i = 0; i < 6; i++) {
		itens[i] = *v[i] ? g_strdup_printf("%s=%s", chaves[i], v[i]) : NULL;
		if (itens[i]) {
			args[n++] = itens[i];
		}
	}
	args[n] = NULL;
	bool ok = n > 2 && rodar(args, erro, tam);
	for (int i = 0; i < 6; i++) {
		g_free(itens[i]);
	}
	return ok;
}

bool idioma_gerar(const char *locale, char *erro, size_t tam) {
	if (!locale || !*locale || !locale_valido(locale)) {
		return false;
	}
	const char *ajudante = getenv("LUMNA_IDIOMA_GERAR") ? getenv("LUMNA_IDIOMA_GERAR") : "/usr/lib/lumna/lumna-idioma-gerar";
	char *argv_pk[] = { "pkexec", (char *)ajudante, (char *)locale, NULL };
	char *argv_direto[] = { (char *)ajudante, (char *)locale, NULL };
	return rodar(getenv("LUMNA_SEM_PKEXEC") ? argv_direto : argv_pk, erro, tam);
}

void idioma_amostra(const char *locale, char *data, char *numero, char *moeda, size_t tam) {
	data[0] = numero[0] = moeda[0] = '\0';
	char nome[64];
	g_strlcpy(nome, locale, sizeof(nome));
	locale_t l = newlocale(LC_ALL_MASK, nome, (locale_t)0);
	if (!l) {
		char *p = strstr(nome, ".UTF-8");
		if (p) {
			strcpy(p, ".utf8");
			l = newlocale(LC_ALL_MASK, nome, (locale_t)0);
		}
	}
	if (!l) {
		return;
	}
	locale_t antes = uselocale(l);
	time_t t0 = 1793000000; /* um dia fixo: 26/10/2026 */
	struct tm tm0;
	gmtime_r(&t0, &tm0);
	strftime(data, tam, "%x", &tm0);
	snprintf(numero, tam, "%'.2f", 1234567.89);
	strfmon(moeda, tam, "%n", 1234.56);
	uselocale(antes);
	freelocale(l);
}
