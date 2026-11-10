#define _GNU_SOURCE
#include "loja.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "json.h"

struct lj_catalogo {
	GArray *v;                /* lj_pacote dos repositórios */
	GHashTable *por_nome;     /* nome → índice + 1 */
	GArray *aur;              /* lj_pacote do último lj_aur_buscar */
	GHashTable *instalados;   /* nome → versão (g_strdup) */
};

struct lj_instalados {
	GHashTable *versoes;      /* nome → versão */
	GHashTable *explicitos;   /* nome → 1 */
	GHashTable *de_fora;      /* nome → versão (pacman -Qm: o AUR e o compilado à mão) */
};

/* ---------------- a tabela dos apps conhecidos ---------------- */

static const struct { const char *pkg, *nome; enum lj_cat cat; } CONHECIDOS[] = {
	{ "firefox", "Firefox", LJ_INTERNET }, { "chromium", "Chromium", LJ_INTERNET }, { "thunderbird", "Thunderbird", LJ_INTERNET }, { "discord", "Discord", LJ_INTERNET },
	{ "telegram-desktop", "Telegram", LJ_INTERNET }, { "qbittorrent", "qBittorrent", LJ_INTERNET }, { "epiphany", "Navegador Web (Epiphany)", LJ_INTERNET },
	{ "blender", "Blender", LJ_GRAFICOS }, { "krita", "Krita", LJ_GRAFICOS }, { "gimp", "GIMP", LJ_GRAFICOS }, { "inkscape", "Inkscape", LJ_GRAFICOS },
	{ "darktable", "darktable", LJ_GRAFICOS }, { "godot", "Godot", LJ_DESENVOLVIMENTO }, { "freecad", "FreeCAD", LJ_GRAFICOS },
	{ "vlc", "VLC", LJ_MULTIMIDIA }, { "mpv", "mpv", LJ_MULTIMIDIA }, { "obs-studio", "OBS Studio", LJ_MULTIMIDIA }, { "kdenlive", "Kdenlive", LJ_MULTIMIDIA },
	{ "audacity", "Audacity", LJ_MULTIMIDIA }, { "handbrake", "HandBrake", LJ_MULTIMIDIA }, { "shotcut", "Shotcut", LJ_MULTIMIDIA }, { "ardour", "Ardour", LJ_MULTIMIDIA },
	{ "steam", "Steam", LJ_JOGOS }, { "lutris", "Lutris", LJ_JOGOS }, { "retroarch", "RetroArch", LJ_JOGOS }, { "supertuxkart", "SuperTuxKart", LJ_JOGOS },
	{ "code", "Code - OSS", LJ_DESENVOLVIMENTO }, { "neovim", "Neovim", LJ_DESENVOLVIMENTO }, { "vim", "Vim", LJ_DESENVOLVIMENTO }, { "emacs", "Emacs", LJ_DESENVOLVIMENTO },
	{ "git", "Git", LJ_DESENVOLVIMENTO }, { "docker", "Docker", LJ_DESENVOLVIMENTO }, { "kate", "Kate", LJ_DESENVOLVIMENTO },
	{ "libreoffice-fresh", "LibreOffice", LJ_SISTEMA }, { "keepassxc", "KeePassXC", LJ_SISTEMA }, { "gparted", "GParted", LJ_SISTEMA }, { "btop", "btop", LJ_SISTEMA },
	{ "htop", "htop", LJ_SISTEMA }, { "timeshift", "Timeshift", LJ_SISTEMA }, { "flatpak", "Flatpak", LJ_SISTEMA },
};

static const char *const NOMES_CAT[LJ_NCATS] = { "Todos", "Desenvolvimento", "Gráficos", "Internet", "Multimídia", "Jogos", "Sistema" };
static const char *const NOMES_TIPO[] = { "Aplicativo", "Utilitário", "Biblioteca", "Driver", "Kernel" };

const char *lj_nome_cat(enum lj_cat c) { return c >= 0 && c < LJ_NCATS ? NOMES_CAT[c] : ""; }
const char *lj_nome_tipo(enum lj_tipo t) { return t >= LJ_APP && t <= LJ_KERNEL ? NOMES_TIPO[t] : ""; }

static bool tem(const char *s, const char *p) { return strcasestr(s, p) != NULL; }

static bool eh_kernel(const char *n) {
	if (!g_str_has_prefix(n, "linux")) {
		return false;
	}
	const char *r = n + 5;
	if (!*r) {
		return true;
	}
	static const char *const sufixos[] = { "-lts", "-zen", "-hardened", "-rt", "-rt-lts", "-cachyos", "-xanmod", "-mainline", "-tkg", NULL };
	for (int i = 0; sufixos[i]; i++) {
		if (!strcmp(r, sufixos[i])) {
			return true;
		}
	}
	return false;
}

static void classificar(lj_pacote *p) {
	const char *n = p->nome;
	p->cat = LJ_TODAS;
	p->destaque = false;
	g_strlcpy(p->exibicao, n, sizeof(p->exibicao));
	for (size_t i = 0; i < G_N_ELEMENTS(CONHECIDOS); i++) {
		if (!strcmp(CONHECIDOS[i].pkg, n)) {
			g_strlcpy(p->exibicao, CONHECIDOS[i].nome, sizeof(p->exibicao));
			p->cat = CONHECIDOS[i].cat;
			p->destaque = true;
			p->tipo = LJ_APP;
			return;
		}
	}
	if (eh_kernel(n)) {
		p->tipo = LJ_KERNEL;
		p->cat = LJ_SISTEMA;
		return;
	}
	if (g_str_has_prefix(n, "nvidia") || g_str_has_prefix(n, "xf86-video-") || g_str_has_prefix(n, "xf86-input-") || g_str_has_prefix(n, "vulkan-") || g_str_has_suffix(n, "-driver")
		|| g_str_has_suffix(n, "-firmware") || g_str_has_prefix(n, "linux-firmware") || g_str_has_prefix(n, "lib32-vulkan-") || g_str_has_prefix(n, "lib32-nvidia")) {
		p->tipo = LJ_DRIVER;
		p->cat = LJ_SISTEMA;
		return;
	}
	if (!strcmp(n, "mesa") || g_str_has_prefix(n, "lib") || g_str_has_suffix(n, "-libs") || g_str_has_suffix(n, "-headers") || g_str_has_prefix(n, "python-") || g_str_has_prefix(n, "perl-")
		|| g_str_has_prefix(n, "ruby-") || g_str_has_prefix(n, "haskell-") || g_str_has_prefix(n, "lua-") || g_str_has_prefix(n, "qt5-") || g_str_has_prefix(n, "qt6-")
		|| g_str_has_prefix(n, "gst-plugin") || g_str_has_prefix(n, "nodejs-") || g_str_has_prefix(n, "php-")) {
		p->tipo = LJ_BIBLIOTECA;
		p->cat = LJ_SISTEMA;
		return;
	}
	if (g_str_has_suffix(n, "-utils") || g_str_has_suffix(n, "-tools") || g_str_has_suffix(n, "-cli") || g_str_has_suffix(n, "-utilities")) {
		p->tipo = LJ_UTILITARIO;
	} else {
		p->tipo = LJ_APP;
	}
	const char *d = p->descricao;
	if (tem(d, "game") || tem(d, "emulator")) {
		p->cat = LJ_JOGOS;
	} else if (tem(d, "video") || tem(d, "audio") || tem(d, "music") || tem(d, "media") || tem(d, "codec") || tem(d, "player")) {
		p->cat = LJ_MULTIMIDIA;
	} else if (tem(d, "image") || tem(d, "graphic") || tem(d, "drawing") || tem(d, "photo") || tem(d, "3d ") || tem(d, "paint")) {
		p->cat = LJ_GRAFICOS;
	} else if (tem(d, "browser") || tem(d, "web ") || tem(d, "mail") || tem(d, "chat") || tem(d, "torrent") || tem(d, "http") || tem(d, "ftp") || tem(d, "download")) {
		p->cat = LJ_INTERNET;
	} else if (tem(d, "compiler") || tem(d, "debugger") || tem(d, "editor") || tem(d, "programming") || tem(d, "language") || tem(d, "version control") || tem(d, "build")) {
		p->cat = LJ_DESENVOLVIMENTO;
	} else {
		p->cat = LJ_SISTEMA;
	}
}

/* ---------------- ler o pacman ---------------- */

static char *rodar_saida(char **argv, bool lc_c) {
	char *saida = NULL;
	char **env = g_get_environ();
	if (lc_c) {
		env = g_environ_setenv(env, "LC_ALL", "C", TRUE);
	}
	bool ok = g_spawn_sync(NULL, argv, env, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &saida, NULL, NULL, NULL);
	g_strfreev(env);
	if (!ok) {
		g_free(saida);
		return NULL;
	}
	return saida;
}

static int64_t tamanho_de(const char *s) {
	char *fim;
	double v = g_ascii_strtod(s, &fim);
	while (*fim == ' ') {
		fim++;
	}
	if (g_str_has_prefix(fim, "GiB")) {
		v *= 1024.0 * 1024 * 1024;
	} else if (g_str_has_prefix(fim, "MiB")) {
		v *= 1024.0 * 1024;
	} else if (g_str_has_prefix(fim, "KiB")) {
		v *= 1024.0;
	}
	return (int64_t)v;
}

static void tira_none(char *s) {
	if (!strcmp(s, "None")) {
		s[0] = '\0';
	}
}

static void indexar(lj_catalogo *c) {
	g_hash_table_remove_all(c->por_nome);
	for (guint i = 0; i < c->v->len; i++) {
		g_hash_table_insert(c->por_nome, g_strdup(g_array_index(c->v, lj_pacote, i).nome), GINT_TO_POINTER((int)i + 1));
	}
}

static GHashTable *tabela_de(char **argv, bool com_versao) {
	GHashTable *t = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	char *q = rodar_saida(argv, true);
	if (!q) {
		return t;
	}
	char **ls = g_strsplit(q, "\n", -1);
	for (char **l = ls; *l; l++) {
		if (!**l) {
			continue;
		}
		char *sp = strchr(*l, ' ');
		if (sp) {
			*sp = '\0';
		}
		g_hash_table_replace(t, g_strdup(*l), g_strdup(com_versao && sp ? sp + 1 : "1"));
	}
	g_strfreev(ls);
	g_free(q);
	return t;
}

lj_instalados *lj_instalados_ler(void) {
	lj_instalados *i = g_new0(lj_instalados, 1);
	char *a_q[] = { "pacman", "-Q", NULL }, *a_e[] = { "pacman", "-Qe", NULL }, *a_m[] = { "pacman", "-Qm", NULL };
	i->versoes = tabela_de(a_q, true);
	i->explicitos = tabela_de(a_e, false);
	i->de_fora = tabela_de(a_m, true);
	return i;
}

void lj_instalados_usar(lj_catalogo *c, lj_instalados *ins) {
	if (!ins) {
		return;
	}
	g_hash_table_destroy(c->instalados);
	c->instalados = ins->versoes;
	for (guint k = 0; k < c->v->len; k++) {
		lj_pacote *x = &g_array_index(c->v, lj_pacote, k);
		const char *vi = g_hash_table_lookup(c->instalados, x->nome);
		x->instalado = vi != NULL;
		g_strlcpy(x->instalada, vi ? vi : "", sizeof(x->instalada));
		x->explicito = g_hash_table_contains(ins->explicitos, x->nome);
		if (!x->instalado) {
			x->atualizavel = false;
		}
	}
	/* o AUR: some o que foi desinstalado, entra o que veio de fora */
	for (int k = (int)c->aur->len - 1; k >= 0; k--) {
		lj_pacote *x = &g_array_index(c->aur, lj_pacote, k);
		const char *vi = g_hash_table_lookup(c->instalados, x->nome);
		x->instalado = vi != NULL;
		g_strlcpy(x->instalada, vi ? vi : "", sizeof(x->instalada));
		x->explicito = g_hash_table_contains(ins->explicitos, x->nome);
		x->externo |= x->instalado;
		if (x->externo && !x->instalado) {
			g_array_remove_index(c->aur, (guint)k);
		}
	}
	GHashTableIter it;
	gpointer nome, ver;
	g_hash_table_iter_init(&it, ins->de_fora);
	while (g_hash_table_iter_next(&it, &nome, &ver)) {
		if (g_hash_table_contains(c->por_nome, nome) || lj_achar(c, nome)) {
			continue;
		}
		lj_pacote x;
		memset(&x, 0, sizeof(x));
		g_strlcpy(x.nome, nome, sizeof(x.nome));
		g_strlcpy(x.exibicao, nome, sizeof(x.exibicao));
		g_strlcpy(x.versao, ver, sizeof(x.versao));
		g_strlcpy(x.descricao, "Instalado fora dos repositórios (do AUR ou compilado à mão).", sizeof(x.descricao));
		g_strlcpy(x.repo, "aur", sizeof(x.repo));
		x.instalado = true;
		g_strlcpy(x.instalada, ver, sizeof(x.instalada));
		x.externo = true;
		x.explicito = g_hash_table_contains(ins->explicitos, nome);
		g_array_append_val(c->aur, x);
	}
	g_hash_table_destroy(ins->explicitos);
	g_hash_table_destroy(ins->de_fora);
	g_free(ins);
}

lj_catalogo *lj_catalogo_ler(void) {
	char *argv_si[] = { "pacman", "-Si", NULL };
	char *si = rodar_saida(argv_si, true);
	if (!si) {
		return NULL;
	}
	lj_catalogo *c = g_new0(lj_catalogo, 1);
	c->v = g_array_new(FALSE, TRUE, sizeof(lj_pacote));
	c->aur = g_array_new(FALSE, TRUE, sizeof(lj_pacote));
	c->por_nome = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	c->instalados = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	lj_pacote p;
	memset(&p, 0, sizeof(p));
	bool aberto = false;
	char **linhas = g_strsplit(si, "\n", -1);
	for (char **l = linhas;; l++) {
		bool fim = !*l;
		if (fim || !**l) {
			if (aberto && p.nome[0]) {
				classificar(&p);
				g_array_append_val(c->v, p);
			}
			memset(&p, 0, sizeof(p));
			aberto = false;
			if (fim) {
				break;
			}
			continue;
		}
		char *dp = strstr(*l, " : ");
		if (!dp || **l == ' ') {
			continue;   /* continuação de uma lista */
		}
		char chave[48];
		g_strlcpy(chave, *l, MIN((size_t)(dp - *l) + 1, sizeof(chave)));
		g_strchomp(chave);
		const char *v = dp + 3;
		aberto = true;
		if (!strcmp(chave, "Repository")) {
			g_strlcpy(p.repo, v, sizeof(p.repo));
		} else if (!strcmp(chave, "Name")) {
			g_strlcpy(p.nome, v, sizeof(p.nome));
		} else if (!strcmp(chave, "Version")) {
			g_strlcpy(p.versao, v, sizeof(p.versao));
		} else if (!strcmp(chave, "Description")) {
			g_strlcpy(p.descricao, v, sizeof(p.descricao));
		} else if (!strcmp(chave, "URL")) {
			g_strlcpy(p.url, v, sizeof(p.url));
			tira_none(p.url);
		} else if (!strcmp(chave, "Licenses")) {
			g_strlcpy(p.licenca, v, sizeof(p.licenca));
			tira_none(p.licenca);
		} else if (!strcmp(chave, "Groups")) {
			g_strlcpy(p.grupos, v, sizeof(p.grupos));
			tira_none(p.grupos);
		} else if (!strcmp(chave, "Download Size")) {
			p.baixar = tamanho_de(v);
		} else if (!strcmp(chave, "Installed Size")) {
			p.tamanho = tamanho_de(v);
		}
	}
	g_strfreev(linhas);
	g_free(si);
	indexar(c);
	lj_instalados *ins = lj_instalados_ler();
	lj_instalados_usar(c, ins);
	return c;
}

void lj_catalogo_liberar(lj_catalogo *c) {
	if (!c) {
		return;
	}
	g_array_free(c->v, TRUE);
	g_array_free(c->aur, TRUE);
	g_hash_table_destroy(c->por_nome);
	g_hash_table_destroy(c->instalados);
	g_free(c);
}

int lj_n(const lj_catalogo *c) { return (int)c->v->len; }
const lj_pacote *lj_pacote_n(const lj_catalogo *c, int i) { return i >= 0 && i < (int)c->v->len ? &g_array_index(c->v, lj_pacote, i) : NULL; }

const lj_pacote *lj_achar(const lj_catalogo *c, const char *nome) {
	gpointer i = g_hash_table_lookup(c->por_nome, nome);
	if (i) {
		return &g_array_index(c->v, lj_pacote, GPOINTER_TO_INT(i) - 1);
	}
	for (guint k = 0; k < c->aur->len; k++) {
		if (!strcmp(g_array_index(c->aur, lj_pacote, k).nome, nome)) {
			return &g_array_index(c->aur, lj_pacote, k);
		}
	}
	return NULL;
}

/* ---------------- atualizações ---------------- */

static int marcar_linhas(lj_catalogo *c, const char *texto) {
	int n = 0;
	char **ls = g_strsplit(texto, "\n", -1);
	for (char **l = ls; *l; l++) {
		char nome[96], velha[48], seta[8], nova[48];
		if (sscanf(*l, "%95s %47s %7s %47s", nome, velha, seta, nova) == 4 && !strcmp(seta, "->")) {
			lj_pacote *p = (lj_pacote *)lj_achar(c, nome);
			if (p) {
				p->atualizavel = true;
				g_strlcpy(p->nova_versao, nova, sizeof(p->nova_versao));
				n++;
			}
		}
	}
	g_strfreev(ls);
	return n;
}

int lj_contar_atualizaveis(const lj_catalogo *c) {
	int n = 0;
	for (guint i = 0; i < c->v->len; i++) {
		n += g_array_index(c->v, lj_pacote, i).atualizavel;
	}
	for (guint i = 0; i < c->aur->len; i++) {
		n += g_array_index(c->aur, lj_pacote, i).atualizavel;
	}
	return n;
}

const char *lj_versao_instalada(const lj_catalogo *c, const char *nome) {
	return g_hash_table_lookup(c->instalados, nome);
}

void lj_marcar_atualizado(lj_catalogo *c, const char *nome) {
	for (int a = 0; a < 2; a++) {
		GArray *v = a ? c->aur : c->v;
		for (guint i = 0; i < v->len; i++) {
			lj_pacote *p = &g_array_index(v, lj_pacote, i);
			if (!nome || !strcmp(p->nome, nome)) {
				p->atualizavel = false;
				p->nova_versao[0] = '\0';
			}
		}
	}
}

int lj_marcar_atualizacoes(lj_catalogo *c, const char *helper) {
	char *ru = g_find_program_in_path("checkupdates");
	char *argv1[] = { ru ? "checkupdates" : "pacman", "-Qu", NULL };
	char *argv0[] = { "checkupdates", NULL };
	char *t = rodar_saida(ru ? argv0 : argv1, true);
	g_free(ru);
	int n = 0;
	if (t) {
		n += marcar_linhas(c, t);
		g_free(t);
	}
	if (helper && *helper) {
		char *argv_a[] = { (char *)helper, "-Qua", NULL };
		char *a = rodar_saida(argv_a, true);
		if (a) {
			/* o AUR pode ter o pacote só como "foreign": entra no catálogo se ainda não está */
			char **ls = g_strsplit(a, "\n", -1);
			for (char **l = ls; *l; l++) {
				char nome[96], velha[48], seta[8], nova[48];
				if (sscanf(*l, "%95s %47s %7s %47s", nome, velha, seta, nova) == 4 && !strcmp(seta, "->") && !lj_achar(c, nome)) {
					lj_pacote x;
					memset(&x, 0, sizeof(x));
					g_strlcpy(x.nome, nome, sizeof(x.nome));
					g_strlcpy(x.exibicao, nome, sizeof(x.exibicao));
					g_strlcpy(x.versao, velha, sizeof(x.versao));
					g_strlcpy(x.repo, "aur", sizeof(x.repo));
					x.instalado = true;
					x.externo = true;
					x.atualizavel = true;
					g_strlcpy(x.nova_versao, nova, sizeof(x.nova_versao));
					g_array_append_val(c->aur, x);
				}
			}
			g_strfreev(ls);
			n += marcar_linhas(c, a);
			g_free(a);
		}
	}
	return n;
}

/* ---------------- buscar e ordenar ---------------- */

void lj_filtro_padrao(lj_filtro *f) {
	memset(f, 0, sizeof(*f));
	f->tipo = -1;
	f->core = f->extra = f->multilib = f->aur = true;
	f->sem_avaliacao = true;
	f->por = LJ_POR_AMBOS;
	f->ordem = LJ_RELEVANCIA;
}

const lj_pacote *lj_resultado(const lj_catalogo *c, int codigo) {
	if (codigo >= 0) {
		return lj_pacote_n(c, codigo);
	}
	int i = -(codigo + 1);
	return i < (int)c->aur->len ? &g_array_index(c->aur, lj_pacote, i) : NULL;
}

static int pontos(const lj_pacote *p, const char *t, enum lj_buscar_por por) {
	if (!t || !*t) {
		return 1;
	}
	int s = 0;
	if (por != LJ_POR_DESCRICAO) {
		if (!g_ascii_strcasecmp(p->nome, t) || !g_ascii_strcasecmp(p->exibicao, t)) {
			s = 1000;
		} else if (g_ascii_strncasecmp(p->nome, t, strlen(t)) == 0 || g_ascii_strncasecmp(p->exibicao, t, strlen(t)) == 0) {
			s = 600;
		} else if (tem(p->nome, t) || tem(p->exibicao, t)) {
			s = 400;
		}
	}
	if (!s && por != LJ_POR_NOME && tem(p->descricao, t)) {
		s = 100;
	}
	return s;
}

static bool repo_vale(const lj_pacote *p, const lj_filtro *f) {
	if (!strcmp(p->repo, "core")) {
		return f->core;
	}
	if (!strcmp(p->repo, "extra")) {
		return f->extra;
	}
	if (!strcmp(p->repo, "multilib")) {
		return f->multilib;
	}
	if (!strcmp(p->repo, "aur")) {
		return f->aur;
	}
	return true;
}

typedef struct {
	const lj_catalogo *c;
	enum lj_ordem ordem;
	const char *texto;
	enum lj_buscar_por por;
} ctx_ord;

static int comparar(gconstpointer a, gconstpointer b, gpointer d) {
	const ctx_ord *x = d;
	const lj_pacote *pa = lj_resultado(x->c, *(const int *)a), *pb = lj_resultado(x->c, *(const int *)b);
	int r = 0;
	switch (x->ordem) {
	case LJ_RELEVANCIA: {
		int sa = pontos(pa, x->texto, x->por), sb = pontos(pb, x->texto, x->por);
		r = sb - sa;
		if (!r && pa->destaque != pb->destaque) {
			r = pb->destaque - pa->destaque;
		}
		if (!r && (pa->popularidade != pb->popularidade)) {
			r = pb->popularidade > pa->popularidade ? 1 : -1;
		}
		break;
	}
	case LJ_TAMANHO:
		r = pb->tamanho > pa->tamanho ? 1 : pb->tamanho < pa->tamanho ? -1 : 0;
		break;
	case LJ_NOTA:
		r = pb->nota > pa->nota ? 1 : pb->nota < pa->nota ? -1 : 0;
		break;
	default:
		break;
	}
	return r ? r : g_ascii_strcasecmp(pa->nome, pb->nome);
}

int lj_buscar(const lj_catalogo *c, const lj_filtro *f, int *saida, int max) {
	GArray *r = g_array_new(FALSE, FALSE, sizeof(int));
	int total = (int)c->v->len + (int)c->aur->len;
	for (int k = 0; k < total; k++) {
		int codigo = k < (int)c->v->len ? k : -(1 + (k - (int)c->v->len));
		const lj_pacote *p = lj_resultado(c, codigo);
		if (!p || !repo_vale(p, f) || pontos(p, f->texto, f->por) == 0) {
			continue;
		}
		if (f->tipo >= 0 && (int)p->tipo != f->tipo) {
			continue;
		}
		if (f->cat != LJ_TODAS && (int)p->cat != f->cat) {
			continue;
		}
		if (f->so_instalados && !p->instalado) {
			continue;
		}
		if (f->so_atualizaveis && !p->atualizavel) {
			continue;
		}
		if (p->avaliacoes == 0 ? !f->sem_avaliacao : p->nota < f->nota_min) {
			continue;
		}
		g_array_append_val(r, codigo);
	}
	ctx_ord x = { c, f->ordem, f->texto, f->por };
	g_array_sort_with_data(r, comparar, &x);
	int n = (int)r->len;
	for (int i = 0; i < n && i < max; i++) {
		saida[i] = g_array_index(r, int, i);
	}
	g_array_free(r, TRUE);
	return n;
}

int lj_destaques(const lj_catalogo *c, enum lj_cat cat, enum lj_ordem ordem, int *saida, int max) {
	lj_filtro f;
	lj_filtro_padrao(&f);
	f.aur = false;
	f.cat = cat;
	f.ordem = ordem;
	int *todos = g_new(int, lj_n(c) + 1);
	int n = lj_buscar(c, &f, todos, lj_n(c));
	int k = 0;
	for (int i = 0; i < n && k < max; i++) {
		if (lj_pacote_n(c, todos[i])->destaque) {
			saida[k++] = todos[i];
		}
	}
	g_free(todos);
	return k;
}

/* ---------------- o AUR ---------------- */

const char *lj_aur_helper(const char *preferido) {
	static const char *ordem[] = { "paru", "yay" };
	if (preferido && *preferido) {
		for (int i = 0; i < 2; i++) {
			if (!strcmp(preferido, ordem[i])) {
				char *p = g_find_program_in_path(ordem[i]);
				if (p) {
					g_free(p);
					return ordem[i];
				}
			}
		}
	}
	for (int i = 0; i < 2; i++) {
		char *p = g_find_program_in_path(ordem[i]);
		if (p) {
			g_free(p);
			return ordem[i];
		}
	}
	return NULL;
}

void lj_aur_limpar(lj_catalogo *c) {
	for (int i = (int)c->aur->len - 1; i >= 0; i--) {
		const lj_pacote *x = &g_array_index(c->aur, lj_pacote, i);
		if (!x->externo && !x->instalado) {   /* o que está instalado fica: Instalados e Atualizações precisam dele */
			g_array_remove_index(c->aur, (guint)i);
		}
	}
}

GArray *lj_aur_consultar(const lj_catalogo *c, const char *termo, char *erro, size_t tam) {
	erro[0] = '\0';
	if (!termo || strlen(termo) < 2) {
		g_strlcpy(erro, "digite pelo menos duas letras", tam);
		return NULL;
	}
	const char *base = g_getenv("LUMNA_AUR_URL");
	char *esc = g_uri_escape_string(termo, NULL, FALSE);
	char *url = g_strdup_printf("%s/rpc/v5/search/%s?by=name-desc", base && *base ? base : "https://aur.archlinux.org", esc);
	g_free(esc);
	char *argv[] = { "curl", "-fsS", "--max-time", "12", url, NULL };
	char *saida = NULL, *err = NULL;
	int estado = 0;
	GError *ge = NULL;
	bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &saida, &err, &estado, &ge);
	g_free(url);
	if (!ok || !WIFEXITED(estado) || WEXITSTATUS(estado) != 0 || !saida) {
		g_strlcpy(erro, "o AUR não respondeu (sem internet?)", tam);
		g_free(saida);
		g_free(err);
		g_clear_error(&ge);
		return NULL;
	}
	g_free(err);
	struct json *j = json_ler(saida);
	g_free(saida);
	const struct json *res = j ? json_chave(j, "results") : NULL;
	if (!res) {
		json_liberar(j);
		g_strlcpy(erro, "resposta do AUR ilegível", tam);
		return NULL;
	}
	GArray *r = g_array_new(FALSE, TRUE, sizeof(lj_pacote));
	for (int i = 0; i < res->nfilhos && r->len < 60; i++) {
		const struct json *o = json_item(res, i);
		const char *nome = json_chave_texto(o, "Name");
		if (!*nome || g_hash_table_contains(c->por_nome, nome)) {
			continue;   /* o repositório oficial vale mais que o AUR */
		}
		lj_pacote p;
		memset(&p, 0, sizeof(p));
		g_strlcpy(p.nome, nome, sizeof(p.nome));
		g_strlcpy(p.exibicao, nome, sizeof(p.exibicao));
		g_strlcpy(p.versao, json_chave_texto(o, "Version"), sizeof(p.versao));
		g_strlcpy(p.descricao, json_chave_texto(o, "Description"), sizeof(p.descricao));
		g_strlcpy(p.url, json_chave_texto(o, "URL"), sizeof(p.url));
		g_strlcpy(p.mantenedor, json_chave_texto(o, "Maintainer"), sizeof(p.mantenedor));
		g_strlcpy(p.repo, "aur", sizeof(p.repo));
		p.votos = (int)json_numero(json_chave(o, "NumVotes"), 0);
		p.popularidade = json_numero(json_chave(o, "Popularity"), 0);
		p.fora_de_data = json_numero(json_chave(o, "OutOfDate"), 0) > 0;
		classificar(&p);
		p.destaque = false;
		p.instalado = g_hash_table_contains(c->instalados, nome);
		g_array_append_val(r, p);
	}
	json_liberar(j);
	return r;
}

void lj_aur_usar(lj_catalogo *c, GArray *achados) {
	lj_aur_limpar(c);
	if (!achados) {
		return;
	}
	for (guint i = 0; i < achados->len; i++) {
		lj_pacote *p = &g_array_index(achados, lj_pacote, i);
		if (!lj_achar(c, p->nome)) {
			g_array_append_val(c->aur, *p);
		}
	}
	g_array_free(achados, TRUE);
}

int lj_aur_buscar(lj_catalogo *c, const char *termo, char *erro, size_t tam) {
	GArray *r = lj_aur_consultar(c, termo, erro, tam);
	if (!r) {
		lj_aur_limpar(c);
		return -1;
	}
	lj_aur_usar(c, r);
	return (int)c->aur->len;
}

/* ---------------- ações ---------------- */

static int rodar_fluxo(char **argv, lj_linha_fn linha, void *dados, char *erro, size_t tam) {
	/* um sh que junta a saída de erro na normal: uma fila só de linhas */
	GPtrArray *a = g_ptr_array_new();
	g_ptr_array_add(a, "sh");
	g_ptr_array_add(a, "-c");
	g_ptr_array_add(a, "exec \"$@\" 2>&1");
	g_ptr_array_add(a, "sh");
	for (char **p = argv; *p; p++) {
		g_ptr_array_add(a, *p);
	}
	g_ptr_array_add(a, NULL);
	GPid pid;
	int fd = -1;
	GError *ge = NULL;
	if (!g_spawn_async_with_pipes(NULL, (char **)a->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL, &fd, NULL, &ge)) {
		g_strlcpy(erro, ge ? ge->message : "não consegui rodar", tam);
		g_clear_error(&ge);
		g_ptr_array_free(a, TRUE);
		return -1;
	}
	g_ptr_array_free(a, TRUE);
	char ultimas[3][400] = { "", "", "" };
	int nu = 0;
	FILE *f = fdopen(fd, "r");
	char buf[2048];
	while (f && fgets(buf, sizeof(buf), f)) {
		g_strchomp(buf);
		if (!*buf) {
			continue;
		}
		g_strlcpy(ultimas[nu % 3], buf, sizeof(ultimas[0]));
		nu++;
		if (linha) {
			linha(dados, buf);
		}
	}
	if (f) {
		fclose(f);
	}
	int estado = 0;
	waitpid(pid, &estado, 0);
	g_spawn_close_pid(pid);
	int codigo = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
	if (codigo != 0) {
		erro[0] = '\0';
		for (int i = MAX(0, nu - 3); i < nu; i++) {
			g_strlcat(erro, ultimas[i % 3], tam);
			g_strlcat(erro, "\n", tam);
		}
		g_strstrip(erro);
		if (codigo == 126 || codigo == 127) {
			g_strlcpy(erro, "a senha foi recusada ou o programa não existe", tam);
		}
	}
	return codigo;
}

int lj_executar(enum lj_acao acao, const lj_pacote *p, const char *helper, lj_linha_fn linha, void *dados, char *erro, size_t tam) {
	erro[0] = '\0';
	const char *sem = g_getenv("LUMNA_SEM_PKEXEC");
	bool direto = sem && *sem;
	bool aur = p && !strcmp(p->repo, "aur");
	if (aur && !helper) {
		g_strlcpy(erro, "para instalar do AUR é preciso ter o paru ou o yay", tam);
		return -1;
	}
	if (acao == LJ_INSTALAR && p) {
		if (aur) {
			char *argv[] = { (char *)helper, "-S", "--noconfirm", "--needed", "--sudo", "pkexec", !strcmp(helper, "paru") ? "--skipreview" : "--answerdiff=None", (char *)p->nome, NULL };
			return rodar_fluxo(argv, linha, dados, erro, tam);
		}
		char *pk[] = { "pkexec", "pacman", "-S", "--noconfirm", "--needed", (char *)p->nome, NULL };
		char *sm[] = { "pacman", "-S", "--noconfirm", "--needed", (char *)p->nome, NULL };
		return rodar_fluxo(direto ? sm : pk, linha, dados, erro, tam);
	}
	if (acao == LJ_REMOVER && p) {
		char *pk[] = { "pkexec", "pacman", "-Rns", "--noconfirm", (char *)p->nome, NULL };
		char *sm[] = { "pacman", "-Rns", "--noconfirm", (char *)p->nome, NULL };
		return rodar_fluxo(direto ? sm : pk, linha, dados, erro, tam);
	}
	if (acao == LJ_ATUALIZAR_TUDO) {
		char *pk[] = { "pkexec", "pacman", "-Syu", "--noconfirm", NULL };
		char *sm[] = { "pacman", "-Syu", "--noconfirm", NULL };
		int r = rodar_fluxo(direto ? sm : pk, linha, dados, erro, tam);
		if (r == 0 && helper) {
			char *argv[] = { (char *)helper, "-Sua", "--noconfirm", "--sudo", "pkexec", NULL };
			r = rodar_fluxo(argv, linha, dados, erro, tam);
		}
		return r;
	}
	g_strlcpy(erro, "ação sem pacote", tam);
	return -1;
}

int lj_executar_nomes(enum lj_acao acao, const char *const *nomes, int n, lj_linha_fn linha, void *dados, char *erro, size_t tam) {
	erro[0] = '\0';
	if ((acao != LJ_INSTALAR && acao != LJ_REMOVER) || n <= 0 || n > 8) {
		g_strlcpy(erro, "ação sem pacotes", tam);
		return -1;
	}
	const char *sem = g_getenv("LUMNA_SEM_PKEXEC");
	GPtrArray *a = g_ptr_array_new();
	if (!(sem && *sem)) {
		g_ptr_array_add(a, "pkexec");
	}
	g_ptr_array_add(a, "pacman");
	if (acao == LJ_INSTALAR) {
		g_ptr_array_add(a, "-S");
		g_ptr_array_add(a, "--noconfirm");
		g_ptr_array_add(a, "--needed");
	} else {
		g_ptr_array_add(a, "-Rns");
		g_ptr_array_add(a, "--noconfirm");
	}
	for (int i = 0; i < n; i++) {
		g_ptr_array_add(a, (gpointer)nomes[i]);
	}
	g_ptr_array_add(a, NULL);
	int r = rodar_fluxo((char **)a->pdata, linha, dados, erro, tam);
	g_ptr_array_free(a, TRUE);
	return r;
}
