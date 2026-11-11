/*
 * lumna-processos: o gerenciador de processos. Acima de todos os outros apps, sem importar quem esteja na tela (um jogo travado em tela cheia, o bloqueio...),
 * com poder sobre os processos: encerrar (pedir ou forçar na hora), a árvore inteira de um aplicativo, e a prioridade em cinco níveis (Baixa, Média, Alta,
 * Máxima e Tempo real).
 *
 *   lumna-processos            abre (ou fecha, se já está aberto). Fala com o serviço residente; sem ele, abre sozinho.
 *   lumna-processos servico    o serviço residente: prioridade de tempo real e fora do alcance do OOM killer, para responder mesmo com um jogo usando toda
 *                              a CPU. Fica quieto (sem janela) e abre a janela quando chamado (o Orbit chama no Ctrl+Shift+Esc).
 *   lumna-processos janela     a janela (o serviço a chama).
 *
 * A janela é uma camada overlay do Orbit (por cima das janelas em tela cheia) com o teclado exclusivo.
 * LUMNA_PROCESSOS_DEBUG=1 escreve posições e estado (os testes); LUMNA_PROCESSOS_AJUDANTE troca o ajudante com privilégio (os testes põem um de mentira).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <cairo.h>
#include <ctype.h>
#include <glib.h>
#include <stdarg.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gravity.h"
#include "processos.h"

#define AJUDANTE_PADRAO "/usr/lib/lumna/lumna-processos-ajudante"
#define INTERVALO_S 2.0

static gv_app *app;

/* ---- o que a tela mostra ---- */

struct linha {
	int pid, ppid;
	uint64_t inicio;
	char nome[80], usuario[32];
	char estado;
	bool meu, infra;
	int nivel;
	double cpu;
	int64_t memoria_kb;
	int nproc;          /* quantos processos tem (o grupo inteiro) */
	int profundidade;   /* 0: o aplicativo; 1...: o que ele abriu */
	bool grupo;         /* tem filhos (abre e fecha) */
	int raiz;           /* o pid da raiz do grupo */
};

static struct pr_lista lista;
static struct linha *linhas;
static int n_linhas;
static int tela;            /* 0 Processos */
static char busca[96];
static int filtro;          /* 0 todos, 1 meus, 2 aplicativos, 3 sistema */
static int ordem;           /* 0 CPU, 1 memória, 2 nome, 3 PID */
static bool decrescente = true;
static GHashTable *abertos; /* pid -> aberto */
static int sel_pid;
static uint64_t sel_inicio;
static char mensagem[260];
static double mensagem_ate;
static double ultima_leitura;
static int meu_uid;
static bool confirmar;
static int confirmar_pid;
static bool confirmar_forcar, confirmar_arvore;
static bool ocupado;        /* esperando o ajudante (pkexec) */
static double cpu_total, gpu_total, disco_total;
static int64_t mem_total_kb, mem_usada_kb;

static void avisar(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void avisar(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(mensagem, sizeof(mensagem), fmt, ap);
	va_end(ap);
	mensagem_ate = gv_agora() + 7;
}

static const char *const FILTROS[] = { "Todos os processos", "Meus processos", "Aplicativos", "Sistema" };
static const char *const NOMES_NIVEL[PR_NIVEIS] = { "Baixa", "Média", "Alta", "Máxima", "Tempo real" };

/* o que é infraestrutura da sessão (não é um aplicativo que a pessoa abriu) */
static bool nome_infra(const char *n) {
	static const char *const infra[] = { "systemd", "dbus-daemon", "dbus-run-session", "pipewire", "pipewire-pulse", "wireplumber", "orbit", "at-spi-bus-laun", "at-spi2-registr",
		"gvfsd", "gvfs-udisks2-vo", "xdg-document-po", "xdg-desktop-por", "xdg-permission-", "polkit", "lumna-polkit", "lumna-teclado", "lumna-ocioso", "lumna-papel", "lumna-barra",
		"lumna-dock", "lumna-avisos", "lumna-luz", "lumna-discos", "lumna-widgets", "lumna-inicio", "lumna-connect", "lumna-gamepad", "lumna-portal", "(sd-pam)", "Xwayland", NULL };
	for (int i = 0; infra[i]; i++) {
		if (!strncmp(n, infra[i], strlen(infra[i]))) {
			return true;
		}
	}
	return false;
}

static bool nome_shell(const char *n) {
	return !strcmp(n, "sh") || !strcmp(n, "bash") || !strcmp(n, "dash") || !strcmp(n, "zsh") || !strcmp(n, "env");
}

/* um processo "corta" a subida: o pai dele é o init, o gerenciador de sessão ou o compositor (os apps são a partir dele) */
static bool nome_corte(const struct pr_proc *p) {
	return p->ppid <= 1 || !strcmp(p->nome, "systemd") || !strcmp(p->nome, "orbit") || !strcmp(p->nome, "dbus-run-session") || !strcmp(p->nome, "dbus-daemon") || !strcmp(p->nome, "greetd");
}

/* o texto que vai na lista para um grupo cuja raiz é uma casca (sh -c firefox): o nome do maior descendente */
static void nome_do_grupo(const struct pr_lista *l, const struct pr_proc *raiz, char *dest, size_t tam) {
	snprintf(dest, tam, "%s", raiz->nome);
	if (!nome_shell(raiz->nome)) {
		return;
	}
	const struct pr_proc *melhor = NULL;
	for (int i = 0; i < l->n; i++) {
		const struct pr_proc *q = &l->v[i];
		if (q->sessao == raiz->sessao && q->pid != raiz->pid && !nome_shell(q->nome) && (!melhor || q->memoria_kb > melhor->memoria_kb)) {
			melhor = q;
		}
	}
	if (melhor) {
		snprintf(dest, tam, "%s", melhor->nome);
	}
}

static int cmp_linhas(const void *a, const void *b) {
	const struct linha *x = a, *y = b;
	if (x->raiz != y->raiz) {
		return 0; /* a ordem entre grupos é feita antes (ordenar_grupos); aqui só dentro do grupo */
	}
	if (x->profundidade != y->profundidade) {
		return x->profundidade - y->profundidade;
	}
	return 0;
}

struct grupo {
	int raiz;
	char nome[80];
	double cpu;
	int64_t mem;
	int nproc;
	bool meu, infra;
	int primeiro;
	uid_t uid;
	char usuario[32];
	char estado;
	int nivel;
	uint64_t inicio;
};

static int ordenar_grupos_por;
static bool ordenar_desc;

static int cmp_grupos(const void *a, const void *b) {
	const struct grupo *x = a, *y = b;
	int r = 0;
	switch (ordenar_grupos_por) {
	case 0: r = x->cpu < y->cpu ? -1 : x->cpu > y->cpu ? 1 : 0; break;
	case 1: r = x->mem < y->mem ? -1 : x->mem > y->mem ? 1 : 0; break;
	case 2: r = g_ascii_strcasecmp(x->nome, y->nome); break;
	default: r = x->raiz - y->raiz; break;
	}
	if (r == 0) {
		r = x->raiz - y->raiz;
	}
	return ordenar_desc ? -r : r;
}

static bool casa_busca(const struct pr_proc *p, const char *nome) {
	if (!busca[0]) {
		return true;
	}
	char *b = g_utf8_casefold(busca, -1);
	char *n = g_utf8_casefold(nome, -1), *l = g_utf8_casefold(p->linha, -1);
	char pid[16];
	snprintf(pid, sizeof(pid), "%d", p->pid);
	bool ok = strstr(n, b) || strstr(l, b) || strstr(pid, b);
	g_free(b);
	g_free(n);
	g_free(l);
	return ok;
}

/* os processos viram grupos (um por aplicativo, com tudo o que ele abriu) e as linhas da tabela */
static void montar_linhas(void) {
	free(linhas);
	linhas = NULL;
	n_linhas = 0;
	if (!lista.n) {
		return;
	}
	int n = lista.n;
	int *raiz_de = g_new(int, n);
	GHashTable *idx = g_hash_table_new(g_direct_hash, g_direct_equal);
	for (int i = 0; i < n; i++) {
		g_hash_table_insert(idx, GINT_TO_POINTER(lista.v[i].pid), GINT_TO_POINTER(i + 1));
	}
	for (int i = 0; i < n; i++) {
		int cur = i, passos = 0;
		while (passos++ < 64) {
			const struct pr_proc *p = &lista.v[cur];
			int ip = GPOINTER_TO_INT(g_hash_table_lookup(idx, GINT_TO_POINTER(p->ppid))) - 1;
			if (ip < 0 || nome_corte(&lista.v[ip]) || nome_corte(p)) {
				break;
			}
			cur = ip;
		}
		raiz_de[i] = lista.v[cur].pid;
	}
	/* os grupos */
	GHashTable *gi = g_hash_table_new(g_direct_hash, g_direct_equal);
	struct grupo *gr = g_new0(struct grupo, n);
	int ng = 0;
	for (int i = 0; i < n; i++) {
		const struct pr_proc *p = &lista.v[i];
		if (p->kernel || p->pid == 2) {
			continue;
		}
		int r = raiz_de[i];
		int pos = GPOINTER_TO_INT(g_hash_table_lookup(gi, GINT_TO_POINTER(r)));
		if (!pos) {
			const struct pr_proc *raiz = NULL;
			for (int k = 0; k < n; k++) {
				if (lista.v[k].pid == r) {
					raiz = &lista.v[k];
					break;
				}
			}
			if (!raiz) {
				continue;
			}
			gr[ng].raiz = r;
			nome_do_grupo(&lista, raiz, gr[ng].nome, sizeof(gr[ng].nome));
			gr[ng].meu = raiz->uid == (uid_t)meu_uid;
			gr[ng].infra = nome_infra(raiz->nome) || nome_infra(gr[ng].nome);
			gr[ng].uid = raiz->uid;
			snprintf(gr[ng].usuario, sizeof(gr[ng].usuario), "%s", raiz->usuario);
			gr[ng].estado = raiz->estado;
			gr[ng].nivel = raiz->nivel;
			gr[ng].inicio = raiz->inicio;
			pos = ++ng;
			g_hash_table_insert(gi, GINT_TO_POINTER(r), GINT_TO_POINTER(pos));
		}
		struct grupo *g = &gr[pos - 1];
		g->cpu += p->cpu;
		g->mem += p->memoria_kb;
		g->nproc++;
	}
	/* o filtro e a busca (um grupo entra se ele ou algum descendente casa) */
	struct grupo *ok = g_new0(struct grupo, ng > 0 ? ng : 1);
	int nok = 0;
	for (int g = 0; g < ng; g++) {
		bool passa = filtro == 0 || (filtro == 1 && gr[g].meu) || (filtro == 2 && gr[g].meu && !gr[g].infra) || (filtro == 3 && (!gr[g].meu || gr[g].infra));
		if (!passa) {
			continue;
		}
		bool casa = !busca[0] || casa_busca(&(struct pr_proc){ .pid = gr[g].raiz, .linha = "" }, gr[g].nome);
		for (int i = 0; i < n && !casa; i++) {
			casa = raiz_de[i] == gr[g].raiz && casa_busca(&lista.v[i], lista.v[i].nome);
		}
		if (casa) {
			ok[nok++] = gr[g];
		}
	}
	ordenar_grupos_por = ordem;
	ordenar_desc = decrescente;
	qsort(ok, (size_t)nok, sizeof(struct grupo), cmp_grupos);
	/* as linhas: o grupo e, se aberto, o que ele abriu (em ordem de PID, com a profundidade) */
	linhas = calloc((size_t)(n + nok + 1), sizeof(struct linha));
	for (int g = 0; g < nok; g++) {
		const struct pr_proc *raiz = NULL;
		for (int k = 0; k < n; k++) {
			if (lista.v[k].pid == ok[g].raiz) {
				raiz = &lista.v[k];
				break;
			}
		}
		if (!raiz) {
			continue;
		}
		struct linha *l = &linhas[n_linhas++];
		l->pid = raiz->pid;
		l->ppid = raiz->ppid;
		l->inicio = raiz->inicio;
		snprintf(l->nome, sizeof(l->nome), "%s", ok[g].nome);
		snprintf(l->usuario, sizeof(l->usuario), "%s", ok[g].usuario);
		l->estado = ok[g].estado;
		l->meu = ok[g].meu;
		l->infra = ok[g].infra;
		l->nivel = ok[g].nivel;
		l->cpu = ok[g].cpu;
		l->memoria_kb = ok[g].mem;
		l->nproc = ok[g].nproc;
		l->grupo = ok[g].nproc > 1;
		l->raiz = ok[g].raiz;
		if (l->grupo && (busca[0] || g_hash_table_contains(abertos, GINT_TO_POINTER(l->pid)))) {
			/* os descendentes: em largura a partir da raiz, com a profundidade relativa */
			int fila[4096], prof[4096], nf = 0;
			fila[nf] = raiz->pid;
			prof[nf++] = 0;
			for (int h = 0; h < nf && nf < 4096; h++) {
				for (int k = 0; k < n && nf < 4096; k++) {
					if (lista.v[k].ppid == fila[h] && raiz_de[k] == ok[g].raiz && lista.v[k].pid != raiz->pid) {
						fila[nf] = lista.v[k].pid;
						prof[nf++] = prof[h] + 1;
					}
				}
			}
			for (int h = 1; h < nf; h++) {
				for (int k = 0; k < n; k++) {
					if (lista.v[k].pid != fila[h]) {
						continue;
					}
					const struct pr_proc *q = &lista.v[k];
					if (busca[0] && !casa_busca(q, q->nome)) {
						break; /* com busca, o grupo abre e mostra só o que casa */
					}
					struct linha *c = &linhas[n_linhas++];
					c->pid = q->pid;
					c->ppid = q->ppid;
					c->inicio = q->inicio;
					snprintf(c->nome, sizeof(c->nome), "%s", q->nome);
					snprintf(c->usuario, sizeof(c->usuario), "%s", q->usuario);
					c->estado = q->estado;
					c->meu = q->uid == (uid_t)meu_uid;
					c->infra = nome_infra(q->nome);
					c->nivel = q->nivel;
					c->cpu = q->cpu;
					c->memoria_kb = q->memoria_kb;
					c->nproc = 1;
					c->profundidade = prof[h];
					c->raiz = ok[g].raiz;
					break;
				}
			}
		}
	}
	(void)cmp_linhas;
	g_free(ok);
	g_free(gr);
	g_hash_table_destroy(gi);
	g_hash_table_destroy(idx);
	g_free(raiz_de);
}

/* ---- os números de cima: CPU, memória, GPU, discos ---- */

static void ler_sistema(void) {
	static unsigned long long u0, t0;
	static unsigned long long disco_io0;
	static double disco_t0;
	FILE *f = fopen("/proc/stat", "r");
	if (f) {
		unsigned long long u, n, s, id, iow, irq, sirq, st;
		if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &id, &iow, &irq, &sirq, &st) == 8) {
			unsigned long long total = u + n + s + id + iow + irq + sirq + st, ocioso = id + iow;
			if (t0 && total > t0) {
				cpu_total = 100.0 * (double)((total - t0) - (ocioso - u0)) / (double)(total - t0);
			}
			t0 = total;
			u0 = ocioso;
		}
		fclose(f);
	}
	f = fopen("/proc/meminfo", "r");
	if (f) {
		char linha[128];
		int64_t disp = 0;
		while (fgets(linha, sizeof(linha), f)) {
			long long v;
			if (sscanf(linha, "MemTotal: %lld", &v) == 1) {
				mem_total_kb = v;
			} else if (sscanf(linha, "MemAvailable: %lld", &v) == 1) {
				disp = v;
			}
		}
		fclose(f);
		mem_usada_kb = mem_total_kb - disp;
	}
	/* a GPU: o que o amdgpu e o i915 dizem no sysfs; a NVIDIA fica sem número (precisa do nvidia-smi) */
	gpu_total = -1;
	GDir *d = g_dir_open("/sys/class/drm", 0, NULL);
	const char *nome;
	while (d && (nome = g_dir_read_name(d))) {
		if (!g_str_has_prefix(nome, "card") || strchr(nome, '-')) {
			continue;
		}
		char *arq = g_strdup_printf("/sys/class/drm/%s/device/gpu_busy_percent", nome);
		char *txt = NULL;
		if (g_file_get_contents(arq, &txt, NULL, NULL)) {
			gpu_total = atof(txt);
			g_free(txt);
		}
		g_free(arq);
	}
	if (d) {
		g_dir_close(d);
	}
	/* os discos: o mais ocupado (tempo de E/S nos últimos instantes) */
	f = fopen("/proc/diskstats", "r");
	if (f) {
		char linha[256];
		unsigned long long maior = 0;
		while (fgets(linha, sizeof(linha), f)) {
			unsigned maj, min;
			char dev[64];
			unsigned long long v[11];
			if (sscanf(linha, "%u %u %63s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &maj, &min, dev, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10]) < 14) {
				continue;
			}
			bool fisico = (!strncmp(dev, "sd", 2) && !isdigit((unsigned char)dev[strlen(dev) - 1])) || (!strncmp(dev, "nvme", 4) && !strchr(dev, 'p')) || (!strncmp(dev, "vd", 2) && !isdigit((unsigned char)dev[strlen(dev) - 1]));
			if (fisico && v[9] > maior) {
				maior = v[9];
			}
		}
		fclose(f);
		double agora = gv_agora();
		if (disco_t0 > 0 && agora > disco_t0 && maior >= disco_io0) {
			disco_total = 100.0 * ((double)(maior - disco_io0) / 1000.0) / (agora - disco_t0);
			if (disco_total > 100) {
				disco_total = 100;
			}
		}
		disco_io0 = maior;
		disco_t0 = agora;
	}
}

/* ---- o histórico para os gráficos (2 minutos, uma amostra a cada ~2 s) ---- */
#define HIST 60
enum { H_CPU, H_MEM, H_GPU, H_DISCO, H_REDE_BAIXA, H_REDE_SOBE, H_N };
static double hist[H_N][HIST];
static int hist_n;                 /* quantas amostras valem (até HIST) */
static double hist_ultima;
static double rede_baixa, rede_sobe; /* bytes por segundo */
static bool tem_gpu;

static void ler_rede(double agora) {
	static unsigned long long rx0, tx0;
	static double t0;
	FILE *f = fopen("/proc/net/dev", "r");
	if (!f) {
		return;
	}
	char linha[512];
	unsigned long long rx = 0, tx = 0;
	while (fgets(linha, sizeof(linha), f)) {
		char *dp = strchr(linha, ':');
		if (!dp) {
			continue;
		}
		*dp = '\0';
		const char *nome = g_strstrip(linha);
		if (!strcmp(nome, "lo") || g_str_has_prefix(nome, "veth") || g_str_has_prefix(nome, "docker") || g_str_has_prefix(nome, "br-")) {
			continue;
		}
		unsigned long long v[16];
		if (sscanf(dp + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) == 9) {
			rx += v[0];
			tx += v[8];
		}
	}
	fclose(f);
	if (t0 > 0 && agora > t0 && rx >= rx0 && tx >= tx0) {
		rede_baixa = (double)(rx - rx0) / (agora - t0);
		rede_sobe = (double)(tx - tx0) / (agora - t0);
	}
	rx0 = rx;
	tx0 = tx;
	t0 = agora;
}

static void guardar_amostra(void) {
	double agora = gv_agora();
	ler_rede(agora);
	if (hist_ultima > 0 && agora - hist_ultima < 1.5) {
		return; /* uma ação do usuário também lê a lista: não vira amostra (o intervalo curto distorce a CPU) */
	}
	hist_ultima = agora;
	for (int k = 0; k < H_N; k++) {
		memmove(&hist[k][0], &hist[k][1], sizeof(double) * (HIST - 1));
	}
	hist[H_CPU][HIST - 1] = cpu_total;
	hist[H_MEM][HIST - 1] = mem_total_kb > 0 ? 100.0 * (double)mem_usada_kb / (double)mem_total_kb : 0;
	hist[H_GPU][HIST - 1] = gpu_total >= 0 ? gpu_total : 0;
	hist[H_DISCO][HIST - 1] = disco_total;
	hist[H_REDE_BAIXA][HIST - 1] = rede_baixa;
	hist[H_REDE_SOBE][HIST - 1] = rede_sobe;
	tem_gpu = gpu_total >= 0;
	if (hist_n < HIST) {
		hist_n++;
	}
}

static void achar_jogo(void);
static void ler_tudo(void) {
	pr_liberar(&lista);
	pr_listar(&lista);
	ler_sistema();
	guardar_amostra();
	achar_jogo();
	montar_linhas();
	ultima_leitura = gv_agora();
}

/* ---- o jogo em execução: a Steam marca todo processo de um jogo (nativo ou Proton) com SteamGameId no ambiente ---- */
struct jogo_atual {
	bool ativo;
	int appid, pid, nproc;
	uint64_t inicio;
	char nome[80];
};
static struct jogo_atual jogo;

static int steam_id_de(int pid) {
	char arq[48];
	snprintf(arq, sizeof(arq), "/proc/%d/environ", pid);
	FILE *f = fopen(arq, "r");
	if (!f) {
		return 0;
	}
	static char buf[131072];
	size_t n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = '\0';
	for (size_t i = 0; i < n;) {
		if (!strncmp(buf + i, "SteamGameId=", 12)) {
			return atoi(buf + i + 12);
		}
		i += strlen(buf + i) + 1;
	}
	return 0;
}

static void achar_jogo(void) {
	memset(&jogo, 0, sizeof(jogo));
	int n = lista.n;
	if (n <= 0) {
		return;
	}
	int *gid = g_new0(int, n);
	for (int i = 0; i < n; i++) {
		const struct pr_proc *p = &lista.v[i];
		if (!p->kernel && p->uid == (uid_t)meu_uid) {
			gid[i] = steam_id_de(p->pid);
		}
	}
	/* o jogo com mais memória (se houver mais de um); a raiz dele é o processo cujo pai não é do mesmo jogo */
	int melhor_id = 0;
	int64_t melhor_mem = -1;
	for (int i = 0; i < n; i++) {
		if (gid[i] <= 0) {
			continue;
		}
		int64_t mem = 0;
		for (int k = 0; k < n; k++) {
			if (gid[k] == gid[i]) {
				mem += lista.v[k].memoria_kb;
			}
		}
		if (mem > melhor_mem) {
			melhor_mem = mem;
			melhor_id = gid[i];
		}
	}
	if (!melhor_id) {
		g_free(gid);
		return;
	}
	int64_t maior = -1;
	for (int i = 0; i < n; i++) {
		if (gid[i] != melhor_id) {
			continue;
		}
		jogo.nproc++;
		if (lista.v[i].memoria_kb > maior) {
			maior = lista.v[i].memoria_kb;
			g_strlcpy(jogo.nome, lista.v[i].nome, sizeof(jogo.nome));
		}
		bool pai_do_jogo = false;
		for (int k = 0; k < n; k++) {
			if (lista.v[k].pid == lista.v[i].ppid && gid[k] == melhor_id) {
				pai_do_jogo = true;
			}
		}
		if (!pai_do_jogo && (!jogo.pid || lista.v[i].memoria_kb > 0)) {
			jogo.pid = lista.v[i].pid;
			jogo.inicio = lista.v[i].inicio;
		}
	}
	jogo.ativo = jogo.pid > 0;
	jogo.appid = melhor_id;
	g_free(gid);
}

/* ---- as ações ---- */

struct pedido_priv {
	char *args[10];
	char resumo[120];
	int codigo;
	char erro[200];
};

static void priv_trabalho(void *d) {
	struct pedido_priv *p = d;
	GError *err = NULL;
	gint estado = 0;
	char *saida_err = NULL;
	if (!g_spawn_sync(NULL, p->args, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &saida_err, &estado, &err)) {
		p->codigo = PR_ERRO;
		g_strlcpy(p->erro, err ? err->message : "não consegui pedir permissão", sizeof(p->erro));
		g_clear_error(&err);
		return;
	}
	p->codigo = g_spawn_check_wait_status(estado, NULL) ? PR_OK : (WIFEXITED(estado) ? WEXITSTATUS(estado) : PR_ERRO);
	if (WIFEXITED(estado) && WEXITSTATUS(estado) == 126) { /* o pkexec: a pessoa cancelou a senha */
		p->codigo = 126;
	} else if (WIFEXITED(estado) && WEXITSTATUS(estado) == 127) {
		p->codigo = 127;
	}
	g_strlcpy(p->erro, saida_err ? g_strstrip(saida_err) : "", sizeof(p->erro));
	g_free(saida_err);
}

static void priv_pronto(void *d) {
	struct pedido_priv *p = d;
	ocupado = false;
	if (p->codigo == PR_OK) {
		avisar("%s", p->resumo);
	} else if (p->codigo == 126 || p->codigo == 127) {
		avisar("Sem permissão: a senha não foi dada.");
	} else {
		avisar("Não deu: %s", p->erro[0] ? p->erro : "erro desconhecido");
	}
	for (int i = 0; p->args[i]; i++) {
		g_free(p->args[i]);
	}
	g_free(p);
	ler_tudo();
	gv_app_redesenhar(app);
}

/* o trabalho (o pkexec espera a senha) numa linha à parte; o resultado volta à linha principal */
static gpointer thread_priv(gpointer d) {
	priv_trabalho(d);
	gv_na_linha_principal(priv_pronto, d);
	return NULL;
}

/* pede ao ajudante (pkexec: a senha vale por uma sessão) */
static void pedir_ao_ajudante(char **argumentos_do_ajudante, const char *resumo) {
	struct pedido_priv *p = g_new0(struct pedido_priv, 1);
	const char *aj = g_getenv("LUMNA_PROCESSOS_AJUDANTE");
	int k = 0;
	if (!aj || !*aj) {
		p->args[k++] = g_strdup("pkexec");
		aj = AJUDANTE_PADRAO;
	}
	p->args[k++] = g_strdup(aj);
	for (int i = 0; argumentos_do_ajudante[i] && k < 8; i++) {
		p->args[k++] = g_strdup(argumentos_do_ajudante[i]);
	}
	g_strlcpy(p->resumo, resumo, sizeof(p->resumo));
	ocupado = true;
	avisar("Pedindo permissão…");
	g_thread_unref(g_thread_new("processos-priv", thread_priv, p));
}

static const struct linha *linha_de(int pid) {
	for (int i = 0; i < n_linhas; i++) {
		if (linhas[i].pid == pid) {
			return &linhas[i];
		}
	}
	return NULL;
}

static bool pedir_priv_forcado(void) {
	const char *e = g_getenv("LUMNA_PROCESSOS_FORCAR_PRIV");
	return e && *e;
}

static void encerrar_alvo(int pid, uint64_t inicio, const char *nome, char estado, bool forcar, bool arvore) {
	char erro[200] = "";
	int quantos = 0;
	enum pr_resultado r = pedir_priv_forcado() ? PR_PRECISA_PRIV : pr_encerrar(pid, inicio, arvore, forcar, &quantos, erro, sizeof(erro));
	if (r == PR_OK) {
		avisar("%s: %s (%d %s)", forcar ? "Encerrado na hora" : "Pedido de encerramento enviado", nome, quantos, quantos == 1 ? "processo" : "processos");
		if (estado == 'D') {
			avisar("%s está em estado D (esperando disco ou driver): o kernel só o tira quando o driver responder.", nome);
		}
	} else if (r == PR_PRECISA_PRIV) {
		char s_pid[16], s_ini[24];
		snprintf(s_pid, sizeof(s_pid), "%d", pid);
		snprintf(s_ini, sizeof(s_ini), "%llu", (unsigned long long)inicio);
		char *args[] = { "encerrar", s_pid, s_ini, arvore ? "arvore" : "um", forcar ? "forcar" : "pedir", NULL };
		pedir_ao_ajudante(args, forcar ? "Encerrado na hora." : "Pedido de encerramento enviado.");
	} else {
		avisar("%s", erro[0] ? erro : "Não consegui encerrar.");
	}
	ler_tudo();
}

static void encerrar(int pid, bool forcar, bool arvore) {
	const struct linha *l = linha_de(pid);
	if (!l) {
		return;
	}
	char nome[80];
	g_strlcpy(nome, l->nome, sizeof(nome));
	encerrar_alvo(pid, l->inicio, nome, l->estado, forcar, arvore);
}

/* fecha o jogo (e só ele: a Steam, o Orbit e o resto ficam) na hora: SIGKILL na árvore do jogo, sem confirmação (é para isso que se abre o gerenciador com o jogo travado) */
static void fechar_jogo(void) {
	if (!jogo.ativo) {
		return;
	}
	char nome[80];
	g_strlcpy(nome, jogo.nome, sizeof(nome));
	encerrar_alvo(jogo.pid, jogo.inicio, nome, 'R', true, true);
}

static void pedir_encerrar(bool forcar) {
	const struct linha *l = linha_de(sel_pid);
	if (!l) {
		return;
	}
	bool arvore = l->grupo || l->nproc > 1;
	/* um processo do sistema (de outro usuário ou da sessão) pede confirmação: derrubar o compositor ou o systemd leva a sessão junto */
	if (!l->meu || l->infra) {
		confirmar = true;
		confirmar_pid = l->pid;
		confirmar_forcar = forcar;
		confirmar_arvore = arvore;
		return;
	}
	encerrar(l->pid, forcar, arvore);
}

static void mudar_prioridade(int nivel) {
	const struct linha *l = linha_de(sel_pid);
	if (!l) {
		return;
	}
	char erro[200] = "";
	enum pr_resultado r = pedir_priv_forcado() ? PR_PRECISA_PRIV : pr_prioridade(l->pid, l->inicio, (enum pr_nivel)nivel, erro, sizeof(erro));
	if (r == PR_OK) {
		avisar("Prioridade de %s: %s", l->nome, NOMES_NIVEL[nivel]);
	} else if (r == PR_PRECISA_PRIV) {
		char s_pid[16], s_ini[24], s_niv[8];
		snprintf(s_pid, sizeof(s_pid), "%d", l->pid);
		snprintf(s_ini, sizeof(s_ini), "%llu", (unsigned long long)l->inicio);
		snprintf(s_niv, sizeof(s_niv), "%d", nivel);
		char *args[] = { "prioridade", s_pid, s_ini, s_niv, NULL };
		char resumo[120];
		snprintf(resumo, sizeof(resumo), "Prioridade de %s: %s", l->nome, NOMES_NIVEL[nivel]);
		pedir_ao_ajudante(args, resumo);
	} else {
		avisar("%s", erro[0] ? erro : "Não consegui mudar a prioridade.");
	}
	ler_tudo();
}

/* ---- o desenho ---- */

static const char *fmt_mem(int64_t kb) {
	if (kb >= 1024 * 1024) {
		return gv_fmt("%.1f GB", (double)kb / (1024.0 * 1024.0)).chars;
	}
	if (kb >= 1024) {
		return gv_fmt("%.0f MB", (double)kb / 1024.0).chars;
	}
	return gv_fmt("%lld KB", (long long)kb).chars;
}

static Clay_Color roxo(float a) {
	return (Clay_Color){ 139, 92, 246, 255 * a };
}

/* a depuração dos testes: onde está cada coisa e o estado */
struct reg {
	char nome[40];
	Clay_ElementId id;
};
static struct reg regs[1400];
static int n_regs;
static bool depurando;

static void reg(const char *nome, Clay_ElementId id) {
	if (depurando && n_regs < (int)(sizeof(regs) / sizeof(regs[0]))) {
		g_strlcpy(regs[n_regs].nome, nome, sizeof(regs[n_regs].nome));
		regs[n_regs++].id = id;
	}
}

static void debug_posicoes(void) {
	if (!depurando) {
		return;
	}
	static GHashTable *antes;
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	for (int i = 0; i < n_regs; i++) {
		Clay_BoundingBox b = gv_caixa(regs[i].id);
		if (b.width <= 0) {
			continue;
		}
		char *l = g_strdup_printf("%.0f %.0f %.0f %.0f", b.x, b.y, b.width, b.height);
		char *a = g_hash_table_lookup(antes, regs[i].nome);
		if (!a || strcmp(a, l)) {
			printf("pos %s %s\n", regs[i].nome, l);
			g_hash_table_insert(antes, g_strdup(regs[i].nome), l);
		} else {
			g_free(l);
		}
	}
	n_regs = 0;
	static char *estado_antes;
	char *estado = g_strdup_printf("estado tela=%d n=%d sel=%d busca=[%s] filtro=%d ordem=%d%s confirmar=%d ocupado=%d jogo=%d msg=[%s]", tela, n_linhas, sel_pid, busca, filtro, ordem, decrescente ? "-" : "+", confirmar, ocupado, jogo.ativo ? jogo.appid : 0, mensagem);
	if (!estado_antes || strcmp(estado_antes, estado)) {
		printf("%s\n", estado);
		g_free(estado_antes);
		estado_antes = estado;
	} else {
		g_free(estado);
	}
	GString *o = g_string_new("ordem");
	for (int i = 0; i < n_linhas && i < 60; i++) {
		g_string_append_printf(o, " %d%s", linhas[i].pid, linhas[i].pid == sel_pid ? "*" : "");
	}
	static char *ordem_antes;
	if (!ordem_antes || strcmp(ordem_antes, o->str)) {
		printf("%s\n", o->str);
		g_free(ordem_antes);
		ordem_antes = g_strdup(o->str);
	}
	g_string_free(o, TRUE);
	fflush(stdout);
}

static void cabecalho_coluna(const char *id_nome, Clay_ElementId id, const char *texto, int coluna, float largura) {
	reg(id_nome, id);
	CLAY(id, { .layout = { .sizing = { largura > 0 ? CLAY_SIZING_FIXED(largura) : CLAY_SIZING_GROW(0) }, .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, ordem == coluna ? gv_t.texto : gv_t.texto_suave, GV_PESO_MEDIO);
		if (ordem == coluna) {
			gv_icone(decrescente ? "chevron-down" : "chevron-up", 14, gv_t.texto_suave);
		}
	}
	if (gv_tocado(id)) {
		if (ordem == coluna) {
			decrescente = !decrescente;
		} else {
			ordem = coluna;
			decrescente = coluna < 2;
		}
		montar_linhas();
	}
}

static void menu_acoes(const char *base_id) {
	(void)base_id;
	CLAY(CLAY_ID("acoes-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
		if (gv_menu_item(CLAY_ID("ac-pedir"), "close", "Encerrar", NULL)) {
			gv_popup_fechar(CLAY_ID("menu-acoes"));
			gv_popup_fechar(CLAY_ID("menu-linha"));
			pedir_encerrar(false);
		}
		if (gv_menu_item(CLAY_ID("ac-forcar"), "zap", "Forçar encerramento (na hora)", NULL)) {
			gv_popup_fechar(CLAY_ID("menu-acoes"));
			gv_popup_fechar(CLAY_ID("menu-linha"));
			pedir_encerrar(true);
		}
		CLAY(CLAY_ID("ac-div"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
		gv_texto(CLAY_STRING("Prioridade"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
		const struct linha *l = linha_de(sel_pid);
		for (int nv = PR_NIVEIS - 1; nv >= 0; nv--) {
			Clay_ElementId id = CLAY_IDI("ac-prio", (uint32_t)nv);
			if (gv_menu_item(id, l && l->nivel == nv ? "check" : NULL, NOMES_NIVEL[nv], NULL)) {
				gv_popup_fechar(CLAY_ID("menu-acoes"));
				gv_popup_fechar(CLAY_ID("menu-linha"));
				mudar_prioridade(nv);
			}
			char nome[24];
			snprintf(nome, sizeof(nome), "prio/%d", nv);
			reg(nome, id);
		}
	}
}

static void linha_processo(const struct linha *l) {
	Clay_ElementId id = CLAY_IDI("linha", (uint32_t)l->pid);
	char nome_reg[24];
	snprintf(nome_reg, sizeof(nome_reg), "linha/%d", l->pid);
	reg(nome_reg, id);
	bool marcada = l->pid == sel_pid;
	bool aberto = busca[0] || g_hash_table_contains(abertos, GINT_TO_POINTER(l->pid));
	/* o clique marca; o botão direito marca e abre o menu da linha (antes dos filhos: a seta ganha o toque que é dela) */
	if (gv_tocado(id)) {
		sel_pid = l->pid;
		sel_inicio = l->inicio;
		gv_app_redesenhar(app);
	}
	if (gv_menu_no_ponteiro(id, CLAY_ID("menu-linha"))) {
		sel_pid = l->pid;
		sel_inicio = l->inicio;
	}
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .padding = { 16 + (uint16_t)(l->profundidade * 24), 16, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = marcada ? gv_alfa(gv_t.acento, 0.18f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 } }) {
		Clay_ElementId seta = CLAY_IDI("seta", (uint32_t)l->pid);
		CLAY(seta, { .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (l->grupo && l->profundidade == 0) {
				gv_icone(aberto ? "chevron-down" : "chevron-up", 16, gv_t.texto_secundario);
			}
		}
		if (l->grupo && l->profundidade == 0 && gv_tocado(seta)) {
			if (aberto) {
				g_hash_table_remove(abertos, GINT_TO_POINTER(l->pid));
			} else {
				g_hash_table_add(abertos, GINT_TO_POINTER(l->pid));
			}
			montar_linhas();
		}
		gv_icone(l->grupo ? "stack" : l->infra || !l->meu ? "settings" : "app", 20, l->meu && !l->infra ? gv_t.acento : gv_t.texto_secundario);
		CLAY(CLAY_IDI("l-nome", (uint32_t)l->pid), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(l->nome), gv_t.tamanho_corpo, gv_t.texto, l->profundidade == 0 ? GV_PESO_MEDIO : GV_PESO_NORMAL);
			if (l->nproc > 1 && l->profundidade == 0) {
				gv_texto(gv_str(gv_fmt("(%d)", l->nproc).chars), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			if (l->estado == 'D' || l->estado == 'Z' || l->estado == 'T') {
				gv_texto(gv_str(l->estado == 'D' ? "esperando disco/driver" : l->estado == 'Z' ? "zumbi" : "parado"), gv_t.tamanho_legenda + 1, gv_t.aviso, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_IDI("l-pid", (uint32_t)l->pid), { .layout = { .sizing = { CLAY_SIZING_FIXED(90) } } }) {
			gv_texto(gv_str(gv_fmt("%d", l->pid).chars), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("l-cpu", (uint32_t)l->pid), { .layout = { .sizing = { CLAY_SIZING_FIXED(190) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			float f = (float)(l->cpu / 100.0 / (double)(sysconf(_SC_NPROCESSORS_ONLN) > 0 ? sysconf(_SC_NPROCESSORS_ONLN) : 1));
			f = f > 1 ? 1 : f < 0.02f && l->cpu > 0.1 ? 0.02f : f;
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(90), CLAY_SIZING_FIXED(10) } }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(5) }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_PERCENT(f), CLAY_SIZING_GROW(0) } }, .backgroundColor = roxo(1), .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
			}
			gv_texto(gv_str(gv_fmt("%.1f%%", l->cpu).chars), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("l-mem", (uint32_t)l->pid), { .layout = { .sizing = { CLAY_SIZING_FIXED(110) } } }) {
			gv_texto(gv_str(fmt_mem(l->memoria_kb)), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("l-prio", (uint32_t)l->pid), { .layout = { .sizing = { CLAY_SIZING_FIXED(110) } } }) {
			gv_texto(gv_str(NOMES_NIVEL[l->nivel]), gv_t.tamanho_corpo - 1, l->nivel == PR_MEDIA ? gv_t.texto_suave : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static void numero_resumo(const char *icone, const char *rotulo, const char *valor, float largura) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
		(void)largura;
		gv_icone(icone, 22, gv_t.texto_secundario);
		gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(gv_str(valor), gv_t.tamanho_corpo, roxo(1), GV_PESO_MEDIO);
	}
}

static void tela_processos(void) {
	CLAY(CLAY_ID("p-cima"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(CLAY_STRING("Processos"), 30, gv_t.texto, GV_PESO_MEDIO);
		}
		CLAY(CLAY_ID("p-busca-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(220) } } }) {
			if (gv_campo(CLAY_ID("p-busca"), busca, sizeof(busca), "Buscar processo", GV_CAMPO_BUSCA) & GV_CAMPO_MUDOU) {
				montar_linhas();
			}
		}
		reg("busca", CLAY_ID("p-busca"));
		CLAY(CLAY_ID("p-filtro-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(200) } } }) {
			if (gv_lista_suspensa(CLAY_ID("p-filtro"), FILTROS, 4, &filtro)) {
				montar_linhas();
			}
		}
		reg("filtro", CLAY_ID("p-filtro"));
		const struct linha *s = linha_de(sel_pid);
		if (gv_botao_ext(CLAY_ID("p-encerrar"), &(gv_botao_opcoes){ .texto = "Encerrar processo", .estilo = GV_BOTAO_CHEIO, .altura = 44, .desativado = !s || ocupado })) {
			pedir_encerrar(false);
		}
		reg("encerrar", CLAY_ID("p-encerrar"));
		if (gv_botao_ext(CLAY_ID("p-mais"), &(gv_botao_opcoes){ .icone = "chevron-down", .estilo = GV_BOTAO_ELEVADO, .altura = 44, .desativado = !s || ocupado })) {
			gv_popup_alternar(CLAY_ID("menu-acoes"));
		}
		reg("mais", CLAY_ID("p-mais"));
	}
	/* o jogo em execução: fechar na hora, sem procurar na lista */
	if (jogo.ativo) {
		CLAY(CLAY_ID("p-jogo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .padding = { 16, 12, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.acento, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_alfa(gv_t.acento, 0.5f), .width = GV_BORDA(1) } }) {
			gv_icone("gamepad", 26, gv_t.acento);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(gv_fmt("Jogo em execução: %s", jogo.nome).chars), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_str(gv_fmt("Steam %d · %d %s", jogo.appid, jogo.nproc, jogo.nproc == 1 ? "processo" : "processos").chars), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			if (gv_botao_ext(CLAY_ID("p-fechar-jogo"), &(gv_botao_opcoes){ .texto = "Fechar jogo", .icone = "close", .estilo = GV_BOTAO_CHEIO, .altura = 44, .desativado = ocupado })) {
				fechar_jogo();
			}
			reg("fechar-jogo", CLAY_ID("p-fechar-jogo"));
		}
	}
	/* os números do sistema */
	CLAY(CLAY_ID("p-resumo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(58) }, .padding = { 16, 16, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = gv_t.elevada,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		numero_resumo("cpu", "CPU", gv_fmt("%.0f%%", cpu_total).chars, 0);
		numero_resumo("memory", "Memória", gv_fmt("%.1f / %.0f GB", (double)mem_usada_kb / 1048576.0, (double)mem_total_kb / 1048576.0).chars, 0);
		numero_resumo("screens", "GPU", gpu_total >= 0 ? gv_fmt("%.0f%%", gpu_total).chars : "—", 0);
		numero_resumo("drive", "Discos", gv_fmt("%.0f%%", disco_total).chars, 0);
	}
	/* a tabela */
	CLAY(CLAY_ID("p-tabela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.elevada,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) }, .clip = { .vertical = true } }) {
		CLAY(CLAY_ID("p-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .padding = { 16 + 22 + 10 + 20 + 10, 16, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			cabecalho_coluna("col/nome", CLAY_ID("c-nome"), "Nome", 2, 0);
			cabecalho_coluna("col/pid", CLAY_ID("c-pid"), "PID", 3, 90);
			cabecalho_coluna("col/cpu", CLAY_ID("c-cpu"), "CPU", 0, 190);
			cabecalho_coluna("col/mem", CLAY_ID("c-mem"), "Memória", 1, 110);
			CLAY(CLAY_ID("c-prio"), { .layout = { .sizing = { CLAY_SIZING_FIXED(110) } } }) {
				gv_texto(CLAY_STRING("Prioridade"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_MEDIO);
			}
		}
		Clay_ElementId rol = CLAY_ID("p-rolagem");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
			for (int i = 0; i < n_linhas; i++) {
				linha_processo(&linhas[i]);
			}
			if (!n_linhas) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 30, 0 } } }) {
					gv_texto(CLAY_STRING("Nenhum processo com esses filtros."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
		}
	}
	CLAY(CLAY_ID("p-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		bool msg = mensagem[0] && gv_agora() < mensagem_ate;
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(msg ? mensagem : gv_fmt("%d itens exibidos · Atualização: %.0f s", n_linhas, INTERVALO_S).chars), gv_t.tamanho_corpo - 1, msg ? gv_t.texto : gv_t.texto_suave, GV_PESO_NORMAL);
		}
		if (ocupado) {
			gv_texto(CLAY_STRING("esperando a senha…"), gv_t.tamanho_corpo - 1, gv_t.aviso, GV_PESO_NORMAL);
		}
	}
	GV_POPUP(CLAY_ID("menu-acoes"), CLAY_ID("p-mais"), .alinhar = GV_ALINHAR_FIM, .largura = 300) {
		menu_acoes("acoes");
	}
	GV_POPUP(CLAY_ID("menu-linha"), gv_menu_ancora(CLAY_ID("menu-linha")), .lado = GV_POPUP_ABAIXO, .alinhar = GV_ALINHAR_INICIO, .largura = 300, .distancia = 2) {
		menu_acoes("linha");
	}
}

/* ---- Desempenho ---- */

struct graf {
	int a, b;        /* séries (b < 0: só uma) */
	double teto;     /* 0: automático (a maior amostra) */
	Clay_Color cor;
};

static void desenhar_grafico(cairo_t *cr, Clay_BoundingBox c, void *dados) {
	const struct graf *g = dados;
	double teto = g->teto;
	if (teto <= 0) {
		teto = 1024; /* ao menos 1 KB/s: sem tráfego a linha fica no chão em vez de ampliar ruído */
		for (int k = 0; k < HIST; k++) {
			for (int s = 0; s < 2; s++) {
				int idx = s ? g->b : g->a;
				if (idx >= 0 && hist[idx][k] > teto) {
					teto = hist[idx][k];
				}
			}
		}
	}
	cairo_save(cr);
	cairo_translate(cr, c.x, c.y);
	/* a grade */
	cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
	cairo_set_line_width(cr, 1);
	for (int i = 1; i < 4; i++) {
		double y = c.height * i / 4.0;
		cairo_move_to(cr, 0, y + 0.5);
		cairo_line_to(cr, c.width, y + 0.5);
	}
	cairo_stroke(cr);
	for (int s = 0; s < 2; s++) {
		int idx = s ? g->b : g->a;
		if (idx < 0) {
			continue;
		}
		int ini = HIST - hist_n;
		if (hist_n < 2) {
			continue;
		}
		double passo = c.width / (HIST - 1);
		for (int k = ini; k < HIST; k++) {
			double v = hist[idx][k] / teto;
			v = v > 1 ? 1 : v < 0 ? 0 : v;
			double x = k * passo, y = c.height - 2 - v * (c.height - 4);
			if (k == ini) {
				cairo_move_to(cr, x, y);
			} else {
				cairo_line_to(cr, x, y);
			}
		}
		double a = s ? 0.55 : 1;
		if (!s) { /* o preenchimento só da primeira série */
			cairo_path_t *caminho = cairo_copy_path(cr);
			cairo_line_to(cr, (HIST - 1) * passo, c.height);
			cairo_line_to(cr, ini * passo, c.height);
			cairo_close_path(cr);
			cairo_set_source_rgba(cr, g->cor.r / 255, g->cor.g / 255, g->cor.b / 255, 0.16);
			cairo_fill(cr);
			cairo_new_path(cr);
			cairo_append_path(cr, caminho);
			cairo_path_destroy(caminho);
		}
		cairo_set_source_rgba(cr, g->cor.r / 255, g->cor.g / 255, g->cor.b / 255, a);
		cairo_set_line_width(cr, 2);
		cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
		cairo_stroke(cr);
	}
	cairo_restore(cr);
}

static const char *fmt_taxa(double bps) {
	if (bps >= 1024.0 * 1024.0) {
		return gv_fmt("%.1f MB/s", bps / (1024.0 * 1024.0)).chars;
	}
	return gv_fmt("%.0f KB/s", bps / 1024.0).chars;
}

static void cartao_desempenho(const char *id_nome, const char *icone, const char *titulo, const char *valor, const char *detalhe, struct graf *g, uint32_t n) {
	CLAY(CLAY_IDI("d-cartao", n), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 10 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone(icone, 22, g->cor);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(titulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			}
			gv_texto(gv_str(valor), gv_t.tamanho_destaque + 2, g->cor, GV_PESO_MEDIO);
		}
		Clay_ElementId gid = CLAY_IDI("d-graf", n);
		CLAY(gid, gv_livre(desenhar_grafico, g, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_alfa(gv_t.superficie, 0.6f),
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) })) {}
		reg(id_nome, gid);
		gv_texto(gv_str(detalhe), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}

static void tela_desempenho(void) {
	static struct graf g_cpu = { H_CPU, -1, 100, { 139, 92, 246, 255 } }, g_mem = { H_MEM, -1, 100, { 56, 189, 248, 255 } }, g_gpu = { H_GPU, -1, 100, { 52, 211, 153, 255 } },
		g_disco = { H_DISCO, -1, 100, { 251, 191, 36, 255 } }, g_rede = { H_REDE_BAIXA, H_REDE_SOBE, 0, { 244, 114, 182, 255 } };
	CLAY(CLAY_ID("d-cima"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Desempenho"), 30, gv_t.texto, GV_PESO_MEDIO);
	}
	const double cpus = (double)(sysconf(_SC_NPROCESSORS_ONLN) > 0 ? sysconf(_SC_NPROCESSORS_ONLN) : 1);
	char det_cpu[96], det_mem[96], det_rede[96];
	snprintf(det_cpu, sizeof(det_cpu), "%d processos · %.0f núcleos lógicos · últimos %d s", lista.n, cpus, (int)(HIST * INTERVALO_S));
	snprintf(det_mem, sizeof(det_mem), "%.1f GB em uso de %.1f GB", (double)mem_usada_kb / 1048576.0, (double)mem_total_kb / 1048576.0);
	snprintf(det_rede, sizeof(det_rede), "Recebendo %s · Enviando %s", fmt_taxa(rede_baixa), fmt_taxa(rede_sobe));
	char v_cpu[16], v_mem[16], v_gpu[16], v_disco[16];
	snprintf(v_cpu, sizeof(v_cpu), "%.0f%%", cpu_total);
	snprintf(v_mem, sizeof(v_mem), "%.0f%%", mem_total_kb > 0 ? 100.0 * (double)mem_usada_kb / (double)mem_total_kb : 0.0);
	snprintf(v_gpu, sizeof(v_gpu), tem_gpu ? "%.0f%%" : "—", gpu_total);
	snprintf(v_disco, sizeof(v_disco), "%.0f%%", disco_total);
	CLAY(CLAY_ID("d-grade"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
		CLAY(CLAY_ID("d-linha1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
			cartao_desempenho("graf/cpu", "cpu", "CPU", v_cpu, det_cpu, &g_cpu, 0);
			cartao_desempenho("graf/mem", "memory", "Memória", v_mem, det_mem, &g_mem, 1);
		}
		CLAY(CLAY_ID("d-linha2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
			cartao_desempenho("graf/gpu", "screens", "GPU", v_gpu, tem_gpu ? "Uso da placa de vídeo" : "Sem leitura (a NVIDIA precisa do nvidia-smi)", &g_gpu, 2);
			cartao_desempenho("graf/disco", "drive", "Disco", v_disco, "Tempo ocupado do disco mais usado", &g_disco, 3);
		}
		CLAY(CLAY_ID("d-linha3"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
			cartao_desempenho("graf/rede", "network", "Rede", fmt_taxa(rede_baixa + rede_sobe), det_rede, &g_rede, 4);
		}
	}
}

static void dialogo_confirmar(void) {
	const struct linha *l = linha_de(confirmar_pid);
	CLAY(CLAY_ID("dlg"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("dlg-cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(500), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(24), .childGap = 14 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(gv_str(confirmar_forcar ? "Forçar o encerramento?" : "Encerrar este processo?"), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(l ? gv_fmt("\"%s\" (PID %d, de %s) é da sessão ou do sistema. Encerrá-lo pode derrubar aplicativos, a sessão ou o computador.", l->nome, l->pid, l->usuario).chars : "Este processo é da sessão ou do sistema."),
				gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY(CLAY_ID("dlg-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao_ext(CLAY_ID("dlg-nao"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
					confirmar = false;
				}
				reg("confirmar-nao", CLAY_ID("dlg-nao"));
				if (gv_botao_ext(CLAY_ID("dlg-sim"), &(gv_botao_opcoes){ .texto = confirmar_forcar ? "Forçar" : "Encerrar", .estilo = GV_BOTAO_CHEIO })) {
					confirmar = false;
					encerrar(confirmar_pid, confirmar_forcar, confirmar_arvore);
				}
				reg("confirmar-sim", CLAY_ID("dlg-sim"));
			}
		}
	}
}

static void desenhar(gv_app *a, void *dados) {
	(void)dados;
	app = a;
	if (!ultima_leitura || gv_agora() - ultima_leitura >= INTERVALO_S - 0.05) {
		ler_tudo();
	}
	gv_acordar_em(INTERVALO_S);
	/* clicar fora do cartão fecha. Antes de tudo: quem pergunta por último ganha o aperto, então a raiz perde para o cartão e o que há dentro dele */
	bool fora = gv_tocado(CLAY_ID("raiz"));
	(void)gv_premido(CLAY_ID("cartao"));
	if (fora && !confirmar) {
		gv_app_sair(app);
	}
	CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_ID("cartao"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_PERCENT(0.92f), CLAY_SIZING_PERCENT(0.9f) } }, .backgroundColor = gv_t.superficie,
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) }, .clip = { .vertical = true, .horizontal = true },
			.userData = gv_sombra(30, 10, 0.6f) }) {
			CLAY(CLAY_ID("barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(56) }, .padding = { 20, 12, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
				gv_icone("cpu", 22, gv_t.acento);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Gerenciador de processos"), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_botao_ext(CLAY_ID("fechar"), &(gv_botao_opcoes){ .icone = "close", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
					gv_app_sair(app);
				}
				reg("fechar", CLAY_ID("fechar"));
			}
			CLAY(CLAY_ID("corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
				CLAY(CLAY_ID("lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(220), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12), .childGap = 6 },
					.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
					static const struct { const char *id, *nome_reg, *icone, *texto; } itens[] = { { "t-processos", "aba/processos", "list", "Processos" }, { "t-desempenho", "aba/desempenho", "waves", "Desempenho" } };
					for (int t = 0; t < 2; t++) {
						Clay_ElementId id = CLAY_IDI("aba", (uint32_t)t);
						reg(itens[t].nome_reg, id);
						CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 14, 14, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
							.backgroundColor = tela == t ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
							gv_icone(itens[t].icone, 22, tela == t ? gv_t.texto : gv_t.texto_secundario);
							gv_texto(gv_str(itens[t].texto), gv_t.tamanho_corpo, tela == t ? gv_t.texto : gv_t.texto_secundario, tela == t ? GV_PESO_MEDIO : GV_PESO_NORMAL);
						}
						if (gv_tocado(id)) {
							tela = t;
						}
					}
				}
				CLAY(CLAY_ID("conteudo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 18, 14 }, .childGap = 14 } }) {
					if (tela == 1) {
						tela_desempenho();
					} else {
						tela_processos();
					}
				}
			}
		}
	}
	if (confirmar) {
		dialogo_confirmar();
	}
	/* Esc: fecha o diálogo, o menu, e por fim o gerenciador */
	if (gv_tecla() == 0xff1b) {
		if (confirmar) {
			confirmar = false;
		} else if (gv_popup_aberto(CLAY_ID("menu-acoes")) || gv_popup_aberto(CLAY_ID("menu-linha"))) {
			gv_popup_fechar(CLAY_ID("menu-acoes"));
			gv_popup_fechar(CLAY_ID("menu-linha"));
		} else if (busca[0] && gv_focado(CLAY_ID("p-busca"))) {
			busca[0] = '\0';
			montar_linhas();
		} else {
			gv_app_sair(app);
		}
	}
	debug_posicoes();
}

/* ---- o serviço residente e o chamador ---- */

static char *caminho_socket(void) {
	static char c[200];
	const char *rt = getenv("XDG_RUNTIME_DIR");
	snprintf(c, sizeof(c), "%s/lumna-processos.sock", rt && *rt ? rt : "/tmp");
	return c;
}

static char *caminho_pid(void) {
	static char c[200];
	const char *rt = getenv("XDG_RUNTIME_DIR");
	snprintf(c, sizeof(c), "%s/lumna-processos-janela.pid", rt && *rt ? rt : "/tmp");
	return c;
}

static bool viva(pid_t p) {
	return p > 1 && kill(p, 0) == 0;
}

static pid_t ler_pid_janela(void) {
	FILE *f = fopen(caminho_pid(), "r");
	int p = 0;
	if (f) {
		if (fscanf(f, "%d", &p) != 1) {
			p = 0;
		}
		fclose(f);
	}
	return viva((pid_t)p) ? (pid_t)p : 0;
}

static pid_t abrir_janela(char *argv0) {
	pid_t p = fork();
	if (p == 0) {
		char self[512];
		ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
		self[n > 0 ? n : 0] = '\0';
		execl(n > 0 ? self : argv0, "lumna-processos", "janela", (char *)NULL);
		_exit(127);
	}
	return p;
}

static int servico(char *argv0) {
	char resumo[200];
	pr_proteger_este_processo(resumo, sizeof(resumo));
	fprintf(stderr, "lumna-processos (serviço): %s\n", resumo);
	signal(SIGPIPE, SIG_IGN);
	int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", caminho_socket());
	unlink(addr.sun_path);
	if (s < 0 || bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(s, 4) < 0) {
		fprintf(stderr, "lumna-processos: não abri o socket: %s\n", strerror(errno));
		return 1;
	}
	chmod(addr.sun_path, 0600);
	pid_t janela = 0;
	for (;;) {
		struct pollfd pf = { .fd = s, .events = POLLIN };
		int r = poll(&pf, 1, 1000);
		if (janela > 0 && waitpid(janela, NULL, WNOHANG) == janela) {
			janela = 0;
		}
		if (r <= 0) {
			continue;
		}
		int c = accept4(s, NULL, NULL, SOCK_CLOEXEC);
		if (c < 0) {
			continue;
		}
		char cmd[32] = "";
		struct pollfd pc = { .fd = c, .events = POLLIN };
		if (poll(&pc, 1, 500) > 0) {
			ssize_t n = read(c, cmd, sizeof(cmd) - 1);
			cmd[n > 0 ? n : 0] = '\0';
		}
		close(c);
		bool aberta = janela > 0 && viva(janela);
		if (!strncmp(cmd, "alternar", 8)) {
			if (aberta) {
				kill(janela, SIGTERM);
			} else {
				janela = abrir_janela(argv0);
			}
		} else if (!strncmp(cmd, "mostrar", 7) && !aberta) {
			janela = abrir_janela(argv0);
		} else if (!strncmp(cmd, "fechar", 6) && aberta) {
			kill(janela, SIGTERM);
		}
	}
}

/* fala com o serviço; false se não há serviço */
static bool chamar_servico(const char *comando) {
	int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", caminho_socket());
	if (s < 0 || connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (s >= 0) {
			close(s);
		}
		return false;
	}
	ssize_t n = write(s, comando, strlen(comando));
	close(s);
	return n > 0;
}

static void ao_terminar(int sinal) {
	(void)sinal;
	_exit(0);
}

int main(int argc, char **argv) {
	const char *modo = argc > 1 ? argv[1] : "";
	if (!strcmp(modo, "servico")) {
		return servico(argv[0]);
	}
	if (!strcmp(modo, "fechar")) {
		return chamar_servico("fechar") ? 0 : 1;
	}
	if (strcmp(modo, "janela")) {
		/* o chamador: se há serviço, ele abre (ou fecha) a janela; sem serviço, esta própria instância vira a janela (e um segundo chamado a fecha) */
		if (chamar_servico("alternar")) {
			return 0;
		}
		pid_t ja = ler_pid_janela();
		if (ja && ja != getpid()) {
			kill(ja, SIGTERM);
			return 0;
		}
	}
	depurando = getenv("LUMNA_PROCESSOS_DEBUG") != NULL;
	if (depurando) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	signal(SIGTERM, ao_terminar); /* o serviço (ou o segundo chamado) fecha a janela assim */
	FILE *f = fopen(caminho_pid(), "w");
	if (f) {
		fprintf(f, "%d\n", getpid());
		fclose(f);
	}
	char resumo[200];
	pr_proteger_este_processo(resumo, sizeof(resumo)); /* sem o serviço, a janela também se protege (o serviço a chama já protegida: herda) */
	meu_uid = (int)getuid();
	abertos = g_hash_table_new(g_direct_hash, g_direct_equal);
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Gerenciador de processos", .app_id = "lumna-processos", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY, .ancoras = GV_ANCORA_TUDO,
		.zona_exclusiva = -1, .teclado = true, .teclado_exclusivo = true });
	if (!app) {
		return 1;
	}
	int r = gv_app_correr(app, desenhar, NULL);
	unlink(caminho_pid());
	pr_liberar(&lista);
	return r;
}
