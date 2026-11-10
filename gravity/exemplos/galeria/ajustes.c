/* A engrenagem da Galeria: Biblioteca, Visualização, Edição, Reprodução e Privacidade. Mexe-se em G.edit; só o Aplicar vale (e grava). */
#include <string.h>
#include <sys/stat.h>

#include "app.h"

static const struct { const char *rotulo, *icone, *titulo, *sub; } PAGINAS[] = {
	{ "Biblioteca", "stack", "Biblioteca", "Escolha de onde vêm suas fotos e vídeos." },
	{ "Visualização", "image", "Visualização", "Defina como suas fotos e vídeos aparecem." },
	{ "Edição", "sliders", "Edição", "O que vale quando você edita e salva uma cópia." },
	{ "Reprodução", "play", "Reprodução", "Como os vídeos tocam." },
	{ "Privacidade", "lock", "Privacidade", "O que a Galeria guarda e mostra." },
};

static int64_t cache_bytes = -1;       /* lido ao abrir a página */
static int menu_pasta = -1;            /* a pasta do "..." aberto */
static bool add_aberto;
static char add_caminho[4096];
static char add_erro[160];

void ajustes_abrir(int pagina) {
	G.edit = G.cfg;
	G.pagina_ajustes = pagina;
	G.tela = T_AJUSTES;
	cache_bytes = -1;
	menu_pasta = -1;
	add_aberto = false;
}

/* ---- peças ---- */

static void cartao(Clay_ElementId id, const char *titulo, void (*corpo)(void)) {
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(18), .childGap = 10 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(titulo), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		corpo();
	}
}

static bool linha_suspensa(Clay_ElementId id, const char *rotulo, const char *const *ops, int n, int *v) {
	bool mudou = false;
	CLAY(CLAY_IDI("aj-l", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 4, 4 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("aj-ls", id.id), { .layout = { .sizing = { CLAY_SIZING_FIXED(210) } } }) {
			mudou = gv_lista_suspensa(id, ops, n, v);
		}
	}
	return mudou;
}

static bool linha_interruptor(Clay_ElementId id, const char *rotulo, bool *v) {
	bool mudou = false;
	CLAY(CLAY_IDI("aj-l", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 6 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		mudou = gv_interruptor(id, v);
	}
	return mudou;
}

static void linha_deslizante(Clay_ElementId id, const char *rotulo, int *valor, int min, int max, const char *sufixo) {
	CLAY(CLAY_IDI("aj-l", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 6 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("aj-ds", id.id), { .layout = { .sizing = { CLAY_SIZING_FIXED(160) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			float v01 = (float)(*valor - min) / (float)(max - min);
			if (gv_deslizante(id, &v01, 5, 16)) {
				*valor = min + (int)(v01 * (max - min) + 0.5f);
			}
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(52) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			gv_texto(gv_fmt("%d%s", *valor, sufixo), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

/* ---- Biblioteca ---- */

static void corpo_pastas(void) {
	for (int i = 0; i < G.edit.npastas; i++) {
		Clay_ElementId id = CLAY_IDI("aj-pasta", (uint32_t)i), mais = CLAY_IDI("aj-pasta-mais", (uint32_t)i);
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 8, 6, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			gv_icone("folder", 22, gv_t.texto_secundario);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
				char *casa = g_strdup(g_get_home_dir());
				const char *p = G.edit.pastas[i];
				gv_texto(gv_str(g_str_has_prefix(p, casa) ? gv_fmt("~%s", p + strlen(casa)).chars : p), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				g_free(casa);
			}
			if (gv_botao_redondo(mais, "more", 34, menu_pasta == i, false)) {
				menu_pasta = menu_pasta == i ? -1 : i;
				gv_popup_alternar(CLAY_ID("aj-menu-pasta"));
			}
		}
	}
	if (G.edit.npastas == 0) {
		gv_texto(CLAY_STRING("Nenhuma pasta: a Galeria fica vazia."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	CLAY(CLAY_ID("aj-add-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 8 } } }) {
		if (gv_botao_ext(CLAY_ID("aj-add"), &(gv_botao_opcoes){ .icone = "plus", .texto = "Adicionar pasta", .estilo = GV_BOTAO_ELEVADO, .altura = 40 }) && G.edit.npastas < GL_PASTAS_MAX) {
			add_aberto = true;
			add_erro[0] = '\0';
			g_snprintf(add_caminho, sizeof(add_caminho), "%s/", g_get_home_dir());
			gv_focar(CLAY_ID("aj-add-campo"));
		}
	}
	linha_interruptor(CLAY_ID("aj-auto"), "Buscar novas mídias automaticamente", &G.edit.buscar_auto);
}

static void corpo_miniaturas(void) {
	static const char *const Q[] = { "Rápida", "Equilibrada", "Alta" };
	linha_suspensa(CLAY_ID("aj-qual"), "Qualidade", Q, 3, &G.edit.qualidade);
	if (cache_bytes < 0) {
		cache_bytes = gl_cache_tamanho();
	}
	CLAY(CLAY_ID("aj-cache"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 10, 10 }, .childGap = 12 }, .border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(CLAY_STRING("Cache em uso"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		char t[32];
		g_snprintf(t, sizeof(t), cache_bytes >= 1048576 ? "%.0f MB" : "%.0f KB", cache_bytes >= 1048576 ? cache_bytes / 1048576.0 : cache_bytes / 1024.0);
		gv_texto(gv_str(t), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	CLAY(CLAY_ID("aj-limpar-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
		if (gv_botao_ext(CLAY_ID("aj-limpar"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Limpar cache", .estilo = GV_BOTAO_ELEVADO, .altura = 40 })) {
			gl_cache_limpar();
			cache_bytes = 0;
			aviso("Cache limpo");
			gv_app_redesenhar(G.app);
		}
	}
}

static void corpo_organizacao(void) {
	static const char *const A[] = { "Data da captura", "Data de modificação", "Pasta", "Nenhum" };
	linha_suspensa(CLAY_ID("aj-agrupar"), "Agrupar por", A, 4, &G.edit.agrupar);
	linha_interruptor(CLAY_ID("aj-subpastas"), "Incluir subpastas", &G.edit.subpastas);
}

static void corpo_importar(void) {
	static const char *const D[] = { "Perguntar", "Substituir", "Manter os dois", "Pular" };
	linha_suspensa(CLAY_ID("aj-dup"), "Arquivos duplicados", D, 4, &G.edit.duplicados);
	linha_interruptor(CLAY_ID("aj-manter"), "Manter arquivos na pasta original", &G.edit.manter_original);
}

static void pagina_biblioteca(void) {
	CLAY(CLAY_ID("aj-b1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16 } }) {
		cartao(CLAY_ID("aj-c-pastas"), "Pastas da biblioteca", corpo_pastas);
		cartao(CLAY_ID("aj-c-mini"), "Miniaturas", corpo_miniaturas);
	}
	CLAY(CLAY_ID("aj-b2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16 } }) {
		cartao(CLAY_ID("aj-c-org"), "Organização", corpo_organizacao);
		cartao(CLAY_ID("aj-c-imp"), "Ao importar", corpo_importar);
	}
}

/* ---- as outras páginas ---- */

static void corpo_visualizacao(void) {
	static const char *const V[] = { "Grade", "Lista" };
	static const char *const AJ[] = { "Ajustadas à tela", "No tamanho real" };
	linha_suspensa(CLAY_ID("aj-vista"), "Visualização padrão", V, 2, &G.edit.vista);
	int aj = G.edit.ajustar_tela ? 0 : 1;
	if (linha_suspensa(CLAY_ID("aj-ajuste"), "Abrir as fotos", AJ, 2, &aj)) {
		G.edit.ajustar_tela = aj == 0;
	}
	linha_interruptor(CLAY_ID("aj-nomes"), "Mostrar o nome do arquivo nas miniaturas", &G.edit.nomes);
}

static void corpo_edicao(void) {
	static const char *const F[] = { "O mesmo do original", "JPEG", "PNG" };
	linha_suspensa(CLAY_ID("aj-formato"), "Formato da cópia salva", F, 3, &G.edit.formato);
	linha_deslizante(CLAY_ID("aj-qjpeg"), "Qualidade do JPEG", &G.edit.qualidade_jpeg, 50, 100, "%");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 0 } } }) {
		gv_texto(CLAY_STRING("O original nunca é alterado: o editor sempre salva uma cópia."), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}

static void corpo_reproducao(void) {
	static const char *const P[] = { "O player da Galeria", "O mpv, numa janela separada", "O aplicativo padrão do sistema" };
	linha_suspensa(CLAY_ID("aj-player"), "Abrir os vídeos com", P, 3, &G.edit.video_com);
	linha_deslizante(CLAY_ID("aj-volume"), "Volume", &G.edit.volume, 0, 100, "%");
	linha_interruptor(CLAY_ID("aj-repetir"), "Repetir o vídeo", &G.edit.repetir);
}

static void corpo_privacidade(void) {
	linha_interruptor(CLAY_ID("aj-cache-disco"), "Guardar as miniaturas no disco", &G.edit.cache);
	linha_interruptor(CLAY_ID("aj-lixeira"), "Mostrar a Lixeira na barra lateral", &G.edit.lixeira_na_barra);
	linha_interruptor(CLAY_ID("aj-confirmar"), "Confirmar antes de excluir para sempre", &G.edit.confirmar_apagar);
}

static void restaurar_pagina(void) {
	gl_cfg p;
	gl_cfg_padrao(&p);
	switch (G.pagina_ajustes) {
	case 0:
		memcpy(G.edit.pastas, p.pastas, sizeof(p.pastas));
		G.edit.npastas = p.npastas;
		G.edit.buscar_auto = p.buscar_auto;
		G.edit.qualidade = p.qualidade;
		G.edit.agrupar = p.agrupar;
		G.edit.subpastas = p.subpastas;
		G.edit.duplicados = p.duplicados;
		G.edit.manter_original = p.manter_original;
		break;
	case 1: G.edit.vista = p.vista; G.edit.ajustar_tela = p.ajustar_tela; G.edit.nomes = p.nomes; break;
	case 2: G.edit.formato = p.formato; G.edit.qualidade_jpeg = p.qualidade_jpeg; break;
	case 3: G.edit.video_com = p.video_com; G.edit.volume = p.volume; G.edit.repetir = p.repetir; break;
	default: G.edit.cache = p.cache; G.edit.lixeira_na_barra = p.lixeira_na_barra; G.edit.confirmar_apagar = p.confirmar_apagar; break;
	}
}

static void menu_da_pasta(void) {
	if (menu_pasta < 0 || menu_pasta >= G.edit.npastas) {
		return;
	}
	if (gv_menu_item(CLAY_ID("aj-mp-remover"), "trash", "Remover da biblioteca", NULL)) {
		memmove(G.edit.pastas[menu_pasta], G.edit.pastas[menu_pasta + 1], (size_t)(G.edit.npastas - menu_pasta - 1) * sizeof(G.edit.pastas[0]));
		G.edit.npastas--;
		menu_pasta = -1;
		gv_popup_fechar(CLAY_ID("aj-menu-pasta"));
	}
}

/* ---- a tela ---- */

void ajustes_desenhar(void) {
	CLAY(CLAY_ID("cfg-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("cfg-nav"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(260), CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 14, 12 }, .childGap = 4 },
			.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
			Clay_ElementId vol = CLAY_ID("cfg-voltar");
			CLAY(vol, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 8, 8, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(vol) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
				gv_icone("arrow-left", 20, gv_t.texto_secundario);
				gv_texto(CLAY_STRING("Voltar à galeria"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_tocado(vol)) {
				G.tela = T_BIBLIOTECA;
			}
			CLAY(CLAY_ID("cfg-nav-t"), { .layout = { .padding = { 8, 8, 18, 8 } } }) {
				gv_texto(CLAY_STRING("Configurações"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			}
			for (int pg = 0; pg < 5; pg++) {
				Clay_ElementId id = CLAY_IDI("cfg-pg", (uint32_t)pg);
				bool ativo = G.pagina_ajustes == pg;
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .padding = { 12, 8, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = ativo ? gv_t.acento_premido : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
					gv_icone(PAGINAS[pg].icone, 22, ativo ? gv_t.sobre_acento : gv_t.texto_secundario);
					gv_texto(gv_str(PAGINAS[pg].rotulo), gv_t.tamanho_corpo, ativo ? gv_t.sobre_acento : gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_tocado(id)) {
					G.pagina_ajustes = pg;
					cache_bytes = -1;
				}
			}
		}
		Clay_ElementId rol = CLAY_ID("cfg-rolagem");
		CLAY(CLAY_ID("cfg-direita"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.padding = { 24, 24, 20, 18 }, .childGap = 16 } })) {
				CLAY(CLAY_ID("cfg-cab"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
					gv_texto(gv_str(PAGINAS[G.pagina_ajustes].titulo), 32, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_str(PAGINAS[G.pagina_ajustes].sub), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
				}
				switch (G.pagina_ajustes) {
				case 0: pagina_biblioteca(); break;
				case 1: cartao(CLAY_ID("aj-c-vis"), "Miniaturas e fotos", corpo_visualizacao); break;
				case 2: cartao(CLAY_ID("aj-c-edi"), "Ao salvar uma cópia", corpo_edicao); break;
				case 3: cartao(CLAY_ID("aj-c-rep"), "Vídeos", corpo_reproducao); break;
				default: cartao(CLAY_ID("aj-c-pri"), "Privacidade", corpo_privacidade); break;
				}
			}
			CLAY(CLAY_ID("cfg-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 24, 24, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
				if (gv_botao_ext(CLAY_ID("cfg-restaurar"), &(gv_botao_opcoes){ .icone = "restore", .texto = "Restaurar padrão", .estilo = GV_BOTAO_ELEVADO })) {
					restaurar_pagina();
				}
				CLAY(CLAY_ID("cfg-folga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
				if (gv_botao_ext(CLAY_ID("cfg-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
					G.tela = T_BIBLIOTECA;
				}
				if (gv_botao_ext(CLAY_ID("cfg-aplicar"), &(gv_botao_opcoes){ .texto = "Aplicar", .estilo = GV_BOTAO_CHEIO })) {
					aplicar_ajustes(&G.edit);
					G.tela = T_BIBLIOTECA;
				}
			}
		}
	}
	if (menu_pasta >= 0) {
		GV_POPUP(CLAY_ID("aj-menu-pasta"), CLAY_IDI("aj-pasta-mais", (uint32_t)menu_pasta), .alinhar = GV_ALINHAR_FIM, .largura = 260) {
			CLAY(CLAY_ID("aj-mp-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				menu_da_pasta();
			}
		}
		if (!gv_popup_aberto(CLAY_ID("aj-menu-pasta"))) {
			menu_pasta = -1;
		}
	}
	if (gv_tecla() == 0xff1b && !add_aberto) { /* Esc */
		G.tela = T_BIBLIOTECA;
	}
}

/* o diálogo de adicionar pasta (por cima de tudo); verdadeiro se está aberto */
bool ajustes_dialogo(void) {
	if (!add_aberto) {
		return false;
	}
	CLAY(CLAY_ID("aj-dlg"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("aj-dlg-cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(520), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(22), .childGap = 14 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Adicionar pasta"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Digite o caminho de uma pasta com fotos e vídeos."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			int r = gv_campo(CLAY_ID("aj-add-campo"), add_caminho, sizeof(add_caminho), "/home/você/Fotos", GV_CAMPO_TEXTO);
			if (add_erro[0]) {
				gv_texto(gv_str(add_erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
			bool confirmar = (r & GV_CAMPO_ENTER) != 0;
			CLAY(CLAY_ID("aj-dlg-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao_ext(CLAY_ID("aj-dlg-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
					add_aberto = false;
				}
				confirmar |= gv_botao_ext(CLAY_ID("aj-dlg-ok"), &(gv_botao_opcoes){ .texto = "Adicionar", .estilo = GV_BOTAO_CHEIO });
			}
			if (confirmar) {
				char *p = g_strdup(add_caminho);
				size_t n = strlen(p);
				while (n > 1 && p[n - 1] == '/') {
					p[--n] = '\0';
				}
				struct stat st;
				bool repetida = false;
				for (int i = 0; i < G.edit.npastas; i++) {
					repetida |= !strcmp(G.edit.pastas[i], p);
				}
				if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) {
					g_snprintf(add_erro, sizeof(add_erro), "Essa pasta não existe.");
				} else if (repetida) {
					g_snprintf(add_erro, sizeof(add_erro), "Essa pasta já está na biblioteca.");
				} else if (G.edit.npastas >= GL_PASTAS_MAX || n >= sizeof(G.edit.pastas[0])) {
					g_snprintf(add_erro, sizeof(add_erro), "Não dá para adicionar mais pastas.");
				} else {
					g_strlcpy(G.edit.pastas[G.edit.npastas++], p, sizeof(G.edit.pastas[0]));
					add_aberto = false;
				}
				g_free(p);
			}
		}
	}
	if (gv_tecla() == 0xff1b) {
		add_aberto = false;
	}
	return true;
}
