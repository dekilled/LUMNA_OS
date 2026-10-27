/*
 * Sistema → Data e hora: acertar o relógio (automático pela internet ou à mão), o fuso horário e como
 * as horas aparecem (24 ou 12 horas). Quem muda a hora e o fuso é o timedatectl (o systemd pede a
 * senha pelo polkit); o formato vai para ~/.config/lumna/hora.ini, que a barra e a tela de bloqueio leem
 * (servicos/hora.h).
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hora.h"

/* ---- o que o systemd sabe ---- */

static struct {
	char fuso[64];
	bool ntp, pode_ntp, sincronizado;
} tdc;
static bool tdc_lido;

static void tdc_ler(void) {
	tdc_lido = true;
	memset(&tdc, 0, sizeof(tdc));
	char *argv[] = { "timedatectl", "show", "--property=Timezone", "--property=NTP", "--property=CanNTP", "--property=NTPSynchronized", NULL };
	char *saida = NULL;
	if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL) || !saida) {
		g_free(saida);
		return;
	}
	char **linhas = g_strsplit(saida, "\n", -1);
	for (char **l = linhas; *l; l++) {
		char *igual = strchr(*l, '=');
		if (!igual) {
			continue;
		}
		*igual = '\0';
		const char *v = igual + 1;
		if (!strcmp(*l, "Timezone")) {
			g_strlcpy(tdc.fuso, v, sizeof(tdc.fuso));
		} else if (!strcmp(*l, "NTP")) {
			tdc.ntp = !strcmp(v, "yes");
		} else if (!strcmp(*l, "CanNTP")) {
			tdc.pode_ntp = !strcmp(v, "yes");
		} else if (!strcmp(*l, "NTPSynchronized")) {
			tdc.sincronizado = !strcmp(v, "yes");
		}
	}
	g_strfreev(linhas);
	g_free(saida);
}

/* ---- um comando do timedatectl, numa tarefa (a senha pode demorar) ---- */

struct pedido {
	char *args[5];
	int ok;
	char erro[240];
};

static char erro_ultimo[240];
static bool ocupado;

static void pedido_trabalho(void *dados) {
	struct pedido *p = dados;
	char *argv[8] = { "timedatectl", p->args[0], p->args[1], p->args[2], NULL };
	char *err = NULL;
	int estado = 0;
	bool rodou = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, NULL);
	p->ok = rodou && WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
	g_strlcpy(p->erro, err ? g_strstrip(err) : "", sizeof(p->erro));
	g_free(err);
}

static void pedido_pronto(void *dados) {
	struct pedido *p = dados;
	g_strlcpy(erro_ultimo, p->ok ? "" : p->erro[0] ? p->erro : "Não deu para fazer. Precisa de permissão de administrador.", sizeof(erro_ultimo));
	tdc_ler();
	ocupado = false;
	for (int i = 0; i < 3; i++) {
		g_free(p->args[i]);
	}
	g_free(p);
}

static void pedir(const char *a, const char *b, const char *c) {
	struct pedido *p = g_new0(struct pedido, 1);
	p->args[0] = g_strdup(a);
	p->args[1] = b ? g_strdup(b) : NULL;
	p->args[2] = c ? g_strdup(c) : NULL;
	ocupado = true;
	erro_ultimo[0] = '\0';
	pn_tarefa(pedido_trabalho, pedido_pronto, p);
}

/* ---- Data e hora ---- */

static char campo_data[16], campo_hora[8];

static void hora_abrir(void) {
	tdc_ler();
	time_t agora = time(NULL);
	struct tm t;
	localtime_r(&agora, &t);
	strftime(campo_data, sizeof(campo_data), "%Y-%m-%d", &t);
	strftime(campo_hora, sizeof(campo_hora), "%H:%M", &t);
}

void pn_hora_abrir(void) {
	hora_abrir();
}

void pn_hora_desenhar(void) {
	if (!tdc_lido) {
		hora_abrir();
	}
	static const char *const dias[] = { "domingo", "segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira", "sábado" };
	static const char *const meses[] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro", "outubro", "novembro", "dezembro" };
	time_t agora = time(NULL);
	struct tm t;
	localtime_r(&agora, &t);
	char txt[24];
	hora_texto(&t, true, txt, sizeof(txt));
	CLAY(CLAY_ID("dh-relogio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 20 }, .childGap = 4 } }) {
		gv_texto(gv_str(txt), 44, gv_t.texto, GV_PESO_MEDIO);
		gv_texto(gv_fmt("%s, %d de %s de %d", dias[t.tm_wday], t.tm_mday, meses[t.tm_mon], t.tm_year + 1900), gv_t.tamanho_corpo + 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(gv_fmt("Fuso: %s", tdc.fuso[0] ? tdc.fuso : "desconhecido"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	gv_acordar_em(1.0);
	pn_linha_comecar(CLAY_ID("dh-ntp"), "globe", "Acertar automaticamente pela internet",
		!tdc.pode_ntp ? "Este sistema não tem um serviço de hora (systemd-timesyncd ou chrony)" : tdc.ntp ? (tdc.sincronizado ? "Sincronizado com a internet" : "Ligado, esperando sincronizar") : "Desligado: a hora é a que você definir");
	bool ligado = tdc.ntp;
	if (gv_interruptor(CLAY_ID("dh-ntp-v"), &ligado) && tdc.pode_ntp && !ocupado) {
		pedir("set-ntp", ligado ? "true" : "false", NULL);
	}
	pn_linha_acabar();
	if (!tdc.ntp) {
		pn_subtitulo(CLAY_ID("dh-manual"), "Acertar à mão");
		CLAY(CLAY_ID("dh-campos"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_texto(CLAY_STRING("Data"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY(CLAY_ID("dh-c-data"), { .layout = { .sizing = { CLAY_SIZING_FIXED(170) } } }) {
				gv_campo(CLAY_ID("dh-data"), campo_data, sizeof(campo_data), "AAAA-MM-DD", GV_CAMPO_TEXTO);
			}
			gv_texto(CLAY_STRING("Hora"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY(CLAY_ID("dh-c-hora"), { .layout = { .sizing = { CLAY_SIZING_FIXED(110) } } }) {
				gv_campo(CLAY_ID("dh-hora"), campo_hora, sizeof(campo_hora), "HH:MM", GV_CAMPO_TEXTO);
			}
			int a, m, d, h, mi;
			bool valido = sscanf(campo_data, "%d-%d-%d", &a, &m, &d) == 3 && sscanf(campo_hora, "%d:%d", &h, &mi) == 2 && a >= 2000 && m >= 1 && m <= 12 &&
				d >= 1 && d <= 31 && h >= 0 && h < 24 && mi >= 0 && mi < 60;
			if (gv_botao_ext(CLAY_ID("dh-definir"), &(gv_botao_opcoes){ .icone = "check", .texto = "Definir", .estilo = GV_BOTAO_CHEIO, .desativado = !valido || ocupado }) && valido) {
				char alvo[40];
				snprintf(alvo, sizeof(alvo), "%04d-%02d-%02d %02d:%02d:00", a, m, d, h, mi);
				pedir("set-time", alvo, NULL);
			}
		}
	}
	if (ocupado) {
		gv_texto(CLAY_STRING("Aplicando… confirme a senha se o sistema pedir."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	if (erro_ultimo[0]) {
		gv_texto(gv_str(erro_ultimo), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
	}
}

/* ---- Fuso horário ---- */

#define MAX_FUSOS 700
static char *fusos[MAX_FUSOS];
static int nfusos;
static const char *regioes[48];
static int nregioes;
static int regiao_sel, cidade_sel;
static const char *cidades[MAX_FUSOS];     /* como está no sistema (Sao_Paulo) */
static const char *cidades_txt[MAX_FUSOS]; /* como se mostra (Sao Paulo) */
static int ncidades;
static char fuso_gravado[64];

static void cidades_da_regiao(void) {
	ncidades = 0;
	const char *r = regioes[regiao_sel];
	size_t rl = strlen(r);
	for (int i = 0; i < nfusos; i++) {
		const char *barra = strchr(fusos[i], '/');
		bool tem_regiao = barra != NULL;
		if ((tem_regiao && (size_t)(barra - fusos[i]) == rl && !strncmp(fusos[i], r, rl)) || (!tem_regiao && !strcmp(r, "Outros"))) {
			const char *nome = tem_regiao ? barra + 1 : fusos[i];
			char *vis = g_strdup(nome);
			g_strdelimit(vis, "_", ' ');
			cidades_txt[ncidades] = g_intern_string(vis);
			g_free(vis);
			cidades[ncidades++] = nome;
		}
	}
}

static void fusos_ler(void) {
	for (int i = 0; i < nfusos; i++) {
		g_free(fusos[i]);
	}
	nfusos = nregioes = 0;
	char *argv[] = { "timedatectl", "list-timezones", NULL };
	char *saida = NULL;
	if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL) && saida) {
		char **linhas = g_strsplit(saida, "\n", -1);
		for (char **l = linhas; *l && nfusos < MAX_FUSOS; l++) {
			if (**l) {
				fusos[nfusos++] = g_strdup(*l);
			}
		}
		g_strfreev(linhas);
	}
	g_free(saida);
	/* as regiões, na ordem em que aparecem; o que não tem barra (UTC) vai em "Outros" */
	for (int i = 0; i < nfusos; i++) {
		const char *barra = strchr(fusos[i], '/');
		char r[48];
		if (barra) {
			snprintf(r, sizeof(r), "%.*s", (int)(barra - fusos[i]), fusos[i]);
		} else {
			snprintf(r, sizeof(r), "Outros");
		}
		bool ja = false;
		for (int k = 0; k < nregioes; k++) {
			ja = ja || !strcmp(regioes[k], r);
		}
		if (!ja && nregioes < 48) {
			regioes[nregioes++] = g_intern_string(r);
		}
	}
}

void pn_fuso_abrir(void) {
	tdc_ler();
	fusos_ler();
	g_strlcpy(fuso_gravado, tdc.fuso, sizeof(fuso_gravado));
	regiao_sel = cidade_sel = 0;
	const char *barra = strchr(tdc.fuso, '/');
	for (int k = 0; k < nregioes; k++) {
		bool bate = barra ? (size_t)(barra - tdc.fuso) == strlen(regioes[k]) && !strncmp(tdc.fuso, regioes[k], strlen(regioes[k])) : !strcmp(regioes[k], "Outros");
		if (bate) {
			regiao_sel = k;
		}
	}
	if (nregioes) {
		cidades_da_regiao();
		for (int c = 0; c < ncidades; c++) {
			if (!strcmp(cidades[c], barra ? barra + 1 : tdc.fuso)) {
				cidade_sel = c;
			}
		}
	}
}

static const char *fuso_escolhido(char *buf, size_t tam) {
	if (!nregioes || !ncidades) {
		return "";
	}
	if (!strcmp(regioes[regiao_sel], "Outros")) {
		g_strlcpy(buf, cidades[cidade_sel], tam);
	} else {
		snprintf(buf, tam, "%s/%s", regioes[regiao_sel], cidades[cidade_sel]);
	}
	return buf;
}

bool pn_fuso_pendente(void) {
	char buf[96];
	const char *e = fuso_escolhido(buf, sizeof(buf));
	return *e && strcmp(e, fuso_gravado) != 0;
}

void pn_fuso_aplicar(void) {
	char buf[96];
	const char *e = fuso_escolhido(buf, sizeof(buf));
	if (*e && !ocupado) {
		pedir("set-timezone", e, NULL);
		g_strlcpy(fuso_gravado, e, sizeof(fuso_gravado)); /* se a senha falhar, o erro aparece e o tdc relê */
	}
}

void pn_fuso_descartar(void) {
	pn_fuso_abrir();
}

void pn_fuso_desenhar(void) {
	if (!nregioes) {
		pn_fuso_abrir();
	}
	if (!nregioes) {
		gv_texto(CLAY_STRING("Não consegui ler a lista de fusos (o timedatectl não respondeu)."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	gv_texto(gv_fmt("Agora: %s", fuso_gravado), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	if (pn_ajuste_lista(CLAY_ID("fz-regiao"), "globe", "Região", NULL, regioes, nregioes, &regiao_sel)) {
		cidades_da_regiao();
		cidade_sel = 0;
	}
	pn_ajuste_lista(CLAY_ID("fz-cidade"), "pin", "Cidade", NULL, cidades_txt, ncidades, &cidade_sel);
	if (ocupado) {
		gv_texto(CLAY_STRING("Aplicando… confirme a senha se o sistema pedir."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	if (erro_ultimo[0]) {
		gv_texto(gv_str(erro_ultimo), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
	}
}

/* ---- Formato ---- */

static int formato, formato_gravado; /* 0: 24 horas, 1: 12 horas */

void pn_formato_abrir(void) {
	formato = formato_gravado = hora_24h() ? 0 : 1;
}

bool pn_formato_pendente(void) {
	return formato != formato_gravado;
}

void pn_formato_aplicar(void) {
	char texto[64];
	snprintf(texto, sizeof(texto), "[geral]\nformato = %d\n", formato ? 12 : 24);
	if (pn_gravar("hora.ini", texto)) {
		formato_gravado = formato;
	}
}

void pn_formato_descartar(void) {
	formato = formato_gravado;
}

void pn_formato_restaurar(void) {
	formato = 0;
}

void pn_formato_desenhar(void) {
	static const char *const opcoes[] = { "24 horas", "12 horas" };
	pn_linha_comecar(CLAY_ID("fm-h"), "clock", "Formato da hora", "24 horas, ou 12 com AM e PM, na barra, no bloqueio e no calendário");
	pn_segmentos(CLAY_ID("fm-seg"), opcoes, 2, &formato);
	pn_linha_acabar();
	time_t agora = time(NULL);
	struct tm t;
	localtime_r(&agora, &t);
	char txt[24];
	int era = formato_gravado;
	formato_gravado = formato; /* a prévia usa a escolha, mesmo antes de aplicar */
	bool v24 = formato == 0;
	if (v24) {
		snprintf(txt, sizeof(txt), "%02d:%02d", t.tm_hour, t.tm_min);
	} else {
		snprintf(txt, sizeof(txt), "%d:%02d %s", t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, t.tm_min, t.tm_hour < 12 ? "AM" : "PM");
	}
	formato_gravado = era;
	CLAY(CLAY_ID("fm-previa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 12, 4 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Assim:"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(gv_str(txt), 30, gv_t.texto, GV_PESO_MEDIO);
	}
	CLAY(CLAY_ID("fm-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 10, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("A data e os segundos na barra ficam em Aparência › Barra superior."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (gv_botao(CLAY_ID("fm-ir"), "menu", "Abrir", GV_BOTAO_ELEVADO)) {
			pn_ir("aparencia/barra");
		}
	}
}
