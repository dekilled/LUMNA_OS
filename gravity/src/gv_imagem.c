/*
 * Imagens: PNG, JPEG, WebP, SVG... (o que o gdk-pixbuf tiver carregador) e os ícones dos apps
 * pelo tema de ícones.
 *
 * - descodificam em duas threads, fora do quadro: a interface nunca espera por um JPEG grande;
 *   quando chegam, um eventfd acorda o ciclo e a imagem aparece com um esmaecer curto;
 * - descodificam já no tamanho em que vão aparecer (em pixels do ecrã), não no do ficheiro: um
 *   papel de parede 4K numa tela de 1080 ocupa a memória de 1080. Se a caixa cresce, volta a
 *   descodificar maior; um SVG descodifica sempre nítido;
 * - ficam em cache até GRAVITY_CACHE_MB (48 por omissão); passando disso saem as que não
 *   aparecem há mais tempo.
 */
#include "gv_interno.h"

#include <errno.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

#define TRABALHADORES 2
#define ESMAECER 0.18 /* s */

struct imagem {
	char *chave;           /* o caminho, ou "icone:" e o nome do ícone */
	cairo_surface_t *sup;  /* NULL: ainda não (ou saiu da cache) */
	int sw, sh;            /* o tamanho descodificado */
	int nw, nh;            /* o natural (0: ainda não se sabe) */
	bool vetor;            /* SVG: descodifica em qualquer tamanho */
	bool pedida;           /* um pedido a caminho */
	bool erro;
	uint64_t uso;          /* o último quadro em que apareceu */
	double chegou;         /* quando chegou pela primeira vez (para esmaecer) */
};

struct pedido {
	char *chave;
	int bw, bh;            /* a caixa, em pixels do ecrã */
	enum gv_ajuste ajuste;
};

struct resultado {
	char *chave;
	cairo_surface_t *sup;
	int nw, nh;
	bool vetor;
};

static GHashTable *imagens;
static GAsyncQueue *pedidos, *resultados;
static int aviso = -1; /* eventfd: há resultados */
static uint64_t quadro;
static size_t limite;

/* ---- os ícones do tema (freedesktop) ---- */

static bool existe(const char *caminho) {
	struct stat st;
	return stat(caminho, &st) == 0 && S_ISREG(st.st_mode);
}

/* Procura o ícone de um app: o tema de GRAVITY_ICONES, depois hicolor, depois /usr/share/pixmaps.
 * Sem ler o index.theme: as pastas de sempre (scalable e os tamanhos, maior primeiro). */
static char *achar_icone(const char *nome, int tamanho) {
	if (nome[0] == '/') {
		return existe(nome) ? g_strdup(nome) : NULL;
	}
	static const int tamanhos[] = { 512, 256, 192, 128, 96, 72, 64, 48, 36, 32, 24, 22, 16 };
	const char *tema = getenv("GRAVITY_ICONES");
	const char *temas[] = { tema && *tema ? tema : "Papirus", "Adwaita", "hicolor" };
	GPtrArray *bases = g_ptr_array_new_with_free_func(g_free);
	const char *casa = g_get_home_dir();
	g_ptr_array_add(bases, g_build_filename(g_get_user_data_dir(), "icons", NULL));
	if (casa) {
		g_ptr_array_add(bases, g_build_filename(casa, ".icons", NULL));
	}
	for (const char *const *d = g_get_system_data_dirs(); *d; d++) {
		g_ptr_array_add(bases, g_build_filename(*d, "icons", NULL));
	}
	char *achado = NULL;
	char caminho[1024];
	for (size_t t = 0; t < G_N_ELEMENTS(temas) && !achado; t++) {
		for (guint b = 0; b < bases->len && !achado; b++) {
			const char *base = bases->pdata[b];
			snprintf(caminho, sizeof(caminho), "%s/%s/scalable/apps/%s.svg", base, temas[t], nome);
			if (existe(caminho)) {
				achado = g_strdup(caminho);
				break;
			}
			/* o menor que ainda chega ao tamanho pedido; senão o maior que houver */
			int melhor = -1;
			for (int i = G_N_ELEMENTS(tamanhos) - 1; i >= 0 && melhor < 0; i--) {
				if (tamanhos[i] < tamanho) {
					continue;
				}
				snprintf(caminho, sizeof(caminho), "%s/%s/%dx%d/apps/%s.png", base, temas[t], tamanhos[i], tamanhos[i], nome);
				if (existe(caminho)) {
					melhor = i;
				}
			}
			for (size_t i = 0; i < G_N_ELEMENTS(tamanhos) && melhor < 0; i++) {
				snprintf(caminho, sizeof(caminho), "%s/%s/%dx%d/apps/%s.png", base, temas[t], tamanhos[i], tamanhos[i], nome);
				if (existe(caminho)) {
					melhor = (int)i;
				}
			}
			if (melhor >= 0) {
				achado = g_strdup(caminho);
			}
		}
	}
	for (const char *const *d = g_get_system_data_dirs(); *d && !achado; d++) {
		static const char *const ext[] = { "svg", "png", "xpm" };
		for (size_t e = 0; e < G_N_ELEMENTS(ext) && !achado; e++) {
			snprintf(caminho, sizeof(caminho), "%s/pixmaps/%s.%s", *d, nome, ext[e]);
			if (existe(caminho)) {
				achado = g_strdup(caminho);
			}
		}
	}
	g_ptr_array_free(bases, TRUE);
	return achado;
}

/* ---- as threads ---- */

/* GdkPixbuf (RGBA sem pré-multiplicar) para uma superfície do cairo (ARGB pré-multiplicado). */
static cairo_surface_t *para_cairo(GdkPixbuf *pb) {
	int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
	int canais = gdk_pixbuf_get_n_channels(pb);
	int passo = gdk_pixbuf_get_rowstride(pb);
	const guchar *src = gdk_pixbuf_read_pixels(pb);
	bool alfa = gdk_pixbuf_get_has_alpha(pb);
	cairo_surface_t *sup = cairo_image_surface_create(alfa ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24, w, h);
	if (cairo_surface_status(sup) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(sup);
		return NULL;
	}
	cairo_surface_flush(sup);
	unsigned char *dst = cairo_image_surface_get_data(sup);
	int dpasso = cairo_image_surface_get_stride(sup);
	for (int y = 0; y < h; y++) {
		const guchar *s = src + (size_t)y * passo;
		uint32_t *d = (uint32_t *)(dst + (size_t)y * dpasso);
		for (int x = 0; x < w; x++, s += canais) {
			uint32_t a = alfa ? s[3] : 255;
			uint32_t r = s[0] * a / 255, g = s[1] * a / 255, b = s[2] * a / 255;
			d[x] = a << 24 | r << 16 | g << 8 | b;
		}
	}
	cairo_surface_mark_dirty(sup);
	return sup;
}

static gpointer trabalhar(gpointer dados) {
	(void)dados;
	for (;;) {
		struct pedido *p = g_async_queue_pop(pedidos);
		struct resultado *r = g_new0(struct resultado, 1);
		r->chave = p->chave;
		char *caminho = NULL;
		if (!strncmp(p->chave, "icone:", 6)) {
			caminho = achar_icone(p->chave + 6, MAX(p->bw, p->bh));
		} else {
			caminho = g_strdup(p->chave);
		}
		GdkPixbufFormat *formato = caminho ? gdk_pixbuf_get_file_info(caminho, &r->nw, &r->nh) : NULL;
		if (formato && r->nw > 0 && r->nh > 0) {
			char *nome = gdk_pixbuf_format_get_name(formato);
			r->vetor = nome && !strcmp(nome, "svg");
			g_free(nome);
			/* o tamanho em que vai aparecer; um bitmap nunca acima do natural */
			double ex = (double)p->bw / r->nw, ey = (double)p->bh / r->nh;
			double e = p->ajuste == GV_AJUSTE_COBRIR ? fmax(ex, ey) : fmin(ex, ey);
			int w, h;
			if (p->ajuste == GV_AJUSTE_ESTICAR) {
				w = p->bw;
				h = p->bh;
			} else {
				w = (int)ceil(r->nw * e);
				h = (int)ceil(r->nh * e);
			}
			if (!r->vetor && (w > r->nw || h > r->nh)) {
				w = r->nw;
				h = r->nh;
			}
			w = MAX(w, 1);
			h = MAX(h, 1);
			GError *erro = NULL;
			GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(caminho, w, h, p->ajuste != GV_AJUSTE_ESTICAR, &erro);
			if (pb) {
				/* a orientação das fotos do celular (EXIF) */
				GdkPixbuf *girada = gdk_pixbuf_apply_embedded_orientation(pb);
				g_object_unref(pb);
				r->sup = para_cairo(girada);
				g_object_unref(girada);
			} else {
				fprintf(stderr, "gravity: %s: %s\n", caminho, erro ? erro->message : "?");
				g_clear_error(&erro);
			}
		} else if (caminho && strncmp(p->chave, "icone:", 6)) {
			fprintf(stderr, "gravity: %s: não é uma imagem que eu saiba abrir\n", caminho);
		}
		g_free(caminho);
		g_free(p);
		g_async_queue_push(resultados, r);
		uint64_t um = 1;
		ssize_t n = write(aviso, &um, sizeof(um));
		(void)n;
	}
	return NULL;
}

/* ---- o lado do quadro ---- */

static void chegaram(gv_app *app, int fd, void *dados) {
	(void)dados;
	uint64_t n;
	ssize_t lido = read(fd, &n, sizeof(n));
	(void)lido;
	struct resultado *r;
	double agora = gv_agora();
	while ((r = g_async_queue_try_pop(resultados))) {
		struct imagem *img = g_hash_table_lookup(imagens, r->chave);
		if (img) {
			img->pedida = false;
			img->nw = r->nw;
			img->nh = r->nh;
			img->vetor = r->vetor;
			if (r->sup) {
				if (img->sup) {
					cairo_surface_destroy(img->sup);
				} else if (img->chegou <= 0) {
					img->chegou = agora;
				}
				img->sup = r->sup;
				img->sw = cairo_image_surface_get_width(r->sup);
				img->sh = cairo_image_surface_get_height(r->sup);
			} else if (!img->sup) {
				img->erro = true;
			}
		} else if (r->sup) {
			cairo_surface_destroy(r->sup);
		}
		g_free(r->chave);
		g_free(r);
	}
	gv_app_redesenhar(app);
}

void gv__imagens_iniciar(gv_app *app) {
	if (imagens) {
		return;
	}
	imagens = g_hash_table_new(g_str_hash, g_str_equal);
	pedidos = g_async_queue_new();
	resultados = g_async_queue_new();
	aviso = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	const char *mb = getenv("GRAVITY_CACHE_MB");
	limite = (size_t)(mb && atoi(mb) > 0 ? atoi(mb) : 48) << 20;
	for (int i = 0; i < TRABALHADORES; i++) {
		g_thread_unref(g_thread_new("gravity-imagens", trabalhar, NULL));
	}
	gv_app_vigiar(app, aviso, chegaram, NULL);
}

/* A imagem desta chave, pedindo-a (ou maior) se ainda não chega para a caixa bw × bh (px lógicos). */
static struct imagem *pedir(const char *chave, float bw, float bh, enum gv_ajuste ajuste) {
	if (!imagens || !chave || !*chave) {
		return NULL;
	}
	struct imagem *img = g_hash_table_lookup(imagens, chave);
	if (!img) {
		img = g_new0(struct imagem, 1);
		img->chave = g_strdup(chave);
		g_hash_table_insert(imagens, img->chave, img);
	}
	img->uso = quadro;
	if (img->erro || img->pedida || bw < 1 || bh < 1) {
		return img;
	}
	int escala = gv__app_escala();
	int pw = (int)ceilf(bw * escala), ph = (int)ceilf(bh * escala);
	bool falta = !img->sup;
	if (img->sup && img->nw > 0) {
		/* o que a caixa pede desta imagem, e se o que temos fica curto (com 15% de folga) */
		double ex = (double)pw / img->nw, ey = (double)ph / img->nh;
		double e = ajuste == GV_AJUSTE_COBRIR ? fmax(ex, ey) : fmin(ex, ey);
		double quer_w = ajuste == GV_AJUSTE_ESTICAR ? pw : img->nw * e;
		if (!img->vetor) {
			quer_w = fmin(quer_w, img->nw);
		}
		falta = quer_w > img->sw * 1.15;
	}
	if (falta) {
		struct pedido *p = g_new0(struct pedido, 1);
		p->chave = g_strdup(chave);
		p->bw = pw;
		p->bh = ph;
		p->ajuste = ajuste;
		img->pedida = true;
		g_async_queue_push(pedidos, p);
	}
	return img;
}

static struct gv_extra *extra_imagem(struct imagem *img, enum gv_ajuste ajuste) {
	struct gv_extra *e = gv__quadro_alloc(sizeof(*e));
	if (!e) {
		return NULL;
	}
	memset(e, 0, sizeof(*e));
	e->tipo = GV_EXTRA_IMAGEM;
	e->imagem.ajuste = ajuste;
	e->imagem.fundo = gv_t.elevada;
	if (img && img->sup) {
		e->imagem.sup = img->sup;
		double t = (gv_agora() - img->chegou) / ESMAECER;
		e->imagem.alfa = gv_t.animacoes ? (float)fmin(1, fmax(0, t)) : 1;
		if (e->imagem.alfa < 1) {
			gv__app_pedir();
		}
	}
	return e;
}

Clay_ElementDeclaration gv_imagem(Clay_ElementId id, const char *caminho, enum gv_ajuste ajuste, Clay_ElementDeclaration d) {
	Clay_ElementData dados = Clay_GetElementData(id);
	struct imagem *img = dados.found ? pedir(caminho, dados.boundingBox.width, dados.boundingBox.height, ajuste) : NULL;
	if (!dados.found) {
		/* primeiro quadro: ainda sem caixa; o próximo pede no tamanho certo */
		gv__app_pedir();
	}
	struct gv_extra *e = extra_imagem(img, ajuste);
	if (e && img && img->erro) {
		e->imagem.sup = NULL;
	}
	/* o Clay manda o retângulo do fundo depois da imagem (taparia-a): o fundo vai com ela, por baixo */
	if (e && d.backgroundColor.a > 0) {
		e->imagem.atras = d.backgroundColor;
	}
	d.backgroundColor = (Clay_Color){ 0 };
	d.custom.customData = e;
	return d;
}

float gv_imagem_proporcao(const char *caminho) {
	struct imagem *img = imagens ? g_hash_table_lookup(imagens, caminho) : NULL;
	if (!img) {
		/* pede pequeno só para saber o tamanho (e já fica uma miniatura) */
		img = pedir(caminho, 64, 64, GV_AJUSTE_CONTER);
	}
	return img && img->nh > 0 ? (float)img->nw / img->nh : 0;
}

void gv_icone_app(const char *nome, float tamanho) {
	char chave[256];
	snprintf(chave, sizeof(chave), "icone:%s", nome ? nome : "");
	struct imagem *img = nome && *nome ? pedir(chave, tamanho, tamanho, GV_AJUSTE_CONTER) : NULL;
	if (!img || img->erro) {
		/* sem ícone no tema: o desenho genérico do LUMNA */
		gv_icone("app", tamanho, gv_t.texto_secundario);
		return;
	}
	struct gv_extra *e = extra_imagem(img, GV_AJUSTE_CONTER);
	if (e) {
		e->imagem.fundo = (Clay_Color){ 0 }; /* um ícone a chegar não leva caixa por trás */
	}
	CLAY_AUTO_ID({
		.layout = { .sizing = { CLAY_SIZING_FIXED(tamanho), CLAY_SIZING_FIXED(tamanho) } },
		.custom = { .customData = e },
	}) {}
}

static void contar(gpointer chave, gpointer valor, gpointer dados) {
	(void)chave;
	struct imagem *img = valor;
	if (img->sup) {
		*(size_t *)dados += (size_t)cairo_image_surface_get_stride(img->sup) * img->sh;
	}
}

static void mais_velha(gpointer chave, gpointer valor, gpointer dados) {
	(void)chave;
	struct imagem *img = valor, **velha = dados;
	if (img->sup && img->uso < quadro && (!*velha || img->uso < (*velha)->uso)) {
		*velha = img;
	}
}

void gv__imagens_fim_do_quadro(void) {
	if (!imagens) {
		return;
	}
	size_t total = 0;
	g_hash_table_foreach(imagens, contar, &total);
	/* passa do limite: saem as que não aparecem há mais tempo (as deste quadro ficam) */
	while (total > limite) {
		struct imagem *velha = NULL;
		g_hash_table_foreach(imagens, mais_velha, &velha);
		if (!velha) {
			break;
		}
		total -= (size_t)cairo_image_surface_get_stride(velha->sup) * velha->sh;
		cairo_surface_destroy(velha->sup);
		velha->sup = NULL;
		velha->sw = velha->sh = 0;
	}
	quadro++;
}
