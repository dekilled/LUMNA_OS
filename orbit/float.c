#include "orbit.h"

#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/version.h>

/*
 * As janelas livres do desktop (arranjo = livre, o de fábrica): abrem no meio da área útil, com
 * o tamanho que pedem, e movem-se pela barra de título (xdg move), com Alt+arrastar, e mudam de
 * tamanho pelas bordas (xdg resize) ou com Alt+botão direito. O encaixe só vem de um gatilho:
 * largar a janela com o CURSOR perto da lateral da tela (a posição dele na altura escolhe o encaixe: no meio, a metade com a altura
 * inteira; perto do topo ou do fim, o canto, com um quarto da tela) ou em cima (maximizar), ou Super+setas. Uma janela encaixada ou
 * maximizada volta ao tamanho livre de antes ao arrastá-la. As janelas encaixadas se redimensionam juntas, pela divisa entre elas.
 * O telemóvel continua em mosaico.
 */

#define ORBIT_MIN_W 240
#define ORBIT_MIN_H 160
#define ORBIT_SNAP_ZONE 24 /* px, nas laterais de cada monitor: a zona de encaixe, em volta do CURSOR (a borda da janela encostando não conta). Só vale se SOLTAR com o cursor nela; passar por ela atravessa */
#define ORBIT_SNAP_CORNER 0.30 /* a fração da altura útil, a partir do topo e a partir do fim, onde a lateral dá um canto (um quarto da tela) em vez da metade */
#define ORBIT_SNAP_MIN_FRAC 0.15 /* a menor fração de uma divisa de encaixe */
#define ORBIT_SNAP_TOP 4   /* em cima (maximizar): fina, porque quem arrasta pela barra de título anda sempre perto do topo */

/* o tamanho da janela que o cliente diz (no 0.19 passou a um campo) */
static struct wlr_box client_geometry(struct orbit_view *view) {
	return orbit_view_client_box(view);
}

static struct wlr_box geometry(struct orbit_view *view) {
	struct wlr_box box = client_geometry(view);
	if (box.width < 1 || box.height < 1) {
		box.width = view->fw > 0 ? view->fw : 640;
		box.height = view->fh > 0 ? view->fh : 480;
	}
	return box;
}

/*
 * A janela muda de tela. A janela livre leva a posição junto: o mesmo lugar relativo na tela nova, e dentro da área útil dela
 * (sem isto ela continuava desenhada onde estava, na tela de antes, e atrás do preto de uma tela apagada). As janelas do mosaico,
 * maximizadas ou encaixadas são arrumadas pela tela nova. `carry` é falso quando a janela já está no lugar certo (largada na outra
 * tela com o mouse). Quem chama cuida do espaço (tag) e da tela cheia.
 */
void orbit_view_set_output(struct orbit_view *view, struct orbit_output *dest, bool carry) {
	struct orbit_server *server = view->server;
	struct orbit_output *old = view->output;
	view->output = dest;
	if (!dest || !old || old == dest || !view->floating || !carry) {
		return;
	}
	struct wlr_box ob = { 0 }, nb = { 0 };
	wlr_output_layout_get_box(server->layout, old->wlr, &ob);
	wlr_output_layout_get_box(server->layout, dest->wlr, &nb);
	if (nb.width <= 0 || nb.height <= 0) {
		return;
	}
	if (ob.width <= 0 || ob.height <= 0) {
		orbit_float_place_new(view); /* a tela de antes já não existe: abre no meio da nova */
		return;
	}
	int dx = nb.x - ob.x, dy = nb.y - ob.y;
	view->x += dx;
	view->y += dy;
	view->fx += dx;
	view->fy += dy;
	struct wlr_box usable = orbit_layer_usable(dest, &nb);
	struct wlr_box g = geometry(view);
	int w = g.width > 0 ? g.width : view->fw, h = g.height > 0 ? g.height : view->fh;
	int maxx = usable.x + usable.width - w, maxy = usable.y + usable.height - h;
	view->x = view->x > maxx ? maxx : view->x;
	view->y = view->y > maxy ? maxy : view->y;
	view->x = view->x < usable.x ? usable.x : view->x;
	view->y = view->y < usable.y ? usable.y : view->y;
	view->fx = view->x;
	view->fy = view->y;
	view->placed = true;
	view->sliding = false;
	view->slide_x = view->x;
	view->slide_y = view->y;
	view->slide_to_x = view->x; /* o set_size abaixo conta ao X11 daqui */
	view->slide_to_y = view->y;
}

bool orbit_free_desktop(struct orbit_server *server, struct orbit_output *output) {
	return server->arrangement == ORBIT_LAYOUT_FREE && output && !output_is_phone(output->wlr);
}

/* guarda onde e de que tamanho a janela estava livre (antes de encaixar ou maximizar) */
void orbit_float_save(struct orbit_view *view) {
	if (!view->floating || view->snap || view->maximized || view->fullscreen) {
		return;
	}
	struct wlr_box g = geometry(view);
	view->fx = view->x;
	view->fy = view->y;
	view->fw = g.width;
	view->fh = g.height;
}

/* ao abrir: no meio da área útil (umas por cima das outras, um pouco de lado), do tamanho que pediu */
void orbit_float_place_new(struct orbit_view *view) {
	struct orbit_server *server = view->server;
	struct orbit_output *output = view->output;
	struct wlr_box full = { 0, 0, 1280, 720 };
	if (output) {
		wlr_output_layout_get_box(server->layout, output->wlr, &full);
	}
	struct wlr_box usable = output ? orbit_layer_usable(output, &full) : full;
	struct wlr_box g = client_geometry(view);
	int top = orbit_deco_top(view); /* a barra de título fica por cima da janela e também tem de caber */
	int w = g.width > 0 ? g.width : usable.width * 3 / 5;
	int h = g.height > 0 ? g.height : (usable.height - top) * 7 / 10;
	bool resize = g.width < 1 || g.height < 1;
	if (w > usable.width - 2 * server->gap) {
		w = usable.width - 2 * server->gap;
		resize = true;
	}
	if (h > usable.height - 2 * server->gap - top) {
		h = usable.height - 2 * server->gap - top;
		resize = true;
	}
	int others = 0;
	struct orbit_view *v;
	wl_list_for_each(v, &server->views, link) {
		if (v != view && v->mapped && v->output == output && v->tag == view->tag && v->floating && !v->minimized) {
			others++;
		}
	}
	int step = (others % 6) * 32;
	view->x = usable.x + (usable.width - w) / 2 + step;
	view->y = usable.y + top + (usable.height - top - h) / 2 + step;
	if (view->x + w > usable.x + usable.width) {
		view->x = usable.x + server->gap;
	}
	if (view->y + h > usable.y + usable.height) {
		view->y = usable.y + server->gap + top;
	}
	view->fx = view->x;
	view->fy = view->y;
	view->fw = w;
	view->fh = h;
	view->placed = true;
	view->sliding = false;
	view->slide_x = view->x;
	view->slide_y = view->y;
	view->slide_to_x = view->x; /* o set_size abaixo conta ao X11 daqui */
	view->slide_to_y = view->y;
	if (resize) {
		orbit_view_set_size(view, w, h);
	}
}

/* Uma barra ou dock que aparece (ou muda a zona dela) depois de uma janela livre já ter aberto: a janela que ficou por baixo dela volta para a área
 * útil (no início da sessão os apps e as barras sobem juntos, e quem abre primeiro vê a tela inteira). Só mexe em janela livre solta, nunca numa que
 * se arrasta ou que a pessoa encaixou/maximizou, e só quando a barra de título ou o fundo da janela ficariam fora da área útil. */
void orbit_float_fit(struct orbit_server *server) {
	struct orbit_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (!view->mapped || !view->floating || view->minimized || view->maximized || view->fullscreen || view->snap || !view->output ||
			view == server->grabbed) {
			continue;
		}
		struct wlr_box full;
		wlr_output_layout_get_box(server->layout, view->output->wlr, &full);
		struct wlr_box usable = orbit_layer_usable(view->output, &full);
		struct wlr_box g = geometry(view);
		int top = orbit_deco_top(view);
		int h = g.height > 0 ? g.height : view->h;
		int y = view->y;
		if (y - top < usable.y) {
			y = usable.y + top;
		}
		if (y + h > usable.y + usable.height && h + top <= usable.height) {
			y = usable.y + usable.height - h;
		}
		if (y == view->y) {
			continue;
		}
		int dy = y - view->y;
		view->y = y;
		view->fy += dy;
		view->slide_y = view->y;
		view->sliding = false;
	}
}

/* mostra uma janela livre onde ela está; só manda configure quando sai de maximizada/encaixada */
void orbit_float_show(struct orbit_view *view) {
	/* Interrompe a animação de entrada que um arranjo anterior começou (ela deixa a janela recortada pela posição de ANTES de chegar). Só zerar
	 * `sliding` deixava o recorte para sempre: a janela constava como ativa e visível, e não aparecia na tela (um app que voltava da Mídia para o Desktop
	 * parecia minimizado, e o clique dele na dock não fazia nada porque ele já era o ativo). */
	orbit_slide_unclip(view);
	view->leaving = false;
	view->sliding = false;
	wlr_scene_node_set_enabled(&view->scene_tree->node, true);
	if (view->border) {
		wlr_scene_node_set_enabled(&view->border->node, false);
	}
	if (view->scene_tree->node.parent != view->server->tile_tree) {
		wlr_scene_node_reparent(&view->scene_tree->node, view->server->tile_tree);
	}
	orbit_view_move_now(view, view->x, view->y);
	if (view->fw > 0 && view->fh > 0) {
		view->w = view->fw;
		view->h = view->fh;
	}
	if (!view->cfg_valid || view->cfg_full || view->cfg_max || view->cfg_tiled) {
		orbit_view_set_state(view, false, false, WLR_EDGE_NONE);
		if (view->cfg_valid && view->fw > 0 && view->fh > 0) {
			/* voltou de maximizada ou encaixada: o tamanho livre de antes */
			orbit_view_set_size(view, view->fw, view->fh);
		}
		view->cfg_valid = true;
		view->cfg_full = false;
		view->cfg_max = false;
		view->cfg_tiled = WLR_EDGE_NONE;
		view->cfg_w = view->fw;
		view->cfg_h = view->fh;
	}
	orbit_deco_update(view);
}

/* o encaixe fica na coluna da esquerda (1) ou da direita (2); 0 se não é encaixe */
static int snap_coluna(int snap) {
	switch (snap) {
	case ORBIT_SNAP_E: case ORBIT_SNAP_NE: case ORBIT_SNAP_SE: return 1;
	case ORBIT_SNAP_D: case ORBIT_SNAP_ND: case ORBIT_SNAP_SD: return 2;
	default: return 0;
	}
}

/* a linha do encaixe: 0 a altura inteira (metade da tela), 1 o canto de cima, 2 o de baixo */
static int snap_linha(int snap) {
	switch (snap) {
	case ORBIT_SNAP_NE: case ORBIT_SNAP_ND: return 1;
	case ORBIT_SNAP_SE: case ORBIT_SNAP_SD: return 2;
	default: return 0;
	}
}

static float frac(float f) {
	return f > 0 ? f : 0.5f;
}

/* O retângulo (de fora, com a barra de título) de um encaixe nesta tela: a área útil dividida pelas divisas da tela, com o espaço (gap) em volta
 * de cada janela. Sem janelas encaixadas, as divisas estão no meio. */
struct wlr_box orbit_snap_box_output(struct orbit_output *output, int snap) {
	struct orbit_server *server = output->server;
	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->layout, output->wlr, &full);
	struct wlr_box u = orbit_layer_usable(output, &full);
	int gap = server->gap;
	int col = snap_coluna(snap), lin = snap_linha(snap);
	int wl = (int)((u.width - 3 * gap) * frac(output->snap_x));
	int wr = u.width - 3 * gap - wl;
	struct wlr_box box = { 0 };
	box.x = col == 1 ? u.x + gap : u.x + 2 * gap + wl;
	box.width = col == 1 ? wl : wr;
	if (lin == 0) {
		box.y = u.y + gap;
		box.height = u.height - 2 * gap;
	} else {
		int cima = (int)((u.height - 3 * gap) * frac(col == 1 ? output->snap_yl : output->snap_yr));
		if (lin == 1) {
			box.y = u.y + gap;
			box.height = cima;
		} else {
			box.y = u.y + 2 * gap + cima;
			box.height = u.height - 3 * gap - cima;
		}
	}
	return box;
}

struct wlr_box orbit_snap_box(struct orbit_view *view, int snap) {
	return orbit_snap_box_output(view->output, snap);
}

/* as bordas de uma janela encaixada que dão para a divisa com as vizinhas (as outras encostam na tela e não se mexem) */
uint32_t orbit_snap_inner_edges(int snap) {
	int col = snap_coluna(snap), lin = snap_linha(snap);
	if (!col) {
		return 0;
	}
	uint32_t e = col == 1 ? WLR_EDGE_RIGHT : WLR_EDGE_LEFT;
	if (lin == 1) {
		e |= WLR_EDGE_BOTTOM;
	} else if (lin == 2) {
		e |= WLR_EDGE_TOP;
	}
	return e;
}

void orbit_view_snap(struct orbit_view *view, int snap) {
	if (!view->mapped || !view->floating || view->fullscreen) {
		return;
	}
	if (snap) {
		orbit_float_save(view);
	}
	if (view->maximized) {
		view->maximized = false;
	}
	view->snap = snap;
	if (!snap) {
		view->x = view->fx;
		view->y = view->fy;
	}
	orbit_arrange(view->server);
	orbit_focus(view);
}

/* Super+setas: ←/→ encaixa na metade (de novo no mesmo lado solta), ↑ maximiza, ↓ desfaz um
 * encaixe ou a maximização (e numa janela livre, minimiza) */
void orbit_snap_key(struct orbit_server *server, int dir) {
	struct orbit_view *view = server->focused;
	if (!view || !view->mapped || !view->floating) {
		return;
	}
	if (dir == 1 || dir == 2) {
		orbit_view_snap(view, view->snap == dir ? 0 : dir);
	} else if (dir == 3) {
		orbit_view_set_maximized(view, true); /* guarda o tamanho livre (o de antes do encaixe, se encaixada) */
	} else if (view->maximized) {
		orbit_view_set_maximized(view, false);
	} else if (view->snap) {
		orbit_view_snap(view, 0);
	} else {
		orbit_view_set_minimized(view, true);
	}
}

/* ---- arrastar ---- */

/* um retângulo vazado (contorno de largura w) feito de 4 retângulos, a partir do índice i de snap_rects */
static void zone_outline(struct orbit_server *server, int i, const struct wlr_box *b, int w, const float color[4]) {
	struct wlr_box part[4] = { { b->x, b->y, b->width, w }, { b->x, b->y + b->height - w, b->width, w },
		{ b->x, b->y + w, w, b->height - 2 * w }, { b->x + b->width - w, b->y + w, w, b->height - 2 * w } };
	for (int k = 0; k < 4; k++) {
		struct wlr_scene_rect **r = &server->snap_rects[i + k];
		if (!*r) {
			*r = wlr_scene_rect_create(server->tile_tree, part[k].width, part[k].height, color);
			if (!*r) {
				return;
			}
		}
		wlr_scene_rect_set_size(*r, part[k].width, part[k].height);
		wlr_scene_rect_set_color(*r, color);
		wlr_scene_node_set_position(&(*r)->node, part[k].x, part[k].y);
		wlr_scene_node_set_enabled(&(*r)->node, true);
		wlr_scene_node_raise_to_top(&(*r)->node);
	}
}

static void zone_fill(struct orbit_server *server, int i, const struct wlr_box *b, const float color[4]) {
	struct wlr_scene_rect **r = &server->snap_rects[i];
	if (!*r) {
		*r = wlr_scene_rect_create(server->tile_tree, b->width, b->height, color);
		if (!*r) {
			return;
		}
	}
	wlr_scene_rect_set_size(*r, b->width, b->height);
	wlr_scene_rect_set_color(*r, color);
	wlr_scene_node_set_position(&(*r)->node, b->x, b->y);
	wlr_scene_node_set_enabled(&(*r)->node, true);
	wlr_scene_node_raise_to_top(&(*r)->node);
}

/* a prévia do encaixe: o alvo (cheio) e, nas laterais, a outra metade só no contorno, como no desenho "duas metades" */
static void preview(struct orbit_server *server, struct wlr_box *box, struct wlr_box *other) {
	for (int i = 0; i < 10; i++) {
		if (server->snap_rects[i]) {
			wlr_scene_node_set_enabled(&server->snap_rects[i]->node, false);
		}
	}
	if (!box) {
		return;
	}
	static const float fill[4] = { 0.50f * 0.26f, 0.36f * 0.26f, 0.95f * 0.26f, 0.26f };
	static const float line[4] = { 0.66f, 0.52f, 1.0f, 1.0f };
	static const float faint_fill[4] = { 0.50f * 0.07f, 0.36f * 0.07f, 0.95f * 0.07f, 0.07f };
	static const float faint_line[4] = { 0.66f * 0.5f, 0.52f * 0.5f, 1.0f * 0.5f, 0.5f };
	if (other) {
		zone_fill(server, 0, other, faint_fill);
		zone_outline(server, 1, other, 2, faint_line);
	}
	zone_fill(server, 5, box, fill);
	zone_outline(server, 6, box, 3, line);
}

void orbit_move_begin(struct orbit_server *server, struct orbit_view *view) {
	if (orbit_view_isolated(view)) {
		return; /* a janela de uma tela em Gaming ou Mídia não se arrasta */
	}
	if (!view || !view->mapped || view->fullscreen || server->session_locked) {
		return;
	}
	if (!view->floating && orbit_free_desktop(server, view->output)) {
		view->floating = true;
	}
	if (!view->floating) {
		return; /* no mosaico (o telemóvel) não se arrasta */
	}
	double cx = server->cursor->x, cy = server->cursor->y;
	if (view->maximized || view->snap) {
		/* solta do encaixe: o tamanho livre de antes, debaixo do cursor no mesmo sítio relativo */
		struct wlr_box g = geometry(view);
		double rel = g.width > 0 ? (cx - view->x) / g.width : 0.5;
		int w = view->fw > 0 ? view->fw : g.width;
		view->maximized = false;
		view->snap = 0;
		int top = orbit_deco_top(view);
		view->x = (int)(cx - rel * w);
		view->y = top > 0 ? (int)cy + top / 2 : (int)cy - 16; /* com barra: o cursor no meio dela */
		view->fx = view->x;
		view->fy = view->y;
		orbit_arrange(server);
	}
	server->cursor_mode = ORBIT_CURSOR_MOVE;
	server->grabbed = view;
	server->grab_x = cx;
	server->grab_y = cy;
	server->grab_gx = view->x;
	server->grab_gy = view->y;
	server->snap_pending = 0;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "fleur");
	orbit_focus(view);
}

/* o cursor mexeu a arrastar: a janela segue, e nas bordas aparece a prévia do encaixe */
/* há outra saída ligada logo depois desta borda (dx, dy: de que lado)? Olha dois pontos, perto dos cantos, para um monitor menor ao lado também contar */
void orbit_move_motion(struct orbit_server *server) {
	struct orbit_view *view = server->grabbed;
	struct orbit_output *sob = output_at(server, server->cursor->x, server->cursor->y);
	if (sob && sob != view->output && orbit_output_isolated(sob)) {
		return; /* não se passa a janela para uma tela em Gaming ou Mídia: ela espera na borda */
	}
	view->x = server->grab_gx + (int)(server->cursor->x - server->grab_x);
	view->y = server->grab_gy + (int)(server->cursor->y - server->grab_y);
	orbit_view_move_now(view, view->x, view->y); /* e a janela X11 fica sabendo onde está */
	struct orbit_output *output = output_at(server, server->cursor->x, server->cursor->y);
	server->snap_pending = 0;
	if (!orbit_free_desktop(server, output)) {
		preview(server, NULL, NULL);
		return;
	}
	struct wlr_box full;
	wlr_output_layout_get_box(server->layout, output->wlr, &full);
	double x = server->cursor->x, y = server->cursor->y;
	/* O encaixe depende só do CURSOR (a borda da janela encostando na da tela não conta: uma janela larga agarrada pelo meio chegava à lateral
	 * muito antes do cursor e encaixava sem querer). As laterais do monitor são uma faixa fina (ORBIT_SNAP_ZONE): a janela passa por ela direto
	 * para o monitor do lado, e só entra no arranjo se for SOLTA com o cursor dentro dela. A altura do cursor escolhe o encaixe: perto do topo
	 * ou do fim da lateral, o canto (um quarto da tela); no meio, a metade com a altura inteira. Em cima, uma faixa fina maximiza. */
	bool esq = x <= full.x + ORBIT_SNAP_ZONE, dir = x >= full.x + full.width - 1 - ORBIT_SNAP_ZONE;
	if (esq || dir) {
		struct wlr_box u = orbit_layer_usable(output, &full);
		double fy = u.height > 0 ? (y - u.y) / (double)u.height : 0.5;
		int lin = fy < ORBIT_SNAP_CORNER ? 1 : fy > 1 - ORBIT_SNAP_CORNER ? 2 : 0;
		static const int codigo[2][3] = { { ORBIT_SNAP_E, ORBIT_SNAP_NE, ORBIT_SNAP_SE }, { ORBIT_SNAP_D, ORBIT_SNAP_ND, ORBIT_SNAP_SD } };
		server->snap_pending = codigo[esq ? 0 : 1][lin];
	} else if (y <= full.y + ORBIT_SNAP_TOP) {
		server->snap_pending = ORBIT_SNAP_MAXIMIZAR;
	}
	if (!server->snap_pending) {
		preview(server, NULL, NULL);
		return;
	}
	struct wlr_box box, other;
	struct wlr_box *other_ptr = NULL;
	if (server->snap_pending == ORBIT_SNAP_MAXIMIZAR) {
		box = orbit_layer_usable(output, &full);
	} else {
		/* o alvo cheio e, no contorno, o que sobra para a outra janela: a outra metade, ou o outro canto da mesma coluna */
		int s = server->snap_pending;
		int outro = snap_linha(s) == 0 ? (snap_coluna(s) == 1 ? ORBIT_SNAP_D : ORBIT_SNAP_E)
			: snap_linha(s) == 1 ? s + 1 : s - 1;
		box = orbit_snap_box_output(output, s);
		other = orbit_snap_box_output(output, outro);
		other_ptr = &other;
	}
	preview(server, &box, other_ptr);
}

/* largou: muda de tela se for o caso, e encaixa se estava numa borda */
void orbit_move_end(struct orbit_server *server) {
	struct orbit_view *view = server->grabbed;
	int snap = server->snap_pending;
	server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
	server->grabbed = NULL;
	server->snap_pending = 0;
	preview(server, NULL, NULL);
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
	if (!view || !view->mapped) {
		return;
	}
	struct orbit_output *dest = output_at(server, server->cursor->x, server->cursor->y);
	/* o cursor pode ficar parado na borda do monitor (o compositor de fora segura o mouse na janela do clique) com a janela já quase toda
	 * do outro lado: vale a saída que cobre a maior parte da janela, e não o pixel em que o cursor ficou */
	if (dest && !snap) {
		struct wlr_box jan = { view->x, view->y, view->w > 0 ? view->w : 1, view->h > 0 ? view->h : 1 };
		struct orbit_output *o, *maior = dest;
		long area_maior = 0;
		wl_list_for_each(o, &server->outputs, link) {
			struct wlr_box ob, inter;
			wlr_output_layout_get_box(server->layout, o->wlr, &ob);
			long area = wlr_box_intersection(&inter, &jan, &ob) ? (long)inter.width * inter.height : 0;
			if (area > area_maior && (o == dest || !orbit_output_isolated(o))) {
				area_maior = area;
				maior = o;
			}
		}
		dest = maior;
	}
	if (dest && dest != view->output && !orbit_output_isolated(dest) && !orbit_view_isolated(view)) {
		orbit_split_release(server, view);
		orbit_view_set_output(view, dest, false); /* largada com o mouse: já está no lugar */
		view->tag = dest->tag;
		view->fullscreen = false;
	}
	/* a posição livre fica a de onde começou a arrastar (para voltar a ela ao soltar o encaixe) */
	if (snap) {
		view->x = server->grab_gx;
		view->y = server->grab_gy;
		orbit_float_save(view);
	} else {
		view->fx = view->x;
		view->fy = view->y;
	}
	if (snap == ORBIT_SNAP_MAXIMIZAR) {
		orbit_view_set_maximized(view, true);
		return;
	}
	if (snap) {
		view->snap = snap;
	}
	orbit_arrange(server);
	orbit_focus(view);
}

/* ---- mudar o tamanho ---- */

#define ORBIT_EDGE_GRAB 6 /* px por dentro da borda de uma janela livre que pegam para mudar o tamanho */

/* as bordas de uma janela livre debaixo deste ponto (0: no meio dela) */
/* O quanto, por dentro da borda de uma janela livre, o cursor já está na faixa de mudar o tamanho (a janela não o vê ali): o ponteiro confinado fica de fora dela. */
int orbit_float_edge_margin(struct orbit_view *view) {
	if (!view || !view->floating || view->maximized || view->fullscreen) {
		return 0;
	}
	if (view->snap && !orbit_snap_inner_edges(view->snap)) {
		return 0;
	}
	return ORBIT_EDGE_GRAB + 1;
}

uint32_t orbit_float_edges_at(struct orbit_view *view, double lx, double ly) {
	if (!view || !view->floating || view->maximized || view->fullscreen) {
		return 0;
	}
	struct wlr_box g = geometry(view);
	int top = orbit_deco_top(view); /* a caixa de fora começa no topo da barra */
	double x = lx - view->x, y = ly - (view->y - top);
	int altura = g.height + top;
	uint32_t edges = 0;
	if (x < ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_LEFT;
	} else if (x > g.width - ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_RIGHT;
	}
	if (y < ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_TOP;
	} else if (y > altura - ORBIT_EDGE_GRAB) {
		edges |= WLR_EDGE_BOTTOM;
	}
	if (view->snap) {
		edges &= orbit_snap_inner_edges(view->snap); /* encaixada: só a divisa com as vizinhas se mexe */
	}
	return edges;
}

const char *orbit_edges_cursor(uint32_t edges) {
	switch (edges) {
	case WLR_EDGE_TOP: return "n-resize";
	case WLR_EDGE_BOTTOM: return "s-resize";
	case WLR_EDGE_LEFT: return "w-resize";
	case WLR_EDGE_RIGHT: return "e-resize";
	case WLR_EDGE_TOP | WLR_EDGE_LEFT: return "nw-resize";
	case WLR_EDGE_TOP | WLR_EDGE_RIGHT: return "ne-resize";
	case WLR_EDGE_BOTTOM | WLR_EDGE_LEFT: return "sw-resize";
	case WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT: return "se-resize";
	default: return "left_ptr";
	}
}

/* ---- mudar o tamanho de janelas encaixadas: a divisa entre elas ---- */

static int min_largura(struct orbit_view *view);
static int min_altura(struct orbit_view *view);

/* o menor tamanho que uma janela encaixada nesta coluna (ou linha) aceita: o do Orbit, ou o que o app pediu */
static int snap_min_dim(struct orbit_output *output, int coluna, int linha, bool largura) {
	int m = largura ? ORBIT_MIN_W : ORBIT_MIN_H;
	struct orbit_view *v;
	wl_list_for_each(v, &output->server->views, link) {
		if (!v->mapped || v->output != output || !v->snap || snap_coluna(v->snap) != coluna || (linha && snap_linha(v->snap) != linha)) {
			continue;
		}
		int pedido = largura ? min_largura(v) : min_altura(v);
		if (pedido > m) {
			m = pedido;
		}
	}
	return m;
}

static float limita(float f, int min_a, int min_b, int total) {
	float lo = total > 0 ? (float)min_a / total : ORBIT_SNAP_MIN_FRAC, hi = total > 0 ? 1.0f - (float)min_b / total : 1 - ORBIT_SNAP_MIN_FRAC;
	if (lo < ORBIT_SNAP_MIN_FRAC) {
		lo = ORBIT_SNAP_MIN_FRAC;
	}
	if (hi > 1 - ORBIT_SNAP_MIN_FRAC) {
		hi = 1 - ORBIT_SNAP_MIN_FRAC;
	}
	if (lo > hi) {
		return 0.5f;
	}
	return f < lo ? lo : f > hi ? hi : f;
}

static void snap_resize_begin(struct orbit_server *server, struct orbit_view *view, uint32_t edges) {
	server->cursor_mode = ORBIT_CURSOR_RESIZE;
	server->snap_resizing = true;
	server->snap_resize_edges = edges;
	server->grabbed = view;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, orbit_edges_cursor(edges & 15));
	orbit_focus(view);
}

/* o cursor arrasta a divisa: a fração da tela vira a posição dele, e todas as janelas encaixadas desta tela se refazem de uma vez */
static void snap_resize_motion(struct orbit_server *server) {
	struct orbit_view *view = server->grabbed;
	if (!view || !view->mapped || !view->output || !view->snap) {
		return;
	}
	struct orbit_output *output = view->output;
	struct wlr_box full = { 0 };
	wlr_output_layout_get_box(server->layout, output->wlr, &full);
	struct wlr_box u = orbit_layer_usable(output, &full);
	int gap = server->gap;
	int col = snap_coluna(view->snap), lin = snap_linha(view->snap);
	uint32_t e = server->snap_resize_edges;
	if (e & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) {
		int total = u.width - 3 * gap;
		/* o cursor no meio do espaço entre as duas colunas: a largura da coluna da esquerda é o que fica à esquerda dele */
		int esq = (int)(server->cursor->x - (u.x + gap) - gap / 2.0);
		output->snap_x = limita((float)esq / (total > 0 ? total : 1), snap_min_dim(output, 1, 0, true), snap_min_dim(output, 2, 0, true), total);
	}
	if ((e & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) && lin) {
		int total = u.height - 3 * gap;
		int cima = (int)(server->cursor->y - (u.y + gap) - gap / 2.0);
		/* a divisa vale para o canto de cima e o de baixo da coluna: os mínimos são dos dois */
		float f = limita((float)cima / (total > 0 ? total : 1), snap_min_dim(output, col, 1, false), snap_min_dim(output, col, 2, false), total);
		if (col == 1) {
			output->snap_yl = f;
		} else {
			output->snap_yr = f;
		}
	}
	orbit_arrange(server);
}

void orbit_resize_begin(struct orbit_server *server, struct orbit_view *view, uint32_t edges) {
	if (!view || !view->mapped || view->fullscreen || server->session_locked) {
		return;
	}
	if (!view->floating && orbit_free_desktop(server, view->output)) {
		view->floating = true;
	}
	if (!view->floating) {
		return;
	}
	if (view->snap && !view->maximized && edges && !(edges & ~orbit_snap_inner_edges(view->snap))) {
		snap_resize_begin(server, view, edges); /* a divisa entre janelas encaixadas: elas se redimensionam juntas */
		return;
	}
	if (view->maximized || view->snap) {
		/* encaixada: começa do tamanho em que está, agora livre */
		struct wlr_box g = geometry(view);
		view->fw = g.width;
		view->fh = g.height;
		view->fx = view->x;
		view->fy = view->y;
		view->maximized = false;
		view->snap = 0;
		orbit_arrange(server);
	}
	if (!edges) {
		/* Alt+botão direito: a borda mais perto do cursor */
		struct wlr_box g = geometry(view);
		double cx = server->cursor->x - view->x, cy = server->cursor->y - view->y;
		edges = (cx < g.width / 2.0 ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT) | (cy < g.height / 2.0 ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM);
	}
	struct wlr_box g = geometry(view);
	server->cursor_mode = ORBIT_CURSOR_RESIZE;
	server->grabbed = view;
	server->resize_edges = edges;
	server->grab_x = server->cursor->x;
	server->grab_y = server->cursor->y;
	server->grab_gx = view->x;
	server->grab_gy = view->y;
	server->grab_w = g.width;
	server->grab_h = g.height;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, orbit_edges_cursor(edges & 15));
	orbit_focus(view);
}

/* o menor tamanho de uma janela: o do Orbit, ou o que o próprio app pediu (xdg_toplevel.set_min_size), o que for maior */
static int min_largura(struct orbit_view *view) {
	int m = ORBIT_MIN_W;
	if (view->toplevel && view->toplevel->current.min_width > m) {
		m = view->toplevel->current.min_width;
	}
	return m;
}

static int min_altura(struct orbit_view *view) {
	int m = ORBIT_MIN_H;
	if (view->toplevel && view->toplevel->current.min_height > m) {
		m = view->toplevel->current.min_height;
	}
	return m;
}

void orbit_resize_motion(struct orbit_server *server) {
	if (server->snap_resizing) {
		snap_resize_motion(server);
		return;
	}
	struct orbit_view *view = server->grabbed;
	int dx = (int)(server->cursor->x - server->grab_x);
	int dy = (int)(server->cursor->y - server->grab_y);
	int l = server->grab_gx, t = server->grab_gy;
	int r = l + server->grab_w, b = t + server->grab_h;
	uint32_t e = server->resize_edges;
	if (e & WLR_EDGE_LEFT) {
		l += dx;
		if (r - l < min_largura(view)) {
			l = r - min_largura(view);
		}
	} else if (e & WLR_EDGE_RIGHT) {
		r += dx;
		if (r - l < min_largura(view)) {
			r = l + min_largura(view);
		}
	}
	if (e & WLR_EDGE_TOP) {
		t += dy;
		if (b - t < min_altura(view)) {
			t = b - min_altura(view);
		}
	} else if (e & WLR_EDGE_BOTTOM) {
		b += dy;
		if (b - t < min_altura(view)) {
			b = t + min_altura(view);
		}
	}
	view->x = l;
	view->y = t;
	view->fx = l;
	view->fy = t;
	view->fw = view->w = r - l;
	view->fh = view->h = b - t;
	orbit_view_move_now(view, l, t);
	orbit_view_set_resizing(view, true);
	orbit_view_set_size(view, r - l, b - t);
	view->cfg_w = r - l;
	view->cfg_h = b - t;
}

void orbit_resize_end(struct orbit_server *server) {
	server->snap_resizing = false;
	struct orbit_view *view = server->grabbed;
	server->cursor_mode = ORBIT_CURSOR_PASSTHROUGH;
	server->grabbed = NULL;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
	if (view && view->mapped) {
		orbit_view_set_resizing(view, false);
	}
}
