#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "latex.h"
static int falhas;
static void t(const char *in, const char *esperado) {
	char b[512]; snprintf(b, sizeof(b), "%s", in); latex_simples(b);
	if (strcmp(b, esperado)) { printf("FALHOU: [%s] -> [%s], esperado [%s]\n", in, b, esperado); falhas++; }
}
int main(void) {
	t("Painel de Controle $\\rightarrow$ Teclado $\\rightarrow$ Teclado", "Painel de Controle → Teclado → Teclado");
	t("a \\rightarrow b", "a → b");
	t("2 $\\times$ 3 $\\approx$ 6", "2 × 3 ≈ 6");
	t("custa R$ 5 e R$ 10", "custa R$ 5 e R$ 10");
	t("`$\\rightarrow$` fica no código", "`$\\rightarrow$` fica no código");
	t("$x^2 + \\frac{a}{b}$", "$x^2 + \\frac{a}{b}$");
	t("ok \\todo e \\to fim", "ok \\todo e → fim");
	t("$\\leftarrow$ voltar", "← voltar");
	t("sem nada", "sem nada");
	t("$\\rightarrow", "$→");
	return falhas;
}
