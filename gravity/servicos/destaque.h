/*
 * Destaque de sintaxe para os blocos de código da conversa: o código vira marcação do pango (<span foreground=...>), sem trocar uma
 * letra do texto. Leve de propósito (palavras-chave, textos, comentários e números das linguagens comuns), não um analisador.
 */
#ifndef LUMNA_DESTAQUE_H
#define LUMNA_DESTAQUE_H

#include <glib.h>

struct destaque_cores {
	const char *palavra, *texto, *comentario, *numero, *tipo; /* "#rrggbb" */
};

/* a marcação do pango do código (já escapado); lingua pode ser "" ou desconhecida: aí só escapa. g_free */
char *destaque_markup(const char *lingua, const char *codigo, const struct destaque_cores *cores);
/* a extensão de arquivo da linguagem ("py", "sh"...; "txt" se não souber) */
const char *destaque_extensao(const char *lingua);
/* este texto parece código (várias linhas, a maioria com cara de código)? Para oferecer enviar como bloco. */
gboolean destaque_parece_codigo(const char *texto);
/* a linguagem mais provável de um texto sem fence ("" se não souber) */
const char *destaque_adivinhar(const char *texto);

#endif
