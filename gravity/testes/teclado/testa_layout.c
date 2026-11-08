/* O layout do teclado na tela: as páginas, as maiúsculas, as variantes, a geometria e o que cada toque faz. */
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "teclado_layout.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); falhas++; } } while (0)

static const tl_tecla *acha(const tl_pagina *p, const char *texto) {
	for (int i = 0; i < p->total; i++) {
		if (p->teclas[i].tipo == TL_CHAR && !strcmp(p->teclas[i].texto, texto)) {
			return &p->teclas[i];
		}
	}
	return NULL;
}
static const tl_tecla *acha_tipo(const tl_pagina *p, enum tl_tipo tipo) {
	for (int i = 0; i < p->total; i++) {
		if (p->teclas[i].tipo == tipo) {
			return &p->teclas[i];
		}
	}
	return NULL;
}

int main(void) {
	static tl_pagina p;
	tl_estado e = { .layout = tl_layout_do_orbit("br", "abnt2"), .pagina = TL_LETRAS };

	/* o layout vem do Orbit */
	CONFERE(e.layout == 0, "br devia ser o pt-BR");
	CONFERE(tl_layout_do_orbit("us", "") == 1, "us devia ser o US");
	CONFERE(tl_layout_do_orbit("de", "") == 1, "um layout sem correspondência cai no US");

	/* letras: 5 linhas (números, 3 de letras, a de baixo), o ç está no ABNT2 e não no US */
	tl_montar(&e, &p);
	CONFERE(p.nlinhas == 5, "5 linhas (%d)", p.nlinhas);
	CONFERE(acha(&p, "ç") != NULL, "o ABNT2 tem o ç");
	CONFERE(acha(&p, "q") && acha(&p, "p") && acha(&p, "0"), "q, p e 0");
	CONFERE(acha_tipo(&p, TL_SHIFT) && acha_tipo(&p, TL_APAGAR) && acha_tipo(&p, TL_ENTER) && acha_tipo(&p, TL_ESPACO), "as teclas de sempre");
	CONFERE(!acha_tipo(&p, TL_TAB) && !acha_tipo(&p, TL_SETA), "no celular não há Tab nem setas");
	const tl_tecla *a = acha(&p, "a");
	CONFERE(a && strstr(a->alts, "ã") && strstr(a->alts, "á") && strstr(a->alts, "â"), "o a segurado dá á, à, â, ã");
	CONFERE(acha(&p, "e") && strstr(acha(&p, "e")->alts, "é"), "e → é");
	tl_estado us = { .layout = 1, .pagina = TL_LETRAS };
	tl_montar(&us, &p);
	CONFERE(!acha(&p, "ç"), "o US não tem ç");
	CONFERE(acha(&p, "a") && strstr(acha(&p, "a")->alts, "á"), "o US também tem os acentos por toque longo");

	/* tela larga: Tab, Esc e as setas */
	e.largo = true;
	tl_montar(&e, &p);
	CONFERE(acha_tipo(&p, TL_TAB) && acha_tipo(&p, TL_ESC) && acha_tipo(&p, TL_SETA), "no PC entram Tab, Esc e as setas");
	e.largo = false;

	/* Shift: uma vez vale uma letra, duas trava, a terceira solta */
	tl_montar(&e, &p);
	const tl_tecla *shift = acha_tipo(&p, TL_SHIFT);
	tl_acao ac = tl_tocar(&e, shift);
	CONFERE(ac.resultado == TL_REDESENHAR && e.shift == TL_SHIFT_UMA, "Shift liga");
	tl_montar(&e, &p);
	CONFERE(acha(&p, "A") && !acha(&p, "a"), "com Shift as letras ficam maiúsculas");
	CONFERE(acha(&p, "Ç") != NULL, "ç → Ç");
	CONFERE(acha(&p, "A") && strstr(acha(&p, "A")->alts, "Ã"), "as variantes também ficam maiúsculas");
	ac = tl_tocar(&e, acha(&p, "A"));
	CONFERE(ac.resultado == TL_TEXTO && !strcmp(ac.texto, "A") && e.shift == TL_SHIFT_NAO, "a maiúscula vale uma letra (%s, shift %d)", ac.texto, e.shift);
	tl_tocar(&e, shift);
	tl_tocar(&e, shift);
	CONFERE(e.shift == TL_SHIFT_TRAVADO, "dois toques travam");
	tl_montar(&e, &p);
	ac = tl_tocar(&e, acha(&p, "B"));
	CONFERE(e.shift == TL_SHIFT_TRAVADO && !strcmp(ac.texto, "B"), "travado, continua maiúscula");
	tl_tocar(&e, shift);
	CONFERE(e.shift == TL_SHIFT_NAO, "o terceiro toque solta");

	/* a maiúscula automática */
	CONFERE(tl_capitalizar_agora("") && tl_capitalizar_agora(NULL), "campo vazio: maiúscula");
	CONFERE(tl_capitalizar_agora("Olá. ") && tl_capitalizar_agora("Quê? ") && tl_capitalizar_agora("Oi!  ") && tl_capitalizar_agora("linha\n"), "depois de ponto e espaço: maiúscula");
	CONFERE(!tl_capitalizar_agora("Olá") && !tl_capitalizar_agora("Olá ") && !tl_capitalizar_agora("Olá.") && !tl_capitalizar_agora("a, "), "no meio da frase: minúscula");

	/* espaço, apagar, Enter */
	tl_montar(&e, &p);
	CONFERE(tl_tocar(&e, acha_tipo(&p, TL_ESPACO)).resultado == TL_TEXTO && !strcmp(tl_tocar(&e, acha_tipo(&p, TL_ESPACO)).texto, " "), "espaço escreve um espaço");
	CONFERE(tl_tocar(&e, acha_tipo(&p, TL_APAGAR)).resultado == TL_APAGAR_UM, "apagar");
	CONFERE(tl_tocar(&e, acha_tipo(&p, TL_ENTER)).resultado == TL_ENTER_, "Enter");
	CONFERE(tl_tocar(&e, acha_tipo(&p, TL_FECHAR)).resultado == TL_ESCONDER, "fechar esconde");

	/* idioma troca o layout */
	int antes = e.layout;
	tl_tocar(&e, acha_tipo(&p, TL_IDIOMA));
	CONFERE(e.layout != antes, "a tecla de idioma troca o layout");
	tl_tocar(&e, acha_tipo(&p, TL_IDIOMA));
	CONFERE(e.layout == antes, "e volta");

	/* símbolos */
	for (const tl_tecla *t = &p.teclas[0]; t < &p.teclas[p.total]; t++) {
		if (t->tipo == TL_PAGINA && t->destino == TL_SIMB1) {
			tl_tocar(&e, t);
			break;
		}
	}
	CONFERE(e.pagina == TL_SIMB1, "?123 abre os símbolos");
	tl_montar(&e, &p);
	CONFERE(acha(&p, "@") && acha(&p, "(") && acha(&p, "?") && acha(&p, "\""), "símbolos de sempre");
	CONFERE(acha(&p, "$") && strstr(acha(&p, "$")->alts, "€"), "$ segurado dá €");
	for (int i = 0; i < p.total; i++) {
		if (p.teclas[i].tipo == TL_PAGINA && p.teclas[i].destino == TL_SIMB2) {
			tl_tocar(&e, &p.teclas[i]);
			break;
		}
	}
	tl_montar(&e, &p);
	CONFERE(e.pagina == TL_SIMB2 && acha(&p, "€") && acha(&p, "[") && acha(&p, "©"), "a segunda página de símbolos");
	e.pagina = TL_LETRAS;

	/* emojis: a categoria, os recentes */
	tl_recentes_limpar();
	e.pagina = TL_EMOJI;
	e.categoria = 1;
	tl_montar(&e, &p);
	CONFERE(p.nlinhas == 1 + 3 + 1, "emojis: categorias, 3 linhas, a de baixo (%d)", p.nlinhas);
	CONFERE(acha(&p, "😀") != NULL, "as carinhas");
	const tl_tecla *cat = NULL;
	for (int i = 0; i < p.total; i++) {
		if (p.teclas[i].tipo == TL_CATEGORIA && p.teclas[i].destino == 3) {
			cat = &p.teclas[i];
		}
	}
	CONFERE(cat != NULL, "a categoria dos corações está na linha de cima");
	tl_tocar(&e, cat);
	CONFERE(e.categoria == 3, "trocou de categoria");
	tl_montar(&e, &p);
	CONFERE(acha(&p, "💔") != NULL, "os corações");
	ac = tl_tocar(&e, acha(&p, "💔"));
	CONFERE(ac.resultado == TL_TEXTO && !strcmp(ac.texto, "💔"), "um emoji escreve");
	tl_tocar(&e, acha(&p, "🔥"));
	tl_tocar(&e, acha(&p, "💔"));
	CONFERE(tl_recentes_n() == 2 && !strcmp(tl_recentes_get(0), "💔") && !strcmp(tl_recentes_get(1), "🔥"), "recentes: o último usado vai para a frente, sem repetir (%d: %s %s)",
		tl_recentes_n(), tl_recentes_get(0), tl_recentes_get(1));
	e.categoria = 0;
	tl_montar(&e, &p);
	CONFERE(acha(&p, "💔") != NULL && acha(&p, "🔥") != NULL, "os recentes aparecem na categoria 0");
	for (int i = 0; i < 30; i++) {
		char s[16];
		snprintf(s, sizeof(s), "%d", i);
		tl_recentes_usar(s);
	}
	CONFERE(tl_recentes_n() == TL_MAX_RECENTES, "os recentes têm um teto (%d)", tl_recentes_n());
	e.largo = true;
	e.categoria = 1;
	tl_montar(&e, &p);
	CONFERE(p.nlinhas == 1 + 2 + 1, "na tela larga os emojis ficam em 2 linhas de 12 (%d)", p.nlinhas);
	e.largo = false;
	e.pagina = TL_LETRAS;

	/* todo texto é UTF-8 válido, e nenhuma linha estoura */
	for (int l = 0; l < tl_layouts_n(); l++) {
		for (int pg = 0; pg <= TL_EMOJI; pg++) {
			for (int largo = 0; largo < 2; largo++) {
				tl_estado s = { .layout = l, .pagina = pg, .largo = largo, .categoria = 1 };
				tl_montar(&s, &p);
				CONFERE(p.total > 0 && p.total < TL_MAX_TECLAS, "página %d/%d/%d com %d teclas", l, pg, largo, p.total);
				for (int i = 0; i < p.total; i++) {
					CONFERE(g_utf8_validate(p.teclas[i].texto, -1, NULL) && g_utf8_validate(p.teclas[i].rotulo, -1, NULL) && g_utf8_validate(p.teclas[i].alts, -1, NULL),
						"UTF-8 inválido na página %d/%d tecla %d", l, pg, i);
				}
			}
		}
	}

	/* geometria: tudo dentro do retângulo, nada sobreposto, e o toque na folga vai para a tecla mais perto */
	tl_montar(&e, &p);
	tl_ret r[TL_MAX_TECLAS];
	float W = 1000, H = 300, folga = 6;
	int n = tl_geometria(&p, W, H, folga, r);
	CONFERE(n == p.total, "uma caixa por tecla");
	for (int i = 0; i < n; i++) {
		CONFERE(r[i].x >= 0 && r[i].y >= 0 && r[i].x + r[i].w <= W + 0.01f && r[i].y + r[i].h <= H + 0.01f && r[i].w > 10 && r[i].h > 10, "tecla %d fora ou pequena (%.1f,%.1f %.1fx%.1f)", i, r[i].x, r[i].y, r[i].w, r[i].h);
		for (int j = i + 1; j < n; j++) {
			bool sobrepoe = r[i].x < r[j].x + r[j].w && r[j].x < r[i].x + r[i].w && r[i].y < r[j].y + r[j].h && r[j].y < r[i].y + r[i].h;
			CONFERE(!sobrepoe, "teclas %d e %d se sobrepõem", i, j);
		}
	}
	int iq = -1;
	for (int i = 0; i < p.total; i++) {
		if (!strcmp(p.teclas[i].texto, "q")) {
			iq = i;
		}
	}
	CONFERE(iq >= 0 && tl_tecla_em(r, n, r[iq].x + r[iq].w / 2, r[iq].y + r[iq].h / 2, folga) == iq, "o centro da tecla é a tecla");
	CONFERE(tl_tecla_em(r, n, r[iq].x + r[iq].w + folga / 2, r[iq].y + r[iq].h / 2, folga) >= 0, "a folga entre teclas vai para a mais perto");
	CONFERE(tl_tecla_em(r, n, -50, -50, folga) == -1 && tl_tecla_em(r, n, W + 50, H / 2, folga) == -1, "fora do teclado não é tecla");
	/* a linha de baixo mais larga que a do celular: a tecla de espaço é a maior */
	float maior = 0;
	int imaior = -1;
	for (int i = 0; i < n; i++) {
		if (r[i].w > maior) {
			maior = r[i].w;
			imaior = i;
		}
	}
	CONFERE(imaior >= 0 && p.teclas[imaior].tipo == TL_ESPACO, "a maior tecla é o espaço");

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	puts("layout do teclado na tela: ok");
	return 0;
}
