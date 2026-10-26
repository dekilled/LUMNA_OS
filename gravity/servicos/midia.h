/*
 * Os serviços do modo Mídia (YouTube, Netflix...) e os recentes, para retomar de onde parou.
 *
 *   ~/.config/lumna/midia.ini        o catálogo: os padrões (ligar/desligar) e os que a pessoa adiciona
 *       [youtube]                    um padrão: só o que se muda dele
 *       ativo = nao
 *       [servico meusite]            um adicionado: nome e endereço
 *       nome = Meu site
 *       url = https://exemplo.com
 *   ~/.local/share/lumna/midia/recentes.json    um por serviço, o mais novo primeiro
 *
 * O ícone de cada serviço, se houver, é ~/.local/share/lumna/midia/icones/ID.svg ou .png (ou o
 * que vier com o sistema); sem ele a Mídia desenha o nome na cor do serviço.
 */
#ifndef LUMNA_MIDIA_H
#define LUMNA_MIDIA_H

#include <glib.h>
#include <stdbool.h>

#define MIDIA_MAX_SERVICOS 32
#define MIDIA_MAX_RECENTES 12

struct midia_servico {
	char id[32];
	char nome[48];
	char url[512];
	char login[512]; /* onde se entra na conta, se não for o próprio endereço (vazio: o endereço) */
	char cor[8];     /* "#E50914": a cor da marca, para o cartão sem ícone */
	bool padrao;     /* vem com o sistema (dá para desligar, não para apagar) */
	bool ativo;
};

struct midia_recente {
	char servico[32];
	char url[1024];
	char titulo[160];
	gint64 quando;       /* g_get_real_time() / 1e6 */
	char miniatura[512]; /* um arquivo, ou vazio */
};

/* todos, na ordem do catálogo (os desligados também: ativo = false); devolve quantos */
int midia_servicos_ler(struct midia_servico *saida, int max);
const struct midia_servico *midia_servico_achar(const struct midia_servico *lista, int n, const char *id);
/* acrescenta um serviço; o id sai do nome. Só http(s). false se o endereço não serve ou não coube */
bool midia_servico_adicionar(const char *nome, const char *url, char *id_saida, size_t tam);
bool midia_servico_remover(const char *id);              /* só os adicionados */
bool midia_servico_ativar(const char *id, bool ativo);

int midia_recentes_ler(struct midia_recente *saida, int max);
void midia_recente_gravar(const struct midia_recente *r); /* troca o do mesmo serviço, sobe para o topo */
void midia_recente_apagar(const char *servico);

/* as pastas (criadas se faltam) */
char *midia_pasta_dados(void);       /* ~/.local/share/lumna/midia */
char *midia_icone(const char *id);   /* o arquivo do ícone, ou NULL */

#endif
