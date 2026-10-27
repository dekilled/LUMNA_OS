/*
 * Sistema → Sobre: o computador (modelo, processador, memória...), as atualizações do sistema (o que o
 * checkupdates acha, e o histórico do pacman) e os avisos legais. Os dados vêm de servicos/sistema.c.
 */
#include "painel.h"

#include <stdio.h>
#include <string.h>

#include "sistema.h"

/* ---- Este computador ---- */

static struct sistema_info info;
static bool info_lida;

void pn_sobre_pc_abrir(void) {
	sistema_ler(&info);
	info_lida = true;
}

static void linha_info(uint32_t k, const char *nome, const char *valor) {
	pn_linha_comecar(CLAY_IDI("sb-info", k), NULL, nome, NULL);
	CLAY(CLAY_IDI("sb-info-v", k), { .layout = { .sizing = { CLAY_SIZING_FIT(0, 520) } } }) {
		gv_texto(gv_str(valor), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	pn_linha_acabar();
}

static const char *duracao(int64_t s) {
	if (s < 3600) {
		return gv_fmt("%d min", (int)(s / 60)).chars;
	}
	if (s < 86400) {
		return gv_fmt("%d h %d min", (int)(s / 3600), (int)(s % 3600 / 60)).chars;
	}
	return gv_fmt("%d dias e %d h", (int)(s / 86400), (int)(s % 86400 / 3600)).chars;
}

void pn_sobre_pc_desenhar(void) {
	if (!info_lida) {
		pn_sobre_pc_abrir();
	}
	CLAY(CLAY_ID("sb-capa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 18 }, .childGap = 18, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_ID("sb-logo"), { .layout = { .sizing = { CLAY_SIZING_FIXED(72), CLAY_SIZING_FIXED(72) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.acento, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande) }) {
			gv_icone("lumna", 40, gv_t.acento);
		}
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4 } }) {
			gv_texto(gv_fmt("LUMNA OS %s", info.lumna), 26, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(info.host), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	uint32_t k = 0;
	linha_info(k++, "Nome do computador", info.host);
	if (info.modelo[0]) {
		linha_info(k++, "Modelo", info.modelo);
	}
	linha_info(k++, "Processador", info.cpu[0] ? gv_fmt("%s · %d núcleos", info.cpu, info.nucleos).chars : "—");
	linha_info(k++, "Memória", gv_fmt("%s (%s livres agora)", pn_tamanho(info.memoria_total), pn_tamanho(info.memoria_livre)).chars);
	if (info.gpu[0]) {
		linha_info(k++, "Placa de vídeo", info.gpu);
	}
	linha_info(k++, "Disco do sistema", gv_fmt("%s (%s livres)", pn_tamanho(info.disco_total), pn_tamanho(info.disco_livre)).chars);
	linha_info(k++, "Sistema base", gv_fmt("%s · %s", info.distro, info.arquitetura).chars);
	linha_info(k++, "Kernel", info.kernel);
	linha_info(k++, "Versão do LUMNA", info.lumna);
	linha_info(k++, "Ligado há", duracao(info.ligado_ha_s));
	CLAY(CLAY_ID("sb-copiar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 18, 0 }, .childGap = 12 } }) {
		static double copiado_em;
		if (gv_botao(CLAY_ID("sb-copiar-bt"), "clipboard", "Copiar informações", GV_BOTAO_ELEVADO)) {
			char resumo[1500];
			sistema_resumo(&info, resumo, sizeof(resumo));
			gv_copiar(resumo);
			copiado_em = gv_agora();
		}
		if (gv_agora() - copiado_em < 2.5 && copiado_em > 0) {
			gv_texto(CLAY_STRING("Copiado: é só colar onde precisar."), gv_t.tamanho_corpo - 1, gv_t.sucesso, GV_PESO_NORMAL);
			gv_acordar_em(2.6 - (gv_agora() - copiado_em));
		}
	}
}

/* ---- Atualizações do sistema ---- */

static struct sistema_atualizacao atu[SISTEMA_MAX_ATUALIZACOES];
static int natu = -2;       /* -2: ainda não procurou; -1: não deu para saber (sem checkupdates nem pacman) */
static bool procurando;
static struct sistema_evento hist[24];
static int nhist = -1;
static int natu_tarefa;

static void procurar_trabalho(void *dados) {
	natu_tarefa = sistema_atualizacoes(atu, SISTEMA_MAX_ATUALIZACOES);
}

static void procurar_pronto(void *dados) {
	natu = natu_tarefa;
	procurando = false;
}

static void procurar(void) {
	if (!procurando) {
		procurando = true;
		pn_tarefa(procurar_trabalho, procurar_pronto, NULL);
	}
}

void pn_sobre_atu_abrir(void) {
	nhist = sistema_historico(hist, 24);
}

void pn_sobre_atu_desenhar(void) {
	if (nhist < 0) {
		pn_sobre_atu_abrir();
	}
	CLAY(CLAY_ID("at-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 14 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			if (procurando) {
				gv_texto(CLAY_STRING("Procurando atualizações…"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(CLAY_STRING("Isso pode levar alguns segundos."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			} else if (natu == -2) {
				gv_texto(CLAY_STRING("Ainda não procurei"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(CLAY_STRING("Toque em Procurar para ver o que há de novo no sistema."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			} else if (natu < 0) {
				gv_texto(CLAY_STRING("Não consegui procurar"), gv_t.tamanho_destaque, gv_t.aviso, GV_PESO_MEDIO);
				gv_texto(CLAY_STRING("Faltou o checkupdates (pacote pacman-contrib) ou o pacman não respondeu."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			} else if (natu == 0) {
				gv_texto(CLAY_STRING("Tudo em dia"), gv_t.tamanho_destaque, gv_t.sucesso, GV_PESO_MEDIO);
			} else {
				gv_texto(gv_fmt("%d atualizações disponíveis", natu), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			}
		}
		if (gv_botao_ext(CLAY_ID("at-procurar"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Procurar", .estilo = GV_BOTAO_ELEVADO, .desativado = procurando })) {
			procurar();
		}
		if (natu > 0 && gv_botao(CLAY_ID("at-instalar"), "download", "Atualizar tudo", GV_BOTAO_CHEIO)) {
			/* num terminal: a senha e o que o pacman pergunta aparecem ali */
			pn_terminal("if command -v yay >/dev/null 2>&1; then yay -Syu; else sudo pacman -Syu; fi");
		}
	}
	if (natu > 0) {
		for (int i = 0; i < natu && i < 40; i++) {
			pn_linha_comecar(CLAY_IDI("at-linha", (uint32_t)i), NULL, atu[i].nome, NULL);
			gv_texto(gv_fmt("%s  →  %s", atu[i].antiga, atu[i].nova), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			pn_linha_acabar();
		}
		if (natu > 40) {
			gv_texto(gv_fmt("e mais %d", natu - 40), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	pn_subtitulo(CLAY_ID("at-hist"), "Histórico");
	if (nhist <= 0) {
		gv_texto(CLAY_STRING("Nada registrado ainda (o histórico vem do pacman.log)."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	for (int i = 0; i < nhist; i++) {
		const struct sistema_evento *e = &hist[i];
		pn_linha_comecar(CLAY_IDI("at-h", (uint32_t)i), NULL, gv_fmt("%s %s", e->nome, e->acao).chars, pn_dia(e->quando));
		gv_texto(gv_str(e->de[0] ? gv_fmt("%s  →  %s", e->de, e->para).chars : e->para), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		pn_linha_acabar();
	}
}

/* ---- Avisos legais ---- */

void pn_sobre_legal_desenhar(void) {
	static const struct { const char *nome, *licenca; } itens[] = {
		{ "LUMNA OS (Orbit, Gravity e o shell)", "Os termos do projeto LUMNA" },
		{ "wlroots e Wayland", "MIT" },
		{ "Clay (o layout da interface)", "zlib" },
		{ "GLib, Pango, Cairo, GdkPixbuf, libsoup", "LGPL 2.1 ou mais nova" },
		{ "xkbcommon", "MIT" },
		{ "PulseAudio / PipeWire (cliente)", "LGPL 2.1 ou mais nova" },
		{ "NetworkManager e BlueZ (usados por D-Bus)", "GPL 2 ou mais nova" },
		{ "Mozilla Firefox (a Mídia)", "MPL 2.0" },
		{ "Linux", "GPL 2" },
	};
	gv_texto(CLAY_STRING("O LUMNA é feito de software livre de muita gente. Estes são os principais, com a licença de cada um. "
		"O texto completo de cada licença vem com o pacote do programa, em /usr/share/licenses."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	for (size_t i = 0; i < sizeof(itens) / sizeof(itens[0]); i++) {
		pn_linha_comecar(CLAY_IDI("lg", (uint32_t)i), NULL, itens[i].nome, NULL);
		gv_texto(gv_str(itens[i].licenca), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		pn_linha_acabar();
	}
	CLAY(CLAY_ID("lg-pasta"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 16, 0 } } }) {
		if (gv_botao(CLAY_ID("lg-abrir"), "folder", "Abrir a pasta das licenças", GV_BOTAO_ELEVADO)) {
			pn_executar("xdg-open /usr/share/licenses");
		}
	}
}
