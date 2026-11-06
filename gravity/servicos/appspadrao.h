/*
 * Os apps padrão: que app abre cada tipo de coisa (o navegador, o e-mail, a música...). Quem guarda é o
 * ~/.config/mimeapps.list do freedesktop, pelo GIO, então o que se escolhe aqui vale para qualquer programa
 * (xdg-open, o Arquivos, o Firefox). Cada categoria é um grupo de tipos (video/mp4, video/webm...) que mudam juntos.
 * Para testar, $XDG_CONFIG_HOME e $XDG_DATA_DIRS apontam para pastas falsas.
 */
#ifndef LUMNA_APPSPADRAO_H
#define LUMNA_APPSPADRAO_H

#include <stdbool.h>
#include <stddef.h>

#define PADRAO_MAX_OPCOES 40

struct padrao_opcao {
	char id[160];     /* o .desktop ("firefox.desktop") */
	char nome[128];
};

struct padrao_categoria {
	const char *id, *nome, *sub, *icone;
	char atual[160];   /* o .desktop que está valendo ("" : nenhum) */
	struct padrao_opcao opcoes[PADRAO_MAX_OPCOES];
	int nopcoes;
};

/* quantas categorias há */
int padrao_n(void);
/* lê a categoria i: o que vale agora e os apps que sabem abrir aquele tipo */
void padrao_ler(int i, struct padrao_categoria *c);
/* muda o app da categoria para todos os tipos dela; false se o app não existe */
bool padrao_definir(int i, const char *desktop_id);

#endif
