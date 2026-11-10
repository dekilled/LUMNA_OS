#include "video_player.h"

#include <glib.h>
#include <math.h>
#include <mpv/client.h>
#include <mpv/render.h>
#include <stdlib.h>
#include <string.h>

struct vp {
	mpv_handle *h;
	mpv_render_context *rc;
	void (*acorda)(void *);
	void *dados;
	vp_estado e;
	double seek_pend;
	gint novo_quadro;
	bool pausado;   /* o que o mpv diz */
	uint8_t *buf;
	int bw, bh, bstride;
	size_t bcap;
	GMutex m;
};

static void ao_atualizar(void *d) {
	vp *p = d;
	g_atomic_int_set(&p->novo_quadro, 1);
	if (p->acorda) {
		p->acorda(p->dados);
	}
}

static void ao_acordar(void *d) {
	vp *p = d;
	if (p->acorda) {
		p->acorda(p->dados);
	}
}

vp *vp_criar(void (*acorda)(void *), void *dados) {
	vp *p = g_new0(vp, 1);
	p->acorda = acorda;
	p->dados = dados;
	g_mutex_init(&p->m);
	p->h = mpv_create();
	if (!p->h) {
		g_free(p);
		return NULL;
	}
	mpv_set_option_string(p->h, "vo", "libmpv");
	mpv_set_option_string(p->h, "config", "no");
	mpv_set_option_string(p->h, "terminal", "no");
	mpv_set_option_string(p->h, "audio-display", "no");
	mpv_set_option_string(p->h, "sub-auto", "fuzzy");
	mpv_set_option_string(p->h, "hwdec", "no");
	mpv_set_option_string(p->h, "keep-open", "yes");
	mpv_set_option_string(p->h, "idle", "yes");
	mpv_set_option_string(p->h, "osc", "no");
	mpv_set_option_string(p->h, "input-default-bindings", "no");
	mpv_set_option_string(p->h, "input-vo-keyboard", "no");
	mpv_set_option_string(p->h, "cursor-autohide", "no");
	mpv_set_option_string(p->h, "ytdl", "no");
	mpv_set_option_string(p->h, "load-scripts", "no");
	const char *ao = g_getenv("LUMNA_GALERIA_AO");
	if (ao && *ao) {
		mpv_set_option_string(p->h, "ao", ao);
	}
	if (mpv_initialize(p->h) < 0) {
		mpv_terminate_destroy(p->h);
		g_free(p);
		return NULL;
	}
	mpv_render_param prm[] = { { MPV_RENDER_PARAM_API_TYPE, (void *)MPV_RENDER_API_TYPE_SW }, { MPV_RENDER_PARAM_INVALID, NULL } };
	if (mpv_render_context_create(&p->rc, p->h, prm) < 0) {
		mpv_terminate_destroy(p->h);
		g_free(p);
		return NULL;
	}
	mpv_render_context_set_update_callback(p->rc, ao_atualizar, p);
	mpv_set_wakeup_callback(p->h, ao_acordar, p);
	mpv_observe_property(p->h, 1, "time-pos", MPV_FORMAT_DOUBLE);
	mpv_observe_property(p->h, 2, "duration", MPV_FORMAT_DOUBLE);
	mpv_observe_property(p->h, 3, "pause", MPV_FORMAT_FLAG);
	mpv_observe_property(p->h, 4, "eof-reached", MPV_FORMAT_FLAG);
	mpv_observe_property(p->h, 5, "volume", MPV_FORMAT_DOUBLE);
	mpv_observe_property(p->h, 6, "speed", MPV_FORMAT_DOUBLE);
	mpv_observe_property(p->h, 7, "track-list", MPV_FORMAT_NODE);
	mpv_observe_property(p->h, 8, "dwidth", MPV_FORMAT_INT64);
	mpv_observe_property(p->h, 9, "dheight", MPV_FORMAT_INT64);
	mpv_observe_property(p->h, 10, "aid", MPV_FORMAT_STRING);
	mpv_observe_property(p->h, 11, "sid", MPV_FORMAT_STRING);
	p->e.velocidade = 1;
	p->e.volume = 80;
	return p;
}

void vp_liberar(vp *p) {
	if (!p) {
		return;
	}
	mpv_render_context_set_update_callback(p->rc, NULL, NULL);
	mpv_set_wakeup_callback(p->h, NULL, NULL);
	mpv_render_context_free(p->rc);
	mpv_terminate_destroy(p->h);
	g_free(p->buf);
	g_mutex_clear(&p->m);
	g_free(p);
}

static void cmd(vp *p, const char *a, const char *b, const char *c, const char *d) {
	const char *args[6] = { a, b, c, d, NULL, NULL };
	mpv_command_async(p->h, 0, args);
}

bool vp_abrir(vp *p, const char *caminho, double inicio, bool repetir, int volume) {
	g_mutex_lock(&p->m);
	memset(&p->e, 0, sizeof(p->e));
	p->e.aberto = true;
	p->e.carregando = true;
	p->pausado = false;
	p->e.velocidade = 1;
	p->e.volume = volume;
	p->e.repetir = repetir;
	p->seek_pend = inicio > 0.5 ? inicio : 0;
	g_mutex_unlock(&p->m);
	char vol[16];
	g_snprintf(vol, sizeof(vol), "%d", CLAMP(volume, 0, 130));
	mpv_set_property_string(p->h, "volume", vol);
	mpv_set_property_string(p->h, "speed", "1");
	mpv_set_property_string(p->h, "pause", "no");
	mpv_set_property_string(p->h, "loop-file", repetir ? "inf" : "no");
	mpv_set_property_string(p->h, "aid", "auto");
	mpv_set_property_string(p->h, "sid", "auto");
	cmd(p, "loadfile", caminho, "replace", NULL);
	return true;
}

void vp_pausar(vp *p, bool pausar) {
	mpv_set_property_string(p->h, "pause", pausar ? "yes" : "no");
	if (!pausar && p->e.fim) { /* tocar no fim recomeça */
		cmd(p, "seek", "0", "absolute", NULL);
		g_mutex_lock(&p->m);
		p->e.fim = false;
		g_mutex_unlock(&p->m);
	}
}

void vp_buscar(vp *p, double s) {
	char b[32];
	g_ascii_formatd(b, sizeof(b), "%.3f", s < 0 ? 0 : s);
	cmd(p, "seek", b, "absolute", NULL);
	g_mutex_lock(&p->m);
	p->e.pos = s < 0 ? 0 : s;
	p->e.fim = false;
	g_mutex_unlock(&p->m);
}

void vp_volume(vp *p, int volume) {
	char b[16];
	g_snprintf(b, sizeof(b), "%d", CLAMP(volume, 0, 130));
	mpv_set_property_string(p->h, "volume", b);
	g_mutex_lock(&p->m);
	p->e.volume = CLAMP(volume, 0, 130);
	g_mutex_unlock(&p->m);
}

void vp_velocidade(vp *p, double v) {
	char b[16];
	g_ascii_formatd(b, sizeof(b), "%.2f", CLAMP(v, 0.25, 4.0));
	mpv_set_property_string(p->h, "speed", b);
}

void vp_repetir(vp *p, bool sim) {
	mpv_set_property_string(p->h, "loop-file", sim ? "inf" : "no");
	g_mutex_lock(&p->m);
	p->e.repetir = sim;
	g_mutex_unlock(&p->m);
}

void vp_audio(vp *p, int id) {
	char b[16];
	g_snprintf(b, sizeof(b), "%d", id);
	mpv_set_property_string(p->h, "aid", id > 0 ? b : "no");
}

void vp_legenda(vp *p, int id) {
	char b[16];
	g_snprintf(b, sizeof(b), "%d", id);
	mpv_set_property_string(p->h, "sid", id > 0 ? b : "no");
}

void vp_parar(vp *p) {
	cmd(p, "stop", NULL, NULL, NULL);
	g_mutex_lock(&p->m);
	memset(&p->e, 0, sizeof(p->e));
	p->e.velocidade = 1;
	g_mutex_unlock(&p->m);
}

static void conta_faixas(vp *p, const mpv_node *n) {
	int na = 0, ns = 0;
	if (n->format == MPV_FORMAT_NODE_ARRAY) {
		for (int i = 0; i < n->u.list->num; i++) {
			const mpv_node *t = &n->u.list->values[i];
			if (t->format != MPV_FORMAT_NODE_MAP) {
				continue;
			}
			for (int k = 0; k < t->u.list->num; k++) {
				if (!strcmp(t->u.list->keys[k], "type") && t->u.list->values[k].format == MPV_FORMAT_STRING) {
					const char *tp = t->u.list->values[k].u.string;
					na += !strcmp(tp, "audio");
					ns += !strcmp(tp, "sub");
				}
			}
		}
	}
	p->e.naudio = na;
	p->e.nlegenda = ns;
}

void vp_atualizar(vp *p, vp_estado *saida) {
	for (;;) {
		mpv_event *ev = mpv_wait_event(p->h, 0);
		if (ev->event_id == MPV_EVENT_NONE) {
			break;
		}
		g_mutex_lock(&p->m);
		if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE) {
			mpv_event_property *pr = ev->data;
			if (!strcmp(pr->name, "time-pos") && pr->format == MPV_FORMAT_DOUBLE) {
				p->e.pos = *(double *)pr->data;
			} else if (!strcmp(pr->name, "duration") && pr->format == MPV_FORMAT_DOUBLE) {
				p->e.dur = *(double *)pr->data;
			} else if (!strcmp(pr->name, "pause") && pr->format == MPV_FORMAT_FLAG) {
				p->pausado = *(int *)pr->data;
			} else if (!strcmp(pr->name, "eof-reached") && pr->format == MPV_FORMAT_FLAG) {
				p->e.fim = *(int *)pr->data && !p->e.repetir;
			} else if (!strcmp(pr->name, "volume") && pr->format == MPV_FORMAT_DOUBLE) {
				p->e.volume = (int)lround(*(double *)pr->data);
			} else if (!strcmp(pr->name, "speed") && pr->format == MPV_FORMAT_DOUBLE) {
				p->e.velocidade = *(double *)pr->data;
			} else if (!strcmp(pr->name, "track-list") && pr->format == MPV_FORMAT_NODE) {
				conta_faixas(p, pr->data);
			} else if (!strcmp(pr->name, "dwidth") && pr->format == MPV_FORMAT_INT64) {
				p->e.w = (int)*(int64_t *)pr->data;
			} else if (!strcmp(pr->name, "dheight") && pr->format == MPV_FORMAT_INT64) {
				p->e.h = (int)*(int64_t *)pr->data;
			} else if ((!strcmp(pr->name, "aid") || !strcmp(pr->name, "sid")) && pr->format == MPV_FORMAT_STRING) {
				const char *v = *(char **)pr->data;
				int id = v && g_ascii_isdigit(v[0]) ? atoi(v) : 0;
				if (pr->name[0] == 'a') {
					p->e.audio = id;
				} else {
					p->e.legenda = id;
				}
			}
		} else if (ev->event_id == MPV_EVENT_FILE_LOADED) {
			p->e.carregando = false;
			char *pa = mpv_get_property_string(p->h, "pause");
			p->pausado = pa && !strcmp(pa, "yes");
			p->e.fim = false;
			if (pa) {
				mpv_free(pa);
			}
			if (p->seek_pend > 0) {
				char b[32];
				g_ascii_formatd(b, sizeof(b), "%.3f", p->seek_pend);
				p->seek_pend = 0;
				const char *args[] = { "seek", b, "absolute", NULL };
				mpv_command_async(p->h, 0, args);
			}
		} else if (ev->event_id == MPV_EVENT_END_FILE) {
			mpv_event_end_file *ef = ev->data;
			if (ef->reason == MPV_END_FILE_REASON_ERROR) {
				p->e.erro = true;
				p->e.carregando = false;
			}
		}
		g_mutex_unlock(&p->m);
	}
	g_mutex_lock(&p->m);
	p->e.tocando = p->e.aberto && !p->e.carregando && !p->e.erro && !p->pausado && !p->e.fim;
	*saida = p->e;
	g_mutex_unlock(&p->m);
}

int vp_faixas(vp *p, bool legendas, vp_faixa *v, int max) {
	mpv_node n;
	int total = 0;
	if (mpv_get_property(p->h, "track-list", MPV_FORMAT_NODE, &n) < 0) {
		return 0;
	}
	if (n.format == MPV_FORMAT_NODE_ARRAY) {
		int ordem = 0;
		for (int i = 0; i < n.u.list->num && total < max; i++) {
			const mpv_node *t = &n.u.list->values[i];
			if (t->format != MPV_FORMAT_NODE_MAP) {
				continue;
			}
			const char *tipo = "", *lang = "", *titulo = "", *codec = "";
			int64_t id = 0;
			bool sel = false;
			for (int k = 0; k < t->u.list->num; k++) {
				const char *ch = t->u.list->keys[k];
				const mpv_node *x = &t->u.list->values[k];
				if (!strcmp(ch, "type") && x->format == MPV_FORMAT_STRING) tipo = x->u.string;
				else if (!strcmp(ch, "id") && x->format == MPV_FORMAT_INT64) id = x->u.int64;
				else if (!strcmp(ch, "lang") && x->format == MPV_FORMAT_STRING) lang = x->u.string;
				else if (!strcmp(ch, "title") && x->format == MPV_FORMAT_STRING) titulo = x->u.string;
				else if (!strcmp(ch, "codec") && x->format == MPV_FORMAT_STRING) codec = x->u.string;
				else if (!strcmp(ch, "selected") && x->format == MPV_FORMAT_FLAG) sel = x->u.flag;
			}
			if (strcmp(tipo, legendas ? "sub" : "audio")) {
				continue;
			}
			ordem++;
			vp_faixa *f = &v[total++];
			f->id = (int)id;
			f->escolhida = sel;
			if (*titulo && *lang) {
				g_snprintf(f->titulo, sizeof(f->titulo), "%s (%s)", titulo, lang);
			} else if (*titulo || *lang) {
				g_snprintf(f->titulo, sizeof(f->titulo), "%s", *titulo ? titulo : lang);
			} else {
				g_snprintf(f->titulo, sizeof(f->titulo), "%s %d%s%s", legendas ? "Legenda" : "Faixa", ordem, *codec ? " · " : "", codec);
			}
		}
	}
	mpv_free_node_contents(&n);
	return total;
}

bool vp_quadro(vp *p, int w, int h, uint8_t **dados, int *stride) {
	if (w < 2 || h < 2) {
		return false;
	}
	bool novo = g_atomic_int_compare_and_exchange(&p->novo_quadro, 1, 0);
	uint64_t fl = mpv_render_context_update(p->rc);
	bool quadro = (fl & MPV_RENDER_UPDATE_FRAME) != 0;
	bool mudou_tam = w != p->bw || h != p->bh;
	if (mudou_tam) {
		size_t cap = (size_t)w * 4 * (size_t)h;
		if (cap > p->bcap) {
			p->buf = g_realloc(p->buf, cap);
			p->bcap = cap;
		}
		p->bw = w;
		p->bh = h;
		p->bstride = w * 4;
		memset(p->buf, 0, cap);
	}
	bool desenhou = false;
	if (quadro || mudou_tam || novo) {
		int tam[2] = { w, h };
		size_t st = (size_t)p->bstride;
		mpv_render_param rp[] = { { MPV_RENDER_PARAM_SW_SIZE, tam }, { MPV_RENDER_PARAM_SW_FORMAT, (void *)"bgr0" }, { MPV_RENDER_PARAM_SW_STRIDE, &st },
			{ MPV_RENDER_PARAM_SW_POINTER, p->buf }, { MPV_RENDER_PARAM_INVALID, NULL } };
		desenhou = mpv_render_context_render(p->rc, rp) >= 0;
	}
	*dados = p->buf;
	*stride = p->bstride;
	return desenhou || mudou_tam;
}

bool vp_propriedade(vp *p, const char *nome, char *saida, size_t tam) {
	char *v = mpv_get_property_string(p->h, nome);
	if (!v) {
		return false;
	}
	g_strlcpy(saida, v, tam);
	mpv_free(v);
	return true;
}
