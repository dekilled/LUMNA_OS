/*
 * A bandeja do sistema (os apps que ficam em segundo plano: Discord, Steam, um gerenciador de
 * downloads...), pelo StatusNotifierItem do KDE/freedesktop, que é o que os apps usam hoje. A barra
 * faz de "watcher" e de "host" (org.kde.StatusNotifierWatcher no D-Bus da sessão); cada app
 * registra o seu item e ele aparece aqui. O menu do item vem pelo dbusmenu (com.canonical.dbusmenu)
 * e a barra o desenha com os componentes do Gravity.
 *
 * Mesma ideia dos outros serviços: uma linha de fundo guarda o estado; quem desenha copia e manda
 * comandos. Teste sem apps de verdade: testes/bandeja-falsa.py.
 */
#ifndef LUMNA_BANDEJA_H
#define LUMNA_BANDEJA_H

#include <stdbool.h>

#define BANDEJA_MAX 24
#define BANDEJA_MENU_MAX 48

enum bandeja_estado_item { BANDEJA_PASSIVO, BANDEJA_ATIVO, BANDEJA_ATENCAO };

struct bandeja_item {
	char chave[192];   /* serviço + caminho: identifica o item */
	char id[64];       /* o id do app (ex.: "discord") */
	char titulo[96];
	char icone[200];   /* o nome no tema, ou um arquivo (começa por /) */
	enum bandeja_estado_item estado;
	bool tem_menu;
	bool so_menu;      /* o clique esquerdo abre o menu (ItemIsMenu) */
};

struct bandeja_estado {
	struct bandeja_item itens[BANDEJA_MAX];
	int n;
	unsigned geracao;
};

struct bandeja_entrada {
	int id;
	char texto[96];
	bool separador, habilitado, marcavel, marcado, submenu;
	int nivel;         /* 0 no topo; os filhos de um submenu vêm logo depois, com nivel + 1 */
};

struct bandeja_menu {
	char chave[192];   /* de que item é */
	bool pronto;
	int n;
	struct bandeja_entrada entradas[BANDEJA_MENU_MAX];
	unsigned geracao;
};

int bandeja_iniciar(void);
void bandeja_estado_copiar(struct bandeja_estado *dest);
void bandeja_menu_copiar(struct bandeja_menu *dest);

/* clique esquerdo: Activate (se o app não aceitar e houver menu, o menu é que abre) */
void bandeja_ativar(const char *chave, int x, int y);
/* clique do meio */
void bandeja_secundario(const char *chave, int x, int y);
/* pede o menu do item (chega em bandeja_menu_copiar); sem dbusmenu, manda o ContextMenu ao app */
void bandeja_menu_pedir(const char *chave, int x, int y);
void bandeja_menu_clicar(const char *chave, int id);
void bandeja_menu_fechar(void);

#endif
