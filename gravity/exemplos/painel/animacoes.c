/*
 * Aparência → Animações: o que se move no LUMNA. Tudo é escolhido aqui (e vale ao Aplicar):
 *   - as animações dos apps Gravity   (tema.ini [movimento], pela Aparência: estilo.c);
 *   - o deslize ao trocar de espaço ou de modo (~/.config/lumna/animacoes.ini [geral] deslize, que o Orbit lê na hora);
 *   - abrir e fechar janelas, em animacoes.ini [geral] janelas (o Orbit também lê na hora; jogos nunca animam);
 *   - o carrossel do Super+Tab         (modos.ini [geral] troca = carrossel; desligado, a troca é na hora);
 *   - a animação de abertura, depois do login (animacoes.ini [geral] abertura = sim|nao, e abertura_som: o sussurro da Emma; o lumna-abertura lê).
 * O "Nível" é um atalho que liga ou desliga todos de uma vez: Completa (tudo), Simples (sem o carrossel e sem as janelas animadas, mais leve) e
 * Desligada (nada se move). As animações novas (o lançador) entram aqui quando existirem.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "modos.h"

bool pn_estilo_animacoes_ler(void);
void pn_estilo_animacoes_gravar(bool ligado);

struct animacoes {
	bool apps, deslize, carrossel, janelas, abertura;
};

/* o som da abertura é um ajuste à parte (não faz parte do Nível: dá para ter a animação sem o som e o contrário) */
static bool som, som_gravado;
static struct animacoes cfg, gravada;
static int nivel; /* 0 Completa, 1 Simples, 2 Desligada, 3 Personalizada (só mostra) */

static const struct animacoes predefinidas[3] = {
	{ true, true, true, true, true },    /* Completa */
	{ true, true, false, false, true },  /* Simples: sem o carrossel e sem as janelas animadas (a abertura é a identidade do LUMNA: fica) */
	{ false, false, false, false, false } /* Desligada */
};

static int nivel_de(const struct animacoes *c) {
	for (int i = 0; i < 3; i++) {
		if (!memcmp(c, &predefinidas[i], sizeof(*c))) {
			return i;
		}
	}
	return 3;
}

/* animacoes.ini: [geral] deslize, janelas, abertura e abertura_som (sim por padrão) */
static void ini_ler(bool *deslize, bool *janelas, bool *abertura, bool *abertura_som) {
	*deslize = *janelas = *abertura = *abertura_som = true;
	char caminho[1024];
	pn_caminho(caminho, sizeof(caminho), "animacoes.ini");
	char *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char chave[64], valor[64];
			if (sscanf(*l, " %63[^= ] = %63s", chave, valor) != 2) {
				continue;
			}
			bool sim = strcmp(valor, "nao") && strcmp(valor, "não") && strcmp(valor, "0");
			if (!strcmp(chave, "deslize")) {
				*deslize = sim;
			} else if (!strcmp(chave, "janelas")) {
				*janelas = sim;
			} else if (!strcmp(chave, "abertura")) {
				*abertura = sim;
			} else if (!strcmp(chave, "abertura_som")) {
				*abertura_som = sim;
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
}

void pn_animacoes_abrir(void) {
	struct modos_cfg m;
	modos_cfg_ler(&m);
	gravada.apps = pn_estilo_animacoes_ler();
	ini_ler(&gravada.deslize, &gravada.janelas, &gravada.abertura, &som_gravado);
	gravada.carrossel = m.troca == 1;
	cfg = gravada;
	som = som_gravado;
	nivel = nivel_de(&cfg);
}

bool pn_animacoes_pendente(void) {
	return memcmp(&cfg, &gravada, sizeof(cfg)) != 0 || som != som_gravado;
}

void pn_animacoes_aplicar(void) {
	char texto[384];
	snprintf(texto, sizeof(texto),
		"# O que se move no LUMNA (o Painel > Animações grava este arquivo; o Orbit lê o deslize e as janelas na hora; a abertura é lida no login)\n[geral]\ndeslize = %s\njanelas = %s\nabertura = %s\nabertura_som = %s\n",
		cfg.deslize ? "sim" : "nao", cfg.janelas ? "sim" : "nao", cfg.abertura ? "sim" : "nao", som ? "sim" : "nao");
	if (!pn_gravar("animacoes.ini", texto)) {
		return;
	}
	struct modos_cfg m;
	modos_cfg_ler(&m);
	m.troca = cfg.carrossel ? 1 : 0;
	modos_cfg_gravar(&m);
	pn_estilo_animacoes_gravar(cfg.apps);
	gravada = cfg;
	som_gravado = som;
}

void pn_animacoes_descartar(void) {
	cfg = gravada;
	som = som_gravado;
	nivel = nivel_de(&cfg);
}

void pn_animacoes_restaurar(void) {
	cfg = predefinidas[0];
	som = true;
	nivel = 0;
}

void pn_animacoes_desenhar(void) {
	pn_subtitulo(CLAY_ID("an-s0"), "Nível");
	static const char *const niveis[] = { "Completa", "Simples", "Desligada", "Personalizada" };
	int escolha = nivel;
	if (pn_ajuste_lista(CLAY_ID("an-nivel"), "play", "Nível de animação",
		    "Completa usa todos os efeitos; Simples é mais leve (sem o carrossel e sem animar as janelas); Desligada não move nada. Serve para máquinas mais fracas.", niveis,
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
	pn_linha_comecar(CLAY_ID("an-janelas"), "window", "Abrir e fechar janelas", "A janela nasce um pouco maior e transparente e assenta no tamanho; ao fechar, o contrário. Jogos ficam de fora");
	mudou |= gv_interruptor(CLAY_ID("an-janelas-v"), &cfg.janelas);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("an-carrossel"), "desktop", "Carrossel de modos (Super + Tab)",
		"Mostra os modos em cartões com zoom e solta o Super para entrar. Desligado, o Super + Tab troca na hora");
	mudou |= gv_interruptor(CLAY_ID("an-carrossel-v"), &cfg.carrossel);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("an-abertura"), "play", "Animação de abertura (depois do login)",
		"O logo do LUMNA acende na tela enquanto o desktop termina de abrir. Qualquer tecla ou clique pula. Em todas as telas");
	mudou |= gv_interruptor(CLAY_ID("an-abertura-v"), &cfg.abertura);
	pn_linha_acabar();
	if (mudou) {
		nivel = nivel_de(&cfg);
	}
	pn_linha_comecar(CLAY_ID("an-som"), "volume", "Som da abertura", "O sussurro da Emma. Desligado, a abertura toca em silêncio");
	gv_interruptor(CLAY_ID("an-som-v"), &som);
	pn_linha_acabar();
	gv_texto(CLAY_STRING("Em breve: animação do menu de aplicativos."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
}
