/*
 * A tela de login do LUMNA (lumna-entrada), como nos esboços: o usuário, a senha, Entrar e
 * Trocar usuário; em baixo a sessão (LUMNA · Orbit...) e acessibilidade, reiniciar e desligar.
 * Dois layouts: Centralizado (o cartão ao meio do papel de parede) e Painel lateral (o painel
 * escuro à esquerda, o relógio e o papel à direita).
 *
 * O greetd corre-a como o usuário "greeter", dentro de um Orbit só para ela (orbit -S
 * lumna-entrada): com a senha certa ela pede a sessão ao greetd (servicos/greetd.h) e sai; o
 * Orbit fecha com ela e o greetd abre a sessão escolhida.
 *
 * O layout e o papel vêm de /etc/lumna/entrada.ini (ou $LUMNA_ENTRADA_INI):
 *   layout = centro | lateral
 *   papel = /caminho/da/imagem
 * O último usuário e a última sessão ficam em /var/lib/greetd/lumna-entrada (se der para gravar).
 */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"
#include "greetd.h"
#include "papelini.h"

#define ULTIMO "/var/lib/greetd/lumna-entrada"

static gv_app *app_entrada;
static bool lateral;
static char papel[1024];
static struct greetd_usuario usuarios[32];
static int nusuarios, usuario;
static struct greetd_sessao sessoes[16];
static int nsessoes, sessao;
static char senha[256];
static char erro[256];
static bool entrando;
static bool texto_grande;

/* ---- ajustes e lembranças ---- */

static void ler_ajustes(void) {
	const char *caminho = getenv("LUMNA_ENTRADA_INI");
	char *texto = NULL;
	if (g_file_get_contents(caminho ? caminho : "/etc/lumna/entrada.ini", &texto, NULL, NULL)) {
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
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	if (!papel[0] || access(papel, R_OK) != 0) {
		snprintf(papel, sizeof(papel), "%s", papel_padrao());
	}
}

static void lembrar_ler(void) {
	char *texto = NULL;
	if (!g_file_get_contents(ULTIMO, &texto, NULL, NULL)) {
		return;
	}
	char **linhas = g_strsplit(texto, "\n", -1);
	for (int i = 0; linhas[0] && i < nusuarios; i++) {
		if (!strcmp(usuarios[i].login, linhas[0])) {
			usuario = i;
		}
	}
	for (int i = 0; linhas[0] && linhas[1] && i < nsessoes; i++) {
		if (!strcmp(sessoes[i].id, linhas[1])) {
			sessao = i;
		}
	}
	g_strfreev(linhas);
	g_free(texto);
}

static void lembrar_gravar(void) {
	char *texto = g_strdup_printf("%s\n%s\n", usuarios[usuario].login, sessoes[sessao].id);
	g_file_set_contents(ULTIMO, texto, -1, NULL);
	g_free(texto);
}

static void executar(const char *comando) {
	pid_t p = fork();
	if (p == 0) {
		execl("/bin/sh", "sh", "-c", comando, (char *)NULL);
		_exit(127);
	}
}

/* ---- entrar ---- */

static void entrar(void) {
	if (entrando || !senha[0]) {
		return;
	}
	entrando = true;
	erro[0] = '\0';
	greetd_entrar_em_fundo(usuarios[usuario].login, senha, sessoes[sessao].exec);
	memset(senha, 0, sizeof(senha));
}

static void ver_resultado(void) {
	const char *e;
	if (!entrando) {
		return;
	}
	if (!greetd_terminou(&e)) {
		gv_acordar_em(0.05);
		return;
	}
	entrando = false;
	if (!e) {
		/* entrou: sai; o Orbit (-S) fecha connosco e o greetd abre a sessão */
		lembrar_gravar();
		gv_app_sair(app_entrada);
		return;
	}
	snprintf(erro, sizeof(erro), "%s", e);
	gv_focar(CLAY_ID("senha"));
}

/* ---- desenho ---- */

static Clay_Color branco(float a) {
	return (Clay_Color){ 255, 255, 255, 255 * a };
}

static float t(float base) {
	return texto_grande ? base + 3 : base;
}

static bool icone_botao(Clay_ElementId id, const char *icone, float tamanho) {
	bool tocado = gv_tocado(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(tamanho + 20), CLAY_SIZING_FIXED(tamanho + 20) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? branco(0.1f) : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
		tocado |= gv_focavel(id, gv_t.raio_pequeno);
		gv_icone(icone, tamanho, branco(0.92f));
	}
	return tocado;
}

static void topo(bool com_relogio) {
	CLAY(CLAY_ID("topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 28, 28, 22, 0 }, .childGap = 22,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("lumna", 24, gv_t.acento);
		gv_texto(CLAY_STRING("L U M N A"), t(gv_t.tamanho_corpo + 1), branco(0.95f), GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		gv_icone("wifi", 22, branco(0.9f));
		gv_icone("volume", 22, branco(0.9f));
		if (com_relogio) {
			time_t agora = time(NULL);
			struct tm tm;
			localtime_r(&agora, &tm);
			gv_texto(gv_fmt("%02d:%02d", tm.tm_hour, tm.tm_min), t(gv_t.tamanho_corpo + 1), branco(0.95f), GV_PESO_MEDIO);
		}
	}
}

/* o usuário, a senha, Entrar e Trocar usuário */
static void cartao(void) {
	const struct greetd_usuario *u = &usuarios[usuario];
	CLAY(CLAY_ID("cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(440) }, .childGap = 12,
		.childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(124), CLAY_SIZING_FIXED(124) },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_cor(0x18171eee), .cornerRadius = CLAY_CORNER_RADIUS(62), .border = { .color = branco(0.12f), .width = GV_BORDA(1) } }) {
			gv_icone("user", 62, branco(0.95f));
		}
		gv_texto(gv_str(u->nome[0] ? u->nome : u->login), t(30), branco(1), GV_PESO_MEDIO);
		gv_texto(CLAY_STRING("Bem-vindo de volta"), t(gv_t.tamanho_destaque), branco(0.75f), GV_PESO_NORMAL);
		CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 14, 0 } } }) {}
		CLAY(CLAY_ID("senha-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			if (gv_campo(CLAY_ID("senha"), senha, sizeof(senha), "Senha", GV_CAMPO_SENHA) & GV_CAMPO_ENTER) {
				entrar();
			}
		}
		if (erro[0]) {
			gv_texto(gv_str(erro), t(gv_t.tamanho_corpo), gv_t.perigo, GV_PESO_MEDIO);
		}
		CLAY(CLAY_ID("entrar-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 0 } } }) {
			if (gv_botao_ext(CLAY_ID("entrar"), &(gv_botao_opcoes){ .texto = entrando ? "Entrando…" : "Entrar", .estilo = GV_BOTAO_CHEIO,
				    .crescer = true, .altura = 50, .desativado = entrando || !senha[0] })) {
				entrar();
			}
		}
		if (nusuarios > 1) {
			if (gv_botao(CLAY_ID("trocar"), NULL, "Trocar usuário", GV_BOTAO_SIMPLES)) {
				gv_popup_alternar(CLAY_ID("usuarios"));
			}
			GV_POPUP(CLAY_ID("usuarios"), CLAY_ID("trocar"), .alinhar = GV_ALINHAR_CENTRO, .largura = 300) {
				for (int i = 0; i < nusuarios; i++) {
					if (gv_menu_item(CLAY_IDI("usuario", (uint32_t)i), "user", usuarios[i].nome[0] ? usuarios[i].nome : usuarios[i].login,
						    i == usuario ? "✓" : NULL)) {
						usuario = i;
						senha[0] = erro[0] = '\0';
						gv_popup_fechar(CLAY_ID("usuarios"));
						gv_focar(CLAY_ID("senha"));
					}
				}
			}
		}
	}
}

/* em baixo: a sessão à esquerda; acessibilidade, reiniciar e desligar à direita */
static void base(void) {
	CLAY(CLAY_ID("base"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 36, 28, 0, 36 }, .childGap = 6,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		const char *nomes[16];
		for (int i = 0; i < nsessoes; i++) {
			nomes[i] = sessoes[i].nome;
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(230) } } }) {
			gv_lista_suspensa(CLAY_ID("sessao"), nomes, nsessoes, &sessao);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		if (icone_botao(CLAY_ID("acessibilidade"), "accessibility", 26)) {
			texto_grande = !texto_grande;
		}
		gv_separador(true, 26);
		if (icone_botao(CLAY_ID("reiniciar"), "refresh", 26)) {
			executar("systemctl reboot");
		}
		gv_separador(true, 26);
		if (icone_botao(CLAY_ID("desligar"), "power", 26)) {
			executar("systemctl poweroff");
		}
	}
}

static void relogio_grande(void) {
	static const char *const dias[] = { "Domingo", "Segunda-feira", "Terça-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sábado" };
	static const char *const meses[] = { "janeiro", "fevereiro", "março", "abril", "maio", "junho", "julho", "agosto", "setembro",
		"outubro", "novembro", "dezembro" };
	time_t agora = time(NULL);
	struct tm tm;
	localtime_r(&agora, &tm);
	CLAY(CLAY_ID("relogio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
		gv_texto(gv_fmt("%02d:%02d", tm.tm_hour, tm.tm_min), 150, branco(1), GV_PESO_NORMAL);
		gv_texto(gv_fmt("%s, %d de %s", dias[tm.tm_wday], tm.tm_mday, meses[tm.tm_mon]), 34, branco(0.95f), GV_PESO_NORMAL);
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	ver_resultado();
	time_t agora = time(NULL);
	gv_acordar_em(60 - localtime(&agora)->tm_sec + 0.05);
	Clay_ElementId fundo = CLAY_ID("papel");
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_cor(0x0b0a12ff) })
	CLAY(fundo, gv_imagem(fundo, papel, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
		if (lateral) {
			CLAY(CLAY_ID("painel"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_PERCENT(0.4f), CLAY_SIZING_GROW(0) },
				.childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .backgroundColor = gv_cor(0x1d1e22f4) }) {
				topo(true);
				CLAY_AUTO_ID({ .layout = { .sizing = { .height = CLAY_SIZING_GROW(1) } } }) {}
				cartao();
				CLAY_AUTO_ID({ .layout = { .sizing = { .height = CLAY_SIZING_GROW(1) } } }) {}
				base();
			}
			CLAY(CLAY_ID("lado"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 0, 110, 0, 110 },
				.childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_BOTTOM } } }) {
				relogio_grande();
			}
		} else {
			CLAY(CLAY_ID("centro"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .backgroundColor = gv_cor(0x05040a40) }) {
				topo(true);
				CLAY_AUTO_ID({ .layout = { .sizing = { .height = CLAY_SIZING_GROW(1) } } }) {}
				cartao();
				CLAY_AUTO_ID({ .layout = { .sizing = { .height = CLAY_SIZING_GROW(1) } } }) {}
				base();
			}
		}
	}
	(void)app;
}

int main(int argc, char **argv) {
	ler_ajustes();
	nusuarios = greetd_usuarios(usuarios, 32);
	if (nusuarios == 0) {
		/* sem contas de pessoas (um teste): quem está a correr */
		snprintf(usuarios[0].login, sizeof(usuarios[0].login), "%s", g_get_user_name());
		const char *nome = g_get_real_name();
		snprintf(usuarios[0].nome, sizeof(usuarios[0].nome), "%s", nome && strcmp(nome, "Unknown") ? nome : "");
		nusuarios = 1;
	}
	nsessoes = greetd_sessoes(sessoes, 16);
	lembrar_ler();
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Entrar", .app_id = "lumna-entrada", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_OVERLAY, .ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1, .teclado_exclusivo = true });
	if (!app) {
		return 1;
	}
	app_entrada = app;
	gv_focar(CLAY_ID("senha"));
	return gv_app_correr(app, desenhar, NULL);
}
