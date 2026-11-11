#include <ctype.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "autostart.h"
#include "emma_ativa.h"
#include "emmar.h"
#include "entrada.h"
#include "json.h"
#include "modos.h"
#include "setup.h"

/* ---------------- os arquivos ---------------- */

static char *caminho_lumna(const char *nome) {
	return g_build_filename(g_get_user_config_dir(), "lumna", nome, NULL);
}

static GKeyFile *ini_abrir(const char *nome) {
	GKeyFile *kf = g_key_file_new();
	char *a = caminho_lumna(nome);
	g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL);
	g_free(a);
	return kf;
}

static bool ini_salvar(GKeyFile *kf, const char *nome) {
	char *a = caminho_lumna(nome), *dir = g_path_get_dirname(a);
	g_mkdir_with_parents(dir, 0700);
	bool ok = g_key_file_save_to_file(kf, a, NULL);
	g_free(dir);
	g_free(a);
	return ok;
}

static bool sim(GKeyFile *kf, const char *g, const char *k, bool padrao) {
	char *v = g_key_file_get_string(kf, g, k, NULL);
	bool r = padrao;
	if (v) {
		r = !strcmp(v, "sim") || !strcmp(v, "true") || !strcmp(v, "1");
	}
	g_free(v);
	return r;
}

static void erro_de(char *erro, size_t tam, const char *msg) {
	if (erro && tam) {
		g_strlcpy(erro, msg, tam);
	}
}

/* ---------------- o estado ---------------- */

void setup_ler(struct setup_estado *e) {
	memset(e, 0, sizeof(*e));
	GKeyFile *kf = ini_abrir("setup.ini");
	e->concluido = sim(kf, "setup", "concluido", false);
	e->nao_mostrar = sim(kf, "setup", "nao_mostrar", false);
	e->aberturas = g_key_file_get_integer(kf, "setup", "aberturas", NULL);
	char *p = g_key_file_get_string(kf, "setup", "perfil", NULL);
	if (p && setup_perfil_de_id(p) >= 0) {
		g_strlcpy(e->perfil, p, sizeof(e->perfil));
	}
	g_free(p);
	g_key_file_free(kf);
}

bool setup_gravar(const struct setup_estado *e) {
	GKeyFile *kf = ini_abrir("setup.ini");
	g_key_file_set_string(kf, "setup", "concluido", e->concluido ? "sim" : "nao");
	g_key_file_set_string(kf, "setup", "nao_mostrar", e->nao_mostrar ? "sim" : "nao");
	g_key_file_set_integer(kf, "setup", "aberturas", e->aberturas);
	g_key_file_set_string(kf, "setup", "perfil", e->perfil);
	bool ok = ini_salvar(kf, "setup.ini");
	g_key_file_free(kf);
	return ok;
}

int setup_no_login(const struct setup_estado *e) {
	if (e->concluido || e->nao_mostrar) {
		return 0;
	}
	return e->aberturas == 0 ? 1 : 2;
}

/* ---------------- o perfil ---------------- */

static const struct {
	const char *id, *nome, *descricao, *icone, *pacotes, *modo;
} perfis[PERFIL_N] = {
	{ "diario", "Uso diário", "Navegação e trabalho", "computer", "firefox thunderbird libreoffice-fresh", "desktop" },
	{ "gaming", "Gaming", "Jogos e desempenho", "gamepad", "steam lutris gamemode mangohud", "gaming" },
	{ "midia", "Mídia", "Vídeos e música", "video", "mpv yt-dlp", "midia" },
	{ "dev", "Desenvolvimento", "Editor e terminal", "code", "git neovim docker", "desktop" },
};

const char *setup_perfil_id(int p) { return p >= 0 && p < PERFIL_N ? perfis[p].id : ""; }
const char *setup_perfil_nome(int p) { return p >= 0 && p < PERFIL_N ? perfis[p].nome : ""; }
const char *setup_perfil_descricao(int p) { return p >= 0 && p < PERFIL_N ? perfis[p].descricao : ""; }
const char *setup_perfil_icone(int p) { return p >= 0 && p < PERFIL_N ? perfis[p].icone : "app"; }
const char *setup_perfil_pacotes(int p) { return p >= 0 && p < PERFIL_N ? perfis[p].pacotes : ""; }

int setup_perfil_de_id(const char *id) {
	for (int i = 0; id && i < PERFIL_N; i++) {
		if (!strcmp(perfis[i].id, id)) {
			return i;
		}
	}
	return -1;
}

bool setup_perfil_aplicar(int p, char *erro, size_t tam) {
	if (p < 0 || p >= PERFIL_N) {
		erro_de(erro, tam, "Perfil desconhecido.");
		return false;
	}
	struct modos_cfg c;
	modos_cfg_ler(&c);
	int m = modos_de_id(perfis[p].modo);
	c.ao_ligar = m >= 0 ? m : MODO_DESKTOP;
	bool ok = modos_cfg_gravar(&c);
	GKeyFile *kf = g_key_file_new();
	g_key_file_set_string(kf, "perfil", "id", perfis[p].id);
	g_key_file_set_string(kf, "pacotes", "lista", perfis[p].pacotes);
	ok = ini_salvar(kf, "sugestoes.ini") && ok;
	g_key_file_free(kf);
	struct setup_estado e;
	setup_ler(&e);
	g_strlcpy(e.perfil, perfis[p].id, sizeof(e.perfil));
	ok = setup_gravar(&e) && ok;
	if (!ok) {
		erro_de(erro, tam, "Não consegui gravar o perfil.");
	}
	return ok;
}

char *setup_sugestoes_ler(void) {
	GKeyFile *kf = ini_abrir("sugestoes.ini");
	char *l = g_key_file_get_string(kf, "pacotes", "lista", NULL);
	g_key_file_free(kf);
	return l ? l : g_strdup("");
}

/* ---------------- região e hora ---------------- */

bool setup_hora_formato(bool h24) {
	char *a = caminho_lumna("hora.ini"), *dir = g_path_get_dirname(a);
	g_mkdir_with_parents(dir, 0700);
	char *t = g_strdup_printf("[geral]\nformato = %s\n", h24 ? "24" : "12");
	bool ok = g_file_set_contents(a, t, -1, NULL);
	g_free(t);
	g_free(dir);
	g_free(a);
	return ok;
}

static bool rodar(char **argv, char *erro, size_t tam, const char *falha) {
	char *err = NULL;
	int estado = 0;
	GError *ge = NULL;
	bool rodou = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, &ge);
	bool ok = rodou && WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
	if (!ok) {
		const char *m = err && *g_strstrip(err) ? err : ge ? ge->message : falha;
		erro_de(erro, tam, m);
	}
	g_clear_error(&ge);
	g_free(err);
	return ok;
}

static bool nome_simples(const char *s, const char *extra) {
	if (!s || !*s) {
		return false;
	}
	for (const char *c = s; *c; c++) {
		if (!isalnum((unsigned char)*c) && !strchr(extra, *c)) {
			return false;
		}
	}
	return true;
}

bool setup_fuso(const char *fuso, char *erro, size_t tam) {
	if (!nome_simples(fuso, "_/+-") || strstr(fuso, "..") || fuso[0] == '-') {
		erro_de(erro, tam, "Fuso horário inválido.");
		return false;
	}
	char *argv[] = { "timedatectl", "set-timezone", (char *)fuso, NULL };
	return rodar(argv, erro, tam, "O timedatectl não conseguiu mudar o fuso.");
}

bool setup_teclado(const char *layout, const char *variante, char *erro, size_t tam) {
	if (!nome_simples(layout, "_-") || (variante && *variante && !nome_simples(variante, "_-"))) {
		erro_de(erro, tam, "Teclado inválido.");
		return false;
	}
	struct entrada_cfg c;
	entrada_cfg_ler(&c);
	g_strlcpy(c.layout, layout, sizeof(c.layout));
	g_strlcpy(c.variante, variante ? variante : "", sizeof(c.variante));
	if (!entrada_cfg_gravar(&c)) {
		erro_de(erro, tam, "Não consegui gravar o teclado.");
		return false;
	}
	return true;
}

/* ---------------- e-mail ---------------- */

bool setup_email_valido(const char *e) {
	if (!e || !*e || strlen(e) > 200) {
		return false;
	}
	const char *arroba = strchr(e, '@');
	if (!arroba || arroba == e || strchr(arroba + 1, '@') || !strchr(arroba + 1, '.')) {
		return false;
	}
	for (const char *c = e; *c; c++) {
		if (isspace((unsigned char)*c) || iscntrl((unsigned char)*c)) {
			return false;
		}
	}
	return arroba[1] != '.' && e[strlen(e) - 1] != '.';
}

bool setup_email_guardar(const char *endereco, const char *imap, int imap_porta, const char *smtp, int smtp_porta, char *erro, size_t tam) {
	if (!setup_email_valido(endereco)) {
		erro_de(erro, tam, "Esse endereço de e-mail não parece certo.");
		return false;
	}
	GKeyFile *kf = ini_abrir("setup.ini");
	g_key_file_set_string(kf, "email", "endereco", endereco);
	g_key_file_set_string(kf, "email", "imap", imap ? imap : "");
	g_key_file_set_integer(kf, "email", "imap_porta", imap_porta);
	g_key_file_set_string(kf, "email", "smtp", smtp ? smtp : "");
	g_key_file_set_integer(kf, "email", "smtp_porta", smtp_porta);
	bool ok = ini_salvar(kf, "setup.ini");
	g_key_file_free(kf);
	if (!ok) {
		erro_de(erro, tam, "Não consegui gravar o e-mail.");
	}
	return ok;
}

void setup_email_ler(char *endereco, size_t tam) {
	GKeyFile *kf = ini_abrir("setup.ini");
	char *e = g_key_file_get_string(kf, "email", "endereco", NULL);
	g_strlcpy(endereco, e ? e : "", tam);
	g_free(e);
	g_key_file_free(kf);
}

/* ---------------- nuvem ---------------- */

static const struct {
	const char *nome, *tipo;
} nuvens[NUVEM_N] = { { "Google Drive", "drive" }, { "OneDrive", "onedrive" }, { "Dropbox", "dropbox" } };

const char *setup_nuvem_nome(int p) { return p >= 0 && p < NUVEM_N ? nuvens[p].nome : ""; }
const char *setup_nuvem_tipo(int p) { return p >= 0 && p < NUVEM_N ? nuvens[p].tipo : ""; }

bool setup_rclone_existe(void) {
	char *p = g_find_program_in_path("rclone");
	bool ok = p != NULL;
	g_free(p);
	return ok;
}

char *setup_pasta_expandir(const char *pasta) {
	if (!pasta || !*pasta) {
		return g_build_filename(g_get_home_dir(), "Nuvem", NULL);
	}
	if (pasta[0] == '~' && (pasta[1] == '/' || pasta[1] == '\0')) {
		return g_build_filename(g_get_home_dir(), pasta + (pasta[1] ? 2 : 1), NULL);
	}
	return g_strdup(pasta);
}

static char *remoto_nome(int provedor) {
	return g_strdup_printf("lumna-%s", nuvens[provedor].tipo);
}

bool setup_nuvem_conectar(int provedor, const char *pasta, char *erro, size_t tam) {
	if (provedor < 0 || provedor >= NUVEM_N) {
		erro_de(erro, tam, "Escolha um serviço de nuvem.");
		return false;
	}
	if (!setup_rclone_existe()) {
		erro_de(erro, tam, "O rclone não está instalado (instale pela Loja). A escolha fica guardada.");
		return false;
	}
	char *dir = setup_pasta_expandir(pasta);
	if (g_mkdir_with_parents(dir, 0700) != 0) {
		erro_de(erro, tam, "Não consegui criar a pasta de sincronização.");
		g_free(dir);
		return false;
	}
	g_free(dir);
	char *nome = remoto_nome(provedor);
	char *argv[] = { "rclone", "config", "create", nome, (char *)nuvens[provedor].tipo, NULL };
	bool ok = rodar(argv, erro, tam, "O rclone não conseguiu entrar na conta.");
	g_free(nome);
	return ok;
}

int setup_nuvem_ler(char *pasta, size_t tam, bool *segundo_plano) {
	GKeyFile *kf = ini_abrir("setup.ini");
	char *tipo = g_key_file_get_string(kf, "nuvem", "provedor", NULL), *p = g_key_file_get_string(kf, "nuvem", "pasta", NULL);
	int r = -1;
	for (int i = 0; tipo && i < NUVEM_N; i++) {
		if (!strcmp(tipo, nuvens[i].tipo)) {
			r = i;
		}
	}
	if (pasta) {
		g_strlcpy(pasta, p && *p ? p : "~/Nuvem", tam);
	}
	if (segundo_plano) {
		*segundo_plano = sim(kf, "nuvem", "segundo_plano", false);
	}
	g_free(tipo);
	g_free(p);
	g_key_file_free(kf);
	return r;
}

bool setup_nuvem_aplicar(int provedor, const char *pasta, bool segundo_plano, char *erro, size_t tam) {
	if (provedor < 0 || provedor >= NUVEM_N) {
		erro_de(erro, tam, "Escolha um serviço de nuvem.");
		return false;
	}
	char *dir = setup_pasta_expandir(pasta), *remoto = remoto_nome(provedor);
	GKeyFile *kf = ini_abrir("setup.ini");
	g_key_file_set_string(kf, "nuvem", "provedor", nuvens[provedor].tipo);
	g_key_file_set_string(kf, "nuvem", "pasta", pasta && *pasta ? pasta : "~/Nuvem");
	g_key_file_set_string(kf, "nuvem", "segundo_plano", segundo_plano ? "sim" : "nao");
	g_key_file_set_string(kf, "nuvem", "remoto", remoto);
	bool ok = ini_salvar(kf, "setup.ini");
	g_key_file_free(kf);
	if (ok && g_mkdir_with_parents(dir, 0700) != 0) {
		erro_de(erro, tam, "Não consegui criar a pasta de sincronização.");
		ok = false;
	}
	if (ok) {
		/* ao entrar, o rclone monta a nuvem na pasta; desligado, a entrada some */
		if (segundo_plano) {
			char *cmd = g_strdup_printf("rclone mount %s: \"%s\" --vfs-cache-mode writes", remoto, dir);
			ok = autostart_adicionar("Nuvem (rclone)", cmd, "cloud", NULL, 0);
			g_free(cmd);
			if (!ok) {
				erro_de(erro, tam, "Não consegui ligar a nuvem ao entrar.");
			}
		} else {
			struct autostart_item l[AUTOSTART_MAX];
			int n = autostart_listar(l, AUTOSTART_MAX);
			for (int i = 0; i < n; i++) {
				if (!strcmp(l[i].nome, "Nuvem (rclone)") && l[i].do_usuario) {
					autostart_remover(l[i].arquivo);
				}
			}
		}
	}
	g_free(dir);
	g_free(remoto);
	return ok;
}

/* ---------------- a Emma ---------------- */

static void parar(const char *programa) {
	char *argv[] = { "pkill", "-x", (char *)programa, NULL };
	g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}

bool setup_emma_definir(bool ativa, char *erro, size_t tam) {
	if (!emma_ativa_gravar(ativa)) {
		erro_de(erro, tam, "Não consegui gravar a escolha da Emma.");
		return false;
	}
	if (ativa) {
		char *argv[] = { "lumna-emma", NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, NULL, NULL);
	} else {
		parar("lumna-emma-chat");
		parar("lumna-emma-app");
		parar("lumna-emma");
	}
	return true;
}

bool setup_emma_oi(bool ligar) {
	GKeyFile *kf = ini_abrir("setup.ini");
	g_key_file_set_string(kf, "emma", "oi_emma", ligar ? "sim" : "nao");
	bool ok = ini_salvar(kf, "setup.ini");
	g_key_file_free(kf);
	struct emmar r;
	if (emma_ativa() && emmar_chamar("PUT", "/api/wake", ligar ? "{\"enabled\":true}" : "{\"enabled\":false}", 0, &r)) {
		emmar_liberar(&r);
	}
	return ok;
}

static const struct {
	const char *id, *nome, *url, *modelos;
} presets[] = {
	{ "ollama", "Ollama Cloud", "https://ollama.com/v1", "gemma4:31b-cloud, gpt-oss:120b, kimi-k2.6, deepseek-v4.1-flash" },
	{ "openai", "OpenAI", "https://api.openai.com/v1", "gpt-5.4-mini, gpt-5.5" },
	{ "gemini", "Gemini", "https://generativelanguage.googleapis.com/v1beta/openai/", "gemini-3-flash, gemini-3.1-pro" },
	{ "claude", "Claude", "https://api.anthropic.com/v1/", "claude-sonnet-4-6, claude-haiku-4-5" },
	{ "custom", "Endpoint customizado", "http://localhost:11434/v1", "" },
};

int setup_emma_presets(const char **ids, const char **nomes, const char **urls, const char **modelos, int max) {
	int n = (int)(sizeof(presets) / sizeof(presets[0]));
	for (int i = 0; i < n && i < max; i++) {
		ids[i] = presets[i].id;
		nomes[i] = presets[i].nome;
		urls[i] = presets[i].url;
		modelos[i] = presets[i].modelos;
	}
	return n < max ? n : max;
}

bool setup_emma_provedor(const char *preset, const char *nome, const char *url, const char *chave, const char *modelo, char *erro, size_t tam) {
	if (!url || !*url || !g_str_has_prefix(url, "http")) {
		erro_de(erro, tam, "Falta o endereço da API (começa com http).");
		return false;
	}
	char *a = g_build_filename(g_get_user_config_dir(), "lumna", "emma", "provedores.json", NULL), *dir = g_path_get_dirname(a), *velho = NULL;
	if (g_file_get_contents(a, &velho, NULL, NULL)) {
		struct json *j = json_ler(velho);
		const struct json *ps = json_chave(j, "provedores");
		bool ja = ps && ps->nfilhos > 0;
		json_liberar(j);
		g_free(velho);
		if (ja) { /* já existe configuração (do Painel): o assistente não passa por cima */
			erro_de(erro, tam, "A Emma já tem um provedor; mude em Painel > Emma > Provedores.");
			g_free(dir);
			g_free(a);
			return false;
		}
	}
	GString *s = g_string_new("{\"provedores\":[{\"lid\":\"p1\",\"hid\":\"\",\"nome\":");
	json_escrever_texto(s, nome && *nome ? nome : "Provedor");
	g_string_append(s, ",\"preset\":");
	json_escrever_texto(s, preset && *preset ? preset : "custom");
	g_string_append(s, ",\"url\":");
	json_escrever_texto(s, url);
	g_string_append(s, ",\"modelos\":");
	json_escrever_texto(s, modelo ? modelo : "");
	g_string_append(s, ",\"chave\":");
	char *limpa = g_strdup(chave ? chave : "");
	g_strstrip(limpa);
	json_escrever_texto(s, limpa);
	g_string_append_printf(s, ",\"mascara\":\"\",\"tem_chave\":%s,\"sinc\":false}],\"rotas\":[", *limpa ? "true" : "false");
	/* o primeiro modelo da lista atende as quatro tarefas (a "visão" é opcional) */
	char **ms = g_strsplit(modelo ? modelo : "", ",", -1);
	const char *primeiro = ms[0] ? g_strstrip(ms[0]) : "";
	const char *tarefas[] = { "chat", "planner", "rag", "coder" };
	for (int i = 0; i < 4; i++) {
		g_string_append_printf(s, "%s{\"chave\":\"%s\",\"lid\":\"p1\",\"modelo\":", i ? "," : "", tarefas[i]);
		json_escrever_texto(s, primeiro);
		g_string_append_c(s, '}');
	}
	g_string_append(s, "],\"rotas_sinc\":false,\"apagar\":[],\"proximo\":2}");
	g_strfreev(ms);
	g_free(limpa);
	g_mkdir_with_parents(dir, 0700);
	char *tmp = g_strconcat(a, ".tmp", NULL);
	bool ok = g_file_set_contents(tmp, s->str, -1, NULL);
	if (ok) {
		g_chmod(tmp, 0600); /* tem a chave de API */
		ok = g_rename(tmp, a) == 0;
	}
	if (!ok) {
		erro_de(erro, tam, "Não consegui gravar o provedor da Emma.");
	}
	g_free(tmp);
	g_string_free(s, TRUE);
	g_free(dir);
	g_free(a);
	return ok;
}
