/*
 * O que a Emma faz no computador: o catálogo de ações (que vai ao backend como lumna_tools, para o modelo
 * chamar), a execução, as permissões (as mesmas do shell antigo: ~/.config/lumna/emma.json, chave permissions)
 * e os comandos curtos que se entendem sem o modelo ("pausa", "abre a Netflix", "volume 30"), rápidos e
 * sem internet.
 *
 * Primeira leva (a Mídia vem primeiro): midia.estado/abrir/fechar/inicio/seletor/controle/navegar/digitar,
 * audio.volume, mode.atual/trocar/trocar_telas, apps.listar/abrir, notificar. As outras do catálogo
 * antigo (arquivos, Wi-Fi, loja, serviços...) entram nas próximas levas.
 *
 * Tudo roda na linha principal do serviço (o MPRIS e o Orbit não gostam de vários).
 */
#ifndef LUMNA_EMMAA_H
#define LUMNA_EMMAA_H

#include <glib.h>
#include <stdbool.h>

#include "json.h"

enum { EMMAA_LER, EMMAA_AGIR, EMMAA_PERIGO };

struct emmaa_def {
	const char *nome;       /* "midia.abrir" (para o modelo: lumna_midia_abrir) */
	const char *rotulo;
	const char *descricao;
	const char *params;     /* as propriedades do JSON Schema */
	const char *obrigatorios; /* separados por vírgula */
	int risco;
	const char *padrao;     /* "auto", "ask", "off" */
};

struct emmaa_res {
	bool ok;
	char *resumo;           /* para a tela e para falar */
	char *conteudo;         /* para o modelo (pode ser NULL) */
};

void emmaa_iniciar(void);
const struct emmaa_def *emmaa_catalogo(int *n);
const struct emmaa_def *emmaa_achar(const char *nome);
const struct emmaa_def *emmaa_achar_ferramenta(const char *ferramenta); /* lumna_midia_abrir -> midia.abrir */
/* o JSON de lumna_tools; bloqueado: só o que é seguro com a tela bloqueada */
char *emmaa_ferramentas_json(bool bloqueado);
/* a permissão que vale: a da pessoa, mas o que é perigoso nunca roda sem perguntar */
const char *emmaa_permissao(const struct emmaa_def *d);
bool emmaa_seguro_bloqueado(const struct emmaa_def *d);

/* executa sem perguntar nada (quem chama já decidiu) */
void emmaa_executar(const struct emmaa_def *d, const struct json *args, struct emmaa_res *r);
void emmaa_res_limpar(struct emmaa_res *r);

/* um comando curto que se entende sozinho; true se era um, e então r tem o que fazer/dizer */
bool emmaa_intencao(const char *texto, struct emmaa_res *r);
/* o texto sem acento, minúsculo, só letras e números separados por um espaço */
char *emmaa_normalizar(const char *texto);

#endif
