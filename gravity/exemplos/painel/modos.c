/*
 * Telas e modos → Modos: o modo ao ligar, se cada tela tem o seu modo ou todas trocam juntas, e o
 * que cada modo faz (esconder a dock, não perturbe). Vai para ~/.config/lumna/modos.ini no Aplicar
 * (servicos/modos.h); o layout também vale na hora (orbit-msg modo_layout), e a dock e os avisos
 * leem o arquivo.
 */
#include "painel.h"

#include <stdio.h>
#include <string.h>

#include "modos.h"

static struct modos_cfg cfg, gravada;

void pn_modos_abrir(void) {
	modos_cfg_ler(&gravada);
	cfg = gravada;
}

bool pn_modos_pendente(void) {
	return memcmp(&cfg, &gravada, sizeof(cfg)) != 0;
}

void pn_modos_aplicar(void) {
	/* a quantidade de áreas é da seção Áreas de trabalho: o que ela gravou vale */
	struct modos_cfg atual;
	modos_cfg_ler(&atual);
	cfg.areas = gravada.areas = atual.areas;
	if (modos_cfg_gravar(&cfg)) {
		if (cfg.estender != gravada.estender) {
			modos_definir_layout(cfg.estender);
		}
		gravada = cfg;
	}
}

void pn_modos_descartar(void) {
	cfg = gravada;
}

void pn_modos_restaurar(void) {
	struct modos_cfg padrao;
	memset(&padrao, 0, sizeof(padrao));
	padrao.areas = cfg.areas;
	for (int i = 1; i < MODO_N; i++) {
		padrao.m[i].esconder_dock = padrao.m[i].nao_perturbe = true;
	}
	cfg = padrao;
}

void pn_modos_desenhar(void) {
	struct modos_estado e;
	modos_ler(&e);
	/* o modo de agora, em cada tela */
	CLAY(CLAY_ID("md-agora"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 2, 6, 14 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		if (!e.orbit) {
			gv_texto(CLAY_STRING("O Orbit não respondeu: o modo de agora não aparece (abra o Painel dentro do desktop do LUMNA)."), gv_t.tamanho_corpo - 1,
				gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		for (int i = 0; i < e.n; i++) {
			CLAY_AUTO_ID({ .layout = { .padding = { 12, 12, 6, 6 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_alfa(gv_t.acento, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
				.border = { .color = gv_alfa(gv_t.acento, 0.4f), .width = GV_BORDA(1) } }) {
				gv_icone(modos_icone(e.saidas[i].modo), 18, gv_t.acento);
				gv_texto(gv_str(gv_fmt("%s · %s", e.saidas[i].nome, modos_nome(e.saidas[i].modo)).chars), gv_t.tamanho_corpo - 1, gv_t.texto,
					GV_PESO_NORMAL);
			}
		}
	}
	pn_subtitulo(CLAY_ID("md-s1"), "Ao ligar e entre telas");
	const char *nomes[MODO_N];
	for (int i = 0; i < MODO_N; i++) {
		nomes[i] = modos_nome(i);
	}
	pn_ajuste_lista(CLAY_ID("md-ligar"), "power", "Modo ao ligar", "O modo em que o desktop abre", nomes, MODO_N, &cfg.ao_ligar);
	pn_linha_comecar(CLAY_ID("md-layout"), "screens", "Estender o modo às telas", "Desligado: cada tela tem o seu modo (Desktop numa, Gaming noutra)");
	gv_interruptor(CLAY_ID("md-layout-v"), &cfg.estender);
	pn_linha_acabar();
	for (int m = 0; m < MODO_N; m++) {
		pn_subtitulo(CLAY_IDI("md-t", (uint32_t)m), gv_fmt("Modo %s", modos_nome(m)).chars);
		if (m == MODO_DESKTOP) {
			gv_texto(CLAY_STRING("O modo de todo dia: janelas livres, dock e avisos como estão."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			continue;
		}
		pn_linha_comecar(CLAY_IDI("md-dock", (uint32_t)m), "dock", "Esconder a dock", "A dock some enquanto este modo estiver ligado");
		gv_interruptor(CLAY_IDI("md-dock-v", (uint32_t)m), &cfg.m[m].esconder_dock);
		pn_linha_acabar();
		pn_linha_comecar(CLAY_IDI("md-dnd", (uint32_t)m), "bell", "Não perturbe", "Só os avisos urgentes aparecem");
		gv_interruptor(CLAY_IDI("md-dnd-v", (uint32_t)m), &cfg.m[m].nao_perturbe);
		pn_linha_acabar();
	}
}
