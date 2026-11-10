/* Drivers e kernels: o hardware detectado, os drivers de vídeo, o microcódigo do processador e os kernels, cada um com a checagem de compatibilidade
 * (servicos/loja_hw.c). O que a checagem recusa não instala, a não ser com o modo avançado (Configurações). */
#include <stdio.h>
#include <string.h>

#include "app.h"

void drivers_atualizar(void) {
	if (!L.cat || !L.hw) {
		L.nitens_hw = 0;
		return;
	}
	L.nitens_hw = lh_itens(L.hw, L.cat, L.itens_hw, 32);
}

const lh_item *drivers_item(const char *pacote) {
	for (int i = 0; i < L.nitens_hw; i++) {
		if (!strcmp(L.itens_hw[i].pacote, pacote)) {
			return &L.itens_hw[i];
		}
	}
	return NULL;
}

static bool visivel(const lh_item *it) {
	bool aba = L.d_aba == 1 ? it->grupo == LH_KERNEL : it->grupo != LH_KERNEL;
	if (!aba) {
		return false;
	}
	return !L.d_so_compat || it->instalado || (!it->nao_aplica && it->compat != LH_INCOMPATIVEL);
}

/* ---- uma linha ---- */

static bool etiqueta(Clay_ElementId id, const char *texto, Clay_Color cor, bool cheio) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIT(0), CLAY_SIZING_FIXED(36) }, .padding = { 16, 16, 0, 0 }, .childGap = 6, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = cheio ? gv_alfa(cor, 0.16f) : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(18), .border = { .color = cor, .width = GV_BORDA(1) } }) {
		if (cheio) {
			gv_icone("check", 16, cor);
		}
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, cheio ? cor : cor, GV_PESO_NORMAL);
	}
	return gv_tocado(id);
}

static void linha(const lh_item *it, int i) {
	Clay_ElementId id = CLAY_IDI("lj-d-linha", (uint32_t)i);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(84) }, .padding = { 16, 16, 0, 0 }, .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .border = { .color = gv_t.borda, .width = { .bottom = 1 } }, .clip = { .horizontal = true } }) {
		bool abrir = gv_tocado(id);
		gv_icone("diamond", 34, gv_t.acento);
		/* a coluna do nome tem a largura que sobra (o texto não encolhe sozinho: sem isto o botão da direita sai da janela) */
		bool com_fonte = gv_app_largura(L.app) > 1180, com_aval = gv_app_largura(L.app) > 1450;
		float fixas = 140 + 140 + (com_fonte ? 90 : 0) + (com_aval ? 190 : 0);
		int filhos = 4 + (com_fonte ? 1 : 0) + (com_aval ? 1 : 0);
		float nome_w = gv_app_largura(L.app) - 236 - 56 - 32 - 34 - fixas - 16.0f * (float)(filhos - 1) - 8;
		CLAY(CLAY_IDI("lj-d-nome", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(MAX(nome_w, 120)) }, .childGap = 2 }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(it->exibicao), gv_t.tamanho_destaque + 1, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(it->descricao), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (gv_app_largura(L.app) > 1180) {
			CLAY(CLAY_IDI("lj-d-fonte", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(90) }, .childGap = 4 } }) {
				gv_texto(CLAY_STRING("Fonte"), gv_t.tamanho_legenda, gv_t.texto_secundario, GV_PESO_NORMAL);
				selo(CLAY_IDI("lj-d-repo", (uint32_t)i), it->repo[0] ? it->repo : "—", false, false);
			}
		}
		if (gv_app_largura(L.app) > 1450) {
			CLAY(CLAY_IDI("lj-d-aval", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(190) }, .childGap = 4 } }) {
				gv_texto(CLAY_STRING("Avaliações da comunidade LUMNA"), gv_t.tamanho_legenda, gv_t.texto_secundario, GV_PESO_NORMAL);
				gv_texto(CLAY_STRING("Sem avaliações"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		/* o estado: Instalado / Em uso (verde), Compatível, Verifique (amarelo), Incompatível (vermelho) */
		CLAY(CLAY_IDI("lj-d-est", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(140) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
			if (it->em_uso) {
				etiqueta(CLAY_IDI("lj-d-st", (uint32_t)i), "Em uso", gv_t.sucesso, true);
			} else if (it->instalado) {
				etiqueta(CLAY_IDI("lj-d-st", (uint32_t)i), "Instalado", gv_t.sucesso, true);
			} else {
				etiqueta(CLAY_IDI("lj-d-st", (uint32_t)i), lh_nome_compat(it->compat), it->compat == LH_COMPATIVEL ? gv_t.acento : it->compat == LH_VERIFIQUE ? gv_t.aviso : gv_t.perigo, false);
			}
		}
		CLAY(CLAY_IDI("lj-d-ver-cx", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(140) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
			if (gv_botao_ext(CLAY_IDI("lj-d-ver", (uint32_t)i), &(gv_botao_opcoes){ .texto = "Ver detalhes", .estilo = GV_BOTAO_CONTORNO, .altura = 40 })) {
				g_strlcpy(L.d_det, it->pacote, sizeof(L.d_det));
				L.d_det_aberto = true;
				abrir = false;
			}
		}
		if (abrir && !gv_por_cima(CLAY_IDI("lj-d-ver", (uint32_t)i))) {
			g_strlcpy(L.d_det, it->pacote, sizeof(L.d_det));
			L.d_det_aberto = true;
		}
	}
}

static void secao(const char *icone, const char *titulo, enum lh_grupo grupo, int *seq) {
	int n = 0;
	for (int i = 0; i < L.nitens_hw; i++) {
		n += L.itens_hw[i].grupo == grupo && visivel(&L.itens_hw[i]);
	}
	if (!n) {
		return;
	}
	CLAY(CLAY_IDI("lj-d-sec", (uint32_t)grupo), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 }, .clip = { .horizontal = true } }) {
		CLAY(CLAY_IDI("lj-d-sec-t", (uint32_t)grupo), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone(icone, 30, gv_t.acento);
			gv_texto(gv_str(titulo), gv_t.tamanho_titulo - 2, gv_t.texto, GV_PESO_NEGRITO);
		}
		CLAY(CLAY_IDI("lj-d-lista", (uint32_t)grupo), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.superficie,
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) }, .clip = { .horizontal = true } }) {
			for (int i = 0; i < L.nitens_hw; i++) {
				if (L.itens_hw[i].grupo == grupo && visivel(&L.itens_hw[i])) {
					linha(&L.itens_hw[i], (*seq)++);
				}
			}
		}
	}
}

/* ---- a página ---- */

void tela_drivers(void) {
	if (!L.cat || !L.hw) {
		CLAY(CLAY_ID("lj-d-carga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_texto(L.carregando ? CLAY_STRING("Lendo o hardware e os repositórios…") : CLAY_STRING("Não consegui ler o hardware."), gv_t.tamanho_destaque, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		return;
	}
	Clay_ElementId rol = CLAY_ID("lj-rolagem");
	CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
		.padding = { 28, 28, 4, 28 }, .childGap = 16 } })) {
		titulo_pagina("Drivers e kernels", "Pacotes para o hardware deste computador");
		/* o hardware detectado */
		CLAY(CLAY_ID("lj-d-hwcard"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(68) }, .padding = { 18, 18, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_icone("desktop", 30, gv_t.icone);
			gv_texto(CLAY_STRING("Hardware detectado"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			CLAY(CLAY_ID("lj-d-chips"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .clip = { .horizontal = true } }) {
				for (int i = 0; i < L.hw->ngpu; i++) {
					selo(CLAY_IDI("lj-d-chip", (uint32_t)i), L.hw->gpus[i].nome, false, false);
				}
				if (!L.hw->ngpu) {
					selo(CLAY_IDI("lj-d-chip", 0), "Placa de vídeo não identificada", false, false);
				}
				selo(CLAY_IDI("lj-d-chip", 8), L.hw->arch[0] ? L.hw->arch : "?", false, false);
			}
			CLAY(CLAY_ID("lj-d-hw"), { .layout = { .sizing = { CLAY_SIZING_FIT(0) }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .padding = { 8, 8, 8, 8 } } }) {
				gv_texto(CLAY_STRING("Ver hardware"), gv_t.tamanho_corpo, gv_t.acento, GV_PESO_NORMAL);
				gv_icone("external", 18, gv_t.acento);
			}
			if (gv_tocado(CLAY_ID("lj-d-hw"))) {
				L.d_hw_aberto = true;
			}
		}
		/* Drivers | Kernels e "mostrar só compatíveis" */
		CLAY(CLAY_ID("lj-d-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (selo(CLAY_IDI("lj-d-aba", 0), "Drivers", L.d_aba == 0, true)) {
				L.d_aba = 0;
			}
			if (selo(CLAY_IDI("lj-d-aba", 1), "Kernels", L.d_aba == 1, true)) {
				L.d_aba = 1;
			}
			CLAY(CLAY_ID("lj-d-folga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			gv_texto(CLAY_STRING("Mostrar só compatíveis"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			bool v = L.d_so_compat;
			CLAY(CLAY_ID("lj-d-compat-cx"), { .layout = { .sizing = { CLAY_SIZING_FIT(0) } } }) {
				if (gv_interruptor(CLAY_ID("lj-d-compat"), &v)) {
					L.d_so_compat = v;
				}
			}
		}
		int seq = 0;
		if (L.d_aba == 0) {
			secao("gamepad", "Drivers de vídeo", LH_VIDEO, &seq);
			secao("cpu", "Processador", LH_CPU, &seq);
		} else {
			secao("cpu", "Kernels do sistema", LH_KERNEL, &seq);
		}
		if (seq == 0) {
			gv_texto(CLAY_STRING("Nada compatível por aqui. Desligue “Mostrar só compatíveis” para ver tudo e o motivo de cada um."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("lj-d-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone("info", 20, gv_t.texto_secundario);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Drivers: correspondência com o dispositivo. Kernels: arquitetura, espaço em /boot e suporte ao hardware. Em caso de dúvida a Loja diz “Verifique”, nunca “Compatível”."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
}

/* ---- os diálogos ---- */

static void linha_hw(const char *rotulo, const char *valor, int i) {
	CLAY(CLAY_IDI("lj-hw-l", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
		CLAY(CLAY_IDI("lj-hw-r", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(valor), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
	}
}

static void dialogo_hw(void) {
	bool fechar = false;
	CLAY(CLAY_ID("lj-hw"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 90, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("lj-hw-cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(620), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(24), .childGap = 10 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			CLAY(CLAY_ID("lj-hw-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Hardware deste computador"), gv_t.tamanho_destaque + 4, gv_t.texto, GV_PESO_MEDIO);
				}
				fechar = gv_botao_redondo(CLAY_ID("lj-hw-fechar"), "close", 40, false, false);
			}
			int n = 0;
			for (int i = 0; i < L.hw->ngpu; i++) {
				linha_hw(i == 0 ? "Placa de vídeo" : "Outra placa", gv_fmt("%s  (%s, %s)", L.hw->gpus[i].nome, L.hw->gpus[i].vendor, L.hw->gpus[i].pci).chars, n++);
			}
			if (!L.hw->ngpu) {
				linha_hw("Placa de vídeo", "não identificada (lspci)", n++);
			}
			linha_hw("Processador", L.hw->cpu[0] ? L.hw->cpu : "não identificado", n++);
			if (L.hw->ram > 0) {
				linha_hw("Memória", gv_fmt("%.1f GB", (double)L.hw->ram / (1024.0 * 1024 * 1024)).chars, n++);
			}
			linha_hw("Arquitetura", L.hw->arch, n++);
			linha_hw("Kernel em uso", gv_fmt("%s%s%s", L.hw->kernel_uname, L.hw->kernel_pkg[0] ? "  ·  pacote " : "", L.hw->kernel_pkg).chars, n++);
			linha_hw("Boot", gv_fmt("%s  ·  %s", L.hw->uefi ? "UEFI" : "BIOS", L.hw->boot_nome[0] ? L.hw->boot_nome : "gerenciador não reconhecido").chars, n++);
			if (L.hw->boot_livre >= 0) {
				linha_hw("Livre em /boot", tamanho_texto(L.hw->boot_livre), n++);
			}
		}
	}
	if (fechar) {
		L.d_hw_aberto = false;
	}
}

static const char *cor_texto_motivo(enum lh_compat c, Clay_Color *cor) {
	*cor = c == LH_COMPATIVEL ? gv_t.sucesso : c == LH_VERIFIQUE ? gv_t.aviso : gv_t.perigo;
	return lh_nome_compat(c);
}

static void dialogo_item(void) {
	const lh_item *it = drivers_item(L.d_det);
	if (!it) {
		L.d_det_aberto = false;
		return;
	}
	const lj_pacote *p = lj_achar(L.cat, it->pacote);
	char motivo_rem[256];
	bool pode_rem = lh_pode_remover(it, L.hw, L.cat, motivo_rem, sizeof(motivo_rem));
	bool pode_ins = lh_pode_instalar(it, L.cfg.avancado);
	bool fechar = false, instalar = false, remover = false;
	Clay_Color cor;
	const char *rotulo = cor_texto_motivo(it->compat, &cor);
	CLAY(CLAY_ID("lj-dd"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 90, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("lj-dd-cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(640), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(24), .childGap = 12 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			CLAY(CLAY_ID("lj-dd-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("diamond", 40, gv_t.acento);
				CLAY(CLAY_ID("lj-dd-nm"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
					gv_texto(gv_str(it->exibicao), gv_t.tamanho_titulo, gv_t.texto, GV_PESO_NEGRITO);
					gv_texto(gv_str(it->descricao), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				fechar = gv_botao_redondo(CLAY_ID("lj-dd-fechar"), "close", 40, false, false);
			}
			CLAY(CLAY_ID("lj-dd-motivo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 6 },
				.backgroundColor = gv_alfa(cor, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = cor, .width = GV_BORDA(1) } }) {
				gv_texto(gv_fmt("%s para este computador", rotulo), gv_t.tamanho_corpo + 1, cor, GV_PESO_MEDIO);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(it->motivo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
			}
			if (p) {
				gv_texto(gv_fmt("Versão %s  ·  repositório %s  ·  baixa %s, ocupa %s", p->versao, p->repo, tamanho_texto(p->baixar), tamanho_texto(p->tamanho)), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (it->junto[0] && lj_achar(L.cat, it->junto) && !lj_versao_instalada(L.cat, it->junto)) {
				gv_texto(gv_fmt("Instala junto: %s", it->junto), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (it->grupo == LH_KERNEL) {
				gv_texto(CLAY_STRING("A Loja não mexe no gerenciador de boot: depois de instalar, ela confere se o kernel e a entrada do menu apareceram e avisa o que falta."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			if (it->compat == LH_INCOMPATIVEL && !it->instalado) {
				gv_texto(L.cfg.avancado ? CLAY_STRING("Modo avançado ligado: dá para instalar mesmo assim, por sua conta e risco.") : CLAY_STRING("Bloqueado. Para instalar mesmo assim, ligue o modo avançado em Configurações."), gv_t.tamanho_corpo - 1, L.cfg.avancado ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (it->instalado && !pode_rem && motivo_rem[0]) {
				gv_texto(gv_str(motivo_rem), gv_t.tamanho_corpo - 1, gv_t.aviso, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("lj-dd-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 0 }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (it->instalado && pode_rem) {
					remover = gv_botao_ext(CLAY_ID("lj-dd-remover"), &(gv_botao_opcoes){ .texto = "Remover", .estilo = GV_BOTAO_PERIGO, .altura = 42, .desativado = L.agindo });
				}
				if (!it->instalado) {
					instalar = gv_botao_ext(CLAY_ID("lj-dd-instalar"), &(gv_botao_opcoes){ .texto = it->compat == LH_INCOMPATIVEL ? "Instalar mesmo assim" : "Instalar", .estilo = GV_BOTAO_CHEIO, .altura = 42, .desativado = L.agindo || !pode_ins });
				}
			}
		}
	}
	if (fechar) {
		L.d_det_aberto = false;
	} else if (instalar && pode_ins) {
		g_strlcpy(L.d_conf_pacote, it->pacote, sizeof(L.d_conf_pacote));
		L.d_conf_acao = LJ_INSTALAR;
		L.d_conf_aberta = true;
	} else if (remover && pode_rem) {
		g_strlcpy(L.d_conf_pacote, it->pacote, sizeof(L.d_conf_pacote));
		L.d_conf_acao = LJ_REMOVER;
		L.d_conf_aberta = true;
	}
}

static void dialogo_confirmar(void) {
	const lh_item *it = drivers_item(L.d_conf_pacote);
	if (!it) {
		L.d_conf_aberta = false;
		return;
	}
	bool ok = false, cancela = false;
	CLAY(CLAY_ID("lj-dc"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("lj-dc-cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(520), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(22), .childGap = 14 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(L.d_conf_acao == LJ_REMOVER ? gv_fmt("Remover %s?", it->exibicao) : gv_fmt("Instalar %s?", it->exibicao), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				if (L.d_conf_acao == LJ_REMOVER) {
					gv_texto(it->grupo == LH_KERNEL ? CLAY_STRING("O kernel e os headers dele saem do sistema (pacman -Rns). O kernel em uso nunca é removido. Depois confira se o menu do boot ainda lista o que sobrou.")
						: CLAY_STRING("O driver sai do sistema (pacman -Rns). Se for o driver de vídeo em uso, a tela pode voltar sem aceleração até reiniciar."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				} else if (it->compat == LH_INCOMPATIVEL) {
					gv_texto(gv_fmt("A checagem marcou como INCOMPATÍVEL: %s Você ligou o modo avançado: instalar pode deixar o sistema sem vídeo ou sem boot.", it->motivo), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
				} else if (it->compat == LH_VERIFIQUE) {
					gv_texto(gv_fmt("Atenção: %s", it->motivo), gv_t.tamanho_corpo, gv_t.aviso, GV_PESO_NORMAL);
				} else {
					gv_texto(gv_fmt("%s O sistema vai pedir a sua senha.", it->motivo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
			CLAY(CLAY_ID("lj-dc-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				cancela = gv_botao_ext(CLAY_ID("lj-dc-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_SIMPLES, .altura = 42 });
				ok = gv_botao_ext(CLAY_ID("lj-dc-ok"), &(gv_botao_opcoes){ .texto = L.d_conf_acao == LJ_REMOVER ? "Remover" : "Instalar", .estilo = L.d_conf_acao == LJ_REMOVER || it->compat == LH_INCOMPATIVEL ? GV_BOTAO_PERIGO : GV_BOTAO_CHEIO, .altura = 42 });
			}
		}
	}
	if (cancela) {
		L.d_conf_aberta = false;
	} else if (ok) {
		const char *nomes[2] = { it->pacote, it->junto };
		int n = 1;
		if (it->junto[0] && lj_achar(L.cat, it->junto) && (L.d_conf_acao == LJ_REMOVER ? lj_versao_instalada(L.cat, it->junto) != NULL : lj_versao_instalada(L.cat, it->junto) == NULL)) {
			n = 2;
		}
		char exib[64];
		g_strlcpy(exib, it->exibicao, sizeof(exib));
		iniciar_acao_nomes(L.d_conf_acao, nomes, n, exib, it->grupo == LH_KERNEL);
	}
}

void drivers_dialogos(void) {
	if (L.d_det_aberto && !L.d_conf_aberta && L.tela == T_DRIVERS) {
		dialogo_item();
	}
	if (L.d_hw_aberto && L.hw) {
		dialogo_hw();
	}
	if (L.d_conf_aberta) {
		dialogo_confirmar();
	}
	if (gv_tecla() == 0xff1b) {
		if (L.d_conf_aberta) {
			L.d_conf_aberta = false;
		} else if (L.d_hw_aberto) {
			L.d_hw_aberto = false;
		} else if (L.d_det_aberto) {
			L.d_det_aberto = false;
		}
	}
}
