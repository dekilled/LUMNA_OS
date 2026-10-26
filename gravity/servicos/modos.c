/* Os modos do desktop (modos.h). */
#include "modos.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static const char *const ids[MODO_N] = { "desktop", "gaming", "midia" };
static const char *const nomes[MODO_N] = { "Desktop", "Gaming", "Mídia" };
static const char *const icones[MODO_N] = { "desktop", "gamepad", "media-play" };
static const char *const descricoes[MODO_N] = { "Janelas livres, dock e avisos", "Tela cheia para os jogos, sem distrações",
	"Para ver filmes, séries e ouvir música" };

const char *modos_nome(int m) { return m >= 0 && m < MODO_N ? nomes[m] : nomes[0]; }
const char *modos_id(int m) { return m >= 0 && m < MODO_N ? ids[m] : ids[0]; }
const char *modos_icone(int m) { return m >= 0 && m < MODO_N ? icones[m] : icones[0]; }
const char *modos_descricao(int m) { return m >= 0 && m < MODO_N ? descricoes[m] : descricoes[0]; }

int modos_de_id(const char *id) {
	for (int i = 0; id && i < MODO_N; i++) {
		if (!strcmp(id, ids[i])) {
			return i;
		}
	}
	if (id && (!strcmp(id, "media") || !strcmp(id, "mídia"))) {
		return MODO_MIDIA;
	}
	return -1;
}

/* ---- o Orbit ---- */

static int ligar(void) {
	const char *caminho = getenv("ORBIT_SOCK");
	char fallback[108];
	if (!caminho || !*caminho) {
		const char *runtime = getenv("XDG_RUNTIME_DIR"), *display = getenv("WAYLAND_DISPLAY");
		if (!runtime || !display) {
			return -1;
		}
		snprintf(fallback, sizeof(fallback), "%s/orbit.%s.sock", runtime, display);
		caminho = fallback;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", caminho);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (fd >= 0) {
			close(fd);
		}
		return -1;
	}
	return fd;
}

/* um pedido e a linha de resposta (0 se falhou) */
static bool pedir(const char *pedido, char *resposta, size_t tam) {
	int fd = ligar();
	if (fd < 0) {
		return false;
	}
	struct timeval tv = { .tv_sec = 1 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	char linha[200];
	int n = snprintf(linha, sizeof(linha), "%s\n", pedido);
	bool ok = write(fd, linha, n) == n;
	size_t len = 0;
	while (ok && len + 1 < tam) {
		ssize_t r = read(fd, resposta + len, tam - 1 - len);
		if (r <= 0) {
			break;
		}
		len += r;
		if (memchr(resposta, '\n', len)) {
			break;
		}
	}
	resposta[len] = '\0';
	close(fd);
	return ok && len > 0;
}

bool modos_ler(struct modos_estado *e) {
	memset(e, 0, sizeof(*e));
	char r[2048];
	if (!pedir("modo", r, sizeof(r)) || !strstr(r, "\"ok\":true")) {
		return false;
	}
	e->orbit = true;
	e->estender = strstr(r, "\"layout\":\"estender\"") != NULL;
	const char *ar = strstr(r, "\"areas\":");
	e->areas = ar ? atoi(ar + 8) : MODOS_AREAS_MAX;
	if (e->areas < 1 || e->areas > MODOS_AREAS_MAX) {
		e->areas = MODOS_AREAS_MAX;
	}
	const char *p = r;
	while (e->n < MODOS_MAX_SAIDAS && (p = strstr(p, "{\"saida\":\""))) {
		p += 10;
		const char *fim = strchr(p, '"');
		if (!fim) {
			break;
		}
		struct modos_saida *s = &e->saidas[e->n];
		g_snprintf(s->nome, sizeof(s->nome), "%.*s", (int)(fim - p), p);
		const char *m = strstr(fim, "\"modo\":\"");
		if (!m) {
			break;
		}
		m += 8;
		const char *mf = strchr(m, '"');
		char id[16];
		g_snprintf(id, sizeof(id), "%.*s", mf ? (int)(mf - m) : 0, m);
		s->modo = modos_de_id(id) < 0 ? 0 : modos_de_id(id);
		const char *fim_obj = strchr(mf ? mf : fim, '}');
		const char *pt = strstr(fim, "\"ponteiro\":true");
		s->ponteiro = pt && fim_obj && pt < fim_obj;
		const char *es = strstr(fim, "\"espaco\":");
		s->espaco = es && fim_obj && es < fim_obj ? atoi(es + 9) : 1;
		if (s->espaco < 1 || s->espaco > e->areas) {
			s->espaco = 1;
		}
		e->n++;
		p = fim;
	}
	return true;
}

bool orbit_area_util(const char *saida, int *x, int *y, int *largura, int *altura) {
	char r[4096];
	if (!pedir("saidas", r, sizeof(r)) || !strstr(r, "\"ok\":true")) {
		return false;
	}
	const char *p = r;
	while ((p = strstr(p, "{\"nome\":\""))) {
		p += 9;
		const char *fim = strchr(p, '"');
		const char *u = fim ? strstr(fim, "\"util\":{") : NULL;
		if (!u) {
			return false;
		}
		if (!saida || !*saida || (!strncmp(p, saida, strlen(saida)) && (size_t)(fim - p) == strlen(saida))) {
			int vx = 0, vy = 0, vl = 0, va = 0;
			if (sscanf(u, "\"util\":{\"x\":%d,\"y\":%d,\"largura\":%d,\"altura\":%d", &vx, &vy, &vl, &va) != 4) {
				return false;
			}
			if (x) *x = vx;
			if (y) *y = vy;
			if (largura) *largura = vl;
			if (altura) *altura = va;
			return true;
		}
		p = fim;
	}
	return false;
}

int modos_em_foco(const struct modos_estado *e) {
	for (int i = 0; i < e->n; i++) {
		if (e->saidas[i].ponteiro) {
			return e->saidas[i].modo;
		}
	}
	return e->n ? e->saidas[0].modo : MODO_DESKTOP;
}

int modos_da_saida(const struct modos_estado *e, const char *saida) {
	for (int i = 0; saida && i < e->n; i++) {
		if (!strcmp(e->saidas[i].nome, saida)) {
			return e->saidas[i].modo;
		}
	}
	return -1;
}

bool modos_definir(int modo, const char *saida) {
	char pedido[120], r[1024];
	snprintf(pedido, sizeof(pedido), "modo %s%s%s", modos_id(modo), saida && *saida ? " " : "", saida && *saida ? saida : "");
	return pedir(pedido, r, sizeof(r)) && strstr(r, "\"ok\":true");
}

bool modos_definir_layout(bool estender) {
	char r[256];
	return pedir(estender ? "modo_layout estender" : "modo_layout tela", r, sizeof(r)) && strstr(r, "\"ok\":true");
}

bool modos_definir_areas(int areas) {
	char pedido[40], r[256];
	snprintf(pedido, sizeof(pedido), "areas %d", areas);
	return pedir(pedido, r, sizeof(r)) && strstr(r, "\"ok\":true");
}

bool modos_definir_espaco(int espaco, const char *saida) {
	char pedido[120], r[256];
	snprintf(pedido, sizeof(pedido), "espaco %d%s%s", espaco, saida && *saida ? " " : "", saida && *saida ? saida : "");
	return pedir(pedido, r, sizeof(r)) && strstr(r, "\"ok\":true");
}

int modos_assinar(void) {
	int fd = ligar();
	if (fd < 0) {
		return -1;
	}
	if (write(fd, "subscrever\n", 11) != 11) {
		close(fd);
		return -1;
	}
	return fd;
}

void modos_assinar_ler(int fd) {
	char buf[4096];
	/* sem bloquear: o que veio já não importa, o estado se pede de novo */
	while (recv(fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {
	}
}

/* ---- modos.ini ---- */

static char *caminho_ini(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	return config && *config ? g_build_filename(config, "lumna", "modos.ini", NULL)
				 : g_build_filename(g_get_home_dir(), ".config", "lumna", "modos.ini", NULL);
}

static void padrao(struct modos_cfg *c) {
	memset(c, 0, sizeof(*c));
	c->ao_ligar = MODO_DESKTOP;
	c->areas = MODOS_AREAS_MAX;
	for (int i = 1; i < MODO_N; i++) {
		c->m[i].esconder_dock = c->m[i].nao_perturbe = true;
	}
}

void modos_cfg_ler(struct modos_cfg *c) {
	padrao(c);
	char *caminho = caminho_ini(), *texto = NULL;
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		int secao = -1; /* -1: [geral] */
		for (char **l = linhas; *l; l++) {
			char *s = g_strstrip(*l);
			if (*s == '[') {
				char *f = strchr(s, ']');
				if (f) {
					*f = '\0';
					secao = !strcmp(s + 1, "geral") ? -1 : modos_de_id(s + 1);
				}
				continue;
			}
			char *igual = strchr(s, '=');
			if (!igual || *s == '#') {
				continue;
			}
			*igual = '\0';
			char *k = g_strstrip(s), *v = g_strstrip(igual + 1);
			if (secao == -1) {
				if (!strcmp(k, "layout")) {
					c->estender = !strcmp(v, "estender");
				} else if (!strcmp(k, "areas")) {
					c->areas = atoi(v) < 1 ? 1 : atoi(v) > MODOS_AREAS_MAX ? MODOS_AREAS_MAX : atoi(v);
				} else if (!strcmp(k, "ao_ligar") && modos_de_id(v) >= 0) {
					c->ao_ligar = modos_de_id(v);
				}
			} else if (secao >= 0) {
				if (!strcmp(k, "esconder_dock")) {
					c->m[secao].esconder_dock = !strcmp(v, "sim");
				} else if (!strcmp(k, "nao_perturbe")) {
					c->m[secao].nao_perturbe = !strcmp(v, "sim");
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_free(caminho);
}

bool modos_cfg_gravar(const struct modos_cfg *c) {
	GString *s = g_string_new("# Os modos do desktop (o Painel de controle grava este arquivo)\n[geral]\n");
	g_string_append_printf(s, "layout = %s\nareas = %d\nao_ligar = %s\n", c->estender ? "estender" : "tela", c->areas, modos_id(c->ao_ligar));
	for (int i = 0; i < MODO_N; i++) {
		g_string_append_printf(s, "\n[%s]\nesconder_dock = %s\nnao_perturbe = %s\n", modos_id(i), c->m[i].esconder_dock ? "sim" : "nao",
			c->m[i].nao_perturbe ? "sim" : "nao");
	}
	char *caminho = caminho_ini(), *pasta = g_path_get_dirname(caminho);
	g_mkdir_with_parents(pasta, 0755);
	char *tmp = g_strdup_printf("%s.novo", caminho);
	bool ok = g_file_set_contents(tmp, s->str, -1, NULL) && rename(tmp, caminho) == 0;
	g_free(tmp);
	g_free(pasta);
	g_free(caminho);
	g_string_free(s, TRUE);
	return ok;
}

bool modos_esconde_dock(const char *saida) {
	struct modos_estado e;
	struct modos_cfg c;
	if (!modos_ler(&e)) {
		return false;
	}
	modos_cfg_ler(&c);
	int m = saida && *saida ? modos_da_saida(&e, saida) : modos_em_foco(&e);
	return m > 0 && c.m[m].esconder_dock;
}

bool modos_nao_perturbe(void) {
	struct modos_estado e;
	struct modos_cfg c;
	if (!modos_ler(&e)) {
		return false;
	}
	modos_cfg_ler(&c);
	int m = modos_em_foco(&e);
	return m > 0 && c.m[m].nao_perturbe;
}
