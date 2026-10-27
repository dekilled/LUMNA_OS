/* entrada.c e telas.c: o que o Painel grava, o Orbit lê; as listas do XKB */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "entrada.h"
#include "telas.h"

int main(void) {
	/* entrada.ini: grava e lê de volta */
	struct entrada_cfg c;
	assert(!entrada_cfg_ler(&c)); /* sem arquivo: o padrão */
	assert(!strcmp(c.layout, "br") && c.repeticao_taxa == 25 && c.touchpad.tocar);
	strcpy(c.layout, "br,us");
	strcpy(c.variante, ",intl");
	strcpy(c.opcoes, "grp:alt_shift_toggle,ctrl:nocaps");
	c.repeticao_taxa = 40;
	c.mouse.primario_direito = true;
	c.mouse.velocidade = -0.35;
	c.mouse.plano = true;
	c.touchpad.natural = true;
	c.touchpad.velocidade = 5; /* fora de faixa: o arquivo grava como veio, quem lê limita */
	assert(entrada_cfg_gravar(&c));
	struct entrada_cfg d;
	assert(entrada_cfg_ler(&d));
	assert(!strcmp(d.layout, "br,us") && !strcmp(d.variante, ",intl") && !strcmp(d.opcoes, "grp:alt_shift_toggle,ctrl:nocaps"));
	assert(d.repeticao_taxa == 40 && d.repeticao_atraso == 600);
	assert(d.mouse.primario_direito && d.mouse.plano && !d.mouse.natural);
	assert(d.mouse.velocidade < -0.34 && d.mouse.velocidade > -0.36);
	assert(d.touchpad.natural && d.touchpad.velocidade == 1.0);

	/* layouts e variantes do XKB (um evdev.lst pequeno) */
	struct entrada_item v[16];
	int n = entrada_layouts(v, 16);
	assert(n == 3);
	assert(!strcmp(v[0].id, "us") && !strcmp(v[1].id, "de") && !strcmp(v[2].id, "br")); /* em ordem de nome */
	int nv = entrada_variantes("br", v, 16);
	assert(nv == 2);
	int nu = entrada_variantes("us", v, 16);
	assert(nu == 1 && !strcmp(v[0].id, "intl"));

	/* telas.ini: grava, mescla e lê */
	struct tela_cfg a[2] = { { .nome = "HDMI-A-1", .ligada = 1, .l = 1920, .a = 1080, .hz = 60, .escala = 1.25, .rotacao = 0, .pos = true, .x = 0, .y = 0 },
		{ .nome = "DP-2", .ligada = 0, .rotacao = 90 } };
	assert(telas_cfg_gravar(a, 2));
	struct tela_cfg um = { .nome = "DP-2", .ligada = 1, .l = 2560, .a = 1440, .hz = 144, .escala = 1.0, .rotacao = 270, .pos = true, .x = 1536, .y = 0 };
	assert(telas_cfg_gravar(&um, 1)); /* só a DP-2 muda; a HDMI-A-1 fica */
	struct tela_cfg r[8];
	int nr = telas_cfg_ler(r, 8);
	assert(nr == 2);
	int i1 = !strcmp(r[0].nome, "HDMI-A-1") ? 0 : 1, i2 = 1 - i1;
	assert(r[i1].ligada == 1 && r[i1].l == 1920 && r[i1].hz == 60 && r[i1].escala > 1.24 && r[i1].escala < 1.26);
	assert(!strcmp(r[i2].nome, "DP-2") && r[i2].l == 2560 && r[i2].rotacao == 270 && r[i2].pos && r[i2].x == 1536);

	struct tela t = { .largura = 1920, .altura = 1080, .escala = 1.5, .rotacao = 90, .mm_l = 600, .mm_a = 340 };
	int w, h;
	tela_tamanho_logico(&t, &w, &h);
	assert(w == 720 && h == 1280);
	assert(!strcmp(tela_polegadas(&t), "27\""));
	puts("entrada e telas ok");
	return 0;
}
