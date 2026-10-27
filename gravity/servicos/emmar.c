#include "emmar.h"

#include <glib.h>
#include <string.h>

#include "emmac.h"

bool emmar_chamar(const char *metodo, const char *caminho, const char *corpo, size_t limite, struct emmar *r) {
	memset(r, 0, sizeof(*r));
	if (!limite) {
		limite = 1024 * 1024;
	}
	char *buf = g_malloc(limite), *p = g_strdup_printf("rest %s %s %s", metodo, caminho, corpo ? corpo : "");
	r->servico = emmac_pedido(p, buf, limite);
	g_free(p);
	if (!r->servico) {
		g_strlcpy(r->msg, "O serviço da Emma não está rodando.", sizeof(r->msg));
		g_free(buf);
		return false;
	}
	r->raiz = json_ler(buf);
	g_free(buf);
	r->corpo = json_chave(r->raiz, "corpo");
	r->ok = json_bool(json_chave(r->raiz, "ok"), false);
	r->status = (int)json_numero(json_chave(r->raiz, "status"), 0);
	if (!r->ok) {
		const char *e = json_chave_texto(r->raiz, "erro"), *d = r->corpo && r->corpo->tipo == JSON_OBJETO ? json_chave_texto(r->corpo, "detail") : "";
		if (!r->raiz) {
			e = "resposta ilegível do serviço";
		}
		g_strlcpy(r->msg, *d ? d : *e ? e : r->status ? "O servidor da Emma recusou o pedido." : "O servidor da Emma não respondeu.", sizeof(r->msg));
	}
	return r->ok;
}

void emmar_liberar(struct emmar *r) {
	json_liberar(r->raiz);
	r->raiz = NULL;
	r->corpo = NULL;
}
