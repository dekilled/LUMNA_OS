/*
 * Os aplicativos instalados, para o Painel (Aplicativos e Armazenamento): do banco do pacman
 * (/var/lib/pacman/local, lido direto: rápido e sem senha), do AUR (o que o pacman chama de "foreign",
 * e que o yay ou o paru compilaram) e dos AppImages soltos pela pasta da pessoa.
 *
 * Um aplicativo é um pacote que traz um .desktop visível (ou um AppImage). As dependências e quem precisa
 * de quem vêm do grafo do banco inteiro, então dá para dizer, antes de desinstalar, o que sairia junto
 * (as dependências que ninguém mais usa, como o pacman -Rns) e o que ficaria quebrado.
 *
 * Para testar: $LUMNA_PACMAN_DB (a pasta local/), $LUMNA_PACMAN_RAIZ (onde estão os arquivos dos pacotes, no
 * lugar de /) e um pacman falso no PATH (-Qmq lista os de fora).
 */
#ifndef LUMNA_PACOTES_H
#define LUMNA_PACOTES_H

#include <glib.h>
#include <stdbool.h>
#include <stdint.h>

enum pac_origem { PAC_PACMAN, PAC_AUR, PAC_APPIMAGE };

struct pacote {
	char nome[64];           /* o nome no pacman; no AppImage, o caminho do arquivo */
	char versao[48];
	char descricao[256];
	char url[200];
	char licenca[96];
	char empacotador[96];
	int64_t tamanho;          /* bytes instalados */
	int64_t instalado_em;     /* unix */
	int64_t compilado_em;
	int64_t modificado_em;    /* o arquivo mais novo do pacote */
	bool dependencia;         /* veio como dependência de outro (não foi pedido) */
	enum pac_origem origem;
	char fonte[40];           /* "pacman", "AUR (yay)", "AUR (paru)", "AUR ou compilado à mão", "AppImage" */
	char app_nome[96];        /* o Name= do .desktop */
	char app_id[128];         /* o nome do .desktop ("firefox.desktop"), ou vazio */
	char exec[256];
	char icone[128];
	char dir[256];            /* onde está instalado */
	bool desativado;          /* escondido do lançador pelo Painel */
	bool tem_atualizacao;
	char nova_versao[48];
	char caminho[512];        /* só no AppImage: o arquivo */
};

struct pacotes_bd;

/* lê tudo (leva uma fração de segundo); NULL se não achou o banco do pacman e não há AppImages */
struct pacotes_bd *pacotes_carregar(void);
void pacotes_liberar(struct pacotes_bd *bd);

/* os aplicativos (o que tem .desktop visível ou é AppImage), em ordem de nome */
int pacotes_apps(const struct pacotes_bd *bd, const struct pacote ***lista);
/* qualquer pacote instalado, pelo nome */
const struct pacote *pacotes_achar(const struct pacotes_bd *bd, const char *nome);
/* a soma do espaço dos aplicativos */
int64_t pacotes_apps_tamanho(const struct pacotes_bd *bd);

/* o grafo: de que ele depende, quem precisa dele, quem o tem como opcional (nomes de pacotes; liberar a lista com g_ptr_array_free(.., TRUE)) */
GPtrArray *pacotes_depende_de(const struct pacotes_bd *bd, const struct pacote *p);
GPtrArray *pacotes_necessario_por(const struct pacotes_bd *bd, const struct pacote *p);
GPtrArray *pacotes_opcional_para(const struct pacotes_bd *bd, const struct pacote *p);

/* o que a remoção levaria: o próprio, e as dependências que ninguém mais usa (pacman -Rns). Com cascata, também tudo o que depende dele (-Rcns) */
struct pacotes_remocao {
	GPtrArray *removidos;     /* nomes, o alvo primeiro */
	GPtrArray *quebrariam;    /* quem depende do alvo (sem cascata: a remoção é recusada) */
	int64_t liberado;         /* bytes */
};
void pacotes_previa_remocao(const struct pacotes_bd *bd, const struct pacote *p, bool cascata, struct pacotes_remocao *r);
void pacotes_remocao_liberar(struct pacotes_remocao *r);

/* marca tem_atualizacao/nova_versao no que a lista (nome, versão nova) diz */
void pacotes_marcar_atualizacao(struct pacotes_bd *bd, const char *nome, const char *nova);

/* ações (bloqueiam: chame numa tarefa). Devolvem 0 se deu certo; o texto do erro vai em erro */
int pacotes_remover(const struct pacote *p, bool cascata, char *erro, size_t tam);
/* esconde (ou volta a mostrar) o aplicativo no lançador: um .desktop em ~/.local/share/applications que o cobre */
bool pacotes_definir_ativo(const struct pacote *p, bool ativo);
/* o AppImage: apaga o arquivo e o .desktop dele, se tiver */
int pacotes_remover_appimage(const struct pacote *p, char *erro, size_t tam);

#endif
