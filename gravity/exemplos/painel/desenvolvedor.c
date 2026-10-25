/* Sistema → Desenvolvedor: o modo desenvolvedor e o que ele liga (~/.config/lumna/desenvolvedor.ini,
 * servicos/desenvolvedor.h). Grava ao mexer: não há nada por aplicar. */
#include "painel.h"

#include "desenvolvedor.h"

static struct lumna_dev d;
static bool lido;

static void ler(void) {
	d = dev_ler_cru();
	lido = true;
}

bool pn_ao_vivo(void) {
	if (!lido) {
		ler();
	}
	return d.ativo && d.ao_vivo;
}

static void abrir(void) {
	ler();
}

static void desenhar(void) {
	struct lumna_dev antes = d;
	pn_linha_comecar(CLAY_ID("dev-ativo"), "code", "Modo desenvolvedor", "Liga as opções abaixo, para testar e para aparelhos diferentes");
	gv_interruptor(CLAY_ID("dev-ativo-v"), &d.ativo);
	pn_linha_acabar();
	if (d.ativo) {
		pn_subtitulo(CLAY_ID("dev-s1"), "Toque");
		pn_linha_comecar(CLAY_ID("dev-toque"), "grid", "Tela de toque no desktop",
			"Para monitores com toque: o teclado de números no PIN do bloqueio");
		gv_interruptor(CLAY_ID("dev-toque-v"), &d.toque);
		pn_linha_acabar();
		pn_subtitulo(CLAY_ID("dev-s2"), "Painel de controle");
		pn_linha_comecar(CLAY_ID("dev-vivo"), "zap", "Aplicar em tempo real",
			"Cada mudança vale na hora, sem o botão Aplicar");
		gv_interruptor(CLAY_ID("dev-vivo-v"), &d.ao_vivo);
		pn_linha_acabar();
	}
	if (d.ativo != antes.ativo || d.toque != antes.toque || d.ao_vivo != antes.ao_vivo) {
		dev_gravar(d);
	}
}

static const pn_secao secoes[] = {
	{ "desenvolvedor", "code", "Desenvolvedor", "Opções a mais para testar e para outros aparelhos.", desenhar, NULL, NULL, NULL,
		NULL, NULL, abrir },
};
const pn_categoria pn_desenvolvedor = { "desenvolvedor", "code", "Desenvolvedor", "Toque e aplicar em tempo real", PN_SISTEMA,
	secoes, 1 };
