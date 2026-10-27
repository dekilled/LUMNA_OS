/*
 * As conversas, as rotinas e as memórias da Emma, guardadas em arquivos no PC (~/.local/share/lumna/emma): uma conversa por arquivo em
 * sessoes/, e as rotinas com as memórias em rotinas.json. Nada disso depende de servidor. O app usa as mesmas rotas de sempre (/api/sessions...)
 * por emmas_rest(); a conversa (emmal.c) lê e escreve por emmas_historico() e emmas_anexar().
 */
#ifndef LUMNA_EMMAS_H
#define LUMNA_EMMAS_H

#include <glib.h>
#include <stdbool.h>

/* GET/POST/PATCH/DELETE em /api/sessions, /api/projects, /api/memories e /api/skills; devolve o JSON (g_free) e o status HTTP, ou NULL se a rota não é daqui */
char *emmas_rest(const char *metodo, const char *caminho, const char *corpo, int *status);

/* a conversa de um canal ("assist": o painel flutuante; "app" não usa: o app escolhe a dele); cria quando não há */
bool emmas_sessao_canal(const char *canal, char *sid, size_t n);
void emmas_nova_sessao_canal(const char *canal);

/* as últimas max falas (user/assistant) no formato do provedor: [{"role":"user","content":"..."}...] (g_free) */
char *emmas_historico(const char *sid, int max);
/* uma fala nova (role "user" ou "assistant"; tools: um array JSON do que a Emma fez, ou NULL) */
bool emmas_anexar(const char *sid, const char *role, const char *texto, const char *tools_json);
bool emmas_existe(const char *sid);
bool emmas_garantir(const char *sid); /* cria a cópia local de uma conversa do servidor, com o mesmo id */

#endif
