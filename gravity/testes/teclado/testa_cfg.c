/* Os ajustes do teclado na tela (teclado.ini) e os emojis recentes (arquivo): padrões, valores inválidos, gravar e ler de volta. HOME/XDG apontam para uma pasta de teste. */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "teclado_cfg.h"
#include "teclado_layout.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); falhas++; } } while (0)

int main(int argc, char **argv) {
	tcfg c;
	tcfg_ler(&c);
	CONFERE(!strcmp(c.modo, "auto") && !strcmp(c.tamanho, "medio"), "padrão: auto e médio (%s %s)", c.modo, c.tamanho);
	CONFERE(tcfg_escala(&c) == 1.0f, "escala média 1");
	snprintf(c.modo, sizeof(c.modo), "sempre");
	snprintf(c.tamanho, sizeof(c.tamanho), "grande");
	CONFERE(tcfg_gravar(&c), "gravar");
	tcfg c2;
	tcfg_ler(&c2);
	CONFERE(!strcmp(c2.modo, "sempre") && !strcmp(c2.tamanho, "grande") && tcfg_escala(&c2) > 1.1f, "lê o que gravou (%s %s)", c2.modo, c2.tamanho);
	snprintf(c.tamanho, sizeof(c.tamanho), "pequeno");
	tcfg_gravar(&c);
	tcfg_ler(&c2);
	CONFERE(tcfg_escala(&c2) < 0.9f, "pequeno é menor");
	/* um arquivo com lixo cai nos padrões */
	char *caminho = tcfg_caminho();
	g_file_set_contents(caminho, "[teclado]\nmodo=talvez\ntamanho=enorme\n", -1, NULL);
	tcfg_ler(&c2);
	CONFERE(!strcmp(c2.modo, "auto") && !strcmp(c2.tamanho, "medio"), "valores inválidos viram o padrão (%s %s)", c2.modo, c2.tamanho);
	g_free(caminho);
	/* recentes: gravam e voltam na mesma ordem */
	tl_recentes_limpar();
	tl_recentes_usar("😀");
	tl_recentes_usar("🔥");
	tl_recentes_usar("❤️");
	tcfg_recentes_gravar();
	tl_recentes_limpar();
	CONFERE(tl_recentes_n() == 0, "limpou");
	tcfg_recentes_ler();
	CONFERE(tl_recentes_n() == 3 && !strcmp(tl_recentes_get(0), "❤️") && !strcmp(tl_recentes_get(2), "😀"), "os recentes voltam na ordem (%d: %s %s %s)", tl_recentes_n(),
		tl_recentes_get(0), tl_recentes_get(1), tl_recentes_get(2));
	if (falhas) {
		return 1;
	}
	puts("ajustes do teclado na tela: ok");
	return 0;
}
