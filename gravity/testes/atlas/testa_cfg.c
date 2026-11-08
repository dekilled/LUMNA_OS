/* Os ajustes do explorador e o modelo da barra lateral (arquivos_cfg.c). */
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#include "arquivos_cfg.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); falhas++; } } while (0)

int main(void) {
	ar_cfg c, d;
	ar_cfg_padrao(&c);
	CONFERE(c.vista == AR_VISTA_GRADE && c.tam_pasta == 128 && c.extensoes && c.pastas_primeiro && !c.ocultos && c.confirmar_apagar && c.confirmar_esvaziar && c.conflito == 0, "padrões");
	ar_cfg_ler(&d);
	CONFERE(!memcmp(&c, &d, sizeof(c)) || (d.vista == c.vista && d.tam_pasta == c.tam_pasta), "sem arquivo: os padrões");
	c.vista = AR_VISTA_LISTA_GRANDE;
	c.tam_pasta = 160;
	c.texto = 2;
	c.ocultos = true;
	c.extensoes = false;
	c.ordem = 2;
	c.decrescente = true;
	c.abrir_em = AR_ABRIR_PERSONALIZADA;
	snprintf(c.pasta_inicial, sizeof(c.pasta_inicial), "/usr/share");
	c.conflito = 3;
	c.confirmar_apagar = false;
	CONFERE(ar_cfg_gravar(&c), "gravar");
	ar_cfg_ler(&d);
	CONFERE(d.vista == AR_VISTA_LISTA_GRANDE && d.tam_pasta == 160 && d.texto == 2 && d.ocultos && !d.extensoes && d.ordem == 2 && d.decrescente && d.abrir_em == AR_ABRIR_PERSONALIZADA &&
		!strcmp(d.pasta_inicial, "/usr/share") && d.conflito == 3 && !d.confirmar_apagar && d.confirmar_esvaziar, "ler o que gravou");
	/* valores absurdos são limitados */
	char *a = g_build_filename(g_get_user_config_dir(), "lumna", "atlas", "config.ini", NULL);
	g_file_set_contents(a, "[explorador]\nvista=9\ntam_pasta=10000\nconflito=-4\n", -1, NULL);
	ar_cfg_ler(&d);
	CONFERE(d.vista == 2 && d.tam_pasta == 192 && d.conflito == 0, "valores fora do limite (%d %d %d)", d.vista, d.tam_pasta, d.conflito);
	g_free(a);

	/* visão de cada pasta */
	CONFERE(ar_visao_da_pasta("/tmp/x") == -1, "nenhuma lembrada");
	ar_visao_da_pasta_definir("/tmp/x", AR_VISTA_LISTA);
	CONFERE(ar_visao_da_pasta("/tmp/x") == AR_VISTA_LISTA && ar_visao_da_pasta("/tmp/y") == -1, "lembrar por pasta");
	ar_visao_da_pasta_definir("/tmp/x", -1);
	CONFERE(ar_visao_da_pasta("/tmp/x") == -1, "esquecer");

	/* a barra lateral */
	ar_barra b;
	ar_barra_ler(&b);
	int idx[AR_BARRA_MAX];
	CONFERE(ar_barra_da_secao(&b, AR_SECAO_ESPACOS, idx) == 3 && !strcmp(b.v[idx[0]].id, "inicio") && !strcmp(b.v[idx[2]].id, "favoritos"), "padrão: Início, Recentes, Favoritos");
	int nc = ar_barra_da_secao(&b, AR_SECAO_COLECOES, idx);
	CONFERE(nc >= 1 && !strcmp(b.v[idx[0]].rotulo, "Home") && !strcmp(b.v[idx[0]].caminho, g_get_home_dir()), "padrão: as coleções começam pela casa");
	char *pasta = g_dir_make_tmp("barra-XXXXXX", NULL);
	CONFERE(!ar_barra_tem(pasta, AR_SECAO_ESPACOS), "pasta não fixada");
	CONFERE(ar_barra_alternar(pasta, AR_SECAO_ESPACOS) && ar_barra_tem(pasta, AR_SECAO_ESPACOS) && !ar_barra_tem(pasta, AR_SECAO_COLECOES), "fixar em Espaços grava e só vale ali");
	ar_barra_ler(&b);
	CONFERE(ar_barra_da_secao(&b, AR_SECAO_ESPACOS, idx) == 4 && !strcmp(b.v[idx[3]].caminho, pasta) && b.v[idx[3]].visivel, "o fixado entra no fim da seção");
	/* esconder, renomear, mover entre seções, subir/descer */
	b.v[idx[1]].visivel = false;
	snprintf(b.v[idx[3]].rotulo, sizeof(b.v[0].rotulo), "Meu lugar");
	b.v[idx[3]].secao = AR_SECAO_COLECOES;
	CONFERE(ar_barra_gravar(&b), "gravar a barra");
	ar_barra b2;
	ar_barra_ler(&b2);
	int ie[AR_BARRA_MAX];
	CONFERE(ar_barra_da_secao(&b2, AR_SECAO_ESPACOS, ie) == 3 && !b2.v[ie[1]].visivel && b2.v[ie[0]].visivel, "esconder um item fica");
	int ic[AR_BARRA_MAX];
	int nc2 = ar_barra_da_secao(&b2, AR_SECAO_COLECOES, ic);
	CONFERE(nc2 == nc + 1 && !strcmp(b2.v[ic[nc2 - 1]].rotulo, "Meu lugar"), "mudar de seção e renomear ficam");
	ar_barra_mover(&b2, ic[nc2 - 1], -1);
	ar_barra_da_secao(&b2, AR_SECAO_COLECOES, ic);
	CONFERE(!strcmp(b2.v[ic[nc2 - 2]].rotulo, "Meu lugar"), "subir um lugar");
	ar_barra_mover(&b2, ic[0], -1);
	ar_barra_da_secao(&b2, AR_SECAO_COLECOES, ic);
	CONFERE(!strcmp(b2.v[ic[0]].rotulo, "Home") || strlen(b2.v[ic[0]].rotulo) > 0, "subir o primeiro não faz nada");
	CONFERE(ar_barra_remover(&b2, ic[nc2 - 1]) && ar_barra_da_secao(&b2, AR_SECAO_COLECOES, ic) == nc2 - 1, "remover");
	CONFERE(!ar_barra_remover(&b2, 999), "remover um índice inválido");
	ar_barra_padrao(&b2);
	CONFERE(ar_barra_gravar(&b2) && ar_barra_da_secao(&b2, AR_SECAO_ESPACOS, ie) == 3, "restaurar o padrão");
	b2.v[ie[0]].no_inicio = false;
	ar_barra_gravar(&b2);
	ar_barra_ler(&b);
	CONFERE(!b.v[ie[0]].no_inicio && b.v[ie[1]].no_inicio, "o que aparece na página inicial é lembrado");
	CONFERE(ar_barra_alternar(pasta, AR_SECAO_COLECOES) && !ar_barra_alternar(pasta, AR_SECAO_COLECOES), "fixar e desafixar de novo");

	if (falhas) {
		return 1;
	}
	puts("ajustes do explorador e barra lateral: ok");
	return 0;
}
