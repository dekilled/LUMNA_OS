/*
 * Rolagem: uma área que corta o que passa dela e rola como num celular.
 *
 * - dedo: arrastar rola (depois de uns pixels no eixo da área, para um toque continuar a ser um
 *   toque); ao soltar segue com inércia; nas pontas estica como elástico e volta;
 * - tocar numa lista a correr só a pára (não toca no item por baixo);
 * - rato: a roda rola suave (a área mais funda sob o ponteiro ganha), e a barra arrasta-se;
 * - a barra fina aparece enquanto rola e some depois.
 *
 * O Clay só corta e desloca os filhos (clip.childOffset); a posição, a inércia e o elástico são
 * daqui. As medidas (a área e o conteúdo) vêm do layout do quadro anterior.
 */
#include "gv_interno.h"

#include <math.h>
#include <string.h>

#define GV_MAX_ROLAGENS 32
#define LIMIAR 8.0f       /* px que o dedo anda antes de a área ficar com ele */
#define TRAVAGEM 2.6f     /* a inércia perde e^-2,6 da velocidade por segundo */
#define MOLA 14.0f        /* quão depressa o elástico volta */
#define BARRA_MARGEM 3.0f
#define BARRA_MIN 32.0f

struct rolagem {
	uint32_t id;
	uint64_t usado;      /* o último quadro em que apareceu */
	float pos[2];        /* quanto já rolou (0 = no início), com o elástico */
	float vel[2];        /* px/s da inércia */
	float alvo[2];       /* a roda anda até aqui, suave */
	bool com_alvo;
	bool apertado;       /* o aperto começou cá dentro */
	bool capturou;       /* o dedo é da rolagem (os filhos já não recebem o toque) */
	bool na_barra;       /* o rato agarrou a barra */
	float aperto[2];     /* onde o dedo desceu */
	float pos_aperto[2]; /* a posição nessa altura */
	float ultimo_ponto[2];
	double ultimo;       /* gv_agora() do último quadro */
	double mexeu;        /* a última vez que rolou (para a barra) */
	float barra;         /* 0..1: a barra a aparecer */
};

static struct rolagem rolagens[GV_MAX_ROLAGENS];
static uint64_t quadro;

/* a área mais funda sob o ponteiro, que fica com a roda deste quadro */
static struct rolagem *dono_da_roda;

static struct rolagem *rolagem_de(uint32_t id) {
	struct rolagem *livre = NULL, *velha = NULL;
	for (int i = 0; i < GV_MAX_ROLAGENS; i++) {
		struct rolagem *r = &rolagens[i];
		if (r->id == id) {
			return r;
		}
		if (!r->id && !livre) {
			livre = r;
		}
		if (!velha || r->usado < velha->usado) {
			velha = r;
		}
	}
	struct rolagem *r = livre ? livre : velha;
	memset(r, 0, sizeof(*r));
	r->id = id;
	return r;
}

/* passar do limite estica cada vez menos (como no iOS) */
static float elastico(float fora, float dimensao) {
	if (dimensao <= 0) {
		return 0;
	}
	float c = 0.55f;
	return (1 - 1 / (fora * c / dimensao + 1)) * dimensao;
}

static float com_elastico(float bruto, float max, float dimensao) {
	if (bruto < 0) {
		return -elastico(-bruto, dimensao);
	}
	if (bruto > max) {
		return max + elastico(bruto - max, dimensao);
	}
	return bruto;
}

static void geometria_barra(const struct rolagem *r, int eixo, Clay_BoundingBox b, float conteudo, float *ini, float *tam) {
	float vista = eixo ? b.height : b.width;
	float max = fmaxf(0, conteudo - vista);
	float trilho = vista - 2 * BARRA_MARGEM;
	float t = fmaxf(BARRA_MIN, trilho * vista / fmaxf(conteudo, 1));
	/* esticando nas pontas a barra encolhe */
	float fora = r->pos[eixo] < 0 ? -r->pos[eixo] : r->pos[eixo] > max ? r->pos[eixo] - max : 0;
	t = fmaxf(BARRA_MIN / 2, t - fora);
	float frac = max > 0 ? fminf(1, fmaxf(0, r->pos[eixo] / max)) : 0;
	*tam = t;
	*ini = BARRA_MARGEM + (trilho - t) * frac;
}

Clay_ElementDeclaration gv_rolagem(Clay_ElementId id, enum gv_rolagem_eixo eixos, Clay_ElementDeclaration d) {
	struct rolagem *r = rolagem_de(id.id);
	r->usado = quadro;
	double agora = gv_agora();
	float dt = r->ultimo > 0 ? (float)fmin(agora - r->ultimo, 0.05) : 1 / 60.0f;
	r->ultimo = agora;
	bool usa[2] = { eixos != GV_ROLAGEM_VERTICAL, eixos != GV_ROLAGEM_HORIZONTAL };

	Clay_ElementData dados = Clay_GetElementData(id);
	Clay_ScrollContainerData sc = Clay_GetScrollContainerData(id);
	Clay_BoundingBox b = dados.found ? dados.boundingBox : (Clay_BoundingBox){ 0 };
	float vista[2] = { b.width, b.height };
	float conteudo[2] = { sc.found ? sc.contentDimensions.width : 0, sc.found ? sc.contentDimensions.height : 0 };
	float max[2];
	for (int e = 0; e < 2; e++) {
		max[e] = usa[e] ? fmaxf(0, conteudo[e] - vista[e]) : 0;
	}
	float p[2] = { gv__entrada.x, gv__entrada.y };
	bool sobre = dados.found && Clay_PointerOver(id);
	bool animando = false;

	/* ---- aperto ---- */
	if (gv__entrada.apertou && sobre) {
		r->apertado = true;
		r->capturou = false;
		r->na_barra = false;
		bool corria = fabsf(r->vel[0]) + fabsf(r->vel[1]) > 60;
		for (int e = 0; e < 2; e++) {
			r->aperto[e] = r->ultimo_ponto[e] = p[e];
			r->pos_aperto[e] = r->pos[e];
			r->vel[e] = 0;
		}
		r->com_alvo = false;
		/* tocar numa lista a correr só a pára */
		if (corria) {
			r->capturou = true;
			gv__entrada.ativo = id.id;
			gv__entrada.travado = true;
		}
		/* o rato na barra (a faixa de 12 px da borda) */
		if (!gv__entrada.toque && usa[1] && max[1] > 0 && p[0] >= b.x + b.width - 12) {
			r->na_barra = true;
			r->capturou = true;
			gv__entrada.ativo = id.id;
			gv__entrada.travado = true;
		}
	}

	/* ---- arrastar ---- */
	if (r->apertado && gv__entrada.baixo) {
		float delta[2] = { p[0] - r->aperto[0], p[1] - r->aperto[1] };
		if (!r->capturou && gv__entrada.toque) {
			/* fica com o dedo quando ele anda no nosso eixo mais do que no outro: um
			 * deslizante na horizontal dentro de uma lista vertical continua a ser dele */
			for (int e = 0; e < 2; e++) {
				if (usa[e] && max[e] > 0 && fabsf(delta[e]) > LIMIAR && fabsf(delta[e]) >= fabsf(delta[!e])) {
					r->capturou = true;
				}
			}
			if (r->capturou) {
				gv__entrada.ativo = id.id;
				gv__entrada.travado = true;
				/* começa a rolar daqui, sem o salto do limiar */
				for (int e = 0; e < 2; e++) {
					r->aperto[e] = p[e];
				}
				delta[0] = delta[1] = 0;
			}
		}
		if (r->capturou && r->na_barra) {
			float ini, tam;
			geometria_barra(r, 1, b, conteudo[1], &ini, &tam);
			float trilho = b.height - 2 * BARRA_MARGEM - tam;
			if (trilho > 0) {
				r->pos[1] = fminf(max[1], fmaxf(0, r->pos_aperto[1] + delta[1] * max[1] / trilho));
			}
			r->mexeu = agora;
		} else if (r->capturou) {
			for (int e = 0; e < 2; e++) {
				if (!usa[e]) {
					continue;
				}
				r->pos[e] = com_elastico(r->pos_aperto[e] - delta[e], max[e], vista[e]);
				/* a velocidade do dedo, alisada */
				float v = -(p[e] - r->ultimo_ponto[e]) / fmaxf(dt, 0.004f);
				r->vel[e] = r->vel[e] * 0.4f + v * 0.6f;
			}
			r->mexeu = agora;
		}
		r->ultimo_ponto[0] = p[0];
		r->ultimo_ponto[1] = p[1];
	} else if (r->apertado) {
		/* soltou: se o dedo parou antes de soltar, não há inércia */
		if (!r->capturou || r->na_barra || agora - r->mexeu > 0.08) {
			r->vel[0] = r->vel[1] = 0;
		}
		r->apertado = r->capturou = r->na_barra = false;
	}

	/* ---- inércia, roda e elástico ---- */
	if (!r->apertado) {
		for (int e = 0; e < 2; e++) {
			if (!usa[e]) {
				r->pos[e] = r->vel[e] = 0;
				continue;
			}
			float antes = r->pos[e];
			if (r->com_alvo) {
				r->alvo[e] = fminf(max[e], fmaxf(0, r->alvo[e]));
				r->pos[e] += (r->alvo[e] - r->pos[e]) * (1 - expf(-dt * 18));
				if (fabsf(r->alvo[e] - r->pos[e]) < 0.5f) {
					r->pos[e] = r->alvo[e];
				}
			} else {
				r->pos[e] += r->vel[e] * dt;
				bool fora = r->pos[e] < 0 || r->pos[e] > max[e];
				/* fora dos limites a inércia morre depressa e a mola puxa de volta */
				r->vel[e] *= expf(-dt * (fora ? 22 : TRAVAGEM));
				if (fora) {
					float limite = r->pos[e] < 0 ? 0 : max[e];
					r->pos[e] += (limite - r->pos[e]) * (1 - expf(-dt * MOLA));
					if (fabsf(limite - r->pos[e]) < 0.5f && fabsf(r->vel[e]) < 20) {
						r->pos[e] = limite;
						r->vel[e] = 0;
					}
				}
				if (fabsf(r->vel[e]) < 8) {
					r->vel[e] = 0;
				}
			}
			if (fabsf(r->pos[e] - antes) > 0.01f || r->vel[e] != 0) {
				r->mexeu = agora;
				animando = true;
			}
		}
		if (r->com_alvo && !animando) {
			r->com_alvo = false;
		}
	}
	if (sobre && (max[0] > 0 || max[1] > 0)) {
		dono_da_roda = r; /* os de dentro vêm depois e ficam com ela */
	}

	/* ---- a barra ---- */
	bool perto = sobre && !gv__entrada.toque && p[0] >= b.x + b.width - 16;
	bool mostrar = agora - r->mexeu < 0.9 || r->na_barra || perto;
	float alvo_barra = mostrar ? 1 : 0;
	if (fabsf(r->barra - alvo_barra) > 0.01f) {
		r->barra += (alvo_barra - r->barra) * (1 - expf(-dt * (mostrar ? 16 : 6)));
		animando = true;
	} else {
		r->barra = alvo_barra;
	}
	if (mostrar && !animando && r->barra >= 1 && !r->na_barra && !perto) {
		/* acorda para esconder a barra quando parar de rolar */
		gv__app_acordar_em(r->mexeu + 0.92 - agora);
	}
	if (animando) {
		gv__app_pedir();
	}

	struct gv_extra *x = gv__quadro_alloc(sizeof(*x));
	if (x) {
		memset(x, 0, sizeof(*x));
		x->tipo = GV_EXTRA_ROLAGEM;
		x->rolagem.alfa = r->barra;
		x->rolagem.grossa = r->na_barra || perto;
		x->rolagem.cor = gv_t.texto_suave;
		for (int e = 0; e < 2; e++) {
			x->rolagem.tem[e] = usa[e] && max[e] > 0;
			if (x->rolagem.tem[e]) {
				geometria_barra(r, e, b, conteudo[e], &x->rolagem.ini[e], &x->rolagem.tam[e]);
			}
		}
	}
	d.clip.horizontal = usa[0];
	d.clip.vertical = usa[1];
	d.clip.childOffset = (Clay_Vector2){ -roundf(r->pos[0]), -roundf(r->pos[1]) };
	d.userData = x;
	return d;
}

void gv_rolar_para(Clay_ElementId id, float x, float y, bool animado) {
	struct rolagem *r = rolagem_de(id.id);
	r->vel[0] = r->vel[1] = 0;
	r->alvo[0] = fmaxf(0, x);
	r->alvo[1] = fmaxf(0, y);
	if (animado) {
		r->com_alvo = true;
	} else {
		r->pos[0] = r->alvo[0];
		r->pos[1] = r->alvo[1];
		r->com_alvo = false;
	}
	gv__app_pedir();
}

Clay_Vector2 gv_rolagem_posicao(Clay_ElementId id) {
	for (int i = 0; i < GV_MAX_ROLAGENS; i++) {
		if (rolagens[i].id == id.id) {
			return (Clay_Vector2){ rolagens[i].pos[0], rolagens[i].pos[1] };
		}
	}
	return (Clay_Vector2){ 0 };
}

void gv__rolagens_fim_do_quadro(void) {
	quadro++;
	struct rolagem *r = dono_da_roda;
	dono_da_roda = NULL;
	if (!r || (gv__entrada.roda_x == 0 && gv__entrada.roda_y == 0) || r->apertado) {
		return;
	}
	/* a roda soma ao alvo (várias voltas seguidas aceleram) e a área desliza até lá */
	if (!r->com_alvo) {
		r->alvo[0] = r->pos[0];
		r->alvo[1] = r->pos[1];
	}
	/* Shift + roda rola de lado */
	float rx = gv__entrada.roda_x, ry = gv__entrada.roda_y;
	if (gv__entrada.mods & GV_MOD_SHIFT) {
		rx = ry;
		ry = 0;
	}
	r->alvo[0] -= rx * 5;
	r->alvo[1] -= ry * 5;
	r->vel[0] = r->vel[1] = 0;
	r->com_alvo = true;
	gv__app_pedir();
}
