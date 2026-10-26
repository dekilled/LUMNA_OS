/*
 * A tela de bloqueio do LUMNA (lumna-bloqueio), como nos esboços: o papel de parede, a Emma em
 * cima, o relógio e a data, o que está tocando e o cadeado. Dois layouts: Centralizado (o relógio
 * ao meio, o player embaixo) e Lateral (o relógio no canto, o player à direita), à escolha no
 * Painel (Aparência → Tela de bloqueio), em ~/.config/lumna/bloqueio.ini.
 *
 * Tranca a sessão de verdade (ext-session-lock): se esta app cair, a sessão fica trancada. Para
 * entrar: tocar no cadeado, em qualquer lugar ou começar a digitar; o PIN (se houver) ou a senha
 * do Linux (servicos/entrar.h). O PIN se digita no teclado; o teclado de números na tela só
 * aparece com o modo desenvolvedor → toque (um desktop com tela de toque). A Emma daqui só faz o simples (a hora, a data, a música, a
 * bateria); o resto pede para desbloquear.
 *   ./bloqueio
 */
#define _GNU_SOURCE /* strcasestr */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "desenvolvedor.h"
#include "entrar.h"
#include "gravity.h"
#include "mpris.h"
#include "papelini.h"

enum tela { RELOGIO, ENTRAR, EMMA };

static gv_app *app_bloqueio;
static enum tela tela = RELOGIO;
static bool lateral;           /* o layout */
static bool toque;             /* o teclado de números na tela (modo desenvolvedor) */
static bool mostrar_player = true, mostrar_emma = true;
static char papel[1024];
static struct mpris_estado musica;
static bool usar_senha;        /* sem PIN, ou a pessoa escolheu a senha */
static char pin[16], senha[256];
static char erro[128];
static double mexeu;           /* a última vez que se mexeu (volta ao relógio depois de parado) */
static char pergunta[256], resposta[1024];
static bool resposta_desbloquear;

/* ---- o que se lê ao começar ---- */

static void ler_ajustes(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	char *caminho = config && *config ? g_build_filename(config, "lumna", "bloqueio.ini", NULL)
					  : g_build_filename(g_get_home_dir(), ".config", "lumna", "bloqueio.ini", NULL);
	char *texto = NULL;
	papel[0] = '\0';
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
				lateral = !strcmp(valor, "lateral");
			} else if (!strcmp(chave, "papel")) {
				snprintf(papel, sizeof(papel), "%s", valor);
			} else if (!strcmp(chave, "player")) {
				mostrar_player = strcmp(valor, "nao") != 0;
			} else if (!strcmp(chave, "emma")) {
				mostrar_emma = strcmp(valor, "nao") != 0;
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(caminho);
	if (!papel[0] || access(papel, R_OK) != 0) {
		/* o mesmo do desktop, na tela principal */
		struct papel_cfg cfg;
		papel_ler(&cfg);
		const char *saidas[8];
		int n = gv_saidas(app_bloqueio, saidas, 8);
		const struct papel_item *it = papel_para(&cfg, papel_modo_atual(), n ? saidas[0] : "");
		snprintf(papel, sizeof(papel), "%s", it ? it->imagem : papel_padrao());
	}
}

static int bateria(void) {
	static const char *const caminhos[] = { "/sys/class/power_supply/BAT0/capacity", "/sys/class/power_supply/BAT1/capacity",
		"/sys/class/power_supply/battery/capacity" };
	for (size_t i = 0; i < sizeof(caminhos) / sizeof(caminhos[0]); i++) {
		FILE *f = fopen(caminhos[i], "r");
		int v;
		if (f) {
			bool ok = fscanf(f, "%d", &v) == 1;
			fclose(f);
			if (ok) {
				return v;
			}
		}
	}
	return -1;
}

static void musica_mudou(gv_app *app, int fd, void *dados) {
	mpris_ler(&musica);
	gv_app_redesenhar(app);
}

/* ---- entrar ---- */

static void ir_entrar(void) {
	tela = ENTRAR;
	toque = dev_ler().toque;
	usar_senha = entrar_pin_tamanho() == 0;
	pin[0] = senha[0] = erro[0] = '\0';
	mexeu = gv_agora();
	if (usar_senha) {
		gv_focar(CLAY_ID("senha"));
	}
}

static void conferir(bool com_senha) {
	if (entrar_espera() > 0) {
		return;
	}
	bool ok = com_senha ? entrar_senha_confere(senha) : entrar_pin_confere(pin);
	/* apaga o que se digitou, certo ou errado */
	memset(senha, 0, sizeof(senha));
	memset(pin, 0, sizeof(pin));
	if (ok) {
		entrar_acertou();
		gv_app_desbloquear(app_bloqueio);
		return;
	}
	entrar_errou();
	int espera = entrar_espera();
	if (espera > 0) {
		snprintf(erro, sizeof(erro), "Muitas tentativas. Espere %d s", espera);
	} else {
		snprintf(erro, sizeof(erro), com_senha ? "Senha errada" : "PIN errado");
	}
}

/* ---- a Emma do bloqueio: só o simples ---- */

static const char *const dias[] = { "domingo", "segunda-feira", "terça-feira", "quarta-feira", "quinta-feira", "sexta-feira",
	"sábado" };
static const char *const meses[] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro",
	"outubro", "novembro", "dezembro" };

static bool tem(const char *texto, const char *palavra) {
	return strcasestr(texto, palavra) != NULL;
}

static void responder(void) {
	time_t agora = time(NULL);
	struct tm t;
	localtime_r(&agora, &t);
	resposta_desbloquear = false;
	const char *q = pergunta;
	if (tem(q, "hora")) {
		snprintf(resposta, sizeof(resposta), "São %02d:%02d.", t.tm_hour, t.tm_min);
	} else if (tem(q, "dia") || tem(q, "data") || tem(q, "hoje")) {
		snprintf(resposta, sizeof(resposta), "Hoje é %s, %d de %s.", dias[t.tm_wday], t.tm_mday, meses[t.tm_mon]);
	} else if (tem(q, "bateria")) {
		int b = bateria();
		snprintf(resposta, sizeof(resposta), b >= 0 ? "A bateria está em %d%%." : "Este computador não tem bateria.", b);
	} else if (tem(q, "próxima") || tem(q, "proxima") || tem(q, "pula")) {
		mpris_proxima();
		snprintf(resposta, sizeof(resposta), musica.tem ? "Indo para a próxima." : "Não há nada tocando.");
	} else if (tem(q, "anterior") || tem(q, "volta")) {
		mpris_anterior();
		snprintf(resposta, sizeof(resposta), musica.tem ? "Voltando para a anterior." : "Não há nada tocando.");
	} else if (tem(q, "paus") || tem(q, "para a") || tem(q, "parar")) {
		if (musica.tocando) {
			mpris_tocar_pausar();
		}
		snprintf(resposta, sizeof(resposta), musica.tem ? "Pausado." : "Não há nada tocando.");
	} else if (tem(q, "toca") || tem(q, "continua") || tem(q, "play")) {
		if (musica.tem && !musica.tocando) {
			mpris_tocar_pausar();
		}
		snprintf(resposta, sizeof(resposta), musica.tem ? "Tocando." : "Não há nenhum player aberto.");
	} else if (tem(q, "música") || tem(q, "musica") || tem(q, "tocando")) {
		if (musica.tem && musica.titulo[0]) {
			snprintf(resposta, sizeof(resposta), "Está tocando “%s”%s%s.", musica.titulo, musica.artista[0] ? ", de " : "",
				musica.artista);
		} else {
			snprintf(resposta, sizeof(resposta), "Não há nada tocando.");
		}
	} else {
		snprintf(resposta, sizeof(resposta), "Para isso, desbloqueie primeiro: com a tela bloqueada eu só cuido da hora, da data, da música e da bateria.");
		resposta_desbloquear = true;
	}
	pergunta[0] = '\0';
}

/* ---- desenho ---- */

static Clay_Color branco(float a) {
	return (Clay_Color){ 255, 255, 255, 255 * a };
}

/* o texto por cima do papel: sempre claro, com o tema que for */
static void texto(Clay_String s, float tamanho, float alfa, enum gv_peso peso) {
	gv_texto(s, tamanho, branco(alfa), peso);
}

static bool botao_icone(Clay_ElementId id, const char *icone, float tamanho, bool cheio) {
	bool tocado = gv_tocado(id);
	float caixa = tamanho + (cheio ? 24 : 14);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(caixa), CLAY_SIZING_FIXED(caixa) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = cheio ? (gv_por_cima(id) ? gv_t.acento_cursor : gv_t.acento) : gv_por_cima(id) ? branco(0.12f) : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(caixa / 2) }) {
		tocado |= gv_focavel(id, caixa / 2);
		gv_icone(icone, tamanho, cheio ? gv_t.sobre_acento : branco(0.95f));
	}
	return tocado;
}

static void topo(void) {
	CLAY(CLAY_ID("topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 28, 28, 22, 0 }, .childGap = 22,
		.childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("wifi", 22, branco(0.9f));
		gv_icone("volume", 22, branco(0.9f));
		int b = bateria();
		if (b >= 0) {
			CLAY_AUTO_ID({ .layout = { .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("battery", 24, branco(0.9f));
				texto(gv_fmt("%d%%", b), gv_t.tamanho_corpo, 0.9f, GV_PESO_NORMAL);
			}
		}
	}
}

static void emma(void) {
	Clay_ElementId id = CLAY_ID("emma");
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 18, 18, 10, 10 }, .childGap = 8,
			.childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
		.backgroundColor = gv_por_cima(id) ? branco(0.06f) : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(16) }) {
		tocado |= gv_focavel(id, 16);
		gv_icone("emma", 56, gv_cor(0xe9e2ffff));
		CLAY_AUTO_ID({ .layout = { .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			texto(CLAY_STRING("Fale com a Emma"), gv_t.tamanho_destaque + 1, 0.92f, GV_PESO_NORMAL);
			gv_icone("mic", 20, branco(0.92f));
		}
	}
	if (tocado) {
		tela = EMMA;
		resposta[0] = '\0';
		mexeu = gv_agora();
		gv_focar(CLAY_ID("pergunta"));
	}
}

static void relogio(float tam_hora, bool a_esquerda) {
	time_t agora = time(NULL);
	struct tm t;
	localtime_r(&agora, &t);
	CLAY(CLAY_ID("relogio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 0,
		.childAlignment = { .x = a_esquerda ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_CENTER } } }) {
		texto(gv_fmt("%02d:%02d", t.tm_hour, t.tm_min), tam_hora, 1, GV_PESO_NORMAL);
		texto(gv_fmt("%s, %d de %s", dias[t.tm_wday], t.tm_mday, meses[t.tm_mon]), tam_hora * 0.24f, 0.95f, GV_PESO_NORMAL);
	}
	gv_acordar_em(60 - t.tm_sec + 0.05);
}

static const char *tempo(int64_t us) {
	static char buf[2][16];
	static int qual;
	qual = !qual;
	int s = (int)(us / 1000000);
	snprintf(buf[qual], sizeof(buf[qual]), "%d:%02d", s / 60, s % 60);
	return buf[qual];
}

static void player(float largura) {
	if (!mostrar_player || !musica.tem || !musica.titulo[0]) {
		return;
	}
	CLAY(CLAY_ID("player"), { .layout = { .sizing = { CLAY_SIZING_FIXED(largura) }, .padding = CLAY_PADDING_ALL(16), .childGap = 18,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_cor(0x0f0d18d8), .cornerRadius = CLAY_CORNER_RADIUS(18),
		.border = { .color = branco(0.14f), .width = GV_BORDA(1) } }) {
		Clay_ElementId capa = CLAY_ID("capa");
		if (musica.capa[0]) {
			CLAY(capa, gv_imagem(capa, musica.capa, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
				.layout = { .sizing = { CLAY_SIZING_FIXED(104), CLAY_SIZING_FIXED(104) } }, .cornerRadius = CLAY_CORNER_RADIUS(12) })) {}
		} else {
			CLAY(capa, { .layout = { .sizing = { CLAY_SIZING_FIXED(104), CLAY_SIZING_FIXED(104) },
				.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_alfa(gv_t.acento, 0.25f), .cornerRadius = CLAY_CORNER_RADIUS(12) }) {
				gv_icone("music", 40, branco(0.9f));
			}
		}
		CLAY(CLAY_ID("player-meio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
					texto(gv_str(musica.titulo), gv_t.tamanho_destaque + 2, 1, GV_PESO_MEDIO);
					if (musica.artista[0]) {
						texto(gv_str(musica.artista), gv_t.tamanho_corpo, 0.75f, GV_PESO_NORMAL);
					}
				}
				if (botao_icone(CLAY_ID("anterior"), "skip-back", 22, false)) {
					mpris_anterior();
				}
				if (botao_icone(CLAY_ID("tocar"), musica.tocando ? "pause" : "play", 24, true)) {
					mpris_tocar_pausar();
				}
				if (botao_icone(CLAY_ID("proxima"), "skip-forward", 22, false)) {
					mpris_proxima();
				}
			}
			/* o progresso: anda sozinho enquanto toca */
			int64_t pos = mpris_posicao(&musica);
			float frac = musica.duracao_us > 0 ? (float)pos / (float)musica.duracao_us : 0;
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				texto(gv_str(tempo(pos)), gv_t.tamanho_legenda, 0.8f, GV_PESO_NORMAL);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(4) } }, .backgroundColor = branco(0.18f),
					.cornerRadius = CLAY_CORNER_RADIUS(2) }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_PERCENT(frac < 1 ? frac : 1), CLAY_SIZING_GROW(0) } },
						.backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(2) }) {}
				}
				texto(gv_str(musica.duracao_us > 0 ? tempo(musica.duracao_us) : "--:--"), gv_t.tamanho_legenda, 0.8f, GV_PESO_NORMAL);
			}
			if (musica.tocando) {
				gv_acordar_em(1.0);
			}
		}
	}
}

static void cadeado(void) {
	if (botao_icone(CLAY_ID("cadeado"), "lock", 26, false)) {
		ir_entrar();
	}
}

/* o PIN: os pontos e o teclado de números */
static void entrar_pin(void) {
	int n = entrar_pin_tamanho();
	CLAY(CLAY_ID("pontos"), { .layout = { .childGap = 16, .padding = { 0, 0, 6, 14 } } }) {
		for (int i = 0; i < n; i++) {
			bool cheio = i < (int)strlen(pin);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(14), CLAY_SIZING_FIXED(14) } },
				.backgroundColor = cheio ? branco(0.95f) : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(7),
				.border = { .color = branco(0.8f), .width = GV_BORDA(2) } }) {}
		}
	}
	if (!toque) {
		/* sem toque: os pontos, o teclado e a troca para a senha */
		CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 8, 0 } } }) {
			if (gv_botao(CLAY_ID("usar-senha"), NULL, "Usar a senha", GV_BOTAO_SIMPLES)) {
				usar_senha = true;
				erro[0] = '\0';
				gv_focar(CLAY_ID("senha"));
			}
		}
	} else {
		static const char *const teclas[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "senha", "0", "apagar" };
		for (int linha = 0; linha < 4; linha++) {
			CLAY(CLAY_IDI("teclas", (uint32_t)linha), { .layout = { .childGap = 18 } }) {
				for (int c = 0; c < 3; c++) {
					int k = linha * 3 + c;
					Clay_ElementId id = CLAY_IDI("tecla", (uint32_t)k);
					bool tocado = gv_tocado(id);
					bool numero = k != 9 && k != 11;
					CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(76), CLAY_SIZING_FIXED(76) },
							.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
						.backgroundColor = numero ? (gv_premido(id) ? branco(0.28f) : gv_por_cima(id) ? branco(0.2f) : branco(0.1f))
							: (gv_por_cima(id) ? branco(0.1f) : (Clay_Color){ 0 }),
						.cornerRadius = CLAY_CORNER_RADIUS(38) }) {
						if (k == 11) {
							gv_icone("backspace", 26, branco(0.9f));
						} else if (k == 9) {
							texto(CLAY_STRING("Senha"), gv_t.tamanho_corpo, 0.85f, GV_PESO_NORMAL);
						} else {
							texto(gv_str(teclas[k]), 30, 1, GV_PESO_NORMAL);
						}
					}
					if (tocado) {
						mexeu = gv_agora();
						if (k == 11) {
							size_t l = strlen(pin);
							if (l) {
								pin[l - 1] = '\0';
							}
						} else if (k == 9) {
							usar_senha = true;
							erro[0] = '\0';
							gv_focar(CLAY_ID("senha"));
						} else if (strlen(pin) < sizeof(pin) - 1) {
							strcat(pin, teclas[k]);
							erro[0] = '\0';
						}
					}
				}
			}
		}
	}
	/* o teclado físico também: todas as teclas do quadro, pela ordem */
	uint32_t k;
	for (int i = 0; (k = gv_tecla_n(i)); i++) {
		int d = k >= XKB_KEY_0 && k <= XKB_KEY_9 ? (int)(k - XKB_KEY_0) : k >= XKB_KEY_KP_0 && k <= XKB_KEY_KP_9 ? (int)(k - XKB_KEY_KP_0) : -1;
		if (d >= 0 && strlen(pin) < sizeof(pin) - 1 && (int)strlen(pin) < (n > 0 ? n : 8)) {
			char s[2] = { (char)('0' + d), 0 };
			strcat(pin, s);
			erro[0] = '\0';
			mexeu = gv_agora();
		} else if (k == XKB_KEY_BackSpace && pin[0]) {
			pin[strlen(pin) - 1] = '\0';
		}
	}
	if (n > 0 && (int)strlen(pin) >= n) {
		conferir(false);
	}
}

static void entrar_senha(void) {
	CLAY(CLAY_ID("senha-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(420) }, .padding = { 0, 0, 6, 0 } } }) {
		if (gv_campo(CLAY_ID("senha"), senha, sizeof(senha), "Senha", GV_CAMPO_SENHA) & GV_CAMPO_ENTER) {
			conferir(true);
		}
	}
	CLAY(CLAY_ID("entrar-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(420) } } }) {
		if (gv_botao_ext(CLAY_ID("entrar"), &(gv_botao_opcoes){ .texto = "Entrar", .estilo = GV_BOTAO_CHEIO, .crescer = true,
			    .desativado = !senha[0] || entrar_espera() > 0 })) {
			conferir(true);
		}
	}
	if (entrar_pin_tamanho() > 0 && gv_botao(CLAY_ID("usar-pin"), NULL, "Usar o PIN", GV_BOTAO_SIMPLES)) {
		usar_senha = false;
		erro[0] = '\0';
	}
}

static void painel_entrar(void) {
	CLAY(CLAY_ID("entrar-meio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 10,
		.childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(112), CLAY_SIZING_FIXED(112) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_cor(0x16141ee6), .cornerRadius = CLAY_CORNER_RADIUS(56), .border = { .color = branco(0.15f), .width = GV_BORDA(1) } }) {
			gv_icone("user", 56, branco(0.95f));
		}
		const char *nome = g_get_real_name();
		texto(gv_str(nome && *nome && strcmp(nome, "Unknown") ? nome : g_get_user_name()), 28, 1, GV_PESO_MEDIO);
		texto(usar_senha ? CLAY_STRING("Digite a sua senha") : CLAY_STRING("Digite o seu PIN"), gv_t.tamanho_corpo + 1, 0.8f,
			GV_PESO_NORMAL);
		if (erro[0]) {
			gv_texto(gv_str(entrar_espera() > 0 ? gv_fmt("Muitas tentativas. Espere %d s", entrar_espera()).chars : erro),
				gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_MEDIO);
			if (entrar_espera() > 0) {
				gv_acordar_em(1);
			}
		}
		CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 8, 0 } } }) {}
		if (usar_senha) {
			entrar_senha();
		} else {
			entrar_pin();
		}
		CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 10, 0 } } }) {
			if (gv_botao(CLAY_ID("cancelar"), "back", "Voltar", GV_BOTAO_SIMPLES)) {
				tela = RELOGIO;
			}
		}
	}
}

static void painel_emma(void) {
	CLAY(CLAY_ID("emma-caixa"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(560) },
			.padding = CLAY_PADDING_ALL(22), .childGap = 14 },
		.backgroundColor = gv_cor(0x0f0d18e6), .cornerRadius = CLAY_CORNER_RADIUS(20), .border = { .color = branco(0.14f), .width = GV_BORDA(1) } }) {
		CLAY_AUTO_ID({ .layout = { .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone("emma", 34, gv_cor(0xe9e2ffff));
			texto(CLAY_STRING("Emma"), gv_t.tamanho_destaque + 2, 1, GV_PESO_MEDIO);
		}
		if (resposta[0]) {
			texto(gv_str(resposta), gv_t.tamanho_destaque, 0.92f, GV_PESO_NORMAL);
			if (resposta_desbloquear && gv_botao(CLAY_ID("emma-desbloquear"), "lock", "Desbloquear", GV_BOTAO_CHEIO)) {
				ir_entrar();
			}
		} else {
			texto(CLAY_STRING("Pergunte a hora, a data, o que está tocando, ou peça para pausar ou passar a música."),
				gv_t.tamanho_corpo, 0.7f, GV_PESO_NORMAL);
		}
		if (gv_campo(CLAY_ID("pergunta"), pergunta, sizeof(pergunta), "Pergunte algo simples", GV_CAMPO_TEXTO) & GV_CAMPO_ENTER) {
			if (pergunta[0]) {
				responder();
			}
		}
	}
	CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 10, 0 } } }) {
		if (gv_botao(CLAY_ID("emma-voltar"), "back", "Voltar", GV_BOTAO_SIMPLES)) {
			tela = RELOGIO;
		}
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	double agora = gv_agora();
	/* parado meio minuto fora do relógio: volta a ele (e apaga o que se digitou) */
	if (tela != RELOGIO) {
		/* só teclas e toques contam como mexer (o rato parado por cima não) */
		if (gv_tecla() || gv_tocado(CLAY_ID("raiz"))) {
			mexeu = agora;
		}
		if (agora - mexeu > 30) {
			tela = RELOGIO;
			memset(senha, 0, sizeof(senha));
			memset(pin, 0, sizeof(pin));
		} else {
			gv_acordar_em(30 - (agora - mexeu) + 0.1);
		}
		if (gv_tecla_carregada(XKB_KEY_Escape)) {
			tela = RELOGIO;
		}
	} else if (gv_tecla() && gv_tecla() != XKB_KEY_Escape) {
		/* começar a digitar no relógio vai direto ao PIN: o número que se carregou entra neste mesmo
		 * quadro, pelo teclado do PIN (que lê as teclas do quadro) */
		ir_entrar();
	}

	Clay_ElementId raiz = CLAY_ID("raiz");
	bool tocou_fundo = tela == RELOGIO && gv_tocado(raiz);
	/* a cor por baixo (se o papel faltar) e o papel por cima: num elemento só, a imagem não se via */
	Clay_ElementId fundo = CLAY_ID("papel");
	CLAY(raiz, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_cor(0x0b0a12ff) })
	CLAY(fundo, gv_imagem(fundo, papel, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
		/* o véu escurece o papel ao entrar ou falar com a Emma */
		float veu = gv_anim(raiz.id + 1, tela == RELOGIO ? 0 : 0.55f, gv_t.duracao_normal);
		CLAY(CLAY_ID("veu"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .backgroundColor = gv_cor(0x05040a00 | (uint32_t)(veu * 255)) }) {
			topo();
			if (tela == RELOGIO) {
				if (mostrar_emma) {
					emma();
				}
				if (lateral) {
					CLAY(CLAY_ID("espaco"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(0) } } }) {}
					CLAY(CLAY_ID("baixo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 80, 80, 0, 30 },
						.childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } } }) {
						relogio(170, true);
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
						player(580);
					}
				} else {
					CLAY(CLAY_ID("espaco1"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(1) } } }) {}
					relogio(128, false);
					CLAY(CLAY_ID("espaco2"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(1) } } }) {}
					player(650);
					CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 0, 14 } } }) {}
				}
				CLAY(CLAY_ID("pe"), { .layout = { .padding = { 0, 0, 0, 26 } } }) {
					cadeado();
				}
			} else {
				CLAY(CLAY_ID("espaco3"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(1) } } }) {}
				if (tela == ENTRAR) {
					painel_entrar();
				} else {
					painel_emma();
				}
				CLAY(CLAY_ID("espaco4"), { .layout = { .sizing = { .height = CLAY_SIZING_GROW(1) } } }) {}
			}
		}
	}
	/* tocar no papel (fora dos botões) também vai a entrar */
	if (tocou_fundo && tela == RELOGIO) {
		ir_entrar();
	}
}

int main(int argc, char **argv) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Bloqueio", .app_id = "lumna-bloqueio", .tipo = GV_BLOQUEIO });
	if (!app) {
		return 1;
	}
	app_bloqueio = app;
	ler_ajustes();
	gv_app_bloqueio_fundo(app, papel); /* as outras telas trancam junto, com o mesmo papel */
	int fd = mpris_iniciar();
	if (fd >= 0) {
		gv_app_vigiar(app, fd, musica_mudou, NULL);
	}
	return gv_app_correr(app, desenhar, NULL);
}
