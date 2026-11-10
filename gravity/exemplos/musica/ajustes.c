/* Configurações do LUMNA Music: Reprodução, Biblioteca, Letras, Aparência e Emma. Cada mudança vale na hora (e é gravada). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

bool ajustes_dialogo(void) {
	return dialogo_aberto();
}

/* ---- as linhas ---- */

static bool linha_interruptor(Clay_ElementId id, const char *rotulo, const char *desc, bool *v, const char *icone) {
	bool mudou = false;
	CLAY(CLAY_IDI("mu-cfg-l", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(desc ? 66 : 52) }, .padding = { 4, 4, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		if (icone) {
			gv_icone(icone, 28, gv_t.acento);
		}
		CLAY(CLAY_IDI("mu-cfg-tx", id.id), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			if (desc) {
				gv_texto(gv_str(desc), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		mudou = gv_interruptor(id, v);
	}
	return mudou;
}

static bool linha_lista(Clay_ElementId id, const char *rotulo, const char *const *ops, int n, int *v) {
	bool mudou = false;
	CLAY(CLAY_IDI("mu-cfg-l", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .padding = { 4, 4, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		CLAY(CLAY_IDI("mu-cfg-tx", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("mu-cfg-dd", id.id), { .layout = { .sizing = { CLAY_SIZING_FIXED(330) } } }) {
			mudou = gv_lista_suspensa(id, ops, n, v);
		}
	}
	return mudou;
}

static bool linha_deslizante(Clay_ElementId id, const char *rotulo, int *v, int min, int max, const char *sufixo) {
	bool mudou = false;
	CLAY(CLAY_IDI("mu-cfg-l", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .padding = { 4, 4, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		CLAY(CLAY_IDI("mu-cfg-tx", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("mu-cfg-ds", id.id), { .layout = { .sizing = { CLAY_SIZING_FIXED(340) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			float v01 = (float)(*v - min) / (float)(max - min);
			if (gv_deslizante(id, &v01, 5, 16)) {
				int novo = min + (int)lroundf(v01 * (float)(max - min));
				if (novo != *v) {
					*v = novo;
					mudou = true;
				}
			}
		}
		CLAY(CLAY_IDI("mu-cfg-n", id.id), { .layout = { .sizing = { CLAY_SIZING_FIXED(56) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			gv_texto(gv_fmt("%d%s", *v, sufixo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	return mudou;
}

static void secao(const char *titulo) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 0, 14, 4 } } }) {
		gv_texto(gv_str(titulo), 28, gv_t.texto, GV_PESO_MEDIO);
	}
}

#define CARTAO(id) CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 16, 16, 0, 0 } }, \
	.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } })

/* ---- as abas ---- */

static void aba_reproducao(void) {
	secao("Áudio");
	CARTAO(CLAY_ID("mu-cartao-audio")) {
		/* a saída: "Padrão do sistema" e o que o mpv conhece */
		static const char *ops[16];
		static char *nomes_ops[16];
		int n = 0, atual = 0;
		ops[n] = "Padrão do sistema";
		nomes_ops[n] = NULL;
		n++;
		if (M.dispositivos) {
			static char *guarda[16];
			for (int i = 0; i < 16; i++) {
				g_free(guarda[i]);
				guarda[i] = NULL;
			}
			for (guint i = 0; i < M.dispositivos->len && n < 16; i++) {
				const char *l = M.dispositivos->pdata[i];
				const char *tab = strchr(l, '\t');
				if (!tab || !strncmp(l, "auto\t", 5)) {
					continue;
				}
				guarda[n] = g_strndup(l, (gsize)(tab - l));
				nomes_ops[n] = guarda[n];
				ops[n] = tab + 1;
				if (!strcmp(guarda[n], M.cfg.saida)) {
					atual = n;
				}
				n++;
			}
		}
		if (linha_lista(CLAY_ID("mu-cfg-saida"), "Dispositivo de saída", ops, n, &atual)) {
			g_strlcpy(M.cfg.saida, atual > 0 && nomes_ops[atual] ? nomes_ops[atual] : "", sizeof(M.cfg.saida));
			aplicar_cfg();
			salvar_cfg();
		}
		if (linha_interruptor(CLAY_IDI("mu-cfg", 0), "Normalizar volume", NULL, &M.cfg.normalizar, NULL)) {
			aplicar_cfg();
			salvar_cfg();
		}
		if (linha_interruptor(CLAY_IDI("mu-cfg", 1), "Reprodução sem intervalos", NULL, &M.cfg.sem_intervalos, NULL)) {
			aplicar_cfg();
			salvar_cfg();
		}
		if (linha_deslizante(CLAY_IDI("mu-cfg", 2), "Transição entre faixas", &M.cfg.transicao, 0, 12, " s")) {
			aplicar_cfg();
			salvar_cfg();
		}
	}
	secao("Fila");
	CARTAO(CLAY_ID("mu-cartao-fila")) {
		if (linha_interruptor(CLAY_IDI("mu-cfg", 3), "Restaurar fila ao abrir", NULL, &M.cfg.restaurar_fila, NULL)) {
			salvar_cfg();
		}
	}
	secao("Janela");
	CARTAO(CLAY_ID("mu-cartao-segplano")) {
		if (linha_interruptor(CLAY_IDI("mu-cfg", 30), "Continuar tocando ao fechar", "Fechar a janela com uma música tocando deixa o Music em segundo plano (o ícone da topbar e o widget da área de trabalho controlam). Desligado, fechar encerra o app.", &M.cfg.segundo_plano, NULL)) {
			salvar_cfg();
		}
	}
	secao("Assistente");
	CARTAO(CLAY_ID("mu-cartao-emma")) {
		if (linha_interruptor(CLAY_IDI("mu-cfg", 4), "Emma neste app", "Ativa a assistente Emma apenas no LUMNA Music.", &M.cfg.emma_no_app, "emma")) {
			salvar_cfg();
		}
	}
}

static void aba_biblioteca(void) {
	secao("Pastas da biblioteca");
	CARTAO(CLAY_ID("mu-cartao-pastas")) {
		int remover = -1;
		for (int i = 0; i < M.cfg.npastas; i++) {
			CLAY(CLAY_IDI("mu-cfg-pasta", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(50) }, .padding = { 4, 4, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.border = { .color = gv_t.borda, .width = { .top = i ? 1 : 0 } } }) {
				gv_icone("folder", 24, gv_t.texto_secundario);
				CLAY(CLAY_IDI("mu-cfg-pasta-t", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(M.cfg.pastas[i]), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_botao_redondo(CLAY_IDI("mu-pasta-rm", (uint32_t)i), "close", 34, false, false)) {
					remover = i;
				}
			}
		}
		if (M.cfg.npastas == 0) {
			CLAY(CLAY_ID("mu-cfg-sem-pasta"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 14, 8 } } }) {
				gv_texto(CLAY_STRING("Nenhuma pasta: a biblioteca fica vazia."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		if (remover >= 0) {
			memmove(M.cfg.pastas[remover], M.cfg.pastas[remover + 1], (size_t)(M.cfg.npastas - remover - 1) * sizeof(M.cfg.pastas[0]));
			M.cfg.npastas--;
			salvar_cfg();
			recarregar_biblioteca();
		}
		CLAY(CLAY_ID("mu-cfg-add-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 10, 12 }, .childGap = 12 } }) {
			if (gv_botao_ext(CLAY_ID("mu-adicionar-pasta"), &(gv_botao_opcoes){ .icone = "plus", .texto = "Adicionar pasta", .estilo = GV_BOTAO_CONTORNO, .altura = 44 })) {
				abrir_dialogo_pasta();
			}
			if (gv_botao_ext(CLAY_ID("mu-reescanear"), &(gv_botao_opcoes){ .icone = "refresh", .texto = M.carregando ? "Procurando…" : "Procurar agora", .estilo = GV_BOTAO_CONTORNO, .altura = 44, .desativado = M.carregando })) {
				recarregar_biblioteca();
			}
		}
		if (linha_interruptor(CLAY_IDI("mu-cfg", 5), "Incluir subpastas", NULL, &M.cfg.subpastas, NULL)) {
			salvar_cfg();
			recarregar_biblioteca();
		}
		if (linha_interruptor(CLAY_IDI("mu-cfg", 6), "Buscar novas músicas automaticamente", "A cada minuto e meio, e ao abrir o app.", &M.cfg.buscar_auto, NULL)) {
			salvar_cfg();
		}
	}
	secao("Capas");
	CARTAO(CLAY_ID("mu-cartao-capas")) {
		CLAY(CLAY_ID("mu-cfg-cache"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 4, 4, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("mu-cfg-cache-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_fmt("Capas guardadas: %.1f MB", (double)mu_capas_tamanho() / 1048576.0), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao_ext(CLAY_ID("mu-limpar-capas"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Limpar cache", .estilo = GV_BOTAO_CONTORNO, .altura = 42 })) {
				mu_capas_limpar();
				aviso("Cache de capas limpo");
			}
		}
	}
}

static void aba_letras(void) {
	secao("Letras");
	CARTAO(CLAY_ID("mu-cartao-letras")) {
		if (linha_interruptor(CLAY_IDI("mu-cfg", 7), "Mostrar letras", "De um arquivo .lrc com o mesmo nome ao lado da música, ou da tag do arquivo.", &M.cfg.letras_mostrar, NULL)) {
			salvar_cfg();
		}
		if (linha_interruptor(CLAY_IDI("mu-cfg", 8), "Seguir a reprodução ao abrir a faixa", "A letra rola sozinha junto com a música.", &M.cfg.letras_seguir, NULL)) {
			M.letra_seguir = M.cfg.letras_seguir;
			salvar_cfg();
		}
		static const char *const T[] = { "Pequena", "Média", "Grande" };
		if (linha_lista(CLAY_ID("mu-cfg-tam-letra"), "Tamanho da letra", T, 3, &M.cfg.letras_tamanho)) {
			salvar_cfg();
		}
	}
}

static void aba_aparencia(void) {
	secao("Capas e cores");
	CARTAO(CLAY_ID("mu-cartao-aparencia")) {
		if (linha_interruptor(CLAY_IDI("mu-cfg", 9), "Mostrar as capas", NULL, &M.cfg.capas, NULL)) {
			salvar_cfg();
		}
		if (linha_interruptor(CLAY_IDI("mu-cfg", 10), "Brilho da capa atrás da letra", "A cor média da capa acende o fundo de \"Ouvir agora\".", &M.cfg.fundo_capa, NULL)) {
			salvar_cfg();
		}
		if (linha_deslizante(CLAY_IDI("mu-cfg", 11), "Tamanho das capas nos álbuns", &M.cfg.tam_capa, 120, 240, " px")) {
			salvar_cfg();
		}
	}
}

static void aba_emma(void) {
	secao("Assistente");
	CARTAO(CLAY_ID("mu-cartao-emma2")) {
		if (linha_interruptor(CLAY_IDI("mu-cfg", 12), "Emma neste app", "Mostra o botão da Emma no alto do LUMNA Music, que abre a conversa.", &M.cfg.emma_no_app, "emma")) {
			salvar_cfg();
		}
		CLAY(CLAY_ID("mu-cfg-emma-bt"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 8, 14 } } }) {
			if (gv_botao_ext(CLAY_ID("mu-abrir-emma"), &(gv_botao_opcoes){ .icone = "emma", .texto = "Abrir a Emma", .estilo = GV_BOTAO_CONTORNO, .altura = 44 })) {
				char *argv[] = { "lumna-emma-chat", NULL };
				g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
			}
		}
	}
	CLAY(CLAY_ID("mu-cfg-emma-nota"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 0 } } }) {
		gv_texto(CLAY_STRING("A Emma ainda não controla a música (tocar, pular, escolher). Isso vem depois."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}

void tela_ajustes(void) {
	static const char *const ABAS[] = { "Reprodução", "Biblioteca", "Letras", "Aparência", "Emma" };
	Clay_ElementId rol = CLAY_ID("mu-rolagem");
	CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.padding = { 20, 20, 4, 24 }, .childGap = 12 } })) {
		CLAY(CLAY_ID("mu-cfg-cab"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
			gv_texto(CLAY_STRING("Configurações"), 40, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Ajuste o LUMNA Music do seu jeito"), gv_t.tamanho_corpo + 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("mu-abas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 8, 10 }, .childGap = 12 }, .border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			for (int i = 0; i < 5; i++) {
				Clay_ElementId id = CLAY_IDI("mu-aba", (uint32_t)i);
				bool ativa = M.ajustes_aba == i;
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(42) }, .padding = { 24, 24, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = ativa ? gv_t.acento : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(21),
					.border = { .color = ativa ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(1) } }) {
					gv_texto(gv_str(ABAS[i]), gv_t.tamanho_corpo, ativa ? gv_t.sobre_acento : gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_tocado(id)) {
					M.ajustes_aba = i;
				}
			}
		}
		switch (M.ajustes_aba) {
		case 0: aba_reproducao(); break;
		case 1: aba_biblioteca(); break;
		case 2: aba_letras(); break;
		case 3: aba_aparencia(); break;
		default: aba_emma(); break;
		}
	}
}
