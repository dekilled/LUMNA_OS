/* discos_cfg: as escolhas gravadas e a regra de quando montar. */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "discos_cfg.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

int main(void) {
	discos_cfg c;
	discos_cfg_ler(&c);
	CONFERE(!c.ao_conectar && !c.ao_entrar, "sem arquivo, tudo desligado");

	c.ao_conectar = true;
	c.ao_entrar = false;
	CONFERE(discos_cfg_gravar(&c), "gravar");
	discos_cfg lida;
	discos_cfg_ler(&lida);
	CONFERE(lida.ao_conectar && !lida.ao_entrar, "lê o que gravou");

	/* a regra: só removível, ainda não montado, que o sistema deixa automontar, e só no evento ligado */
	CONFERE(discos_deve_montar(&lida, DISCOS_CONECTOU, true, false, true), "pendrive ligado, ao conectar ligado: monta");
	CONFERE(!discos_deve_montar(&lida, DISCOS_ENTROU, true, false, true), "ao entrar desligado: não monta");
	CONFERE(!discos_deve_montar(&lida, DISCOS_CONECTOU, false, false, true), "partição interna: nunca sozinho");
	CONFERE(!discos_deve_montar(&lida, DISCOS_CONECTOU, true, true, true), "já montado: nada");
	CONFERE(!discos_deve_montar(&lida, DISCOS_CONECTOU, true, false, false), "o sistema escondeu: nada");

	lida.ao_conectar = false;
	lida.ao_entrar = true;
	discos_cfg_gravar(&lida);
	discos_cfg_ler(&c);
	CONFERE(!c.ao_conectar && c.ao_entrar, "troca de escolha");
	CONFERE(discos_deve_montar(&c, DISCOS_ENTROU, true, false, true), "ao entrar ligado: monta o que já estava ligado");

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("discos (montagem automática): ok\n");
	return 0;
}
