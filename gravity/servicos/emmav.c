#include "emmav.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "emmab.h"

#define TAXA 16000
#define PEDACO 1600             /* 50 ms de áudio: 800 amostras de 16 bits */
#define PEDACO_MS 50
#define FIM_SILENCIO_MS 1300
#define SEM_FALA_MS 7000
#define MAXIMO_MS 25000

static volatile gint parar_agora, cancelar_agora, ouvindo;

/* ---- o detector ---- */

int emmav_detector_empurrar(struct emmav_detector *d, double volume) {
	d->passado_ms += PEDACO_MS;
	if (d->passado_ms <= 400) {
		d->piso = d->passado_ms == PEDACO_MS ? volume : MIN(d->piso, volume);
	}
	double limiar = MAX(d->piso * 2.5, 350.0);
	if (!d->comecou) {
		d->altos = volume > limiar ? d->altos + 1 : 0;
		if (d->altos >= 3) { /* 150 ms seguidos acima do ruído: começou */
			d->comecou = true;
			return EMMAV_FALANDO;
		}
		return d->passado_ms >= SEM_FALA_MS ? EMMAV_NADA : EMMAV_ESPERA;
	}
	d->silencio_ms = volume < limiar * 0.8 ? d->silencio_ms + PEDACO_MS : 0;
	return d->silencio_ms >= FIM_SILENCIO_MS || d->passado_ms >= MAXIMO_MS ? EMMAV_FIM : EMMAV_FALANDO;
}

static double rms(const guint8 *pcm, gsize n) {
	gsize amostras = n / 2;
	if (!amostras) {
		return 0;
	}
	double soma = 0;
	for (gsize i = 0; i < amostras; i++) {
		double v = (gint16)(pcm[2 * i] | (pcm[2 * i + 1] << 8));
		soma += v * v;
	}
	return sqrt(soma / (double)amostras);
}

bool emmav_wav_gravar(const char *caminho, const guint8 *pcm, gsize n) {
	GByteArray *a = g_byte_array_new();
	guint32 dados = (guint32)n, v32;
	guint16 v16;
#define P32(x) (v32 = GUINT32_TO_LE(x), g_byte_array_append(a, (guint8 *)&v32, 4))
#define P16(x) (v16 = GUINT16_TO_LE(x), g_byte_array_append(a, (guint8 *)&v16, 2))
	g_byte_array_append(a, (const guint8 *)"RIFF", 4);
	P32(36 + dados);
	g_byte_array_append(a, (const guint8 *)"WAVEfmt ", 8);
	P32(16);
	P16(1);
	P16(1);
	P32(TAXA);
	P32(TAXA * 2);
	P16(2);
	P16(16);
	g_byte_array_append(a, (const guint8 *)"data", 4);
	P32(dados);
	g_byte_array_append(a, pcm, (guint)n);
	bool ok = g_file_set_contents(caminho, (const char *)a->data, (gssize)a->len, NULL);
	g_byte_array_free(a, TRUE);
	return ok;
}

/* ---- o microfone ---- */

static GSubprocess *abrir_gravador(char **erro) {
	const char *teste = g_getenv("LUMNA_GRAVADOR");
	GError *e = NULL;
	if (teste && *teste) {
		char **argv = NULL;
		GSubprocess *p = NULL;
		if (g_shell_parse_argv(teste, NULL, &argv, NULL)) {
			p = g_subprocess_newv((const char *const *)argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &e);
		}
		g_strfreev(argv);
		g_clear_error(&e);
		if (!p && erro) {
			*erro = g_strdup("o gravador de teste não abriu");
		}
		return p;
	}
	static const char *const parec[] = { "parec", "--raw", "--format=s16le", "--rate=16000", "--channels=1", "--latency-msec=50", NULL };
	static const char *const arecord[] = { "arecord", "-q", "-t", "raw", "-f", "S16_LE", "-r", "16000", "-c", "1", NULL };
	const char *const *opcoes[] = { parec, arecord };
	bool achou = false;
	for (int i = 0; i < 2; i++) {
		char *bin = g_find_program_in_path(opcoes[i][0]);
		if (!bin) {
			continue;
		}
		g_free(bin);
		achou = true;
		GSubprocess *p = g_subprocess_newv(opcoes[i], G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE, &e);
		g_clear_error(&e);
		if (p) {
			return p;
		}
	}
	if (erro) {
		*erro = g_strdup(achou ? "o microfone não abriu; o PipeWire está rodando?" : "sem gravador de áudio (instale libpulse ou alsa-utils)");
	}
	return NULL;
}

char *emmav_ouvir(void (*nivel)(double, bool, void *), void *dados, char **erro) {
	if (!g_atomic_int_compare_and_exchange(&ouvindo, 0, 1)) {
		return NULL;
	}
	g_atomic_int_set(&parar_agora, 0);
	g_atomic_int_set(&cancelar_agora, 0);
	GSubprocess *p = abrir_gravador(erro);
	if (!p) {
		g_atomic_int_set(&ouvindo, 0);
		return NULL;
	}
	GInputStream *in = g_subprocess_get_stdout_pipe(p);
	GByteArray *pcm = g_byte_array_new();
	struct emmav_detector det = { 0 };
	guint8 pedaco[PEDACO];
	int fim = EMMAV_ESPERA, tique = 0;
	bool leu_algo = false;
	while (!g_atomic_int_get(&cancelar_agora)) {
		if (g_atomic_int_get(&parar_agora)) {
			fim = det.comecou ? EMMAV_FIM : EMMAV_NADA;
			break;
		}
		gsize lidos = 0;
		if (!g_input_stream_read_all(in, pedaco, PEDACO, &lidos, NULL, NULL) || lidos == 0) {
			break; /* o gravador acabou (ou o microfone caiu) */
		}
		leu_algo = true;
		g_byte_array_append(pcm, pedaco, (guint)lidos);
		double v = rms(pedaco, lidos);
		fim = emmav_detector_empurrar(&det, v);
		if (nivel && ++tique % 2 == 0) {
			nivel(MIN(v / 6000.0, 1.0), det.comecou, dados);
		}
		if (fim == EMMAV_FIM || fim == EMMAV_NADA) {
			break;
		}
	}
	g_subprocess_force_exit(p);
	g_subprocess_wait(p, NULL, NULL);
	g_object_unref(p);
	char *caminho = NULL;
	bool cancelado = g_atomic_int_get(&cancelar_agora);
	if (!leu_algo && erro && !*erro && !cancelado) {
		*erro = g_strdup("o microfone não entregou áudio; o PipeWire está rodando?");
	}
	if (!cancelado && fim != EMMAV_NADA && det.comecou && pcm->len > 0) {
		caminho = g_build_filename(g_get_user_runtime_dir(), "lumna-emma-fala.wav", NULL);
		if (!emmav_wav_gravar(caminho, pcm->data, pcm->len)) {
			g_free(caminho);
			caminho = NULL;
		}
	}
	g_byte_array_free(pcm, TRUE);
	g_atomic_int_set(&ouvindo, 0);
	return caminho;
}

void emmav_ouvir_parar(bool cancelar) {
	g_atomic_int_set(cancelar ? &cancelar_agora : &parar_agora, 1);
}

bool emmav_ouvindo(void) {
	return g_atomic_int_get(&ouvindo);
}

/* ---- a fala ---- */

struct frase {
	gint geracao;
	char *texto;
	char *arquivo;
};

static GAsyncQueue *fila_texto, *fila_pronta;
static volatile gint geracao_atual, pendentes;
static GMutex tocador_trinco;
static GSubprocess *tocador;
static void (*estado_cb)(const char *, void *);
static void *estado_dados;
static volatile gint contador;

static void avisar(const char *e) {
	if (estado_cb) {
		estado_cb(e, estado_dados);
	}
}

/* um problema ao falar: guardado para o Painel mostrar, escrito no terminal e avisado na tela (uma vez por minuto, para não encher) */
static GMutex erro_trinco;
static char *ultimo_erro;
static gint64 erro_avisado_em;

static void registrar_erro(const char *msg) {
	g_mutex_lock(&erro_trinco);
	g_free(ultimo_erro);
	ultimo_erro = g_strdup(msg);
	gint64 agora = g_get_monotonic_time();
	bool avisar_tela = agora - erro_avisado_em > 60 * G_USEC_PER_SEC;
	if (avisar_tela) {
		erro_avisado_em = agora;
	}
	g_mutex_unlock(&erro_trinco);
	fprintf(stderr, "lumna-emma: voz: %s\n", msg);
	if (avisar_tela && !g_getenv("LUMNA_EMMA_ESPERA_TESTE")) {
		char *corpo = g_strdup_printf("%s\nVeja Painel > Emma > Voz.", msg);
		char *argv[] = { "notify-send", "-a", "Emma", "A Emma não conseguiu falar", corpo, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
		g_free(corpo);
	}
}

char *emmav_ultimo_erro(void) {
	g_mutex_lock(&erro_trinco);
	char *e = g_strdup(ultimo_erro);
	g_mutex_unlock(&erro_trinco);
	return e;
}

void emmav_erro_limpar(void) {
	g_mutex_lock(&erro_trinco);
	g_clear_pointer(&ultimo_erro, g_free);
	g_mutex_unlock(&erro_trinco);
}

static void terminou(void) {
	if (g_atomic_int_dec_and_test(&pendentes)) {
		avisar("calada");
	}
}

static bool geracao_vale(gint g) {
	return g_atomic_int_get(&geracao_atual) == g;
}

static void tocar(const char *arquivo, gint geracao) {
	const char *teste = g_getenv("LUMNA_TOCADOR");
	char **argv = NULL;
	GSubprocess *p = NULL;
	GSubprocessLauncher *l = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
	/* "Emma" no mixer de áudio */
	g_subprocess_launcher_setenv(l, "PIPEWIRE_PROPS", "{ application.name = \"Emma\" media.name = \"Emma\" }", TRUE);
	g_subprocess_launcher_setenv(l, "PULSE_PROP", "application.name=Emma", TRUE);
	GError *e = NULL;
	if (teste && *teste) {
		char *cmd = g_strdup_printf("%s %s", teste, arquivo);
		if (g_shell_parse_argv(cmd, NULL, &argv, NULL)) {
			p = g_subprocess_launcher_spawnv(l, (const char *const *)argv, &e);
		}
		g_free(cmd);
	} else {
		const char *bins[] = { "pw-play", "paplay" };
		for (int i = 0; i < 2 && !p; i++) {
			char *b = g_find_program_in_path(bins[i]);
			if (b) {
				const char *av[] = { bins[i], arquivo, NULL };
				p = g_subprocess_launcher_spawnv(l, av, &e);
				g_clear_error(&e);
				g_free(b);
			}
		}
	}
	g_strfreev(argv);
	g_clear_error(&e);
	g_object_unref(l);
	if (!p) {
		registrar_erro("não achei o pw-play nem o paplay para tocar o som (instale o pipewire-pulse ou o libpulse)");
		return;
	}
	g_mutex_lock(&tocador_trinco);
	tocador = p;
	g_mutex_unlock(&tocador_trinco);
	g_subprocess_wait(p, NULL, NULL); /* emmav_calar() mata o tocador e o wait volta */
	if (g_subprocess_get_if_exited(p)) {
		int st = g_subprocess_get_exit_status(p);
		if (st != 0) {
			char *m = g_strdup_printf("o tocador de som saiu com erro %d (sem saída de áudio, ou o servidor de som não está ligado)", st);
			registrar_erro(m);
			g_free(m);
		} else {
			emmav_erro_limpar();
		}
	}
	(void)geracao;
	g_mutex_lock(&tocador_trinco);
	tocador = NULL;
	g_mutex_unlock(&tocador_trinco);
	g_object_unref(p);
}

static gpointer buscar_linha(gpointer d) {
	(void)d;
	for (;;) {
		struct frase *f = g_async_queue_pop(fila_texto);
		/* no máximo duas frases prontas à espera de tocar: gerar tudo adiantado enche a pasta de áudio e, numa resposta longa, a pessoa
		 * ouve a primeira frase e só depois de um tempo as outras. Se a conversa foi cortada (emmav_calar), larga a espera. */
		while (g_async_queue_length(fila_pronta) >= 2 && geracao_vale(f->geracao)) {
			g_usleep(80000);
		}
		if (geracao_vale(f->geracao)) {
			/* um nome só para cada frase: com poucos nomes que se repetiam, a 5ª frase escrevia por cima do arquivo da 1ª, que ainda
			 * esperava a vez, e a 1ª ao terminar apagava o da 5ª: o tocador saía com erro 1 no meio da fala */
			f->arquivo = g_strdup_printf("%s/lumna-emma-voz-%d-%d.audio", g_get_user_runtime_dir(), (int)getpid(), g_atomic_int_add(&contador, 1));
			char *erro = NULL;
			if (!emmab_tts(f->texto, f->arquivo, &erro)) {
				g_free(f->arquivo);
				f->arquivo = NULL;
				registrar_erro(erro && *erro ? erro : "o servidor da Emma não gerou a voz (veja Painel > Emma > Voz: o motor escolhido precisa estar instalado ou ter chave)");
				g_free(erro);
			}
		}
		g_free(f->texto);
		f->texto = NULL;
		g_async_queue_push(fila_pronta, f);
	}
	return NULL;
}

static gpointer tocar_linha(gpointer d) {
	(void)d;
	for (;;) {
		struct frase *f = g_async_queue_pop(fila_pronta);
		if (f->arquivo) {
			if (geracao_vale(f->geracao)) {
				avisar("falando");
				tocar(f->arquivo, f->geracao);
			}
			g_unlink(f->arquivo);
			g_free(f->arquivo);
		}
		terminou();
		g_free(f);
	}
	return NULL;
}

void emmav_falar_iniciar(void (*estado)(const char *, void *), void *dados) {
	estado_cb = estado;
	estado_dados = dados;
	fila_texto = g_async_queue_new();
	fila_pronta = g_async_queue_new();
	g_thread_unref(g_thread_new("emma-voz-busca", buscar_linha, NULL));
	g_thread_unref(g_thread_new("emma-voz-toca", tocar_linha, NULL));
}

void emmav_falar(const char *texto) {
	if (!fila_texto || !texto || !*texto) {
		return;
	}
	struct frase *f = g_new0(struct frase, 1);
	f->geracao = g_atomic_int_get(&geracao_atual);
	f->texto = g_strdup(texto);
	g_atomic_int_inc(&pendentes);
	g_async_queue_push(fila_texto, f);
}

void emmav_calar(void) {
	g_atomic_int_inc(&geracao_atual);
	g_mutex_lock(&tocador_trinco);
	if (tocador) {
		g_subprocess_force_exit(tocador);
	}
	g_mutex_unlock(&tocador_trinco);
}

bool emmav_falando(void) {
	return g_atomic_int_get(&pendentes) > 0;
}
