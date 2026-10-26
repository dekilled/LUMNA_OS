/*
 * Sistema → Teclado e mouse → Atalhos: o registro dos atalhos do Orbit e a troca de cada um. Vão
 * para ~/.config/orbit/atalhos.ini no Aplicar; o Orbit vigia o arquivo e passa a usá-los na hora.
 * Ao gravar um atalho novo, os do Orbit param (orbit-msg atalhos pausar), senão o Alt+Tab que se
 * quer gravar trocava de janela em vez de chegar aqui.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

struct atalho {
	const char *id;   /* o nome da ação no Orbit */
	const char *nome;
	const char *padrao;
	const char *grupo;
};

static const struct atalho atalhos[] = {
	{ "lancador", "Abrir e fechar o lançador", "Super", "Desktop" },
	{ "modo", "Trocar o modo (Desktop, Gaming, Mídia)", "Super+Tab", "Desktop" },
	{ "terminal", "Abrir o terminal", "Alt+Return", "Desktop" },
	{ "captura", "Capturar a tela", "-", "Desktop" },
	{ "alternar", "Alternar entre as janelas", "Alt+Tab", "Janelas" },
	{ "alternar_voltar", "Alternar ao contrário", "Alt+Shift+Tab", "Janelas" },
	{ "minimizar", "Minimizar a janela", "Super+h", "Janelas" },
	{ "encaixar_esquerda", "Encaixar na metade esquerda", "Super+Left", "Janelas" },
	{ "encaixar_direita", "Encaixar na metade direita", "Super+Right", "Janelas" },
	{ "maximizar", "Maximizar", "Super+Up", "Janelas" },
	{ "restaurar", "Desfazer o encaixe (ou minimizar)", "Super+Down", "Janelas" },
	{ "fechar", "Fechar a janela", "Alt+q", "Janelas" },
	{ "ecra", "Tela cheia", "Alt+f", "Janelas" },
	{ "flutuar", "Soltar do mosaico", "Alt+space", "Janelas" },
	{ "foco_seguinte", "Foco na janela seguinte", "Alt+j", "Janelas" },
	{ "foco_anterior", "Foco na janela anterior", "Alt+k", "Janelas" },
	{ "saida_esquerda", "Mandar para a tela da esquerda", "Alt+Left", "Janelas" },
	{ "saida_direita", "Mandar para a tela da direita", "Alt+Right", "Janelas" },
	{ "espaco_1", "Ir para a área 1", "Alt+1", "Áreas de trabalho" },
	{ "espaco_2", "Ir para a área 2", "Alt+2", "Áreas de trabalho" },
	{ "espaco_3", "Ir para a área 3", "Alt+3", "Áreas de trabalho" },
	{ "espaco_4", "Ir para a área 4", "Alt+4", "Áreas de trabalho" },
	{ "mover_1", "Mandar a janela para a área 1", "Alt+Shift+1", "Áreas de trabalho" },
	{ "mover_2", "Mandar a janela para a área 2", "Alt+Shift+2", "Áreas de trabalho" },
	{ "mover_3", "Mandar a janela para a área 3", "Alt+Shift+3", "Áreas de trabalho" },
	{ "mover_4", "Mandar a janela para a área 4", "Alt+Shift+4", "Áreas de trabalho" },
};
#define NATALHOS ((int)(sizeof(atalhos) / sizeof(atalhos[0])))

static char valor[NATALHOS][64], gravado[NATALHOS][64];
static int gravando = -1; /* a linha à espera das teclas */
static char aviso[200];

static void caminho(char *buf, size_t tam) {
	const char *config = getenv("XDG_CONFIG_HOME");
	if (config && *config) {
		snprintf(buf, tam, "%s/orbit/atalhos.ini", config);
	} else {
		snprintf(buf, tam, "%s/.config/orbit/atalhos.ini", g_get_home_dir());
	}
}

static void parar_gravacao(void) {
	if (gravando >= 0) {
		gravando = -1;
		pn_executar("orbit-msg atalhos retomar");
	}
}

void pn_atalhos_abrir(void) {
	for (int i = 0; i < NATALHOS; i++) {
		snprintf(gravado[i], sizeof(gravado[i]), "%s", atalhos[i].padrao);
	}
	char c[1024];
	caminho(c, sizeof(c));
	char *texto = NULL;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#' || **l == '[') {
				continue;
			}
			*igual = '\0';
			char *chave = g_strstrip(*l), *v = g_strstrip(igual + 1);
			for (int i = 0; i < NATALHOS; i++) {
				if (!strcmp(chave, atalhos[i].id)) {
					snprintf(gravado[i], sizeof(gravado[i]), "%s", v);
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	memcpy(valor, gravado, sizeof(valor));
	aviso[0] = '\0';
	parar_gravacao();
}

bool pn_atalhos_pendente(void) {
	return memcmp(valor, gravado, sizeof(valor)) != 0;
}

void pn_atalhos_aplicar(void) {
	parar_gravacao();
	GString *s = g_string_new("# Os atalhos do Orbit (o Painel de controle grava este arquivo)\n[atalho]\n");
	for (int i = 0; i < NATALHOS; i++) {
		g_string_append_printf(s, "%s = %s\n", atalhos[i].id, valor[i]);
	}
	char c[1024];
	caminho(c, sizeof(c));
	char *pasta = g_path_get_dirname(c);
	g_mkdir_with_parents(pasta, 0755);
	g_free(pasta);
	if (g_file_set_contents(c, s->str, -1, NULL)) {
		memcpy(gravado, valor, sizeof(valor));
	}
	g_string_free(s, TRUE);
}

void pn_atalhos_descartar(void) {
	parar_gravacao();
	memcpy(valor, gravado, sizeof(valor));
	aviso[0] = '\0';
}

void pn_atalhos_restaurar(void) {
	parar_gravacao();
	for (int i = 0; i < NATALHOS; i++) {
		snprintf(valor[i], sizeof(valor[i]), "%s", atalhos[i].padrao);
	}
	aviso[0] = '\0';
}

static bool modificadora(uint32_t k) {
	return k == XKB_KEY_Shift_L || k == XKB_KEY_Shift_R || k == XKB_KEY_Control_L || k == XKB_KEY_Control_R ||
		k == XKB_KEY_Alt_L || k == XKB_KEY_Alt_R || k == XKB_KEY_Super_L || k == XKB_KEY_Super_R || k == XKB_KEY_Meta_L ||
		k == XKB_KEY_Meta_R || k == XKB_KEY_ISO_Level3_Shift || k == XKB_KEY_Caps_Lock;
}

/* as teclas que chegaram neste quadro viram o atalho da linha que está gravando */
static void ouvir(void) {
	uint32_t k;
	for (int n = 0; (k = gv_tecla_n(n)); n++) {
		uint32_t mods = gv_modificadores();
		if (k == XKB_KEY_Escape && !mods) {
			parar_gravacao();
			return;
		}
		if (modificadora(k)) {
			continue;
		}
		if (k == XKB_KEY_ISO_Left_Tab) {
			k = XKB_KEY_Tab; /* Shift+Tab chega assim; o Orbit vê o Tab */
		}
		k = xkb_keysym_to_lower(k);
		char nome[40];
		if (xkb_keysym_get_name(k, nome, sizeof(nome)) <= 0) {
			continue;
		}
		char novo[64];
		snprintf(novo, sizeof(novo), "%s%s%s%s%s", mods & GV_MOD_LOGO ? "Super+" : "", mods & GV_MOD_CTRL ? "Ctrl+" : "",
			mods & GV_MOD_ALT ? "Alt+" : "", mods & GV_MOD_SHIFT ? "Shift+" : "", nome);
		/* o mesmo atalho noutra ação: sai de lá */
		aviso[0] = '\0';
		for (int i = 0; i < NATALHOS; i++) {
			if (i != gravando && !g_ascii_strcasecmp(valor[i], novo)) {
				snprintf(valor[i], sizeof(valor[i]), "-");
				snprintf(aviso, sizeof(aviso), "%s saiu de \"%s\", que ficou sem atalho.", novo, atalhos[i].nome);
			}
		}
		snprintf(valor[gravando], sizeof(valor[gravando]), "%s", novo);
		parar_gravacao();
		return;
	}
}

/* "Alt+Shift+Tab" em teclinhas */
static void teclas(Clay_ElementId id, const char *atalho) {
	CLAY(id, { .layout = { .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		if (!atalho[0] || !strcmp(atalho, "-")) {
			gv_texto(CLAY_STRING("Nenhum"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		} else {
			char **partes = g_strsplit(atalho, "+", -1);
			for (char **p = partes; *p; p++) {
				const char *t = *p;
				if (!g_ascii_strcasecmp(t, "Return")) {
					t = "Enter";
				} else if (!g_ascii_strcasecmp(t, "space")) {
					t = "Espaço";
				} else if (!g_ascii_strcasecmp(t, "Left")) {
					t = "←";
				} else if (!g_ascii_strcasecmp(t, "Right")) {
					t = "→";
				} else if (!g_ascii_strcasecmp(t, "Up")) {
					t = "↑";
				} else if (!g_ascii_strcasecmp(t, "Down")) {
					t = "↓";
				}
				char mostrar[64];
				snprintf(mostrar, sizeof(mostrar), "%s", t);
				if (g_utf8_strlen(mostrar, -1) == 1) {
					mostrar[0] = (char)g_ascii_toupper(mostrar[0]);
				}
				if (p != partes) {
					gv_texto(CLAY_STRING("+"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
				}
				CLAY_AUTO_ID({ .layout = { .padding = { 8, 8, 3, 4 } }, .backgroundColor = gv_t.elevada,
					.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
					.border = { .color = gv_t.borda_controle, .width = { 1, 1, 1, 2 } } }) {
					gv_texto(gv_str(gv_fmt("%s", mostrar).chars), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_MEDIO);
				}
			}
			g_strfreev(partes);
		}
	}
}

void pn_atalhos_desenhar(void) {
	if (gravando >= 0) {
		ouvir();
	}
	gv_texto(CLAY_STRING("Toque em Mudar e aperte as teclas novas (Esc cancela). O Super sozinho abre o lançador."),
		gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	if (aviso[0]) {
		CLAY_AUTO_ID({ .layout = { .padding = { 0, 0, 6, 0 } } }) {
			gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.aviso, GV_PESO_MEDIO);
		}
	}
	const char *grupo = NULL;
	for (int i = 0; i < NATALHOS; i++) {
		if (!grupo || strcmp(grupo, atalhos[i].grupo)) {
			grupo = atalhos[i].grupo;
			pn_subtitulo(CLAY_IDI("at-g", (uint32_t)i), grupo);
		}
		pn_linha_comecar(CLAY_IDI("at", (uint32_t)i), NULL, atalhos[i].nome, NULL);
		if (gravando == i) {
			CLAY_AUTO_ID({ .layout = { .padding = { 10, 10, 5, 5 } }, .backgroundColor = gv_t.selecionado,
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.acento, .width = GV_BORDA(1) } }) {
				gv_texto(CLAY_STRING("Aperte as teclas…"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_MEDIO);
			}
			if (gv_botao(CLAY_IDI("at-cancelar", (uint32_t)i), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
				parar_gravacao();
			}
		} else {
			teclas(CLAY_IDI("at-v", (uint32_t)i), valor[i]);
			if (strcmp(valor[i], atalhos[i].padrao) &&
			    gv_botao(CLAY_IDI("at-padrao", (uint32_t)i), NULL, "Padrão", GV_BOTAO_SIMPLES)) {
				snprintf(valor[i], sizeof(valor[i]), "%s", atalhos[i].padrao);
			}
			if (strcmp(valor[i], "-") && gv_botao(CLAY_IDI("at-tirar", (uint32_t)i), NULL, "Desligar", GV_BOTAO_SIMPLES)) {
				snprintf(valor[i], sizeof(valor[i]), "-");
			}
			if (gv_botao(CLAY_IDI("at-mudar", (uint32_t)i), NULL, "Mudar", GV_BOTAO_ELEVADO)) {
				if (gravando < 0) {
					pn_executar("orbit-msg atalhos pausar");
				}
				gravando = i;
				aviso[0] = '\0';
			}
		}
		pn_linha_acabar();
	}
}
