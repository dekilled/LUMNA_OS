/*
 * lumna-abertura: a animação que toca depois do login, por cima de tudo, enquanto o desktop termina de subir (a sessão a chama primeiro, antes da barra,
 * da dock e do papel de parede). O vídeo é um arquivo só (/usr/share/lumna/abertura/abertura.mp4, montado por iso/branding/abertura/gera.sh) tocado pelo
 * player próprio do LUMNA (video_player.c, libmpv por software) com a linha do tempo de servicos/abertura_tempo.c:
 *   começo (as letras e o sussurro da Emma) -> um laço lento (a "respiração") até o desktop ficar pronto -> o fim (distorção e preto) -> o preto
 *   esmaece e aparece o desktop.
 *   - o desktop está pronto quando a barra, a dock e o papel de parede estão na tela (o Orbit diz: orbit-msg camadas); passados 20 s sai assim mesmo;
 *   - qualquer tecla ou clique (depois de 1 s) pula direto para o fim;
 *   - cada tela é um processo (o da principal abre os outros, como o papel de parede); todos seguem o mesmo relógio, então ficam juntos; só o da
 *     principal toca o som;
 *   - desligar: ~/.config/lumna/animacoes.ini [geral] abertura = nao (o Painel > Animações); abertura_som = nao toca sem o som.
 *   ./abertura [--saida NOME]
 * Para os testes: LUMNA_ABERTURA_DIR (onde está abertura.mp4/abertura.ini), LUMNA_ABERTURA_CAMADAS (os nomes a esperar, separados por vírgula; vazio: não espera),
 * LUMNA_ABERTURA_AO=null (sem som), LUMNA_ABERTURA_LOG=1 (conta o que acontece em stderr), LUMNA_ABERTURA_PRONTO_EM=SEGUNDOS (fica pronto nesse instante).
 */
#include <cairo.h>
#include <glib.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "abertura_tempo.h"
#include "gravity.h"
#include "modos.h"
#include "video_player.h"

#define MAX_FILHAS 7
#define LIMITE_PRONTO 20.0   /* s: passou disso, sai mesmo sem a barra, a dock e o papel */
#define ESMAECER 0.35        /* s: o preto some e aparece o desktop */
#define SO_PULA_DEPOIS 1.0   /* s: uma tecla do login ainda pingando não pula */

static gv_app *app_ab;
static const char *minha_saida;  /* NULL: a da principal (abre as outras) */
static bool filha;
static vp *player;
static ab_tempos tempos;
static ab_eventos eventos = { -1, -1 };
static double t0;                /* CLOCK_MONOTONIC em que a animação começou (o mesmo em todas as telas) */
static double ultimo_alvo = -1, ultimo_e = -1, ultimo_seek;
static double acabou_em = -1;
static char arquivo[1024], estado_ini[600];
static time_t estado_mtime;
static long estado_mtime_ns;
static bool com_log, abriu;
static uint8_t *quadro_dados;
static cairo_surface_t *quadro_surf;
static int quadro_w, quadro_h;
static double proxima_olhada;    /* a próxima consulta ao Orbit (as camadas prontas) */
static const char *esperar[8];
static int nesperar;
static char esperar_buf[200];

static struct {
	char saida[64];
	pid_t pid;
} filhas[MAX_FILHAS];
static int nfilhas;

static double mono(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void log_(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void log_(const char *fmt, ...) {
	if (!com_log) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "abertura[%s %.2f]: ", minha_saida ? minha_saida : "principal", mono() - t0);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* ~/.config/lumna/animacoes.ini: abertura e abertura_som (sim por padrão) */
static void ajustes(bool *abertura, bool *som) {
	*abertura = *som = true;
	const char *base = getenv("XDG_CONFIG_HOME");
	char caminho[600];
	if (base && *base) {
		snprintf(caminho, sizeof(caminho), "%s/lumna/animacoes.ini", base);
	} else {
		snprintf(caminho, sizeof(caminho), "%s/.config/lumna/animacoes.ini", getenv("HOME") ? getenv("HOME") : "");
	}
	char *texto = NULL;
	if (!g_file_get_contents(caminho, &texto, NULL, NULL)) {
		return;
	}
	char **linhas = g_strsplit(texto, "\n", -1);
	for (char **l = linhas; *l; l++) {
		char chave[64], valor[64];
		if (sscanf(*l, " %63[^= ] = %63s", chave, valor) != 2) {
			continue;
		}
		bool sim = strcmp(valor, "nao") && strcmp(valor, "não") && strcmp(valor, "0");
		if (!strcmp(chave, "abertura")) {
			*abertura = sim;
		} else if (!strcmp(chave, "abertura_som")) {
			*som = sim;
		}
	}
	g_strfreev(linhas);
	g_free(texto);
}

/* o estado compartilhado entre as telas: quando começou, quando ficou pronto, quando pularam */
static void estado_gravar(void) {
	char tmp[640];
	snprintf(tmp, sizeof(tmp), "%s.tmp%d", estado_ini, (int)getpid());
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	fprintf(f, "t0 = %.6f\npronto = %.6f\npulo = %.6f\n", t0, eventos.pronto, eventos.pulo);
	fclose(f);
	rename(tmp, estado_ini);
}

static bool estado_ler(bool so_se_mudou) {
	struct stat st;
	if (stat(estado_ini, &st) != 0) {
		return false;
	}
	if (so_se_mudou && st.st_mtim.tv_sec == estado_mtime && st.st_mtim.tv_nsec == estado_mtime_ns) {
		return false;
	}
	FILE *f = fopen(estado_ini, "r");
	if (!f) {
		return false;
	}
	estado_mtime = st.st_mtim.tv_sec;
	estado_mtime_ns = st.st_mtim.tv_nsec;
	char linha[120], chave[32];
	double v, nt0 = -1, np = -1, nu = -1;
	while (fgets(linha, sizeof(linha), f)) {
		if (sscanf(linha, " %31[^= ] = %lf", chave, &v) == 2) {
			if (!strcmp(chave, "t0")) {
				nt0 = v;
			} else if (!strcmp(chave, "pronto")) {
				np = v;
			} else if (!strcmp(chave, "pulo")) {
				nu = v;
			}
		}
	}
	fclose(f);
	if (nt0 > 0) {
		t0 = nt0;
	}
	eventos.pronto = np;
	eventos.pulo = nu;
	return true;
}

/* ---- o desktop está pronto? (a principal pergunta ao Orbit) ---- */

static bool camada_mapeada(const char *resposta, const char *nome) {
	char chave[100];
	snprintf(chave, sizeof(chave), "\"nome\":\"%s\"", nome);
	const char *p = resposta;
	while ((p = strstr(p, chave))) {
		const char *fim = strchr(p, '}');
		const char *m = strstr(p, "\"mapeada\":true");
		if (m && fim && m < fim) {
			return true;
		}
		p += strlen(chave);
	}
	return false;
}

static bool desktop_pronto(void) {
	const char *forcado = getenv("LUMNA_ABERTURA_PRONTO_EM"); /* só para os testes: fica pronto neste instante (s desde o começo) */
	if (forcado && *forcado) {
		return mono() - t0 >= atof(forcado);
	}
	if (nesperar == 0) {
		return true;
	}
	char r[16384];
	if (!orbit_pedido("camadas", r, sizeof(r))) {
		return false;
	}
	for (int i = 0; i < nesperar; i++) {
		if (!camada_mapeada(r, esperar[i])) {
			return false;
		}
	}
	return true;
}

/* ---- uma abertura por tela ---- */

static void filha_abrir(const char *saida) {
	pid_t p = fork();
	if (p == 0) {
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		execl("/proc/self/exe", "abertura", "--saida", saida, (char *)NULL);
		_exit(127);
	}
	if (p > 0 && nfilhas < MAX_FILHAS) {
		snprintf(filhas[nfilhas].saida, sizeof(filhas[0].saida), "%s", saida);
		filhas[nfilhas++].pid = p;
	}
}

static void filhas_rever(void) {
	const char *saidas[8];
	int n = gv_saidas(app_ab, saidas, 8);
	for (int i = 0; i < nfilhas; i++) {
		bool existe = false;
		for (int k = 1; k < n; k++) {
			existe |= !strcmp(saidas[k], filhas[i].saida);
		}
		bool morreu = waitpid(filhas[i].pid, NULL, WNOHANG) == filhas[i].pid;
		if (!existe || morreu) {
			if (!morreu) {
				kill(filhas[i].pid, SIGTERM);
				waitpid(filhas[i].pid, NULL, 0);
			}
			filhas[i--] = filhas[--nfilhas];
		}
	}
	if (acabou_em >= 0) {
		return; /* já acabando: não abre mais telas */
	}
	for (int k = 1; k < n; k++) {
		bool tem = false;
		for (int i = 0; i < nfilhas; i++) {
			tem |= !strcmp(filhas[i].saida, saidas[k]);
		}
		if (!tem) {
			filha_abrir(saidas[k]);
		}
	}
}

/* ---- o desenho ---- */

static gint acordando;

static void acordou_na_principal(void *d) {
	(void)d;
	g_atomic_int_set(&acordando, 0);
	gv_app_redesenhar(app_ab);
}

static void acorda(void *d) {
	(void)d;
	if (g_atomic_int_compare_and_exchange(&acordando, 0, 1)) { /* um aviso por quadro, não um por evento */
		gv_na_linha_principal(acordou_na_principal, NULL);
	}
}

static void pintar(cairo_t *cr, Clay_BoundingBox caixa, void *dados) {
	(void)dados;
	double alfa = 1.0;
	if (acabou_em >= 0) {
		alfa = 1.0 - (mono() - acabou_em) / ESMAECER;
		alfa = alfa < 0 ? 0 : alfa;
	}
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, alfa); /* o preto, que esmaece no fim */
	cairo_paint(cr);
	cairo_restore(cr);
	if (quadro_surf && acabou_em < 0) {
		cairo_save(cr);
		cairo_translate(cr, caixa.x, caixa.y);
		cairo_scale(cr, caixa.width / quadro_w, caixa.height / quadro_h);
		cairo_set_source_surface(cr, quadro_surf, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	double agora = mono();
	if (!filha && gv_saidas_mudaram(app)) {
		filhas_rever();
	}
	/* os outros dizem (por arquivo) quando ficou pronto e quando pularam */
	if (filha || eventos.pronto < 0 || eventos.pulo < 0) {
		estado_ler(true);
	}
	double e = agora - t0;
	/* a principal: ficou pronto? passou do limite? alguém pulou? */
	if (!filha) {
		bool mudou = false;
		if (eventos.pronto < 0 && agora >= proxima_olhada) {
			proxima_olhada = agora + 0.15;
			if (desktop_pronto() || e >= LIMITE_PRONTO) {
				eventos.pronto = e;
				mudou = true;
				log_("o desktop está pronto");
			}
		}
		if (eventos.pulo < 0 && e >= SO_PULA_DEPOIS && (gv_tecla() != 0 || gv_tocado(CLAY_ID("raiz")))) {
			eventos.pulo = e;
			mudou = true;
			log_("pulou");
		}
		if (mudou) {
			estado_gravar();
		}
	} else if (eventos.pulo < 0 && e >= SO_PULA_DEPOIS && (gv_tecla() != 0 || gv_tocado(CLAY_ID("raiz")))) {
		/* o clique pode ter sido nesta tela: avisa as outras pelo mesmo arquivo */
		eventos.pulo = e;
		estado_gravar();
	}
	bool acabou = false;
	double alvo = ab_posicao(&tempos, &eventos, e, &acabou);
	if (!abriu) {
		abriu = true;
		vp_abrir(player, arquivo, alvo > 0.5 ? alvo : 0, false, 100);
		ultimo_alvo = alvo;
		ultimo_e = e;
		ultimo_seek = agora;
	} else if (!acabou) {
		/* salto na linha do tempo (o laço voltou, ou saiu do laço, ou pulou): busca; no ritmo normal o player anda sozinho */
		double previsto = ultimo_alvo + (e - ultimo_e);
		vp_estado est;
		vp_atualizar(player, &est);
		bool salto = fabs(alvo - previsto) > 0.08;
		bool solto = !est.carregando && fabs(est.pos - alvo) > 0.6 && agora - ultimo_seek > 1.0; /* ficou para trás (máquina lenta) ou à frente */
		if (salto || solto) {
			vp_buscar(player, alvo);
			ultimo_seek = agora;
			log_("busca %.3f (%s)", alvo, salto ? "salto" : "ressincroniza");
		}
		ultimo_alvo = alvo;
		ultimo_e = e;
	}
	if (acabou && acabou_em < 0) {
		acabou_em = agora;
		vp_pausar(player, true);
		log_("acabou o vídeo; esmaecendo");
	}
	if (acabou_em >= 0 && agora - acabou_em >= ESMAECER) {
		log_("saindo");
		gv_app_sair(app);
		return;
	}
	/* o quadro do player (no tamanho da tela) */
	Clay_BoundingBox cx = gv_caixa(CLAY_ID("raiz"));
	int w = (int)cx.width, h = (int)cx.height;
	if (w > 8 && h > 8 && acabou_em < 0) {
		uint8_t *d;
		int st;
		bool mudou = vp_quadro(player, w, h, &d, &st);
		if (mudou || !quadro_surf || d != quadro_dados || w != quadro_w || h != quadro_h) {
			if (quadro_surf) {
				cairo_surface_destroy(quadro_surf);
			}
			quadro_surf = cairo_image_surface_create_for_data(d, CAIRO_FORMAT_RGB24, w, h, st);
			quadro_dados = d;
			quadro_w = w;
			quadro_h = h;
		}
		if (mudou) {
			cairo_surface_mark_dirty(quadro_surf);
		}
	}
	CLAY(CLAY_ID("raiz"), gv_livre(pintar, NULL, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
	gv_acordar_em(acabou_em >= 0 ? 0.016 : 0.033);
}

int main(int argc, char **argv) {
	com_log = getenv("LUMNA_ABERTURA_LOG") != NULL;
	if (argc >= 3 && !strcmp(argv[1], "--saida")) {
		minha_saida = argv[2];
		filha = true;
	}
	bool ligada, som;
	ajustes(&ligada, &som);
	if (!ligada && !filha) {
		return 0; /* desligada no Painel */
	}
	const char *dir = getenv("LUMNA_ABERTURA_DIR");
	if (!dir || !*dir) {
		dir = "/usr/share/lumna/abertura";
	}
	char ini[600];
	snprintf(arquivo, sizeof(arquivo), "%s/abertura.mp4", dir);
	snprintf(ini, sizeof(ini), "%s/abertura.ini", dir);
	if (access(arquivo, R_OK) != 0 || !ab_ler_tempos(ini, &tempos)) {
		fprintf(stderr, "abertura: não achei %s e %s (o pacote lumna-gravity as instala)\n", arquivo, ini);
		return 0; /* sem os arquivos não há o que mostrar: o desktop segue */
	}
	const char *rt = getenv("XDG_RUNTIME_DIR");
	snprintf(estado_ini, sizeof(estado_ini), "%s/lumna-abertura.ini", rt && *rt ? rt : "/tmp");
	const char *camadas = getenv("LUMNA_ABERTURA_CAMADAS");
	snprintf(esperar_buf, sizeof(esperar_buf), "%s", camadas ? camadas : "lumna-barra,lumna-dock,lumna-papel");
	for (char *p = strtok(esperar_buf, ","); p && nesperar < 8; p = strtok(NULL, ",")) {
		if (*p) {
			esperar[nesperar++] = p;
		}
	}
	if (!filha) {
		t0 = mono();
		estado_gravar();
	} else {
		t0 = mono();
		for (int i = 0; i < 20 && !estado_ler(false); i++) {
			g_usleep(50000); /* a principal grava logo; sem arquivo, esta tela segue o próprio relógio */
		}
	}
	if (filha || !som || getenv("LUMNA_ABERTURA_AO")) {
		setenv("LUMNA_GALERIA_AO", filha || !som ? "null" : getenv("LUMNA_ABERTURA_AO"), 1);
	}
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Abertura", .app_id = "lumna-abertura", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_TUDO, .zona_exclusiva = -1 /* por cima da barra e da dock */, .teclado_exclusivo = !filha,
		.saida = minha_saida ? minha_saida : "@principal" });
	if (!app) {
		return 1;
	}
	app_ab = app;
	player = vp_criar(acorda, NULL);
	if (!player) {
		return 1;
	}
	if (!filha) {
		filhas_rever();
	}
	log_("começa (%s)", arquivo);
	return gv_app_correr(app, desenhar, NULL);
}
