/* gv_texto_ajustar: o nome cabe em N linhas de uma largura (quebra nos separadores ou no meio da palavra) e o que passa vira "…". */
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "gravity.h"
#include "gv_interno.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

int main(void) {
	gv_tema_padrao(&gv_t);
	gv__texto_iniciar();
	const float tam = 14, larg = 120;
	Clay_String ls[3];

	int n = gv_texto_ajustar("Curto", larg, tam, GV_PESO_NORMAL, 2, ls);
	CONFERE(n == 1 && ls[0].length == 5 && !memcmp(ls[0].chars, "Curto", 5), "o que cabe fica como está");

	/* duas palavras que não cabem numa linha: quebra no espaço, sem reticências */
	n = gv_texto_ajustar("Pasta com nome medio", larg, tam, GV_PESO_NORMAL, 2, ls);
	CONFERE(n == 2, "quebra em duas linhas");
	CONFERE(n == 2 && !strstr(ls[1].chars, "…") && gv_texto_largura(ls[0].chars, tam, GV_PESO_NORMAL) <= larg + 1, "a primeira linha cabe e não há reticências");

	/* uma palavra comprida sem espaço: quebra no meio, em vez de cortar */
	n = gv_texto_ajustar("abcdefghijklmnopqrstuvwxyz", larg, tam, GV_PESO_NORMAL, 2, ls);
	CONFERE(n == 2, "uma palavra comprida quebra no meio");
	char junto[256];
	snprintf(junto, sizeof(junto), "%.*s%.*s", ls[0].length, ls[0].chars, ls[1].length, ls[1].chars);
	CONFERE(!strcmp(junto, "abcdefghijklmnopqrstuvwxyz"), "as duas linhas juntas são o nome inteiro");

	/* separadores: quebra depois do "_" */
	n = gv_texto_ajustar("Projeto_de_Historias_Fantasticas", larg, tam, GV_PESO_NORMAL, 2, ls);
	CONFERE(n == 2 && ls[0].chars[ls[0].length - 1] == '_', "quebra depois de um separador");

	/* o que passa de duas linhas termina em reticências, e cada linha cabe */
	n = gv_texto_ajustar("Relatorio-trimestral-financeiro-2026-versao-final-revisado-de-novo.pdf", larg, tam, GV_PESO_NORMAL, 2, ls);
	CONFERE(n == 2 && strstr(ls[1].chars, "…") != NULL, "o que passa vira reticências na última linha");
	CONFERE(n == 2 && gv_texto_largura(ls[1].chars, tam, GV_PESO_NORMAL) <= larg + 1, "a linha com reticências cabe na largura");

	/* uma linha só (a lista) */
	n = gv_texto_ajustar("documento_com_um_nome_muito_comprido.txt", larg, tam, GV_PESO_NORMAL, 1, ls);
	CONFERE(n == 1 && strstr(ls[0].chars, "…") != NULL && gv_texto_largura(ls[0].chars, tam, GV_PESO_NORMAL) <= larg + 1, "uma linha com reticências");

	/* acentos (UTF-8) não são cortados no meio de um caractere */
	n = gv_texto_ajustar("Configurações_avançadas_do_sistema_operacional_ção", 100, tam, GV_PESO_NORMAL, 1, ls);
	CONFERE(n == 1 && g_utf8_validate(ls[0].chars, ls[0].length, NULL), "UTF-8 continua válido");

	if (falhas) {
		fprintf(stderr, "%d falha(s)\n", falhas);
		return 1;
	}
	printf("texto (ajustar nomes com quebra e reticências): ok\n");
	return 0;
}
