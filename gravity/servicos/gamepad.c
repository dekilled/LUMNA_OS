#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gamepad.h"

#define BIT(arr, b) ((arr)[(b) / 8] & (1 << ((b) % 8)))

/* ---------------- as configurações ---------------- */

void gp_cfg_padrao(struct gp_cfg *c) {
	memset(c, 0, sizeof(*c));
	c->zona_morta = 15;
	c->vibracao = true;
	g_strlcpy(c->guia, "biblioteca", sizeof(c->guia));
	g_strlcpy(c->energia, "performance", sizeof(c->energia));
}

char *gp_cfg_caminho(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "gamepad.ini", NULL);
}

static GKeyFile *ini(void) {
	GKeyFile *kf = g_key_file_new();
	char *a = gp_cfg_caminho();
	g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL);
	g_free(a);
	return kf;
}

static bool ini_salvar(GKeyFile *kf) {
	char *a = gp_cfg_caminho(), *dir = g_path_get_dirname(a);
	g_mkdir_with_parents(dir, 0700);
	bool ok = g_key_file_save_to_file(kf, a, NULL);
	g_free(dir);
	g_free(a);
	return ok;
}

static bool sim(GKeyFile *kf, const char *g, const char *k, bool padrao) {
	char *v = g_key_file_get_string(kf, g, k, NULL);
	bool r = v ? (!strcmp(v, "sim") || !strcmp(v, "true") || !strcmp(v, "1")) : padrao;
	g_free(v);
	return r;
}

static bool valor_ok(const char *v, const char *const *ok) {
	for (int i = 0; v && ok[i]; i++) {
		if (!strcmp(v, ok[i])) {
			return true;
		}
	}
	return false;
}

void gp_cfg_ler(struct gp_cfg *c) {
	gp_cfg_padrao(c);
	GKeyFile *kf = ini();
	if (g_key_file_has_key(kf, "geral", "zona_morta", NULL)) {
		int z = g_key_file_get_integer(kf, "geral", "zona_morta", NULL);
		c->zona_morta = z < 0 ? 0 : z > 50 ? 50 : z;
	}
	c->vibracao = sim(kf, "geral", "vibracao", true);
	c->trocar_ab = sim(kf, "geral", "trocar_ab", false);
	c->fps = sim(kf, "gaming", "fps", false);
	char *g = g_key_file_get_string(kf, "geral", "guia", NULL), *e = g_key_file_get_string(kf, "gaming", "energia", NULL);
	static const char *const guias[] = { "biblioteca", "inicio", "emma", "nada", NULL };
	static const char *const energias[] = { "performance", "balanced", "power-saver", "nao", NULL };
	if (valor_ok(g, guias)) {
		g_strlcpy(c->guia, g, sizeof(c->guia));
	}
	if (valor_ok(e, energias)) {
		g_strlcpy(c->energia, e, sizeof(c->energia));
	}
	g_free(g);
	g_free(e);
	g_key_file_free(kf);
}

bool gp_cfg_gravar(const struct gp_cfg *c) {
	GKeyFile *kf = ini();
	g_key_file_set_integer(kf, "geral", "zona_morta", c->zona_morta < 0 ? 0 : c->zona_morta > 50 ? 50 : c->zona_morta);
	g_key_file_set_string(kf, "geral", "vibracao", c->vibracao ? "sim" : "nao");
	g_key_file_set_string(kf, "geral", "trocar_ab", c->trocar_ab ? "sim" : "nao");
	g_key_file_set_string(kf, "geral", "guia", c->guia);
	g_key_file_set_string(kf, "gaming", "energia", c->energia);
	g_key_file_set_string(kf, "gaming", "fps", c->fps ? "sim" : "nao");
	bool ok = ini_salvar(kf);
	g_key_file_free(kf);
	return ok;
}

void gp_apelidos_ler(struct gp_disp *lista, int n) {
	GKeyFile *kf = ini();
	for (int i = 0; i < n; i++) {
		char *a = g_key_file_get_string(kf, "apelidos", lista[i].id, NULL);
		g_strlcpy(lista[i].apelido, a ? a : "", sizeof(lista[i].apelido));
		g_free(a);
		lista[i].ignorado = sim(kf, "ignorados", lista[i].id, false);
	}
	g_key_file_free(kf);
}

static bool id_ok(const char *id) {
	return id && *id && !strpbrk(id, "=[]\n");
}

bool gp_apelido_gravar(const char *id, const char *apelido) {
	if (!id_ok(id)) {
		return false;
	}
	GKeyFile *kf = ini();
	char *a = g_strdup(apelido ? apelido : "");
	g_strstrip(a);
	for (char *c = a; *c; c++) {
		if (*c == '\n' || *c == '\r') {
			*c = ' ';
		}
	}
	if (*a) {
		g_key_file_set_string(kf, "apelidos", id, a);
	} else {
		g_key_file_remove_key(kf, "apelidos", id, NULL);
	}
	g_free(a);
	bool ok = ini_salvar(kf);
	g_key_file_free(kf);
	return ok;
}

bool gp_ignorar_gravar(const char *id, bool ignorar) {
	if (!id_ok(id)) {
		return false;
	}
	GKeyFile *kf = ini();
	if (ignorar) {
		g_key_file_set_string(kf, "ignorados", id, "sim");
	} else {
		g_key_file_remove_key(kf, "ignorados", id, NULL);
	}
	bool ok = ini_salvar(kf);
	g_key_file_free(kf);
	return ok;
}

const char *gp_botao_nome(int b) {
	static const char *const n[GP_N] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "SELECT", "START", "GUIA", "CIMA", "BAIXO", "ESQ", "DIR" };
	return b >= 0 && b < GP_N ? n[b] : "";
}

/* ---------------- achar os controles ---------------- */

static const char *falsos(void) {
	const char *f = g_getenv("LUMNA_GAMEPAD_FALSOS");
	return f && *f ? f : NULL;
}

static struct gp_disp *conhecido(struct gp_disp *l, int n, const char *caminho) {
	for (int i = 0; i < n; i++) {
		if (!strcmp(l[i].caminho, caminho)) {
			return &l[i];
		}
	}
	return NULL;
}

static void zerar_estado(struct gp_disp *d) {
	memset(d->dir, 0, sizeof(d->dir));
	memset(d->hat, 0, sizeof(d->hat));
	memset(d->gatilho, 0, sizeof(d->gatilho));
	d->eixo[0] = d->eixo[1] = 0;
	d->eixo_min[0] = d->eixo_min[1] = -32768;
	d->eixo_max[0] = d->eixo_max[1] = 32767;
	d->efeito = -1;
}

static bool abrir_real(const char *caminho, struct gp_disp *d) {
	int fd = open(caminho, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		return false;
	}
	unsigned char ev[(EV_MAX + 8) / 8] = { 0 }, key[(KEY_MAX + 8) / 8] = { 0 }, abs_[(ABS_MAX + 8) / 8] = { 0 }, ff[(FF_MAX + 8) / 8] = { 0 };
	ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev);
	ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key);
	ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_)), abs_);
	ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff)), ff);
	/* um controle: botão A (BTN_SOUTH) e o analógico esquerdo; o resto (teclado, mouse, sensores de movimento, touchpad) não é */
	if (!BIT(ev, EV_KEY) || !BIT(ev, EV_ABS) || !BIT(key, BTN_SOUTH) || !BIT(abs_, ABS_X) || !BIT(abs_, ABS_Y)) {
		close(fd);
		return false;
	}
	char nome[96] = "", uniq[64] = "", phys[64] = "";
	struct input_id id = { 0 };
	ioctl(fd, EVIOCGNAME(sizeof(nome)), nome);
	ioctl(fd, EVIOCGUNIQ(sizeof(uniq)), uniq);
	ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys);
	ioctl(fd, EVIOCGID, &id);
	memset(d, 0, sizeof(*d));
	g_strlcpy(d->caminho, caminho, sizeof(d->caminho));
	g_strlcpy(d->nome, nome[0] ? nome : "Controle", sizeof(d->nome));
	g_strlcpy(d->id, uniq[0] ? uniq : phys[0] ? phys : nome, sizeof(d->id));
	d->bluetooth = id.bustype == BUS_BLUETOOTH;
	d->vendor = id.vendor;
	d->product = id.product;
	d->rumble = BIT(ff, FF_RUMBLE) != 0;
	d->fd = fd;
	zerar_estado(d);
	struct input_absinfo ai;
	for (int e = 0; e < 2; e++) {
		if (ioctl(fd, EVIOCGABS(e == 0 ? ABS_X : ABS_Y), &ai) == 0 && ai.maximum > ai.minimum) {
			d->eixo_min[e] = ai.minimum;
			d->eixo_max[e] = ai.maximum;
		}
	}
	return true;
}

static bool abrir_falso(const char *pasta, const char *base, struct gp_disp *d) {
	char *dev = g_strdup_printf("%s/%s.dev", pasta, base), *ev = g_strdup_printf("%s/%s.ev", pasta, base);
	GKeyFile *kf = g_key_file_new();
	bool ok = g_key_file_load_from_file(kf, dev, G_KEY_FILE_NONE, NULL);
	int fd = ok ? open(ev, O_RDWR | O_NONBLOCK | O_CLOEXEC) : -1; /* O_RDWR: um FIFO sem escritor não dá fim de arquivo */
	if (ok && fd >= 0) {
		memset(d, 0, sizeof(*d));
		g_strlcpy(d->caminho, ev, sizeof(d->caminho));
		char *n = g_key_file_get_string(kf, "controle", "nome", NULL), *u = g_key_file_get_string(kf, "controle", "uniq", NULL), *b = g_key_file_get_string(kf, "controle", "bus", NULL);
		g_strlcpy(d->nome, n ? n : "Controle", sizeof(d->nome));
		g_strlcpy(d->id, u && *u ? u : base, sizeof(d->id));
		d->bluetooth = b && !strcmp(b, "bluetooth");
		d->vendor = g_key_file_get_integer(kf, "controle", "vendor", NULL);
		d->product = g_key_file_get_integer(kf, "controle", "product", NULL);
		d->rumble = sim(kf, "controle", "rumble", true);
		d->fd = fd;
		zerar_estado(d);
		g_free(n);
		g_free(u);
		g_free(b);
	} else if (fd >= 0) {
		close(fd);
	}
	g_key_file_free(kf);
	g_free(dev);
	g_free(ev);
	return ok && fd >= 0;
}

int gp_bateria(const struct gp_disp *d) {
	const char *e = g_getenv("LUMNA_POWER_DIR");
	char *base = e && *e ? g_strdup(e) : g_strdup("/sys/class/power_supply");
	int r = -1;
	GDir *dir = g_dir_open(base, 0, NULL);
	const char *nome;
	char *id = g_ascii_strdown(d->id, -1);
	while (dir && (nome = g_dir_read_name(dir)) && r < 0) {
		char *n = g_ascii_strdown(nome, -1);
		/* o nome do item de bateria leva o endereço do controle (ps-controller-battery-AA:BB..., hid-AA:BB...-battery, gip...) */
		if (strchr(id, ':') && strstr(n, id)) { /* só por endereço (AA:BB:...): um nome curto casaria com qualquer bateria */
			char *cap = g_build_filename(base, nome, "capacity", NULL), *t = NULL;
			if (g_file_get_contents(cap, &t, NULL, NULL)) {
				r = atoi(t);
				g_free(t);
			}
			g_free(cap);
		}
		g_free(n);
	}
	if (dir) {
		g_dir_close(dir);
	}
	g_free(id);
	g_free(base);
	if (r < 0 && falsos()) {
		char *dev = g_strdup_printf("%.*sdev", (int)strlen(d->caminho) - 2, d->caminho); /* o .ev falso: o .dev dele fica ao lado */
		GKeyFile *kf = g_key_file_new();
		if (g_key_file_load_from_file(kf, dev, G_KEY_FILE_NONE, NULL) && g_key_file_has_key(kf, "controle", "bateria", NULL)) {
			r = g_key_file_get_integer(kf, "controle", "bateria", NULL);
		}
		g_key_file_free(kf);
		g_free(dev);
	}
	return r > 100 ? 100 : r;
}

int gp_varrer(struct gp_disp *lista, int n, int max) {
	/* quem sumiu (o arquivo não existe mais) é fechado; os novos entram no fim */
	const char *fal = falsos();
	GPtrArray *achados = g_ptr_array_new_with_free_func(g_free);
	if (fal) {
		GDir *d = g_dir_open(fal, 0, NULL);
		const char *nome;
		while (d && (nome = g_dir_read_name(d))) {
			if (g_str_has_suffix(nome, ".dev")) {
				char *base = g_strndup(nome, strlen(nome) - 4);
				g_ptr_array_add(achados, g_strdup_printf("%s/%s.ev", fal, base));
				g_free(base);
			}
		}
		if (d) {
			g_dir_close(d);
		}
	} else {
		GDir *d = g_dir_open("/dev/input", 0, NULL);
		const char *nome;
		while (d && (nome = g_dir_read_name(d))) {
			if (g_str_has_prefix(nome, "event")) {
				g_ptr_array_add(achados, g_strdup_printf("/dev/input/%s", nome));
			}
		}
		if (d) {
			g_dir_close(d);
		}
	}
	for (int i = 0; i < n;) {
		bool existe = false;
		for (guint k = 0; k < achados->len; k++) {
			existe |= !strcmp(achados->pdata[k], lista[i].caminho);
		}
		if (!existe) {
			gp_fechar(&lista[i]);
			memmove(&lista[i], &lista[i + 1], (size_t)(n - i - 1) * sizeof(lista[0]));
			n--;
		} else {
			i++;
		}
	}
	for (guint k = 0; k < achados->len && n < max; k++) {
		const char *c = achados->pdata[k];
		if (conhecido(lista, n, c)) {
			continue;
		}
		struct gp_disp novo;
		bool ok;
		if (fal) {
			char *b = g_path_get_basename(c);
			b[strlen(b) - 3] = '\0';
			ok = abrir_falso(fal, b, &novo);
			g_free(b);
		} else {
			ok = abrir_real(c, &novo);
		}
		if (ok) {
			lista[n++] = novo;
		}
	}
	g_ptr_array_free(achados, TRUE);
	for (int i = 0; i < n; i++) {
		lista[i].bateria = gp_bateria(&lista[i]);
	}
	gp_apelidos_ler(lista, n);
	return n;
}

void gp_fechar(struct gp_disp *d) {
	if (d->fd >= 0) {
		close(d->fd);
	}
	d->fd = -1;
}

/* ---------------- ler e traduzir ---------------- */

static int de_tecla(int code) {
	switch (code) {
	case BTN_SOUTH: return GP_A;
	case BTN_EAST: return GP_B;
	case BTN_NORTH: return GP_X;
	case BTN_WEST: return GP_Y;
	case BTN_TL: return GP_LB;
	case BTN_TR: return GP_RB;
	case BTN_TL2: return GP_LT;
	case BTN_TR2: return GP_RT;
	case BTN_SELECT: return GP_SELECT;
	case BTN_START: return GP_START;
	case BTN_MODE: return GP_GUIA;
	case BTN_DPAD_UP: return GP_CIMA;
	case BTN_DPAD_DOWN: return GP_BAIXO;
	case BTN_DPAD_LEFT: return GP_ESQ;
	case BTN_DPAD_RIGHT: return GP_DIR;
	default: return -1;
	}
}

typedef void (*emite_fn)(const struct gp_disp *, const struct gp_evento *, void *);

static void dizer(const struct gp_disp *d, const struct gp_cfg *c, int botao, bool p, emite_fn emite, void *dados) {
	if (c->trocar_ab && botao == GP_A) {
		botao = GP_B;
	} else if (c->trocar_ab && botao == GP_B) {
		botao = GP_A;
	}
	struct gp_evento e = { botao, p };
	emite(d, &e, dados);
}

/* a direção efetiva de um sentido: o direcional (hat) ou o analógico além da zona morta; só avisa quando muda */
static void direcao(struct gp_disp *d, const struct gp_cfg *c, int sentido, bool ativo_analogico, bool hat, emite_fn emite, void *dados) {
	bool agora = ativo_analogico || hat, antes = d->dir[sentido];
	if (agora != antes) {
		d->dir[sentido] = agora;
		dizer(d, c, GP_CIMA + sentido, agora, emite, dados);
	}
}

static void analogicos(struct gp_disp *d, const struct gp_cfg *c, emite_fn emite, void *dados) {
	double v[2];
	for (int e = 0; e < 2; e++) {
		double meio = (d->eixo_min[e] + d->eixo_max[e]) / 2.0, meia = (d->eixo_max[e] - d->eixo_min[e]) / 2.0;
		v[e] = meia > 0 ? (d->eixo[e] - meio) / meia : 0;
	}
	/* a zona morta é o quanto o analógico precisa andar para contar; a histerese (metade da folga) evita tremer no limite */
	double lim = c->zona_morta / 100.0;
	if (lim < 0.25) {
		lim = 0.25; /* para navegar, nunca menos que um quarto do curso */
	}
	double solta = lim * 0.7;
	direcao(d, c, 0, v[1] < (d->dir[0] ? -solta : -lim), d->hat[0], emite, dados);
	direcao(d, c, 1, v[1] > (d->dir[1] ? solta : lim), d->hat[1], emite, dados);
	direcao(d, c, 2, v[0] < (d->dir[2] ? -solta : -lim), d->hat[2], emite, dados);
	direcao(d, c, 3, v[0] > (d->dir[3] ? solta : lim), d->hat[3], emite, dados);
}

bool gp_ler(struct gp_disp *d, const struct gp_cfg *c, emite_fn emite, void *dados) {
	struct input_event ev[32];
	for (;;) {
		ssize_t n = read(d->fd, ev, sizeof(ev));
		if (n < 0) {
			return errno == EAGAIN || errno == EINTR; /* ENODEV: o controle foi desconectado */
		}
		if (n == 0) {
			return false;
		}
		for (size_t i = 0; i < (size_t)n / sizeof(ev[0]); i++) {
			const struct input_event *e = &ev[i];
			if (e->type == EV_KEY) {
				int b = de_tecla(e->code);
				if (b >= 0 && e->value != 2) {
					/* o direcional que vem como botão e o que vem como hat entram no mesmo estado */
					if (b >= GP_CIMA) {
						d->hat[b - GP_CIMA] = e->value != 0;
						analogicos(d, c, emite, dados);
					} else {
						dizer(d, c, b, e->value != 0, emite, dados);
					}
				}
			} else if (e->type == EV_ABS) {
				if (e->code == ABS_X || e->code == ABS_Y) {
					d->eixo[e->code == ABS_X ? 0 : 1] = e->value;
					analogicos(d, c, emite, dados);
				} else if (e->code == ABS_HAT0X) {
					d->hat[2] = e->value < 0;
					d->hat[3] = e->value > 0;
					analogicos(d, c, emite, dados);
				} else if (e->code == ABS_HAT0Y) {
					d->hat[0] = e->value < 0;
					d->hat[1] = e->value > 0;
					analogicos(d, c, emite, dados);
				} else if (e->code == ABS_Z || e->code == ABS_RZ) {
					/* os gatilhos analógicos viram LT/RT além de metade do curso (0 a 255 ou 0 a 1023: o pico dos mais comuns) */
					int k = e->code == ABS_Z ? 0 : 1;
					bool p = e->value > 127;
					if (p != d->gatilho[k]) {
						d->gatilho[k] = p;
						dizer(d, c, k == 0 ? GP_LT : GP_RT, p, emite, dados);
					}
				}
			}
		}
	}
}

bool gp_vibrar(struct gp_disp *d, int ms) {
	if (!d->rumble) {
		return false;
	}
	if (falsos()) {
		char *log = g_build_filename(falsos(), "vibrou.log", NULL);
		char *l = g_strdup_printf("%s %d\n", d->id, ms);
		FILE *f = fopen(log, "a");
		if (f) {
			fputs(l, f);
			fclose(f);
		}
		g_free(l);
		g_free(log);
		return true;
	}
	int fd = open(d->caminho, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		return false; /* sem permissão de escrita: o controle não vibra daqui (o jogo ainda vibra pelo dele) */
	}
	struct ff_effect fx = { 0 };
	fx.type = FF_RUMBLE;
	fx.id = -1;
	fx.u.rumble.strong_magnitude = 0x8000;
	fx.u.rumble.weak_magnitude = 0x4000;
	fx.replay.length = (uint16_t)(ms < 1 ? 1 : ms > 2000 ? 2000 : ms);
	bool ok = ioctl(fd, EVIOCSFF, &fx) == 0;
	if (ok) {
		struct input_event play = { .type = EV_FF, .code = (uint16_t)fx.id, .value = 1 };
		ok = write(fd, &play, sizeof(play)) == (ssize_t)sizeof(play);
		g_usleep((gulong)(ms * 1000 + 20000)); /* o efeito toca até o fim: só então se descarrega */
		ioctl(fd, EVIOCRMFF, fx.id);
	}
	close(fd);
	return ok;
}
