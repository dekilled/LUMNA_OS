/*
 * Painel > Emma > Personalidade: o jeito da Emma e as diretrizes dela. Vivem no servidor da Emma (as Configurações dele); aqui se edita.
 * Uma cópia fica em ~/.config/lumna/emma/persona.json para a conversa direta (sem servidor) falar igual: o arquivo é só uma cópia
 * automática, ninguém o edita à mão.
 */
#include "painel.h"

#include <glib/gstdio.h>
#include <string.h>

#include "emmar.h"
#include "json.h"

#define TAM 6000

static char pers[TAM], dirs[TAM];
static bool lido, carregando, salvando, sujo, nsfw;
static char aviso[300];
static bool aviso_erro;

struct job {
	bool salvar, nsfw, ok, servidor;
	char pers[TAM], dirs[TAM], erro[240];
};

static char *caminho_local(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "emma", "persona.json", NULL);
}

static void gravar_local(const char *p, const char *d) {
	GString *g = g_string_new("{\"personalidade\":");
	json_escrever_texto(g, p);
	g_string_append(g, ",\"diretrizes\":");
	json_escrever_texto(g, d);
	g_string_append_c(g, '}');
	char *cam = caminho_local(), *dir = g_path_get_dirname(cam), *tmp = g_strconcat(cam, ".tmp", NULL);
	g_mkdir_with_parents(dir, 0700);
	if (g_file_set_contents(tmp, g->str, -1, NULL)) {
		g_chmod(tmp, 0600);
		g_rename(tmp, cam);
	}
	g_free(tmp);
	g_free(dir);
	g_free(cam);
	g_string_free(g, TRUE);
}

static void trabalho(void *d) {
	struct job *j = d;
	if (j->salvar) {
		gravar_local(j->pers, j->dirs);
		GString *c = g_string_new(NULL);
		g_string_printf(c, "{\"%s\":", j->nsfw ? "personality" : "personality_sfw");
		json_escrever_texto(c, j->pers);
		g_string_append_printf(c, ",\"%s\":", j->nsfw ? "system_prompt" : "system_prompt_sfw");
		json_escrever_texto(c, j->dirs);
		g_string_append_c(c, '}');
		struct emmar r;
		j->ok = emmar_chamar("PUT", "/api/settings", c->str, 0, &r);
		j->servidor = j->ok;
		if (!j->ok) {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
		g_string_free(c, TRUE);
		return;
	}
	struct emmar r;
	if (emmar_chamar("GET", "/api/settings", NULL, 0, &r)) {
		j->nsfw = json_bool(json_chave(r.corpo, "profile_nsfw"), false);
		g_strlcpy(j->pers, json_chave_texto(r.corpo, j->nsfw ? "personality" : "personality_sfw"), sizeof(j->pers));
		g_strlcpy(j->dirs, json_chave_texto(r.corpo, j->nsfw ? "system_prompt" : "system_prompt_sfw"), sizeof(j->dirs));
		j->ok = j->servidor = true;
	} else {
		g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		char *cam = caminho_local(), *txt = NULL;
		if (g_file_get_contents(cam, &txt, NULL, NULL)) { /* sem servidor: o que ficou guardado */
			struct json *js = json_ler(txt);
			g_strlcpy(j->pers, json_chave_texto(js, "personalidade"), sizeof(j->pers));
			g_strlcpy(j->dirs, json_chave_texto(js, "diretrizes"), sizeof(j->dirs));
			json_liberar(js);
			j->ok = true;
		}
		g_free(txt);
		g_free(cam);
	}
	emmar_liberar(&r);
}

static void pronto(void *d) {
	struct job *j = d;
	if (j->salvar) {
		salvando = false;
		if (j->servidor) {
			sujo = false;
			g_strlcpy(aviso, "Personalidade e diretrizes salvas.", sizeof(aviso));
			aviso_erro = false;
		} else {
			sujo = false; /* a cópia local foi gravada: a conversa direta já usa */
			g_snprintf(aviso, sizeof(aviso), "Salvo aqui, mas o servidor da Emma não recebeu (%s). Salve de novo quando ele estiver ligado.", j->erro);
			aviso_erro = true;
		}
	} else {
		carregando = false;
		lido = true;
		if (j->ok && !sujo) {
			g_strlcpy(pers, j->pers, sizeof(pers));
			g_strlcpy(dirs, j->dirs, sizeof(dirs));
			nsfw = j->nsfw;
		}
		if (!j->servidor) {
			g_snprintf(aviso, sizeof(aviso), "Sem conexão com o servidor da Emma%s%s. O que você escrever fica guardado aqui.", j->erro[0] ? ": " : "", j->erro);
			aviso_erro = true;
		}
	}
	g_free(j);
}

static void carregar(void) {
	if (carregando) {
		return;
	}
	carregando = true;
	pn_tarefa(trabalho, pronto, g_new0(struct job, 1));
}

void pn_emma_pers_abrir(void) {
	aviso[0] = '\0';
	lido = false;
}

bool pn_emma_pers_pendente(void) {
	return sujo;
}

void pn_emma_pers_aplicar(void) {
	if (salvando) {
		return;
	}
	salvando = true;
	struct job *j = g_new0(struct job, 1);
	j->salvar = true;
	j->nsfw = nsfw;
	g_strlcpy(j->pers, pers, sizeof(j->pers));
	g_strlcpy(j->dirs, dirs, sizeof(j->dirs));
	pn_tarefa(trabalho, pronto, j);
}

void pn_emma_pers_descartar(void) {
	sujo = false;
	lido = false;
}

static void bloco(const char *id, const char *titulo, const char *dica, char *texto, size_t tam, const char *vazio, int linhas) {
	CLAY(CLAY_SID(gv_fmt("emper-%s", id)), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6, .padding = { 0, 0, 0, 14 } } }) {
		gv_texto(gv_str(titulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
		gv_texto(gv_str(dica), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		if (gv_campo_area(CLAY_SID(gv_fmt("emper-c-%s", id)), texto, tam, vazio, linhas, linhas + 6) & GV_CAMPO_MUDOU) {
			sujo = true;
		}
	}
}

void pn_emma_pers_desenhar(void) {
	if (!lido && !carregando && !sujo) {
		carregar();
	}
	if (aviso[0]) {
		Clay_Color cor = aviso_erro ? gv_t.perigo : gv_t.sucesso;
		CLAY(CLAY_ID("emper-aviso"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 8, 10, 10 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(cor, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_alfa(cor, 0.5f), .width = GV_BORDA(1) } }) {
			gv_icone(aviso_erro ? "info" : "check", 18, cor);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(aviso), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao(CLAY_ID("emper-aviso-x"), "close", NULL, GV_BOTAO_SIMPLES)) {
				aviso[0] = '\0';
			}
		}
	}
	if (carregando && !lido) {
		gv_texto(CLAY_STRING("Lendo a personalidade da Emma…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	bloco("p", "Personalidade", "Quem a Emma é e como ela fala: o jeito, o humor, o carinho.", pers, sizeof(pers), "Escreva ou cole aqui a personalidade da Emma…", 7);
	bloco("d", "Diretrizes", "Regras que ela sempre segue, do seu jeito (o que fazer e o que evitar).", dirs, sizeof(dirs), "Escreva ou cole aqui as diretrizes…", 5);
	gv_texto(CLAY_STRING("Clique em Aplicar para salvar. Vale na conversa pelo servidor e na conversa direta."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
}
