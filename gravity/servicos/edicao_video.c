#include "edicao_video.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utime.h>

void ev_padrao(ev_params *p, double duracao) {
	memset(p, 0, sizeof(*p));
	p->fim = duracao;
	p->volume = 100;
}

bool ev_alterado(const ev_params *p, double duracao) {
	return p->inicio > 0.05 || p->fim < duracao - 0.05 || p->mudo || p->volume != 100 || (p->giro & 3);
}

void ev_ajustar(ev_params *p, double duracao, double min) {
	if (min > duracao) {
		min = duracao;
	}
	p->inicio = CLAMP(p->inicio, 0, duracao - min);
	p->fim = CLAMP(p->fim, p->inicio + min, duracao);
	p->volume = CLAMP(p->volume, 0, 200);
	p->giro &= 3;
}

/* ---- rodar um programa ---- */

static bool rodar(char **argv, GString *saida, double timeout_s) {
	char *tmo = g_strdup_printf("%d", (int)timeout_s);
	GPtrArray *a = g_ptr_array_new();
	g_ptr_array_add(a, "timeout");
	g_ptr_array_add(a, tmo);
	for (int i = 0; argv[i]; i++) {
		g_ptr_array_add(a, argv[i]);
	}
	g_ptr_array_add(a, NULL);
	gchar *out = NULL;
	gint st = 0;
	gboolean ok = g_spawn_sync(NULL, (char **)a->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, &st, NULL);
	g_ptr_array_free(a, TRUE);
	g_free(tmo);
	bool bom = ok && WIFEXITED(st) && WEXITSTATUS(st) == 0;
	if (bom && saida && out) {
		g_string_append(saida, out);
	}
	g_free(out);
	return bom;
}

bool ev_sondar(const char *caminho, ev_info *info) {
	memset(info, 0, sizeof(*info));
	GString *s = g_string_new(NULL);
	char *argv[] = { "ffprobe", "-v", "error", "-show_entries", "stream=codec_type:format=duration", "-of", "default=nw=1", (char *)caminho, NULL };
	bool ok = rodar(argv, s, 20);
	if (ok) {
		char **ls = g_strsplit(s->str, "\n", -1);
		for (int i = 0; ls[i]; i++) {
			if (!strcmp(ls[i], "codec_type=video")) {
				info->video = true;
			} else if (!strcmp(ls[i], "codec_type=audio")) {
				info->audio = true;
			} else if (g_str_has_prefix(ls[i], "duration=")) {
				info->duracao = g_ascii_strtod(ls[i] + 9, NULL);
			}
		}
		g_strfreev(ls);
	}
	g_string_free(s, TRUE);
	return ok && info->video && info->duracao > 0;
}

static const char *giro_filtro(int giro) {
	switch (giro & 3) {
	case 1: return "transpose=1";
	case 2: return "transpose=1,transpose=1";
	case 3: return "transpose=2";
	default: return NULL;
	}
}

static char *filtro_de(int giro, const char *escala) {
	const char *g = giro_filtro(giro);
	return g ? g_strdup_printf("%s,%s", g, escala) : g_strdup(escala);
}

GdkPixbuf *ev_quadro(const char *caminho, double t, int giro, int max_lado) {
	char *escala = g_strdup_printf("scale='if(gt(iw,ih),min(iw,%d),-2)':'if(gt(iw,ih),-2,min(ih,%d))'", max_lado, max_lado);
	char *vf = filtro_de(giro, escala);
	char *ts = g_strdup_printf("%.3f", MAX(t, 0));
	char *argv[] = { "ffmpeg", "-v", "error", "-ss", ts, "-i", (char *)caminho, "-frames:v", "1", "-vf", vf, "-f", "image2pipe", "-c:v", "png", "pipe:1", NULL };
	GPtrArray *a = g_ptr_array_new();
	g_ptr_array_add(a, "timeout");
	g_ptr_array_add(a, "30");
	for (int i = 0; argv[i]; i++) {
		g_ptr_array_add(a, argv[i]);
	}
	g_ptr_array_add(a, NULL);
	GdkPixbuf *pb = NULL;
	gint st = 0;
	GPid pid;
	gint fd_out = -1;
	if (g_spawn_async_with_pipes(NULL, (char **)a->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL, &fd_out, NULL, NULL)) {
		GByteArray *buf = g_byte_array_new();
		uint8_t tmp[65536];
		ssize_t r;
		while ((r = read(fd_out, tmp, sizeof(tmp))) > 0 || (r < 0 && errno == EINTR)) {
			if (r > 0) {
				g_byte_array_append(buf, tmp, (guint)r);
			}
		}
		close(fd_out);
		waitpid(pid, &st, 0);
		g_spawn_close_pid(pid);
		if (buf->len > 0) {
			GdkPixbufLoader *l = gdk_pixbuf_loader_new();
			if (gdk_pixbuf_loader_write(l, buf->data, buf->len, NULL) && gdk_pixbuf_loader_close(l, NULL)) {
				pb = gdk_pixbuf_loader_get_pixbuf(l);
				if (pb) {
					g_object_ref(pb);
				}
			} else {
				gdk_pixbuf_loader_close(l, NULL);
			}
			g_object_unref(l);
		}
		g_byte_array_free(buf, TRUE);
	}
	g_ptr_array_free(a, TRUE);
	g_free(escala);
	g_free(vf);
	g_free(ts);
	return pb;
}

/* ---- a prévia tocando ---- */

struct ev_toque {
	GPid pid;
	int fd;
	gsize quadro;
	gint parado;
};

ev_toque *ev_tocar(const char *caminho, double t0, int giro, int w, int h, int fps) {
	char *escala = g_strdup_printf("scale=%d:%d,fps=%d", w, h, fps);
	char *vf = filtro_de(giro, escala);
	/* o giro acontece antes da escala: o w×h pedido é o do quadro já girado */
	char *ts = g_strdup_printf("%.3f", MAX(t0, 0));
	char *argv[] = { "ffmpeg", "-v", "error", "-nostdin", "-ss", ts, "-re", "-i", (char *)caminho, "-an", "-vf", vf, "-f", "rawvideo", "-pix_fmt", "bgra", "pipe:1", NULL };
	ev_toque *t = g_new0(ev_toque, 1);
	t->fd = -1;
	if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &t->pid, NULL, &t->fd, NULL, NULL)) {
		g_free(t);
		t = NULL;
	} else {
		t->quadro = (gsize)w * (gsize)h * 4;
	}
	g_free(escala);
	g_free(vf);
	g_free(ts);
	return t;
}

bool ev_toque_proximo(ev_toque *t, uint8_t *buf) {
	gsize feito = 0;
	while (feito < t->quadro) {
		ssize_t r = read(t->fd, buf + feito, t->quadro - feito);
		if (r < 0 && errno == EINTR) {
			continue;
		}
		if (r <= 0) {
			return false;
		}
		feito += (gsize)r;
	}
	return true;
}

void ev_toque_parar(ev_toque *t) {
	if (!t || !g_atomic_int_compare_and_exchange(&t->parado, 0, 1)) {
		return;
	}
	kill(t->pid, SIGKILL);
	/* quem lê o pipe (outra thread) vê o fim do arquivo; o fd é fechado depois de ela sair: aqui só se espera o processo */
	waitpid(t->pid, NULL, 0);
	g_spawn_close_pid(t->pid);
}

/* ---- exportar ---- */

static char *nome_da_copia(const char *origem) {
	char *base = g_path_get_basename(origem), *dir = g_path_get_dirname(origem);
	char *ponto = strrchr(base, '.');
	if (ponto && ponto != base) {
		*ponto = 0;
	}
	char *r = NULL;
	for (int n = 1; n < 10000 && !r; n++) {
		char *nome = n == 1 ? g_strdup_printf("%s-editado.mp4", base) : g_strdup_printf("%s-editado-%d.mp4", base, n);
		char *c = g_build_filename(dir, nome, NULL);
		g_free(nome);
		if (!g_file_test(c, G_FILE_TEST_EXISTS)) {
			r = c;
		} else {
			g_free(c);
		}
	}
	g_free(base);
	g_free(dir);
	return r;
}

char *ev_exportar(const char *origem, const ev_params *pin, time_t data, void (*progresso)(double, void *), void *dados, volatile int *cancelar, GError **erro) {
	ev_info info;
	if (!ev_sondar(origem, &info)) {
		g_set_error(erro, G_FILE_ERROR, G_FILE_ERROR_INVAL, "não deu para ler o vídeo");
		return NULL;
	}
	ev_params p = *pin;
	ev_ajustar(&p, info.duracao, 0.1);
	double dur = p.fim - p.inicio;
	char *destino = nome_da_copia(origem);
	if (!destino) {
		g_set_error(erro, G_FILE_ERROR, G_FILE_ERROR_FAILED, "não deu para escolher o nome da cópia");
		return NULL;
	}
	char *dir = g_path_get_dirname(destino), *nome = g_path_get_basename(destino);
	char *tnome = g_strdup_printf(".%s.lumna-tmp", nome);
	char *tmp = g_build_filename(dir, tnome, NULL);
	g_free(dir);
	g_free(nome);
	g_free(tnome);

	GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
#define A(s) g_ptr_array_add(a, g_strdup(s))
	A("ffmpeg"); A("-v"); A("error"); A("-nostdin"); A("-nostats"); A("-progress"); A("pipe:1"); A("-y");
	A("-ss"); g_ptr_array_add(a, g_strdup_printf("%.3f", p.inicio));
	A("-i"); A(origem);
	A("-t"); g_ptr_array_add(a, g_strdup_printf("%.3f", dur));
	const char *g = giro_filtro(p.giro);
	if (g) {
		A("-vf"); A(g);
	}
	if (p.mudo || !info.audio) {
		A("-an");
	} else if (p.volume != 100) {
		A("-af"); g_ptr_array_add(a, g_strdup_printf("volume=%.3f", p.volume / 100.0));
	}
	A("-c:v"); A("libx264"); A("-preset"); A("veryfast"); A("-crf"); A("20"); A("-pix_fmt"); A("yuv420p");
	if (!p.mudo && info.audio) {
		A("-c:a"); A("aac"); A("-b:a"); A("160k");
	}
	A("-movflags"); A("+faststart"); A("-f"); A("mp4"); A(tmp);
#undef A
	g_ptr_array_add(a, NULL);

	GPid pid;
	gint fd = -1, fd_err = -1;
	char *resultado = NULL;
	if (!g_spawn_async_with_pipes(NULL, (char **)a->pdata, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid, NULL, &fd, &fd_err, erro)) {
		goto fim;
	}
	{
		FILE *f = fdopen(fd, "r");
		char linha[512];
		bool morto = false;
		while (f && fgets(linha, sizeof(linha), f)) {
			if (cancelar && g_atomic_int_get(cancelar) && !morto) {
				kill(pid, SIGKILL);
				morto = true;
			}
			if (g_str_has_prefix(linha, "out_time_us=") && progresso) {
				double us = g_ascii_strtod(linha + 12, NULL);
				progresso(CLAMP(us / 1e6 / dur, 0, 1), dados);
			}
		}
		if (f) {
			fclose(f);
		}
		if (cancelar && g_atomic_int_get(cancelar) && !morto) {
			kill(pid, SIGKILL);
			morto = true;
		}
		GString *e = g_string_new(NULL);
		char b[512];
		ssize_t r;
		while ((r = read(fd_err, b, sizeof(b))) > 0) {
			g_string_append_len(e, b, r);
		}
		close(fd_err);
		gint st = 0;
		waitpid(pid, &st, 0);
		g_spawn_close_pid(pid);
		bool cancelado = cancelar && g_atomic_int_get(cancelar);
		if (!cancelado && WIFEXITED(st) && WEXITSTATUS(st) == 0 && g_rename(tmp, destino) == 0) {
			if (data > 0) {
				struct utimbuf u = { data, data };
				utime(destino, &u);
			}
			if (progresso) {
				progresso(1, dados);
			}
			resultado = g_strdup(destino);
		} else if (!cancelado) {
			g_strstrip(e->str);
			g_set_error(erro, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s", e->len ? e->str : "o ffmpeg falhou");
		}
		g_string_free(e, TRUE);
		if (!resultado) {
			g_unlink(tmp);
		}
	}
fim:
	g_ptr_array_free(a, TRUE);
	g_free(tmp);
	g_free(destino);
	return resultado;
}

void ev_toque_liberar(ev_toque *t) { /* depois de parar e de a thread que lia ter saído */
	if (t) {
		if (t->fd >= 0) {
			close(t->fd);
		}
		g_free(t);
	}
}
