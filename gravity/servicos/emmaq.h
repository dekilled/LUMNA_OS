/*
 * Arquivos e comandos da Emma: o que ela lê, escreve, apaga e executa no computador, e a política que decide cada chamada.
 *
 *   A pasta dela é ~/Emma (com Histórias e Scripts): lá dentro ela lê e escreve à vontade (copiando antes o que for sobrescrever).
 *   Na sua pasta pessoal ela lê, e cria arquivos novos, sem pedir. Pede a sua aprovação (mostrando o que, onde, por quê e o código)
 *   para: sobrescrever ou acrescentar a um arquivo seu que não é dela, mexer em pasta ou arquivo oculto (.config, .bashrc...),
 *   apagar (vai para a lixeira) e tudo que fica FORA da pasta pessoal. Comandos sempre pedem.
 *   Recusa sempre (nem com aprovação): as chaves e o banco do próprio LUMNA, chaves privadas (~/.ssh, ~/.gnupg) e o que o sistema
 *   guarda de senhas; nenhum motivo bom precisa disso e um texto malicioso poderia tentar.
 */
#ifndef LUMNA_EMMAQ_H
#define LUMNA_EMMAQ_H

#include <glib.h>
#include <stdbool.h>

enum emmaq_op { EMMAQ_LER, EMMAQ_LISTAR, EMMAQ_ESCREVER, EMMAQ_ANEXAR, EMMAQ_APAGAR, EMMAQ_EXECUTAR };
enum emmaq_veredito { EMMAQ_LIVRE, EMMAQ_APROVAR, EMMAQ_NEGAR };

struct emmaq_juizo {
	enum emmaq_veredito veredito;
	char *real;   /* o caminho já resolvido (absoluto, sem ~ nem ..), g_free */
	char *porque; /* a razão, em português, quando pede aprovação ou nega (senão NULL) */
};

/* a pasta da Emma (~/Emma); g_free */
char *emmaq_pasta_dela(void);
/* julga uma operação sobre um caminho (relativo: dentro de ~/Emma; ~ vale a pessoal). Libere com emmaq_juizo_limpar */
struct emmaq_juizo emmaq_julgar(enum emmaq_op op, const char *caminho);
void emmaq_juizo_limpar(struct emmaq_juizo *j);

/* as execuções (o caminho já é o resolvido). Devolvem o que contar ao modelo (g_free) e, em erro, NULL com *erro (g_free) */
char *emmaq_listar(const char *real, char **erro);
char *emmaq_ler(const char *real, char **erro);
char *emmaq_escrever(const char *real, const char *conteudo, bool anexar, char **erro);
char *emmaq_apagar(const char *real, char **erro);
/* roda "sh -c comando" na pasta (NULL: ~/Emma), no máximo timeout segundos; a saída vem cortada */
char *emmaq_executar(const char *comando, const char *pasta, int timeout, bool *ok, char **erro);

/* o que mostrar a quem vai aprovar um comando: ele mais o conteúdo dos arquivos pequenos que ele chama (g_free) */
char *emmaq_detalhe_comando(const char *comando, const char *pasta);

#endif
