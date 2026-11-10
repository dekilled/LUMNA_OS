#include "musica_player.h"

#include <errno.h>
#include <stdio.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "json.h"

typedef struct deck {
	struct mp_player *p;
	GPid pid;
	int fd;
	char sock[256];
	GThread *leitor;
	bool vivo;
	/* o que o mpv conta (sob p->m) */
	double pos, dur;
	gint64 pos_em;
	bool pausado, carregada, eof_com_proxima;
	char caminho[4096];
	char proxima_anexada[4096];
	int req;
	double seek_pend;   /* ir para aqui quando o arquivo carregar */
	/* a resposta de uma consulta */
	int resp_id;
	char resp[512];
	bool resp_ok;
} deck;

struct mp_player {
	GMutex ctl;         /* serializa o que mexe nos decks (a API e o monitor); o m protege só os campos */
	GMutex m;
	GCond c;
	void (*evento)(int, void *);
	void *dados;
	deck *a;            /* o que toca */
	deck *b;            /* o da transição, preparado ou entrando */
	char saida[256];
	bool normalizar, sem_intervalos;
	int transicao, volume;
	char proxima[4096]; /* a seguinte (para a transição) */
	gint monitor_vivo;
	GThread *monitor;
	bool b_entrando;
	bool pausado_geral;
	int n_decks;
};

static void avisar(mp_player *p, int tipo) {
	if (p->evento) {
		p->evento(tipo, p->dados);
	}
}

/* ---- falar com um mpv ---- */

static bool enviar(deck *d, const char *json) {
	if (g_getenv("LUMNA_MUSICA_MPVDBG")) {
		fprintf(stderr, "mpv>> %s\n", json);
	}
	if (d->fd < 0) {
		return false;
	}
	size_t n = strlen(json);
	char *linha = g_strdup_printf("%s\n", json);
	ssize_t r = write(d->fd, linha, n + 1);
	g_free(linha);
	return r == (ssize_t)(n + 1);
}

static void comando(deck *d, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static void comando(deck *d, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char *c = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	char *j = g_strdup_printf("{\"command\":[%s]}", c);
	enviar(d, j);
	g_free(j);
	g_free(c);
}

static char *js(const char *s) {
	GString *g = g_string_new(NULL);
	json_escrever_texto(g, s);
	return g_string_free(g, FALSE);
}

static void tratar_linha(deck *d, const char *linha) {
	if (g_getenv("LUMNA_MUSICA_MPVDBG")) {
		fprintf(stderr, "mpv<< %s\n", linha);
	}
	if (getenv("MPDBG")) fprintf(stderr, "<< %s\n", linha);
	struct json *j = json_ler(linha);
	if (!j) {
		return;
	}
	mp_player *p = d->p;
	const char *ev = json_chave_texto(j, "event");
	if (!*ev) {
		/* a resposta de um pedido com request_id */
		const struct json *id = json_chave(j, "request_id");
		if (id) {
			g_mutex_lock(&p->m);
			d->resp_id = (int)json_numero(id, 0);
			d->resp_ok = !strcmp(json_chave_texto(j, "error"), "success");
			const struct json *dado = json_chave(j, "data");
			d->resp[0] = 0;
			if (dado && dado->tipo == JSON_NUMERO) {
				g_snprintf(d->resp, sizeof(d->resp), "%.6f", dado->n);
			} else if (dado && dado->tipo == JSON_TEXTO) {
				g_strlcpy(d->resp, dado->s, sizeof(d->resp));
			} else if (dado && dado->tipo == JSON_BOOL) {
				g_strlcpy(d->resp, dado->b ? "yes" : "no", sizeof(d->resp));
			}
			g_cond_broadcast(&p->c);
			g_mutex_unlock(&p->m);
		}
	} else if (!strcmp(ev, "property-change")) {
		const char *nome = json_chave_texto(j, "name");
		const struct json *dado = json_chave(j, "data");
		bool muda_estado = false;
		g_mutex_lock(&p->m);
		if (!strcmp(nome, "time-pos") && dado && dado->tipo == JSON_NUMERO) {
			d->pos = dado->n;
			d->pos_em = g_get_monotonic_time();
		} else if (!strcmp(nome, "duration") && dado && dado->tipo == JSON_NUMERO) {
			d->dur = dado->n;
		} else if (!strcmp(nome, "pause") && dado) {
			d->pausado = dado->b;
			d->pos_em = g_get_monotonic_time();
			muda_estado = d == p->a;
		}
		g_mutex_unlock(&p->m);
		if (muda_estado) {
			avisar(p, MP_EV_ESTADO);
		}
	} else if (!strcmp(ev, "file-loaded")) {
		bool avancou = false;
		g_mutex_lock(&p->m);
		d->carregada = true;
		if (d->seek_pend > 0) {
			comando(d, "\"seek\",%.3f,\"absolute\"", d->seek_pend);
			d->seek_pend = 0;
		}
		if (d->eof_com_proxima) { /* o gapless emendou na que estava anexada */
			d->eof_com_proxima = false;
			g_strlcpy(d->caminho, d->proxima_anexada, sizeof(d->caminho));
			d->proxima_anexada[0] = 0;
			d->pos = 0;
			d->pos_em = g_get_monotonic_time();
			avancou = true;
		}
		g_mutex_unlock(&p->m);
		if (avancou) {
			avisar(p, MP_EV_AVANCOU);
		}
	} else if (!strcmp(ev, "end-file")) {
		const char *motivo = json_chave_texto(j, "reason");
		if (!strcmp(motivo, "eof")) {
			bool emenda;
			g_mutex_lock(&p->m);
			emenda = d->proxima_anexada[0] != 0 && d == p->a;
			d->eof_com_proxima = emenda;
			bool em_transicao = d == p->a && p->b != NULL && p->proxima[0] != 0 && p->transicao > 0; /* o monitor faz a troca */
			if (em_transicao) {
				d->carregada = false;
				d->pos = d->dur;
			} else if (!emenda) {
				d->carregada = false;
				d->pos = d->dur;
			}
			bool eh_b = d == p->b;
			g_mutex_unlock(&p->m);
			if (!emenda && !eh_b && !em_transicao) {
				avisar(p, MP_EV_FIM);
			}
		} else if (!strcmp(motivo, "error")) {
			g_mutex_lock(&p->m);
			d->carregada = false;
			g_mutex_unlock(&p->m);
			avisar(p, MP_EV_ERRO);
		}
	}
	json_liberar(j);
}

static gpointer ler_thread(gpointer arg) {
	deck *d = arg;
	GString *buf = g_string_new(NULL);
	char tmp[4096];
	for (;;) {
		ssize_t r = read(d->fd, tmp, sizeof(tmp));
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r <= 0) {
			break;
		}
		g_string_append_len(buf, tmp, r);
		char *nl;
		while ((nl = memchr(buf->str, '\n', buf->len))) {
			*nl = 0;
			tratar_linha(d, buf->str);
			g_string_erase(buf, 0, (nl - buf->str) + 1);
		}
	}
	g_string_free(buf, TRUE);
	bool era_a;
	g_mutex_lock(&d->p->m);
	d->vivo = false;
	era_a = d == d->p->a;
	g_cond_broadcast(&d->p->c);
	g_mutex_unlock(&d->p->m);
	if (era_a && g_atomic_int_get(&d->p->monitor_vivo)) {
		avisar(d->p, MP_EV_MORREU);
	}
	return NULL;
}

/* ---- subir e derrubar um mpv ---- */

static deck *deck_novo(mp_player *p, const char *arquivo_inicial, bool pausado, double inicio, int volume) {
	deck *d = g_new0(deck, 1);
	d->p = p;
	d->fd = -1;
	const char *rt = g_get_user_runtime_dir();
	g_snprintf(d->sock, sizeof(d->sock), "%s/lumna-musica-%d-%d.sock", rt && g_file_test(rt, G_FILE_TEST_IS_DIR) ? rt : "/tmp", (int)getpid(), g_atomic_int_add(&p->n_decks, 1));
	unlink(d->sock);
	GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
#define A(s) g_ptr_array_add(a, g_strdup(s))
	A("mpv"); A("--no-config"); A("--idle=yes"); A("--no-video"); A("--no-terminal"); A("--audio-display=no"); A("--force-window=no"); A("--really-quiet");
	g_ptr_array_add(a, g_strdup_printf("--input-ipc-server=%s", d->sock));
	g_ptr_array_add(a, g_strdup_printf("--volume=%d", volume));
	g_ptr_array_add(a, g_strdup_printf("--gapless-audio=%s", p->sem_intervalos ? "yes" : "no"));
	const char *ao = g_getenv("LUMNA_MUSICA_AO");
	if (ao && *ao) {
		g_ptr_array_add(a, g_strdup_printf("--ao=%s", ao));
	}
	if (p->saida[0] && !(ao && *ao)) {
		g_ptr_array_add(a, g_strdup_printf("--audio-device=%s", p->saida));
	}
	if (p->normalizar) {
		A("--replaygain=track"); A("--af=lavfi=[dynaudnorm=f=250:g=7:m=10]");
	}
	if (arquivo_inicial) {
		if (pausado) {
			A("--pause=yes");
		}
		if (inicio > 0) {
			g_ptr_array_add(a, g_strdup_printf("--start=%.3f", inicio));
		}
		A("--"); A(arquivo_inicial);
	}
#undef A
	g_ptr_array_add(a, NULL);
	GError *e = NULL;
	if (!g_spawn_async(NULL, (char **)a->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &d->pid, &e)) {
		g_clear_error(&e);
		g_ptr_array_free(a, TRUE);
		g_free(d);
		return NULL;
	}
	g_ptr_array_free(a, TRUE);
	/* conectar ao socket (o mpv leva um instante para criá-lo) */
	for (int i = 0; i < 100 && d->fd < 0; i++) {
		int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
		struct sockaddr_un ad = { .sun_family = AF_UNIX };
		g_strlcpy(ad.sun_path, d->sock, sizeof(ad.sun_path));
		if (connect(fd, (struct sockaddr *)&ad, sizeof(ad)) == 0) {
			d->fd = fd;
		} else {
			close(fd);
			g_usleep(50000);
		}
	}
	if (d->fd < 0) {
		kill(d->pid, SIGKILL);
		waitpid(d->pid, NULL, 0);
		g_spawn_close_pid(d->pid);
		unlink(d->sock);
		g_free(d);
		return NULL;
	}
	d->vivo = true;
	if (arquivo_inicial) {
		g_strlcpy(d->caminho, arquivo_inicial, sizeof(d->caminho));
		d->pausado = pausado;
		d->carregada = true; /* o mpv já carregou o arquivo da linha de comando antes de a gente conectar: o file-loaded passou */
	}
	d->pos = inicio;
	d->pos_em = g_get_monotonic_time();
	comando(d, "\"observe_property\",1,\"time-pos\"");
	comando(d, "\"observe_property\",2,\"duration\"");
	comando(d, "\"observe_property\",3,\"pause\"");
	d->leitor = g_thread_new("musica-mpv", ler_thread, d);
	return d;
}

static void deck_matar(deck *d) {
	if (!d) {
		return;
	}
	kill(d->pid, SIGKILL);
	waitpid(d->pid, NULL, 0);
	g_spawn_close_pid(d->pid);
	if (d->leitor) {
		g_thread_join(d->leitor); /* o mpv morreu: a leitura chega ao fim */
	}
	if (d->fd >= 0) {
		close(d->fd);
	}
	unlink(d->sock);
	g_free(d);
}

static bool garantir_a(mp_player *p, const char *arquivo, bool pausado, double inicio) {
	g_mutex_lock(&p->m);
	bool vivo = p->a && p->a->vivo;
	g_mutex_unlock(&p->m);
	if (vivo) {
		return true;
	}
	g_mutex_lock(&p->m);
	deck *velho = p->a;
	p->a = NULL;
	int vol = p->volume;
	g_mutex_unlock(&p->m);
	deck_matar(velho);
	deck *d = deck_novo(p, arquivo, pausado, inicio, vol);
	g_mutex_lock(&p->m);
	p->a = d;
	g_mutex_unlock(&p->m);
	return d != NULL;
}

/* ---- a transição (crossfade) ---- */

static void b_cancelar(mp_player *p) {
	g_mutex_lock(&p->m);
	deck *b = p->b;
	p->b = NULL;
	p->b_entrando = false;
	g_mutex_unlock(&p->m);
	deck_matar(b);
}

static void monitor_tick(mp_player *p, char *preparada) {
	{
		g_mutex_lock(&p->m);
		deck *a = p->a;
		int trans = p->transicao;
		bool tem_prox = p->proxima[0] != 0;
		double resta = -1, pos = 0;
		bool tocando = false;
		if (a && a->vivo && a->carregada && a->dur > 0) {
			pos = a->pos + (a->pausado ? 0 : (double)(g_get_monotonic_time() - a->pos_em) / 1e6);
			resta = a->dur - pos;
			tocando = !a->pausado;
		}
		bool a_acabou = a && a->vivo && !a->carregada && p->b != NULL && p->proxima[0] != 0 && trans > 0;
		bool b_pronto = p->b != NULL, entrando = p->b_entrando;
		char prox[4096];
		g_strlcpy(prox, p->proxima, sizeof(prox));
		int vol = p->volume;
		g_mutex_unlock(&p->m);
		if (a_acabou) {
			resta = 0; /* o a chegou ao fim antes de a troca fechar: troca já */
			tocando = true;
		}
		if (trans <= 0 || !tem_prox || resta < 0) {
			if (b_pronto && (trans <= 0 || !tem_prox)) {
				b_cancelar(p);
			}
			return;
		}
		/* o b muda se a seguinte mudou */
		if (b_pronto && strcmp(preparada, prox)) {
			b_cancelar(p);
			b_pronto = false;
			entrando = false;
		}
		if (!b_pronto && resta <= trans + 3 && resta > trans) {
			deck *b = deck_novo(p, prox, true, 0, 0);
			if (b) {
				g_mutex_lock(&p->m);
				p->b = b;
				g_mutex_unlock(&p->m);
				g_strlcpy(preparada, prox, 4096);
			}
			return;
		}
		if (b_pronto && resta <= trans && tocando) {
			g_mutex_lock(&p->m);
			deck *b = p->b;
			if (b && !p->b_entrando) {
				p->b_entrando = true;
				comando(b, "\"set_property\",\"pause\",false");
			}
			g_mutex_unlock(&p->m);
			double f = CLAMP(1.0 - resta / trans, 0.0, 1.0);
			g_mutex_lock(&p->m);
			if (p->b) {
				comando(a, "\"set_property\",\"volume\",%d", (int)(vol * (1 - f)));
				comando(p->b, "\"set_property\",\"volume\",%d", (int)(vol * f));
			}
			g_mutex_unlock(&p->m);
			if (resta <= 0.25 || f >= 0.99) { /* terminou: o b passa a ser o a */
				g_mutex_lock(&p->m);
				deck *velho = p->a;
				p->a = p->b;
				p->b = NULL;
				p->b_entrando = false;
				p->proxima[0] = 0;
				comando(p->a, "\"set_property\",\"volume\",%d", vol);
				g_strlcpy(p->a->caminho, prox, sizeof(p->a->caminho));
				g_mutex_unlock(&p->m);
				preparada[0] = 0;
				deck_matar(velho);
				avisar(p, MP_EV_AVANCOU);
			}
		}
		(void)entrando;
	}
}

static gpointer monitor_thread(gpointer arg) {
	mp_player *p = arg;
	char preparada[4096] = "";
	while (g_atomic_int_get(&p->monitor_vivo)) {
		g_usleep(100000);
		g_mutex_lock(&p->ctl);
		monitor_tick(p, preparada);
		g_mutex_unlock(&p->ctl);
	}
	return NULL;
}

/* ---- a interface ---- */

mp_player *mp_criar(void (*evento)(int, void *), void *dados) {
	mp_player *p = g_new0(mp_player, 1);
	g_mutex_init(&p->ctl);
	g_mutex_init(&p->m);
	g_cond_init(&p->c);
	p->evento = evento;
	p->dados = dados;
	p->normalizar = true;
	p->sem_intervalos = true;
	p->volume = 80;
	g_atomic_int_set(&p->monitor_vivo, 1);
	p->monitor = g_thread_new("musica-monitor", monitor_thread, p);
	return p;
}

void mp_liberar(mp_player *p) {
	if (!p) {
		return;
	}
	g_atomic_int_set(&p->monitor_vivo, 0);
	g_thread_join(p->monitor);
	g_mutex_lock(&p->ctl);
	b_cancelar(p);
	g_mutex_lock(&p->m);
	deck *a = p->a;
	p->a = NULL;
	g_mutex_unlock(&p->m);
	deck_matar(a);
	g_mutex_unlock(&p->ctl);
	g_mutex_clear(&p->ctl);
	g_mutex_clear(&p->m);
	g_cond_clear(&p->c);
	g_free(p);
}

static void mp_ajustes_i(mp_player *p, const char *saida, bool normalizar, bool sem_intervalos, int transicao) {
	g_mutex_lock(&p->m);
	bool saida_mudou = strcmp(p->saida, saida ? saida : "") != 0;
	bool norm_mudou = p->normalizar != normalizar, gap_mudou = p->sem_intervalos != sem_intervalos;
	g_strlcpy(p->saida, saida ? saida : "", sizeof(p->saida));
	p->normalizar = normalizar;
	p->sem_intervalos = sem_intervalos;
	p->transicao = CLAMP(transicao, 0, 12);
	deck *a = p->a;
	g_mutex_unlock(&p->m);
	if (a && a->vivo) {
		if (saida_mudou) {
			char *s = js(p->saida[0] ? p->saida : "auto");
			comando(a, "\"set_property\",\"audio-device\",%s", s);
			g_free(s);
		}
		if (norm_mudou) {
			comando(a, "\"set_property\",\"af\",\"%s\"", normalizar ? "lavfi=[dynaudnorm=f=250:g=7:m=10]" : "");
			comando(a, "\"set_property\",\"replaygain\",\"%s\"", normalizar ? "track" : "no");
		}
		if (gap_mudou) {
			comando(a, "\"set_property\",\"gapless-audio\",\"%s\"", sem_intervalos ? "yes" : "no");
		}
	}
}

static bool mp_tocar_i(mp_player *p, const char *caminho, double inicio, bool pausado) {
	b_cancelar(p);
	g_mutex_lock(&p->m);
	p->proxima[0] = 0;
	g_mutex_unlock(&p->m);
	if (!p->a || !p->a->vivo) {
		return garantir_a(p, caminho, pausado, inicio);
	}
	deck *a = p->a;
	g_mutex_lock(&p->m);
	g_strlcpy(a->caminho, caminho, sizeof(a->caminho));
	a->proxima_anexada[0] = 0;
	a->eof_com_proxima = false;
	a->pos = inicio;
	a->dur = 0;
	a->pos_em = g_get_monotonic_time();
	a->pausado = pausado;
	g_mutex_unlock(&p->m);
	char *c = js(caminho);
	comando(a, "\"set_property\",\"volume\",%d", p->volume);
	comando(a, "\"set_property\",\"pause\",%s", pausado ? "true" : "false");
	g_mutex_lock(&p->m);
	a->seek_pend = inicio;
	g_mutex_unlock(&p->m);
	comando(a, "\"loadfile\",%s,\"replace\"", c);
	g_free(c);
	return true;
}

static void mp_proxima_i(mp_player *p, const char *caminho) {
	g_mutex_lock(&p->m);
	g_strlcpy(p->proxima, caminho ? caminho : "", sizeof(p->proxima));
	deck *a = p->a;
	bool gapless = p->sem_intervalos && p->transicao == 0;
	bool tinha = a && a->proxima_anexada[0];
	if (a && a->vivo && a->carregada && (gapless || tinha)) {
		comando(a, "\"playlist-clear\"");
		a->proxima_anexada[0] = 0;
		if (gapless && caminho && *caminho) {
			g_strlcpy(a->proxima_anexada, caminho, sizeof(a->proxima_anexada));
			char *c = js(caminho);
			comando(a, "\"loadfile\",%s,\"append\"", c);
			g_free(c);
		}
	}
	g_mutex_unlock(&p->m);
}

static void mp_pausar_i(mp_player *p, bool pausar) {
	g_mutex_lock(&p->m);
	deck *a = p->a, *b = p->b;
	if (a && a->vivo) {
		comando(a, "\"set_property\",\"pause\",%s", pausar ? "true" : "false");
	}
	if (b && b->vivo && p->b_entrando) {
		comando(b, "\"set_property\",\"pause\",%s", pausar ? "true" : "false");
	}
	g_mutex_unlock(&p->m);
}

static void mp_buscar_i(mp_player *p, double s) {
	g_mutex_lock(&p->m);
	deck *a = p->a;
	bool entrando = p->b_entrando;
	g_mutex_unlock(&p->m);
	if (entrando) {
		b_cancelar(p); /* mexer no tempo no meio da transição: ela é desfeita */
		g_mutex_lock(&p->m);
		if (p->a) {
			comando(p->a, "\"set_property\",\"volume\",%d", p->volume);
		}
		g_mutex_unlock(&p->m);
	}
	g_mutex_lock(&p->m);
	if (a && a->vivo && a->carregada) {
		comando(a, "\"seek\",%.3f,\"absolute\"", s < 0 ? 0 : s);
		a->pos = s < 0 ? 0 : s;
		a->pos_em = g_get_monotonic_time();
	}
	g_mutex_unlock(&p->m);
}

static void mp_volume_i(mp_player *p, int volume) {
	g_mutex_lock(&p->m);
	p->volume = CLAMP(volume, 0, 100);
	if (p->a && p->a->vivo && !p->b_entrando) {
		comando(p->a, "\"set_property\",\"volume\",%d", p->volume);
	}
	g_mutex_unlock(&p->m);
}

static void mp_parar_i(mp_player *p) {
	b_cancelar(p);
	g_mutex_lock(&p->m);
	p->proxima[0] = 0;
	deck *a = p->a;
	if (a && a->vivo) {
		comando(a, "\"stop\"");
		a->carregada = false;
		a->caminho[0] = 0;
		a->pos = a->dur = 0;
		a->proxima_anexada[0] = 0;
	}
	g_mutex_unlock(&p->m);
}

static bool mp_consultar_texto_i(mp_player *p, const char *propriedade, char *saida, size_t tam);

void mp_ajustes(mp_player *p, const char *saida, bool normalizar, bool sem_intervalos, int transicao) {
	g_mutex_lock(&p->ctl);
	mp_ajustes_i(p, saida, normalizar, sem_intervalos, transicao);
	g_mutex_unlock(&p->ctl);
	
}

bool mp_tocar(mp_player *p, const char *caminho, double inicio, bool pausado) {
	g_mutex_lock(&p->ctl);
	bool r = mp_tocar_i(p, caminho, inicio, pausado);
	g_mutex_unlock(&p->ctl);
	return r;
}

void mp_proxima(mp_player *p, const char *caminho) {
	g_mutex_lock(&p->ctl);
	mp_proxima_i(p, caminho);
	g_mutex_unlock(&p->ctl);
	
}

void mp_pausar(mp_player *p, bool pausar) {
	g_mutex_lock(&p->ctl);
	mp_pausar_i(p, pausar);
	g_mutex_unlock(&p->ctl);
	
}

void mp_buscar(mp_player *p, double s) {
	g_mutex_lock(&p->ctl);
	mp_buscar_i(p, s);
	g_mutex_unlock(&p->ctl);
	
}

void mp_volume(mp_player *p, int volume) {
	g_mutex_lock(&p->ctl);
	mp_volume_i(p, volume);
	g_mutex_unlock(&p->ctl);
	
}

void mp_parar(mp_player *p) {
	g_mutex_lock(&p->ctl);
	mp_parar_i(p);
	g_mutex_unlock(&p->ctl);
	
}

bool mp_consultar_texto(mp_player *p, const char *propriedade, char *saida, size_t tam) {
	g_mutex_lock(&p->ctl);
	bool r = mp_consultar_texto_i(p, propriedade, saida, tam);
	g_mutex_unlock(&p->ctl);
	return r;
}

void mp_estado_ler(mp_player *p, mp_estado *e) {
	memset(e, 0, sizeof(*e));
	g_mutex_lock(&p->m);
	e->volume = p->volume;
	deck *a = p->a;
	if (a && a->vivo) {
		e->vivo = true;
		e->carregada = a->carregada || a->caminho[0];
		e->tocando = a->carregada && !a->pausado;
		e->dur = a->dur;
		e->pos = a->pos + (e->tocando ? (double)(g_get_monotonic_time() - a->pos_em) / 1e6 : 0);
		if (e->dur > 0 && e->pos > e->dur) {
			e->pos = e->dur;
		}
		g_strlcpy(e->caminho, a->caminho, sizeof(e->caminho));
	}
	g_mutex_unlock(&p->m);
}

static bool mp_consultar_texto_i(mp_player *p, const char *propriedade, char *saida, size_t tam) {
	g_mutex_lock(&p->m);
	deck *a = p->a;
	if (!a || !a->vivo) {
		g_mutex_unlock(&p->m);
		return false;
	}
	int id = ++a->req + 1000;
	a->resp_id = 0;
	char *j = g_strdup_printf("{\"command\":[\"get_property\",\"%s\"],\"request_id\":%d}", propriedade, id);
	enviar(a, j);
	g_free(j);
	gint64 fim = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
	while (a->resp_id != id && a->vivo) {
		if (!g_cond_wait_until(&p->c, &p->m, fim)) {
			break;
		}
	}
	bool ok = a->resp_id == id && a->resp_ok;
	if (ok) {
		g_strlcpy(saida, a->resp, tam);
	}
	g_mutex_unlock(&p->m);
	return ok;
}

bool mp_consultar(mp_player *p, const char *propriedade, double *valor) {
	char t[512];
	if (!mp_consultar_texto(p, propriedade, t, sizeof(t))) {
		return false;
	}
	*valor = g_ascii_strtod(t, NULL);
	return true;
}

void mp_dispositivos(GPtrArray *saida) {
	char *argv[] = { "mpv", "--no-config", "--audio-device=help", NULL };
	gchar *out = NULL;
	gint st = 0;
	if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &st, NULL) && out) {
		char **ls = g_strsplit(out, "\n", -1);
		for (int i = 0; ls[i]; i++) {
			char *l = g_strstrip(ls[i]);
			if (l[0] != '\'') {
				continue;
			}
			char *f = strchr(l + 1, '\'');
			if (!f) {
				continue;
			}
			char *nome = g_strndup(l + 1, (gsize)(f - l - 1));
			char *desc = strchr(f, '(');
			char *d2 = desc ? g_strdup(desc + 1) : g_strdup(nome);
			if (strlen(d2) > 0 && d2[strlen(d2) - 1] == ')') {
				d2[strlen(d2) - 1] = 0;
			}
			g_ptr_array_add(saida, g_strdup_printf("%s\t%s", nome, d2));
			g_free(nome);
			g_free(d2);
		}
		g_strfreev(ls);
	}
	g_free(out);
}
