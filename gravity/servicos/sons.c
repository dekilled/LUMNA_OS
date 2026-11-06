#include "sons.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *pasta(void) {
	const char *e = getenv("LUMNA_SONS_DIR");
	return e && *e ? e : "/usr/share/sounds/freedesktop/stereo";
}

static void fabrica(struct sons_cfg *c) {
	c->avisos = true;
	c->volume = 70;
	g_strlcpy(c->aviso, "message-new-instant", sizeof(c->aviso));
	g_strlcpy(c->critico, "dialog-warning", sizeof(c->critico));
}

void sons_ler(struct sons_cfg *c) {
	fabrica(c);
	const char *cfg = getenv("XDG_CONFIG_HOME");
	char *caminho = cfg && *cfg ? g_build_filename(cfg, "lumna", "sons.ini", NULL) : g_build_filename(g_get_home_dir(), ".config", "lumna", "sons.ini", NULL), *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			char *k = g_strstrip(*l), *v = g_strstrip(igual + 1);
			if (!strcmp(k, "avisos")) {
				c->avisos = strcmp(v, "nao") != 0;
			} else if (!strcmp(k, "volume")) {
				c->volume = atoi(v);
			} else if (!strcmp(k, "aviso")) {
				g_strlcpy(c->aviso, v, sizeof(c->aviso));
			} else if (!strcmp(k, "critico")) {
				g_strlcpy(c->critico, v, sizeof(c->critico));
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(caminho);
	c->volume = c->volume < 0 ? 0 : c->volume > 100 ? 100 : c->volume;
}

bool sons_gravar(const struct sons_cfg *c) {
	const char *cfg = getenv("XDG_CONFIG_HOME");
	char *dir = cfg && *cfg ? g_build_filename(cfg, "lumna", NULL) : g_build_filename(g_get_home_dir(), ".config", "lumna", NULL);
	g_mkdir_with_parents(dir, 0755);
	char *caminho = g_build_filename(dir, "sons.ini", NULL), *tmp = g_strconcat(caminho, ".novo", NULL);
	char *texto = g_strdup_printf("# Os sons do sistema (o Painel de controle grava este arquivo)\navisos = %s\nvolume = %d\naviso = %s\ncritico = %s\n", c->avisos ? "sim" : "nao",
		c->volume, c->aviso, c->critico);
	bool ok = g_file_set_contents(tmp, texto, -1, NULL) && g_rename(tmp, caminho) == 0;
	g_free(texto);
	g_free(tmp);
	g_free(caminho);
	g_free(dir);
	return ok;
}

static bool extensao_de_som(const char *n) {
	return g_str_has_suffix(n, ".oga") || g_str_has_suffix(n, ".ogg") || g_str_has_suffix(n, ".wav") || g_str_has_suffix(n, ".mp3") || g_str_has_suffix(n, ".flac");
}

static int por_nome(const void *a, const void *b) {
	return strcmp(a, b);
}

int sons_listar(char nomes[][40], int max) {
	GDir *dir = g_dir_open(pasta(), 0, NULL);
	const char *nome;
	int n = 0;
	while (dir && (nome = g_dir_read_name(dir)) && n < max) {
		if (!extensao_de_som(nome)) {
			continue;
		}
		char base[40];
		g_strlcpy(base, nome, sizeof(base));
		char *ponto = strrchr(base, '.');
		if (ponto) {
			*ponto = '\0';
		}
		bool repetido = false;
		for (int i = 0; i < n; i++) {
			repetido |= !strcmp(nomes[i], base);
		}
		if (!repetido) {
			g_strlcpy(nomes[n++], base, 40);
		}
	}
	if (dir) {
		g_dir_close(dir);
	}
	qsort(nomes, (size_t)n, 40, por_nome);
	return n;
}

static char *arquivo_de(const char *nome) {
	static const char *const exts[] = { ".oga", ".ogg", ".wav", ".mp3", ".flac" };
	if (!nome || !*nome || strchr(nome, '/')) {
		return NULL;
	}
	for (int i = 0; i < 5; i++) {
		char *c = g_strconcat(pasta(), "/", nome, exts[i], NULL);
		if (g_file_test(c, G_FILE_TEST_IS_REGULAR)) {
			return c;
		}
		g_free(c);
	}
	return NULL;
}

bool sons_tocar(const char *nome, int volume) {
	char *arquivo = arquivo_de(nome);
	if (!arquivo) {
		return false;
	}
	volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
	char *vol = NULL;
	char *tocador = g_find_program_in_path("pw-play");
	if (tocador) {
		vol = g_strdup_printf("--volume=%.2f", volume / 100.0);
	} else if ((tocador = g_find_program_in_path("paplay"))) {
		vol = g_strdup_printf("--volume=%d", volume * 65536 / 100);
	}
	bool ok = false;
	if (tocador) {
		char *argv[] = { tocador, vol, arquivo, NULL };
		ok = g_spawn_async(NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
	}
	g_free(tocador);
	g_free(vol);
	g_free(arquivo);
	return ok;
}

const char *sons_rotulo(const char *nome) {
	static const struct { const char *id, *rotulo; } nomes[] = {
		{ "message", "Mensagem" }, { "message-new-instant", "Mensagem nova" }, { "bell", "Sino" }, { "complete", "Concluído" },
		{ "dialog-information", "Informação" }, { "dialog-warning", "Atenção" }, { "dialog-error", "Erro" }, { "alarm-clock-elapsed", "Despertador" },
		{ "phone-incoming-call", "Chamada" }, { "power-plug", "Tomada" }, { "power-unplug", "Sem tomada" }, { "service-login", "Entrou" },
		{ "service-logout", "Saiu" }, { "suspend-error", "Erro ao suspender" }, { "trash-empty", "Lixeira esvaziada" }, { "camera-shutter", "Foto" },
		{ "screen-capture", "Captura de tela" }, { "audio-volume-change", "Volume" }, { "device-added", "Aparelho ligado" }, { "device-removed", "Aparelho tirado" },
		{ "network-connectivity-established", "Conectou" }, { "network-connectivity-lost", "Desconectou" }, { "window-attention", "Janela chama" },
	};
	for (size_t i = 0; i < sizeof(nomes) / sizeof(nomes[0]); i++) {
		if (!strcmp(nomes[i].id, nome)) {
			return nomes[i].rotulo;
		}
	}
	return nome;
}
