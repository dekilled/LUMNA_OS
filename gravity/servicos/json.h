/*
 * JSON pequeno, o bastante para o controle remoto e o DevTools do navegador: lê um texto para
 * uma árvore, consulta por chave e escreve texto escapado. Não guarda a ordem de nada além da
 * dos filhos, e números são double.
 *
 *   struct json *j = json_ler(texto);
 *   const char *tipo = json_texto(json_chave(j, "type"));   // NULL se não houver ou não for texto
 *   json_liberar(j);
 */
#ifndef LUMNA_JSON_H
#define LUMNA_JSON_H

#include <glib.h>
#include <stdbool.h>

enum json_tipo { JSON_NULO, JSON_BOOL, JSON_NUMERO, JSON_TEXTO, JSON_LISTA, JSON_OBJETO };

struct json {
	enum json_tipo tipo;
	bool b;
	double n;
	char *s;             /* texto */
	char *chave;         /* a chave, se for filho de um objeto */
	struct json **filhos;
	int nfilhos;
};

struct json *json_ler(const char *texto);   /* NULL se inválido */
void json_liberar(struct json *j);
const struct json *json_chave(const struct json *obj, const char *chave);
const struct json *json_item(const struct json *lista, int i);
const char *json_texto(const struct json *j);                  /* NULL se não for texto */
double json_numero(const struct json *j, double padrao);
bool json_bool(const struct json *j, bool padrao);
/* atalho: o texto de obj[chave], ou "" */
const char *json_chave_texto(const struct json *obj, const char *chave);

/* escrita: acrescenta a um GString, com aspas e escapes */
void json_escrever_texto(GString *s, const char *texto);
void json_escrever(GString *s, const struct json *j);   /* a árvore de volta, compacta */

#endif
