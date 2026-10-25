/*
 * Os componentes do Gravity. Em modo imediato um componente é uma função chamada a cada quadro:
 * declara os elementos dele no Clay e responde à entrada pelo layout do quadro anterior.
 *
 * Interação: o elemento que recebe o aperto fica "activo" (gv__entrada.ativo) até soltar; um
 * toque é soltar ainda sobre ele. Os componentes declarados por dentro de outros são chamados
 * depois, por isso o mais fundo ganha o aperto (um botão dentro de um cartão tocável).
 */
#include "gv_interno.h"

#include <math.h>
#include <string.h>

/* ---- interação ---- */

static void candidato(Clay_ElementId id) {
	if (gv__entrada.apertou && !gv__entrada.travado && Clay_PointerOver(id)) {
		gv__entrada.ativo = id.id;
	}
}

bool gv_por_cima(Clay_ElementId id) {
	return Clay_PointerOver(id);
}

bool gv_premido(Clay_ElementId id) {
	candidato(id);
	return gv__entrada.ativo == id.id && gv__entrada.baixo;
}

bool gv_tocado(Clay_ElementId id) {
	candidato(id);
	return gv__entrada.soltou && gv__entrada.ativo == id.id && Clay_PointerOver(id);
}

Clay_BoundingBox gv_caixa(Clay_ElementId id) {
	Clay_ElementData dados = Clay_GetElementData(id);
	return dados.found ? dados.boundingBox : (Clay_BoundingBox){ 0 };
}

uint32_t gv_tecla(void) {
	return gv__entrada.nteclas ? gv__entrada.teclas[0] : 0;
}

bool gv_tecla_carregada(uint32_t keysym) {
	for (int i = 0; i < gv__entrada.nteclas; i++) {
		if (gv__entrada.teclas[i] == keysym) {
			return true;
		}
	}
	return false;
}

uint32_t gv_modificadores(void) {
	return gv__entrada.mods;
}

void gv__entrada_fim_do_quadro(void) {
	if (gv__entrada.soltou && !gv__entrada.baixo) {
		gv__entrada.ativo = 0;
		gv__entrada.travado = false;
		/* um dedo que saiu não fica "por cima" de nada */
		if (gv__entrada.toque) {
			gv__entrada.x = gv__entrada.y = -1;
			gv__app_pedir();
		}
	}
	gv__campos_fim_do_quadro();
	gv__rolagens_fim_do_quadro();
	gv__imagens_fim_do_quadro();
	gv__entrada.apertou = false;
	gv__entrada.soltou = false;
	gv__entrada.nteclas = 0;
	gv__entrada.texto[0] = '\0';
	gv__entrada.ime.chegou = false;
	gv__entrada.roda_x = gv__entrada.roda_y = 0;
}

/* ---- peças ---- */

static struct gv_extra *extra(enum gv_extra_tipo tipo) {
	struct gv_extra *e = gv__quadro_alloc(sizeof(*e));
	if (e) {
		memset(e, 0, sizeof(*e));
		e->tipo = tipo;
	}
	return e;
}

void *gv_sombra(float desfoque, float desloca_y, float alfa) {
	struct gv_extra *e = extra(GV_EXTRA_SOMBRA);
	if (e) {
		e->sombra.desfoque = desfoque;
		e->sombra.desloca_y = desloca_y;
		e->sombra.alfa = alfa;
	}
	return e;
}

static Clay_Color misturar(Clay_Color a, Clay_Color b, float t) {
	return (Clay_Color){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
}

bool gv_icone_existe(const char *nome) {
	return gv__icone_caminho(nome) != NULL;
}

void gv_icone(const char *nome, float tamanho, Clay_Color cor) {
	const char *caminho = gv__icone_caminho(nome);
	if (!caminho) {
		caminho = gv__icone_caminho("app");
	}
	struct gv_extra *e = extra(GV_EXTRA_ICONE);
	if (e) {
		e->icone.caminho = caminho;
		e->icone.cor = cor;
		e->icone.traco = gv_t.traco_icone;
	}
	CLAY_AUTO_ID({
		.layout = { .sizing = { CLAY_SIZING_FIXED(tamanho), CLAY_SIZING_FIXED(tamanho) } },
		.custom = { .customData = e },
	}) {}
}

void gv_separador(bool vertical, float comprimento) {
	Clay_SizingAxis eixo = comprimento > 0 ? CLAY_SIZING_FIXED(comprimento) : CLAY_SIZING_GROW(0);
	CLAY_AUTO_ID({
		.layout = { .sizing = vertical ? (Clay_Sizing){ CLAY_SIZING_FIXED(1), eixo } : (Clay_Sizing){ eixo, CLAY_SIZING_FIXED(1) } },
		.backgroundColor = gv_t.borda,
	}) {}
}

Clay_ElementDeclaration gv_cartao(void) {
	return (Clay_ElementDeclaration){
		.layout = {
			.layoutDirection = CLAY_TOP_TO_BOTTOM,
			.padding = { 20, 20, 20, 20 },
			.childGap = 0,
		},
		.backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
		.userData = gv_sombra(40, 16, 0.45f),
	};
}

/* o estado de um controle: 0 parado, 0,5 sob o cursor, 1 premido (animado) */
static float estado_de(Clay_ElementId id, bool *tocado) {
	*tocado = gv_tocado(id);
	bool premido = gv_premido(id);
	bool sobre = gv_por_cima(id) && !gv__entrada.toque;
	return gv_anim(id.id, premido ? 1 : sobre ? 0.5f : 0, gv_t.duracao_rapida);
}

/* parado → sob o cursor → premido */
static Clay_Color tres(Clay_Color parado, Clay_Color sobre, Clay_Color premido, float e) {
	return e <= 0.5f ? misturar(parado, sobre, e * 2) : misturar(sobre, premido, (e - 0.5f) * 2);
}

/* ---- botões ---- */

bool gv_botao_ext(Clay_ElementId id, const gv_botao_opcoes *o) {
	bool tocado;
	float e = estado_de(id, &tocado);
	if (o->desativado) {
		e = 0;
		tocado = false;
	}
	Clay_Color nada = { 0 };
	Clay_Color fundo = nada, borda = nada, conteudo = gv_t.texto;
	switch (o->estilo) {
	case GV_BOTAO_CHEIO:
		fundo = tres(gv_t.acento, gv_t.acento_cursor, gv_t.acento_premido, e);
		conteudo = gv_t.sobre_acento;
		break;
	case GV_BOTAO_ELEVADO:
		fundo = tres(gv_t.elevada, gv_t.sob_cursor, gv_t.selecionado, e);
		borda = gv_t.borda;
		break;
	case GV_BOTAO_CONTORNO:
		fundo = tres(gv_alfa(gv_t.sob_cursor, 0), gv_t.sob_cursor, gv_t.selecionado, e);
		borda = gv_t.borda_controle;
		break;
	case GV_BOTAO_SIMPLES:
		fundo = tres(gv_alfa(gv_t.sob_cursor, 0), gv_t.sob_cursor, gv_t.selecionado, e);
		break;
	case GV_BOTAO_PERIGO:
		fundo = gv_alfa(gv_t.perigo, 0.12f * e);
		borda = gv_alfa(gv_t.perigo, 0.55f);
		conteudo = gv_t.perigo;
		break;
	}
	if (o->desativado) {
		fundo = o->estilo == GV_BOTAO_CHEIO ? gv_t.elevada : fundo;
		borda = borda.a > 0 ? gv_t.borda : borda;
		conteudo = gv_t.desativado;
	}
	CLAY(id, {
		.layout = {
			.sizing = { o->crescer ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(o->altura > 0 ? o->altura : 44) },
			.padding = { o->texto ? 18 : 12, o->texto ? 18 : 12, 0, 0 },
			.childGap = 10,
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
		},
		.backgroundColor = fundo,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = borda, .width = GV_BORDA(borda.a > 0 ? 1 : 0) },
	}) {
		if (o->icone) {
			gv_icone(o->icone, 20, conteudo);
		}
		if (o->texto) {
			gv_texto(gv_str(o->texto), gv_t.tamanho_corpo, conteudo,
				o->estilo == GV_BOTAO_CHEIO ? GV_PESO_MEDIO : GV_PESO_NORMAL);
		}
	}
	return tocado;
}

bool gv_botao(Clay_ElementId id, const char *icone, const char *texto, enum gv_estilo_botao estilo) {
	return gv_botao_ext(id, &(gv_botao_opcoes){ .icone = icone, .texto = texto, .estilo = estilo });
}

bool gv_botao_redondo(Clay_ElementId id, const char *icone, float tamanho, bool ligado, bool perigo) {
	bool tocado;
	float e = estado_de(id, &tocado);
	float ativo = gv_anim(id.id + 1, ligado ? 1 : 0, gv_t.duracao_normal);
	Clay_Color marca = perigo ? gv_t.perigo : gv_t.acento;
	Clay_Color fundo = tres(gv_t.elevada, gv_t.sob_cursor, gv_t.selecionado, e);
	fundo = misturar(fundo, perigo ? misturar(gv_t.elevada, gv_t.perigo, 0.12f) : gv_t.selecionado, ativo * (1 - e));
	Clay_Color borda = misturar(gv_t.borda, gv_alfa(marca, 0.6f), ativo);
	Clay_Color cor = misturar(gv_t.texto_secundario, marca, ativo);
	if (tamanho <= 0) {
		tamanho = 44;
	}
	CLAY(id, {
		.layout = {
			.sizing = { CLAY_SIZING_FIXED(tamanho), CLAY_SIZING_FIXED(tamanho) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER },
		},
		.backgroundColor = fundo,
		.cornerRadius = CLAY_CORNER_RADIUS(tamanho / 2),
		.border = { .color = borda, .width = GV_BORDA(1) },
	}) {
		gv_icone(icone, tamanho * 0.46f, cor);
	}
	return tocado;
}

/* ---- deslizante e interruptor ---- */

bool gv_deslizante(Clay_ElementId id, float *valor, float trilho, float puxador) {
	if (trilho <= 0) {
		trilho = 6;
	}
	if (puxador <= 0) {
		puxador = 20;
	}
	candidato(id);
	bool arrastando = gv__entrada.ativo == id.id && (gv__entrada.baixo || gv__entrada.soltou);
	bool mudou = false;
	if (arrastando) {
		Clay_BoundingBox b = gv_caixa(id);
		if (b.width > puxador) {
			float v = (gv__entrada.x - b.x - puxador / 2) / (b.width - puxador);
			v = fmaxf(0, fminf(1, v));
			if (fabsf(v - *valor) > 0.0005f) {
				*valor = v;
				mudou = true;
			}
		}
	}
	bool sobre = gv_por_cima(id) && !gv__entrada.toque;
	struct gv_extra *e = extra(GV_EXTRA_DESLIZANTE);
	if (e) {
		e->deslizante.valor = *valor;
		e->deslizante.trilho = trilho;
		e->deslizante.puxador = puxador;
		e->deslizante.cheio = arrastando ? gv_t.acento_premido : sobre ? gv_t.acento_cursor : gv_t.acento;
		e->deslizante.vazio = gv_t.borda;
		e->deslizante.cor_puxador = gv_t.texto;
		e->deslizante.realce = gv_anim(id.id, arrastando ? 1 : sobre ? 0.5f : 0, gv_t.duracao_rapida);
	}
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(fmaxf(puxador + 12, 32)) } },
		.custom = { .customData = e },
	}) {}
	return mudou;
}

bool gv_interruptor(Clay_ElementId id, bool *ligado) {
	bool mudou = false;
	if (gv_tocado(id)) {
		*ligado = !*ligado;
		mudou = true;
	}
	float pos = gv_anim(id.id, *ligado ? 1 : 0, gv_t.duracao_normal);
	bool sobre = gv_por_cima(id) && !gv__entrada.toque;
	struct gv_extra *e = extra(GV_EXTRA_INTERRUPTOR);
	if (e) {
		e->interruptor.posicao = pos;
		e->interruptor.fundo = misturar(sobre ? gv_t.sob_cursor : gv_t.borda, sobre ? gv_t.acento_cursor : gv_t.acento, pos);
		e->interruptor.bolinha = misturar(gv_t.texto_secundario, gv_t.texto, pos);
	}
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_FIXED(52), CLAY_SIZING_FIXED(30) } },
		.custom = { .customData = e },
	}) {}
	return mudou;
}

/* ---- linhas, abas e a barra da janela ---- */

bool gv_linha(Clay_ElementId id, const char *icone, const char *titulo, const char *subtitulo, bool *interruptor) {
	bool tocado;
	float e = estado_de(id, &tocado);
	bool mudou = false;
	Clay_ElementId chave = Clay__HashStringWithOffset(id.stringId, id.offset * 64 + 2000003u, id.baseId);
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(subtitulo ? 76 : 56) }, .padding = { 18, 18, 14, 14 },
			.childGap = 22, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = interruptor ? (Clay_Color){ 0 } : gv_alfa(gv_t.sob_cursor, fminf(e * 2, 1)),
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
	}) {
		if (icone) {
			gv_icone(icone, 30, gv_t.icone);
		}
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_NORMAL);
			if (subtitulo) {
				gv_texto(gv_str(subtitulo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		if (interruptor) {
			mudou = gv_interruptor(chave, interruptor);
		} else {
			gv_icone("forward", 20, gv_t.texto_secundario);
		}
	}
	if (interruptor) {
		/* tocar na linha também liga e desliga */
		if (tocado && !mudou && !gv_por_cima(chave)) {
			*interruptor = !*interruptor;
			mudou = true;
		}
		return mudou;
	}
	return tocado;
}

bool gv_abas(Clay_ElementId id, const char *const *nomes, int n, int *ativa) {
	bool mudou = false;
	CLAY(id, { .layout = { .childGap = 14 } }) {
		for (int i = 0; i < n; i++) {
			Clay_ElementId aba = Clay__HashStringWithOffset(id.stringId, id.offset * 64 + 3000017u + i, id.baseId);
			if (gv_tocado(aba) && *ativa != i) {
				*ativa = i;
				mudou = true;
			}
			bool esta = *ativa == i;
			bool sobre = gv_por_cima(aba) && !gv__entrada.toque;
			float marca = gv_anim(aba.id, esta ? 1 : 0, gv_t.duracao_normal);
			CLAY(aba, {
				.layout = { .sizing = { .height = CLAY_SIZING_FIXED(56) }, .padding = { 10, 10, 0, 0 },
					.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.border = { .color = gv_alfa(gv_t.acento, marca), .width = { .bottom = 2 } },
			}) {
				gv_texto(gv_str(nomes[i]), gv_t.tamanho_corpo + 1,
					esta ? gv_t.texto : sobre ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
	return mudou;
}

static bool botao_janela(Clay_ElementId id, const char *icone, bool fechar) {
	bool tocado;
	float e = estado_de(id, &tocado);
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_FIXED(44), CLAY_SIZING_FIXED(36) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = fechar ? gv_alfa(gv_t.perigo, 0.25f * fminf(e * 2, 1)) : gv_alfa(gv_t.sob_cursor, fminf(e * 2, 1)),
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
	}) {
		gv_icone(icone, 18, gv_t.texto_secundario);
	}
	return tocado;
}

void gv_barra_janela(gv_app *app, Clay_ElementId id, const char *icone, const char *titulo) {
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(54) }, .padding = { 20, 10, 0, 0 }, .childGap = 14,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } },
	}) {
		if (icone) {
			gv_icone(icone, 22, gv_t.texto_secundario);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
		}
		if (botao_janela(Clay__HashStringWithOffset(id.stringId, 4000001u, id.baseId), "minimize", false)) {
			gv_app_minimizar(app);
		}
		if (botao_janela(Clay__HashStringWithOffset(id.stringId, 4000002u, id.baseId), "maximize", false)) {
			gv_app_maximizar(app);
		}
		if (botao_janela(Clay__HashStringWithOffset(id.stringId, 4000003u, id.baseId), "close", true)) {
			gv_app_sair(app);
		}
	}
	/* arrastar a barra move a janela (o compositor faz o resto) */
	if (gv__entrada.apertou && gv_por_cima(id) && !gv__entrada.ativo) {
		gv_app_mover(app);
	}
}
