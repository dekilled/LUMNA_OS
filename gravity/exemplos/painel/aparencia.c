/*
 * Aparência: tema, cores, papel de parede, dock, barra superior, tela de bloqueio e tamanho.
 * A Dock já mexe de verdade (grava ~/.config/lumna/dock.ini, que a dock vigia); as outras chegam
 * a seguir (PAINEL.md).
 */
#include "painel.h"

#include <stdio.h>
#include <string.h>

/* ---- Dock ---- */

struct dock_ajustes {
	int formato;   /* 0 pílula, 1 barra */
	int monitores; /* 0 só no principal, 1 em todos */
	int maximizar; /* 0 ocultar, 1 esticar */
};
static struct dock_ajustes dock, dock_gravada;

static void dock_abrir(void) {
	memset(&dock_gravada, 0, sizeof(dock_gravada));
	char caminho[1024];
	pn_caminho(caminho, sizeof(caminho), "dock.ini");
	FILE *f = fopen(caminho, "r");
	if (f) {
		char linha[256];
		while (fgets(linha, sizeof(linha), f)) {
			char chave[64], valor[64];
			if (sscanf(linha, " %63[^= ] = %63s", chave, valor) != 2) {
				continue;
			}
			if (!strcmp(chave, "formato")) {
				dock_gravada.formato = !strcmp(valor, "barra");
			} else if (!strcmp(chave, "monitores")) {
				dock_gravada.monitores = !strcmp(valor, "todos");
			} else if (!strcmp(chave, "ao_maximizar")) {
				dock_gravada.maximizar = !strcmp(valor, "esticar");
			}
		}
		fclose(f);
	}
	dock = dock_gravada;
}

static bool dock_pendente(void) {
	return memcmp(&dock, &dock_gravada, sizeof(dock)) != 0;
}

static void dock_aplicar(void) {
	char texto[512];
	snprintf(texto, sizeof(texto),
		"# A dock do LUMNA (o Painel de controle grava este arquivo)\n"
		"formato = %s\nmonitores = %s\nao_maximizar = %s\n",
		dock.formato ? "barra" : "pilula", dock.monitores ? "todos" : "principal", dock.maximizar ? "esticar" : "ocultar");
	if (pn_gravar("dock.ini", texto)) {
		dock_gravada = dock;
	}
}

static void dock_descartar(void) {
	dock = dock_gravada;
}

static void dock_restaurar(void) {
	memset(&dock, 0, sizeof(dock));
}

/* um cartão de escolha com o desenho da dock numa telinha */
static bool cartao_formato(Clay_ElementId id, const char *nome, const char *descricao, bool barra, bool escolhido) {
	bool tocado = gv_tocado(id);
	CLAY(id, {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14),
			.childGap = 12 },
		.backgroundColor = escolhido ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = escolhido ? gv_t.acento : gv_t.borda, .width = GV_BORDA(escolhido ? 2 : 1) },
	}) {
		tocado |= gv_focavel(id, gv_t.raio_grande);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(110) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_BOTTOM }, .padding = { 0, 0, 0, barra ? 0 : 10 } },
			.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { barra ? CLAY_SIZING_GROW(0) : CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(barra ? 20 : 22) },
				.childGap = 7, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.sob_cursor,
				.cornerRadius = barra ? (Clay_CornerRadius){ 0, 0, gv_t.raio_medio, gv_t.raio_medio } : CLAY_CORNER_RADIUS(8) }) {
				for (int i = 0; i < 5; i++) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10) } },
						.backgroundColor = i == 0 ? gv_t.acento : gv_t.texto_suave, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {}
				}
			}
		}
		gv_texto(gv_str(nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
		gv_texto(gv_str(descricao), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	return tocado;
}

static void dock_desenhar(void) {
	pn_subtitulo(CLAY_ID("dock-s1"), "Formato");
	CLAY(CLAY_ID("dock-formatos"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16, .padding = { 0, 0, 0, 14 } } }) {
		if (cartao_formato(CLAY_ID("dock-pilula"), "Flutuante", "Pequena e ao centro, com os cantos redondos", false,
			    dock.formato == 0)) {
			dock.formato = 0;
		}
		if (cartao_formato(CLAY_ID("dock-barra"), "Barra inteira", "De borda a borda, com os ícones ao centro", true,
			    dock.formato == 1)) {
			dock.formato = 1;
		}
	}
	static const char *const monitores[] = { "Só no monitor principal", "Em todos os monitores" };
	pn_ajuste_lista(CLAY_ID("dock-monitores"), "screens", "Monitores", "Onde a dock aparece", monitores, 2, &dock.monitores);
	static const char *const maximizar[] = { "Ocultar (volta com o mouse)", "Esticar de borda a borda" };
	pn_ajuste_lista(CLAY_ID("dock-maximizar"), "maximize", "Com uma janela maximizada", "Em cada monitor, conforme a janela dele",
		maximizar, 2, &dock.maximizar);
}

/* ---- as outras seções (papel.c, estilo.c, barra.c) ---- */

void pn_papel_abrir(void);
bool pn_papel_pendente(void);
void pn_papel_aplicar(void);
void pn_papel_descartar(void);
void pn_papel_restaurar(void);
void pn_papel_desenhar(void);

void pn_estilo_abrir(void);
bool pn_tema_pendente(void), pn_cores_pendente(void), pn_tamanho_pendente(void);
void pn_tema_aplicar(void), pn_cores_aplicar(void), pn_tamanho_aplicar(void);
void pn_tema_descartar(void), pn_cores_descartar(void), pn_tamanho_descartar(void);
void pn_tema_restaurar(void), pn_cores_restaurar(void), pn_tamanho_restaurar(void);
void pn_tema_desenhar(void), pn_cores_desenhar(void), pn_tamanho_desenhar(void);

void pn_bloqueio_abrir(void);
bool pn_bloqueio_pendente(void);
void pn_bloqueio_aplicar(void);
void pn_bloqueio_descartar(void);
void pn_bloqueio_restaurar(void);
void pn_bloqueio_desenhar(void);

void pn_barra_abrir(void);
bool pn_barra_pendente(void);
void pn_barra_aplicar(void);
void pn_barra_descartar(void);
void pn_barra_restaurar(void);
void pn_barra_desenhar(void);

/* ---- a categoria ---- */

static const pn_secao secoes[] = {
	{ "tema", "window", "Tema", "Escolha a aparência das janelas e dos painéis.", pn_tema_desenhar, NULL, pn_tema_pendente,
		pn_tema_aplicar, pn_tema_descartar, pn_tema_restaurar, pn_estilo_abrir },
	{ "cores", "brush", "Cores", "Defina as cores dos controles e dos destaques.", pn_cores_desenhar, NULL, pn_cores_pendente,
		pn_cores_aplicar, pn_cores_descartar, pn_cores_restaurar, pn_estilo_abrir },
	{ "papel", "image", "Papel de parede", "Escolha o fundo de cada tela e modo.", pn_papel_desenhar,
		"Um por tela;Um por modo;Coleção LUMNA;Trocar automaticamente", pn_papel_pendente, pn_papel_aplicar,
		pn_papel_descartar, pn_papel_restaurar, pn_papel_abrir },
	{ "dock", "dock", "Dock", "O formato da dock e onde ela aparece.", dock_desenhar, NULL, dock_pendente, dock_aplicar,
		dock_descartar, dock_restaurar, dock_abrir },
	{ "barra", "menu", "Barra superior", "O que a barra do topo mostra.", pn_barra_desenhar, "Relógio com segundos e a data",
		pn_barra_pendente, pn_barra_aplicar, pn_barra_descartar, pn_barra_restaurar, pn_barra_abrir },
	{ "bloqueio", "lock", "Tela de bloqueio", "Como o LUMNA fica quando está bloqueado.", pn_bloqueio_desenhar,
		"Layout centralizado ou lateral;PIN;O que está tocando;Emma", pn_bloqueio_pendente, pn_bloqueio_aplicar,
		pn_bloqueio_descartar, pn_bloqueio_restaurar, pn_bloqueio_abrir },
	{ "tamanho", "text", "Tamanho e textos", "O tamanho das letras e a fonte da interface.", pn_tamanho_desenhar,
		"Tamanho do texto;Fonte", pn_tamanho_pendente, pn_tamanho_aplicar, pn_tamanho_descartar, pn_tamanho_restaurar,
		pn_estilo_abrir },
};

const pn_categoria pn_aparencia = { "aparencia", "brush", "Aparência", "Cores, tema e papel de parede", PN_GERAL, secoes,
	sizeof(secoes) / sizeof(secoes[0]) };
