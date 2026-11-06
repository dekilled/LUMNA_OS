/*
 * Idioma e região: o idioma do sistema (LANG), os formatos (LC_TIME, LC_NUMERIC, LC_MONETARY, LC_PAPER, LC_MEASUREMENT) e
 * os idiomas disponíveis. Quem muda é o localectl (o systemd pede a senha pelo polkit); o que fica gravado está em
 * /etc/locale.conf, que se lê direto. Os idiomas gerados vêm do `locale -a`; para deixar mais um disponível, o
 * session/lumna-idioma-gerar (pkexec). Para testar, $LUMNA_ETC aponta para um /etc falso e o PATH para um localectl,
 * um locale e um pkexec falsos.
 */
#ifndef LUMNA_IDIOMA_H
#define LUMNA_IDIOMA_H

#include <stdbool.h>
#include <stddef.h>

#define IDIOMA_MAX 400

struct idioma_cfg {
	char lang[48];
	char tempo[48], numeros[48], moeda[48], papel[48], medidas[48]; /* "" : o mesmo do idioma */
};

/* o /etc/locale.conf de agora */
void idioma_ler(struct idioma_cfg *c);
/* "pt_BR.UTF-8" → "Português (Brasil)" (o código, se não souber o nome) */
void idioma_nome(const char *locale, char *saida, size_t tam);
/* os idiomas disponíveis (gerados, em UTF-8), por nome; devolve quantos */
int idioma_geradas(char lista[][48], int max);
/* os que o sistema sabe gerar e ainda não estão (de /usr/share/i18n/SUPPORTED, só UTF-8) */
int idioma_suportados(char lista[][48], int max);
/* muda (localectl set-locale); erro recebe o motivo se falhar. Vale para os próximos logins. */
bool idioma_definir(const struct idioma_cfg *c, char *erro, size_t tam);
/* deixa um idioma disponível (pkexec lumna-idioma-gerar) */
bool idioma_gerar(const char *locale, char *erro, size_t tam);
/* como ficam a data, os números e o dinheiro nesse idioma ("" se ele não está gerado aqui) */
void idioma_amostra(const char *locale, char *data, char *numero, char *moeda, size_t tam);

#endif
