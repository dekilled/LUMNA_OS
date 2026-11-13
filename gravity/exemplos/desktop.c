/*
 * lumna-desktop: os ícones e as pastas da área de trabalho. Mostra o que há na pasta da Área de trabalho da pessoa (~/Desktop, ou a que o
 * xdg-user-dirs disser) numa grade, na camada de fundo, logo acima do papel de parede.
 *
 *  - Clique seleciona, duplo clique abre (pasta no Atlas, atalho .desktop executa, o resto no programa padrão), Ctrl+clique soma, o retângulo
 *    em volta (arrastar no vazio) seleciona vários.
 *  - Arrastar um ícone muda de lugar (a grade encaixa) ou leva os arquivos para uma pasta, para o Atlas, para outro app: é o arrastar e soltar do
 *    sistema (text/uri-list), o mesmo do Atlas. Soltar arquivos de fora aqui copia para a Área de trabalho.
 *  - Botão direito: no ícone (Abrir, Copiar, Recortar, Renomear, Lixeira) ou no vazio (Nova pasta, Novo documento, Colar e os atalhos do desktop).
 *  - F2 renomeia, Delete manda para a lixeira, Ctrl+C/X/V/A/Z, Enter abre.
 *
 * Os lugares dos ícones ficam em ~/.config/lumna/desktop.ini. A cópia, a mudança e a lixeira são as operações do Atlas (arquivos_op), com o
 * desfazer delas.
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include "aricone.h"
#include "arquivos.h"
#include "arquivos_op.h"
#include "gravity.h"

#define MIME_URIS "text/uri-list"
#define CELULA_W 108
#define CELULA_H 118
#define MARGEM 16
#define ICONE 58

struct item {
	char nome[256];
	char *caminho;
	ar_entrada e;
	char exibir[256]; /* o nome que se mostra (o Name= de um atalho .desktop) */
	bool atalho;      /* um .desktop */
	int col, row;     /* o lugar guardado (-1: ainda sem lugar) */
	int dcol, drow;   /* o lugar que aparece agora (cabe na tela e não bate em outro) */
	bool sel;
};

static gv_app *app;
static char *pasta_desktop;
static const char *minha_saida;  /* NULL: a tela principal (que abre as instâncias das outras) */
static char grupo[96] = "posicoes"; /* o grupo do desktop.ini com os lugares desta tela */
static char pref[80] = "";          /* nas linhas de depuração: "[SAIDA] " nas instâncias das outras telas */
static char *ini_texto;             /* o que o desktop.ini tinha na última leitura/gravação (a outra tela mexe nele) */
#define MAX_FILHAS 8
static struct { char saida[64]; pid_t pid; } filhas[MAX_FILHAS];
static int nfilhas;
static struct item *itens;
static int n_itens;
static GKeyFile *lugares;
static bool depurando;

static int colunas = 8, linhas = 6;
static bool geometria; /* já se sabe o tamanho da tela: antes disso nada ganha lugar (nem lugar guardado) */
static int renomeando = -1;
static char nome_edicao[256];
static bool foco_renomear;
static bool recarregar_pedido = true;
static int ultimo_item = -1;
static double ultimo_clique;
static int ancora = -1;

/* o retângulo de seleção */
static bool laco;
static float laco_x0, laco_y0;
static bool laco_somar;
static bool laco_base[4096];

/* o arraste */
static uint32_t dnd_cand;
static float dnd_x0, dnd_y0;
static int dnd_ancora = -1;      /* o ícone que se arrastou (para saber o deslocamento ao soltar dentro do desktop) */
static bool dnd_daqui;           /* o arraste em andamento saiu deste desktop */
static int alvo_col = -1, alvo_row = -1, alvo_item = -1; /* onde cairia um arraste que passa por cima */

/* onde se pediu o menu do vazio (a Nova pasta nasce ali) */
static int menu_col, menu_row;
static int pendente_col = -1, pendente_row = -1; /* onde pôr o que ainda vai aparecer (um arquivo copiado de fora, uma pasta nova) */
static bool pendente_renomear;

static char mensagem[300];
static double mensagem_ate;

#define MAX_OPS 8
static ar_op *ops[MAX_OPS];
static int n_ops;

static void avisar(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void avisar(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(mensagem, sizeof(mensagem), fmt, ap);
	va_end(ap);
	mensagem_ate = gv_agora() + 5;
	gv_app_redesenhar(app);
}

/* ---- onde ficam os ícones ---- */

static char *arquivo_lugares(void) {
	return g_build_filename(g_get_user_config_dir(), "lumna", "desktop.ini", NULL);
}

static void lugares_ler(void) {
	g_clear_pointer(&lugares, g_key_file_free);
	lugares = g_key_file_new();
	char *c = arquivo_lugares();
	g_key_file_load_from_file(lugares, c, G_KEY_FILE_NONE, NULL);
	g_free(c);
	g_free(ini_texto);
	ini_texto = g_key_file_to_data(lugares, NULL, NULL);
}

static void lugares_gravar(void) {
	char *c = arquivo_lugares(), *pai = g_path_get_dirname(c);
	g_mkdir_with_parents(pai, 0700);
	g_key_file_save_to_file(lugares, c, NULL);
	g_free(ini_texto);
	ini_texto = g_key_file_to_data(lugares, NULL, NULL);
	g_free(pai);
	g_free(c);
}

/* o item mora numa tela só: o lugar vale no grupo desta e sai dos grupos das outras */
static void lugar_gravar_nome(const char *nome, int col, int row) {
	gsize n = 0;
	char **gs = g_key_file_get_groups(lugares, &n);
	for (gsize i = 0; i < n; i++) {
		if ((!strcmp(gs[i], "posicoes") || g_str_has_prefix(gs[i], "posicoes ")) && strcmp(gs[i], grupo)) {
			g_key_file_remove_key(lugares, gs[i], nome, NULL);
		}
	}
	g_strfreev(gs);
	g_key_file_set_integer_list(lugares, grupo, nome, (int[]){ col, row }, 2);
}

static void lugar_guardar(const struct item *it) {
	if (it->col >= 0) {
		lugar_gravar_nome(it->nome, it->col, it->row);
	}
}

static bool lugar_ler(struct item *it) {
	gsize n = 0;
	int *v = g_key_file_get_integer_list(lugares, grupo, it->nome, &n, NULL);
	bool ok = v && n == 2 && v[0] >= 0 && v[1] >= 0;
	if (ok) {
		it->col = v[0];
		it->row = v[1];
	} else {
		it->col = it->row = -1;
	}
	g_free(v);
	return ok;
}

/* o item é desta tela? Tem lugar no grupo dela; sem lugar em nenhuma, ou num grupo de uma tela que não existe mais, é da principal */
static bool item_e_daqui(const char *nome) {
	if (g_key_file_has_key(lugares, grupo, nome, NULL)) {
		return true;
	}
	if (minha_saida) {
		return false;
	}
	const char *saidas[8];
	int n = gv_saidas(app, saidas, 8);
	gsize ng = 0;
	char **gs = g_key_file_get_groups(lugares, &ng);
	bool de_outra = false;
	for (gsize i = 0; i < ng && !de_outra; i++) {
		if (!g_str_has_prefix(gs[i], "posicoes ") || !g_key_file_has_key(lugares, gs[i], nome, NULL)) {
			continue;
		}
		for (int k = 1; k < n; k++) { /* as outras telas (a 0 é a principal) */
			de_outra |= !strcmp(saidas[k], gs[i] + 9);
		}
	}
	g_strfreev(gs);
	return !de_outra;
}

static bool celula_ocupada(int col, int row, int ignorar) {
	for (int i = 0; i < n_itens; i++) {
		if (i != ignorar && itens[i].dcol == col && itens[i].drow == row) {
			return true;
		}
	}
	return false;
}

/* a célula livre mais perto de (col,row), em anéis cada vez maiores */
static bool celula_perto(int col, int row, int ignorar, int *rc, int *rr) {
	if (col < 0) col = 0;
	if (row < 0) row = 0;
	if (col >= colunas) col = colunas - 1;
	if (row >= linhas) row = linhas - 1;
	for (int d = 0; d <= colunas + linhas; d++) {
		for (int dc = -d; dc <= d; dc++) {
			for (int dr = -d; dr <= d; dr++) {
				if (abs(dc) != d && abs(dr) != d) {
					continue;
				}
				int c = col + dc, r = row + dr;
				if (c >= 0 && c < colunas && r >= 0 && r < linhas && !celula_ocupada(c, r, ignorar)) {
					*rc = c;
					*rr = r;
					return true;
				}
			}
		}
	}
	return false;
}

/* o primeiro lugar livre: de cima para baixo, coluna por coluna (como na maioria das áreas de trabalho) */
static bool primeira_livre(int ignorar, int *rc, int *rr) {
	for (int c = 0; c < colunas; c++) {
		for (int r = 0; r < linhas; r++) {
			if (!celula_ocupada(c, r, ignorar)) {
				*rc = c;
				*rr = r;
				return true;
			}
		}
	}
	return false;
}

/* decide onde cada ícone aparece: o lugar guardado se couber na tela e estiver livre; senão, o mais perto livre */
static void arrumar(void) {
	for (int i = 0; i < n_itens; i++) {
		itens[i].dcol = itens[i].drow = -1;
	}
	if (!geometria) {
		return;
	}
	/* 1. os que têm lugar e cabem */
	for (int i = 0; i < n_itens; i++) {
		struct item *it = &itens[i];
		if (it->col >= 0 && it->col < colunas && it->row < linhas && !celula_ocupada(it->col, it->row, i)) {
			it->dcol = it->col;
			it->drow = it->row;
		}
	}
	/* 2. os que não têm lugar: o que acabou de nascer vai para onde se pediu (a pasta nova, o arquivo solto); os outros, o primeiro livre */
	bool lugar_novo = false;
	for (int i = 0; i < n_itens; i++) {
		struct item *it = &itens[i];
		if (it->dcol >= 0) {
			continue;
		}
		int c, r;
		bool ok;
		if (it->col < 0 && pendente_col >= 0) {
			ok = celula_perto(pendente_col, pendente_row, i, &c, &r);
		} else if (it->col >= 0) {
			ok = celula_perto(it->col, it->row, i, &c, &r); /* o lugar dele não cabe (a tela encolheu) ou está tomado */
		} else {
			ok = primeira_livre(i, &c, &r);
		}
		if (ok) {
			it->dcol = c;
			it->drow = r;
			if (it->col < 0) { /* um ícone novo: o lugar vale e fica guardado */
				it->col = c;
				it->row = r;
				lugar_guardar(it);
				lugar_novo = true;
			}
		}
	}
	if (lugar_novo) {
		lugares_gravar();
	}
}

/* ---- ler a pasta ---- */

static void item_liberar(struct item *it) {
	g_free(it->caminho);
}

static void ler_atalho(struct item *it) {
	GKeyFile *kf = g_key_file_new();
	if (g_key_file_load_from_file(kf, it->caminho, G_KEY_FILE_NONE, NULL)) {
		char *n = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
		if (n && *n) {
			g_strlcpy(it->exibir, n, sizeof(it->exibir));
		}
		g_free(n);
	}
	g_key_file_free(kf);
}

static void ler_pasta(void) {
	recarregar_pedido = false;
	/* uma edição de nome em andamento sobrevive à recarga (o aviso de "pasta mudou" chega logo depois de criar uma pasta nova) */
	char editando[256] = "";
	if (renomeando >= 0 && renomeando < n_itens) {
		g_strlcpy(editando, itens[renomeando].nome, sizeof(editando));
	}
	ar_lista l;
	char *erro = NULL;
	if (!ar_listar(pasta_desktop, false, &l, &erro)) {
		g_free(erro);
		return;
	}
	ar_ordenar(&l, AR_O_NOME, false);
	struct item *novos = g_new0(struct item, l.n > 0 ? l.n : 1);
	int n = 0;
	for (int i = 0; i < l.n; i++) {
		if (!item_e_daqui(l.v[i].nome)) {
			continue; /* mora na área de trabalho de outra tela */
		}
		struct item *it = &novos[n];
		it->e = l.v[i];
		g_strlcpy(it->nome, l.v[i].nome, sizeof(it->nome));
		it->caminho = g_build_filename(pasta_desktop, l.v[i].nome, NULL);
		g_strlcpy(it->exibir, l.v[i].nome, sizeof(it->exibir));
		it->atalho = !l.v[i].pasta && g_str_has_suffix(l.v[i].nome, ".desktop");
		if (it->atalho) {
			ler_atalho(it);
		}
		for (int k = 0; k < n_itens; k++) { /* o que já se sabia: a seleção fica */
			if (!strcmp(itens[k].nome, it->nome)) {
				it->sel = itens[k].sel;
			}
		}
		lugar_ler(it);
		n++;
	}
	ar_liberar(&l);
	for (int i = 0; i < n_itens; i++) {
		item_liberar(&itens[i]);
	}
	g_free(itens);
	itens = novos;
	n_itens = n;
	renomeando = -1;
	for (int i = 0; i < n_itens && editando[0]; i++) {
		if (!strcmp(itens[i].nome, editando)) {
			renomeando = i;
		}
	}
	arrumar();
	pendente_col = pendente_row = -1;
	if (pendente_renomear) {
		pendente_renomear = false;
	}
	ultimo_item = -1;
	ancora = -1;
	gv_app_redesenhar(app);
}

static void pasta_mudou(gv_app *a, int fd, void *dados) {
	(void)a;
	(void)dados;
	char buf[4096];
	while (read(fd, buf, sizeof(buf)) > 0) {
	}
	recarregar_pedido = true;
	gv_app_redesenhar(app);
}

/* ---- o que se faz com os itens ---- */

static int quantos_selecionados(void) {
	int n = 0;
	for (int i = 0; i < n_itens; i++) {
		n += itens[i].sel;
	}
	return n;
}

static void limpar_selecao(void) {
	for (int i = 0; i < n_itens; i++) {
		itens[i].sel = false;
	}
}

static GPtrArray *caminhos_selecionados(void) {
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; i < n_itens; i++) {
		if (itens[i].sel) {
			g_ptr_array_add(v, g_strdup(itens[i].caminho));
		}
	}
	return v;
}

static void executar(const char *comando) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			setsid();
			execl("/bin/sh", "sh", "-c", comando, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

static void abrir_item(int idx) {
	struct item *it = &itens[idx];
	if (it->e.pasta) {
		char *q = g_shell_quote(it->caminho), *cmd = g_strdup_printf("lumna-atlas %s", q);
		executar(cmd);
		g_free(cmd);
		g_free(q);
		return;
	}
	GError *err = NULL;
	if (it->atalho) {
		GDesktopAppInfo *info = g_desktop_app_info_new_from_filename(it->caminho);
		if (!info || !g_app_info_launch(G_APP_INFO(info), NULL, NULL, &err)) {
			avisar("Não consegui abrir “%s”%s%s", it->exibir, err ? ": " : "", err ? err->message : "");
		}
		g_clear_error(&err);
		g_clear_object(&info);
		return;
	}
	char *uri = g_filename_to_uri(it->caminho, NULL, NULL);
	if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &err)) {
		avisar("Não há programa para abrir “%s”", it->exibir);
		g_clear_error(&err);
	}
	g_free(uri);
}

static void op_acordou(void *dados) {
	(void)dados;
	gv_app_redesenhar(app);
}

static void iniciar_op(enum ar_op_tipo tipo, GPtrArray *origens, const char *destino) {
	if (n_ops >= MAX_OPS || !origens->len) {
		avisar("Espere as operações em andamento");
		return;
	}
	ar_op *op = ar_op_iniciar(tipo, (char *const *)origens->pdata, (int)origens->len, destino, op_acordou, NULL);
	if (op) {
		ops[n_ops++] = op;
	} else {
		avisar("Não consegui começar");
	}
}

static void ops_ver(void) {
	for (int i = 0; i < n_ops;) {
		ar_op_estado st;
		ar_op_estado_ler(ops[i], &st);
		if (st.pergunta) { /* sem janela de pergunta no desktop: os dois ficam (nome (2)) */
			ar_op_responder(ops[i], AR_C_MANTER_AMBOS, true);
		}
		if (st.terminou) {
			if (st.erros) {
				avisar("%s", st.erro[0] ? st.erro : "Algo deu errado");
			}
			ar_op_liberar(ops[i]);
			ops[i] = ops[--n_ops];
			recarregar_pedido = true;
			continue;
		}
		i++;
	}
	if (n_ops) {
		gv_acordar_em(0.2);
	}
}

static void copiar_selecao(bool recortar) {
	GPtrArray *v = caminhos_selecionados();
	if (v->len) {
		char *t = ar_area_gravar(recortar, (char *const *)v->pdata, (int)v->len);
		gv_copiar(t);
		g_free(t);
		avisar("%u %s %s", v->len, v->len == 1 ? "item" : "itens", recortar ? (v->len == 1 ? "recortado" : "recortados") : (v->len == 1 ? "copiado" : "copiados"));
	}
	g_ptr_array_free(v, TRUE);
}

static void colar(int col, int row) {
	bool recortar = false;
	char **l = ar_area_ler(gv_colar(), &recortar);
	if (!l) {
		avisar("Não há nada copiado para colar");
		return;
	}
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; l[i]; i++) {
		if (l[i][0]) {
			g_ptr_array_add(v, g_strdup(l[i]));
		}
	}
	g_strfreev(l);
	pendente_col = col;
	pendente_row = row;
	iniciar_op(recortar ? AR_OP_MOVER : AR_OP_COPIAR, v, pasta_desktop);
	g_ptr_array_free(v, TRUE);
}

static void para_lixeira(void) {
	GPtrArray *v = caminhos_selecionados();
	if (v->len) {
		iniciar_op(AR_OP_LIXEIRA, v, NULL);
		avisar("%u %s para a lixeira (Ctrl+Z desfaz)", v->len, v->len == 1 ? "item" : "itens");
	}
	g_ptr_array_free(v, TRUE);
}

static void desfazer(void) {
	char *erro = NULL;
	if (ar_desfazer(&erro)) {
		recarregar_pedido = true;
		avisar("Desfeito");
	} else {
		avisar("%s", erro ? erro : "Não há o que desfazer");
	}
	g_free(erro);
}

static void iniciar_renomear(int idx) {
	renomeando = idx;
	g_strlcpy(nome_edicao, itens[idx].nome, sizeof(nome_edicao));
	foco_renomear = true;
}

static void criar(bool pasta, int col, int row) {
	char *criado = NULL, *erro = NULL;
	if (!ar_criar(pasta_desktop, pasta ? "Nova pasta" : "Novo documento.txt", pasta, &criado, &erro)) {
		avisar("%s", erro ? erro : "Não deu para criar");
		g_free(erro);
		return;
	}
	pendente_col = col;
	pendente_row = row;
	ler_pasta();
	limpar_selecao();
	char *nome = g_path_get_basename(criado);
	for (int i = 0; i < n_itens; i++) {
		if (!strcmp(itens[i].nome, nome)) {
			itens[i].sel = true;
			iniciar_renomear(i);
		}
	}
	g_free(nome);
	g_free(criado);
}

static void confirmar_renome(void) {
	int idx = renomeando;
	renomeando = -1;
	if (idx < 0 || idx >= n_itens || !strcmp(nome_edicao, itens[idx].nome)) {
		return;
	}
	char *motivo = NULL;
	if (!ar_nome_valido(nome_edicao, &motivo)) {
		avisar("%s", motivo ? motivo : "Nome inválido");
		g_free(motivo);
		return;
	}
	char *novo = NULL, *erro = NULL;
	char antigo[256];
	g_strlcpy(antigo, itens[idx].nome, sizeof(antigo));
	int col = itens[idx].col, row = itens[idx].row;
	if (ar_renomear(itens[idx].caminho, nome_edicao, &novo, &erro)) {
		/* o lugar passa para o nome novo */
		g_key_file_remove_key(lugares, grupo, antigo, NULL);
		char *n = g_path_get_basename(novo);
		lugar_gravar_nome(n, col, row);
		lugares_gravar();
		g_free(n);
		recarregar_pedido = true;
	} else {
		avisar("%s", erro ? erro : "Não deu para renomear");
	}
	g_free(novo);
	g_free(erro);
}

/* ---- o arraste ---- */

static void celula_do_ponto(Clay_Vector2 p, int *col, int *row) {
	*col = (int)floorf((p.x - MARGEM) / CELULA_W);
	*row = (int)floorf((p.y - MARGEM) / CELULA_H);
	if (*col < 0) *col = 0;
	if (*row < 0) *row = 0;
	if (*col >= colunas) *col = colunas - 1;
	if (*row >= linhas) *row = linhas - 1;
}

static void arrastar_ver(int idx, Clay_ElementId id) {
	if (gv_arrastando_daqui() || gv_ponteiro_e_toque()) {
		return;
	}
	Clay_Vector2 p = gv_ponteiro();
	if (!gv_premido(id)) {
		if (dnd_cand == id.id) {
			dnd_cand = 0;
		}
		return;
	}
	if (dnd_cand != id.id) {
		dnd_cand = id.id;
		dnd_x0 = p.x;
		dnd_y0 = p.y;
		return;
	}
	if ((p.x - dnd_x0) * (p.x - dnd_x0) + (p.y - dnd_y0) * (p.y - dnd_y0) <= 100) {
		return;
	}
	dnd_cand = 0;
	if (!itens[idx].sel) { /* arrastar um item solto leva só ele */
		limpar_selecao();
		itens[idx].sel = true;
	}
	/* uma linha de comentário (a spec do text/uri-list permite; o Atlas e os outros ignoram) diz de qual instância o arraste saiu: o destino
	 * distingue "mudar de lugar nesta tela" de "veio da área de trabalho de outra tela" */
	GString *g = g_string_new(NULL);
	g_string_append_printf(g, "# lumna-desktop %d\r\n", (int)getpid());
	int n = 0;
	for (int i = 0; i < n_itens; i++) {
		if (itens[i].sel) {
			char *uri = g_filename_to_uri(itens[i].caminho, NULL, NULL);
			if (uri) {
				g_string_append_printf(g, "%s\r\n", uri);
				n++;
			}
			g_free(uri);
		}
	}
	if (n && gv_arrastar_comecar(MIME_URIS, g->str, n > 1 ? "copy" : itens[idx].e.pasta ? "folder" : "document")) {
		dnd_ancora = idx;
		dnd_daqui = true;
	}
	g_string_free(g, TRUE);
}

static bool ponteiro_em_p(Clay_BoundingBox b, Clay_Vector2 p) {
	return b.width > 0 && p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
}

/* soltaram arquivos aqui: de dentro (muda de lugar, ou vai para uma pasta) ou de fora (copia para a Área de trabalho) */
static void soltar_ver(void) {
	alvo_col = alvo_row = alvo_item = -1;
	static Clay_Vector2 ultimo_p;
	static int ultimo_col = -1, ultimo_row = -1, ultimo_item_alvo = -1;
	Clay_Vector2 p;
	bool sobre = gv_arraste_sobre(&p);
	if (sobre) {
		celula_do_ponto(p, &alvo_col, &alvo_row);
		for (int i = 0; i < n_itens; i++) {
			if (itens[i].e.pasta && !itens[i].sel && ponteiro_em_p(gv_caixa(CLAY_IDI("dk-cel", (uint32_t)i)), p)) {
				alvo_item = i;
			}
		}
		ultimo_p = p;
		ultimo_col = alvo_col;
		ultimo_row = alvo_row;
		ultimo_item_alvo = alvo_item;
	}
	char *dados = NULL;
	if (!gv_arraste_soltou(MIME_URIS, &dados)) {
		if (!sobre) {
			ultimo_col = ultimo_row = ultimo_item_alvo = -1;
		}
		return;
	}
	(void)ultimo_p;
	int col = ultimo_col, row = ultimo_row, alvo = ultimo_item_alvo;
	if (depurando) {
		printf("%ssolta col=%d row=%d alvo=%d\n", pref, col, row, alvo);
	}
	ultimo_col = ultimo_row = ultimo_item_alvo = -1;
	bool daqui = false;
	int ancora_idx = dnd_ancora;
	dnd_daqui = false;
	dnd_ancora = -1;
	{ /* de qual instância o arraste saiu (a linha de comentário que o arrastar_ver põe) */
		const char *m = strstr(dados, "# lumna-desktop ");
		daqui = m && atoi(m + 16) == (int)getpid();
	}
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	bool todos_do_desktop = true;
	char **ls = g_strsplit_set(dados, "\r\n", -1);
	for (int i = 0; ls[i]; i++) {
		if (!g_str_has_prefix(ls[i], "file://")) {
			continue;
		}
		char *c = g_filename_from_uri(ls[i], NULL, NULL);
		if (!c) {
			continue;
		}
		char *pai = g_path_get_dirname(c);
		if (strcmp(pai, pasta_desktop)) {
			todos_do_desktop = false;
		}
		g_free(pai);
		g_ptr_array_add(v, c);
	}
	g_strfreev(ls);
	free(dados);
	if (!v->len || col < 0) {
		g_ptr_array_free(v, TRUE);
		return;
	}
	if (alvo >= 0) {
		/* em cima de uma pasta: vai para dentro dela (de dentro do desktop, muda; de fora, copia) */
		GPtrArray *vai = g_ptr_array_new_with_free_func(g_free);
		for (guint i = 0; i < v->len; i++) {
			const char *c = g_ptr_array_index(v, i);
			if (strcmp(c, itens[alvo].caminho)) { /* não põe a pasta dentro dela mesma */
				g_ptr_array_add(vai, g_strdup(c));
			}
		}
		if (vai->len) {
			iniciar_op(todos_do_desktop ? AR_OP_MOVER : AR_OP_COPIAR, vai, itens[alvo].caminho);
		}
		g_ptr_array_free(vai, TRUE);
	} else if (todos_do_desktop && daqui && ancora_idx >= 0 && ancora_idx < n_itens) {
		/* mudar de lugar: o ícone que se pegou vai para a célula de baixo do ponteiro e os outros mantêm a distância dele */
		int dc = col - itens[ancora_idx].dcol, dr = row - itens[ancora_idx].drow;
		int n_sel = 0;
		struct { int i, c, r; } mv[512];
		for (int i = 0; i < n_itens && n_sel < 512; i++) {
			if (itens[i].sel) {
				mv[n_sel].i = i;
				mv[n_sel].c = itens[i].dcol + dc;
				mv[n_sel].r = itens[i].drow + dr;
				n_sel++;
			}
		}
		/* tira os movidos da grade, para a conta de "livre" não esbarrar neles mesmos */
		for (int k = 0; k < n_sel; k++) {
			itens[mv[k].i].dcol = itens[mv[k].i].drow = -1;
		}
		for (int k = 0; k < n_sel; k++) {
			int c, r;
			if (celula_perto(mv[k].c, mv[k].r, mv[k].i, &c, &r)) {
				itens[mv[k].i].dcol = c;
				itens[mv[k].i].drow = r;
			}
		}
		for (int k = 0; k < n_sel; k++) {
			struct item *it = &itens[mv[k].i];
			if (it->dcol >= 0) {
				it->col = it->dcol;
				it->row = it->drow;
				lugar_guardar(it);
			}
		}
		lugares_gravar();
		arrumar();
	} else if (todos_do_desktop && !daqui) {
		/* veio da área de trabalho de outra tela: os ícones passam a morar nesta, a partir de onde se soltou */
		for (guint i = 0; i < v->len; i++) {
			char *nome = g_path_get_basename(g_ptr_array_index(v, i));
			int r = row + (int)i;
			lugar_gravar_nome(nome, col, r < linhas ? r : linhas - 1); /* se bater em outro, o arrumar acha o livre mais perto */
			g_free(nome);
		}
		lugares_gravar();
		recarregar_pedido = true;
	} else if (!todos_do_desktop) {
		/* de fora: copia para a Área de trabalho e os que aparecerem ficam onde se soltou */
		pendente_col = col;
		pendente_row = row;
		iniciar_op(AR_OP_COPIAR, v, pasta_desktop);
	}
	g_ptr_array_free(v, TRUE);
	gv_app_redesenhar(app);
}

/* ---- desenho ---- */

static ic_dados ic_pool[1024];
static int ic_n;

static void icone_do_item(const struct item *it, Clay_ElementId id) {
	ic_dados *d = &ic_pool[ic_n++ % (int)(sizeof(ic_pool) / sizeof(ic_pool[0]))];
	memset(d, 0, sizeof(*d));
	d->tipo = it->e.tipo;
	d->link = it->e.link;
	if (it->atalho) {
		d->tipo = AR_OUTRO;
		g_strlcpy(d->selo, "APP", sizeof(d->selo));
	} else if (!it->e.pasta) {
		ar_selo(&it->e, d->selo, sizeof(d->selo));
	}
	CLAY(id, gv_livre(ar_icone_desenhar, d, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(ICONE), CLAY_SIZING_FIXED(ICONE) } } })) {}
}

static const char *so_utf8(const char *s) {
	if (g_utf8_validate(s, -1, NULL)) {
		return s;
	}
	char *v = g_utf8_make_valid(s, -1);
	const char *r = gv_str(v).chars;
	g_free(v);
	return r;
}

static void celula(int idx) {
	struct item *it = &itens[idx];
	Clay_ElementId id = CLAY_IDI("dk-cel", (uint32_t)idx);
	float x = MARGEM + (float)it->dcol * CELULA_W, y = MARGEM + (float)it->drow * CELULA_H;
	bool sobre = gv_por_cima(id);
	bool alvo = alvo_item == idx;
	Clay_Color fundo = alvo ? gv_alfa(gv_t.acento, 0.45f) : it->sel ? gv_alfa(gv_t.acento, 0.28f) : sobre ? (Clay_Color){ 255, 255, 255, 30 } : (Clay_Color){ 0, 0, 0, 0 };
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(CELULA_W - 8), CLAY_SIZING_FIXED(CELULA_H - 6) }, .padding = { 4, 4, 8, 4 }, .childGap = 6,
			.childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .offset = { x + 4, y + 3 }, .zIndex = 2 },
		.backgroundColor = fundo, .cornerRadius = CLAY_CORNER_RADIUS(10), .border = { .color = it->sel ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .width = GV_BORDA(2) } }) {
		icone_do_item(it, CLAY_IDI("dk-ic", (uint32_t)idx));
		if (renomeando == idx) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				int r = gv_campo(CLAY_ID("dk-ren"), nome_edicao, sizeof(nome_edicao), "Nome", GV_CAMPO_TEXTO);
				if (foco_renomear) {
					gv_focar(CLAY_ID("dk-ren"));
					foco_renomear = false;
				}
				if (r & GV_CAMPO_ENTER) {
					confirmar_renome();
				}
			}
		} else {
			CLAY(CLAY_IDI("dk-nome", (uint32_t)idx), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 2, 2 }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
				.backgroundColor = it->sel ? (Clay_Color){ 0, 0, 0, 0 } : (Clay_Color){ 0, 0, 0, 105 }, .cornerRadius = CLAY_CORNER_RADIUS(6) }) {
				Clay_String ls[2];
				int nl = gv_texto_ajustar(so_utf8(it->exibir), CELULA_W - 26, gv_t.tamanho_corpo - 1, GV_PESO_NORMAL, 2, ls);
				for (int i = 0; i < nl; i++) {
					gv_texto(ls[i], gv_t.tamanho_corpo - 1, (Clay_Color){ 255, 255, 255, 255 }, GV_PESO_NORMAL);
				}
			}
		}
	}
}

/* o clique: seleciona; o segundo em seguida abre */
static void tratar_clique(int idx, Clay_ElementId id, bool *tocou) {
	if (!gv_tocado(id) || renomeando == idx) {
		return;
	}
	*tocou = true;
	double agora = gv_agora();
	uint32_t mods = gv_modificadores();
	gv_app_redesenhar(app);
	if (idx == ultimo_item && agora - ultimo_clique < 0.45 && !(mods & (GV_MOD_CTRL | GV_MOD_SHIFT))) {
		abrir_item(idx);
		ultimo_item = -1;
		return;
	}
	if (mods & GV_MOD_SHIFT && ancora >= 0 && ancora < n_itens) {
		/* do último clicado até este, pela ordem dos itens */
		int a = ancora < idx ? ancora : idx, b = ancora < idx ? idx : ancora;
		if (!(mods & GV_MOD_CTRL)) {
			limpar_selecao();
		}
		for (int i = a; i <= b; i++) {
			itens[i].sel = true;
		}
	} else if (mods & GV_MOD_CTRL) {
		itens[idx].sel = !itens[idx].sel;
		ancora = idx;
	} else {
		limpar_selecao();
		itens[idx].sel = true;
		ancora = idx;
	}
	ultimo_item = idx;
	ultimo_clique = agora;
}

/* ---- menus ---- */

static void menu_do_item(void) {
	Clay_ElementId m = CLAY_ID("dk-menu-item");
	GV_MENU_PONTEIRO(m) {
		int n = quantos_selecionados();
		if (n == 1) {
			int idx = -1;
			for (int i = 0; i < n_itens; i++) {
				idx = itens[i].sel ? i : idx;
			}
			if (idx >= 0 && gv_menu_item(CLAY_ID("dk-m-abrir"), "folder", "Abrir", "Enter")) {
				abrir_item(idx);
				gv_popup_fechar(m);
			}
		}
		if (gv_menu_item(CLAY_ID("dk-m-copiar"), "copy", "Copiar", "Ctrl+C")) {
			copiar_selecao(false);
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("dk-m-recortar"), "cut", "Recortar", "Ctrl+X")) {
			copiar_selecao(true);
			gv_popup_fechar(m);
		}
		if (n == 1 && gv_menu_item(CLAY_ID("dk-m-renomear"), "edit", "Renomear", "F2")) {
			for (int i = 0; i < n_itens; i++) {
				if (itens[i].sel) {
					iniciar_renomear(i);
				}
			}
			gv_popup_fechar(m);
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("dk-m-lixeira"), "trash", "Lixeira", "Delete")) {
			para_lixeira();
			gv_popup_fechar(m);
		}
	}
}

static void menu_do_vazio(void) {
	Clay_ElementId m = CLAY_ID("dk-menu-vazio");
	GV_MENU_PONTEIRO(m) {
		if (gv_menu_item(CLAY_ID("dk-v-pasta"), "folder", "Nova pasta", "Ctrl+Shift+N")) {
			criar(true, menu_col, menu_row);
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("dk-v-doc"), "document", "Novo documento", NULL)) {
			criar(false, menu_col, menu_row);
			gv_popup_fechar(m);
		}
		bool r;
		char **pode = ar_area_ler(gv_colar(), &r);
		if (pode) {
			if (gv_menu_item(CLAY_ID("dk-v-colar"), "paste", "Colar", "Ctrl+V")) {
				colar(menu_col, menu_row);
				gv_popup_fechar(m);
			}
			g_strfreev(pode);
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("dk-v-terminal"), "terminal", "Abrir terminal", NULL)) {
			executar("${TERMINAL:-foot}");
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("dk-v-atlas"), "folder", "Atlas", NULL)) {
			executar("lumna-atlas");
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("dk-v-apps"), "grid", "Todos os apps", NULL)) {
			executar("lumna-lancador");
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("dk-v-emma"), "sparkle", "Perguntar à Emma", NULL)) {
			executar("lumna-emma");
			gv_popup_fechar(m);
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("dk-v-papel"), "image", "Papel de parede…", NULL)) {
			executar("lumna-painel --papel");
			gv_popup_fechar(m);
		}
		if (gv_menu_item(CLAY_ID("dk-v-config"), "settings", "Configurações", NULL)) {
			executar("lumna-painel");
			gv_popup_fechar(m);
		}
	}
}

/* ---- teclado ---- */

static void teclas(void) {
	uint32_t mods = gv_modificadores();
	bool ctrl = mods & GV_MOD_CTRL, shift = mods & GV_MOD_SHIFT;
	for (int i = 0;; i++) {
		uint32_t s = gv_tecla_n(i);
		if (!s) {
			break;
		}
		if (renomeando >= 0) {
			if (s == XKB_KEY_Escape) {
				renomeando = -1;
			}
			continue; /* o campo cuida do resto */
		}
		uint32_t k = s >= 'A' && s <= 'Z' ? s + 32 : s;
		if (ctrl && k == 'a') {
			for (int j = 0; j < n_itens; j++) {
				itens[j].sel = true;
			}
		} else if (ctrl && k == 'c') {
			copiar_selecao(false);
		} else if (ctrl && k == 'x') {
			copiar_selecao(true);
		} else if (ctrl && k == 'v') {
			int c, r;
			if (!primeira_livre(-1, &c, &r)) {
				c = r = 0;
			}
			colar(c, r);
		} else if (ctrl && k == 'z') {
			desfazer();
		} else if (ctrl && shift && k == 'n') {
			int c, r;
			if (!primeira_livre(-1, &c, &r)) {
				c = r = 0;
			}
			criar(true, c, r);
		} else if (s == XKB_KEY_Delete || s == XKB_KEY_KP_Delete) {
			para_lixeira();
		} else if (s == XKB_KEY_F2 && quantos_selecionados() == 1) {
			for (int j = 0; j < n_itens; j++) {
				if (itens[j].sel) {
					iniciar_renomear(j);
				}
			}
		} else if ((s == XKB_KEY_Return || s == XKB_KEY_KP_Enter) && !ctrl) {
			for (int j = 0; j < n_itens; j++) {
				if (itens[j].sel) {
					abrir_item(j);
				}
			}
		} else if (s == XKB_KEY_Escape) {
			limpar_selecao();
		}
	}
}

/* ---- depuração dos testes ---- */

static void depurar(void) {
	if (!depurando) {
		return;
	}
	static GHashTable *antes;
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	for (int i = 0; i < n_itens; i++) {
		Clay_BoundingBox b = gv_caixa(CLAY_IDI("dk-cel", (uint32_t)i));
		if (b.width <= 0) {
			continue;
		}
		char *l = g_strdup_printf("%.0f %.0f %.0f %.0f", b.x, b.y, b.width, b.height);
		char *a = g_hash_table_lookup(antes, itens[i].nome);
		if (!a || strcmp(a, l)) {
			printf("%spos cel/%s %s\n", pref, itens[i].nome, l);
			g_hash_table_insert(antes, g_strdup(itens[i].nome), l);
		} else {
			g_free(l);
		}
	}
	GString *e = g_string_new(pref);
	g_string_append(e, "estado ");
	g_string_append_printf(e, "total=%d grade=%dx%d ops=%d sel=[", n_itens, colunas, linhas, n_ops);
	bool primeiro = true;
	for (int i = 0; i < n_itens; i++) {
		if (itens[i].sel) {
			g_string_append_printf(e, "%s%s", primeiro ? "" : "|", itens[i].nome);
			primeiro = false;
		}
	}
	g_string_append(e, "] lugares=[");
	for (int i = 0; i < n_itens; i++) {
		g_string_append_printf(e, "%s%s@%d,%d", i ? "|" : "", itens[i].nome, itens[i].dcol, itens[i].drow);
	}
	g_string_append_printf(e, "] ren=[%s] msg=[%s]", renomeando >= 0 ? itens[renomeando].nome : "-", mensagem_ate > gv_agora() ? mensagem : "");
	static char *ultimo;
	if (!ultimo || strcmp(ultimo, e->str)) {
		printf("%s\n", e->str);
		g_free(ultimo);
		ultimo = g_strdup(e->str);
	}
	g_string_free(e, TRUE);
	fflush(stdout);
}

/* ---- uma área de trabalho por tela (a da principal abre as das outras, como o papel de parede) ---- */

static void filha_abrir(const char *saida) {
	pid_t p = fork();
	if (p == 0) {
		prctl(PR_SET_PDEATHSIG, SIGTERM); /* a principal fechou: esta também */
		execl("/proc/self/exe", "desktop", "--saida", saida, (char *)NULL);
		_exit(127);
	}
	if (p > 0 && nfilhas < MAX_FILHAS) {
		snprintf(filhas[nfilhas].saida, sizeof(filhas[0].saida), "%s", saida);
		filhas[nfilhas++].pid = p;
	}
}

static void filhas_rever(void) {
	const char *saidas[8];
	int n = gv_saidas(app, saidas, 8);
	for (int i = 0; i < nfilhas; i++) {
		bool existe = false;
		for (int k = 1; k < n; k++) {
			existe |= !strcmp(saidas[k], filhas[i].saida);
		}
		bool morreu = waitpid(filhas[i].pid, NULL, WNOHANG) == filhas[i].pid;
		if (!existe || morreu) {
			if (!morreu) {
				kill(filhas[i].pid, SIGTERM);
				waitpid(filhas[i].pid, NULL, 0);
			}
			filhas[i--] = filhas[--nfilhas];
		}
	}
	for (int k = 1; k < n; k++) {
		bool tem = false;
		for (int i = 0; i < nfilhas; i++) {
			tem |= !strcmp(filhas[i].saida, saidas[k]);
		}
		if (!tem) {
			filha_abrir(saidas[k]);
		}
	}
}

/* o desktop.ini mudou (a área de trabalho de outra tela moveu um ícone para cá ou daqui): lê de novo, se o conteúdo é outro */
static void ini_mudou(gv_app *a, int fd, void *dados) {
	(void)a;
	(void)dados;
	char buf[4096];
	while (read(fd, buf, sizeof(buf)) > 0) {
	}
	char *c = arquivo_lugares(), *texto = NULL;
	GKeyFile *kf = g_key_file_new();
	char *novo = g_key_file_load_from_file(kf, c, G_KEY_FILE_NONE, NULL) ? g_key_file_to_data(kf, NULL, NULL) : NULL;
	if (novo && (!ini_texto || strcmp(novo, ini_texto))) {
		lugares_ler();
		recarregar_pedido = true;
		gv_app_redesenhar(app);
	}
	g_free(novo);
	g_free(texto);
	g_key_file_free(kf);
	g_free(c);
}

/* ---- o quadro ---- */

static void desenhar(gv_app *a, void *dados) {
	(void)a;
	(void)dados;
	ops_ver();
	if (!minha_saida && gv_saidas_mudaram(app)) {
		filhas_rever();
		recarregar_pedido = true; /* os ícones de uma tela que saiu voltam para a principal */
	}
	if (recarregar_pedido) {
		ler_pasta();
	}
	Clay_ElementId raiz = CLAY_ID("dk-raiz");
	Clay_BoundingBox rb = gv_caixa(raiz);
	if (rb.width > 0) {
		int c = (int)((rb.width - 2 * MARGEM) / CELULA_W), r = (int)((rb.height - 2 * MARGEM) / CELULA_H);
		if (c < 1) c = 1;
		if (r < 1) r = 1;
		if (c != colunas || r != linhas || !geometria) {
			colunas = c;
			linhas = r;
			geometria = true;
			arrumar();
		}
	}
	teclas();
	soltar_ver();

	/* o menu: o mais de dentro pede primeiro */
	bool algum_menu = false;
	for (int i = 0; i < n_itens && !algum_menu; i++) {
		Clay_ElementId id = CLAY_IDI("dk-cel", (uint32_t)i);
		if (gv_menu_no_ponteiro(id, CLAY_ID("dk-menu-item")) && ponteiro_em_p(gv_caixa(id), gv_ponteiro())) {
			if (!itens[i].sel) {
				limpar_selecao();
				itens[i].sel = true;
				ancora = i;
			}
			algum_menu = true;
		}
	}
	if (!algum_menu && gv_menu_no_ponteiro(raiz, CLAY_ID("dk-menu-vazio"))) {
		celula_do_ponto(gv_ponteiro(), &menu_col, &menu_row);
		limpar_selecao();
	}

	bool raiz_tocada = gv_tocado(raiz), item_tocado = false;
	/* o retângulo de seleção: arrastar começando no vazio */
	Clay_Vector2 p = gv_ponteiro();
	static float px0, py0;
	static bool candidato;
	if (!gv_ponteiro_baixo() || gv_arrastando_daqui()) {
		laco = false;
		candidato = false;
	} else if (!laco && gv_premido(raiz) && !gv_ponteiro_e_toque()) {
		bool em_item = false;
		for (int i = 0; i < n_itens; i++) {
			em_item = em_item || ponteiro_em_p(gv_caixa(CLAY_IDI("dk-cel", (uint32_t)i)), p);
		}
		if (!candidato) {
			px0 = p.x;
			py0 = p.y;
			candidato = !em_item;
		} else if (fabsf(p.x - px0) > 5 || fabsf(p.y - py0) > 5) {
			laco = true;
			laco_x0 = px0;
			laco_y0 = py0;
			laco_somar = gv_modificadores() & GV_MOD_CTRL;
			for (int i = 0; i < n_itens && i < 4096; i++) {
				laco_base[i] = itens[i].sel;
			}
			candidato = false;
		}
	}
	Clay_BoundingBox lr = { 0 };
	if (laco) {
		lr.x = fminf(laco_x0, p.x);
		lr.y = fminf(laco_y0, p.y);
		lr.width = fabsf(p.x - laco_x0);
		lr.height = fabsf(p.y - laco_y0);
		for (int i = 0; i < n_itens; i++) {
			float x = MARGEM + (float)itens[i].dcol * CELULA_W + 4, y = MARGEM + (float)itens[i].drow * CELULA_H + 3, w = CELULA_W - 8, h = CELULA_H - 6;
			bool cruza = x < lr.x + lr.width && x + w > lr.x && y < lr.y + lr.height && y + h > lr.y;
			itens[i].sel = cruza || (laco_somar && i < 4096 && laco_base[i]);
		}
		gv_app_redesenhar(app);
	}

	CLAY(raiz, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		/* o lugar onde o arraste cairia */
		if (alvo_col >= 0 && alvo_item < 0) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(CELULA_W - 8), CLAY_SIZING_FIXED(CELULA_H - 6) } },
				.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .offset = { MARGEM + (float)alvo_col * CELULA_W + 4, MARGEM + (float)alvo_row * CELULA_H + 3 }, .zIndex = 1,
					.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH },
				.backgroundColor = gv_alfa(gv_t.acento, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(10), .border = { .color = gv_alfa(gv_t.acento, 0.6f), .width = GV_BORDA(2) } }) {}
		}
		for (int i = 0; i < n_itens; i++) {
			if (itens[i].dcol < 0) {
				continue;
			}
			Clay_ElementId id = CLAY_IDI("dk-cel", (uint32_t)i);
			arrastar_ver(i, id);
			tratar_clique(i, id, &item_tocado);
			celula(i);
		}
		if (laco) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(lr.width), CLAY_SIZING_FIXED(lr.height) } },
				.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .offset = { lr.x, lr.y }, .zIndex = 3, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH },
				.backgroundColor = gv_alfa(gv_t.acento, 0.18f), .border = { .color = gv_alfa(gv_t.acento, 0.8f), .width = GV_BORDA(1) } }) {}
		}
		/* o aviso, embaixo no meio */
		if (mensagem[0] && gv_agora() < mensagem_ate) {
			CLAY(CLAY_ID("dk-aviso"), { .layout = { .padding = { 16, 16, 9, 9 } },
				.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_CENTER_BOTTOM, CLAY_ATTACH_POINT_CENTER_BOTTOM }, .offset = { 0, -24 }, .zIndex = 4,
					.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				gv_texto(gv_str(mensagem), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			gv_acordar_em(0.5);
		}
	}
	/* um clique no vazio tira a seleção (e confirma o renomear) */
	if (raiz_tocada && !item_tocado && !laco) {
		if (renomeando >= 0) {
			confirmar_renome();
		}
		limpar_selecao();
		ancora = -1;
		gv_app_redesenhar(app);
	}
	menu_do_item();
	menu_do_vazio();
	depurar();
}

/* a pasta da Área de trabalho: a do xdg-user-dirs, a não ser que ela seja a pasta pessoal inteira (o xdg-user-dirs grava XDG_DESKTOP_DIR="$HOME"
 * quando a pessoa não tem uma pasta de desktop: os ícones seriam tudo o que há na casa, o cache, os programas...). Aí vale ~/Desktop, que começa vazia. */
static char *achar_pasta_desktop(void) {
	const char *d = g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
	const char *casa = g_get_home_dir();
	if (d && *d) {
		char *limpa = g_canonicalize_filename(d, NULL), *casa_limpa = g_canonicalize_filename(casa, NULL);
		bool e_casa = !strcmp(limpa, casa_limpa) || !strcmp(limpa, "/");
		g_free(casa_limpa);
		if (!e_casa) {
			return limpa;
		}
		g_free(limpa);
	}
	return g_build_filename(casa, "Desktop", NULL);
}

int main(int argc, char **argv) {
	if (argc >= 3 && !strcmp(argv[1], "--saida")) {
		minha_saida = argv[2];
		snprintf(grupo, sizeof(grupo), "posicoes %s", minha_saida);
		snprintf(pref, sizeof(pref), "[%s] ", minha_saida);
	}
	depurando = getenv("LUMNA_DESKTOP_DEBUG") != NULL;
	if (depurando) {
		setvbuf(stdout, NULL, _IOLBF, 0);
	}
	pasta_desktop = achar_pasta_desktop();
	g_mkdir_with_parents(pasta_desktop, 0755);
	lugares_ler();
	ar_op_conflito_padrao(AR_C_MANTER_AMBOS);
	/* (o Orbit mantém esta camada por cima do papel de parede, que é do mesmo nível: não depende de quem nasce primeiro) */
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Área de trabalho", .app_id = "lumna-desktop", .tipo = GV_CAMADA, .nivel = GV_NIVEL_FUNDO, .ancoras = GV_ANCORA_TUDO,
		.zona_exclusiva = 0, .teclado = true, .saida = minha_saida ? minha_saida : "@principal" });
	if (!app) {
		return 1;
	}
	gv_arraste_aceitar(MIME_URIS);
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd >= 0 && inotify_add_watch(fd, pasta_desktop, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB) >= 0) {
		gv_app_vigiar(app, fd, pasta_mudou, NULL);
	}
	/* o desktop.ini é compartilhado entre as telas: uma mexe, as outras leem */
	char *ini = arquivo_lugares(), *pasta_ini = g_path_get_dirname(ini);
	g_mkdir_with_parents(pasta_ini, 0700);
	int fd_ini = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd_ini >= 0 && inotify_add_watch(fd_ini, pasta_ini, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) >= 0) {
		gv_app_vigiar(app, fd_ini, ini_mudou, NULL);
	}
	g_free(pasta_ini);
	g_free(ini);
	if (!minha_saida) {
		filhas_rever();
	}
	return gv_app_correr(app, desenhar, NULL);
}
