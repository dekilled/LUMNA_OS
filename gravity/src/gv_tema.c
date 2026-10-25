/*
 * O tema: as cores, a forma, o texto e o movimento do LUMNA, num sítio só. Um tema.ini muda o
 * que quiser, chave a chave (o que não aparece fica como está):
 *
 *   # ~/.config/gravity/tema.ini
 *   acento = #f472b6        (ou pelo nome do token do design: accent = #f472b6)
 *   raio_grande = 18
 *   fonte = Noto Sans
 *   duracao_normal = 300
 *   curva = mola
 *   animacoes = sim
 *
 * Cores em #RRGGBB ou #RRGGBBAA. Linhas com # ou ; são comentários; [secções] são só arrumação.
 */
#include "gv_interno.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

gv_tema gv_t;

Clay_Color gv_cor(uint32_t rgba) {
	return (Clay_Color){ (rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff, rgba & 0xff };
}

Clay_Color gv_alfa(Clay_Color cor, float alfa) {
	cor.a = alfa * 255;
	return cor;
}

void gv_tema_padrao(gv_tema *t) {
	*t = (gv_tema){
		/* a paleta da UI nova do LUMNA: superfícies neutras (como nas telas novas; o violeta fica
		 * para o acento e o papel de parede) */
		.fundo = gv_cor(0x141518ff),
		.superficie = gv_cor(0x1d1e22ff),
		.elevada = gv_cor(0x25262aff),
		.sob_cursor = gv_cor(0x2d2e33ff),
		.selecionado = gv_cor(0x2b2340ff),
		.borda = gv_cor(0x303238ff),
		.borda_controle = gv_cor(0x4a4c53ff),
		.texto = gv_cor(0xf4f1faff),
		.texto_secundario = gv_cor(0xbcb5ceff),
		.texto_suave = gv_cor(0x8e869fff),
		.acento = gv_cor(0xa78bfaff),
		.acento_cursor = gv_cor(0xb79fffff),
		.acento_premido = gv_cor(0x8f73dcff),
		.sobre_acento = gv_cor(0x160f26ff),
		.foco = gv_cor(0xc4b5fdff),
		.sucesso = gv_cor(0x78c6a3ff),
		.aviso = gv_cor(0xe4bb77ff),
		.perigo = gv_cor(0xe58b98ff),
		.info = gv_cor(0x86b8e8ff),
		.desativado = gv_cor(0x625b70ff),
		.divisor = gv_cor(0x8e869fff),
		.veu = gv_cor(0x0b0c0e99),
		.icone = gv_cor(0xf4f1faff),
		/* cantos menores e superfícies sólidas, como nas telas novas */
		.raio_pequeno = 6,
		.raio_medio = 8,
		.raio_grande = 12,
		.espaco = 4,
		.traco_icone = 1.6f,
		.fonte = "Inter, Noto Sans, sans-serif",
		.tamanho_legenda = 13,
		.tamanho_corpo = 15,
		.tamanho_destaque = 17,
		.tamanho_titulo = 20,
		.duracao_rapida = 120,
		.duracao_normal = 200,
		.duracao_lenta = 320,
		.curva = GV_CURVA_SUAVE,
		.animacoes = true,
	};
}

/* A paleta clara: superfícies claras e texto escuro; o acento fica o que estiver (o tema.ini
 * pode trocá-lo depois desta linha). */
static void paleta_clara(gv_tema *t) {
	t->fundo = gv_cor(0xe9ebefff);
	t->superficie = gv_cor(0xf6f6f8ff);
	t->elevada = gv_cor(0xffffffff);
	t->sob_cursor = gv_cor(0xeceef2ff);
	t->selecionado = gv_cor(0xece6fdff);
	t->borda = gv_cor(0xdcdfe5ff);
	t->borda_controle = gv_cor(0xb8bcc6ff);
	t->texto = gv_cor(0x1b1a22ff);
	t->texto_secundario = gv_cor(0x4c4958ff);
	t->texto_suave = gv_cor(0x777383ff);
	t->sobre_acento = gv_cor(0xffffffff);
	t->desativado = gv_cor(0xaaa7b4ff);
	t->divisor = gv_cor(0x9a96a6ff);
	t->veu = gv_cor(0x1b1a2255);
	t->icone = gv_cor(0x2a2833ff);
	t->sucesso = gv_cor(0x2f8f66ff);
	t->aviso = gv_cor(0xb27a1eff);
	t->perigo = gv_cor(0xc8475aff);
	t->info = gv_cor(0x2f74b8ff);
}

/* O contraste alto: o texto secundário perto do principal e as bordas bem marcadas. */
static void contraste_alto(gv_tema *t) {
	bool claro = t->superficie.r > 128;
	t->texto_secundario = t->texto;
	t->texto_suave = claro ? gv_cor(0x3a3846ff) : gv_cor(0xd8d3e4ff);
	t->borda = claro ? gv_cor(0x8f93a0ff) : gv_cor(0x6a6d76ff);
	t->borda_controle = claro ? gv_cor(0x55596480) : gv_cor(0xa4a7b0ff);
	t->borda_controle.a = 255;
}

static bool ler_cor(const char *texto, Clay_Color *cor) {
	if (texto[0] != '#') {
		return false;
	}
	size_t n = strlen(texto + 1);
	char *fim;
	unsigned long v = strtoul(texto + 1, &fim, 16);
	if (*fim) {
		return false;
	}
	if (n == 6) {
		*cor = gv_cor((uint32_t)(v << 8) | 0xff);
	} else if (n == 8) {
		*cor = gv_cor((uint32_t)v);
	} else {
		return false;
	}
	return true;
}

struct chave_cor {
	const char *nome;
	const char *token; /* o nome no design do LUMNA */
	size_t deslocamento;
};

#define COR(campo, token) { #campo, token, offsetof(gv_tema, campo) }
static const struct chave_cor cores[] = {
	COR(fundo, "bg"), COR(superficie, "surface"), COR(elevada, "raised"), COR(sob_cursor, "hover"),
	COR(selecionado, "selected"), COR(borda, "border"), COR(borda_controle, "border-control"), COR(texto, "text"),
	COR(texto_secundario, "text-secondary"), COR(texto_suave, "text-muted"), COR(acento, "accent"),
	COR(acento_cursor, "accent-hover"), COR(acento_premido, "accent-pressed"), COR(sobre_acento, "on-accent"),
	COR(foco, "focus"), COR(sucesso, "success"), COR(aviso, "warning"), COR(perigo, "danger"), COR(info, "info"),
	COR(desativado, "disabled"), COR(divisor, "grip-idle"), COR(veu, "scrim"), COR(icone, "icon"),
};

#define NUM(campo) { #campo, NULL, offsetof(gv_tema, campo) }
static const struct chave_cor numeros[] = {
	NUM(raio_pequeno), NUM(raio_medio), NUM(raio_grande), NUM(espaco), NUM(traco_icone),
	NUM(tamanho_legenda), NUM(tamanho_corpo), NUM(tamanho_destaque), NUM(tamanho_titulo),
};

static const struct chave_cor inteiros[] = {
	NUM(duracao_rapida), NUM(duracao_normal), NUM(duracao_lenta),
};

static char *aparar(char *s) {
	while (isspace((unsigned char)*s)) {
		s++;
	}
	char *fim = s + strlen(s);
	while (fim > s && isspace((unsigned char)fim[-1])) {
		*--fim = '\0';
	}
	return s;
}

static void aplicar(gv_tema *t, const char *chave, const char *valor, const char *ficheiro, int linha) {
	for (size_t i = 0; i < sizeof(cores) / sizeof(cores[0]); i++) {
		if (!strcmp(chave, cores[i].nome) || !strcmp(chave, cores[i].token)) {
			if (!ler_cor(valor, (Clay_Color *)((char *)t + cores[i].deslocamento))) {
				fprintf(stderr, "gravity: %s:%d: cor %s inválida (use #RRGGBB ou #RRGGBBAA)\n", ficheiro, linha, valor);
			}
			return;
		}
	}
	for (size_t i = 0; i < sizeof(numeros) / sizeof(numeros[0]); i++) {
		if (!strcmp(chave, numeros[i].nome)) {
			*(float *)((char *)t + numeros[i].deslocamento) = strtof(valor, NULL);
			return;
		}
	}
	for (size_t i = 0; i < sizeof(inteiros) / sizeof(inteiros[0]); i++) {
		if (!strcmp(chave, inteiros[i].nome)) {
			*(int *)((char *)t + inteiros[i].deslocamento) = atoi(valor);
			return;
		}
	}
	if (!strcmp(chave, "fonte")) {
		snprintf(t->fonte, sizeof(t->fonte), "%s", valor);
	} else if (!strcmp(chave, "curva")) {
		t->curva = !strcasecmp(valor, "mola") ? GV_CURVA_MOLA
			: !strcasecmp(valor, "linear") ? GV_CURVA_LINEAR : GV_CURVA_SUAVE;
	} else if (!strcmp(chave, "estilo")) {
		/* escuro (o de fábrica), claro, ou automático: claro das 7h às 19h */
		bool claro = !strcasecmp(valor, "claro");
		if (!strcasecmp(valor, "automatico") || !strcasecmp(valor, "automático")) {
			time_t agora = time(NULL);
			struct tm tm;
			localtime_r(&agora, &tm);
			claro = tm.tm_hour >= 7 && tm.tm_hour < 19;
		}
		if (claro) {
			paleta_clara(t);
		}
	} else if (!strcmp(chave, "contraste")) {
		if (!strcasecmp(valor, "alto")) {
			contraste_alto(t);
		}
	} else if (!strcmp(chave, "animacoes")) {
		t->animacoes = !strcasecmp(valor, "sim") || !strcmp(valor, "1") || !strcasecmp(valor, "true");
	} else {
		fprintf(stderr, "gravity: %s:%d: chave %s desconhecida\n", ficheiro, linha, chave);
	}
}

bool gv_tema_carregar(const char *caminho) {
	char padrao[512];
	if (!caminho) {
		caminho = getenv("GRAVITY_TEMA");
	}
	if (!caminho) {
		const char *config = getenv("XDG_CONFIG_HOME");
		const char *casa = getenv("HOME");
		if (config && *config) {
			snprintf(padrao, sizeof(padrao), "%s/gravity/tema.ini", config);
		} else if (casa) {
			snprintf(padrao, sizeof(padrao), "%s/.config/gravity/tema.ini", casa);
		} else {
			return false;
		}
		caminho = padrao;
	}
	FILE *f = fopen(caminho, "r");
	if (!f) {
		return false;
	}
	char linha[512];
	int n = 0;
	while (fgets(linha, sizeof(linha), f)) {
		n++;
		char *s = aparar(linha);
		if (!*s || *s == '#' || *s == ';' || *s == '[') {
			continue;
		}
		char *igual = strchr(s, '=');
		if (!igual) {
			fprintf(stderr, "gravity: %s:%d: falta o =\n", caminho, n);
			continue;
		}
		*igual = '\0';
		aplicar(&gv_t, aparar(s), aparar(igual + 1), caminho, n);
	}
	fclose(f);
	return true;
}
