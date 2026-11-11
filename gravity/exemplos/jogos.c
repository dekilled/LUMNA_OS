/*
 * lumna-jogos: a biblioteca do modo Gaming. Início (o jogo em destaque e "Seus jogos"), Biblioteca (todos, com filtros por origem),
 * Detalhes (Principal e Propriedades) e a Desinstalação com confirmação. Lê a Steam, o Lutris, o Heroic e os jogos avulsos
 * (servicos/jogos.h). O app_id "lumna-jogos" faz o Orbit abrir a janela na tela que está em Gaming. Teclado: setas, Enter, Esc.
 *
 *   lumna-jogos [--tela inicio|biblioteca] [--jogo ID]     para testes: abre direto numa tela
 */
#include <cairo.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"
#include "bluetooth.h"
#include "gamepad.h"
#include "gamepadc.h"
#include "jogos.h"
#include "json.h"
#include "modos.h"

enum { T_INICIO, T_BIBLIOTECA, T_DETALHES, T_CONFIG, T_LOJA, T_BUSCA, T_LOJA_JOGO };
enum { CP_CONTROLES, CP_AJUSTES, CP_GUIA, CP_GAMING, CP_STEAM, CP_GOG, CP_EPIC, CP_N };
enum { LS_SUB_DEST, LS_SUB_CATEG, LS_SUB_OFERTAS, LS_SUB_DESEJOS, LS_SUB_EPIC, LS_SUB_N };
static int l_sub;
enum { ABA_PRINCIPAL, ABA_PROPRIEDADES };
enum { F_TODOS, F_FAVORITOS, F_STEAM, F_LUTRIS, F_HEROIC, F_AVULSOS, F_GOG, F_EPIC, F_N };

static float K = 1.0f;
#define U(n) ((float)(n) * K)

static gv_app *app;
static struct jogo lista[JOGOS_MAX];
static int njogos;
static int tela = T_INICIO, aba = ABA_PRINCIPAL, filtro = F_TODOS;
static int sel;              /* o card em foco (índice na lista que aparece na tela) */
static char det_id[64];      /* o jogo aberto em Detalhes */
static struct jogo ed;       /* a cópia que as Propriedades editam */
static bool ed_suja, ed_viva;
static char aviso[200];
static double aviso_ate, relista_em;
static bool confirmando, apagar_arquivos, adicionando;
static char add_nome[128], add_exec[400];
static char env_k[JOGO_ENV_MAX][48], env_v[JOGO_ENV_MAX][120];

/* ---- configurações do Gaming ---- */
struct ctl {
	char id[64], nome[96], apelido[48];
	bool bluetooth, vibra, ignorado;
	int bateria;
};
static int cfg_pg;
static struct gp_cfg gcfg;
static struct ctl ctls[GP_MAX];
static int nctl;
static bool servico_pad;
static double pad_lido_em;
static bool bt_ligado;
static char renomeando[64], apelido_ed[48], remover_bt[96], teste_msg[160];
static bool dnd_gaming;

static const char *const filtros_nome[F_N] = { "Todos", "Favoritos", "Steam", "Lutris", "Heroic", "Avulsos", "GOG", "Epic" };

/* ---------------- o que aparece ---------------- */

static bool passa_filtro(const struct jogo *j) {
	switch (filtro) {
	case F_FAVORITOS: return j->favorito;
	case F_STEAM: return j->origem == ORIGEM_STEAM;
	case F_LUTRIS: return j->origem == ORIGEM_LUTRIS;
	case F_HEROIC: return j->origem == ORIGEM_HEROIC;
	case F_AVULSOS: return j->origem == ORIGEM_AVULSO;
	case F_GOG: return j->origem == ORIGEM_GOG;
	case F_EPIC: return j->origem == ORIGEM_EPIC;
	default: return true;
	}
}

static int visiveis(int *idx) {
	int n = 0;
	for (int i = 0; i < njogos; i++) {
		if (passa_filtro(&lista[i])) {
			idx[n++] = i;
		}
	}
	return n;
}

static struct jogo *por_id(const char *id) {
	for (int i = 0; i < njogos; i++) {
		if (!strcmp(lista[i].id, id)) {
			return &lista[i];
		}
	}
	return NULL;
}

/* o jogo em destaque: o último que se jogou (senão o primeiro) */
static struct jogo *destaque(void) {
	struct jogo *d = NULL;
	for (int i = 0; i < njogos; i++) {
		if (!d || lista[i].ultima > d->ultima) {
			d = &lista[i];
		}
	}
	return d;
}

/* "Seus jogos": os mais recentes primeiro */
static int recentes(int *idx, int max) {
	int n = 0;
	for (int i = 0; i < njogos; i++) {
		idx[n++] = i;
	}
	for (int a = 0; a < n; a++) {
		for (int b = a + 1; b < n; b++) {
			if (lista[idx[b]].ultima > lista[idx[a]].ultima) {
				int t = idx[a];
				idx[a] = idx[b];
				idx[b] = t;
			}
		}
	}
	return n < max ? n : max;
}

static void papel_ler(void);

static void relistar(void) {
	jogos_listar(lista, 0); /* (o valor de max 0 não lê nada: só garante o símbolo) */
	njogos = jogos_listar(lista, JOGOS_MAX);
	relista_em = gv_agora() + 5.0;
	papel_ler();
	if (det_id[0] && !por_id(det_id) && tela == T_DETALHES) {
		tela = T_BIBLIOTECA; /* o jogo saiu (foi desinstalado) */
		det_id[0] = '\0';
		ed_viva = false;
	}
}

static void avisar(const char *t) {
	g_strlcpy(aviso, t, sizeof(aviso));
	aviso_ate = gv_agora() + 4.0;
}

static const char *quando(int64_t t) {
	if (t <= 0) {
		return "Nunca jogado";
	}
	int64_t d = (int64_t)time(NULL) - t;
	if (d < 3600) {
		return "Há poucos minutos";
	}
	if (d < 86400) {
		return gv_fmt("Há %d h", (int)(d / 3600)).chars;
	}
	if (d < 2 * 86400) {
		return "Ontem";
	}
	return gv_fmt("Há %d dias", (int)(d / 86400)).chars;
}

static const char *tamanho_txt(int64_t b) {
	if (b <= 0) {
		return "—";
	}
	return b >= 1000000000LL ? gv_fmt("%.1f GB", (double)b / 1e9).chars : gv_fmt("%.0f MB", (double)b / 1e6).chars;
}

static bool tecla(uint32_t s) {
	return gv_tecla_carregada(s);
}

static void abrir_detalhes(const struct jogo *j) {
	g_strlcpy(det_id, j->id, sizeof(det_id));
	tela = T_DETALHES;
	aba = ABA_PRINCIPAL;
	ed_viva = false;
	confirmando = false;
}

static void abrir_config(void);
static void l_abrir_loja(void);
static void l_abrir_busca(int genero);
static int l_downloads(void);

static void abrir(const struct jogo *j) {
	char erro[200] = "";
	if (jogos_lancar(j, erro, sizeof(erro))) {
		avisar(gv_fmt("Abrindo %s…", j->nome).chars);
		relistar();
	} else {
		avisar(erro);
	}
}

/* ---------------- o fundo das telas ---------------- */

/* o papel de parede da Biblioteca (~/.config/lumna/jogos/ui.ini): "auto" (a arte do jogo em destaque), "imagem" (um arquivo) ou "nenhum" */
static struct {
	int modo; /* 0 auto, 1 imagem, 2 nenhum */
	char imagem[400];
} papel;

static char *ui_ini(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "jogos", "ui.ini", NULL);
}

static void papel_ler(void) {
	GKeyFile *kf = g_key_file_new();
	char *a = ui_ini();
	papel.modo = 0;
	papel.imagem[0] = '\0';
	if (g_key_file_load_from_file(kf, a, G_KEY_FILE_NONE, NULL)) {
		char *m = g_key_file_get_string(kf, "biblioteca", "papel", NULL), *i = g_key_file_get_string(kf, "biblioteca", "imagem", NULL);
		papel.modo = m && !strcmp(m, "imagem") ? 1 : m && !strcmp(m, "nenhum") ? 2 : 0;
		g_strlcpy(papel.imagem, i ? i : "", sizeof(papel.imagem));
		g_free(m);
		g_free(i);
	}
	g_free(a);
	g_key_file_free(kf);
}

static void papel_gravar(void) {
	GKeyFile *kf = g_key_file_new();
	char *a = ui_ini(), *dir = g_path_get_dirname(a);
	g_key_file_set_string(kf, "biblioteca", "papel", papel.modo == 1 ? "imagem" : papel.modo == 2 ? "nenhum" : "auto");
	g_key_file_set_string(kf, "biblioteca", "imagem", papel.imagem);
	g_mkdir_with_parents(dir, 0700);
	g_key_file_save_to_file(kf, a, NULL);
	g_free(dir);
	g_free(a);
	g_key_file_free(kf);
}

/* o arquivo do papel de parede da Biblioteca ("" : só o fundo escuro) */
static const char *papel_arquivo(void) {
	if (papel.modo == 2) {
		return "";
	}
	if (papel.modo == 1) {
		return papel.imagem[0] && g_file_test(papel.imagem, G_FILE_TEST_IS_REGULAR) ? papel.imagem : "";
	}
	struct jogo *d = destaque();
	return d ? d->fundo : "";
}

/* o desenho dos degradês (o Gravity não tem um elemento de degradê: pelo gv_livre) */
static void draw_topo(cairo_t *cr, Clay_BoundingBox b, void *dados) {
	/* a barra de cima começa escura e, a partir do meio, fica transparente */
	cairo_pattern_t *p = cairo_pattern_create_linear(0, b.y, 0, b.y + b.height);
	cairo_pattern_add_color_stop_rgba(p, 0.0, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.94);
	cairo_pattern_add_color_stop_rgba(p, 0.5, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.0);
	cairo_pattern_add_color_stop_rgba(p, 1.0, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.0);
	cairo_rectangle(cr, b.x, b.y, b.width, b.height);
	cairo_set_source(cr, p);
	cairo_fill(cr);
	cairo_pattern_destroy(p);
}

static void draw_inicio(cairo_t *cr, Clay_BoundingBox b, void *dados) {
	/* o jogo selecionado aparece limpo à direita: a esquerda (onde fica o texto) e a parte de baixo (onde ficam os jogos) escurecem */
	cairo_pattern_t *h = cairo_pattern_create_linear(b.x, 0, b.x + b.width, 0);
	cairo_pattern_add_color_stop_rgba(h, 0.0, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.82);
	cairo_pattern_add_color_stop_rgba(h, 0.55, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.0);
	cairo_rectangle(cr, b.x, b.y, b.width, b.height);
	cairo_set_source(cr, h);
	cairo_fill(cr);
	cairo_pattern_destroy(h);
	cairo_pattern_t *v = cairo_pattern_create_linear(0, b.y, 0, b.y + b.height);
	cairo_pattern_add_color_stop_rgba(v, 0.0, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.0);
	cairo_pattern_add_color_stop_rgba(v, 0.62, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.0);
	cairo_pattern_add_color_stop_rgba(v, 1.0, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.9);
	cairo_rectangle(cr, b.x, b.y, b.width, b.height);
	cairo_set_source(cr, v);
	cairo_fill(cr);
	cairo_pattern_destroy(v);
}

static void draw_biblioteca(cairo_t *cr, Clay_BoundingBox b, void *dados) {
	/* o papel de parede fica por trás, escurecido e mais forte nos cantos, para a grade de capas se ler bem */
	cairo_set_source_rgba(cr, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.66);
	cairo_rectangle(cr, b.x, b.y, b.width, b.height);
	cairo_fill(cr);
	cairo_pattern_t *r = cairo_pattern_create_radial(b.x + b.width * 0.5, b.y + b.height * 0.45, b.height * 0.2, b.x + b.width * 0.5, b.y + b.height * 0.45, b.width * 0.7);
	cairo_pattern_add_color_stop_rgba(r, 0.0, 8 / 255.0, 6 / 255.0, 20 / 255.0, 0.0);
	cairo_pattern_add_color_stop_rgba(r, 1.0, 4 / 255.0, 3 / 255.0, 12 / 255.0, 0.55);
	cairo_rectangle(cr, b.x, b.y, b.width, b.height);
	cairo_set_source(cr, r);
	cairo_fill(cr);
	cairo_pattern_destroy(r);
}

/* ---------------- peças ---------------- */

static Clay_Color roxo(float a) {
	return (Clay_Color){ 139, 92, 246, 255 * a };
}

static void capa(Clay_ElementId id, const struct jogo *j, float larg, float alt, bool foco, bool fundo) {
	const char *img = fundo ? j->fundo : j->capa;
	Clay_ElementDeclaration d = { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(larg), CLAY_SIZING_FIXED(alt) }, .padding = CLAY_PADDING_ALL(U(10)),
		.childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } }, .backgroundColor = (Clay_Color){ 40, 30, 70, 255 }, .cornerRadius = CLAY_CORNER_RADIUS(U(8)),
		.border = { .color = foco ? roxo(1) : (Clay_Color){ 255, 255, 255, 30 }, .width = GV_BORDA(foco ? 3 : 1) } };
	if (img && *img) {
		CLAY(id, gv_imagem(id, img, GV_AJUSTE_COBRIR, d)) {}
	} else {
		CLAY(id, d) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				gv_icone("gamepad", U(40), gv_alfa(gv_t.texto, 0.35f));
			}
		}
	}
}

static void rotulo_card(const struct jogo *j, bool foco) {
	gv_texto(gv_str(j->nome), U(16), foco ? gv_t.texto : gv_t.texto_secundario, GV_PESO_MEDIO);
}

static bool botao_u(Clay_ElementId id, const char *icone, const char *texto, enum gv_estilo_botao estilo, bool desativado) {
	return gv_botao_ext(id, &(gv_botao_opcoes){ .icone = icone, .texto = texto, .estilo = estilo, .altura = U(52), .desativado = desativado });
}

/* ---------------- a barra de cima e a de baixo ---------------- */

static void barra_cima(void) {
	static const char *const abas[3] = { "Início", "Biblioteca", "Loja" };
	CLAY(CLAY_ID("topo"), gv_livre(draw_topo, NULL, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(104)) }, .padding = { U(56), U(56), 0, U(24) }, .childGap = U(28),
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } })) {
		gv_icone("lumna", U(36), gv_t.acento);
		gv_texto(CLAY_STRING("LUMNA"), U(26), gv_t.texto, GV_PESO_MEDIO);
		gv_texto(CLAY_STRING("Gaming"), U(22), gv_t.texto_secundario, GV_PESO_NORMAL);
		for (int i = 0; i < 3; i++) {
			bool ativa = (tela == T_INICIO && i == 0) || ((tela == T_BIBLIOTECA || tela == T_DETALHES) && i == 1) || ((tela == T_LOJA || tela == T_BUSCA || tela == T_LOJA_JOGO) && i == 2);
			Clay_ElementId id = CLAY_IDI("nav", (uint32_t)i);
			CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { U(14), U(14), U(8), U(8) }, .childGap = U(4), .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				bool ativou = gv_focavel(id, U(8));
				gv_texto(gv_str(abas[i]), U(20), ativa ? gv_t.texto : gv_t.texto_secundario, ativa ? GV_PESO_MEDIO : GV_PESO_NORMAL);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(3)) } }, .backgroundColor = ativa ? roxo(1) : (Clay_Color){ 0 } }) {}
				if (gv_tocado(id) || ativou) {
					confirmando = false;
					adicionando = false;
					if (i == 2) {
						l_abrir_loja();
					} else {
						tela = i == 0 ? T_INICIO : T_BIBLIOTECA;
					}
				}
			}
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		if (aviso[0] && gv_agora() < aviso_ate) {
			gv_texto(gv_str(aviso), U(18), gv_t.aviso, GV_PESO_NORMAL);
		}
		if (gv_botao_redondo(CLAY_ID("n-busca"), "search", U(44), tela == T_BUSCA, false)) {
			l_abrir_busca(0);
		}
		{
			int nd = l_downloads();
			CLAY(CLAY_ID("n-dl"), { .layout = { .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
				if (gv_botao_redondo(CLAY_ID("n-dl-b"), "download", U(44), nd > 0, false)) {
					l_abrir_loja();
					l_sub = LS_SUB_DEST;
				}
				if (nd > 0) {
					CLAY(CLAY_ID("n-dl-n"), { .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP }, .zIndex = 5 },
						.layout = { .padding = { U(6), U(6), U(1), U(1) } }, .backgroundColor = roxo(1), .cornerRadius = CLAY_CORNER_RADIUS(U(8)) }) {
						gv_texto(gv_fmt("%d", nd), U(13), (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_MEDIO);
					}
				}
			}
		}
		if (gv_botao_redondo(CLAY_ID("engrenagem"), "settings", U(44), tela == T_CONFIG, false)) {
			abrir_config();
		}
		time_t t = time(NULL);
		struct tm lt;
		localtime_r(&t, &lt);
		gv_texto(gv_fmt("%02d:%02d", lt.tm_hour, lt.tm_min), U(22), gv_t.texto, GV_PESO_NORMAL);
	}
}

static void barra_baixo(void) {
	CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(70)) }, .padding = { U(56), U(56), 0, 0 }, .childGap = U(34), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = (Clay_Color){ 10, 8, 24, 255 } }) {
		gv_texto(CLAY_STRING("Enter  Selecionar"), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(CLAY_STRING("Esc  Voltar"), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
		if (tela == T_DETALHES || tela == T_CONFIG) {
			gv_texto(CLAY_STRING("LB RB  Abas"), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		gv_texto(CLAY_STRING("Guia  Menu do Gaming"), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

/* ---------------- Início ---------------- */

/* o jogo do card selecionado em "Seus jogos": é ele que aparece no centro e dá o fundo da tela */
static struct jogo *selecionado_inicio(void) {
	int idx[JOGOS_MAX];
	int n = recentes(idx, 6);
	if (!n) {
		return NULL;
	}
	if (sel >= n) {
		sel = n - 1;
	}
	return &lista[idx[sel < 0 ? 0 : sel]];
}

static void t_inicio(void) {
	struct jogo *d = selecionado_inicio();
	CLAY(CLAY_ID("inicio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		if (!d) {
			CLAY(CLAY_ID("vazio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(16),
				.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("gamepad", U(90), gv_t.acento);
				gv_texto(CLAY_STRING("Nenhum jogo por aqui ainda"), U(34), gv_t.texto, GV_PESO_MEDIO);
				gv_texto(CLAY_STRING("Instale pela Steam, pelo Lutris ou pelo Heroic, ou adicione um jogo avulso na Biblioteca."), U(20), gv_t.texto_secundario, GV_PESO_NORMAL);
				if (botao_u(CLAY_ID("vazio-bib"), "plus", "Abrir a Biblioteca", GV_BOTAO_CHEIO, false)) {
					tela = T_BIBLIOTECA;
				}
			}
		} else {
			CLAY(CLAY_ID("hero"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(72), U(72), U(40), U(10) }, .childGap = U(14),
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(CLAY_STRING("E M   D E S T A Q U E   N A   S U A   B I B L I O T E C A"), U(15), gv_t.texto_secundario, GV_PESO_MEDIO);
				if (d != destaque()) {
					/* (o selecionado não é o último que se jogou: o rótulo vira o do card) */
				}
				CLAY(CLAY_ID("hero-t"), { .layout = { .sizing = { CLAY_SIZING_FIXED(U(900)) } } }) {
					gv_texto(gv_str(d->nome), U(78), gv_t.texto, GV_PESO_SEMINEGRITO);
				}
				gv_texto(gv_fmt("%s%s%s", jogos_origem_nome(d->origem), d->versao[0] ? " · v" : "", d->versao), U(22), gv_t.texto_secundario, GV_PESO_NORMAL);
				CLAY(CLAY_ID("hero-b"), { .layout = { .childGap = U(16), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					if (botao_u(CLAY_ID("continuar"), "play", d->ultima ? "Continuar" : "Jogar", GV_BOTAO_CHEIO, false)) {
						abrir(d);
					}
					if (botao_u(CLAY_ID("sobre"), NULL, "Sobre o jogo", GV_BOTAO_CONTORNO, false)) {
						abrir_detalhes(d);
					}
					if (gv_botao_redondo(CLAY_ID("mais"), "dots", U(52), false, false)) {
						abrir_detalhes(d);
					}
				}
				gv_texto(gv_fmt("Última sessão: %s · %.0f h jogadas", quando(d->ultima), d->horas), U(17), gv_t.texto_suave, GV_PESO_NORMAL);
			}
			int idx[JOGOS_MAX];
			int n = recentes(idx, 6);
			if (sel >= n) {
				sel = n - 1;
			}
			CLAY(CLAY_ID("seus"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), 0, U(18) }, .childGap = U(10) } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_texto(CLAY_STRING("Seus jogos"), U(30), gv_t.texto, GV_PESO_MEDIO);
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
					if (gv_botao_ext(CLAY_ID("ver-bib"), &(gv_botao_opcoes){ .icone = "arrow-right", .texto = "Ver biblioteca", .estilo = GV_BOTAO_SIMPLES, .altura = U(40) })) {
						tela = T_BIBLIOTECA;
					}
				}
				CLAY(CLAY_ID("fila"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(14) } }) {
					for (int i = 0; i < n; i++) {
						struct jogo *j = &lista[idx[i]];
						bool foco = i == sel;
						Clay_ElementId id = CLAY_IDI("ic", (uint32_t)i);
						CLAY(CLAY_IDI("ic-c", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = U(8) } }) {
							bool ativou = gv_focavel(id, U(8));
							capa(id, j, U(260), U(146), foco, true);
							rotulo_card(j, foco);
							if (gv_tocado(id) || ativou) {
								sel = i;
								abrir_detalhes(j);
							}
						}
					}
				}
			}
		}
	}
}

/* ---------------- Biblioteca ---------------- */

static void dialogo_adicionar(void) {
	CLAY(CLAY_ID("add-fundo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER }, .zIndex = 50, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE },
		.backgroundColor = (Clay_Color){ 0, 0, 0, 170 } }) {
		CLAY(CLAY_ID("add-caixa"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(720)) }, .padding = CLAY_PADDING_ALL(U(28)), .childGap = U(12) },
			.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(U(14)), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Adicionar um jogo"), U(28), gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Para um jogo de fora de qualquer loja: o executável (Linux, ou .exe com o Wine/Proton) e um nome."), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Nome"), U(16), gv_t.texto_secundario, GV_PESO_MEDIO);
			gv_campo(CLAY_ID("add-nome"), add_nome, sizeof(add_nome), "Meu jogo", GV_CAMPO_TEXTO);
			gv_texto(CLAY_STRING("Executável (o caminho completo)"), U(16), gv_t.texto_secundario, GV_PESO_MEDIO);
			gv_campo(CLAY_ID("add-exec"), add_exec, sizeof(add_exec), "/home/eu/Jogos/jogo.sh", GV_CAMPO_TEXTO);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12), .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (botao_u(CLAY_ID("add-cancelar"), NULL, "Cancelar", GV_BOTAO_ELEVADO, false) || tecla(XKB_KEY_Escape)) {
					adicionando = false;
				}
				if (botao_u(CLAY_ID("add-ok"), "plus", "Adicionar", GV_BOTAO_CHEIO, !add_nome[0] || !add_exec[0])) {
					char id[64], erro[200] = "";
					if (jogos_avulso_adicionar(add_nome, add_exec, "", id, sizeof(id), erro, sizeof(erro))) {
						adicionando = false;
						add_nome[0] = add_exec[0] = '\0';
						relistar();
						avisar("Jogo adicionado.");
					} else {
						avisar(erro);
					}
				}
			}
		}
	}
}

static void t_biblioteca(void) {
	int idx[JOGOS_MAX];
	int n = visiveis(idx);
	if (sel >= n) {
		sel = n ? n - 1 : 0;
	}
	CLAY(CLAY_ID("bib"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), U(26), U(10) }, .childGap = U(18) } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_texto(CLAY_STRING("Biblioteca"), U(38), gv_t.texto, GV_PESO_SEMINEGRITO);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(24)) } } }) {}
			for (int f = 0; f < F_N; f++) {
				if (gv_botao_ext(CLAY_IDI("filtro", (uint32_t)f), &(gv_botao_opcoes){ .texto = filtros_nome[f], .estilo = filtro == f ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO, .altura = U(42) })) {
					filtro = f;
					sel = 0;
				}
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			gv_texto(gv_fmt("%d jogo%s", n, n == 1 ? "" : "s"), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
			if (gv_botao_ext(CLAY_ID("add"), &(gv_botao_opcoes){ .icone = "plus", .texto = "Adicionar jogo", .estilo = GV_BOTAO_CONTORNO, .altura = U(42) })) {
				adicionando = true;
			}
		}
		Clay_ElementId rol = CLAY_ID("bib-rolagem");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(22) } })) {
			if (!n) {
				gv_texto(CLAY_STRING("Nenhum jogo neste filtro."), U(22), gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			int colunas = (int)(gv_app_largura(app) / U(250));
			colunas = colunas < 2 ? 2 : colunas > 8 ? 8 : colunas;
			for (int linha = 0; linha * colunas < n; linha++) {
				CLAY(CLAY_IDI("bl", (uint32_t)linha), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(18) } }) {
					for (int c = 0; c < colunas && linha * colunas + c < n; c++) {
						int i = linha * colunas + c;
						struct jogo *j = &lista[idx[i]];
						bool foco = i == sel;
						Clay_ElementId id = CLAY_IDI("card", (uint32_t)i);
						CLAY(CLAY_IDI("card-c", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = U(8) } }) {
							bool ativou = gv_focavel(id, U(8));
							capa(id, j, U(200), U(280), foco, false);
							CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(200)) }, .childGap = U(6) } }) {
								rotulo_card(j, foco);
							}
							gv_texto(gv_str(jogos_origem_nome(j->origem)), U(14), gv_t.texto_suave, GV_PESO_NORMAL);
							if (gv_tocado(id) || ativou) {
								sel = i;
								abrir_detalhes(j);
							}
						}
					}
				}
			}
		}
	}
	if (adicionando) {
		dialogo_adicionar();
	}
}

/* ---------------- Detalhes ---------------- */

static void ed_carregar(const struct jogo *j) {
	ed = *j;
	for (int i = 0; i < JOGO_ENV_MAX; i++) {
		g_strlcpy(env_k[i], i < j->nenv ? j->env[i].k : "", sizeof(env_k[0]));
		g_strlcpy(env_v[i], i < j->nenv ? j->env[i].v : "", sizeof(env_v[0]));
	}
	ed_viva = true;
	ed_suja = false;
}

static void ed_aplicar(void) {
	int n = 0;
	for (int i = 0; i < JOGO_ENV_MAX && i < ed.nenv; i++) {
		if (env_k[i][0]) {
			g_strlcpy(ed.env[n].k, env_k[i], sizeof(ed.env[0].k));
			g_strlcpy(ed.env[n].v, env_v[i], sizeof(ed.env[0].v));
			n++;
		}
	}
	ed.nenv = n;
	if (jogos_props_gravar(&ed)) {
		avisar("Propriedades salvas.");
		relistar();
		ed_viva = false;
	} else {
		avisar("Não consegui salvar as propriedades.");
	}
}

static void disco(const struct jogo *j, double *usado, double *total, double *livre) {
	struct statvfs v;
	*usado = *total = *livre = 0;
	if (j->pasta[0] && statvfs(j->pasta, &v) == 0) {
		*total = (double)v.f_blocks * (double)v.f_frsize / 1e9;
		*livre = (double)v.f_bavail * (double)v.f_frsize / 1e9;
		*usado = *total - *livre;
	}
}

static void linha_info(const char *k, const char *v) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12) } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(150)) } } }) {
			gv_texto(gv_str(k), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		gv_texto(gv_str(v), U(17), gv_t.texto, GV_PESO_NORMAL);
	}
}

static void dialogo_desinstalar(const struct jogo *j) {
	CLAY(CLAY_ID("des-fundo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER }, .zIndex = 50, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE },
		.backgroundColor = (Clay_Color){ 0, 0, 0, 180 } }) {
		CLAY(CLAY_ID("des-caixa"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(700)) }, .padding = CLAY_PADDING_ALL(U(30)), .childGap = U(14) },
			.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(U(14)), .border = { .color = gv_alfa(gv_t.perigo, 0.6f), .width = GV_BORDA(1) } }) {
			gv_texto(gv_fmt("Desinstalar %s?", j->nome), U(30), gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(jogos_desinstalar_texto(j)), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
			if (j->origem == ORIGEM_AVULSO) {
				gv_linha(CLAY_ID("apagar-arquivos"), "trash", "Apagar também os arquivos do jogo", j->pasta[0] ? j->pasta : NULL, &apagar_arquivos);
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12), .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (botao_u(CLAY_ID("des-cancelar"), NULL, "Cancelar", GV_BOTAO_ELEVADO, false) || tecla(XKB_KEY_Escape)) {
					confirmando = false;
				}
				if (botao_u(CLAY_ID("des-ok"), "trash", "Desinstalar", GV_BOTAO_PERIGO, false)) {
					bool saiu = false;
					char erro[200] = "";
					if (jogos_desinstalar(j, apagar_arquivos, &saiu, erro, sizeof(erro))) {
						avisar(saiu ? "Jogo removido da biblioteca." : gv_fmt("Aberto: continue a desinstalação em %s.", jogos_origem_nome(j->origem)).chars);
						confirmando = false;
						apagar_arquivos = false;
						relistar();
					} else {
						avisar(erro);
					}
				}
			}
		}
	}
}

static void aba_principal(const struct jogo *j) {
	CLAY(CLAY_ID("princ"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(16) } }) {
		gv_texto(CLAY_STRING("Sua jornada"), U(30), gv_t.texto, GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(16) } }) {
			const char *ic[3] = { "clock", "clipboard", "drive" };
			const char *v[3] = { gv_fmt("%.0f h", j->horas).chars, quando(j->ultima), tamanho_txt(j->tamanho) };
			const char *r[3] = { "Tempo de jogo", "Última sessão", "Em disco" };
			char b0[40], b1[40], b2[40];
			g_strlcpy(b0, v[0], sizeof(b0));
			g_strlcpy(b1, v[1], sizeof(b1));
			g_strlcpy(b2, v[2], sizeof(b2));
			const char *vs[3] = { b0, b1, b2 };
			for (int i = 0; i < 3; i++) {
				CLAY(CLAY_IDI("stat", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(U(16)), .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = (Clay_Color){ 255, 255, 255, 12 }, .cornerRadius = CLAY_CORNER_RADIUS(U(8)), .border = { .color = (Clay_Color){ 255, 255, 255, 30 }, .width = GV_BORDA(1) } }) {
					gv_icone(ic[i], U(34), gv_t.icone);
					CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM } }) {
						gv_texto(gv_str(vs[i]), U(24), gv_t.texto, GV_PESO_MEDIO);
						gv_texto(gv_str(r[i]), U(16), gv_t.texto_secundario, GV_PESO_NORMAL);
					}
				}
			}
		}
		gv_texto(CLAY_STRING("Sobre o jogo"), U(26), gv_t.texto, GV_PESO_MEDIO);
		gv_texto(gv_fmt("%s, instalado pela %s%s%s.", j->nome, jogos_origem_nome(j->origem), j->pasta[0] ? " em " : "", j->pasta), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void campo_prop(const char *rot, const char *id, char *txt, size_t tam, const char *dica, bool editavel) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(210)) } } }) {
			gv_texto(gv_str(rot), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			if (editavel) {
				if (gv_campo(CLAY_SID(gv_str(id)), txt, tam, dica, GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
					ed_suja = true;
				}
			} else {
				gv_texto(gv_str(txt[0] ? txt : "—"), U(17), gv_t.texto, GV_PESO_NORMAL);
			}
		}
	}
}

static void aba_propriedades(const struct jogo *j) {
	bool edit = jogos_props_editaveis(j);
	if (!ed_viva) {
		ed_carregar(j);
	}
	CLAY(CLAY_ID("props"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12) } }) {
		gv_texto(CLAY_STRING("Inicialização"), U(30), gv_t.texto, GV_PESO_MEDIO);
		if (!edit) {
			gv_texto(gv_fmt("A %s guarda as propriedades deste jogo. Edite pelo gerenciador dela.", jogos_origem_nome(j->origem)), U(17), gv_t.aviso, GV_PESO_NORMAL);
		}
		campo_prop("Executável", "p-exec", ed.exec, sizeof(ed.exec), "/caminho/do/jogo", edit);
		campo_prop("Opções de inicialização", "p-opcoes", ed.opcoes, sizeof(ed.opcoes), "gamemoderun %command%", edit);
		gv_texto(CLAY_STRING("Use %command% para representar o executável do jogo."), U(15), gv_t.texto_suave, GV_PESO_NORMAL);
		campo_prop("Diretório de trabalho", "p-dir", ed.dir, sizeof(ed.dir), "/caminho/da/pasta", edit);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_texto(CLAY_STRING("Variáveis de ambiente"), U(20), gv_t.texto, GV_PESO_MEDIO);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (edit && ed.nenv < JOGO_ENV_MAX && gv_botao_ext(CLAY_ID("p-add-env"), &(gv_botao_opcoes){ .icone = "plus", .texto = "Adicionar", .estilo = GV_BOTAO_SIMPLES, .altura = U(36) })) {
				ed.nenv++;
				ed_suja = true;
			}
		}
		for (int i = 0; i < ed.nenv && i < JOGO_ENV_MAX; i++) {
			CLAY(CLAY_IDI("env", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(10), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY(CLAY_IDI("env-kc", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(U(260)) } } }) {
					if (gv_campo(CLAY_IDI("env-k", (uint32_t)i), env_k[i], sizeof(env_k[0]), "NOME", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
						ed_suja = true;
					}
				}
				CLAY(CLAY_IDI("env-vc", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					if (gv_campo(CLAY_IDI("env-v", (uint32_t)i), env_v[i], sizeof(env_v[0]), "valor", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
						ed_suja = true;
					}
				}
				if (edit && gv_botao_redondo(CLAY_IDI("env-x", (uint32_t)i), "close", U(36), false, false)) {
					for (int k = i; k < ed.nenv - 1; k++) {
						memcpy(env_k[k], env_k[k + 1], sizeof(env_k[0]));
						memcpy(env_v[k], env_v[k + 1], sizeof(env_v[0]));
					}
					env_k[ed.nenv - 1][0] = env_v[ed.nenv - 1][0] = '\0';
					ed.nenv--;
					ed_suja = true;
				}
			}
		}
	}
}

static void painel_lado_principal(const struct jogo *j) {
	double usado, total, livre;
	disco(j, &usado, &total, &livre);
	CLAY(CLAY_ID("lado"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(U(22)), .childGap = U(10) },
		.backgroundColor = (Clay_Color){ 255, 255, 255, 12 }, .cornerRadius = CLAY_CORNER_RADIUS(U(10)), .border = { .color = (Clay_Color){ 255, 255, 255, 30 }, .width = GV_BORDA(1) } }) {
		CLAY_AUTO_ID({ .layout = { .childGap = U(10), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(12)), CLAY_SIZING_FIXED(U(12)) } }, .backgroundColor = gv_t.sucesso, .cornerRadius = CLAY_CORNER_RADIUS(U(6)) }) {}
			gv_texto(CLAY_STRING("Instalado"), U(24), gv_t.texto, GV_PESO_MEDIO);
		}
		linha_info("Versão", j->versao[0] ? j->versao : "—");
		linha_info("Origem", jogos_origem_nome(j->origem));
		linha_info("Compatibilidade", j->origem == ORIGEM_STEAM ? "Proton · pela Steam" : j->compat ? "Wine/Proton" : "Nativo");
		if (total > 0) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, U(8), 0 } } }) {
				gv_texto(CLAY_STRING("Armazenamento"), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
				gv_texto(gv_str(tamanho_txt(j->tamanho)), U(17), gv_t.texto, GV_PESO_MEDIO);
			}
			CLAY(CLAY_ID("disco"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(9)) } }, .backgroundColor = (Clay_Color){ 255, 255, 255, 25 }, .cornerRadius = CLAY_CORNER_RADIUS(U(5)) }) {
				CLAY(CLAY_ID("disco-u"), { .layout = { .sizing = { CLAY_SIZING_PERCENT((float)(usado / total)), CLAY_SIZING_GROW(0) } }, .backgroundColor = roxo(1), .cornerRadius = CLAY_CORNER_RADIUS(U(5)) }) {}
			}
			gv_texto(gv_fmt("%.1f GB de %.0f GB usados · %.1f GB livres", usado, total, livre), U(14), gv_t.texto_suave, GV_PESO_NORMAL);
		}
		{
			/* o caminho comprido perde o começo, não o fim (o que identifica a pasta) */
			size_t lp = strlen(j->pasta);
			char curto[64];
			if (lp > 34) {
				g_snprintf(curto, sizeof(curto), "…%s", j->pasta + lp - 33);
			} else {
				g_strlcpy(curto, j->pasta[0] ? j->pasta : "—", sizeof(curto));
			}
			linha_info("Local", curto);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(10), .padding = { 0, 0, U(8), 0 } } }) {
			if (gv_botao_ext(CLAY_ID("abrir-pasta"), &(gv_botao_opcoes){ .icone = "folder", .texto = "Abrir pasta", .estilo = GV_BOTAO_CONTORNO, .crescer = true, .altura = U(46), .desativado = !j->pasta[0] })) {
				char *a[] = { "xdg-open", (char *)j->pasta, NULL };
				g_spawn_async(NULL, a, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
			}
			if (gv_botao_ext(CLAY_ID("gerenciar"), &(gv_botao_opcoes){ .icone = "settings", .texto = "Gerenciar", .estilo = GV_BOTAO_CONTORNO, .crescer = true, .altura = U(46) })) {
				aba = ABA_PROPRIEDADES;
			}
		}
		if (j->origem == ORIGEM_STEAM && gv_menu_item(CLAY_ID("verificar"), "file", "Verificar arquivos", NULL)) {
			char *cmd = g_strdup_printf("steam steam://validate/%s", j->ref);
			char *a[] = { "sh", "-c", cmd, NULL };
			g_spawn_async(NULL, a, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
			g_free(cmd);
			avisar("A Steam abriu para verificar os arquivos.");
		}
		if (gv_menu_item(CLAY_ID("config-ini"), "settings", "Configurar inicialização", NULL)) {
			aba = ABA_PROPRIEDADES;
		}
		if (gv_menu_item(CLAY_ID("desinstalar"), "trash", "Desinstalar", NULL)) {
			confirmando = true;
			apagar_arquivos = false;
		}
	}
}

static void painel_lado_props(const struct jogo *j) {
	bool edit = jogos_props_editaveis(j);
	CLAY(CLAY_ID("lado-p"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(U(22)), .childGap = U(14) },
		.backgroundColor = (Clay_Color){ 255, 255, 255, 12 }, .cornerRadius = CLAY_CORNER_RADIUS(U(10)), .border = { .color = (Clay_Color){ 255, 255, 255, 30 }, .width = GV_BORDA(1) } }) {
		gv_texto(CLAY_STRING("Compatibilidade"), U(26), gv_t.texto, GV_PESO_MEDIO);
		if (edit) {
			if (gv_linha(CLAY_ID("p-compat"), NULL, "Camada de compatibilidade", "Wine ou Proton (para .exe)", &ed.compat)) {
				ed_suja = true;
			}
			static const char *const janelas[2] = { "Tela cheia", "Janela" };
			int jn = !strcmp(ed.janela, "janela");
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(CLAY_STRING("Modo de janela"), U(18), gv_t.texto, GV_PESO_NORMAL);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					if (gv_lista_suspensa(CLAY_ID("p-janela"), janelas, 2, &jn)) {
						g_strlcpy(ed.janela, jn ? "janela" : "cheia", sizeof(ed.janela));
						ed_suja = true;
					}
				}
			}
			if (gv_linha(CLAY_ID("p-gpu"), NULL, "Preferir GPU dedicada", NULL, &ed.gpu)) {
				ed_suja = true;
			}
		} else {
			gv_texto(gv_fmt("Compatibilidade e janela são da %s.", jogos_origem_nome(j->origem)), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (gv_menu_item(CLAY_ID("p-gerenciador"), "external", gv_fmt("Abrir %s", jogos_origem_nome(j->origem)).chars, NULL)) {
			char *a[] = { "sh", "-c", j->origem == ORIGEM_STEAM ? "steam" : j->origem == ORIGEM_LUTRIS ? "lutris" : j->origem == ORIGEM_HEROIC ? "heroic" : "true", NULL };
			g_spawn_async(NULL, a, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
		}
		if (edit && gv_menu_item(CLAY_ID("p-restaurar-i"), "refresh", "Restaurar configurações padrão", NULL)) {
			jogos_props_restaurar(&lista[0] == j ? &lista[0] : por_id(j->id));
			ed_viva = false;
			relistar();
			avisar("Propriedades restauradas.");
		}
	}
}

static void t_detalhes(void) {
	struct jogo *j = por_id(det_id);
	if (!j) {
		tela = T_BIBLIOTECA;
		return;
	}
	CLAY(CLAY_ID("det"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("det-hero"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(250)) }, .padding = { U(64), U(64), U(10), U(12) }, .childGap = U(8),
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao_ext(CLAY_ID("det-voltar"), &(gv_botao_opcoes){ .icone = "arrow-left", .texto = "Biblioteca", .estilo = GV_BOTAO_SIMPLES, .altura = U(36) })) {
				tela = T_BIBLIOTECA;
			}
			gv_texto(gv_str(j->nome), U(64), gv_t.texto, GV_PESO_SEMINEGRITO);
			gv_texto(gv_fmt("%s%s%s", jogos_origem_nome(j->origem), j->versao[0] ? " · v" : "", j->versao), U(21), gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (botao_u(CLAY_ID("det-continuar"), "play", j->ultima ? "Continuar" : "Jogar", GV_BOTAO_CHEIO, false)) {
					abrir(j);
				}
				if (gv_botao_redondo(CLAY_ID("det-fav"), "heart", U(52), j->favorito, false)) {
					jogos_favoritar(j, !j->favorito);
					relistar();
				}
				if (gv_botao_redondo(CLAY_ID("det-mais"), "dots", U(52), false, false)) {
					aba = ABA_PROPRIEDADES;
				}
			}
		}
		CLAY(CLAY_ID("det-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = (Clay_Color){ 12, 9, 26, 235 } }) {
			static const char *const abas[2] = { "Principal", "Propriedades" };
			CLAY(CLAY_ID("det-abas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { U(64), U(64), U(8), 0 } } }) {
				if (gv_abas(CLAY_ID("det-aba"), abas, 2, &aba)) {
					ed_viva = ed_viva && aba == ABA_PROPRIEDADES;
				}
			}
			CLAY(CLAY_ID("det-dois"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(64), U(64), U(14), U(10) }, .childGap = U(34) } }) {
				Clay_ElementId rol = CLAY_ID("det-rol");
				CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
					if (aba == ABA_PRINCIPAL) {
						aba_principal(j);
					} else {
						aba_propriedades(j);
					}
				}
				Clay_ElementId rol2 = CLAY_ID("det-rol2");
				CLAY(rol2, gv_rolagem(rol2, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(520)), CLAY_SIZING_GROW(0) } } })) {
					if (aba == ABA_PRINCIPAL) {
						painel_lado_principal(j);
					} else {
						painel_lado_props(j);
					}
				}
			}
			if (aba == ABA_PROPRIEDADES && jogos_props_editaveis(j)) {
				CLAY(CLAY_ID("props-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { U(64), U(64), U(8), U(8) }, .childGap = U(12), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_texto(gv_str(ed_suja ? "Alterações não salvas" : ""), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
					if (botao_u(CLAY_ID("p-restaurar"), NULL, "Restaurar padrão", GV_BOTAO_ELEVADO, false)) {
						jogos_props_restaurar(j);
						ed_viva = false;
						relistar();
						avisar("Propriedades restauradas.");
					}
					if (botao_u(CLAY_ID("p-cancelar"), NULL, "Cancelar", GV_BOTAO_ELEVADO, !ed_suja)) {
						ed_viva = false;
					}
					if (botao_u(CLAY_ID("p-aplicar"), NULL, "Aplicar", GV_BOTAO_CHEIO, !ed_suja)) {
						ed_aplicar();
					}
				}
			}
		}
	}
	if (confirmando) {
		dialogo_desinstalar(j);
	}
}

#include "jogos_loja.h"

/* ---------------- Configurações do Gaming ---------------- */

static void pad_ler(void) {
	char *r = g_malloc(32 * 1024);
	servico_pad = false;
	nctl = 0;
	if (gamepadc_pedido("estado", r, 32 * 1024)) {
		struct json *j = json_ler(r);
		if (j && json_bool(json_chave(j, "ok"), false)) {
			servico_pad = true;
			const struct json *l = json_chave(j, "controles");
			for (int i = 0; l && i < l->nfilhos && nctl < GP_MAX; i++) {
				const struct json *c = json_item(l, i);
				struct ctl *o = &ctls[nctl++];
				g_strlcpy(o->id, json_chave_texto(c, "id"), sizeof(o->id));
				g_strlcpy(o->nome, json_chave_texto(c, "nome"), sizeof(o->nome));
				g_strlcpy(o->apelido, json_chave_texto(c, "apelido"), sizeof(o->apelido));
				o->bluetooth = json_bool(json_chave(c, "bluetooth"), false);
				o->vibra = json_bool(json_chave(c, "vibra"), false);
				o->ignorado = json_bool(json_chave(c, "ignorado"), false);
				o->bateria = (int)json_numero(json_chave(c, "bateria"), -1);
			}
		}
		json_liberar(j);
	}
	g_free(r);
	gp_cfg_ler(&gcfg);
	struct modos_cfg mc;
	modos_cfg_ler(&mc);
	dnd_gaming = mc.m[MODO_GAMING].nao_perturbe;
	pad_lido_em = gv_agora();
}

static void bt_mudou(gv_app *a, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	gv_app_redesenhar(a);
}

static void cfg_salvar(void) {
	gp_cfg_gravar(&gcfg);
	char r[256];
	gamepadc_pedido("recarregar", r, sizeof(r));
}

static void abrir_config(void) {
	tela = T_CONFIG;
	confirmando = false;
	adicionando = false;
	pad_ler();
	if (!bt_ligado) {
		int fd = bt_iniciar();
		if (fd >= 0) {
			gv_app_vigiar(app, fd, bt_mudou, NULL);
		}
		bt_ligado = true;
	}
}

static void ctl_nome(const struct ctl *c, char *saida, size_t tam) {
	g_strlcpy(saida, c->apelido[0] ? c->apelido : c->nome, tam);
}

static bool e_controle_bt(const struct bt_disp *d) {
	return g_str_has_prefix(d->icone, "input-gaming") || g_str_has_prefix(d->icone, "input-joystick");
}

static void cfg_controles(void) {
	CLAY(CLAY_ID("cfg-ctl"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12) } }) {
		gv_texto(CLAY_STRING("Controles conectados"), U(28), gv_t.texto, GV_PESO_MEDIO);
		if (!servico_pad) {
			gv_texto(CLAY_STRING("O serviço dos controles (lumna-gamepad) não está rodando."), U(18), gv_t.aviso, GV_PESO_NORMAL);
		} else if (!nctl) {
			gv_texto(CLAY_STRING("Nenhum controle conectado. Ligue um por USB ou pareie um por Bluetooth, logo abaixo."), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		for (int i = 0; i < nctl; i++) {
			struct ctl *c = &ctls[i];
			char nome[96];
			ctl_nome(c, nome, sizeof(nome));
			CLAY(CLAY_IDI("ctl", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(U(16)), .childGap = U(10) },
				.backgroundColor = (Clay_Color){ 255, 255, 255, 12 }, .cornerRadius = CLAY_CORNER_RADIUS(U(8)), .border = { .color = (Clay_Color){ 255, 255, 255, 30 }, .width = GV_BORDA(1) } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_icone("gamepad", U(36), c->ignorado ? gv_t.texto_suave : gv_t.acento);
					CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } } }) {
						gv_texto(gv_str(nome), U(22), c->ignorado ? gv_t.texto_secundario : gv_t.texto, GV_PESO_MEDIO);
						gv_texto(gv_fmt("%s%s%s%s", c->bluetooth ? "Bluetooth" : "USB", c->bateria >= 0 ? " · bateria " : "", c->bateria >= 0 ? gv_fmt("%d%%", c->bateria).chars : "",
							c->ignorado ? " · ignorado" : ""), U(16), gv_t.texto_secundario, GV_PESO_NORMAL);
					}
				}
				bool editando = !strcmp(renomeando, c->id);
				if (editando) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(10), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						CLAY(CLAY_IDI("ctl-ap", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
							int r = gv_campo(CLAY_IDI("ctl-apc", (uint32_t)i), apelido_ed, sizeof(apelido_ed), "Apelido (ex.: Controle da sala)", GV_CAMPO_TEXTO);
							if (r & GV_CAMPO_ENTER) {
								char p[200], resp[256];
								g_snprintf(p, sizeof(p), "apelido %s %s", c->id, apelido_ed);
								gamepadc_pedido(p, resp, sizeof(resp));
								renomeando[0] = '\0';
								pad_ler();
							}
						}
						if (gv_botao_ext(CLAY_IDI("ctl-ap-ok", (uint32_t)i), &(gv_botao_opcoes){ .texto = "Salvar", .estilo = GV_BOTAO_CHEIO, .altura = U(44) })) {
							char p[200], resp[256];
							g_snprintf(p, sizeof(p), "apelido %s %s", c->id, apelido_ed);
							gamepadc_pedido(p, resp, sizeof(resp));
							renomeando[0] = '\0';
							pad_ler();
						}
						if (gv_botao_ext(CLAY_IDI("ctl-ap-x", (uint32_t)i), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO, .altura = U(44) })) {
							renomeando[0] = '\0';
						}
					}
				} else {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(10) } }) {
						if (gv_botao_ext(CLAY_IDI("ctl-vib", (uint32_t)i), &(gv_botao_opcoes){ .icone = "waves", .texto = "Testar vibração", .estilo = GV_BOTAO_ELEVADO, .altura = U(44), .desativado = !c->vibra })) {
							char p[100], resp[256];
							g_snprintf(p, sizeof(p), "vibrar %s", c->id);
							g_strlcpy(teste_msg, gamepadc_pedido(p, resp, sizeof(resp)) && strstr(resp, "\"ok\":true") ? "Vibrou." : "Esse controle não vibra (ou a vibração está desligada em Ajustes).", sizeof(teste_msg));
						}
						if (gv_botao_ext(CLAY_IDI("ctl-ren", (uint32_t)i), &(gv_botao_opcoes){ .icone = "edit", .texto = "Renomear", .estilo = GV_BOTAO_ELEVADO, .altura = U(44) })) {
							g_strlcpy(renomeando, c->id, sizeof(renomeando));
							g_strlcpy(apelido_ed, c->apelido, sizeof(apelido_ed));
						}
						if (gv_botao_ext(CLAY_IDI("ctl-ign", (uint32_t)i), &(gv_botao_opcoes){ .icone = c->ignorado ? "play" : "close", .texto = c->ignorado ? "Voltar a usar" : "Ignorar", .estilo = GV_BOTAO_ELEVADO, .altura = U(44) })) {
							char p[200], resp[256];
							g_snprintf(p, sizeof(p), "ignorar %s %s", c->id, c->ignorado ? "nao" : "sim");
							gamepadc_pedido(p, resp, sizeof(resp));
							pad_ler();
						}
					}
				}
			}
		}
		if (teste_msg[0]) {
			gv_texto(gv_str(teste_msg), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		/* adicionar e remover: o Bluetooth */
		struct bt_estado bt;
		bt_estado_copiar(&bt);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, U(14), 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_texto(CLAY_STRING("Adicionar um controle (Bluetooth)"), U(28), gv_t.texto, GV_PESO_MEDIO);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			bool procurando = false;
			for (int a = 0; a < bt.nadapt; a++) {
				procurando |= bt.adapt[a].procurando;
			}
			if (bt.bluez && bt.nadapt && gv_botao_ext(CLAY_ID("bt-procurar"), &(gv_botao_opcoes){ .icone = "search", .texto = procurando ? "Parar de procurar" : "Procurar controles", .estilo = GV_BOTAO_CHEIO, .altura = U(44) })) {
				for (int a = 0; a < bt.nadapt; a++) {
					bt_procurar(bt.adapt[a].caminho, !procurando);
				}
			}
		}
		if (!bt.bluez) {
			gv_texto(CLAY_STRING("O Bluetooth (BlueZ) não está disponível neste computador."), U(18), gv_t.aviso, GV_PESO_NORMAL);
		} else if (!bt.nadapt) {
			gv_texto(CLAY_STRING("Nenhum adaptador Bluetooth."), U(18), gv_t.texto_secundario, GV_PESO_NORMAL);
		} else {
			gv_texto(CLAY_STRING("Coloque o controle para parear (no Xbox: o botão de sincronizar; no PlayStation: Share + Guia)."), U(16), gv_t.texto_suave, GV_PESO_NORMAL);
		}
		int mostrados = 0;
		for (int i = 0; i < bt.ndisp; i++) {
			const struct bt_disp *d = &bt.disp[i];
			if (!e_controle_bt(d)) {
				continue;
			}
			mostrados++;
			CLAY(CLAY_IDI("bt", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(U(14)), .childGap = U(12), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = (Clay_Color){ 255, 255, 255, 8 }, .cornerRadius = CLAY_CORNER_RADIUS(U(8)), .border = { .color = (Clay_Color){ 255, 255, 255, 25 }, .width = GV_BORDA(1) } }) {
				gv_icone("bluetooth", U(26), d->conectado ? gv_t.acento : gv_t.icone);
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(d->nome), U(20), gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(d->conectado ? "Conectado" : d->pareado ? "Pareado" : "Disponível"), U(15), gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				if (!d->pareado) {
					if (gv_botao_ext(CLAY_IDI("bt-parear", (uint32_t)i), &(gv_botao_opcoes){ .icone = "plus", .texto = "Parear", .estilo = GV_BOTAO_CHEIO, .altura = U(42) })) {
						bt_parear(d->caminho);
					}
				} else {
					if (!d->conectado && gv_botao_ext(CLAY_IDI("bt-conectar", (uint32_t)i), &(gv_botao_opcoes){ .texto = "Conectar", .estilo = GV_BOTAO_ELEVADO, .altura = U(42) })) {
						bt_conectar(d->caminho);
					}
					bool conf = !strcmp(remover_bt, d->caminho);
					if (gv_botao_ext(CLAY_IDI("bt-remover", (uint32_t)i), &(gv_botao_opcoes){ .icone = "trash", .texto = conf ? "Remover mesmo?" : "Remover", .estilo = conf ? GV_BOTAO_PERIGO : GV_BOTAO_ELEVADO, .altura = U(42) })) {
						if (conf) {
							bt_esquecer(d->caminho);
							remover_bt[0] = '\0';
						} else {
							g_strlcpy(remover_bt, d->caminho, sizeof(remover_bt));
						}
					}
				}
			}
		}
		if (bt.bluez && bt.nadapt && !mostrados) {
			gv_texto(CLAY_STRING("Nenhum controle Bluetooth à vista. Toque em \"Procurar controles\"."), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (bt.erro[0]) {
			gv_texto(gv_str(bt.erro), U(16), gv_t.perigo, GV_PESO_NORMAL);
		}
	}
}

static void cfg_ajustes(void) {
	CLAY(CLAY_ID("cfg-aj"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(14) } }) {
		gv_texto(CLAY_STRING("Ajustes do controle"), U(28), gv_t.texto, GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(6) } }) {
			gv_texto(gv_fmt("Zona morta dos analógicos: %d%%", gcfg.zona_morta), U(20), gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Quanto o analógico precisa andar para contar na navegação. Controles gastos pedem mais. (Para navegar, nunca menos que 25%.)"), U(16), gv_t.texto_secundario, GV_PESO_NORMAL);
			float v = gcfg.zona_morta / 50.0f;
			CLAY(CLAY_ID("aj-zona-c"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(40)) } } }) {
				if (gv_deslizante(CLAY_ID("aj-zona"), &v, U(8), U(24))) {
					gcfg.zona_morta = (int)(v * 50.0f + 0.5f);
					cfg_salvar();
				}
			}
		}
		if (gv_linha(CLAY_ID("aj-vib"), "waves", "Vibração", "Os tranquinhos de teste e do menu. Os jogos vibram pelo controle por conta própria.", &gcfg.vibracao)) {
			cfg_salvar();
		}
		if (gv_linha(CLAY_ID("aj-ab"), "gamepad", "Trocar A e B (estilo Nintendo)", "A volta (Esc) e B confirma (Enter), só na navegação do Gaming.", &gcfg.trocar_ab)) {
			cfg_salvar();
		}
		gv_texto(CLAY_STRING("Mapa dos botões na navegação"), U(22), gv_t.texto, GV_PESO_MEDIO);
		static const char *const mapa[][2] = { { "Direcional / analógico esquerdo", "Setas" }, { "A", "Enter (confirma)" }, { "B", "Esc (volta)" }, { "LB e RB", "Troca de aba (Page Up e Page Down)" },
			{ "Guia", "Abre o Gaming (veja a página Botão Guia)" } };
		for (int i = 0; i < 5; i++) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(14) } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(300)) } } }) {
					gv_texto(gv_str(mapa[i][0]), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				gv_texto(gv_str(mapa[i][1]), U(17), gv_t.texto, GV_PESO_NORMAL);
			}
		}
	}
}

static void cfg_guia(void) {
	static const char *const ids[4] = { "biblioteca", "inicio", "emma", "nada" };
	static const char *const nomes[4] = { "Abrir a biblioteca de jogos", "Ir ao Início do Gaming", "Chamar a Emma", "Não fazer nada" };
	static const char *const subs[4] = { "Traz o LUMNA Jogos para a frente (abre se estiver fechado).", "Traz o LUMNA Jogos e volta ao Início.", "Abre ou fecha o chat da Emma por cima do jogo.", "O botão Guia fica só para o jogo." };
	CLAY(CLAY_ID("cfg-gu"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12) } }) {
		gv_texto(CLAY_STRING("Botão Guia"), U(28), gv_t.texto, GV_PESO_MEDIO);
		gv_texto(CLAY_STRING("O que o botão do meio do controle faz, em qualquer janela (inclusive por cima de um jogo)."), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
		for (int i = 0; i < 4; i++) {
			bool sel_ = !strcmp(gcfg.guia, ids[i]);
			Clay_ElementId id = CLAY_IDI("guia", (uint32_t)i);
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(U(16)), .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = sel_ ? roxo(0.16f) : (Clay_Color){ 255, 255, 255, 10 }, .cornerRadius = CLAY_CORNER_RADIUS(U(8)), .border = { .color = sel_ ? roxo(1) : (Clay_Color){ 255, 255, 255, 30 }, .width = GV_BORDA(sel_ ? 2 : 1) } }) {
				bool ativou = gv_focavel(id, U(8));
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(nomes[i]), U(21), gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(subs[i]), U(15), gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				if (sel_) {
					gv_icone("check", U(26), gv_t.acento);
				}
				if (gv_tocado(id) || ativou) {
					g_strlcpy(gcfg.guia, ids[i], sizeof(gcfg.guia));
					cfg_salvar();
				}
			}
		}
	}
}

static void cfg_gaming(void) {
	static const char *const energias[4] = { "Desempenho", "Equilibrado", "Economia", "Não mexer" };
	static const char *const energia_id[4] = { "performance", "balanced", "power-saver", "nao" };
	int e = 0;
	for (int i = 0; i < 4; i++) {
		if (!strcmp(gcfg.energia, energia_id[i])) {
			e = i;
		}
	}
	char *mango = g_find_program_in_path("mangohud");
	CLAY(CLAY_ID("cfg-gm"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(14) } }) {
		gv_texto(CLAY_STRING("Gaming"), U(28), gv_t.texto, GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(6) } }) {
			gv_texto(CLAY_STRING("Perfil de energia no modo Gaming"), U(21), gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Ao entrar no Gaming o perfil muda; ao sair, volta ao que estava. Precisa do power-profiles-daemon."), U(16), gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY(CLAY_ID("gm-energia-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(U(360)) } } }) {
				if (gv_lista_suspensa(CLAY_ID("gm-energia"), energias, 4, &e)) {
					g_strlcpy(gcfg.energia, energia_id[e], sizeof(gcfg.energia));
					cfg_salvar();
				}
			}
		}
		if (gv_linha(CLAY_ID("gm-dnd"), "bell", "Não perturbar no Gaming", "Só os avisos urgentes passam enquanto a tela está em Gaming.", &dnd_gaming)) {
			struct modos_cfg mc;
			modos_cfg_ler(&mc);
			mc.m[MODO_GAMING].nao_perturbe = dnd_gaming;
			modos_cfg_gravar(&mc);
		}
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(6) } }) {
			static const char *const papeis[3] = { "Automático (a arte do jogo em destaque)", "Uma imagem minha", "Nenhum (só o fundo escuro)" };
			gv_texto(CLAY_STRING("Papel de parede da Biblioteca"), U(21), gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("O que aparece por trás da grade de jogos, escurecido para as capas se lerem bem."), U(16), gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY(CLAY_ID("gm-papel-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(U(480)) } } }) {
				if (gv_lista_suspensa(CLAY_ID("gm-papel"), papeis, 3, &papel.modo)) {
					papel_gravar();
				}
			}
			if (papel.modo == 1) {
				if (gv_campo(CLAY_ID("gm-papel-img"), papel.imagem, sizeof(papel.imagem), "/home/eu/Imagens/papel.jpg", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
					papel_gravar();
				}
				if (papel.imagem[0] && !g_file_test(papel.imagem, G_FILE_TEST_IS_REGULAR)) {
					gv_texto(CLAY_STRING("Esse arquivo não existe."), U(16), gv_t.perigo, GV_PESO_NORMAL);
				}
			}
		}
		if (gv_linha(CLAY_ID("gm-fps"), "stack", "Mostrar o FPS (MangoHud)", mango ? "Nos jogos avulsos que o Gaming abre. Na Steam, use mangohud %command% nas opções do jogo." : "O MangoHud não está instalado (pacman -S mangohud).", &gcfg.fps)) {
			cfg_salvar();
		}
	}
	g_free(mango);
}

/* quantos downloads da Steam estão em curso (os que esta sessão pediu) */
static int l_downloads(void) {
	static double lido_em;
	static int n;
	if (gv_agora() - lido_em < 1.5) {
		return n;
	}
	lido_em = gv_agora();
	int vivos = 0;
	for (int i = 0; i < l_nbaixando; i++) {
		struct steam_prog p;
		struct lg_prog gp;
		if (l_baixando[i] < 0) {
			if (lg_progresso(-l_baixando[i], &gp) && gp.baixando) {
				vivos++;
			}
		} else if (jogos_steam_progresso(l_baixando[i], &p) && p.baixando) {
			vivos++;
		}
	}
	for (int i = 0; i < l_nbaixando_epic; i++) {
		struct le_prog ep;
		if (le_progresso(l_baixando_epic[i], &ep) && ep.baixando) {
			vivos++;
		}
	}
	n = vivos;
	return n;
}

static void t_config(void) {
	static const char *const paginas[CP_N] = { "Controles", "Ajustes do controle", "Botão Guia", "Gaming", "Steam", "GOG", "Epic" };
	static const char *const icones[CP_N] = { "gamepad", "sliders", "lumna", "settings", "bag", "bag", "bag" };
	if (gv_agora() - pad_lido_em > 1.5) {
		pad_ler();
	}
	gv_acordar_em(1.5);
	CLAY(CLAY_ID("cfg"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(56), U(56), U(26), U(10) }, .childGap = U(34) } }) {
		CLAY(CLAY_ID("cfg-lado"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(360)) }, .childGap = U(10) } }) {
			gv_texto(CLAY_STRING("Configurações"), U(38), gv_t.texto, GV_PESO_SEMINEGRITO);
			for (int i = 0; i < CP_N; i++) {
				Clay_ElementId id = CLAY_IDI("cfg-pg", (uint32_t)i);
				bool ativa = cfg_pg == i;
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(U(14)), .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = ativa ? roxo(0.18f) : (Clay_Color){ 255, 255, 255, 8 }, .cornerRadius = CLAY_CORNER_RADIUS(U(8)), .border = { .color = ativa ? roxo(1) : (Clay_Color){ 255, 255, 255, 25 }, .width = GV_BORDA(ativa ? 2 : 1) } }) {
					bool ativou = gv_focavel(id, U(8));
					gv_icone(icones[i], U(26), ativa ? gv_t.acento : gv_t.icone);
					gv_texto(gv_str(paginas[i]), U(21), ativa ? gv_t.texto : gv_t.texto_secundario, ativa ? GV_PESO_MEDIO : GV_PESO_NORMAL);
					if (gv_tocado(id) || ativou) {
						cfg_pg = i;
					}
				}
			}
		}
		Clay_ElementId rol = CLAY_ID("cfg-rol");
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } })) {
			switch (cfg_pg) {
			case CP_CONTROLES: cfg_controles(); break;
			case CP_AJUSTES: cfg_ajustes(); break;
			case CP_GUIA: cfg_guia(); break;
			case CP_STEAM: cfg_steam(); break;
			case CP_GOG: cfg_gog(); break;
			case CP_EPIC: cfg_epic(); break;
			default: cfg_gaming(); break;
			}
		}
	}
}

/* ---------------- a janela ---------------- */

static void depurar(void);

static int l_gog_contar(void) {
	int n = 0;
	for (int i = 0; i < l_ndest; i++) {
		n += l_dest[i].loja == LS_LOJA_GOG;
	}
	return n;
}

static void navegar_teclado(void) {
	/* Home: volta ao Início, de qualquer tela (o Guia pede isso ao controle) */
	if (tecla(XKB_KEY_Home) && !adicionando && !confirmando) {
		tela = T_INICIO;
		return;
	}
	bool pagina_antes = tecla(XKB_KEY_Prior), pagina_depois = tecla(XKB_KEY_Next); /* LB e RB */
	if (adicionando || confirmando) {
		bool seta_volta = tecla(XKB_KEY_Up) || tecla(XKB_KEY_Left), seta_vai = tecla(XKB_KEY_Down) || tecla(XKB_KEY_Right);
		if (seta_volta) {
			gv_foco_pedir(-1);
		}
		if (seta_vai) {
			gv_foco_pedir(1);
		}
		return;
	}
	if (tela == T_LOJA || tela == T_BUSCA || tela == T_LOJA_JOGO) {
		if (tecla(XKB_KEY_Escape)) {
			tela = tela == T_LOJA ? T_INICIO : T_LOJA;
			return;
		}
		if (tecla(XKB_KEY_Up)) {
			gv_foco_pedir(-1);
		}
		if (tecla(XKB_KEY_Down)) {
			gv_foco_pedir(1);
		}
		if (tela == T_LOJA && l_sub == LS_SUB_DEST) {
			if (tecla(XKB_KEY_Right) && l_sel < (l_ndest < 5 ? l_ndest : 5) - 1) {
				l_sel++;
			}
			if (tecla(XKB_KEY_Left) && l_sel > 0) {
				l_sel--;
			}
			if ((tecla(XKB_KEY_Return) || tecla(XKB_KEY_KP_Enter)) && l_ndest && l_sel < l_ndest && !l_busca_txt[0]) { /* com algo digitado na busca, o Enter é dela */
				l_abrir_jogo(l_dest[l_sel].appid, l_dest[l_sel].loja);
			}
		} else {
			if (tecla(XKB_KEY_Left)) {
				gv_foco_pedir(-1);
			}
			if (tecla(XKB_KEY_Right)) {
				gv_foco_pedir(1);
			}
		}
		int passo = pagina_depois ? 1 : pagina_antes ? -1 : 0;
		if (passo && tela == T_LOJA) {
			l_sub = (l_sub + passo + LS_SUB_N) % LS_SUB_N;
			l_sel = 0;
		} else if (passo && tela == T_LOJA_JOGO) {
			l_aba = (l_aba + passo + LJ_ABA_N) % LJ_ABA_N;
		}
		return;
	}
	if (tela == T_DETALHES || tela == T_CONFIG) {
		if (tecla(XKB_KEY_Escape)) {
			tela = tela == T_CONFIG ? T_INICIO : T_BIBLIOTECA;
			return;
		}
		if (tecla(XKB_KEY_Up) || tecla(XKB_KEY_Left)) {
			gv_foco_pedir(-1);
		}
		if (tecla(XKB_KEY_Down) || tecla(XKB_KEY_Right)) {
			gv_foco_pedir(1);
		}
		int passo = pagina_depois ? 1 : pagina_antes ? -1 : 0;
		if (passo && tela == T_DETALHES) {
			aba = (aba + passo + 2) % 2;
			ed_viva = ed_viva && aba == ABA_PROPRIEDADES;
		} else if (passo && tela == T_CONFIG) {
			cfg_pg = (cfg_pg + passo + CP_N) % CP_N;
		}
		return;
	}
	int idx[JOGOS_MAX];
	int n = tela == T_INICIO ? recentes(idx, 6) : visiveis(idx);
	int colunas = tela == T_INICIO ? n : (int)(gv_app_largura(app) / U(250));
	colunas = colunas < 1 ? 1 : colunas;
	if (tecla(XKB_KEY_Right) && sel < n - 1) {
		sel++;
	}
	if (tecla(XKB_KEY_Left) && sel > 0) {
		sel--;
	}
	if (tecla(XKB_KEY_Down) && tela == T_BIBLIOTECA && sel + colunas < n) {
		sel += colunas;
	}
	if (tecla(XKB_KEY_Up) && tela == T_BIBLIOTECA && sel - colunas >= 0) {
		sel -= colunas;
	}
	if ((tecla(XKB_KEY_Return) || tecla(XKB_KEY_KP_Enter)) && n > 0 && sel < n) {
		abrir_detalhes(&lista[idx[sel]]);
	}
	if (tecla(XKB_KEY_Escape) && tela == T_BIBLIOTECA) {
		tela = T_INICIO;
	}
	/* LB e RB andam pelas abas de cima: Início, Biblioteca, Configurações */
	if (pagina_depois) {
		if (tela == T_INICIO) {
			tela = T_BIBLIOTECA;
		} else {
			l_abrir_loja();
		}
	} else if (pagina_antes) {
		if (tela == T_BIBLIOTECA) {
			tela = T_INICIO;
		}
	}
}

static void desenhar(gv_app *a, void *dados) {
	K = gv_app_largura(a) / 1920.0f;
	K = K < 0.5f ? 0.5f : K > 1.8f ? 1.8f : K;
	if (gv_agora() >= relista_em && !adicionando && !confirmando) {
		relistar();
	}
	gv_acordar_em(5.0);
	navegar_teclado();
	struct jogo *d = tela == T_DETALHES ? por_id(det_id) : tela == T_INICIO ? selecionado_inicio() : NULL;
	const char *fundo = tela == T_BIBLIOTECA ? papel_arquivo() : tela == T_CONFIG ? "" : d ? d->fundo : "";
	if (tela == T_LOJA && l_sub == LS_SUB_DEST && l_ndest) {
		fundo = l_img(l_dest[l_sel < l_ndest ? l_sel : 0].fundo) ? l_img(l_dest[l_sel < l_ndest ? l_sel : 0].fundo) : "";
	} else if (tela == T_LOJA_JOGO && l_det_ok) {
		fundo = l_img(l_det.fundo) ? l_img(l_det.fundo) : "";
	}
	Clay_ElementDeclaration base = { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = (Clay_Color){ 12, 9, 26, 255 } };
	Clay_ElementDeclaration cena = { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } };
	void (*degrade)(cairo_t *, Clay_BoundingBox, void *) = tela == T_INICIO || (tela == T_LOJA && l_sub == LS_SUB_DEST) ? draw_inicio : (tela == T_BIBLIOTECA || tela == T_BUSCA) && fundo[0] ? draw_biblioteca : NULL;
	Clay_ElementDeclaration janela = fundo[0] ? gv_imagem(CLAY_ID("janela"), fundo, GV_AJUSTE_COBRIR, base) : base;
	CLAY(CLAY_ID("janela"), janela) {
		CLAY(CLAY_ID("cena"), degrade ? gv_livre(degrade, NULL, cena) : cena) {
			barra_cima();
			if (tela == T_INICIO) {
				t_inicio();
			} else if (tela == T_BIBLIOTECA) {
				t_biblioteca();
			} else if (tela == T_CONFIG) {
				t_config();
			} else if (tela == T_LOJA) {
				t_loja();
			} else if (tela == T_BUSCA) {
				t_busca();
			} else if (tela == T_LOJA_JOGO) {
				t_loja_jogo();
			} else {
				t_detalhes();
			}
			barra_baixo();
		}
	}
	depurar();
}

/* ---------------- depuração (LUMNA_JOGOS_DEBUG) ---------------- */

static void depurar(void) {
	static int ligado = -1;
	static GHashTable *antes;
	static char *estado_antes;
	if (ligado < 0) {
		ligado = getenv("LUMNA_JOGOS_DEBUG") != NULL;
	}
	if (!ligado) {
		return;
	}
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	GArray *ids = g_array_new(FALSE, FALSE, sizeof(uint32_t));
#define F(x) do { g_ptr_array_add(nomes, g_strdup(x)); Clay_ElementId e_ = CLAY_ID(x); g_array_append_val(ids, e_.id); } while (0)
#define S(base, n) for (int i_ = 0; i_ < (n); i_++) { g_ptr_array_add(nomes, g_strdup_printf("%s/%d", base, i_)); Clay_ElementId e_ = CLAY_IDI(base, (uint32_t)i_); g_array_append_val(ids, e_.id); }
	F("continuar"); F("sobre"); F("mais"); F("ver-bib"); F("det-voltar"); F("det-continuar"); F("det-fav"); F("det-mais"); F("add"); F("add-nome"); F("add-exec"); F("add-ok"); F("add-cancelar");
	F("abrir-pasta"); F("gerenciar"); F("verificar"); F("config-ini"); F("desinstalar"); F("des-ok"); F("des-cancelar"); F("apagar-arquivos");
	F("engrenagem"); F("ep-abrir"); F("ep-codigo"); F("ep-entrar"); F("ep-sair"); F("l-epic-recarregar"); F("gg-abrir"); F("gg-codigo"); F("gg-entrar"); F("gg-sair"); F("li-pasta"); F("l-loja"); F("l-ver"); F("l-preco"); F("l-busca"); F("b-campo"); F("b-gratis"); F("b-linux"); F("b-um"); F("b-coop"); F("lj-comprar"); F("lj-desejo"); F("lj-instalar"); F("li-ok"); F("li-nao"); F("li-local"); F("lj-desinstalar"); F("lj-jogar"); F("lj-atualizar"); F("lj-voltar"); F("n-busca"); F("n-dl-b"); F("st-chave"); F("st-id"); F("st-salvar"); F("lj-trailer"); F("l-tentar"); F("bt-procurar"); F("aj-zona"); F("aj-vib"); F("aj-ab"); F("gm-energia"); F("gm-dnd"); F("gm-fps"); F("gm-papel"); F("gm-papel-img"); F("p-add-env"); F("p-restaurar"); F("p-cancelar"); F("p-aplicar"); F("p-compat"); F("p-gpu"); F("p-gerenciador");
	S("nav", 3); S("l-chip", LS_GEN_N); S("b-gen", LS_GEN_N); S("cfg-pg", CP_N); S("ctl-vib", GP_MAX); S("ctl-ren", GP_MAX); S("ctl-ign", GP_MAX); S("ctl-apc", GP_MAX); S("ctl-ap-ok", GP_MAX); S("guia", 4); S("bt-parear", 8); S("bt-remover", 8); S("ic", 6); S("card", 40); S("filtro", F_N); S("env-k", JOGO_ENV_MAX); S("env-v", JOGO_ENV_MAX); S("env-x", JOGO_ENV_MAX);
	{ Clay_ElementId b_ = CLAY_ID("l-subabas"); for (int i_ = 0; i_ < LS_SUB_N; i_++) { g_ptr_array_add(nomes, g_strdup_printf("l-subabas/%d", i_)); Clay_ElementId e_ = Clay__HashStringWithOffset(b_.stringId, b_.offset * 64 + 3000017u + (uint32_t)i_, b_.baseId); g_array_append_val(ids, e_.id); } }
	{ Clay_ElementId b_ = CLAY_ID("det-aba"); for (int i_ = 0; i_ < 2; i_++) { g_ptr_array_add(nomes, g_strdup_printf("det-aba/%d", i_)); Clay_ElementId e_ = Clay__HashStringWithOffset(b_.stringId, b_.offset * 64 + 3000017u + (uint32_t)i_, b_.baseId); g_array_append_val(ids, e_.id); } }
	g_ptr_array_add(nomes, g_strdup("p-exec")); { Clay_ElementId e_ = CLAY_SID(CLAY_STRING("p-exec")); g_array_append_val(ids, e_.id); }
	g_ptr_array_add(nomes, g_strdup("p-opcoes")); { Clay_ElementId e_ = CLAY_SID(CLAY_STRING("p-opcoes")); g_array_append_val(ids, e_.id); }
	g_ptr_array_add(nomes, g_strdup("p-dir")); { Clay_ElementId e_ = CLAY_SID(CLAY_STRING("p-dir")); g_array_append_val(ids, e_.id); }
#undef F
#undef S
	bool mudou = false;
	for (guint i = 0; i < nomes->len; i++) {
		Clay_BoundingBox b = gv_caixa((Clay_ElementId){ .id = g_array_index(ids, uint32_t, i) });
		char *linha = g_strdup_printf("%.0f %.0f %.0f %.0f", b.width > 0 ? b.x : 0, b.width > 0 ? b.y : 0, b.width > 0 ? b.width : 0, b.width > 0 ? b.height : 0);
		char *ant = g_hash_table_lookup(antes, nomes->pdata[i]);
		if ((!ant && b.width > 0) || (ant && strcmp(ant, linha))) {
			printf("pos %s %s\n", (char *)nomes->pdata[i], linha);
			g_hash_table_insert(antes, g_strdup(nomes->pdata[i]), linha);
			mudou = true;
		} else {
			g_free(linha);
		}
	}
	int idx[JOGOS_MAX];
	struct jogo *d = por_id(det_id);
	char *est = g_strdup_printf("estado tela=%d aba=%d filtro=%d njogos=%d nvis=%d sel=%d det=%s favorito=%d confirmando=%d adicionando=%d suja=%d aviso=%s opcoes=%s nenv=%d pg=%d nctl=%d servico=%d zona=%d ab=%d vib=%d guia=%s energia=%s fps=%d dnd=%d papel=%d lsub=%d lndest=%d lnres=%d ltotal=%d lappid=%d ldet=%d ldes=%d ldlg=%d lprog=%d,%d lpossui=%d lbaix=%d lsel=%d lgog=%d lgogin=%d lgprog=%d,%d lgpossui=%d lepic=%d,%d lepicin=%d leprog=%d,%d lloja=%d",
		tela, aba, filtro, njogos, visiveis(idx), sel, det_id[0] ? det_id : "-", d ? d->favorito : 0, confirmando, adicionando, ed_suja,
		aviso[0] && gv_agora() < aviso_ate ? aviso : "-", d && d->opcoes[0] ? d->opcoes : "-", d ? d->nenv : 0,
		cfg_pg, nctl, servico_pad, gcfg.zona_morta, gcfg.trocar_ab, gcfg.vibracao, gcfg.guia, gcfg.energia, gcfg.fps, dnd_gaming, papel.modo,
		l_sub, l_ndest, l_nres, l_total, l_appid, l_det_ok, l_ndesejos, l_instalando_dialogo, l_prog.baixando, l_prog.pct, l_npossui, l_nbaixando, l_sel,
		l_gog_contar(), l_gog_logado, l_prog_gog.baixando, l_prog_gog.pct, l_npossui_gog,
		l_nepic_lib, l_nepic_promo, l_epic_logado, l_prog_epic.baixando, l_prog_epic.pct, l_loja);
	for (char *c = est; *c; c++) {
		if (*c == '\n') {
			*c = ' ';
		}
	}
	if (!estado_antes || strcmp(estado_antes, est) || mudou) {
		printf("%s\n", est);
		g_free(estado_antes);
		estado_antes = est;
		fflush(stdout);
	} else {
		g_free(est);
	}
	g_ptr_array_free(nomes, TRUE);
	g_array_free(ids, TRUE);
}

/* lumna-jogos vigia: sobe com a sessão; quando uma tela entra em Gaming e a biblioteca não está aberta, abre (como o serviço da Mídia faz com as telas dela) */
/* o perfil de energia do Gaming (gamepad.ini [gaming] energia): muda ao entrar e devolve ao sair */
static char energia_antes[24];
static bool energia_mudada;

static void energia_ajustar(bool gaming) {
	struct gp_cfg c;
	gp_cfg_ler(&c);
	if (gaming && !energia_mudada && strcmp(c.energia, "nao")) {
		char *out = NULL;
		char *get[] = { "powerprofilesctl", "get", NULL };
		if (g_spawn_sync(NULL, get, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, NULL, NULL) && out) {
			g_strlcpy(energia_antes, g_strstrip(out), sizeof(energia_antes));
			char *set[] = { "powerprofilesctl", "set", c.energia, NULL };
			g_spawn_sync(NULL, set, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, NULL, NULL);
			energia_mudada = energia_antes[0] != '\0';
		}
		g_free(out);
	} else if (!gaming && energia_mudada) {
		char *set[] = { "powerprofilesctl", "set", energia_antes, NULL };
		g_spawn_sync(NULL, set, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		energia_mudada = false;
	}
}

static int vigia(void) {
	int fd = modos_assinar();
	if (fd < 0) {
		fprintf(stderr, "lumna-jogos vigia: o Orbit não respondeu\n");
		return 1;
	}
	for (;;) {
		struct modos_estado e;
		bool gaming = false;
		if (modos_ler(&e) && e.orbit) {
			for (int i = 0; i < e.n; i++) {
				gaming |= e.saidas[i].modo == MODO_GAMING;
			}
		}
		energia_ajustar(gaming);
		if (gaming) {
			int estado = 0;
			char *pg[] = { "pgrep", "-x", "lumna-jogos", NULL };
			g_spawn_sync(NULL, pg, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL, &estado, NULL);
			if (estado != 0) {
				char *ab[] = { "lumna-jogos", NULL };
				g_spawn_async(NULL, ab, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
			}
		}
		char b[256];
		if (read(fd, b, sizeof(b)) <= 0) {
			sleep(1);
			fd = modos_assinar();
			if (fd < 0) {
				return 1;
			}
		}
		modos_assinar_ler(fd);
	}
}

int main(int argc, char **argv) {
	if (argc > 1 && !strcmp(argv[1], "vigia")) {
		return vigia();
	}
	if (argc > 4 && !strcmp(argv[1], "epic-instalar")) {
		/* o instalador da Epic, num processo à parte: epic-instalar APP_NAME TITULO PASTA */
		return le_instalar_agora(argv[2], argv[3], argv[4]);
	}
	if (argc > 5 && !strcmp(argv[1], "gog-instalar")) {
		/* o instalador da GOG, num processo à parte: gog-instalar ID TITULO PASTA linux|windows */
		return lg_instalar_agora(atoi(argv[2]), argv[3], argv[4], !strcmp(argv[5], "linux"));
	}
	if (getenv("LUMNA_JOGOS_DEBUG")) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	const char *ir_jogo = NULL;
	int aba_ini = 0, loja_jogo_ini = 0;
	const char *loja_epic_ini = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--tela") && i + 1 < argc) {
			{ const char *t_ = argv[++i]; tela = !strcmp(t_, "biblioteca") ? T_BIBLIOTECA : !strcmp(t_, "config") ? T_CONFIG : !strcmp(t_, "loja") ? T_LOJA : T_INICIO; }
		} else if (!strcmp(argv[i], "--jogo") && i + 1 < argc) {
			ir_jogo = argv[++i];
		} else if (!strcmp(argv[i], "--loja-jogo") && i + 1 < argc) {
			loja_jogo_ini = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--loja-epic") && i + 1 < argc) {
			loja_epic_ini = argv[++i];
		} else if (!strcmp(argv[i], "--aba") && i + 1 < argc) {
			aba_ini = atoi(argv[++i]);
		}
	}
	relistar();
	gp_cfg_ler(&gcfg);
	if (tela == T_LOJA) {
		l_abrir_loja();
	}
	if (loja_epic_ini) {
		/* --loja-epic APP[:lib]: abre a página de um jogo da Epic (com :lib, o da biblioteca) */
		struct ls_item it = { .loja = LS_LOJA_EPIC };
		g_strlcpy(it.gid, loja_epic_ini, sizeof(it.gid));
		char *dp = strchr(it.gid, ':');
		if (dp) {
			*dp = '\0';
			it.possui = true;
		}
		it.appid = (int)(g_str_hash(it.gid) & 0x7fffffff);
		l_abrir_loja();
		l_abrir_item(&it);
	}
	if (loja_jogo_ini) {
		l_abrir_loja();
		l_abrir_jogo(loja_jogo_ini < 0 ? -loja_jogo_ini : loja_jogo_ini, loja_jogo_ini < 0 ? LS_LOJA_GOG : LS_LOJA_STEAM);
	}
	if (ir_jogo && por_id(ir_jogo)) {
		abrir_detalhes(por_id(ir_jogo));
		aba = aba_ini ? ABA_PROPRIEDADES : ABA_PRINCIPAL;
	}
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Gaming", .app_id = "lumna-jogos", .largura = 1280, .altura = 720 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
