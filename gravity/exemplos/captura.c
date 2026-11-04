/*
 * lumna-captura: captura de tela. O motor é o grim (a imagem, pelo screencopy do Orbit); a interface é nossa.
 *
 *   lumna-captura            escolher a captura (área, tela inteira ou janela), com a área ajustável
 *   lumna-captura --tela     a tela inteira, direto para a prévia
 *   lumna-captura --janela   começa pelo modo janela
 *   lumna-captura --video    escolher e GRAVAR (a tela inteira ou uma área); Print de novo para parar
 *   lumna-captura --parar    para a gravação que estiver rodando
 *   lumna-captura --para-emma ALVO     escolher o que mostrar à Emma (ALVO: app | popup); o recorte aparece antes de ir
 *   lumna-captura --janela-foco ALVO   mostra à Emma a janela em foco, direto (também confirmando)
 *   lumna-captura --previa ARQUIVO   (interno) a janela de prévia de uma captura ou gravação
 *   (internos da gravação: --gravando, --contagem, --barra)
 *
 * Gravar: o wf-recorder grava (o screencopy do Orbit); a contagem de 3 s, a barra com o tempo e o parar são nossos. A barra só aparece se não
 * cair dentro do que se grava (senão sairia no vídeo): então, numa gravação da tela toda, para-se com Print.
 *
 * Escolher: congela a tela do monitor do ponteiro (um grim antes de a camada abrir, para ela não sair na foto), escurece, e se arrasta a área;
 * as alças e o interior ajustam, as setas movem (Shift: de 10 em 10). Enter captura, Esc cancela.
 * Prévia: Copiar, Salvar, Salvar como, Enviar (abrir com outro programa; o celular fica reservado) e Descartar. A captura vive em
 * $XDG_RUNTIME_DIR/lumna-capturas até ser salva: fechar a prévia sem salvar apaga o arquivo.
 */
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-unix.h>
#include <signal.h>
#include <sys/wait.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "emmac.h"
#include "gravity.h"
#include "json.h"
#include "modos.h"
#include "telas.h"

/* ---------- comum ---------- */

static gv_app *app;
static char aviso[300];
static char modo_emma[16];         /* "app" | "popup": a captura é para mostrar à Emma (a prévia vira o pedido de confirmação) */
static bool eh_video;              /* é uma gravação (mp4/webm/mkv), não uma foto */
static char poster[600];           /* um quadro do vídeo, se o ffmpeg existe */
static char info_video[120];

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
	const char *fotos = g_get_user_special_dir(eh_video ? G_USER_DIRECTORY_VIDEOS : G_USER_DIRECTORY_PICTURES);
	char *base = fotos ? g_strdup(fotos) : g_build_filename(g_get_home_dir(), eh_video ? "Videos" : "Pictures", NULL);
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
	if (poster[0]) {
		g_unlink(poster);
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
	if (poster[0]) {
		g_unlink(poster);
		poster[0] = '\0';
	}
	g_strlcpy(arquivo, nome_como, sizeof(arquivo));
	salva = true;
	g_snprintf(aviso, sizeof(aviso), "Salvo em %s", arquivo);
}

static const char *extensao_do_arquivo(void) {
	const char *p = strrchr(arquivo, '.');
	return p ? p + 1 : "png";
}

static void salvar(void) {
	char *pasta = pasta_capturas(), *nome = nome_novo(eh_video ? "Gravação" : "Captura", extensao_do_arquivo()), *dest = g_build_filename(pasta, nome, NULL);
	salvar_em(dest);
	g_free(pasta);
	g_free(nome);
	g_free(dest);
}

static void copiar_imagem(void) {
	if (eh_video) { /* um vídeo não cabe na área de transferência como imagem: o caminho do arquivo */
		char *argv[] = { "wl-copy", arquivo, NULL };
		if (g_find_program_in_path("wl-copy") && g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL)) {
			g_strlcpy(aviso, "O caminho do arquivo foi copiado.", sizeof(aviso));
		} else {
			g_strlcpy(aviso, "Falta o wl-copy (pacote wl-clipboard).", sizeof(aviso));
		}
		return;
	}
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
		if (modo_emma[0]) { /* só isto vai à Emma: confira antes (uma senha, uma conversa...) */
			CLAY(CLAY_ID("cp-emma-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Isto vai para a Emma. Confira se não tem nada que você não quer mostrar."), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_botao_ext(CLAY_ID("cp-emma-nao"), &(gv_botao_opcoes){ .icone = "close", .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
					gv_app_sair(a);
				}
				if (gv_botao_ext(CLAY_ID("cp-emma-sim"), &(gv_botao_opcoes){ .icone = "send", .texto = "Mostrar à Emma", .estilo = GV_BOTAO_CHEIO, .altura = 38 })) {
					char *p = g_strdup_printf("anexar_emma %s %s", modo_emma, arquivo);
					char r[512] = "";
					bool falou = emmac_pedido(p, r, sizeof(r)); /* sobe a imagem: uma fração de segundo */
					g_free(p);
					struct json *j = falou ? json_ler(r) : NULL;
					if (j && json_bool(json_chave(j, "ok"), false)) {
						json_liberar(j);
						gv_app_sair(a);
						return;
					}
					g_strlcpy(aviso, !falou ? "O serviço da Emma não respondeu." : j && *json_chave_texto(j, "erro") ? json_chave_texto(j, "erro") : "Não consegui mandar a imagem.", sizeof(aviso));
					json_liberar(j);
				}
			}
		}
		if (!modo_emma[0]) CLAY(CLAY_ID("cp-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao_ext(CLAY_ID("cp-copiar"), &(gv_botao_opcoes){ .icone = "copy", .texto = eh_video ? "Copiar caminho" : "Copiar", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
				copiar_imagem();
			}
			if (gv_botao_ext(CLAY_ID("cp-salvar"), &(gv_botao_opcoes){ .icone = "save", .texto = salva ? "Salva" : "Salvar", .estilo = salva ? GV_BOTAO_ELEVADO : GV_BOTAO_CHEIO, .altura = 38,
				    .desativado = salva })) {
				salvar();
			}
			if (gv_botao_ext(CLAY_ID("cp-como"), &(gv_botao_opcoes){ .icone = "edit", .texto = "Salvar como…", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
				if (!nome_como[0] || !pedir_como) {
					char *pasta = pasta_capturas(), *nome = nome_novo(eh_video ? "Gravação" : "Captura", extensao_do_arquivo()), *dest = g_build_filename(pasta, nome, NULL);
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
					int r = gv_campo(CLAY_ID("cp-como-campo"), nome_como, sizeof(nome_como), eh_video ? "Caminho e nome do arquivo (.mp4)" : "Caminho e nome do arquivo (.png)", GV_CAMPO_TEXTO);
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
			CLAY(im, gv_imagem(im, eh_video ? (poster[0] ? poster : "") : arquivo, GV_AJUSTE_CONTER, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } })) {
				if (eh_video && gv_botao_ext(CLAY_ID("cp-tocar"), &(gv_botao_opcoes){ .icone = "play", .texto = "Reproduzir", .estilo = GV_BOTAO_CHEIO, .altura = 44 })) {
					abrir_com_outro();
				}
			}
		}
		gv_texto(gv_fmt("%s · %s%s%s", eh_video ? info_video : gv_fmt("%d × %d px", img_l, img_a).chars, salva ? "salva" : "não salva (fechar descarta)", aviso[0] ? " · " : "", aviso), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static int previa(const char *caminho, const char *alvo_emma) {
	g_strlcpy(arquivo, caminho, sizeof(arquivo));
	if (alvo_emma) {
		g_strlcpy(modo_emma, alvo_emma, sizeof(modo_emma));
	}
	eh_video = g_str_has_suffix(caminho, ".mp4") || g_str_has_suffix(caminho, ".webm") || g_str_has_suffix(caminho, ".mkv");
	int l = 960, a = 640;
	if (eh_video) {
		GStatBuf st;
		g_stat(caminho, &st);
		g_snprintf(info_video, sizeof(info_video), "Gravação de %.1f MB", (double)st.st_size / (1024 * 1024));
		if (g_find_program_in_path("ffmpeg")) { /* um quadro do começo, para ver o que é */
			char *pasta = pasta_temp(), *nome = nome_novo("quadro", "png");
			char *dest = g_build_filename(pasta, nome, NULL);
			char *argv[] = { "ffmpeg", "-y", "-loglevel", "error", "-i", (char *)caminho, "-frames:v", "1", dest, NULL };
			if (rodar(argv)) {
				g_strlcpy(poster, dest, sizeof(poster));
			}
			g_free(pasta);
			g_free(nome);
			g_free(dest);
		}
	} else {
		GdkPixbuf *pb = gdk_pixbuf_new_from_file(caminho, NULL);
		if (!pb) {
			fprintf(stderr, "lumna-captura: não consegui abrir %s\n", caminho);
			return 1;
		}
		img_l = gdk_pixbuf_get_width(pb);
		img_a = gdk_pixbuf_get_height(pb);
		g_object_unref(pb);
		l = img_l / 2 + 60;
		a = img_a / 2 + 150;
	}
	app = gv_app_criar(&(gv_opcoes){ .titulo = modo_emma[0] ? "Mostrar à Emma" : eh_video ? "Gravação" : "Captura", .app_id = "lumna-captura", .tipo = GV_JANELA, .largura = l < 760 ? 760 : l > 1280 ? 1280 : l,
		.altura = a < 520 ? 520 : a > 860 ? 860 : a });
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

static bool modo_video;           /* a barra está em Vídeo: capturar vira gravar */
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

static void gravar_area(void);

static void capturar(void) {
	if (!tem_sel || sel.w < 2 || sel.h < 2) {
		return;
	}
	if (modo_video) {
		gravar_area();
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
		char *argv[] = { n > 0 ? exe : "lumna-captura", modo_emma[0] ? "--confirmar-emma" : "--previa", dest, modo_emma[0] ? modo_emma : NULL, NULL };
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
		float tx = ha ? v.x : (W - 640) / 2, ty = ha ? v.y + v.h + 12 : H - 120;
		if (ha && ty + 56 > H) {
			ty = v.y > 70 ? v.y - 64 : H - 70;
		}
		tx = fmaxf(8, fminf(tx, W - 640));
		CLAY(CLAY_ID("cs-barra"), { .layout = { .sizing = { CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 8, 8 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(14), .border = { .color = gv_t.borda_controle, .width = { 1, 1, 1, 1, 0 } },
			.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { tx, ty }, .zIndex = 30 } }) {
			for (int v2 = 0; v2 < 2 && !modo_emma[0]; v2++) {
				if (gv_botao_ext(CLAY_IDI("cs-tipo", (uint32_t)v2), &(gv_botao_opcoes){ .icone = v2 ? "video" : "camera", .texto = v2 ? "Vídeo" : "Foto", .estilo = modo_video == (bool)v2 ? GV_BOTAO_CHEIO : GV_BOTAO_SIMPLES, .altura = 36 })) {
					modo_video = v2;
				}
			}
			static const char *rot[] = { "Área", "Tela", "Janela" }, *ico[] = { "crop", "computer", "window" };
			for (int i = 0; i < 3; i++) {
				if (gv_botao_ext(CLAY_IDI("cs-modo", (uint32_t)i), &(gv_botao_opcoes){ .icone = ico[i], .texto = rot[i], .estilo = modo == (enum modo)i ? GV_BOTAO_CHEIO : GV_BOTAO_SIMPLES, .altura = 36 }) && modo != (enum modo)i) {
					definir_modo((enum modo)i, W, H);
				}
			}
			if (gv_botao_ext(CLAY_ID("cs-capturar"), &(gv_botao_opcoes){ .icone = modo_video ? "video" : "image", .texto = modo_video ? "Gravar" : "Capturar", .estilo = GV_BOTAO_CHEIO, .altura = 36, .desativado = !tem_sel })) {
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

/* ---------- gravar ---------- */

static char *exe_proprio(void) {
	char exe[600];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	exe[n > 0 ? n : 0] = '\0';
	return g_strdup(n > 0 ? exe : "lumna-captura");
}

static char *arq_pid(void) {
	const char *rt = g_get_user_runtime_dir();
	return g_build_filename(rt && *rt ? rt : g_get_tmp_dir(), "lumna-captura-gravando.pid", NULL);
}

/* o pid de quem está gravando agora (0: ninguém) */
static pid_t pid_gravando(void) {
	char *f = arq_pid(), *t = NULL;
	pid_t pid = 0;
	if (g_file_get_contents(f, &t, NULL, NULL)) {
		pid = (pid_t)atoi(t);
		if (pid > 1 && kill(pid, 0) != 0) {
			pid = 0; /* sobrou de uma gravação que morreu */
		}
	}
	g_free(t);
	g_free(f);
	return pid > 1 ? pid : 0;
}

static void avisar(const char *texto) {
	char *av[] = { "notify-send", "-a", "Captura de tela", "Captura de tela", (char *)texto, NULL };
	g_spawn_async(NULL, av, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
}

static void gravar_area(void) {
	/* a área em pixels lógicos da saída; o gravador quer as coordenadas do layout, e o controlador faz a conta */
	char *exe = exe_proprio();
	char ox[16], oy[16], lx[16], ly[16], w[16], h[16];
	g_snprintf(ox, sizeof(ox), "%d", saida_x);
	g_snprintf(oy, sizeof(oy), "%d", saida_y);
	g_snprintf(lx, sizeof(lx), "%d", (int)lroundf(sel.x));
	g_snprintf(ly, sizeof(ly), "%d", (int)lroundf(sel.y));
	g_snprintf(w, sizeof(w), "%d", (int)lroundf(sel.w));
	g_snprintf(h, sizeof(h), "%d", (int)lroundf(sel.h));
	char *argv[] = { exe, "--gravando", saida_nome[0] ? saida_nome : "-", ox, oy, lx, ly, w, h, NULL };
	g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
	g_free(exe);
	if (congelada[0]) {
		g_unlink(congelada);
		congelada[0] = '\0';
	}
	gv_app_sair(app);
}

/* o controlador: sem janela; conta, grava e espera o pedido de parar (o botão da barra, Print de novo ou --parar) */
struct grav {
	GMainLoop *laco;
	GPid contagem, rec, barra;
	char arquivo[600];
	bool parar_pedido;
	char saida[40];
	int ox, oy, lx, ly, w, h;
};

static void grav_fim(struct grav *g) {
	if (g->barra) {
		kill(g->barra, SIGTERM);
	}
	char *f = arq_pid();
	g_unlink(f);
	g_free(f);
	GStatBuf st;
	if (g_stat(g->arquivo, &st) == 0 && st.st_size > 1024) {
		char *exe = exe_proprio();
		char *argv[] = { exe, "--previa", g->arquivo, NULL };
		g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
		g_free(exe);
	} else {
		g_unlink(g->arquivo);
		avisar("A gravação saiu vazia: confira se o wf-recorder está instalado e funcionando.");
	}
	g_main_loop_quit(g->laco);
}

static void rec_saiu(GPid pid, gint st, gpointer d) {
	struct grav *g = d;
	g_spawn_close_pid(pid);
	g->rec = 0;
	grav_fim(g);
}

static gboolean grav_parar(gpointer d) {
	struct grav *g = d;
	g->parar_pedido = true;
	if (g->rec) {
		kill(g->rec, SIGINT); /* o wf-recorder fecha o arquivo direito ao receber o SIGINT */
	} else if (g->contagem) {
		kill(g->contagem, SIGTERM); /* desistiu na contagem */
	}
	return G_SOURCE_CONTINUE;
}

static void barra_saiu(GPid pid, gint st, gpointer d) {
	struct grav *g = d;
	g_spawn_close_pid(pid);
	g->barra = 0;
}

static void contagem_saiu(GPid pid, gint st, gpointer d) {
	struct grav *g = d;
	g_spawn_close_pid(pid);
	g->contagem = 0;
	if (g->parar_pedido || !g_spawn_check_wait_status(st, NULL)) {
		char *f = arq_pid();
		g_unlink(f);
		g_free(f);
		g_main_loop_quit(g->laco);
		return;
	}
	char *pasta = pasta_temp(), *nome = nome_novo("gravacao", "mp4");
	g_snprintf(g->arquivo, sizeof(g->arquivo), "%s/%s", pasta, nome);
	g_free(pasta);
	g_free(nome);
	char geom[64];
	g_snprintf(geom, sizeof(geom), "%d,%d %dx%d", g->ox + g->lx, g->oy + g->ly, g->w, g->h);
	char *argv[10];
	int n = 0;
	argv[n++] = "wf-recorder";
	if (strcmp(g->saida, "-")) {
		argv[n++] = "-o";
		argv[n++] = g->saida;
	}
	argv[n++] = "-g";
	argv[n++] = geom;
	argv[n++] = "-f";
	argv[n++] = g->arquivo;
	argv[n] = NULL;
	GError *e = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &g->rec, &e)) {
		avisar("Não consegui iniciar a gravação: falta o wf-recorder (instale o pacote wf-recorder).");
		g_clear_error(&e);
		char *f = arq_pid();
		g_unlink(f);
		g_free(f);
		g_main_loop_quit(g->laco);
		return;
	}
	g_child_watch_add(g->rec, rec_saiu, g);
	/* a barra só se não cair dentro do que se grava: senão sairia no vídeo (e então se para com Print). Ela fica no topo, no meio da tela */
	struct telas_estado est;
	int W = 1920;
	if (telas_ler(&est)) {
		for (int i = 0; i < est.n; i++) {
			if (!strcmp(est.v[i].nome, g->saida) || !strcmp(g->saida, "-")) {
				int lw, lh;
				tela_tamanho_logico(&est.v[i], &lw, &lh);
				W = lw;
				break;
			}
		}
	}
	int bx = W / 2 - 130, by = 4, bw = 260, bh = 70;
	bool cai = !(g->lx + g->w <= bx || g->lx >= bx + bw || g->ly + g->h <= by || g->ly >= by + bh);
	if (!cai) {
		char *exe = exe_proprio();
		char *bargv[] = { exe, "--barra", g->saida, NULL };
		if (g_spawn_async(NULL, bargv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &g->barra, NULL)) {
			g_child_watch_add(g->barra, barra_saiu, g);
		}
		g_free(exe);
	}
	avisar(cai ? "Gravando. Aperte Print para parar." : "Gravando. Pare pela barra ou apertando Print.");
}

static int gravando(char **a) {
	struct grav g = { 0 };
	g_strlcpy(g.saida, a[0], sizeof(g.saida));
	g.ox = atoi(a[1]);
	g.oy = atoi(a[2]);
	g.lx = atoi(a[3]);
	g.ly = atoi(a[4]);
	g.w = atoi(a[5]);
	g.h = atoi(a[6]);
	char *f = arq_pid(), *pid = g_strdup_printf("%d", (int)getpid());
	g_file_set_contents(f, pid, -1, NULL);
	g_free(f);
	g_free(pid);
	g.laco = g_main_loop_new(NULL, FALSE);
	g_unix_signal_add(SIGUSR1, grav_parar, &g);
	g_unix_signal_add(SIGINT, grav_parar, &g);
	g_unix_signal_add(SIGTERM, grav_parar, &g);
	char *exe = exe_proprio(), lx[16], ly[16], w[16], h[16];
	g_snprintf(lx, sizeof(lx), "%d", g.lx);
	g_snprintf(ly, sizeof(ly), "%d", g.ly);
	g_snprintf(w, sizeof(w), "%d", g.w);
	g_snprintf(h, sizeof(h), "%d", g.h);
	char *cargv[] = { exe, "--contagem", lx, ly, w, h, g.saida, NULL };
	if (!g_spawn_async(NULL, cargv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &g.contagem, NULL)) {
		g_free(exe);
		return 1;
	}
	g_free(exe);
	g_child_watch_add(g.contagem, contagem_saiu, &g);
	g_main_loop_run(g.laco);
	return 0;
}

/* a contagem regressiva: 3, 2, 1 no meio do que vai ser gravado, e a moldura dele; sai sozinha (código 0) */
static float t_ini;
static int cg_x, cg_y, cg_w, cg_h;

static void contagem_desenhar(gv_app *a, void *dados) {
	(void)dados;
	float t = gv_agora() - t_ini;
	int n = 3 - (int)t;
	if (n < 1) {
		gv_app_sair(a);
		return;
	}
	gv_app_redesenhar(a);
	gv_acordar_em(0.1);
	CLAY(CLAY_ID("cg-raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("cg-moldura"), { .layout = { .sizing = { CLAY_SIZING_FIXED((float)cg_w), CLAY_SIZING_FIXED((float)cg_h) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_t.perigo, .width = { 2, 2, 2, 2, 0 } },
			.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .offset = { (float)cg_x, (float)cg_y }, .zIndex = 5, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
			CLAY(CLAY_ID("cg-num"), { .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(96) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = { 0, 0, 0, 170 }, .cornerRadius = CLAY_CORNER_RADIUS(48) }) {
				gv_texto(gv_fmt("%d", n), 56, (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_MEDIO);
			}
		}
	}
}

static int contagem_ui(char **a) {
	cg_x = atoi(a[0]);
	cg_y = atoi(a[1]);
	cg_w = atoi(a[2]);
	cg_h = atoi(a[3]);
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Contagem", .app_id = "lumna-captura-contagem", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY, .ancoras = GV_ANCORA_TUDO,
		.zona_exclusiva = -1, .saida = strcmp(a[4], "-") ? a[4] : NULL });
	if (!app) {
		return 1;
	}
	t_ini = gv_agora();
	return gv_app_correr(app, contagem_desenhar, NULL);
}

/* a barra da gravação: o ponto vermelho, o tempo e Parar (em cima, no meio; só existe se não sai no vídeo) */
static void barra_desenhar(gv_app *a, void *dados) {
	(void)dados;
	(void)a;
	float t = gv_agora() - t_ini;
	gv_acordar_em(1.0);
	int seg = (int)t;
	CLAY(CLAY_ID("br-raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 6, 6 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(16), .border = { .color = gv_t.borda_controle, .width = { 1, 1, 1, 1, 0 } } }) {
		CLAY(CLAY_ID("br-ponto"), { .layout = { .sizing = { CLAY_SIZING_FIXED(12), CLAY_SIZING_FIXED(12) } }, .backgroundColor = gv_alfa(gv_t.perigo, (seg & 1) ? 0.45f : 1.0f), .cornerRadius = CLAY_CORNER_RADIUS(6) }) {}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_fmt("%02d:%02d", seg / 60, seg % 60), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		}
		if (gv_botao_ext(CLAY_ID("br-parar"), &(gv_botao_opcoes){ .icone = "close", .texto = "Parar", .estilo = GV_BOTAO_PERIGO, .altura = 36 })) {
			pid_t p = pid_gravando();
			if (p) {
				kill(p, SIGUSR1);
			}
		}
	}
}

static int barra_ui(char **a) {
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Gravando", .app_id = "lumna-captura-barra", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY, .ancoras = GV_ANCORA_CIMA, .largura = 240, .altura = 52,
		.zona_exclusiva = -1, .margens = { 10, 0, 0, 0 }, .saida = strcmp(a[0], "-") ? a[0] : NULL });
	if (!app) {
		return 1;
	}
	t_ini = gv_agora();
	return gv_app_correr(app, barra_desenhar, NULL);
}

/* a janela em foco, direto: o grim recorta pela geometria que o Orbit diz; depois, o mesmo pedido de confirmação */
static int janela_foco(const char *alvo) {
	char *buf = g_malloc(256 * 1024);
	int x = 0, y = 0, w = 0, h = 0;
	bool achou = false;
	if (orbit_pedido("janelas", buf, 256 * 1024)) {
		struct json *j = json_ler(buf);
		const struct json *ls = json_chave(j, "janelas");
		for (int i = 0; ls && i < ls->nfilhos; i++) {
			const struct json *w2 = json_item(ls, i);
			if (json_bool(json_chave(w2, "foco"), false)) {
				x = (int)json_numero(json_chave(w2, "x"), 0);
				y = (int)json_numero(json_chave(w2, "y"), 0);
				w = (int)json_numero(json_chave(w2, "w"), 0);
				h = (int)json_numero(json_chave(w2, "h"), 0);
				achou = w > 0 && h > 0;
				break;
			}
		}
		json_liberar(j);
	}
	g_free(buf);
	if (!achou) {
		avisar("Não achei uma janela em foco para mostrar à Emma.");
		return 1;
	}
	if (!g_find_program_in_path("grim")) {
		avisar("Falta o grim (pacote grim) para capturar a janela.");
		return 1;
	}
	char *pasta = pasta_temp(), *nome = nome_novo("janela", "png"), *dest = g_build_filename(pasta, nome, NULL), geom[64];
	g_snprintf(geom, sizeof(geom), "%d,%d %dx%d", x, y, w, h);
	char *argv[] = { "grim", "-g", geom, "-t", "png", dest, NULL };
	int rc = 1;
	if (rodar(argv)) {
		char *exe = exe_proprio();
		char *av[] = { exe, "--confirmar-emma", dest, (char *)alvo, NULL };
		rc = g_spawn_async(NULL, av, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL) ? 0 : 1;
		g_free(exe);
	} else {
		avisar("O grim não conseguiu capturar a janela.");
	}
	g_free(pasta);
	g_free(nome);
	g_free(dest);
	return rc;
}

int main(int argc, char **argv) {
	bool so_tela = false;
	enum modo inicial = M_AREA;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--previa") && i + 1 < argc) {
			return previa(argv[i + 1], NULL);
		} else if (!strcmp(argv[i], "--confirmar-emma") && i + 2 < argc) {
			return previa(argv[i + 1], argv[i + 2]);
		} else if (!strcmp(argv[i], "--para-emma") && i + 1 < argc) {
			g_strlcpy(modo_emma, argv[++i], sizeof(modo_emma));
		} else if (!strcmp(argv[i], "--janela-foco") && i + 1 < argc) {
			return janela_foco(argv[i + 1]);
		} else if (!strcmp(argv[i], "--gravando") && i + 7 < argc) {
			return gravando(argv + i + 1);
		} else if (!strcmp(argv[i], "--contagem") && i + 5 < argc) {
			return contagem_ui(argv + i + 1);
		} else if (!strcmp(argv[i], "--barra") && i + 1 < argc) {
			return barra_ui(argv + i + 1);
		} else if (!strcmp(argv[i], "--parar")) {
			pid_t p = pid_gravando();
			return p && kill(p, SIGUSR1) == 0 ? 0 : 1;
		} else if (!strcmp(argv[i], "--video")) {
			modo_video = true;
		} else if (!strcmp(argv[i], "--tela")) {
			so_tela = true;
		} else if (!strcmp(argv[i], "--janela")) {
			inicial = M_JANELA;
		}
	}
	pid_t gravando_ja = pid_gravando(); /* Print enquanto grava: para a gravação (em vez de abrir outra escolha) */
	if (gravando_ja) {
		return kill(gravando_ja, SIGUSR1) == 0 ? 0 : 1;
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
