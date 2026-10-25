/* As ajudas que as seções do Painel partilham: os arquivos de config e as linhas de ajuste. */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void pn_caminho(char *buf, size_t tam, const char *nome) {
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *casa = getenv("HOME");
	if (config && *config) {
		snprintf(buf, tam, "%s/lumna/%s", config, nome);
	} else {
		snprintf(buf, tam, "%s/.config/lumna/%s", casa ? casa : "", nome);
	}
}

bool pn_gravar(const char *nome, const char *conteudo) {
	char caminho[1024], temp[1100];
	pn_caminho(caminho, sizeof(caminho), nome);
	char *pasta = g_path_get_dirname(caminho);
	g_mkdir_with_parents(pasta, 0755);
	g_free(pasta);
	snprintf(temp, sizeof(temp), "%s.novo", caminho);
	if (!g_file_set_contents(temp, conteudo, -1, NULL)) {
		return false;
	}
	return rename(temp, caminho) == 0;
}

void pn_subtitulo(Clay_ElementId id, const char *texto) {
	CLAY(id, { .layout = { .padding = { 0, 0, 14, 8 } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
	}
}

void pn_linha_comecar(Clay_ElementId id, const char *icone, const char *nome, const char *sub) {
	Clay__OpenElementWithId(id);
	Clay__ConfigureOpenElement((Clay_ElementDeclaration){
		.layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 2, 12, 12 }, .childGap = 16,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } },
	});
	if (icone) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(30) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
			gv_icone(icone, 24, gv_t.icone);
		}
	}
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
		gv_texto(gv_str(nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
		if (sub) {
			gv_texto(gv_str(sub), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

void pn_linha_acabar(void) {
	Clay__CloseElement();
}

bool pn_ajuste_lista(Clay_ElementId id, const char *icone, const char *nome, const char *sub, const char *const *opcoes,
	int n, int *escolha) {
	bool mudou = false;
	pn_linha_comecar(id, icone, nome, sub);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(260) } } }) {
		mudou = gv_lista_suspensa(Clay__HashStringWithOffset(id.stringId, 9000001u, id.baseId), opcoes, n, escolha);
	}
	pn_linha_acabar();
	return mudou;
}

bool pn_segmentos(Clay_ElementId id, const char *const *opcoes, int n, int *escolha) {
	bool mudou = false;
	CLAY(id, { .layout = { .childGap = 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		for (int i = 0; i < n; i++) {
			Clay_ElementId s = Clay__HashStringWithOffset(id.stringId, 7000000u + (uint32_t)i, id.baseId);
			bool esta = *escolha == i;
			bool tocado = gv_tocado(s);
			Clay_CornerRadius r = { i == 0 ? gv_t.raio_pequeno : 0, i == n - 1 ? gv_t.raio_pequeno : 0,
				i == 0 ? gv_t.raio_pequeno : 0, i == n - 1 ? gv_t.raio_pequeno : 0 };
			CLAY(s, { .layout = { .sizing = { CLAY_SIZING_FIXED(120), CLAY_SIZING_FIXED(34) },
					.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = esta ? gv_t.selecionado : gv_por_cima(s) ? gv_t.sob_cursor : (Clay_Color){ 0 },
				.cornerRadius = r,
				.border = { .color = esta ? gv_alfa(gv_t.acento, 0.7f) : gv_t.borda_controle,
					.width = { .left = i > 0 ? 1 : 0 } } }) {
				tocado |= gv_focavel(s, gv_t.raio_pequeno);
				gv_texto(gv_str(opcoes[i]), gv_t.tamanho_corpo, esta ? gv_t.acento : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (tocado && !esta) {
				*escolha = i;
				mudou = true;
			}
		}
	}
	return mudou;
}

void pn_executar(const char *comando) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			setsid();
			execl("/bin/sh", "sh", "-c", comando, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}
