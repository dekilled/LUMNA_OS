/*
 * Aparência → Tela de bloqueio: o layout (Centralizado ou Lateral, o mesmo na tela de login), o papel
 * (o do desktop, um da Coleção ou uma imagem sua), o que aparece (o que está tocando, a Emma),
 * quando tranca sozinha (o lumna-ocioso) e o PIN. Vai para ~/.config/lumna/bloqueio.ini no
 * Aplicar; o layout também para /etc/lumna/entrada.ini (pede a senha de administrador, pkexec).
 * O PIN grava-se na hora, com a senha (servicos/entrar.h).
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "entrar.h"
#include "papelini.h"

struct bloqueio_ajustes {
	int lateral;
	bool mesmo_papel;
	char papel[512];
	bool player, emma;
	int apos; /* minutos sem mexer até trancar (0: nunca) */
	bool suspender;
};
static const struct bloqueio_ajustes fabrica = { .mesmo_papel = true, .player = true, .emma = true, .apos = 5, .suspender = true };
static const char *const apos_nomes[] = { "Nunca", "1 minuto", "2 minutos", "5 minutos", "10 minutos", "15 minutos", "30 minutos", "1 hora" };
static const int apos_minutos[] = { 0, 1, 2, 5, 10, 15, 30, 60 };
#define NAPOS 8
/* o layout da tela de login (/etc/lumna/entrada.ini): -1 se não deu para ler */
static int login_lateral = -1;
static char login_msg[160];
static struct bloqueio_ajustes b, gravado;
static char colecao[PAPEL_COLECAO_MAX][512];
static int ncolecao;

/* o formulário do PIN */
static bool pin_aberto;
static char pin_senha[256], pin_novo[16], pin_msg[128];
static bool pin_msg_erro;

void pn_bloqueio_abrir(void) {
	gravado = fabrica;
	char caminho[1024];
	pn_caminho(caminho, sizeof(caminho), "bloqueio.ini");
	char *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			char *chave = g_strstrip(*l), *valor = g_strstrip(igual + 1);
			if (!strcmp(chave, "layout")) {
				gravado.lateral = !strcmp(valor, "lateral");
			} else if (!strcmp(chave, "papel")) {
				snprintf(gravado.papel, sizeof(gravado.papel), "%s", valor);
				gravado.mesmo_papel = !valor[0];
			} else if (!strcmp(chave, "player")) {
				gravado.player = strcmp(valor, "nao") != 0;
			} else if (!strcmp(chave, "emma")) {
				gravado.emma = strcmp(valor, "nao") != 0;
			} else if (!strcmp(chave, "bloquear_apos")) {
				gravado.apos = atoi(valor);
			} else if (!strcmp(chave, "bloquear_suspender")) {
				gravado.suspender = strcmp(valor, "nao") != 0;
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	b = gravado;
	ncolecao = papel_colecao(colecao, PAPEL_COLECAO_MAX);
	pn_suas_carregar();
	login_lateral = -1;
	login_msg[0] = '\0';
	char *login = NULL;
	if (g_file_get_contents("/etc/lumna/entrada.ini", &login, NULL, NULL)) {
		login_lateral = strstr(login, "lateral") != NULL;
		g_free(login);
	}
	pin_aberto = false;
	pin_msg[0] = '\0';
}

bool pn_bloqueio_pendente(void) {
	return b.lateral != gravado.lateral || b.mesmo_papel != gravado.mesmo_papel || b.player != gravado.player ||
		b.emma != gravado.emma || b.apos != gravado.apos || b.suspender != gravado.suspender || (!b.mesmo_papel && strcmp(b.papel, gravado.papel));
}

void pn_bloqueio_aplicar(void) {
	char texto[1400];
	snprintf(texto, sizeof(texto),
		"# A tela de bloqueio (o Painel de controle grava este arquivo)\nlayout = %s\npapel = %s\nplayer = %s\nemma = %s\n"
		"bloquear_apos = %d\nbloquear_suspender = %s\n",
		b.lateral ? "lateral" : "centro", b.mesmo_papel ? "" : b.papel, b.player ? "sim" : "nao", b.emma ? "sim" : "nao", b.apos,
		b.suspender ? "sim" : "nao");
	if (pn_gravar("bloqueio.ini", texto)) {
		gravado = b;
	}
	/* a tela de login tem o mesmo layout (só se o login do LUMNA estiver instalado) */
	if (login_lateral >= 0 && login_lateral != b.lateral) {
		char cmd[256];
		snprintf(cmd, sizeof(cmd), "printf 'layout = %s\\n' | pkexec tee /etc/lumna/entrada.ini >/dev/null",
			b.lateral ? "lateral" : "centro");
		if (system(cmd) == 0) {
			login_lateral = b.lateral;
			snprintf(login_msg, sizeof(login_msg), "A tela de login também ficou %s.", b.lateral ? "Lateral" : "Centralizada");
		} else {
			snprintf(login_msg, sizeof(login_msg), "A tela de login continua como estava (sem a senha de administrador).");
		}
	}
}

void pn_bloqueio_descartar(void) {
	b = gravado;
}

void pn_bloqueio_restaurar(void) {
	b = fabrica;
}

/* a telinha de cada layout: o relógio e o player nos lugares deles */
static bool cartao_layout(Clay_ElementId id, const char *nome, bool lateral, bool escolhido) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14),
			.childGap = 12, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
		.backgroundColor = escolhido ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = escolhido ? gv_t.acento : gv_t.borda, .width = GV_BORDA(escolhido ? 2 : 1) } }) {
		tocado |= gv_focavel(id, gv_t.raio_grande);
		Clay_Color claro = gv_cor(0xf4f1faff), suave = gv_cor(0xffffff40);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(112) },
				.padding = CLAY_PADDING_ALL(12), .childGap = 6, .childAlignment = { .x = lateral ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_CENTER } },
			.backgroundColor = gv_cor(0x241a44ff), .cornerRadius = CLAY_CORNER_RADIUS(8) }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(lateral ? 1 : 0) } } }) {}
			if (lateral) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
					CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4 } }) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(64), CLAY_SIZING_FIXED(20) } }, .backgroundColor = claro,
							.cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(52), CLAY_SIZING_FIXED(5) } }, .backgroundColor = suave,
							.cornerRadius = CLAY_CORNER_RADIUS(2) }) {}
					}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(70), CLAY_SIZING_FIXED(24) } }, .backgroundColor = suave,
						.cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
				}
			} else {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(70), CLAY_SIZING_FIXED(22) } }, .backgroundColor = claro,
					.cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(56), CLAY_SIZING_FIXED(5) } }, .backgroundColor = suave,
					.cornerRadius = CLAY_CORNER_RADIUS(2) }) {}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(110), CLAY_SIZING_FIXED(22) } }, .backgroundColor = suave,
					.cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
			}
		}
		gv_texto(gv_str(nome), gv_t.tamanho_corpo, gv_t.texto, escolhido ? GV_PESO_MEDIO : GV_PESO_NORMAL);
	}
	return tocado;
}

static void secao_pin(void) {
	int tamanho = entrar_pin_tamanho();
	pn_linha_comecar(CLAY_ID("bl-pin"), "lock", "PIN",
		tamanho ? gv_fmt("Um PIN de %d números; a senha do Linux também desbloqueia", tamanho).chars
			: "Sem PIN: desbloqueia com a senha do Linux");
	if (!pin_aberto && gv_botao(CLAY_ID("bl-pin-abrir"), NULL, tamanho ? "Trocar ou tirar" : "Criar um PIN", GV_BOTAO_ELEVADO)) {
		pin_aberto = true;
		pin_msg[0] = pin_senha[0] = pin_novo[0] = '\0';
		gv_focar(CLAY_ID("bl-pin-senha"));
	}
	pn_linha_acabar();
	if (pin_aberto) {
		CLAY(CLAY_ID("bl-pin-form"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
				.padding = CLAY_PADDING_ALL(16), .childGap = 10 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Para mexer no PIN, confirme a sua senha do Linux."), gv_t.tamanho_corpo, gv_t.texto_secundario,
				GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .childGap = 12 } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(260) } } }) {
					gv_campo(CLAY_ID("bl-pin-senha"), pin_senha, sizeof(pin_senha), "Senha", GV_CAMPO_SENHA);
				}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(200) } } }) {
					gv_campo(CLAY_ID("bl-pin-novo"), pin_novo, sizeof(pin_novo), "Novo PIN (4 a 8 números)", GV_CAMPO_NUMERO);
				}
			}
			if (pin_msg[0]) {
				gv_texto(gv_str(pin_msg), gv_t.tamanho_corpo, pin_msg_erro ? gv_t.perigo : gv_t.sucesso, GV_PESO_MEDIO);
			}
			CLAY_AUTO_ID({ .layout = { .childGap = 10 } }) {
				if (gv_botao(CLAY_ID("bl-pin-gravar"), NULL, "Gravar o PIN", GV_BOTAO_CHEIO)) {
					const char *e = entrar_pin_definir(pin_senha, pin_novo);
					pin_msg_erro = e != NULL;
					snprintf(pin_msg, sizeof(pin_msg), "%s", e ? e : "PIN gravado");
					memset(pin_senha, 0, sizeof(pin_senha));
					memset(pin_novo, 0, sizeof(pin_novo));
					if (!e) {
						pin_aberto = false;
					}
				}
				if (tamanho && gv_botao(CLAY_ID("bl-pin-tirar"), NULL, "Tirar o PIN", GV_BOTAO_PERIGO)) {
					const char *e = entrar_pin_tirar(pin_senha);
					pin_msg_erro = e != NULL;
					snprintf(pin_msg, sizeof(pin_msg), "%s", e ? e : "PIN tirado");
					memset(pin_senha, 0, sizeof(pin_senha));
					if (!e) {
						pin_aberto = false;
					}
				}
				if (gv_botao(CLAY_ID("bl-pin-cancelar"), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
					pin_aberto = false;
					memset(pin_senha, 0, sizeof(pin_senha));
					memset(pin_novo, 0, sizeof(pin_novo));
				}
			}
		}
	} else if (pin_msg[0]) {
		gv_texto(gv_str(pin_msg), gv_t.tamanho_corpo, pin_msg_erro ? gv_t.perigo : gv_t.sucesso, GV_PESO_MEDIO);
	}
}

void pn_bloqueio_desenhar(void) {
	pn_subtitulo(CLAY_ID("bl-s1"), "Layout");
	CLAY(CLAY_ID("bl-layouts"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .padding = { 0, 0, 0, 16 } } }) {
		if (cartao_layout(CLAY_ID("bl-centro"), "Centralizado", false, !b.lateral)) {
			b.lateral = 0;
		}
		if (cartao_layout(CLAY_ID("bl-lateral"), "Lateral", true, b.lateral)) {
			b.lateral = 1;
		}
	}
	if (login_msg[0]) {
		gv_texto(gv_str(login_msg), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	} else if (login_lateral >= 0) {
		gv_texto(CLAY_STRING("A tela de login usa o mesmo layout (o Aplicar pede a senha de administrador)."), gv_t.tamanho_corpo - 1,
			gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	pn_linha_comecar(CLAY_ID("bl-papel"), "image", "O mesmo papel de parede do desktop", "Desligado: um da Coleção LUMNA ou uma imagem sua");
	gv_interruptor(CLAY_ID("bl-papel-v"), &b.mesmo_papel);
	pn_linha_acabar();
	if (!b.mesmo_papel) {
		CLAY(CLAY_ID("bl-colecao"), { .layout = { .childGap = 12, .padding = { 0, 0, 4, 12 } } }) {
			for (int i = 0; i < ncolecao && i < 6; i++) {
				Clay_ElementId id = CLAY_IDI("bl-papel", (uint32_t)i);
				bool escolhida = !strcmp(b.papel, colecao[i]);
				bool tocado = gv_tocado(id);
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(140), CLAY_SIZING_FIXED(80) }, .padding = CLAY_PADDING_ALL(2) },
					.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio + 2),
					.border = { .color = escolhida ? gv_t.acento : gv_t.borda, .width = GV_BORDA(escolhida ? 2 : 1) } }) {
					tocado |= gv_focavel(id, gv_t.raio_medio + 2);
					Clay_ElementId img = CLAY_IDI("bl-papel-img", (uint32_t)i);
					CLAY(img, gv_imagem(img, colecao[i], GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
						.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
						.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) })) {}
				}
				if (tocado) {
					snprintf(b.papel, sizeof(b.papel), "%.511s", colecao[i]);
				}
			}
		}
		/* uma imagem sua: a escolhida aparece aqui, e o botão abre todas */
		bool da_colecao = false;
		for (int i = 0; i < ncolecao; i++) {
			da_colecao |= !strcmp(b.papel, colecao[i]);
		}
		CLAY(CLAY_ID("bl-sua"), { .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .padding = { 0, 0, 0, 12 } } }) {
			if (b.papel[0] && !da_colecao) {
				pn_miniatura(CLAY_ID("bl-sua-img"), b.papel, 140, 80, true, NULL);
			}
			if (gv_botao(CLAY_ID("bl-arquivo"), "folder", "Escolher arquivo", GV_BOTAO_ELEVADO)) {
				gv_popup_alternar(CLAY_ID("bl-suas"));
			}
		}
		const char *sua = pn_suas_popup(CLAY_ID("bl-suas"), CLAY_ID("bl-arquivo"), b.papel);
		if (sua) {
			snprintf(b.papel, sizeof(b.papel), "%.511s", sua);
		}
	}
	pn_linha_comecar(CLAY_ID("bl-player"), "music", "Mostrar o que está tocando", "Com os botões de tocar, pausar e passar");
	gv_interruptor(CLAY_ID("bl-player-v"), &b.player);
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("bl-emma"), "emma", "Fale com a Emma",
		"Bloqueada, ela só diz a hora e a data, cuida da música e da bateria");
	gv_interruptor(CLAY_ID("bl-emma-v"), &b.emma);
	pn_linha_acabar();
	pn_subtitulo(CLAY_ID("bl-s3"), "Trancar sozinha");
	{
		int escolha = 3;
		for (int i = 0; i < NAPOS; i++) {
			if (apos_minutos[i] == b.apos) {
				escolha = i;
			}
		}
		if (pn_ajuste_lista(CLAY_ID("bl-apos"), "clock", "Depois de ficar parado", "Todas as telas trancam juntas; um vídeo tocando segura",
			    apos_nomes, NAPOS, &escolha)) {
			b.apos = apos_minutos[escolha];
		}
	}
	pn_linha_comecar(CLAY_ID("bl-susp"), "moon", "Trancar antes de suspender", "Ao acordar, o computador já está trancado");
	gv_interruptor(CLAY_ID("bl-susp-v"), &b.suspender);
	pn_linha_acabar();
	pn_subtitulo(CLAY_ID("bl-s2"), "Desbloquear");
	secao_pin();
	CLAY(CLAY_ID("bl-agora"), { .layout = { .padding = { 0, 0, 14, 0 } } }) {
		if (gv_botao(CLAY_ID("bl-bloquear"), "lock", "Bloquear agora", GV_BOTAO_ELEVADO)) {
			pn_executar("lumna-bloqueio");
		}
	}
}
