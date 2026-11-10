#include <stdio.h>
#include <string.h>

#include "app.h"

static bool item_lateral(Clay_ElementId id, const char *icone, const char *texto, bool ativo, int selo) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 14, 12, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativo ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
		.border = { .color = gv_t.acento, .width = { .left = ativo ? 3 : 0 } } }) {
		gv_icone(icone, 24, ativo ? gv_t.acento : gv_t.texto_secundario);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 0, 0 } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo, ativo ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (selo > 0) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIT(22), CLAY_SIZING_FIXED(22) }, .padding = { 7, 7, 0, 0 }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(11) }) {
				gv_texto(gv_fmt("%d", selo), gv_t.tamanho_legenda, gv_t.sobre_acento, GV_PESO_MEDIO);
			}
		}
	}
	return gv_tocado(id);
}

void barra_lateral(void) {
	CLAY(CLAY_ID("lj-lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(236), CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 16, 12 }, .childGap = 2 },
		.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
		static const char *const ICONES[] = { "home", "package", "download", "refresh" };
		static const char *const NOMES[] = { "Explorar", "Pacotes", "Instalados", "Atualizações" };
		for (int t = 0; t < 4; t++) {
			if (item_lateral(CLAY_IDI("lj-nav", (uint32_t)t), ICONES[t], NOMES[t], L.tela == t, t == T_ATUALIZACOES ? L.natualiz : 0)) {
				ir_para(t);
			}
		}
		CLAY(CLAY_ID("lj-lat-div"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) }, .padding = { 0, 0, 0, 0 } }, .backgroundColor = gv_t.borda }) {}
		CLAY(CLAY_ID("lj-lat-esp1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(8) } } }) {}
		if (item_lateral(CLAY_IDI("lj-nav", (uint32_t)T_DRIVERS), "cpu", "Drivers e kernels", L.tela == T_DRIVERS, 0)) {
			ir_para(T_DRIVERS);
		}
		CLAY(CLAY_ID("lj-lat-folga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}
		if (item_lateral(CLAY_IDI("lj-nav", (uint32_t)T_CONFIG), "settings", "Configurações", L.tela == T_CONFIG, 0)) {
			ir_para(T_CONFIG);
		}
	}
}

void barra_do_alto(void) {
	CLAY(CLAY_ID("lj-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(72) }, .padding = { 20, 20, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_ID("lj-busca-cx"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			if (gv_campo(CLAY_ID("lj-busca"), L.busca, sizeof(L.busca), L.tela == T_PACOTES ? "Buscar pacotes na Loja LUMNA" : "Buscar por nome ou pacote", GV_CAMPO_BUSCA) & GV_CAMPO_MUDOU) {
				if (L.busca[0] && L.tela != T_PACOTES && L.tela != T_INSTALADOS && L.tela != T_ATUALIZACOES) {
					L.tela = T_PACOTES;   /* digitar em qualquer tela leva à lista de pacotes */
				}
				L.mostrar = 100;
				L.aur_em = gv_agora() + 0.5;
				marcar_sujo();
			}
		}
		if (gv_botao_redondo(CLAY_ID("lj-recarregar"), "refresh", 42, L.carregando, false)) {
			recarregar_catalogo();
		}
	}
	/* Ctrl+K foca a busca */
	for (int i = 0;; i++) {
		uint32_t t = gv_tecla_n(i);
		if (!t) {
			break;
		}
		if ((t == 'k' || t == 'K') && (gv_modificadores() & GV_MOD_CTRL)) {
			gv_focar(CLAY_ID("lj-busca"));
		}
	}
}

/* ---------------- o andamento ---------------- */

void painel_andamento(void) {
	if (!L.ag_painel) {
		return;
	}
	bool fim, ok;
	char erro[512];
	GPtrArray *ult = g_ptr_array_new_with_free_func(g_free);
	g_mutex_lock(&L.ag_mutex);
	fim = L.ag_fim;
	ok = L.ag_ok;
	g_strlcpy(erro, L.ag_erro, sizeof(erro));
	for (guint i = L.ag_linhas->len > 7 ? L.ag_linhas->len - 7 : 0; i < L.ag_linhas->len; i++) {
		g_ptr_array_add(ult, g_strdup(L.ag_linhas->pdata[i]));
	}
	g_mutex_unlock(&L.ag_mutex);
	const char *verbo = L.ag_acao == LJ_INSTALAR ? "Instalando" : L.ag_acao == LJ_REMOVER ? "Removendo" : "Atualizando";
	const char *feito = L.ag_acao == LJ_INSTALAR ? "instalado" : L.ag_acao == LJ_REMOVER ? "removido" : "atualizado";
	const char *alvo = L.ag_acao == LJ_ATUALIZAR_TUDO ? "o sistema" : L.ag_exibicao;
	bool fechar = false;
	CLAY(CLAY_ID("lj-ag-painel"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(440), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 8 },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_BOTTOM, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM }, .offset = { -20, -20 }, .zIndex = 80,
			.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = fim && !ok ? gv_t.perigo : gv_t.borda_controle, .width = GV_BORDA(1) },
		.userData = gv_sombra(24, 8, 0.45f) }) {
		CLAY(CLAY_ID("lj-ag-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone(!fim ? "refresh" : ok ? "check" : "info", 22, !fim ? gv_t.acento : ok ? gv_t.sucesso : gv_t.perigo);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 0, 0 } }, .clip = { .horizontal = true } }) {
				gv_texto(!fim ? gv_fmt("%s %s…", verbo, alvo) : ok ? gv_fmt("%s: %s", alvo, feito) : gv_fmt("Não deu para concluir (%s)", alvo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			}
			if (fim && gv_botao_redondo(CLAY_ID("lj-ag-fechar"), "close", 32, false, false)) {
				fechar = true;
			}
		}
		if (!fim) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("A janela da senha pode aparecer. A Loja espera até terminar."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_ID("lj-ag-log"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(8) },
			.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
			for (guint i = 0; i < ult->len; i++) {
				CLAY(CLAY_IDI("lj-ag-l", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(18) } }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(ult->pdata[i]), gv_t.tamanho_legenda, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
			if (!ult->len) {
				gv_texto(CLAY_STRING("Esperando o pacman…"), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		if (fim && !ok && erro[0]) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(erro), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
		}
	}
	g_ptr_array_free(ult, TRUE);
	if (fechar) {
		L.ag_painel = false;
	}
}

/* ---------------- os diálogos ---------------- */

static void cartao_dialogo(Clay_ElementId id, float largura, const char *titulo, void (*corpo)(void *), void *dados) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_IDI("lj-dlg-cartao", id.id), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(22), .childGap = 14 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			corpo(dados);
		}
	}
}

static void corpo_confirmar(void *d) {
	const lj_pacote *p = L.cat ? lj_achar(L.cat, L.conf_nome) : NULL;
	bool aur = p && !strcmp(p->repo, "aur");
	const char *nome = p ? p->exibicao : L.conf_nome;
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
		if (L.conf_acao == LJ_REMOVER) {
			gv_texto(gv_fmt("Remover %s e o que só ele usava (pacman -Rns). Os arquivos pessoais ficam.", nome), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		} else if (aur) {
			gv_texto(gv_fmt("%s vem do AUR: não é um pacote oficial do Arch. O código é compilado nesta máquina, sem revisão. Instale só se confiar em quem o mantém.", nome), gv_t.tamanho_corpo, gv_t.aviso, GV_PESO_NORMAL);
		} else {
			gv_texto(gv_fmt("Instalar %s (%s%s). O sistema vai pedir a sua senha.", nome, p ? p->repo : "", p && p->baixar ? gv_fmt(", baixa %.1f MB", (double)p->baixar / 1048576.0).chars : ""), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	if (aur && !L.helper && L.conf_acao == LJ_INSTALAR) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(CLAY_STRING("Nem o paru nem o yay estão instalados: sem um deles não dá para instalar do AUR."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		}
	}
	bool ok = false, cancela = false;
	CLAY(CLAY_ID("lj-conf-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
		cancela = gv_botao_ext(CLAY_ID("lj-conf-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_SIMPLES, .altura = 42 });
		ok = gv_botao_ext(CLAY_ID("lj-conf-ok"), &(gv_botao_opcoes){ .texto = L.conf_acao == LJ_REMOVER ? "Remover" : "Instalar", .estilo = L.conf_acao == LJ_REMOVER ? GV_BOTAO_PERIGO : GV_BOTAO_CHEIO, .altura = 42 });
	}
	if (cancela) {
		L.conf_aberta = false;
	} else if (ok) {
		iniciar_acao(L.conf_acao, L.conf_nome);
	}
}

void detalhe_dialogo(void);

void dialogos(void) {
	if (L.det_aberto && !L.conf_aberta) {
		detalhe_dialogo();
	}
	if (L.conf_aberta) {
		cartao_dialogo(CLAY_ID("lj-dlg-conf"), 480, L.conf_acao == LJ_REMOVER ? "Remover o pacote?" : "Instalar o pacote?", corpo_confirmar, NULL);
		if (gv_tecla() == 0xff1b) {
			L.conf_aberta = false;
		}
	} else if (L.det_aberto && gv_tecla() == 0xff1b) {
		L.det_aberto = false;
	}
}
