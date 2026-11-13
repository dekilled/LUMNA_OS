/*
 * Aparência → Animações: o que se move no LUMNA. Tudo é escolhido aqui (e vale ao Aplicar):
 *   - as animações dos apps Gravity   (tema.ini [movimento], pela Aparência: estilo.c);
 *   - o deslize ao trocar de espaço ou de modo (~/.config/lumna/animacoes.ini [geral] deslize, que o Orbit lê na hora);
 *   - o carrossel do Super+Tab         (modos.ini [geral] troca = carrossel; desligado, a troca é na hora).
 * O "Nível" é um atalho que liga ou desliga os três de uma vez: Completa (tudo), Simples (sem o carrossel, mais leve) e Desligada (nada se move).
 * As animações novas (janelas, lançador) entram aqui quando existirem.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "modos.h"

bool pn_estilo_animacoes_ler(void);
void pn_estilo_animacoes_gravar(bool ligado);

struct animacoes {
	bool apps, deslize, carrossel;
};

static struct animacoes cfg, gravada;
static int nivel; /* 0 Completa, 1 Simples, 2 Desligada, 3 Personalizada (só mostra) */

static const struct animacoes predefinidas[3] = {
	{ true, true, true },   /* Completa */
	{ true, true, false },  /* Simples: sem o carrossel */
	{ false, false, false } /* Desligada */
};

static int nivel_de(const struct animacoes *c) {
	for (int i = 0; i < 3; i++) {
		if (!memcmp(c, &predefinidas[i], sizeof(*c))) {
			return i;
		}
	}
	return 3;
}

static bool deslize_ler(void) {
	char caminho[1024];
	pn_caminho(caminho, sizeof(caminho), "animacoes.ini");
	char *texto = NULL;
	bool r = true;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char chave[64], valor[64];
			if (sscanf(*l, " %63[^= ] = %63s", chave, valor) == 2 && !strcmp(chave, "deslize")) {
				r = strcmp(valor, "nao") && strcmp(valor, "não") && strcmp(valor, "0");
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	return r;
}

void pn_animacoes_abrir(void) {
	struct modos_cfg m;
	modos_cfg_ler(&m);
	gravada.apps = pn_estilo_animacoes_ler();
	gravada.deslize = deslize_ler();
	gravada.carrossel = m.troca == 1;
	cfg = gravada;
	nivel = nivel_de(&cfg);
}

bool pn_animacoes_pendente(void) {
	return memcmp(&cfg, &gravada, sizeof(cfg)) != 0;
}

void pn_animacoes_aplicar(void) {
	char texto[256];
	snprintf(texto, sizeof(texto), "# O que se move no LUMNA (o Painel > Animações grava este arquivo; o Orbit o lê na hora)\n[geral]\ndeslize = %s\n",
		cfg.deslize ? "sim" : "nao");
	if (!pn_gravar("animacoes.ini", texto)) {
		return;
	}
	struct modos_cfg m;
	modos_cfg_ler(&m);
	m.troca = cfg.carrossel ? 1 : 0;
	modos_cfg_gravar(&m);
	pn_estilo_animacoes_gravar(cfg.apps);
	gravada = cfg;
}

void pn_animacoes_descartar(void) {
	cfg = gravada;
	nivel = nivel_de(&cfg);
}

void pn_animacoes_restaurar(void) {
	cfg = predefinidas[0];
	nivel = 0;
}

void pn_animacoes_desenhar(void) {
	pn_subtitulo(CLAY_ID("an-s0"), "Nível");
	static const char *const niveis[] = { "Completa", "Simples", "Desligada", "Personalizada" };
	int escolha = nivel;
	if (pn_ajuste_lista(CLAY_ID("an-nivel"), "play", "Nível de animação",
		    "Completa usa todos os efeitos; Simples é mais leve (sem o carrossel); Desligada não move nada. Serve para máquinas mais fracas.", niveis,
		    nivel == 3 ? 4 : 3, &escolha) &&
		escolha < 3) {
		cfg = predefinidas[escolha];
		nivel = escolha;
	}
	pn_subtitulo(CLAY_ID("an-s1"), "Ajuste fino");
	bool mudou = false;
	pn_linha_comecar(CLAY_ID("an-apps"), "window", "Animações dos apps", "Transições nos apps e painéis do LUMNA");
	mudou |= gv_interruptor(CLAY_ID("an-apps-v"), &cfg.apps);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("an-deslize"), "stack", "Deslizar ao trocar de espaço ou de modo", "As janelas deslizam; os três espaços do Desktop dão a volta (1, 2, 3 e de novo 1)");
	mudou |= gv_interruptor(CLAY_ID("an-deslize-v"), &cfg.deslize);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("an-carrossel"), "desktop", "Carrossel de modos (Super + Tab)",
		"Mostra os modos em cartões com zoom e solta o Super para entrar. Desligado, o Super + Tab troca na hora");
	mudou |= gv_interruptor(CLAY_ID("an-carrossel-v"), &cfg.carrossel);
	pn_linha_acabar();
	if (mudou) {
		nivel = nivel_de(&cfg);
	}
	gv_texto(CLAY_STRING("Em breve: animação ao abrir janelas e no menu de aplicativos."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
}
