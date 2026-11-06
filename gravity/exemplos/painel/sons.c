/*
 * Som → Sons do sistema: o som dos avisos (ligar e desligar, o volume e qual som para o aviso normal e para o crítico), com um
 * botão de testar. Vai para ~/.config/lumna/sons.ini no Aplicar e o lumna-avisos toca (servicos/sons.h).
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "sons.h"

static struct sons_cfg cfg, gravada;
static char lista[SONS_MAX][40];
static char rotulos[SONS_MAX][64];
static const char *nomes[SONS_MAX];
static int n;
static char aviso[200];

void pn_sons_abrir(void) {
	sons_ler(&gravada);
	cfg = gravada;
	n = sons_listar(lista, SONS_MAX);
	for (int i = 0; i < n; i++) {
		g_strlcpy(rotulos[i], sons_rotulo(lista[i]), sizeof(rotulos[i]));
		nomes[i] = rotulos[i];
	}
	aviso[0] = '\0';
}

bool pn_sons_pendente(void) {
	return memcmp(&cfg, &gravada, sizeof(cfg)) != 0;
}

void pn_sons_aplicar(void) {
	if (sons_gravar(&cfg)) {
		gravada = cfg;
	}
}

void pn_sons_descartar(void) {
	cfg = gravada;
}

void pn_sons_restaurar(void) {
	cfg = (struct sons_cfg){ .avisos = true, .volume = 70 };
	g_strlcpy(cfg.aviso, "message-new-instant", sizeof(cfg.aviso));
	g_strlcpy(cfg.critico, "dialog-warning", sizeof(cfg.critico));
}

static int indice(const char *nome) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(lista[i], nome)) {
			return i;
		}
	}
	return 0;
}

static void nota(const char *id, const char *texto, bool erro) {
	if (texto && *texto) {
		CLAY(CLAY_SID(gv_fmt("%s", id)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 4 } } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, erro ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

void pn_sons_desenhar(void) {
	pn_subtitulo(CLAY_ID("sn-s0"), "Avisos");
	pn_linha_comecar(CLAY_ID("sn-liga"), "bell", "Tocar um som quando chega um aviso", "Os avisos silenciosos e o modo não perturbe ficam sem som");
	gv_interruptor(CLAY_ID("sn-liga-v"), &cfg.avisos);
	pn_linha_acabar();
	if (n == 0) {
		nota("sn-vazio", "Nenhum som encontrado em /usr/share/sounds/freedesktop/stereo. Instale o pacote sound-theme-freedesktop.", true);
		return;
	}
	pn_linha_comecar(CLAY_ID("sn-vol"), "volume", "Volume dos avisos", NULL);
	CLAY(CLAY_ID("sn-vol-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(260) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		float v = cfg.volume / 100.0f;
		if (gv_deslizante(CLAY_ID("sn-vol-d"), &v, 6, 18)) {
			cfg.volume = (int)(v * 100.0f + 0.5f);
		}
		gv_texto(gv_fmt("%d%%", cfg.volume), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	pn_linha_acabar();
	int sa = indice(cfg.aviso), sc = indice(cfg.critico);
	if (pn_ajuste_lista(CLAY_ID("sn-aviso"), "bell", "Som do aviso", "Para os avisos normais", nomes, n, &sa)) {
		g_strlcpy(cfg.aviso, lista[sa], sizeof(cfg.aviso));
		sons_tocar(cfg.aviso, cfg.volume);
	}
	if (pn_ajuste_lista(CLAY_ID("sn-crit"), "shield", "Som do aviso crítico", "Para os avisos que pedem atenção (bateria fraca, erros)", nomes, n, &sc)) {
		g_strlcpy(cfg.critico, lista[sc], sizeof(cfg.critico));
		sons_tocar(cfg.critico, cfg.volume);
	}
	pn_linha_comecar(CLAY_ID("sn-testar"), "play", "Ouvir", "Toca o som do aviso normal no volume de acima");
	if (gv_botao(CLAY_ID("sn-testar-b"), NULL, "Testar", GV_BOTAO_ELEVADO)) {
		g_strlcpy(aviso, sons_tocar(cfg.aviso, cfg.volume) ? "" : "Não deu para tocar (o pw-play ou o paplay estão instalados?).", sizeof(aviso));
	}
	pn_linha_acabar();
	nota("sn-aviso-erro", aviso, true);
}
