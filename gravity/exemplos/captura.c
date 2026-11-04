/*
 * lumna-captura: captura de tela. O motor é o grim (a imagem, pelo screencopy do Orbit); a interface é nossa.
 *
 *   lumna-captura            escolher a captura (área, tela inteira ou janela), com a área ajustável
 *   lumna-captura --tela     a tela inteira, direto para a prévia
 *   lumna-captura --janela   começa pelo modo janela
 *   lumna-captura --previa ARQUIVO   (interno) a janela de prévia de uma captura
 *
 * Escolher: congela a tela do monitor do ponteiro (um grim antes de a camada abrir, para ela não sair na foto), escurece, e se arrasta a área;
 * as alças e o interior ajustam, as setas movem (Shift: de 10 em 10). Enter captura, Esc cancela.
 * Prévia: Copiar, Salvar, Salvar como, Enviar (abrir com outro programa; o celular fica reservado) e Descartar. A captura vive em
 * $XDG_RUNTIME_DIR/lumna-capturas até ser salva: fechar a prévia sem salvar apaga o arquivo.
 */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"
#include "json.h"
#include "modos.h"
#include "telas.h"

/* ---------- comum ---------- */

static gv_app *app;
static char aviso[300];

static char *pasta_temp(void) {
	const char *rt = g_get_user_runtime_dir();
	char *p = g_build_filename(rt && *rt ? rt : g_get_tmp_dir(), "lumna-capturas", NULL);
	g_mkdir_with_parents(p, 0700);
	return p;
}

static char *nome_novo(const char *prefixo, const char *ext) {
	GDateTime *t = g_date_time_new_now_local();
	char *hora = g_date_time_format(t, "%Y-%m-%d %H-%M-%S");
	g_date_time_unref(t);
	char *n = g_strdup_printf("%s %s.%s", prefixo, hora, ext);
	g_free(hora);
	return n;
}

static char *pasta_capturas(void) {
	const char *fotos = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	char *base = fotos ? g_strdup(fotos) : g_build_filename(g_get_home_dir(), "Pictures", NULL);
	char *p = g_build_filename(base, "Capturas", NULL);
	g_free(base);
	return p;
}

/* roda e espera; true se saiu com 0 */
static bool rodar(char **argv) {
	int st = 0;
	GError *e = NULL;
	bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &st, &e);
	g_clear_error(&e);
	return ok && g_spawn_check_wait_status(st, NULL);
}

/* ---------- a prévia ---------- */

static char arquivo[600];          /* a captura de agora (temporária até ser salva) */
static bool salva;                 /* já foi guardada: o arquivo é da pessoa e não se apaga */
static int img_l, img_a;
static char nome_como[600];        /* o campo de Salvar como */
static bool pedir_como;

static void descartar_se_nao_salva(void) {
	if (!salva && arquivo[0]) {
		g_unlink(arquivo);
	}
}

static char *copiar_para(const char *destino) {
	char *pasta = g_path_get_dirname(destino), *erro = NULL;
	g_mkdir_with_parents(pasta, 0755);
	g_free(pasta);
	if (g_file_test(destino, G_FILE_TEST_EXISTS)) {
		/* nunca por cima de um arquivo da pessoa: acrescenta -1, -2... */
		char *base = g_strdup(destino), *ponto = strrchr(base, '.');
		char *ext = ponto ? g_strdup(ponto) : g_strdup("");
		if (ponto) {
			*ponto = '\0';
		}
		for (int i = 1; i < 1000; i++) {
			char *c = g_strdup_printf("%s-%d%s", base, i, ext);
			if (!g_file_test(c, G_FILE_TEST_EXISTS)) {
				destino = g_intern_string(c);
				g_free(c);
				break;
			}
			g_free(c);
		}
		g_free(base);
		g_free(ext);
	}
	char *conteudo = NULL;
	gsize n = 0;
	GError *e = NULL;
	if (!g_file_get_contents(arquivo, &conteudo, &n, &e) || !g_file_set_contents(destino, conteudo, (gssize)n, &e)) {
		erro = g_strdup(e ? e->message : "não deu para gravar");
	} else {
		g_strlcpy(nome_como, destino, sizeof(nome_como));
	}
	g_free(conteudo);
	g_clear_error(&e);
	return erro;
}

static void salvar_em(const char *destino) {
	char *erro = copiar_para(destino);
	if (erro) {
		g_snprintf(aviso, sizeof(aviso), "Não consegui salvar: %s", erro);
		g_free(erro);
		return;
	}
	g_unlink(arquivo); /* o temporário já cumpriu: o arquivo agora é o da pessoa */
	g_strlcpy(arquivo, nome_como, sizeof(arquivo));
	salva = true;
	g_snprintf(aviso, sizeof(aviso), "Salvo em %s", arquivo);
}

static void salvar(void) {
	char *pasta = pasta_capturas(), *nome = nome_novo("Captura", "png"), *dest = g_build_filename(pasta, nome, NULL);
	salvar_em(dest);
	g_free(pasta);
	g_free(nome);
	g_free(dest);
}

static void copiar_imagem(void) {
	if (!g_find_program_in_path("wl-copy")) {
		g_strlcpy(aviso, "Falta o wl-copy (pacote wl-clipboard) para copiar a imagem.", sizeof(aviso));
		return;
	}
	char *argv[] = { "sh", "-c", "wl-copy -t image/png < \"$1\"", "sh", arquivo, NULL };
	GError *e = NULL;
	if (g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &e)) {
		g_strlcpy(aviso, "Copiada: é só colar (Ctrl+V).", sizeof(aviso));
	} else {
		g_snprintf(aviso, sizeof(aviso), "Não consegui copiar: %s", e->message);
	}
	g_clear_error(&e);
}

static void abrir_com_outro(void) {
	char *argv[] = { "xdg-open", arquivo, NULL };
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

static void previa_desenhar(gv_app *a, void *dados) {
	(void)dados;
	uint32_t t = gv_tecla();
	uint32_t mods = gv_modificadores();
	bool ctrl = mods & GV_MOD_CTRL;
	if (ctrl && gv_tecla_carregada(XKB_KEY_c)) {
		copiar_imagem();
	} else if (ctrl && gv_tecla_carregada(XKB_KEY_s) && !salva) {
		salvar();
	}
	if (t == XKB_KEY_Escape) {
		if (pedir_como) {
			pedir_como = false;
		} else {
			gv_app_sair(a);
		}
	}
	CLAY(CLAY_ID("cp-raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 12 },
		.backgroundColor = gv_t.fundo }) {
		CLAY(CLAY_ID("cp-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao_ext(CLAY_ID("cp-copiar"), &(gv_botao_opcoes){ .icone = "copy", .texto = "Copiar", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
				copiar_imagem();
			}
			if (gv_botao_ext(CLAY_ID("cp-salvar"), &(gv_botao_opcoes){ .icone = "save", .texto = salva ? "Salva" : "Salvar", .estilo = salva ? GV_BOTAO_ELEVADO : GV_BOTAO_CHEIO, .altura = 38,
				    .desativado = salva })) {
				salvar();
			}
			if (gv_botao_ext(CLAY_ID("cp-como"), &(gv_botao_opcoes){ .icone = "edit", .texto = "Salvar como…", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
				if (!nome_como[0] || !pedir_como) {
					char *pasta = pasta_capturas(), *nome = nome_novo("Captura", "png"), *dest = g_build_filename(pasta, nome, NULL);
					g_strlcpy(nome_como, dest, sizeof(nome_como));
					g_free(pasta);
					g_free(nome);
					g_free(dest);
				}
				pedir_como = !pedir_como;
				if (pedir_como) {
					gv_focar(CLAY_ID("cp-como-campo"));
				}
			}
			bool env = gv_botao_ext(CLAY_ID("cp-enviar"), &(gv_botao_opcoes){ .icone = "external", .texto = "Enviar", .estilo = GV_BOTAO_ELEVADO, .altura = 38 });
			if (env) {
				gv_popup_alternar(CLAY_ID("cp-menu-env"));
			}
			GV_POPUP(CLAY_ID("cp-menu-env"), CLAY_ID("cp-enviar"), .largura = 260) {
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6) } }) {
					if (gv_menu_item(CLAY_ID("cp-env-abrir"), "external", "Abrir com outro programa", NULL)) {
						abrir_com_outro();
						gv_popup_fechar(CLAY_ID("cp-menu-env"));
					}
					gv_texto(CLAY_STRING("  Celular (em breve, pelo LUMNA Connect)"), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (gv_botao_ext(CLAY_ID("cp-descartar"), &(gv_botao_opcoes){ .icone = salva ? "close" : "trash", .texto = salva ? "Fechar" : "Descartar", .estilo = salva ? GV_BOTAO_ELEVADO : GV_BOTAO_PERIGO, .altura = 38 })) {
				gv_app_sair(a);
			}
		}
		if (pedir_como) {
			CLAY(CLAY_ID("cp-como"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					int r = gv_campo(CLAY_ID("cp-como-campo"), nome_como, sizeof(nome_como), "Caminho e nome do arquivo (.png)", GV_CAMPO_TEXTO);
					if ((r & GV_CAMPO_ENTER) && nome_como[0]) {
						salvar_em(nome_como);
						pedir_como = false;
					}
				}
				if (gv_botao_ext(CLAY_ID("cp-como-ok"), &(gv_botao_opcoes){ .texto = "Salvar", .estilo = GV_BOTAO_CHEIO, .altura = 38, .desativado = !nome_como[0] })) {
					salvar_em(nome_como);
					pedir_como = false;
				}
			}
		}
		CLAY(CLAY_ID("cp-imagem-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(12) }) {
			Clay_ElementId im = CLAY_ID("cp-imagem");
			CLAY(im, gv_imagem(im, arquivo, GV_AJUSTE_CONTER, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
		}
		gv_texto(gv_fmt("%d × %d px · %s%s%s", img_l, img_a, salva ? "salva" : "não salva (fechar descarta)", aviso[0] ? " · " : "", aviso), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static int previa(const char *caminho) {
	g_strlcpy(arquivo, caminho, sizeof(arquivo));
	GdkPixbuf *pb = gdk_pixbuf_new_from_file(caminho, NULL);
	if (!pb) {
		fprintf(stderr, "lumna-captura: não consegui abrir %s\n", caminho);
		return 1;
	}
	img_l = gdk_pixbuf_get_width(pb);
	img_a = gdk_pixbuf_get_height(pb);
	g_object_unref(pb);
	int l = img_l / 2 + 60, a = img_a / 2 + 150;
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Captura", .app_id = "lumna-captura", .tipo = GV_JANELA, .largura = l < 760 ? 760 : l > 1280 ? 1280 : l, .altura = a < 520 ? 520 : a > 860 ? 860 : a });
	if (!app) {
		descartar_se_nao_salva();
		return 1;
	}
	gv_app_correr(app, previa_desenhar, NULL);
	descartar_se_nao_salva(); /* fechou sem salvar: some */
	return 0;
}

/* ---------- escolher ---------- */

enum modo { M_AREA, M_TELA, M_JANELA };
enum arrasto { A_NADA, A_CRIAR, A_MOVER, A_ALCA };

struct ret {
	float x, y, w, h;
};

static enum modo modo = M_AREA;
static struct ret sel;
static bool tem_sel;
static enum arrasto arrasto = A_NADA;
static int alca;                    /* 0..7: cantos e meios, a partir do canto de cima à esquerda, no sentido horário */
static Clay_Vector2 ini_p;
static struct ret ini_sel;
static bool estava_premido;
static char congelada[600];         /* a foto da tela inteira do monitor */
static char saida_nome[40];
static float escala_img = 1;        /* pixels da foto por pixel lógico */
static int foto_l, foto_a;
static struct janela {
	struct ret r;
	char titulo[80];
} janelas[64];
static int njanelas;
static int saida_x, saida_y;

static void ler_janelas(void) {
	njanelas = 0;
	char *buf = g_malloc(256 * 1024);
	if (orbit_pedido("janelas", buf, 256 * 1024)) {
		struct json *j = json_ler(buf);
		const struct json *ls = json_chave(j, "janelas");
		for (int i = 0; ls && i < ls->nfilhos && njanelas < 64; i++) {
			const struct json *w = json_item(ls, i);
			if (strcmp(json_chave_texto(w, "saida"), saida_nome) && saida_nome[0]) {
				continue;
			}
			float x = (float)json_numero(json_chave(w, "x"), 0) - saida_x, y = (float)json_numero(json_chave(w, "y"), 0) - saida_y;
			struct janela *d = &janelas[njanelas++];
			d->r = (struct ret){ x, y, (float)json_numero(json_chave(w, "w"), 0), (float)json_numero(json_chave(w, "h"), 0) };
			g_strlcpy(d->titulo, json_chave_texto(w, "titulo"), sizeof(d->titulo));
		}
		json_liberar(j);
	}
	g_free(buf);
}

static bool dentro(const struct ret *r, Clay_Vector2 p) {
	return p.x >= r->x && p.x <= r->x + r->w && p.y >= r->y && p.y <= r->y + r->h;
}

static void normalizar(struct ret *r, float W, float H) {
	if (r->w < 0) {
		r->x += r->w;
		r->w = -r->w;
	}
	if (r->h < 0) {
		r->y += r->h;
		r->h = -r->h;
	}
	if (r->x < 0) {
		r->w += r->x;
		r->x = 0;
	}
	if (r->y < 0) {
		r->h += r->y;
		r->y = 0;
	}
	if (r->x + r->w > W) {
		r->w = W - r->x;
	}
	if (r->y + r->h > H) {
		r->h = H - r->y;
	}
}

/* o centro de cada alça */
static Clay_Vector2 alca_pos(const struct ret *r, int i) {
	float cx = r->x + r->w / 2, cy = r->y + r->h / 2, dx = r->x + r->w, dy = r->y + r->h;
	Clay_Vector2 v[8] = { { r->x, r->y }, { cx, r->y }, { dx, r->y }, { dx, cy }, { dx, dy }, { cx, dy }, { r->x, dy }, { r->x, cy } };
	return v[i];
}

static int alca_sob(const struct ret *r, Clay_Vector2 p) {
	for (int i = 0; i < 8; i++) {
		Clay_Vector2 c = alca_pos(r, i);
		if (fabsf(p.x - c.x) <= 9 && fabsf(p.y - c.y) <= 9) {
			return i;
		}
	}
	return -1;
}

static void capturar(void) {
	if (!tem_sel || sel.w < 2 || sel.h < 2) {
		return;
	}
	GdkPixbuf *pb = gdk_pixbuf_new_from_file(congelada, NULL);
	if (!pb) {
		g_strlcpy(aviso, "Não consegui ler a foto da tela.", sizeof(aviso));
		return;
	}
	int x = (int)lroundf(sel.x * escala_img), y = (int)lroundf(sel.y * escala_img), w = (int)lroundf(sel.w * escala_img), h = (int)lroundf(sel.h * escala_img);
	int PW = gdk_pixbuf_get_width(pb), PH = gdk_pixbuf_get_height(pb);
	x = x < 0 ? 0 : x > PW - 1 ? PW - 1 : x;
	y = y < 0 ? 0 : y > PH - 1 ? PH - 1 : y;
	w = w < 1 ? 1 : x + w > PW ? PW - x : w;
	h = h < 1 ? 1 : y + h > PH ? PH - y : h;
	GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(pb, x, y, w, h);
	char *pasta = pasta_temp(), *nome = nome_novo("captura", "png"), *dest = g_build_filename(pasta, nome, NULL);
	GError *e = NULL;
	if (gdk_pixbuf_save(sub, dest, "png", &e, NULL)) {
		char exe[600];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		exe[n > 0 ? n : 0] = '\0';
		char *argv[] = { n > 0 ? exe : "lumna-captura", "--previa", dest, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
		g_unlink(congelada);
		congelada[0] = '\0';
		gv_app_sair(app);
	} else {
		g_snprintf(aviso, sizeof(aviso), "Não consegui gravar a captura: %s", e->message);
		g_clear_error(&e);
	}
	g_object_unref(sub);
	g_object_unref(pb);
	g_free(pasta);
	g_free(nome);
	g_free(dest);
}

static void definir_modo(enum modo m, float W, float H) {
	modo = m;
	arrasto = A_NADA;
	if (m == M_TELA) {
		sel = (struct ret){ 0, 0, W, H };
		tem_sel = true;
	} else if (m == M_JANELA) {
		tem_sel = false;
		ler_janelas();
	}
}

static void escolher_desenhar(gv_app *a, void *dados) {
	(void)dados;
	float W = gv_app_largura(a), H = gv_app_altura(a);
	uint32_t t = gv_tecla();
	bool shift = gv_modificadores() & GV_MOD_SHIFT;
	float passo = shift ? 10 : 1;
	if (t == XKB_KEY_Escape) {
		gv_app_sair(a);
		return;
	}
	if ((t == XKB_KEY_Return || t == XKB_KEY_KP_Enter) && tem_sel) {
		capturar();
	}
	if (tem_sel && modo == M_AREA && (t == XKB_KEY_Left || t == XKB_KEY_Right || t == XKB_KEY_Up || t == XKB_KEY_Down)) {
		float dx = t == XKB_KEY_Left ? -passo : t == XKB_KEY_Right ? passo : 0, dy = t == XKB_KEY_Up ? -passo : t == XKB_KEY_Down ? passo : 0;
		sel.x += dx;
		sel.y += dy;
		normalizar(&sel, W, H);
	}
	if (W > 0 && foto_l > 0) {
		escala_img = (float)foto_l / W; /* pixels da foto por pixel lógico (a escala do monitor) */
	}
	Clay_ElementId arena = CLAY_ID("cs-arena");
	bool premido = gv_premido(arena);
	Clay_Vector2 p = gv_ponteiro();
	if (modo == M_AREA) {
		if (premido && !estava_premido) {
			ini_p = p;
			ini_sel = sel;
			int h = tem_sel ? alca_sob(&sel, p) : -1;
			if (h >= 0) {
				arrasto = A_ALCA;
				alca = h;
			} else if (tem_sel && dentro(&sel, p)) {
				arrasto = A_MOVER;
			} else {
				arrasto = A_CRIAR;
				sel = (struct ret){ p.x, p.y, 0, 0 };
				tem_sel = true;
			}
		}
		if (premido && arrasto != A_NADA) {
			float dx = p.x - ini_p.x, dy = p.y - ini_p.y;
			if (arrasto == A_CRIAR) {
				float cx = fmaxf(0, fminf(p.x, W)), cy = fmaxf(0, fminf(p.y, H));
				sel = (struct ret){ fminf(ini_p.x, cx), fminf(ini_p.y, cy), fabsf(cx - ini_p.x), fabsf(cy - ini_p.y) };
			} else if (arrasto == A_MOVER) {
				sel = ini_sel;
				sel.x = fmaxf(0, fminf(ini_sel.x + dx, W - ini_sel.w));
				sel.y = fmaxf(0, fminf(ini_sel.y + dy, H - ini_sel.h));
			} else {
				struct ret n = ini_sel;
				float x2 = n.x + n.w, y2 = n.y + n.h;
				if (alca == 0 || alca == 6 || alca == 7) { n.x = ini_sel.x + dx; n.w = x2 - n.x; }
				if (alca == 2 || alca == 3 || alca == 4) { n.w = ini_sel.w + dx; }
				if (alca == 0 || alca == 1 || alca == 2) { n.y = ini_sel.y + dy; n.h = y2 - n.y; }
				if (alca == 4 || alca == 5 || alca == 6) { n.h = ini_sel.h + dy; }
				normalizar(&n, W, H);
				sel = n;
			}
		}
		if (!premido && estava_premido) {
			if (tem_sel && (sel.w < 6 || sel.h < 6)) {
				tem_sel = false; /* um clique solto não é uma área */
			}
			arrasto = A_NADA;
		}
	} else if (modo == M_JANELA && gv_tocado(arena)) {
		/* a janela sob o clique vira a área (que ainda se ajusta) */
		for (int i = 0; i < njanelas; i++) {
			if (dentro(&janelas[i].r, p)) {
				sel = janelas[i].r;
				normalizar(&sel, W, H);
				tem_sel = true;
				modo = M_AREA;
				break;
			}
		}
	}
	estava_premido = premido;

	CLAY(CLAY_ID("cs-raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = { 0, 0, 0, 255 } }) {
		Clay_ElementId foto = CLAY_ID("cs-foto");
		CLAY(arena, gv_imagem(foto, congelada, GV_AJUSTE_ESTICAR, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {}
		struct ret v = sel;
		if (modo == M_JANELA) {
			tem_sel = false;
			for (int i = 0; i < njanelas; i++) {
				if (dentro(&janelas[i].r, p)) {
					v = janelas[i].r;
					normalizar(&v, W, H);
					break;
				}
			}
		}
		bool ha = (modo == M_JANELA) ? v.w > 0 : tem_sel;
		/* o escurecido: tudo, menos a área */
		Clay_Color esc = { 0, 0, 0, 120 };
		struct ret vv = ha ? v : (struct ret){ 0, 0, 0, 0 };
		struct ret partes[4] = { { 0, 0, W, vv.y }, { 0, vv.y + vv.h, W, H - vv.y - vv.h }, { 0, vv.y, vv.x, vv.h }, { vv.x + vv.w, vv.y, W - vv.x - vv.w, vv.h } };
		if (!ha) {
			partes[0] = (struct ret){ 0, 0, W, H };
			partes[1] = partes[2] = partes[3] = (struct ret){ 0, 0, 0, 0 };
		}
		for (int i = 0; i < 4; i++) {
			if (partes[i].w > 0 && partes[i].h > 0) {
				CLAY(CLAY_IDI("cs-esc", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(partes[i].w), CLAY_SIZING_FIXED(partes[i].h) } }, .backgroundColor = esc,
					.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { partes[i].x, partes[i].y }, .zIndex = 10, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
			}
		}
		if (ha) {
			CLAY(CLAY_ID("cs-borda"), { .layout = { .sizing = { CLAY_SIZING_FIXED(v.w), CLAY_SIZING_FIXED(v.h) } },
				.border = { .color = gv_t.acento, .width = { 2, 2, 2, 2, 0 } },
				.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { v.x, v.y }, .zIndex = 12, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
			if (modo == M_AREA) {
				for (int i = 0; i < 8; i++) {
					Clay_Vector2 c = alca_pos(&v, i);
					CLAY(CLAY_IDI("cs-alca", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10) } }, .backgroundColor = { 255, 255, 255, 255 },
						.cornerRadius = CLAY_CORNER_RADIUS(2), .border = { .color = gv_t.acento, .width = { 1, 1, 1, 1, 0 } },
						.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { c.x - 5, c.y - 5 }, .zIndex = 14, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
				}
			}
			/* a medida, em pixels da foto (o que de fato se captura) */
			CLAY(CLAY_ID("cs-medida"), { .layout = { .padding = { 8, 8, 3, 3 } }, .backgroundColor = { 0, 0, 0, 190 }, .cornerRadius = CLAY_CORNER_RADIUS(6),
				.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { v.x, v.y > 30 ? v.y - 28 : v.y + 6 }, .zIndex = 15, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
				gv_texto(gv_fmt("%d × %d", (int)lroundf(v.w * escala_img), (int)lroundf(v.h * escala_img)), gv_t.tamanho_corpo - 2, (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_MEDIO);
			}
		}
		/* a barra: modos à esquerda, capturar e cancelar à direita; embaixo da área (ou da tela) */
		float tx = ha ? v.x : (W - 470) / 2, ty = ha ? v.y + v.h + 12 : H - 120;
		if (ha && ty + 56 > H) {
			ty = v.y > 70 ? v.y - 64 : H - 70;
		}
		tx = fmaxf(8, fminf(tx, W - 480));
		CLAY(CLAY_ID("cs-barra"), { .layout = { .sizing = { CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 8, 8 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(14), .border = { .color = gv_t.borda_controle, .width = { 1, 1, 1, 1, 0 } },
			.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { tx, ty }, .zIndex = 30 } }) {
			static const char *rot[] = { "Área", "Tela", "Janela" }, *ico[] = { "crop", "computer", "window" };
			for (int i = 0; i < 3; i++) {
				if (gv_botao_ext(CLAY_IDI("cs-modo", (uint32_t)i), &(gv_botao_opcoes){ .icone = ico[i], .texto = rot[i], .estilo = modo == (enum modo)i ? GV_BOTAO_CHEIO : GV_BOTAO_SIMPLES, .altura = 36 }) && modo != (enum modo)i) {
					definir_modo((enum modo)i, W, H);
				}
			}
			if (gv_botao_ext(CLAY_ID("cs-capturar"), &(gv_botao_opcoes){ .icone = "image", .texto = "Capturar", .estilo = GV_BOTAO_CHEIO, .altura = 36, .desativado = !tem_sel })) {
				capturar();
			}
			if (gv_botao_ext(CLAY_ID("cs-cancelar"), &(gv_botao_opcoes){ .icone = "close", .estilo = GV_BOTAO_SIMPLES, .altura = 36 })) {
				gv_app_sair(a);
			}
		}
		if (!ha) {
			CLAY(CLAY_ID("cs-dica"), { .layout = { .padding = { 14, 14, 8, 8 } }, .backgroundColor = { 0, 0, 0, 170 }, .cornerRadius = CLAY_CORNER_RADIUS(10),
				.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { W / 2 - 170, H / 2 - 20 }, .zIndex = 20, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
				gv_texto(gv_str(modo == M_JANELA ? "Passe sobre uma janela e clique" : "Arraste para escolher a área  ·  Esc cancela"), gv_t.tamanho_corpo, (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_NORMAL);
			}
		}
		if (aviso[0]) {
			CLAY(CLAY_ID("cs-aviso"), { .layout = { .padding = { 12, 12, 6, 6 } }, .backgroundColor = gv_t.perigo, .cornerRadius = CLAY_CORNER_RADIUS(8),
				.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { 20, 20 }, .zIndex = 40, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
				gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_NORMAL);
			}
		}
	}
}

/* a foto da tela do monitor do ponteiro; false se não deu (e o aviso diz por quê) */
static bool congelar(bool inteira_logo) {
	struct telas_estado est;
	saida_nome[0] = '\0';
	if (telas_ler(&est) && est.n > 0) {
		int alvo = 0;
		for (int i = 0; i < est.n; i++) {
			if (est.v[i].ponteiro) {
				alvo = i;
			}
		}
		g_strlcpy(saida_nome, est.v[alvo].nome, sizeof(saida_nome));
		saida_x = est.v[alvo].x;
		saida_y = est.v[alvo].y;
	}
	(void)inteira_logo;
	char *pasta = pasta_temp(), *nome = nome_novo("tela", "png");
	char *dest = g_build_filename(pasta, nome, NULL);
	bool ok;
	if (saida_nome[0]) {
		char *argv[] = { "grim", "-o", saida_nome, "-t", "png", dest, NULL };
		ok = rodar(argv);
	} else {
		char *argv[] = { "grim", "-t", "png", dest, NULL };
		ok = rodar(argv);
	}
	if (ok) {
		g_strlcpy(congelada, dest, sizeof(congelada));
		GdkPixbuf *pb = gdk_pixbuf_new_from_file(dest, NULL);
		if (pb) {
			foto_l = gdk_pixbuf_get_width(pb);
			foto_a = gdk_pixbuf_get_height(pb);
			g_object_unref(pb);
		}
	}
	g_free(pasta);
	g_free(nome);
	g_free(dest);
	return ok;
}

int main(int argc, char **argv) {
	bool so_tela = false;
	enum modo inicial = M_AREA;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--previa") && i + 1 < argc) {
			return previa(argv[i + 1]);
		} else if (!strcmp(argv[i], "--tela")) {
			so_tela = true;
		} else if (!strcmp(argv[i], "--janela")) {
			inicial = M_JANELA;
		}
	}
	if (!g_find_program_in_path("grim")) {
		fprintf(stderr, "lumna-captura: falta o grim (instale o pacote grim)\n");
		char *av[] = { "notify-send", "Captura de tela", "Falta o programa grim. Instale o pacote “grim”.", NULL };
		g_spawn_async(NULL, av, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
		return 1;
	}
	if (!congelar(so_tela)) {
		fprintf(stderr, "lumna-captura: o grim não conseguiu capturar a tela\n");
		return 1;
	}
	if (so_tela) { /* sem escolher: a tela inteira vai direto para a prévia */
		char exe[600];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		exe[n > 0 ? n : 0] = '\0';
		char *av[] = { n > 0 ? exe : "lumna-captura", "--previa", congelada, NULL };
		return g_spawn_async(NULL, av, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL) ? 0 : 1;
	}
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Captura", .app_id = "lumna-captura-escolher", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY, .ancoras = GV_ANCORA_TUDO,
		.zona_exclusiva = -1, .teclado_exclusivo = true, .saida = saida_nome[0] ? saida_nome : NULL });
	if (!app) {
		g_unlink(congelada);
		return 1;
	}
	modo = inicial;
	if (inicial == M_JANELA) {
		ler_janelas();
	}
	gv_app_correr(app, escolher_desenhar, NULL);
	if (congelada[0]) {
		g_unlink(congelada);
	}
	return 0;
}
