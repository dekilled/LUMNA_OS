/*
 * Painel > Emma > Importar do harness: traz as conversas, memórias, projetos, skills, personalidade e diretrizes de um backup (.zip) do
 * harness para o servidor da Emma do LUMNA. Mescla: o que o LUMNA já tem fica, os provedores, as chaves e o token não são tocados.
 * O backup se baixa no próprio harness (Configurações > Backup). O arquivo tem de estar na sua pasta pessoal.
 */
#include "painel.h"

#include <string.h>

#include "emmar.h"
#include "json.h"

static char caminho[600];
static bool trabalhando;
static char resultado[600];
static bool resultado_erro;

struct job {
	char caminho[600], resumo[600], erro[300];
	bool ok;
};

static int contar(const struct json *novos, const char *chave) {
	return (int)json_numero(json_chave(novos, chave), 0);
}

static void trabalho(void *d) {
	struct job *j = d;
	GString *c = g_string_new("{\"caminho\":");
	json_escrever_texto(c, j->caminho);
	g_string_append_c(c, '}');
	struct emmar r;
	j->ok = emmar_chamar("POST", "/api/backup/importar-arquivo", c->str, 0, &r);
	if (j->ok) {
		const struct json *novos = json_chave(r.corpo, "novos"), *cfg = json_chave(r.corpo, "configuracoes");
		g_snprintf(j->resumo, sizeof(j->resumo), "Importado: %d conversas, %d memórias, %d projetos e %d skills novos%s.", contar(novos, "sessions"),
			contar(novos, "memories"), contar(novos, "projects"), contar(novos, "skills"),
			cfg && cfg->nfilhos ? "; a personalidade e as diretrizes foram atualizadas" : "");
	} else {
		g_strlcpy(j->erro, r.msg, sizeof(j->erro));
	}
	emmar_liberar(&r);
	g_string_free(c, TRUE);
}

static void pronto(void *d) {
	struct job *j = d;
	trabalhando = false;
	resultado_erro = !j->ok;
	g_strlcpy(resultado, j->ok ? j->resumo : j->erro, sizeof(resultado));
	g_free(j);
}

void pn_emma_imp_abrir(void) {
	resultado[0] = '\0';
}

void pn_emma_imp_desenhar(void) {
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		gv_texto(CLAY_STRING("1. No harness: Configurações > Backup > baixar o backup (um .zip)."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(CLAY_STRING("2. Cole aqui o caminho do arquivo (ele precisa estar na sua pasta pessoal):"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_campo(CLAY_ID("emimp-caminho"), caminho, sizeof(caminho), "/home/você/Downloads/lumna-backup-….zip", GV_CAMPO_TEXTO);
		CLAY_AUTO_ID({ .layout = { .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao(CLAY_ID("emimp-ir"), "download", trabalhando ? "Importando…" : "Importar", GV_BOTAO_CHEIO) && !trabalhando && caminho[0]) {
				trabalhando = true;
				resultado[0] = '\0';
				struct job *j = g_new0(struct job, 1);
				g_strlcpy(j->caminho, caminho, sizeof(j->caminho));
				pn_tarefa(trabalho, pronto, j);
			}
		}
		if (resultado[0]) {
			Clay_Color cor = resultado_erro ? gv_t.perigo : gv_t.sucesso;
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 10, 10 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_alfa(cor, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_alfa(cor, 0.5f), .width = GV_BORDA(1) } }) {
				gv_icone(resultado_erro ? "info" : "check", 18, cor);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(resultado), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
			}
		}
		gv_texto(CLAY_STRING("Nada do que já está no LUMNA é apagado, e os provedores, as chaves e o token ficam como estão. Importar de novo não duplica."),
			gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}
