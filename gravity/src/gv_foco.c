/*
 * O foco: um só para a app inteira (campos, botões, interruptores, listas...).
 *
 * - cada componente focável entra na fila (gv__focavel) na ordem em que aparece; Tab e
 *   Shift+Tab andam por ela, dando a volta;
 * - o anel de foco só aparece quando se navega pelo teclado (como o :focus-visible da web): um
 *   aperto do rato ou do dedo foca o que apertou, sem anel; apertar fora de tudo tira o foco;
 * - Enter e Espaço ativam o focado (gv__foco_ativou);
 * - um popup aberto pelo teclado puxa o foco para dentro (para o item marcado, se houver); lá
 *   dentro as setas ↑ ↓ andam como o Tab; quando o popup fecha o foco volta para onde estava.
 */
#include "gv_interno.h"

#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define GV_MAX_FOCAVEIS 256

struct focavel {
	uint32_t id;
	int camada;      /* 0: a superfície principal; 1..: o popup */
	bool preferido;  /* o item a focar quando o popup abre (a opção escolhida) */
};

static struct focavel fila[GV_MAX_FOCAVEIS];
static int nfila;
static uint32_t foco;
static bool visivel;          /* o anel aparece (navegação pelo teclado) */
static uint32_t retorno;      /* para onde volta quando o popup fecha */
static uint32_t apertado;     /* o focável apertado neste quadro (o mais fundo) */
static int ausente;           /* quadros seguidos sem o focado na fila */
static int camada_atual;

uint32_t gv__foco_id(void) {
	return foco;
}

void gv__foco_definir(uint32_t id, bool anel) {
	if (foco != id || visivel != anel) {
		foco = id;
		visivel = anel;
		ausente = 0;
		gv__app_pedir();
	}
}

void gv_focar(Clay_ElementId id) {
	gv__foco_definir(id.id, false);
}

bool gv_focado(Clay_ElementId id) {
	return foco && foco == id.id;
}

void gv__foco_camada(int camada) {
	camada_atual = camada;
}

bool gv__foco_tecla(Clay_ElementId id, uint32_t sym) {
	if (foco != id.id) {
		return false;
	}
	for (int i = 0; i < gv__entrada.nteclas; i++) {
		if (gv__entrada.teclas[i] == sym) {
			memmove(&gv__entrada.teclas[i], &gv__entrada.teclas[i + 1], (gv__entrada.nteclas - i - 1) * sizeof(uint32_t));
			gv__entrada.nteclas--;
			visivel = true;
			return true;
		}
	}
	return false;
}

bool gv__focavel(Clay_ElementId id, bool preferido) {
	if (nfila < GV_MAX_FOCAVEIS) {
		fila[nfila++] = (struct focavel){ id.id, camada_atual, preferido };
	}
	if (gv__entrada.apertou && !gv__entrada.travado && Clay_PointerOver(id)) {
		apertado = id.id; /* os de dentro vêm depois e ganham */
	}
	return foco == id.id;
}

bool gv__foco_ativou(Clay_ElementId id) {
	return gv__foco_tecla(id, XKB_KEY_Return) || gv__foco_tecla(id, XKB_KEY_KP_Enter) || gv__foco_tecla(id, XKB_KEY_space);
}

bool gv_focavel(Clay_ElementId id, float raio) {
	gv__focavel(id, false);
	gv__foco_anel(id, raio);
	return gv__foco_ativou(id);
}

void gv__foco_anel(Clay_ElementId id, float raio) {
	float a = gv_anim(id.id ^ 0x5f0c0u, foco == id.id && visivel ? 1 : 0, gv_t.duracao_rapida);
	if (a <= 0.01f) {
		return;
	}
	/* um contorno 3 px por fora, flutuando sobre o elemento (não mexe no layout) */
	CLAY_AUTO_ID({
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.cornerRadius = CLAY_CORNER_RADIUS(raio + 3),
		.border = { .color = gv_alfa(gv_t.foco, a), .width = GV_BORDA(2) },
		.floating = {
			.attachTo = CLAY_ATTACH_TO_PARENT,
			.attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER },
			.expand = { 3, 3 },
			.zIndex = (int16_t)(camada_atual ? GV_POPUP_Z + camada_atual - 1 : 0),
			.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH,
			.clipTo = CLAY_CLIP_TO_ATTACHED_PARENT,
		},
	}) {}
}

static int achar(uint32_t id) {
	for (int i = 0; i < nfila; i++) {
		if (fila[i].id == id) {
			return i;
		}
	}
	return -1;
}

/* o próximo (ou o anterior) da mesma camada, dando a volta */
static void andar(int camada, int passo) {
	int n = 0, atual = -1;
	int idx[GV_MAX_FOCAVEIS];
	for (int i = 0; i < nfila; i++) {
		if (fila[i].camada == camada) {
			if (fila[i].id == foco) {
				atual = n;
			}
			idx[n++] = i;
		}
	}
	if (!n) {
		return;
	}
	int novo = atual < 0 ? (passo > 0 ? 0 : n - 1) : ((atual + passo) % n + n) % n;
	gv__foco_definir(fila[idx[novo]].id, true);
}

void gv__foco_fim_do_quadro(void) {
	/* o aperto: foca o que apertou (sem anel), ou tira o foco se não apertou nada focável */
	if (gv__entrada.apertou && !gv__entrada.travado) {
		gv__foco_definir(apertado, false);
		retorno = 0;
	}
	apertado = 0;

	/* a camada de cima com focáveis (um popup aberto manda) */
	int topo = 0;
	for (int i = 0; i < nfila; i++) {
		if (fila[i].camada > topo) {
			topo = fila[i].camada;
		}
	}
	int i = achar(foco);
	if (topo > 0 && visivel && (i < 0 || fila[i].camada != topo)) {
		/* um popup abriu pelo teclado: o foco entra nele */
		int escolhido = -1;
		for (int k = 0; k < nfila; k++) {
			if (fila[k].camada == topo && (escolhido < 0 || fila[k].preferido)) {
				escolhido = k;
				if (fila[k].preferido) {
					break;
				}
			}
		}
		if (escolhido >= 0) {
			retorno = foco;
			gv__foco_definir(fila[escolhido].id, true);
			i = escolhido;
		}
	}
	if (foco && i < 0) {
		/* o focado sumiu (um popup fechou): volta para de onde veio; dá um quadro de folga a
		 * quem foi focado antes de aparecer */
		if (retorno && achar(retorno) >= 0) {
			gv__foco_definir(retorno, visivel);
			retorno = 0;
		} else if (++ausente > 1) {
			gv__foco_definir(0, false);
		}
	}

	/* uma tecla de cada vez (várias podem chegar no mesmo quadro) */
	bool shift = gv__entrada.mods & GV_MOD_SHIFT;
	for (int k = 0; k < gv__entrada.nteclas; k++) {
		uint32_t s = gv__entrada.teclas[k];
		if (s == XKB_KEY_ISO_Left_Tab || (s == XKB_KEY_Tab && shift)) {
			andar(topo, -1);
		} else if (s == XKB_KEY_Tab) {
			andar(topo, 1);
		} else if (topo > 0 && s == XKB_KEY_Down) {
			andar(topo, 1);
		} else if (topo > 0 && s == XKB_KEY_Up) {
			andar(topo, -1);
		}
	}
	nfila = 0;
	camada_atual = 0;
}
