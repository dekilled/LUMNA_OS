/*
 * lumna-atlas (Atlas): o explorador de arquivos do LUMNA OS.
 *
 *   barra de título · barra de ferramentas (voltar, avançar, subir, caminho, busca, novo, ordenar, grade/lista, ajustes)
 *   barra lateral (Espaços, Coleções, Fixados) · a pasta em grade ou lista (só desenha o que está à vista) · painel de detalhes
 *   barra de status (quantos itens, o que está selecionado, o tamanho dos ícones)
 *
 * O que sabe do disco está em servicos/arquivos.c (testado sem tela). Abrir um arquivo usa o aplicativo padrão do tipo (Painel > Aplicativos padrão).
 * A pasta de agora é vigiada (inotify): o que muda por fora aparece aqui.
 *
 *   lumna-atlas [pasta]
 */
#define _GNU_SOURCE
#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <pango/pangocairo.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "arquivos.h"
#include "arquivos_cfg.h"
#include "arquivos_discos.h"
#include "arquivos_op.h"
#include "arquivos_prop.h"
#include "arquivos_rede.h"
#include "connectc.h"
#include "gravity.h"
#include "json.h"

/* ---- estado ---- */

enum vista { V_PASTA, V_RECENTES, V_FAVORITOS, V_INICIO, V_REDE, V_NUVEM };
static bool em_lixeira(void);
#define ESPECIAL_RECENTES "@recentes"
#define ESPECIAL_FAVORITOS "@favoritos"
#define ESPECIAL_INICIO "@inicio"
#define ESPECIAL_REDE "@rede"
#define ESPECIAL_NUVEM "@nuvem"
/* páginas de cartões (sem lista de arquivos): o Início e as duas da Rede */
#define VISTA_CARTOES (vista == V_INICIO || vista == V_REDE || vista == V_NUVEM)

static gv_app *app;
static enum vista vista;
static char pasta[4096];
static ar_lista lista;
static char **cam;          /* o caminho completo de cada entrada de lista */
static bool *sel;           /* selecionado (parelho com lista.v) */
static int *vis, nvis;      /* o que aparece (índices em lista.v), depois do filtro e da busca */
static int ancora = -1;     /* o último clicado (o começo de uma faixa com Shift) */
static int foco = -1;       /* o item do teclado */
static char erro_pasta[256];
static ar_hist hist;
static int vista_atual;     /* a visualização de agora (a padrão, ou a lembrada da pasta) */
static int pagina_cfg = -1; /* -1: os arquivos; 0..4: uma página de configurações */
static bool zoom_sujo;
static int edit_item = -1;  /* na página da barra lateral: o item com o editor aberto */
static ar_cfg cfg;          /* os ajustes de agora (os salvos); as páginas de configurações editam uma cópia */
static ar_cfg cfg_edit;
static ar_barra barra;      /* a barra lateral de agora */
static ar_barra barra_edit;
#define modo_lista (vista_atual != AR_VISTA_GRADE)
static enum ar_filtro filtro = AR_F_TUDO;
static char busca[128];
static bool detalhes = true;
static int inotify_fd = -1;
static int inotify_wd = -1;
static char aviso[256];
static double aviso_ate;
static double ultimo_clique;
static int ultimo_item = -1;  /* o último item clicado (para o duplo clique) */
static int menu_item = -1;  /* o item do menu de contexto */
static bool focar_nome;     /* o formulário do + acabou de abrir: o campo do nome pega o foco */
static int novo_tipo;       /* 0 nada, 1 pasta, 2 arquivo de texto: o formulário do "+" */
static char novo_nome[256];
static int n_pastas, n_arquivos;
/* operações (copiar, mover, lixeira...) */
static ar_op *op;
static ar_op_estado op_st;
static enum ar_op_tipo op_tipo;
static double desfazer_ate;         /* até quando a barra de status oferece "Desfazer" */
static char desfazer_texto[96];
static bool op_cortada;             /* o que se cola vem de um Recortar */
static bool confirmar_apagar;       /* a confirmação de apagar para sempre */
static GPtrArray *apagar_alvos;
static bool propriedades_abertas;
static void pr_abrir(void);
static int renomeando = -1;         /* o item que está sendo renomeado (índice em lista.v) */
static char renome_nome[256];
static bool focar_renome;
static int renome_quadros;
static bool editando_caminho;       /* Ctrl+L: o caminho vira um campo com sugestões */
static char caminho_txt[4096];
static bool focar_caminho;
static int caminho_quadros;
static ar_sugestao sug[12];
static int n_sug, sug_sel = -1;
static bool confirmar_esvaziar;     /* confirmar esvaziar a lixeira */
static bool clonar_aberto;          /* o diálogo do git clone */
static char clonar_url[512];
static bool focar_clonar;
static bool para_todos;             /* o "aplicar a todos" da pergunta de conflito */
static int cont_filtros[6];

#define ID_CONTEUDO CLAY_ID("conteudo")

static void avisar(const char *formato, ...) __attribute__((format(printf, 1, 2)));
static void avisar(const char *formato, ...) {
	va_list ap;
	va_start(ap, formato);
	vsnprintf(aviso, sizeof(aviso), formato, ap);
	va_end(ap);
	aviso_ate = gv_agora() + 4;
	gv_acordar_em(4.2);
	gv_app_redesenhar(app);
}

static float tam_icone(void) { return (float)cfg.tam_pasta * 0.85f; }
static float altura_linha(void) { return vista_atual == AR_VISTA_LISTA_GRANDE ? 76.0f : 44.0f; }
static void mudar_vista(int v);
static void conteudo_inicio(void);
static void pedir_esvaziar(void);
static void abrir_configuracoes(int pagina);
static void conteudo_rede(void);
static bool rede_dialogo_aberto(void);
static void rede_rever(void);
static void sync_rever(void);
static void rede_menus(void);
static void rede_dialogos(void);
static void menu_lateral(void);

/* ---- carregar a pasta ---- */

static void liberar_lista(void) {
	if (cam) {
		for (int i = 0; i < lista.n; i++) {
			g_free(cam[i]);
		}
		g_free(cam);
		cam = NULL;
	}
	g_free(sel);
	g_free(vis);
	sel = NULL;
	vis = NULL;
	nvis = 0;
	ar_liberar(&lista);
}

static void refiltrar(void) {
	g_free(vis);
	vis = g_new(int, lista.n > 0 ? lista.n : 1);
	nvis = ar_filtrar(&lista, busca, filtro, vis);
	n_pastas = n_arquivos = 0;
	memset(cont_filtros, 0, sizeof(cont_filtros));
	for (int i = 0; i < lista.n; i++) {
		if (lista.v[i].pasta) {
			n_pastas++;
		} else {
			n_arquivos++;
		}
		for (int f = 0; f < 6; f++) {
			cont_filtros[f] += ar_no_filtro(&lista.v[i], (enum ar_filtro)f);
		}
	}
}

static void nomear_caminhos(void) {
	cam = g_new0(char *, lista.n > 0 ? lista.n : 1);
	for (int i = 0; i < lista.n; i++) {
		cam[i] = vista == V_PASTA ? ar_unir(pasta, lista.v[i].nome) : NULL;
	}
}

static void vigiar_pasta(void) {
	if (inotify_fd < 0) {
		return;
	}
	if (inotify_wd >= 0) {
		inotify_rm_watch(inotify_fd, inotify_wd);
		inotify_wd = -1;
	}
	if (vista == V_PASTA) {
		inotify_wd = inotify_add_watch(inotify_fd, pasta, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF);
	}
}

/* Recarregar a lista (libera e refaz lista, cam, sel e vis) no MEIO do desenho deixava a grade, a lista e os menus lendo memória liberada: um
 * Enter no renome, um clique que abre uma pasta, "mostrar ocultos"... e o Atlas caía (visto com o ASAN). Enquanto se desenha, o pedido só fica
 * anotado; a recarga roda no começo do quadro seguinte, quando nada está segurando ponteiros para a lista. */
static int recarga_bloqueada;
static bool recarga_pendente, recarga_preservar;

static void carregar(bool preservar) {
	extern void previas_limpar(void);
	if (recarga_bloqueada) {
		recarga_preservar = recarga_pendente ? recarga_preservar && preservar : preservar;
		recarga_pendente = true;
		gv_app_redesenhar(app);
		return;
	}
	if (!preservar) {
		previas_limpar();
	}
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	if (preservar && sel) {
		for (int i = 0; i < lista.n; i++) {
			if (sel[i]) {
				g_ptr_array_add(nomes, g_strdup(cam[i] ? cam[i] : lista.v[i].nome));
			}
		}
	}
	liberar_lista();
	erro_pasta[0] = '\0';
	if (vista == V_PASTA) {
		char *erro = NULL;
		if (!ar_listar(pasta, cfg.ocultos, &lista, &erro)) {
			snprintf(erro_pasta, sizeof(erro_pasta), "%s", erro ? erro : "Não consegui abrir esta pasta");
			g_free(erro);
			memset(&lista, 0, sizeof(lista));
		}
		ar_ordenar(&lista, cfg.ordem, cfg.decrescente);
		nomear_caminhos();
	} else if (VISTA_CARTOES) {
		lista.v = g_new(ar_entrada, 1);
		lista.cap = 1;
		cam = g_new0(char *, 1);
	} else {
		ar_lugar lug[200];
		int n = vista == V_RECENTES ? ar_recentes(lug, 200) : ar_favoritos(lug, 200);
		lista.v = g_new(ar_entrada, n > 0 ? n : 1);
		lista.cap = n > 0 ? n : 1;
		cam = g_new0(char *, n > 0 ? n : 1);
		for (int i = 0; i < n; i++) {
			if (ar_entrada_de(lug[i].caminho, &lista.v[lista.n])) {
				cam[lista.n++] = g_strdup(lug[i].caminho);
			}
		}
		ar_lugares_liberar(lug, n);
	}
	sel = g_new0(bool, lista.n > 0 ? lista.n : 1);
	for (guint k = 0; k < nomes->len; k++) {
		for (int i = 0; i < lista.n; i++) {
			if (!strcmp(nomes->pdata[k], cam[i] ? cam[i] : lista.v[i].nome)) {
				sel[i] = true;
			}
		}
	}
	g_ptr_array_free(nomes, TRUE);
	refiltrar();
	if (!preservar) {
		ancora = foco = ultimo_item = -1;
	} else if (foco >= lista.n) {
		foco = -1;
	}
	vigiar_pasta();
	gv_app_redesenhar(app);
}

static void ir_para(const char *destino, bool na_historia) {
	if (!strcmp(destino, ESPECIAL_RECENTES)) {
		vista = V_RECENTES;
	} else if (!strcmp(destino, ESPECIAL_FAVORITOS)) {
		vista = V_FAVORITOS;
	} else if (!strcmp(destino, ESPECIAL_INICIO)) {
		vista = V_INICIO;
	} else if (!strcmp(destino, ESPECIAL_REDE)) {
		vista = V_REDE;
	} else if (!strcmp(destino, ESPECIAL_NUVEM)) {
		vista = V_NUVEM;
	} else {
		vista = V_PASTA;
		snprintf(pasta, sizeof(pasta), "%s", destino);
		char *lix = ar_lixeira_pasta();
		if (!strcmp(destino, lix)) {
			g_mkdir_with_parents(lix, 0700); /* a lixeira ainda não existe até a primeira vez */
		}
		g_free(lix);
	}
	if (na_historia) {
		ar_hist_ir(&hist, destino);
	}
	busca[0] = '\0';
	filtro = AR_F_TUDO;
	if (cfg.lembrar_por_pasta) {
		int v = vista == V_PASTA ? ar_visao_da_pasta(pasta) : -1;
		vista_atual = v >= 0 ? v : cfg.vista; /* cada pasta lembra a sua; sem lembrança, a padrão */
	}
	if (vista == V_PASTA && strcmp(cfg.ultima_pasta, pasta)) {
		g_strlcpy(cfg.ultima_pasta, pasta, sizeof(cfg.ultima_pasta));
		ar_cfg_gravar(&cfg);
	}
	carregar(false);
	gv_rolar_para(ID_CONTEUDO, 0, 0, false);
}

static void subir(void) {
	if (vista != V_PASTA) {
		return;
	}
	char *pai = ar_pai(pasta);
	if (pai) {
		ir_para(pai, true);
		g_free(pai);
	}
}

static void voltar(void) {
	const char *d = ar_hist_voltar(&hist);
	if (d) {
		ir_para(d, false);
	}
}

static void avancar(void) {
	const char *d = ar_hist_avancar(&hist);
	if (d) {
		ir_para(d, false);
	}
}

/* ---- abas ----
 * Cada aba tem o seu histórico (o lugar de agora é o fim dele) e onde a lista estava rolada. A aba de agora usa o `hist` global, como sempre;
 * as outras ficam guardadas em abas[]. Trocar de aba guarda a de agora e relê o lugar da outra. */
#define MAX_ABAS 12
typedef struct aba {
	ar_hist hist;
	double rolagem;
} aba;
static aba abas[MAX_ABAS];
static int n_abas = 1;
static int aba_atual;

static void aba_guardar(void) {
	abas[aba_atual].hist = hist;
	abas[aba_atual].rolagem = gv_rolagem_posicao(ID_CONTEUDO).y;
}

/* mostra a aba aba_atual (o `hist` já é dela) */
static void aba_carregar(void) {
	ir_para(ar_hist_atual(&hist), false);
	gv_rolar_para(ID_CONTEUDO, 0, abas[aba_atual].rolagem, false);
}

static void aba_mostrar(int i) {
	if (i < 0 || i >= n_abas || i == aba_atual) {
		return;
	}
	aba_guardar();
	aba_atual = i;
	hist = abas[i].hist;
	aba_carregar();
}

/* uma aba nova logo depois da de agora, já nela. destino NULL: o Início */
static void aba_nova(const char *destino) {
	if (n_abas >= MAX_ABAS) {
		avisar("Já são %d abas abertas", MAX_ABAS);
		return;
	}
	aba_guardar();
	int novo = aba_atual + 1;
	memmove(&abas[novo + 1], &abas[novo], sizeof(aba) * (size_t)(n_abas - novo));
	n_abas++;
	memset(&abas[novo], 0, sizeof(aba));
	ar_hist_ir(&abas[novo].hist, destino ? destino : ESPECIAL_INICIO);
	aba_atual = novo;
	hist = abas[novo].hist;
	aba_carregar();
}

static void aba_fechar(int i) {
	if (n_abas <= 1 || i < 0 || i >= n_abas) {
		return;
	}
	ar_hist_liberar(i == aba_atual ? &hist : &abas[i].hist);
	memmove(&abas[i], &abas[i + 1], sizeof(aba) * (size_t)(n_abas - i - 1));
	n_abas--;
	if (i == aba_atual) {
		aba_atual = i >= n_abas ? n_abas - 1 : i;
		hist = abas[aba_atual].hist;
		aba_carregar();
	} else if (i < aba_atual) {
		aba_atual--;
	}
}

static void abrir_arquivo(const char *caminho) {
	char *uri = g_filename_to_uri(caminho, NULL, NULL);
	GError *err = NULL;
	if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &err)) {
		char *base = g_path_get_basename(caminho);
		avisar("Não consegui abrir %s: %s", base, err && err->message ? err->message : "nenhum aplicativo para este tipo");
		g_free(base);
		g_clear_error(&err);
	} else {
		ar_recente_registrar(caminho);
	}
	g_free(uri);
}

static void abrir_item(int i) {
	if (i < 0 || i >= lista.n) {
		return;
	}
	if (lista.v[i].pasta) {
		ir_para(cam[i], true);
	} else {
		abrir_arquivo(cam[i]);
	}
}

/* A pasta mudou por fora (um download, uma cópia, o gerenciador de pacotes...): cada aviso do inotify relia a pasta inteira, de imediato. Numa pasta
 * grande com arquivos chegando, isso era uma releitura por arquivo (a lista pisca e a janela trava). Agora os avisos se juntam: relê 0,3 s depois do
 * último, e no máximo 1,5 s depois do primeiro se não para de chegar. */
static bool ino_pendente;
static double ino_primeiro, ino_ultimo;

static void inotify_pronto(gv_app *a, int fd, void *dados) {
	char buf[4096];
	bool mudou = false;
	while (read(fd, buf, sizeof(buf)) > 0) {
		mudou = true;
	}
	if (mudou && vista == V_PASTA) {
		double agora = gv_agora();
		if (!ino_pendente) {
			ino_pendente = true;
			ino_primeiro = agora;
		}
		ino_ultimo = agora;
		gv_acordar_em(0.32);
		gv_app_redesenhar(app);
	}
}

/* no começo de cada quadro: já é hora de reler a pasta? */
static void ino_rever(void) {
	if (!ino_pendente) {
		return;
	}
	double agora = gv_agora();
	if (agora - ino_ultimo >= 0.3 || agora - ino_primeiro >= 1.5) {
		ino_pendente = false;
		if (vista == V_PASTA) {
			carregar(true);
		}
	} else {
		gv_acordar_em(0.32 - (agora - ino_ultimo));
	}
}

/* ---- seleção ---- */

static int n_selecionados(void) {
	int n = 0;
	for (int i = 0; i < lista.n; i++) {
		n += sel[i];
	}
	return n;
}

static int primeiro_selecionado(void) {
	for (int i = 0; i < lista.n; i++) {
		if (sel[i]) {
			return i;
		}
	}
	return -1;
}

static void limpar_selecao(void) {
	memset(sel, 0, (size_t)(lista.n > 0 ? lista.n : 1));
}

static int pos_em_vis(int idx) {
	for (int k = 0; k < nvis; k++) {
		if (vis[k] == idx) {
			return k;
		}
	}
	return -1;
}

static void clicar_item(int idx, uint32_t mods) {
	if (mods & GV_MOD_SHIFT && ancora >= 0) {
		int a = pos_em_vis(ancora), b = pos_em_vis(idx);
		if (a >= 0 && b >= 0) {
			if (!(mods & GV_MOD_CTRL)) {
				limpar_selecao();
			}
			for (int k = a < b ? a : b; k <= (a < b ? b : a); k++) {
				sel[vis[k]] = true;
			}
		}
	} else if (mods & GV_MOD_CTRL) {
		sel[idx] = !sel[idx];
		ancora = idx;
	} else {
		limpar_selecao();
		sel[idx] = true;
		ancora = idx;
	}
	foco = idx;
}

/* ---- exibição: o tamanho do texto, o nome com ou sem extensão ---- */

static float escala_texto(void) {
	return cfg.texto == 1 ? 0.9f : cfg.texto == 2 ? 1.15f : 1.0f;
}

static struct { float corpo, legenda, destaque, titulo; bool guardado; } texto_original;

/* o tamanho do texto da pessoa vale por um quadro: o Gravity lê do gv_t a cada texto */
static void texto_aplicar(bool aplicar) {
	if (aplicar) {
		if (!texto_original.guardado) {
			texto_original.corpo = gv_t.tamanho_corpo;
			texto_original.legenda = gv_t.tamanho_legenda;
			texto_original.destaque = gv_t.tamanho_destaque;
			texto_original.titulo = gv_t.tamanho_titulo;
			texto_original.guardado = true;
		}
		float f = escala_texto();
		gv_t.tamanho_corpo = texto_original.corpo * f;
		gv_t.tamanho_legenda = texto_original.legenda * f;
		gv_t.tamanho_destaque = texto_original.destaque * f;
		gv_t.tamanho_titulo = texto_original.titulo * f;
	} else if (texto_original.guardado) {
		gv_t.tamanho_corpo = texto_original.corpo;
		gv_t.tamanho_legenda = texto_original.legenda;
		gv_t.tamanho_destaque = texto_original.destaque;
		gv_t.tamanho_titulo = texto_original.titulo;
	}
}

/* o nome como se mostra: sem a extensão, se a pessoa preferiu (o que se digita ao renomear é sempre o nome inteiro) */
/* o texto de um nome para mostrar: um nome que não é UTF-8 (pendrives antigos, zips) vira UTF-8 com o caractere de "inválido" no lugar dos bytes ruins
 * (o pango recusa o texto inválido e o nome sumia da tela); o arquivo em si continua com o nome verdadeiro */
static const char *utf8_para_mostrar(const char *s) {
	if (g_utf8_validate(s, -1, NULL)) {
		return s;
	}
	char *v = g_utf8_make_valid(s, -1);
	const char *r = gv_str(v).chars;
	g_free(v);
	return r;
}

static const char *nome_exibido(const ar_entrada *e) {
	if (cfg.extensoes || e->pasta) {
		return utf8_para_mostrar(e->nome);
	}
	const char *ponto = strrchr(e->nome, '.');
	if (!ponto || ponto == e->nome || strlen(ponto) > 9 || !ponto[1]) {
		return utf8_para_mostrar(e->nome);
	}
	return utf8_para_mostrar(gv_fmt("%.*s", (int)(ponto - e->nome), e->nome).chars);
}

static void salvar_cfg(void) {
	ar_cfg_gravar(&cfg);
}

static void mudar_vista(int v) {
	vista_atual = v;
	if (cfg.lembrar_por_pasta && vista == V_PASTA) {
		ar_visao_da_pasta_definir(pasta, v);
	} else {
		cfg.vista = v;
		salvar_cfg();
	}
	gv_rolar_para(ID_CONTEUDO, 0, 0, false);
}

/* ---- operações: copiar, recortar, colar, renomear, lixeira, desfazer ---- */

static bool em_lixeira(void) {
	if (vista != V_PASTA) {
		return false;
	}
	char *l = ar_lixeira_pasta();
	bool sim = !strcmp(pasta, l);
	g_free(l);
	return sim;
}

static void op_redesenhar(void *dados) {
	gv_app_redesenhar(app);
}

/* chamado da thread da operação: pede um quadro à linha principal */
static void op_acordou(void *dados) {
	gv_na_linha_principal(op_redesenhar, NULL);
}

static GPtrArray *caminhos_selecionados(void) {
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; i < lista.n; i++) {
		if (sel[i] && cam[i]) {
			g_ptr_array_add(v, g_strdup(cam[i]));
		}
	}
	return v;
}

static void selecionar_caminho(const char *caminho) {
	for (int i = 0; i < lista.n; i++) {
		if (cam[i] && !strcmp(cam[i], caminho)) {
			limpar_selecao();
			sel[i] = true;
			ancora = foco = i;
			return;
		}
	}
}

static void iniciar_op(enum ar_op_tipo tipo, GPtrArray *alvos, const char *destino) {
	if (op) {
		avisar("Espere a operação de agora terminar");
		return;
	}
	if (!alvos->len) {
		return;
	}
	op_tipo = tipo;
	memset(&op_st, 0, sizeof(op_st));
	op = ar_op_iniciar(tipo, (char *const *)alvos->pdata, (int)alvos->len, destino, op_acordou, NULL);
	if (!op) {
		avisar("Não consegui começar");
	}
	gv_app_redesenhar(app);
}

/* Em Configurações > Avançado > Registrar erros: uma linha por operação que deu erro, em ~/.local/state/lumna/atlas.log */
static void registrar_erro(const char *texto, int quantos) {
	if (!cfg.registrar_erros) {
		return;
	}
	char *dir = g_build_filename(g_get_user_state_dir(), "lumna", NULL);
	g_mkdir_with_parents(dir, 0700);
	char *arq = g_build_filename(dir, "atlas.log", NULL);
	FILE *f = fopen(arq, "a");
	if (f) {
		GDateTime *agora = g_date_time_new_now_local();
		char *quando = g_date_time_format(agora, "%Y-%m-%d %H:%M:%S");
		fprintf(f, "%s  %s (%d erro%s)\n", quando, texto ? texto : "erro", quantos, quantos == 1 ? "" : "s");
		g_free(quando);
		g_date_time_unref(agora);
		fclose(f);
	}
	g_free(arq);
	g_free(dir);
}

/* a cada quadro: a operação anda; quando termina, a tela se atualiza e diz o que houve */
static void op_rever(void) {
	if (!op) {
		return;
	}
	ar_op_estado_ler(op, &op_st);
	if (!op_st.terminou) {
		gv_acordar_em(0.25);
		return;
	}
	ar_op_liberar(op);
	op = NULL;
	const char *quantos = op_st.itens_total == 1 ? "1 item" : gv_fmt("%d itens", op_st.itens_total).chars;
	if (op_st.cancelado) {
		avisar("Cancelado");
	} else if (op_st.erros) {
		registrar_erro(op_st.erro, op_st.erros);
		avisar("%s%s", op_st.erro, op_st.erros > 1 ? gv_fmt(" (e mais %d erro%s)", op_st.erros - 1, op_st.erros > 2 ? "s" : "").chars : "");
	} else {
		switch (op_tipo) {
		case AR_OP_LIXEIRA: avisar("%s movido%s para a lixeira", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_COPIAR: avisar("%s copiado%s", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_MOVER: avisar("%s movido%s", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_APAGAR: avisar("%s apagado%s para sempre", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_COMPACTAR: avisar("Compactado"); break;
		case AR_OP_EXTRAIR: avisar("Extraído"); break;
		case AR_OP_RESTAURAR: avisar("%s restaurado%s", quantos, op_st.itens_total == 1 ? "" : "s"); break;
		case AR_OP_CLONAR: avisar("Repositório clonado"); break;
		}
	}
	if (!op_st.erros && !op_st.cancelado && ar_desfazer_ha(desfazer_texto, sizeof(desfazer_texto)) && op_tipo != AR_OP_APAGAR && op_tipo != AR_OP_RESTAURAR) {
		desfazer_ate = gv_agora() + 10;
		gv_acordar_em(10.2);
	}
	if (op_tipo == AR_OP_MOVER && op_cortada && !op_st.erros) {
		gv_copiar(" "); /* o que foi recortado já foi: não cola de novo */
		op_cortada = false;
	}
	bool tinha_cria = op_st.criado[0] != '\0';
	char criado[4096];
	snprintf(criado, sizeof(criado), "%s", op_st.criado);
	carregar(true);
	if (tinha_cria) {
		selecionar_caminho(criado);
	}
}

static char **area_atual(bool *recortar) {
	const char *t = gv_colar();
	return ar_area_ler(t, recortar);
}

static void copiar_selecao(bool recortar) {
	GPtrArray *v = caminhos_selecionados();
	if (!v->len) {
		g_ptr_array_free(v, TRUE);
		return;
	}
	char *texto = ar_area_gravar(recortar, (char *const *)v->pdata, (int)v->len);
	gv_copiar(texto);
	g_free(texto);
	op_cortada = recortar;
	avisar("%u %s %s", v->len, v->len == 1 ? "item" : "itens", recortar ? (v->len == 1 ? "recortado" : "recortados") : (v->len == 1 ? "copiado" : "copiados"));
	g_ptr_array_free(v, TRUE);
}

static bool pode_colar(void) {
	bool r;
	char **l = area_atual(&r);
	bool sim = l != NULL;
	g_strfreev(l);
	return sim;
}

static void colar_em(const char *destino) {
	bool recortar = false;
	char **l = area_atual(&recortar);
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
	op_cortada = recortar;
	iniciar_op(recortar ? AR_OP_MOVER : AR_OP_COPIAR, v, destino);
	g_ptr_array_free(v, TRUE);
}

static void colar(void) {
	if (vista != V_PASTA || em_lixeira()) {
		avisar("Abra uma pasta para colar");
		return;
	}
	colar_em(pasta);
}

static void apagar_selecao(bool permanente) {
	GPtrArray *v = caminhos_selecionados();
	if (!v->len) {
		g_ptr_array_free(v, TRUE);
		return;
	}
	if (permanente || em_lixeira()) {
		if (!cfg.confirmar_apagar) { /* a pessoa desligou a confirmação (Configurações > Avançado) */
			iniciar_op(AR_OP_APAGAR, v, NULL);
			g_ptr_array_free(v, TRUE);
			return;
		}
		if (apagar_alvos) {
			g_ptr_array_free(apagar_alvos, TRUE);
		}
		apagar_alvos = v;
		confirmar_apagar = true;
		return;
	}
	iniciar_op(AR_OP_LIXEIRA, v, NULL);
	g_ptr_array_free(v, TRUE);
}

static void restaurar_selecao(void) {
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; i < lista.n; i++) {
		if (sel[i]) {
			g_ptr_array_add(v, g_strdup(lista.v[i].nome));
		}
	}
	iniciar_op(AR_OP_RESTAURAR, v, NULL);
	g_ptr_array_free(v, TRUE);
}

static void desfazer_ultimo(void) {
	char *erro = NULL;
	if (op) {
		avisar("Espere a operação de agora terminar");
		return;
	}
	if (!ar_desfazer_ha(NULL, 0)) {
		avisar("Não há nada para desfazer");
		return;
	}
	if (ar_desfazer(&erro)) {
		avisar("Desfeito");
	} else {
		avisar("Não consegui desfazer: %s", erro ? erro : "?");
	}
	g_free(erro);
	desfazer_ate = 0;
	carregar(true);
}

static void iniciar_renomear(int idx) {
	if (idx < 0 || idx >= lista.n || !cam[idx] || em_lixeira()) {
		return;
	}
	renomeando = idx;
	snprintf(renome_nome, sizeof(renome_nome), "%s", lista.v[idx].nome);
	focar_renome = true;
	renome_quadros = 0;
}

static void confirmar_renome(void) {
	int idx = renomeando;
	renomeando = -1;
	if (idx < 0 || idx >= lista.n || !cam[idx] || !strcmp(renome_nome, lista.v[idx].nome)) {
		return;
	}
	char *novo = NULL, *erro = NULL;
	if (ar_renomear(cam[idx], renome_nome, &novo, &erro)) {
		desfazer_ate = gv_agora() + 10;
		snprintf(desfazer_texto, sizeof(desfazer_texto), "Renomear");
		avisar("Renomeado");
		carregar(true);
		selecionar_caminho(novo);
	} else {
		avisar("%s", erro ? erro : "Não consegui renomear");
	}
	g_free(novo);
	g_free(erro);
}

static void abrir_no_terminal(const char *pasta_alvo) {
	char *argv[] = { "lumna-pulse", "--pasta", (char *)pasta_alvo, NULL };
	GError *err = NULL;
	if (!g_spawn_async(pasta_alvo, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &err)) {
		avisar("Não consegui abrir o terminal: %s", err && err->message ? err->message : "?");
		g_clear_error(&err);
	}
}

static void abrir_nova_janela(const char *caminho) {
	char *argv[] = { "lumna-atlas", (char *)caminho, NULL };
	GError *err = NULL;
	if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &err)) {
		avisar("Não consegui abrir outra janela: %s", err && err->message ? err->message : "?");
		g_clear_error(&err);
	}
}

static void compactar_selecao(void) {
	GPtrArray *v = caminhos_selecionados();
	if (v->len && vista == V_PASTA && !em_lixeira()) {
		iniciar_op(AR_OP_COMPACTAR, v, pasta);
	} else if (v->len) {
		char *pai = g_path_get_dirname(v->pdata[0]);
		iniciar_op(AR_OP_COMPACTAR, v, pai);
		g_free(pai);
	}
	g_ptr_array_free(v, TRUE);
}

static void extrair_selecao(void) {
	GPtrArray *v = caminhos_selecionados();
	if (v->len) {
		char *pai = g_path_get_dirname(v->pdata[0]);
		iniciar_op(AR_OP_EXTRAIR, v, pai);
		g_free(pai);
	}
	g_ptr_array_free(v, TRUE);
}

/* os celulares conectados que aceitam arquivos (pergunta ao lumna-connect; sem ele, nenhum) */
static struct {
	char id[40], nome[48];
} celulares[8];
static int n_celulares;

static void celulares_ler(void) {
	n_celulares = 0;
	char *buf = g_malloc(64 * 1024);
	if (connectc_pedido("connect", buf, 64 * 1024)) {
		struct json *j = json_ler(buf);
		const struct json *ds = json_chave(j, "dispositivos");
		bool ativo = json_bool(json_chave(j, "ativo"), true);
		for (int i = 0; ativo && ds && i < ds->nfilhos && n_celulares < 8; i++) {
			const struct json *d = json_item(ds, i);
			if (json_bool(json_chave(d, "conectado"), false) && json_bool(json_chave(json_chave(d, "recursos"), "files"), true)) {
				g_strlcpy(celulares[n_celulares].id, json_chave_texto(d, "id"), sizeof(celulares[0].id));
				g_strlcpy(celulares[n_celulares].nome, json_chave_texto(d, "nome"), sizeof(celulares[0].nome));
				n_celulares++;
			}
		}
		json_liberar(j);
	}
	g_free(buf);
}

static void enviar_ao_celular(int c) {
	int enviados = 0;
	char *erro = NULL;
	for (int i = 0; i < lista.n; i++) {
		if (!sel[i] || lista.v[i].pasta || !cam[i]) {
			continue;
		}
		char *pedido = g_strdup_printf("enviar %s %s", celulares[c].id, cam[i]), r[1024] = "";
		if (!connectc_pedido(pedido, r, sizeof(r))) {
			erro = g_strdup("O serviço do celular não respondeu");
		} else {
			struct json *j = json_ler(r);
			if (json_bool(json_chave(j, "ok"), false)) {
				enviados++;
			} else if (!erro) {
				erro = g_strdup(*json_chave_texto(j, "erro") ? json_chave_texto(j, "erro") : "Não deu para enviar");
			}
			json_liberar(j);
		}
		g_free(pedido);
	}
	if (erro) {
		avisar("%s", erro);
	} else {
		avisar("Oferecido a %s: ele puxa %s (veja o progresso no Painel > LUMNA Connect)", celulares[c].nome, enviados == 1 ? "o arquivo" : "os arquivos");
	}
	g_free(erro);
}

/* ---- desenho dos ícones (feitos à mão, sem arquivo de imagem) ---- */

typedef struct ic_dados {
	enum ar_tipo tipo;
	char selo[8];
	bool link;
	uint32_t cor; /* a cor da pasta (0: a do tema) */
	enum ar_tipo previa[4]; /* o que há dentro da pasta, em miniatura */
	int nprevia;
} ic_dados;
static ic_dados ic_pool[1536];
static int ic_n;

static void cor_rgb(cairo_t *cr, uint32_t rgb, double a) {
	cairo_set_source_rgba(cr, ((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0, a);
}

static void rr(cairo_t *cr, double x, double y, double w, double h, double r) {
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

static uint32_t cor_do_tipo(enum ar_tipo t) {
	switch (t) {
	case AR_PDF: return 0xE5484D;
	case AR_IMAGEM: return 0x00A2C7;
	case AR_AUDIO: return 0xA78BFA;
	case AR_VIDEO: return 0xD6409F;
	case AR_MARKDOWN: return 0x3E63DD;
	case AR_PLANILHA: return 0x30A46C;
	case AR_APRESENTACAO: return 0xF76B15;
	case AR_DOCUMENTO: return 0x3E8EF7;
	case AR_COMPACTADO: return 0xE5A100;
	case AR_CODIGO: return 0x12A594;
	default: return 0x8B8D98;
	}
}

static uint32_t cor_do_tipo(enum ar_tipo t);

static void desenhar_pasta(cairo_t *cr, double x, double y, double w, double h, uint32_t rgb, const enum ar_tipo *previa, int nprevia) {
	double aba = h * 0.16;
	cairo_pattern_t *g = cairo_pattern_create_linear(0, y, 0, y + h);
	Clay_Color a = gv_t.acento;
	if (rgb) {
		a = (Clay_Color){ (float)((rgb >> 16) & 255), (float)((rgb >> 8) & 255), (float)(rgb & 255), 255 };
	}
	cairo_pattern_add_color_stop_rgb(g, 0, fmin(1, a.r / 255.0 + 0.30), fmin(1, a.g / 255.0 + 0.24), fmin(1, a.b / 255.0 + 0.26));
	cairo_pattern_add_color_stop_rgb(g, 1, fmin(1, a.r / 255.0 + 0.10), fmin(1, a.g / 255.0 + 0.06), fmin(1, a.b / 255.0 + 0.10));
	/* a aba de trás, mais escura */
	rr(cr, x, y + h * 0.04, w * 0.46, aba * 2.2, h * 0.07);
	cairo_set_source_rgb(cr, fmin(1, a.r / 255.0 + 0.16), fmin(1, a.g / 255.0 + 0.10), fmin(1, a.b / 255.0 + 0.14));
	cairo_fill(cr);
	rr(cr, x, y + aba, w, h - aba, h * 0.10);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);
	/* as miniaturas do que há dentro: quatro páginas pequenas dispostas 2 por 2 no corpo da pasta */
	double mw = w * 0.30, mh = (h - aba) * 0.34;
	for (int i = 0; i < nprevia && i < 4; i++) {
		double mx = x + w * 0.14 + (i % 2) * (mw + w * 0.12), my = y + aba + (h - aba) * 0.16 + (i / 2) * (mh + (h - aba) * 0.08);
		rr(cr, mx, my, mw, mh, mw * 0.14);
		cairo_set_source_rgba(cr, 0.96, 0.96, 0.98, 0.95);
		cairo_fill(cr);
		rr(cr, mx + mw * 0.2, my + mh * 0.28, mw * 0.6, mh * 0.34, mw * 0.08);
		cor_rgb(cr, cor_do_tipo(previa[i]), 1);
		cairo_fill(cr);
	}
}

static void desenhar_pagina(cairo_t *cr, double x, double y, double w, double h, const ic_dados *d) {
	double pw = w * 0.74, ph = h, px = x + (w - pw) / 2, dobra = pw * 0.28;
	cairo_new_path(cr);
	cairo_move_to(cr, px + h * 0.06, y);
	cairo_line_to(cr, px + pw - dobra, y);
	cairo_line_to(cr, px + pw, y + dobra);
	cairo_line_to(cr, px + pw, y + ph - h * 0.06);
	cairo_curve_to(cr, px + pw, y + ph, px + pw, y + ph, px + pw - h * 0.06, y + ph);
	cairo_line_to(cr, px + h * 0.06, y + ph);
	cairo_curve_to(cr, px, y + ph, px, y + ph, px, y + ph - h * 0.06);
	cairo_line_to(cr, px, y + h * 0.06);
	cairo_curve_to(cr, px, y, px, y, px + h * 0.06, y);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.95, 0.95, 0.97);
	cairo_fill(cr);
	/* a dobra */
	cairo_move_to(cr, px + pw - dobra, y);
	cairo_line_to(cr, px + pw - dobra, y + dobra);
	cairo_line_to(cr, px + pw, y + dobra);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.82, 0.82, 0.88);
	cairo_fill(cr);
	/* o selo colorido do tipo */
	double sw = pw * 0.60, sh = sw * 0.62, sx = px + (pw - sw) / 2, sy = y + ph * 0.42;
	rr(cr, sx, sy, sw, sh, sh * 0.22);
	cor_rgb(cr, cor_do_tipo(d->tipo), 1);
	cairo_fill(cr);
	if (d->selo[0]) {
		PangoLayout *l = pango_cairo_create_layout(cr);
		char fonte[96];
		snprintf(fonte, sizeof(fonte), "%s Bold %.1f", gv_t.fonte, sh * (strlen(d->selo) > 3 ? 0.30 : 0.36) * 0.75);
		PangoFontDescription *fd = pango_font_description_from_string(fonte);
		pango_layout_set_font_description(l, fd);
		pango_font_description_free(fd);
		pango_layout_set_text(l, d->selo, -1);
		int tw, th;
		pango_layout_get_pixel_size(l, &tw, &th);
		cairo_move_to(cr, sx + (sw - tw) / 2.0, sy + (sh - th) / 2.0);
		cairo_set_source_rgb(cr, 1, 1, 1);
		pango_cairo_show_layout(cr, l);
		g_object_unref(l);
	}
}

static void desenhar_icone(cairo_t *cr, Clay_BoundingBox c, void *dados) {
	const ic_dados *d = dados;
	double s = fmin(c.width, c.height);
	double x = c.x + (c.width - s) / 2, y = c.y + (c.height - s) / 2;
	if (d->tipo == AR_PASTA) {
		desenhar_pasta(cr, x + s * 0.04, y + s * 0.10, s * 0.92, s * 0.80, d->cor, d->previa, d->nprevia);
	} else {
		desenhar_pagina(cr, x + s * 0.04, y + s * 0.04, s * 0.92, s * 0.92, d);
	}
	if (d->link) { /* uma setinha no canto: é um atalho */
		cairo_arc(cr, x + s * 0.82, y + s * 0.82, s * 0.12, 0, 2 * M_PI);
		cairo_set_source_rgb(cr, 1, 1, 1);
		cairo_fill(cr);
		cairo_move_to(cr, x + s * 0.76, y + s * 0.88);
		cairo_line_to(cr, x + s * 0.88, y + s * 0.76);
		cairo_move_to(cr, x + s * 0.78, y + s * 0.76);
		cairo_line_to(cr, x + s * 0.88, y + s * 0.76);
		cairo_line_to(cr, x + s * 0.88, y + s * 0.86);
		cairo_set_source_rgb(cr, 0.2, 0.2, 0.25);
		cairo_set_line_width(cr, s * 0.035);
		cairo_stroke(cr);
	}
}

/* O que há dentro de cada pasta à vista (as miniaturas coloridas). Duas regras que faltavam:
 *  - nunca na linha principal: abrir uma pasta com dezenas de subpastas lia e conferia centenas de arquivos de CADA uma antes do primeiro quadro (num pendrive
 *    ou HD lento, o app ficava congelado por segundos); agora uma thread lê, a pasta aparece já e as miniaturas entram quando chegam;
 *  - guardado pela data da pasta, não só "até recarregar": uma recarga da mesma pasta (o inotify avisando) não joga fora nem recalcula o que não mudou, e nada
 *    pisca. */
typedef struct {
	int n;
	enum ar_tipo t[4];
	bool tem;           /* já se leu alguma vez (n e t valem) */
	time_t mtime;       /* a data da pasta quando se leu */
	bool pedida;        /* uma thread está lendo... */
	time_t mtime_pedida; /* ...para esta data da pasta */
} previa_t;
typedef struct {
	char *caminho;
	time_t mtime;
	int n;
	enum ar_tipo t[4];
} previa_pedido;

static GHashTable *previas_cache;
static GThreadPool *previas_fila;
static int previas_pendentes;
#define PREVIAS_MAX_PENDENTES 48

void previas_limpar(void);
void previas_limpar(void) {
	if (previas_cache) {
		g_hash_table_remove_all(previas_cache);
	}
}

static void previa_chegou(void *dados) {
	previa_pedido *p = dados;
	previas_pendentes--;
	previa_t *c = previas_cache ? g_hash_table_lookup(previas_cache, p->caminho) : NULL;
	if (c) { /* (a pasta pode ter saído do cache enquanto a thread lia: então o resultado se perde, sem problema) */
		c->n = p->n;
		memcpy(c->t, p->t, sizeof(c->t));
		c->tem = true;
		c->mtime = p->mtime;
		if (c->pedida && c->mtime_pedida == p->mtime) {
			c->pedida = false;
		}
		gv_app_redesenhar(app);
	}
	g_free(p->caminho);
	g_free(p);
}

static void previa_ler(gpointer dados, gpointer usuario) {
	previa_pedido *p = dados;
	p->n = ar_previa_pasta(p->caminho, p->t, 4);
	gv_na_linha_principal(previa_chegou, p);
}

/* quantas miniaturas já se sabe desta pasta (0 enquanto a thread não leu); se a pasta mudou, as de antes ficam até as novas chegarem: nada pisca */
static int previa_da_pasta(const char *caminho, time_t mtime, enum ar_tipo *saida) {
	if (!previas_cache) {
		previas_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		previas_fila = g_thread_pool_new(previa_ler, NULL, 1, FALSE, NULL);
	}
	previa_t *c = g_hash_table_lookup(previas_cache, caminho);
	if (!c) {
		if (g_hash_table_size(previas_cache) > 4000) { /* uma pasta de milhares de pastas: não guarda para sempre */
			g_hash_table_remove_all(previas_cache);
		}
		c = g_new0(previa_t, 1);
		g_hash_table_insert(previas_cache, g_strdup(caminho), c);
	}
	bool atual = c->tem && c->mtime == mtime;
	bool a_caminho = c->pedida && c->mtime_pedida == mtime;
	if (!atual && !a_caminho) {
		if (previas_pendentes >= PREVIAS_MAX_PENDENTES) {
			gv_acordar_em(0.25); /* fila cheia (rolagem rápida): tenta de novo logo, com o que estiver à vista então */
		} else {
			previa_pedido *p = g_new0(previa_pedido, 1);
			p->caminho = g_strdup(caminho);
			p->mtime = mtime;
			c->pedida = true;
			c->mtime_pedida = mtime;
			previas_pendentes++;
			g_thread_pool_push(previas_fila, p, NULL);
		}
	}
	if (!c->tem) {
		return 0;
	}
	memcpy(saida, c->t, sizeof(c->t));
	return c->n;
}

/* O ícone de uma entrada: uma imagem mostra a miniatura (o Gravity descodifica numa thread); o resto é desenhado. */
static void icone(Clay_ElementId id, int idx, float tam) {
	const ar_entrada *e = &lista.v[idx];
	if (e->tipo == AR_IMAGEM && cam[idx] && e->tamanho < 40 * 1024 * 1024) {
		float prop = gv_imagem_proporcao(cam[idx]);
		float w = tam, h = tam;
		if (prop > 0) {
			if (prop >= 1) {
				h = tam / prop;
			} else {
				w = tam * prop;
			}
		}
		CLAY(CLAY_ID_LOCAL("mini"), { .layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(id, gv_imagem(id, cam[idx], GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
				.layout = { .sizing = { CLAY_SIZING_FIXED(w * 0.92), CLAY_SIZING_FIXED(h * 0.92) } },
				.cornerRadius = CLAY_CORNER_RADIUS(6), .backgroundColor = gv_t.elevada })) {}
		}
		return;
	}
	ic_dados *d = &ic_pool[ic_n++ % (int)(sizeof(ic_pool) / sizeof(ic_pool[0]))];
	d->tipo = e->tipo;
	d->link = e->link;
	d->cor = 0;
	d->nprevia = 0;
	char emblema[32] = "";
	if (e->pasta && cam[idx]) {
		ar_pasta_estilo(cam[idx], &d->cor, emblema, sizeof(emblema));
		if (cfg.previas && !emblema[0] && tam >= 60) { /* com emblema, o emblema é o desenho; sem ícone pequeno demais, a prévia */
			d->nprevia = previa_da_pasta(cam[idx], e->mtime, d->previa);
		}
	}
	ar_selo(e, d->selo, sizeof(d->selo));
	CLAY(id, gv_livre(desenhar_icone, d, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) } } })) {
		if (emblema[0]) {
			CLAY_AUTO_ID({ .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
				.offset = { 0, tam * 0.08f }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
				gv_icone(emblema, tam * 0.34f, (Clay_Color){ 255, 255, 255, 235 });
			}
		}
	}
}

/* ---- barra de ferramentas ---- */

static void botao_barra(Clay_ElementId id, const char *ic, bool ativo, bool desativado, bool *clicado) {
	gv_botao_opcoes o = { .icone = ic, .estilo = ativo ? GV_BOTAO_ELEVADO : GV_BOTAO_SIMPLES, .desativado = desativado, .altura = 40 };
	*clicado = gv_botao_ext(id, &o);
}

static const char *nome_da_vista(void) {
	if (vista == V_RECENTES) {
		return "Recentes";
	}
	if (vista == V_FAVORITOS) {
		return "Favoritos";
	}
	if (vista == V_INICIO) {
		return "Início";
	}
	if (vista == V_REDE) {
		return "Compartilhadas";
	}
	if (vista == V_NUVEM) {
		return "Nuvem";
	}
	if (em_lixeira()) {
		return "Lixeira";
	}
	ar_parte p[32];
	int n = ar_partes(pasta, p, 32);
	const char *r = gv_str(p[n - 1].rotulo).chars;
	ar_partes_liberar(p, n);
	return r;
}

static void editar_caminho_comecar(void) {
	if (vista != V_PASTA) {
		snprintf(caminho_txt, sizeof(caminho_txt), "%s/", g_get_home_dir());
	} else {
		snprintf(caminho_txt, sizeof(caminho_txt), "%.4000s%s", pasta, strcmp(pasta, "/") ? "/" : "");
	}
	editando_caminho = true;
	focar_caminho = true;
	caminho_quadros = 0;
	ar_sugestoes_liberar(sug, n_sug);
	n_sug = ar_sugestoes(caminho_txt, sug, 12);
	sug_sel = -1;
}

static void editar_caminho_acabar(void) {
	editando_caminho = false;
	ar_sugestoes_liberar(sug, n_sug);
	n_sug = 0;
	sug_sel = -1;
}

static void editar_caminho_ir(void) {
	char *alvo = NULL;
	if (sug_sel >= 0 && sug_sel < n_sug) {
		alvo = g_strdup(sug[sug_sel].caminho);
	} else {
		alvo = ar_caminho_digitado(caminho_txt, vista == V_PASTA ? pasta : g_get_home_dir());
	}
	if (alvo) {
		editar_caminho_acabar();
		ir_para(alvo, true);
	} else {
		avisar("Esse caminho não existe: %s", caminho_txt);
	}
	g_free(alvo);
}

/* Tab: completa com a sugestão escolhida (ou a primeira) e já entra na pasta, para seguir digitando */
static void editar_caminho_completar(void) {
	int i = sug_sel >= 0 ? sug_sel : 0;
	if (i >= n_sug) {
		return;
	}
	snprintf(caminho_txt, sizeof(caminho_txt), "%s/", sug[i].caminho);
	ar_sugestoes_liberar(sug, n_sug);
	n_sug = ar_sugestoes(caminho_txt, sug, 12);
	sug_sel = -1;
	focar_caminho = true;
}

static void caminho_barra(void) {
	if (editando_caminho) {
		CLAY(CLAY_ID("caminho"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 10, 4, 0, 0 }, .childGap = 6,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_icone("folder", 20, gv_t.texto_secundario);
			CLAY(CLAY_ID("caminho-campo-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				int r = gv_campo(CLAY_ID("caminho-campo"), caminho_txt, sizeof(caminho_txt), "Digite um caminho", GV_CAMPO_TEXTO);
				if (focar_caminho) {
					gv_focar(CLAY_ID("caminho-campo"));
					focar_caminho = false;
				}
				if (r & GV_CAMPO_MUDOU) {
					ar_sugestoes_liberar(sug, n_sug);
					n_sug = ar_sugestoes(caminho_txt, sug, 12);
					sug_sel = -1;
				}
				if (r & GV_CAMPO_ENTER) {
					editar_caminho_ir();
				}
			}
			if (gv_botao_redondo(CLAY_ID("caminho-ir"), "arrow-right", 30, true, false)) {
				editar_caminho_ir();
			}
		}
		/* sem foco por uns quadros depois de abrir: o campo ainda está pegando; depois, perder o foco cancela */
		if (++caminho_quadros > 4 && !gv_focado(CLAY_ID("caminho-campo")) && !gv_premido(CLAY_ID("caminho-ir"))) {
			editar_caminho_acabar();
		}
		return;
	}
	bool vazio = gv_tocado(CLAY_ID("caminho")); /* antes das partes: elas ganham o aperto */
	CLAY(CLAY_ID("caminho"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 14, 14, 0, 0 }, .childGap = 6,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .clip = { .horizontal = true } }) {
		if (vista != V_PASTA || em_lixeira()) {
			gv_icone(vista == V_RECENTES ? "clock" : vista == V_FAVORITOS ? "star" : vista == V_INICIO ? "home" : vista == V_REDE ? "network" : vista == V_NUVEM ? "cloud" : "trash", 20, gv_t.texto_secundario);
			gv_texto(gv_str(nome_da_vista()), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		} else {
			ar_parte p[32];
			int n = ar_partes(pasta, p, 32);
			gv_icone("folder", 20, gv_t.texto_secundario);
			int ini = n > 4 ? n - 4 : 0; /* um caminho comprido mostra só o fim */
			if (ini) {
				gv_texto(CLAY_STRING("…"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
				gv_texto(CLAY_STRING("/"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			for (int i = ini; i < n; i++) {
				Clay_ElementId id = CLAY_IDI("parte", (uint32_t)i);
				bool ultimo = i == n - 1;
				CLAY(id, { .layout = { .padding = { 6, 6, 3, 3 } }, .cornerRadius = CLAY_CORNER_RADIUS(6),
					.backgroundColor = gv_por_cima(id) && !ultimo ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 } }) {
					gv_texto(gv_str(p[i].rotulo), gv_t.tamanho_corpo, ultimo ? gv_t.texto : gv_t.texto_secundario, ultimo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
				}
				if (gv_tocado(id) && !ultimo) {
					ir_para(p[i].caminho, true);
					vazio = false;
				}
				if (!ultimo) {
					gv_texto(CLAY_STRING("/"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			ar_partes_liberar(p, n);
		}
	}
	if (vazio && !editando_caminho) {
		editar_caminho_comecar(); /* clicar no vazio do caminho (ou Ctrl+L) é digitar o caminho */
	}
}

static const char *const NOMES_ORDEM[] = { "Nome", "Data de modificação", "Tamanho", "Tipo" };

static void criar_novo(void) {
	if (!novo_nome[0] || vista != V_PASTA) {
		return;
	}
	char *alvo = ar_unir(pasta, novo_nome);
	for (int k = 2; g_file_test(alvo, G_FILE_TEST_EXISTS) && k < 1000; k++) {
		g_free(alvo);
		char *n2 = g_strdup_printf("%s (%d)", novo_nome, k);
		alvo = ar_unir(pasta, n2);
		g_free(n2);
	}
	bool ok;
	if (novo_tipo == 1) {
		ok = g_mkdir(alvo, 0755) == 0;
	} else {
		FILE *f = fopen(alvo, "wx");
		ok = f != NULL;
		if (f) {
			fclose(f);
		}
	}
	if (!ok) {
		avisar("Não consegui criar: %s", g_strerror(errno));
	} else {
		carregar(false);
		for (int i = 0; i < lista.n; i++) {
			if (!strcmp(cam[i], alvo)) {
				limpar_selecao();
				sel[i] = true;
				foco = ancora = i;
			}
		}
	}
	g_free(alvo);
	novo_tipo = 0;
	novo_nome[0] = '\0';
	gv_popup_fechar(CLAY_ID("menu-novo"));
}

/* o nome de um lugar, para o título de uma aba */
static const char *rotulo_do_destino(const char *d) {
	if (!d) {
		return "";
	}
	if (!strcmp(d, ESPECIAL_INICIO)) {
		return "Início";
	}
	if (!strcmp(d, ESPECIAL_RECENTES)) {
		return "Recentes";
	}
	if (!strcmp(d, ESPECIAL_FAVORITOS)) {
		return "Favoritos";
	}
	if (!strcmp(d, ESPECIAL_REDE)) {
		return "Compartilhadas";
	}
	if (!strcmp(d, ESPECIAL_NUVEM)) {
		return "Nuvem";
	}
	char *lix = ar_lixeira_pasta();
	bool eh_lixeira = !strcmp(d, lix);
	g_free(lix);
	if (eh_lixeira) {
		return "Lixeira";
	}
	ar_parte p[32];
	int n = ar_partes(d, p, 32);
	const char *r = n > 0 ? gv_str(p[n - 1].rotulo).chars : d;
	ar_partes_liberar(p, n);
	return r;
}

/* a faixa das abas: só aparece com duas ou mais. Cada aba tem o ícone do lugar, o nome e o X; o + abre outra */
static void faixa_de_abas(void) {
	if (n_abas < 2) {
		return;
	}
	CLAY(CLAY_ID("abas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 12, 12, 0, 0 }, .childGap = 6, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		int fechar = -1, mostrar = -1;
		for (int i = 0; i < n_abas; i++) {
			bool ativa = i == aba_atual;
			const char *dest = ar_hist_atual(i == aba_atual ? &hist : &abas[i].hist);
			const char *ic = !dest ? "folder" : !strcmp(dest, ESPECIAL_INICIO) ? "home" : !strcmp(dest, ESPECIAL_RECENTES) ? "clock" : !strcmp(dest, ESPECIAL_FAVORITOS) ? "star" : !strcmp(dest, ESPECIAL_REDE) ? "network" : !strcmp(dest, ESPECIAL_NUVEM) ? "cloud" : "folder";
			char nome[96];
			snprintf(nome, sizeof(nome), "%s", rotulo_do_destino(dest));
			if (g_utf8_strlen(nome, -1) > 22) { /* nome comprido: corta com reticências */
				*g_utf8_offset_to_pointer(nome, 21) = '\0';
				g_strlcat(nome, "…", sizeof(nome));
			}
			Clay_ElementId id = CLAY_IDI("aba", (uint32_t)i);
			bool tocou = gv_tocado(id); /* antes do X: o X, chamado depois, ganha o toque que é dele */
			bool sobre = gv_por_cima(id);
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0, 230), CLAY_SIZING_FIXED(36) }, .padding = { 12, 4, 0, 0 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = ativa ? gv_t.selecionado : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
				gv_icone(ic, 18, ativa ? gv_t.texto : gv_t.texto_secundario);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(gv_str(nome), gv_t.tamanho_corpo - 1, ativa ? gv_t.texto : gv_t.texto_secundario, ativa ? GV_PESO_MEDIO : GV_PESO_NORMAL);
				}
				if (gv_botao_ext(CLAY_IDI("aba-x", (uint32_t)i), &(gv_botao_opcoes){ .icone = "close", .estilo = GV_BOTAO_SIMPLES, .altura = 28 })) {
					fechar = i;
				}
			}
			if (tocou && fechar < 0) {
				mostrar = i;
			}
		}
		bool c;
		botao_barra(CLAY_ID("aba-nova"), "plus", false, n_abas >= MAX_ABAS, &c);
		if (c) {
			aba_nova(NULL);
		}
		if (fechar >= 0) {
			aba_fechar(fechar);
		} else if (mostrar >= 0) {
			aba_mostrar(mostrar);
		}
	}
}

static void barra_ferramentas(void) {
	CLAY(CLAY_ID("ferramentas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 16, 16, 0, 0 }, .childGap = 8,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		bool c;
		botao_barra(CLAY_ID("t-voltar"), "arrow-left", false, !ar_hist_pode_voltar(&hist), &c);
		if (c) {
			voltar();
		}
		botao_barra(CLAY_ID("t-avancar"), "arrow-right", false, !ar_hist_pode_avancar(&hist), &c);
		if (c) {
			avancar();
		}
		char *pai = vista == V_PASTA ? ar_pai(pasta) : NULL;
		botao_barra(CLAY_ID("t-subir"), "arrow-up", false, !pai, &c);
		g_free(pai);
		if (c) {
			subir();
		}
		CLAY(CLAY_ID("t-div"), { .layout = { .sizing = { CLAY_SIZING_FIXED(1), CLAY_SIZING_FIXED(28) } }, .backgroundColor = gv_t.borda }) {}
		caminho_barra();
		CLAY(CLAY_ID("t-busca"), { .layout = { .sizing = { CLAY_SIZING_FIXED(280) } } }) {
			if (gv_campo(CLAY_ID("busca"), busca, sizeof(busca), gv_fmt("Buscar em %s", nome_da_vista()).chars, GV_CAMPO_BUSCA) & GV_CAMPO_MUDOU) {
				refiltrar();
			}
		}
		botao_barra(CLAY_ID("t-novo"), "plus", false, vista != V_PASTA, &c);
		if (c) {
			novo_tipo = 0;
			gv_popup_alternar(CLAY_ID("menu-novo"));
		}
		botao_barra(CLAY_ID("t-ordem"), "sliders", false, false, &c);
		if (c) {
			gv_popup_alternar(CLAY_ID("menu-ordem"));
		}
		botao_barra(CLAY_ID("t-grade"), "grid", !modo_lista, false, &c);
		if (c) {
			mudar_vista(AR_VISTA_GRADE);
		}
		botao_barra(CLAY_ID("t-lista"), "list", modo_lista, false, &c);
		if (c) {
			mudar_vista(vista_atual == AR_VISTA_LISTA_GRANDE ? AR_VISTA_LISTA_GRANDE : AR_VISTA_LISTA);
		}
		botao_barra(CLAY_ID("t-ajustes"), "settings", false, false, &c);
		if (c) {
			abrir_configuracoes(0);
		}
	}

	GV_POPUP(CLAY_ID("menu-novo"), CLAY_ID("t-novo"), .alinhar = GV_ALINHAR_FIM, .largura = 260) {
		CLAY(CLAY_ID("mn-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(8), .childGap = 4 } }) {
			if (!novo_tipo) {
				if (gv_menu_item(CLAY_ID("mn-pasta"), "folder-plus", "Nova pasta", NULL)) {
					novo_tipo = 1;
					focar_nome = true;
					snprintf(novo_nome, sizeof(novo_nome), "Nova pasta");
				}
				if (gv_menu_item(CLAY_ID("mn-arq"), "file", "Novo arquivo de texto", NULL)) {
					novo_tipo = 2;
					focar_nome = true;
					snprintf(novo_nome, sizeof(novo_nome), "Novo arquivo.txt");
				}
			} else {
				gv_texto(novo_tipo == 1 ? CLAY_STRING("Nome da pasta") : CLAY_STRING("Nome do arquivo"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
				int r = gv_campo(CLAY_ID("mn-nome"), novo_nome, sizeof(novo_nome), "Nome", GV_CAMPO_TEXTO);
				if (focar_nome) {
					gv_focar(CLAY_ID("mn-nome"));
					focar_nome = false;
				}
				if (r & GV_CAMPO_ENTER) {
					criar_novo();
				} else if (gv_botao(CLAY_ID("mn-criar"), NULL, "Criar", GV_BOTAO_CHEIO)) {
					criar_novo();
				}
			}
		}
	}
	GV_POPUP(CLAY_ID("menu-ordem"), CLAY_ID("t-ordem"), .alinhar = GV_ALINHAR_FIM, .largura = 260) {
		CLAY(CLAY_ID("mo-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(8), .childGap = 2 } }) {
			gv_texto(CLAY_STRING("Ordenar por"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			for (int o = 0; o < 4; o++) {
				if (gv_menu_item(CLAY_IDI("mo-o", (uint32_t)o), cfg.ordem == (int)o ? "check" : NULL, NOMES_ORDEM[o], NULL)) {
					cfg.ordem = (enum ar_ordem)o;
					carregar(true);
					gv_popup_fechar(CLAY_ID("menu-ordem"));
				}
			}
			CLAY(CLAY_ID("mo-div"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			if (gv_menu_item(CLAY_ID("mo-asc"), !cfg.decrescente ? "check" : NULL, "Crescente", NULL)) {
				cfg.decrescente = false;
				carregar(true);
				gv_popup_fechar(CLAY_ID("menu-ordem"));
			}
			if (gv_menu_item(CLAY_ID("mo-desc"), cfg.decrescente ? "check" : NULL, "Decrescente", NULL)) {
				cfg.decrescente = true;
				carregar(true);
				gv_popup_fechar(CLAY_ID("menu-ordem"));
			}
		}
	}
}

/* ---- barra lateral ---- */

/* ---- arrastar para reordenar a barra lateral ----
 * Com o mouse: apertar num item e andar uns pixels. Com o dedo: segurar meio segundo e então andar (andar logo rola a lista). Uma linha mostra onde o item
 * cai (inclusive em outra seção) e um fantasma acompanha o ponteiro; soltar grava, Esc cancela. */
static int arr_item = -1;       /* índice na barra do item arrastado (-1: nenhum) */
static int arr_cand = -1;       /* apertado, ainda sem andar o bastante */
static float arr_x0, arr_y0;
static int arr_secao, arr_antes; /* onde cairia: a seção e antes de qual índice (-1: no fim dela) */
static bool arr_suprimir;       /* este quadro o arraste acabou: o soltar não é um clique */
static int lat_menu_idx = -1;   /* o item da barra lateral do menu de contexto (índice na barra; -1: um fixo, como a Lixeira) */
static char lat_menu_destino[4096];

static void item_lateral(Clay_ElementId id, int idx, const char *ic, const char *rotulo, bool ativo, const char *destino) {
	bool arrastado = idx >= 0 && arr_item == idx;
	if (idx >= 0 ? gv_menu_no_ponteiro_mouse(id, CLAY_ID("menu-lateral")) : gv_menu_no_ponteiro(id, CLAY_ID("menu-lateral"))) { /* os fixos (Lixeira, Rede) não arrastam: o dedo parado abre o menu */ /* o botão direito; o dedo parado é do arraste (soltar sem andar abre este mesmo menu) */
		lat_menu_idx = idx;
		snprintf(lat_menu_destino, sizeof(lat_menu_destino), "%s", destino);
	}
	bool sobre = gv_por_cima(id) && arr_item < 0;
	if (idx >= 0 && arr_item < 0) {
		if (gv_premido(id)) {
			Clay_Vector2 p = gv_ponteiro();
			if (arr_cand != idx) {
				arr_cand = idx;
				arr_x0 = p.x;
				arr_y0 = p.y;
			} else if (!gv_ponteiro_e_toque() && (fabsf(p.x - arr_x0) > 6 || fabsf(p.y - arr_y0) > 6)) {
				arr_item = idx;
				arr_cand = -1;
				gv_rolagem_travar(); /* a lista não rola com o arraste (vale já no quadro seguinte, o do primeiro movimento) */
			}
		}
		if (gv_ponteiro_e_toque() && gv_segurou(id)) { /* o mouse não precisa segurar: anda e pronto (segurar com o mouse continua sendo um clique devagar) */
			Clay_Vector2 p0 = gv_ponteiro();
			arr_x0 = p0.x;
			arr_y0 = p0.y;
			arr_item = idx;
			arr_cand = -1;
			/* sem isto a trava só era pedida no quadro seguinte e valia dois quadros depois: com a tela parada (sem quadros à toa) o primeiro movimento do dedo ainda rolava a lista */
			gv_rolagem_travar();
		}
	}
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38) }, .padding = { 12, 10, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = arrastado ? gv_t.fundo : ativo ? gv_t.selecionado : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		gv_icone(ic, 20, arrastado ? gv_t.texto_suave : ativo ? gv_t.texto : gv_t.texto_secundario);
		gv_texto(gv_str(rotulo), gv_t.tamanho_corpo - 1, arrastado ? gv_t.texto_suave : ativo ? gv_t.texto : gv_t.texto_secundario, ativo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
	}
	if (gv_tocado(id) && !arr_suprimir && arr_item < 0) {
		ir_para(destino, true);
	}
}

static void titulo_lateral(const char *t) {
	CLAY_AUTO_ID({ .layout = { .padding = { 12, 8, 14, 6 } } }) {
		gv_texto(gv_str(t), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
	}
}

static const char *destino_do_item(const ar_barra_item *it) {
	if (!strcmp(it->id, "inicio")) {
		return ESPECIAL_INICIO;
	}
	if (!strcmp(it->id, "recentes")) {
		return ESPECIAL_RECENTES;
	}
	if (!strcmp(it->id, "favoritos")) {
		return ESPECIAL_FAVORITOS;
	}
	return it->caminho;
}

static bool item_da_barra_ativo(const ar_barra_item *it) {
	if (!strcmp(it->id, "inicio")) {
		return vista == V_INICIO;
	}
	if (!strcmp(it->id, "recentes")) {
		return vista == V_RECENTES;
	}
	if (!strcmp(it->id, "favoritos")) {
		return vista == V_FAVORITOS;
	}
	return vista == V_PASTA && !strcmp(pasta, it->caminho);
}

/* a cada quadro, antes de desenhar: soltar aplica o arraste, Esc cancela, e sem apertar nada não há candidato */
static void arraste_rever(void) {
	arr_suprimir = false;
	if (arr_item < 0) {
		if (!gv_ponteiro_baixo()) {
			arr_cand = -1;
		}
		return;
	}
	if (arr_item >= barra.n || gv_tecla() == 0xff1b) { /* a barra mudou embaixo, ou Esc */
		arr_item = -1;
		arr_cand = -1;
		arr_suprimir = true;
		return;
	}
	if (!gv_ponteiro_baixo()) {
		Clay_Vector2 p = gv_ponteiro();
		if (gv_ponteiro_e_toque() && fabsf(p.x - arr_x0) < 10 && fabsf(p.y - arr_y0) < 10) { /* segurou e soltou sem andar: o menu do item */
			lat_menu_idx = arr_item;
			snprintf(lat_menu_destino, sizeof(lat_menu_destino), "%s", destino_do_item(&barra.v[arr_item]));
			gv_menu_pedir_no_ponteiro(CLAY_ID("menu-lateral"));
			arr_item = -1;
			arr_suprimir = true;
			return;
		}
		int novo = ar_barra_mover_para(&barra, arr_item, arr_secao, arr_antes);
		(void)novo;
		ar_barra_gravar(&barra);
		arr_item = -1;
		arr_suprimir = true;
		gv_app_redesenhar(app);
		return;
	}
	gv_rolagem_travar();
}

/* com o arraste em curso: acha onde o item cairia pela altura do ponteiro e desenha a linha e o fantasma */
static void arraste_desenhar(void) {
	if (arr_item < 0 || arr_item >= barra.n) {
		return;
	}
	Clay_Vector2 p = gv_ponteiro();
	int ant = -1, sec = barra.v[arr_item].secao;
	bool achou = false;
	float linha_y = 0, linha_x = 0;
	int prev = -1; /* o item visível anterior (sem contar o arrastado) */
	int ult = -1;
	for (int s2 = 0; s2 < AR_SECOES_N && !achou; s2++) {
		int idx[AR_BARRA_MAX];
		int n = ar_barra_da_secao(&barra, s2, idx);
		for (int k = 0; k < n && !achou; k++) {
			int i = idx[k];
			if (!barra.v[i].visivel || i == arr_item) {
				continue;
			}
			Clay_BoundingBox b = gv_caixa(CLAY_IDI("l-it", (uint32_t)i));
			if (b.width <= 0) {
				continue;
			}
			if (prev >= 0 && barra.v[prev].secao != s2) {
				Clay_BoundingBox pb = gv_caixa(CLAY_IDI("l-it", (uint32_t)prev));
				if (p.y < pb.y + pb.height + 12) { /* na beira de baixo do último da seção de cima: cai no fim dela */
					ant = -1;
					sec = barra.v[prev].secao;
					linha_y = pb.y + pb.height + 1;
					linha_x = pb.x;
					achou = true;
					break;
				}
			}
			if (p.y < b.y + b.height / 2) {
				ant = i;
				sec = s2;
				linha_y = b.y - 1;
				linha_x = b.x;
				achou = true;
			}
			prev = i;
			ult = i;
		}
	}
	if (!achou && ult >= 0) {
		Clay_BoundingBox ub = gv_caixa(CLAY_IDI("l-it", (uint32_t)ult));
		ant = -1;
		sec = barra.v[ult].secao;
		linha_y = ub.y + ub.height + 1;
		linha_x = ub.x;
	}
	arr_antes = ant;
	arr_secao = sec;
	if (linha_x > 0 || linha_y > 0) {
		CLAY(CLAY_ID("l-linha"), { .layout = { .sizing = { CLAY_SIZING_FIXED(216), CLAY_SIZING_FIXED(3) } }, .backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(2),
			.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 80, .offset = { linha_x, linha_y }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
	}
	const ar_barra_item *it = &barra.v[arr_item];
	CLAY(CLAY_ID("l-fantasma"), { .layout = { .sizing = { CLAY_SIZING_FIXED(216), CLAY_SIZING_FIXED(40) }, .padding = { 12, 10, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.acento, .width = GV_BORDA(1) },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 81, .offset = { p.x - 100, p.y - 20 }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
		gv_icone(it->icone, 20, gv_t.texto);
		gv_texto(gv_str(it->rotulo), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_MEDIO);
	}
}

static void barra_lateral(void) {
	arraste_rever();
	Clay_ElementId rol = CLAY_ID("lateral-rolagem");
	CLAY(CLAY_ID("lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(236), CLAY_SIZING_GROW(0) } },
		.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
		CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 10, 10, 4, 12 }, .childGap = 2 } })) {
			static const char *const TITULOS[AR_SECOES_N] = { "Espaços", "Coleções" };
			for (int sec = 0; sec < AR_SECOES_N; sec++) {
				int idx[AR_BARRA_MAX];
				int n = ar_barra_da_secao(&barra, sec, idx);
				int visiveis = 0;
				for (int k = 0; k < n; k++) {
					visiveis += barra.v[idx[k]].visivel;
				}
				if (!visiveis) {
					continue;
				}
				if (sec > 0) {
					CLAY(CLAY_IDI("l-div", (uint32_t)sec), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
				}
				titulo_lateral(TITULOS[sec]);
				for (int k = 0; k < n; k++) {
					const ar_barra_item *it = &barra.v[idx[k]];
					if (it->visivel) {
						item_lateral(CLAY_IDI("l-it", (uint32_t)idx[k]), idx[k], it->icone, it->rotulo, item_da_barra_ativo(it), destino_do_item(it));
					}
				}
			}
			arraste_desenhar();
			CLAY(CLAY_ID("l-div-rede"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			titulo_lateral("Rede");
			item_lateral(CLAY_ID("l-rede"), -1, "network", "Compartilhadas", vista == V_REDE, ESPECIAL_REDE);
			item_lateral(CLAY_ID("l-nuvem"), -1, "cloud", "Nuvem", vista == V_NUVEM, ESPECIAL_NUVEM);
			CLAY(CLAY_ID("l-d3"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			char *lix = ar_lixeira_pasta();
			item_lateral(CLAY_ID("l-lixeira"), -1, "trash", "Lixeira", em_lixeira(), lix);
			g_free(lix);
		}
	}
}

/* ---- o conteúdo ---- */

static int colunas;      /* quantas por linha na grade (a conta do quadro anterior) */

/* o ponteiro (ou o dedo) está mesmo dentro desta caixa? (o pedido de menu vale só para o item sob ele) */
static bool ponteiro_em(Clay_BoundingBox b) {
	Clay_Vector2 p = gv_ponteiro();
	return b.width > 0 && p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
}

static bool ponteiro_em_p(Clay_BoundingBox b, Clay_Vector2 p) {
	return b.width > 0 && p.x >= b.x && p.x <= b.x + b.width && p.y >= b.y && p.y <= b.y + b.height;
}

static void abrir_menu_do_item(int i) {
	if (!sel[i]) {
		limpar_selecao();
		sel[i] = true;
		ancora = foco = i;
	}
	menu_item = i;
}

/* o campo no lugar do nome, enquanto se renomeia: Enter confirma, Esc cancela, clicar fora também confirma */
static void campo_renomear(void) {
	int r = gv_campo(CLAY_ID("renome"), renome_nome, sizeof(renome_nome), "Nome", GV_CAMPO_TEXTO);
	if (focar_renome) {
		gv_focar(CLAY_ID("renome"));
		focar_renome = false;
	}
	if (r & GV_CAMPO_ENTER) {
		confirmar_renome();
	} else if (++renome_quadros > 4 && !gv_focado(CLAY_ID("renome"))) {
		confirmar_renome();
	}
}

static bool item_recortado(int idx, char **area, bool recortar) {
	if (!recortar || !area || !cam[idx]) {
		return false;
	}
	for (int i = 0; area[i]; i++) {
		if (!strcmp(area[i], cam[idx])) {
			return true;
		}
	}
	return false;
}

static char **area_quadro;       /* o que a área de transferência tem neste quadro */
static bool area_quadro_recortar;

/* ---- arrastar e soltar de arquivos (entre janelas, e dentro desta) ----
 * Arrastar: apertar num item e andar (mouse); vai a seleção como text/uri-list. Soltar: numa pasta da grade, num atalho de pasta da barra lateral ou no fundo da
 * lista (a pasta de agora) copia para lá. Entre os apps do LUMNA a ação é sempre copiar. */
#define MIME_URIS "text/uri-list"
static uint32_t dnd_cand;
static float dnd_x0, dnd_y0;
static int dnd_alvo_idx = -1;        /* a pasta da grade debaixo do arraste (índice em lista), ou -1 */
static char dnd_alvo[4096];          /* para onde cairia ("" nada) */

static bool dnd_origem_ok(void) {
	return renomeando < 0 && !em_lixeira() && vista != V_INICIO && vista != V_REDE && vista != V_NUVEM && pagina_cfg < 0;
}

static void arraste_arquivo_ver(Clay_ElementId id, int idx) {
	if (gv_arrastando_daqui() || gv_ponteiro_e_toque() || !dnd_origem_ok()) {
		return;
	}
	Clay_Vector2 p = gv_ponteiro();
	if (!gv_premido(id)) {
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
	if (!sel[idx]) { /* arrastar um item solto leva só ele */
		limpar_selecao();
		sel[idx] = true;
		ancora = foco = idx;
	}
	GString *g = g_string_new(NULL);
	int n = 0;
	for (int i = 0; i < lista.n; i++) {
		if (sel[i] && cam[i]) {
			char *uri = g_filename_to_uri(cam[i], NULL, NULL);
			if (uri) {
				g_string_append_printf(g, "%s\r\n", uri);
				n++;
			}
			g_free(uri);
		}
	}
	if (n) {
		gv_arrastar_comecar(MIME_URIS, g->str, n > 1 ? "copy" : lista.v[idx].pasta ? "folder" : "document");
	}
	g_string_free(g, TRUE);
}

/* a cada quadro: onde cairia um arraste que passa por cima desta janela, e o que fazer quando soltam */
static void dnd_rever(void) {
	dnd_alvo_idx = -1;
	dnd_alvo[0] = '\0';
	Clay_Vector2 p;
	bool sobre = gv_arraste_sobre(&p);
	if (sobre && pagina_cfg < 0) {
		/* um atalho de pasta da barra lateral */
		for (int i = 0; i < barra.n && !dnd_alvo[0]; i++) {
			if (barra.v[i].visivel && barra.v[i].caminho[0] && ponteiro_em_p(gv_caixa(CLAY_IDI("l-it", (uint32_t)i)), p)) {
				snprintf(dnd_alvo, sizeof(dnd_alvo), "%s", barra.v[i].caminho);
			}
		}
		/* uma pasta da grade ou da lista */
		for (int k = 0; k < nvis && !dnd_alvo[0] && vista == V_PASTA; k++) {
			int idx = vis[k];
			Clay_ElementId id = modo_lista ? CLAY_IDI("lin", (uint32_t)idx) : CLAY_IDI("cel", (uint32_t)idx);
			if (lista.v[idx].pasta && cam[idx] && ponteiro_em_p(gv_caixa(id), p)) {
				dnd_alvo_idx = idx;
				snprintf(dnd_alvo, sizeof(dnd_alvo), "%s", cam[idx]);
			}
		}
		/* o fundo da lista: a pasta de agora */
		if (!dnd_alvo[0] && vista == V_PASTA && !em_lixeira() && ponteiro_em_p(gv_caixa(ID_CONTEUDO), p)) {
			snprintf(dnd_alvo, sizeof(dnd_alvo), "%s", pasta);
		}
	}
	static char ultimo[4096]; /* o alvo do último quadro com o arraste por cima: soltar já tira o "por cima" */
	if (sobre) {
		snprintf(ultimo, sizeof(ultimo), "%s", dnd_alvo);
	}
	char *dados = NULL;
	if (!gv_arraste_soltou(MIME_URIS, &dados)) {
		if (!sobre) {
			ultimo[0] = '\0';
		}
		return;
	}
	char destino[4096];
	snprintf(destino, sizeof(destino), "%s", ultimo);
	ultimo[0] = '\0';
	if (destino[0]) {
		GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
		char **linhas = g_strsplit_set(dados, "\r\n", -1);
		for (int i = 0; linhas[i]; i++) {
			if (g_str_has_prefix(linhas[i], "file://")) {
				char *c = g_filename_from_uri(linhas[i], NULL, NULL);
				if (c) {
					char *pai = g_path_get_dirname(c);
					bool ja_la = !strcmp(pai, destino) || !strcmp(c, destino); /* já está lá, ou é a própria pasta */
					g_free(pai);
					if (ja_la) {
						g_free(c);
					} else {
						g_ptr_array_add(v, c);
					}
				}
			}
		}
		g_strfreev(linhas);
		if (v->len) {
			iniciar_op(AR_OP_COPIAR, v, destino);
		}
		g_ptr_array_free(v, TRUE);
	}
	free(dados);
}

/* o nome numa linha só, com "…" no fim se não couber na coluna (a largura que ela tinha no quadro anterior; sem ela ainda, o nome inteiro) */
static Clay_String nome_em_uma_linha(Clay_ElementId coluna, const char *nome, float tamanho) {
	float larg = gv_caixa(coluna).width;
	if (larg <= 8) {
		return gv_str(nome);
	}
	Clay_String linha;
	return gv_texto_ajustar(nome, larg - 4, tamanho, GV_PESO_NORMAL, 1, &linha) ? linha : gv_str(nome);
}

static void celula_grade(int idx, float tam, float cw, float ch) {
	Clay_ElementId id = CLAY_IDI("cel", (uint32_t)idx);
	const ar_entrada *e = &lista.v[idx];
	if (e->pasta && e->itens < -1 + 1 && e->itens == -1) {
		lista.v[idx].itens = ar_contar_itens(cam[idx], cfg.ocultos);
	}
	bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-item"));
	if (pediu && ponteiro_em(gv_caixa(id))) {
		abrir_menu_do_item(idx);
	}
	arraste_arquivo_ver(id, idx);
	Clay_ElementId mais = CLAY_IDI("cel-mais", (uint32_t)idx);
	bool sobre = gv_por_cima(id) || gv_por_cima(mais);
	bool recortado = item_recortado(idx, area_quadro, area_quadro_recortar);
	Clay_Color fundo = dnd_alvo_idx == idx ? gv_alfa(gv_t.acento, 0.35f) : sel[idx] ? gv_alfa(gv_t.acento, 0.18f) : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 };
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(cw), CLAY_SIZING_FIXED(ch) }, .padding = { 6, 6, 10, 6 },
		.childGap = 6, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .backgroundColor = fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = sel[idx] ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .width = GV_BORDA(2) } }) {
		icone(CLAY_IDI("cel-ic", (uint32_t)idx), idx, tam);
		CLAY(CLAY_IDI("cel-nome", (uint32_t)idx), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
			.clip = { .horizontal = true, .vertical = true } }) {
			CLAY(CLAY_IDI("cel-nome-t", (uint32_t)idx), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0, (gv_t.tamanho_corpo + 6) * 2) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .clip = { .vertical = true } }) {
				if (renomeando == idx) {
					campo_renomear();
				} else {
					/* o nome inteiro, em até duas linhas (quebra nos espaços, nos "_" e "-", ou no meio da palavra); se ainda passar, termina em "…" */
					Clay_String ls[2];
					int nl = gv_texto_ajustar(nome_exibido(e), cw - 24, gv_t.tamanho_corpo - 1, GV_PESO_NORMAL, 2, ls);
					for (int i = 0; i < nl; i++) {
						gv_texto(ls[i], gv_t.tamanho_corpo - 1, recortado ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
					}
				}
			}
			if (e->pasta && e->itens >= 0) {
				gv_texto(gv_fmt(e->itens == 1 ? "%d item" : "%d itens", e->itens), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		if ((sobre || sel[idx]) && renomeando != idx) { /* o "..." no canto: o mesmo menu do botão direito */
			bool sm = gv_por_cima(mais);
			CLAY(mais, { .layout = { .sizing = { CLAY_SIZING_FIXED(30), CLAY_SIZING_FIXED(30) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = sm ? gv_t.selecionado : gv_alfa(gv_t.superficie, 0.9f), .cornerRadius = CLAY_CORNER_RADIUS(15),
				.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
				.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_TOP }, .offset = { -6, 6 } } }) {
				gv_icone("more", 20, gv_t.texto);
			}
		}
	}
}

static void linha_lista(int idx, int linha_par) {
	Clay_ElementId id = CLAY_IDI("lin", (uint32_t)idx);
	const ar_entrada *e = &lista.v[idx];
	if (e->pasta && e->itens == -1) {
		lista.v[idx].itens = ar_contar_itens(cam[idx], cfg.ocultos);
	}
	bool pediu = gv_menu_no_ponteiro(id, CLAY_ID("menu-item"));
	if (pediu && ponteiro_em(gv_caixa(id))) {
		abrir_menu_do_item(idx);
	}
	arraste_arquivo_ver(id, idx);
	Clay_ElementId mais = CLAY_IDI("lin-mais", (uint32_t)idx);
	bool sobre = gv_por_cima(id) || gv_por_cima(mais);
	Clay_Color fundo = sel[idx] ? gv_alfa(gv_t.acento, 0.18f) : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 };
	char data[48], tam[32];
	ar_data_humana(e->mtime, time(NULL), data, sizeof(data));
	if (e->pasta && vista_atual == AR_VISTA_LISTA_GRANDE) {
		tam[0] = '\0'; /* os itens já estão sob o nome */
	} else if (e->pasta) {
		snprintf(tam, sizeof(tam), e->itens >= 0 ? (e->itens == 1 ? "%d item" : "%d itens") : "", e->itens);
	} else {
		ar_tamanho_humano(e->tamanho, tam, sizeof(tam));
	}
	bool grande = vista_atual == AR_VISTA_LISTA_GRANDE;
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(altura_linha()) }, .padding = { 14, 14, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		icone(CLAY_IDI("lin-ic", (uint32_t)idx), idx, grande ? 54 : 30);
		CLAY(CLAY_IDI("lin-nome", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
			if (renomeando == idx) {
				campo_renomear();
			} else if (grande) {
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
					gv_texto(nome_em_uma_linha(CLAY_IDI("lin-nome", (uint32_t)idx), nome_exibido(e), gv_t.tamanho_corpo + 1), gv_t.tamanho_corpo + 1, item_recortado(idx, area_quadro, area_quadro_recortar) ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_str(e->pasta ? (e->itens >= 0 ? gv_fmt(e->itens == 1 ? "%d item" : "%d itens", e->itens).chars : "") : ar_tipo_nome(e)), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			} else {
				gv_texto(nome_em_uma_linha(CLAY_IDI("lin-nome", (uint32_t)idx), nome_exibido(e), gv_t.tamanho_corpo), gv_t.tamanho_corpo, item_recortado(idx, area_quadro, area_quadro_recortar) ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_IDI("lin-data", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			gv_texto(gv_str(data), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_IDI("lin-tam", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_FIXED(90) } } }) {
			gv_texto(gv_str(tam), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (!grande) {
			CLAY(CLAY_IDI("lin-tipo", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
				gv_texto(gv_str(ar_tipo_nome(e)), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		if ((sobre || sel[idx]) && renomeando != idx) { /* o "..." no fim da linha */
			bool sm = gv_por_cima(mais);
			CLAY(mais, { .layout = { .sizing = { CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = sm ? gv_t.selecionado : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(16) }) {
				gv_icone("more", 20, gv_t.texto);
			}
		} else {
			CLAY(CLAY_IDI("lin-mais-vazio", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(32) } } }) {}
		}
	}
	(void)linha_par;
}

/* um clique (ou toque) num item: seleciona; o segundo em seguida abre */
static void tratar_clique(int idx, Clay_ElementId id) {
	if (!gv_tocado(id) || renomeando == idx) {
		return;
	}
	double agora = gv_agora();
	uint32_t mods = gv_modificadores();
	if (idx == ultimo_item && agora - ultimo_clique < 0.45 && !(mods & (GV_MOD_CTRL | GV_MOD_SHIFT))) {
		abrir_item(idx);
		ultimo_item = -1;
		return;
	}
	clicar_item(idx, mods);
	ultimo_item = idx;
	ultimo_clique = agora;
}

static void cabecalho_lista(void) {
	static const struct { const char *nome; enum ar_ordem o; float larg; } COLS[] = {
		{ "Nome", AR_O_NOME, 0 }, { "Modificado", AR_O_DATA, 150 }, { "Tamanho", AR_O_TAMANHO, 90 }, { "Tipo", AR_O_TIPO, 130 } };
	CLAY(CLAY_ID("cab-lista"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(34) }, .padding = { 16 + 14 + (vista_atual == AR_VISTA_LISTA_GRANDE ? 54 : 30) + 14, 16 + 14 + 6, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		for (int c = 0; c < (vista_atual == AR_VISTA_LISTA_GRANDE ? 3 : 4); c++) {
			Clay_ElementId id = CLAY_IDI("cab-col", (uint32_t)c);
			CLAY(id, { .layout = { .sizing = { COLS[c].larg ? CLAY_SIZING_FIXED(COLS[c].larg) : CLAY_SIZING_GROW(0) }, .childGap = 4, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(gv_str(COLS[c].nome), gv_t.tamanho_legenda + 1, cfg.ordem == (int)COLS[c].o ? gv_t.texto : gv_t.texto_suave, GV_PESO_MEDIO);
				if (cfg.ordem == (int)COLS[c].o) {
					gv_icone(cfg.decrescente ? "chevron-down" : "chevron-up", 14, gv_t.texto_suave);
				}
			}
			if (gv_tocado(id)) {
				if (cfg.ordem == (int)COLS[c].o) {
					cfg.decrescente = !cfg.decrescente;
				} else {
					cfg.ordem = COLS[c].o;
					cfg.decrescente = false;
				}
				carregar(true);
			}
		}
	}
}

static const char *const ROTULO_FILTRO[] = { "Tudo", "Pastas", "Documentos", "Imagens", "Mídia", "Outros" };
static const char *const ICONE_FILTRO[] = { NULL, "folder", "document", "image", "video", "file" };

static void cabecalho_conteudo(void) {
	CLAY(CLAY_ID("cabeca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 18, 8 }, .childGap = 4 } }) {
		gv_texto(gv_str(nome_da_vista()), 32, gv_t.texto, GV_PESO_NORMAL);
		const char *sub;
		if (erro_pasta[0]) {
			sub = "Esta pasta não abriu";
		} else if (n_pastas && n_arquivos) {
			sub = gv_fmt("%d %s · %d %s", n_pastas, n_pastas == 1 ? "pasta" : "pastas", n_arquivos, n_arquivos == 1 ? "arquivo" : "arquivos").chars;
		} else if (n_pastas) {
			sub = gv_fmt("%d %s", n_pastas, n_pastas == 1 ? "pasta" : "pastas").chars;
		} else if (n_arquivos) {
			sub = gv_fmt("%d %s", n_arquivos, n_arquivos == 1 ? "arquivo" : "arquivos").chars;
		} else {
			sub = "Vazia";
		}
		gv_texto(gv_str(sub), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		if (em_lixeira() && nvis > 0) {
			CLAY(CLAY_ID("lix-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 8, 0 }, .childGap = 8 } }) {
				if (gv_botao_ext(CLAY_ID("lix-esvaziar"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Esvaziar a lixeira", .estilo = GV_BOTAO_PERIGO, .altura = 38 })) {
					pedir_esvaziar();
				}
				if (n_selecionados() > 0 && gv_botao_ext(CLAY_ID("lix-restaurar"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Restaurar", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
					restaurar_selecao();
				}
			}
		}
	}
	/* os filtros: só os que têm algo (e o que está ligado); se nenhum separa nada, a faixa nem aparece */
	int uteis = 0;
	for (int f = 1; f < 6; f++) {
		uteis += cont_filtros[f] && cont_filtros[f] != cont_filtros[0];
	}
	if (!uteis && filtro == AR_F_TUDO) {
		return;
	}
	CLAY(CLAY_ID("filtros"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 4, 10 }, .childGap = 10 } }) {
		for (int f = 0; f < 6; f++) {
			if (f && !cont_filtros[f] && filtro != (enum ar_filtro)f) {
				continue;
			}
			if (f && cont_filtros[f] == cont_filtros[0] && filtro != (enum ar_filtro)f) {
				continue; /* é tudo: o filtro não separa nada */
			}
			Clay_ElementId id = CLAY_IDI("filtro", (uint32_t)f);
			bool ligado = filtro == (enum ar_filtro)f;
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(f ? 132 : 96), CLAY_SIZING_FIXED(38) }, .childGap = 8,
				.childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = ligado ? gv_t.acento : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(19),
				.border = { .color = ligado ? gv_t.acento : gv_t.borda, .width = GV_BORDA(1) } }) {
				if (ICONE_FILTRO[f]) {
					gv_icone(ICONE_FILTRO[f], 16, ligado ? gv_t.sobre_acento : gv_t.texto_secundario);
				}
				gv_texto(gv_str(ROTULO_FILTRO[f]), gv_t.tamanho_corpo - 1, ligado ? gv_t.sobre_acento : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (gv_tocado(id)) {
				filtro = (enum ar_filtro)f;
				refiltrar();
				gv_rolar_para(ID_CONTEUDO, 0, 0, false);
			}
		}
	}
}

static void conteudo(void) {
	if (vista == V_INICIO) {
		conteudo_inicio();
		return;
	}
	if (vista == V_REDE || vista == V_NUVEM) {
		conteudo_rede();
		return;
	}
	CLAY(CLAY_ID("principal"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		cabecalho_conteudo();
		if (modo_lista && !erro_pasta[0]) {
			cabecalho_lista();
		}
		CLAY(ID_CONTEUDO, gv_rolagem(ID_CONTEUDO, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 16, 16, 6, 16 } } })) {
			float larg = gv_caixa(ID_CONTEUDO).width - 32 - 12;
			if (larg < 200) {
				larg = 800;
			}
			float topo = gv_rolagem_posicao(ID_CONTEUDO).y;
			float alt_view = gv_caixa(ID_CONTEUDO).height > 0 ? gv_caixa(ID_CONTEUDO).height : 600;
			if (erro_pasta[0]) {
				CLAY(CLAY_ID("erro"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 40, 0 }, .childGap = 10,
					.childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
					gv_icone("lock", 40, gv_t.texto_suave);
					gv_texto(CLAY_STRING("Não foi possível abrir esta pasta"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(erro_pasta), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			} else if (!nvis) {
				CLAY(CLAY_ID("vazio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 60, 0 }, .childGap = 10,
					.childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
					gv_icone(busca[0] ? "search" : vista == V_FAVORITOS ? "star" : vista == V_RECENTES ? "clock" : "folder", 44, gv_t.texto_suave);
					gv_texto(busca[0] ? CLAY_STRING("Nada com esse nome aqui") : vista == V_FAVORITOS ? CLAY_STRING("Nenhum favorito ainda") :
						vista == V_RECENTES ? CLAY_STRING("Nada aberto recentemente") : CLAY_STRING("Esta pasta está vazia"), gv_t.tamanho_destaque, gv_t.texto_secundario, GV_PESO_NORMAL);
					if (vista == V_FAVORITOS) {
						gv_texto(CLAY_STRING("Clique com o botão direito num item e escolha Favoritar"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
					}
				}
			} else if (!modo_lista) {
				float tam = tam_icone();
				float cw = tam + 68, ch = tam + 14 + 10 + 6 + (gv_t.tamanho_corpo + 6) * 2 + 20;
				int cols = (int)(larg / cw);
				colunas = cols < 1 ? 1 : cols;
				int linhas = (nvis + colunas - 1) / colunas;
				int r0 = (int)(topo / ch) - 1, r1 = (int)((topo + alt_view) / ch) + 1;
				r0 = r0 < 0 ? 0 : r0;
				r1 = r1 >= linhas ? linhas - 1 : r1;
				if (r0 > 0) {
					CLAY(CLAY_ID("esp-cima"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(r0 * ch) } } }) {}
				}
				for (int r = r0; r <= r1; r++) {
					CLAY(CLAY_IDI("grade-linha", (uint32_t)r), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(ch) }, .childGap = 0 } }) {
						for (int c = 0; c < colunas; c++) {
							int k = r * colunas + c;
							if (k >= nvis) {
								break;
							}
							celula_grade(vis[k], tam, cw, ch);
						}
					}
				}
				if (r1 < linhas - 1) {
					CLAY(CLAY_ID("esp-baixo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((linhas - 1 - r1) * ch) } } }) {}
				}
			} else {
				float ch = altura_linha();
				int r0 = (int)(topo / ch) - 2, r1 = (int)((topo + alt_view) / ch) + 2;
				r0 = r0 < 0 ? 0 : r0;
				r1 = r1 >= nvis ? nvis - 1 : r1;
				if (r0 > 0) {
					CLAY(CLAY_ID("esp-cima"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(r0 * ch) } } }) {}
				}
				for (int k = r0; k <= r1; k++) {
					linha_lista(vis[k], k & 1);
				}
				if (r1 < nvis - 1) {
					CLAY(CLAY_ID("esp-baixo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED((nvis - 1 - r1) * ch) } } }) {}
				}
			}
		}
	}
}

/* os cliques são tratados depois do desenho: o Clay já sabe onde cada um está */
static void tratar_cliques_conteudo(void) {
	if (VISTA_CARTOES) {
		return; /* o Início e a Rede têm os seus cartões; o fundo da rolagem pedir o toque por último tirava o clique deles */
	}
	bool fundo = gv_tocado(ID_CONTEUDO); /* primeiro: quem pede o aperto depois ganha, e o item vale mais que o fundo */
	bool algum = false;
	for (int k = 0; k < nvis; k++) {
		int idx = vis[k];
		Clay_ElementId id = modo_lista ? CLAY_IDI("lin", (uint32_t)idx) : CLAY_IDI("cel", (uint32_t)idx);
		bool no_item = gv_caixa(id).width > 0 && gv_tocado(id);
		Clay_ElementId mais = modo_lista ? CLAY_IDI("lin-mais", (uint32_t)idx) : CLAY_IDI("cel-mais", (uint32_t)idx);
		if (gv_caixa(mais).width > 0 && gv_tocado(mais)) { /* chamado depois do item: o "..." ganha o toque que é dele */
			abrir_menu_do_item(idx);
			gv_menu_pedir_no_ponteiro(CLAY_ID("menu-item"));
			algum = true;
		} else if (no_item) {
			tratar_clique(idx, id);
			algum = true;
		}
	}
	if (!algum && fundo && !(gv_modificadores() & (GV_MOD_CTRL | GV_MOD_SHIFT))) {
		limpar_selecao();
		ancora = foco = -1;
	}
}

/* ---- painel de detalhes ---- */

static char det_caminho[4096];
static int64_t det_bytes;
static int det_itens;
static bool det_completo;
static bool permissoes_abertas;

static void linha_detalhe(const char *rotulo, const char *valor) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(84) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(valor), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static void painel_detalhes(void) {
	int n = n_selecionados();
	int idx = n == 1 ? primeiro_selecionado() : -1;
	ar_entrada pasta_atual;
	const ar_entrada *e = NULL;
	const char *caminho = NULL;
	if (idx >= 0) {
		e = &lista.v[idx];
		caminho = cam[idx];
	} else if (n == 0 && vista == V_PASTA && ar_entrada_de(pasta, &pasta_atual)) {
		e = &pasta_atual;
		caminho = pasta;
	}
	CLAY(CLAY_ID("detalhes"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(310), CLAY_SIZING_GROW(0) }, .padding = { 20, 20, 18, 14 }, .childGap = 14 },
		.border = { .color = gv_t.borda, .width = { .left = 1 } } }) {
		CLAY(CLAY_ID("det-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("det-titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(n > 1 ? gv_fmt("%d itens", n).chars : e ? (e == &pasta_atual ? nome_da_vista() : e->nome) : nome_da_vista()), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao_redondo(CLAY_ID("det-fechar"), "close", 32, false, false)) {
				detalhes = false;
			}
		}
		if (n > 1) {
			int64_t total = 0;
			int pastas = 0;
			for (int i = 0; i < lista.n; i++) {
				if (sel[i]) {
					total += lista.v[i].tamanho;
					pastas += lista.v[i].pasta;
				}
			}
			char t[32];
			ar_tamanho_humano(total, t, sizeof(t));
			CLAY(CLAY_ID("det-multi"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(150) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande) }) {
				gv_icone("stack", 56, gv_t.texto_suave);
			}
			linha_detalhe("Selecionados", gv_fmt("%d %s, %d %s", pastas, pastas == 1 ? "pasta" : "pastas", n - pastas, n - pastas == 1 ? "arquivo" : "arquivos").chars);
			linha_detalhe("Tamanho", t);
		} else if (e) {
			/* a prévia */
			CLAY(CLAY_ID("det-previa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(210) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
				if (e->tipo == AR_IMAGEM && caminho) {
					CLAY(CLAY_ID("det-img"), gv_imagem(CLAY_ID("det-img"), caminho, GV_AJUSTE_CONTER, (Clay_ElementDeclaration){
						.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) })) {}
				} else {
					ic_dados *d = &ic_pool[ic_n++ % (int)(sizeof(ic_pool) / sizeof(ic_pool[0]))];
					d->tipo = e->tipo;
					d->link = e->link;
					d->cor = 0;
					char emblema[32] = "";
					if (e->pasta && caminho) {
						ar_pasta_estilo(caminho, &d->cor, emblema, sizeof(emblema));
					}
					ar_selo(e, d->selo, sizeof(d->selo));
					CLAY(CLAY_ID("det-icone"), gv_livre(desenhar_icone, d, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(140), CLAY_SIZING_FIXED(140) } } })) {
						if (emblema[0]) {
							CLAY_AUTO_ID({ .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
								.offset = { 0, 11 }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
								gv_icone(emblema, 48, (Clay_Color){ 255, 255, 255, 235 });
							}
						}
					}
				}
			}
			CLAY(CLAY_ID("det-linhas"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
				linha_detalhe("Tipo", ar_tipo_nome(e));
				if (e->pasta && caminho) {
					if (strcmp(det_caminho, caminho)) {
						snprintf(det_caminho, sizeof(det_caminho), "%s", caminho);
						det_completo = ar_tamanho_pasta(caminho, 20000, &det_bytes, &det_itens);
					}
					int itens = ar_contar_itens(caminho, cfg.ocultos);
					linha_detalhe("Itens", itens >= 0 ? gv_fmt("%d %s", itens, itens == 1 ? "item" : "itens").chars : "—");
					char t[32];
					ar_tamanho_humano(det_bytes, t, sizeof(t));
					linha_detalhe("Tamanho", det_completo ? t : gv_fmt("mais de %s", t).chars);
				} else {
					char t[32];
					ar_tamanho_humano(e->tamanho, t, sizeof(t));
					linha_detalhe("Tamanho", t);
				}
				if (caminho) {
					char *pai = ar_pai(caminho);
					const char *p = pai ? pai : "/";
					size_t n = g_utf8_strlen(p, -1);
					linha_detalhe("Local", n > 22 ? gv_fmt("…%s", g_utf8_offset_to_pointer(p, (glong)n - 21)).chars : p);
					g_free(pai);
				}
				char data[48];
				ar_data_humana(e->mtime, time(NULL), data, sizeof(data));
				linha_detalhe("Modificado", data);
			}
			CLAY(CLAY_ID("det-div"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			if (caminho) {
				bool fav = ar_eh_favorito(caminho);
				gv_botao_opcoes o = { .icone = "star", .texto = fav ? "Remover dos favoritos" : "Favoritar", .estilo = GV_BOTAO_SIMPLES, .crescer = true };
				if (gv_botao_ext(CLAY_ID("det-fav"), &o)) {
					ar_favorito_alternar(caminho);
					if (vista == V_FAVORITOS) {
						carregar(true);
					}
				}
			}
			/* as permissões, recolhidas */
			Clay_ElementId pid = CLAY_ID("det-perm");
			CLAY(pid, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("shield", 20, gv_t.texto_secundario);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					gv_texto(CLAY_STRING("Permissões"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				gv_icone(permissoes_abertas ? "chevron-up" : "chevron-down", 18, gv_t.texto_suave);
			}
			if (gv_tocado(pid)) {
				permissoes_abertas = !permissoes_abertas;
			}
			if (permissoes_abertas) {
				char p[16];
				ar_permissoes_texto(e->modo, p, sizeof(p));
				linha_detalhe("Modo", gv_fmt("%s (%04o)", p, (unsigned)(e->modo & 07777)).chars);
				linha_detalhe("Dono", e->uid == getuid() ? "Você" : gv_fmt("usuário %u", (unsigned)e->uid).chars);
			}
		}
	}
}

/* ---- barra de status ---- */

static void barra_status(void) {
	CLAY(CLAY_ID("status"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 20, 20, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		if (op) {
			/* uma operação em andamento: o que é, a barra e Cancelar */
			float prog = op_st.total > 0 ? (float)op_st.feito / (float)op_st.total : op_st.itens_total > 0 ? (float)op_st.itens_feitos / (float)op_st.itens_total : 0;
			prog = prog < 0 ? 0 : prog > 1 ? 1 : prog;
			CLAY(CLAY_ID("op-texto"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(op_st.atual[0] ? gv_fmt("%s · %s", op_st.descricao, op_st.atual).chars : op_st.descricao), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("op-trilho"), { .layout = { .sizing = { CLAY_SIZING_FIXED(220), CLAY_SIZING_FIXED(8) } }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {
				CLAY(CLAY_ID("op-barra"), { .layout = { .sizing = { CLAY_SIZING_PERCENT(prog > 0.02f ? prog : 0.02f), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
			}
			if (gv_botao_ext(CLAY_ID("st-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO, .altura = 34 })) {
				ar_op_cancelar(op);
			}
		} else {
		const char *texto;
		bool dentro = aviso[0] && gv_agora() < aviso_ate;
		int n = n_selecionados();
		if (dentro) {
			texto = aviso;
		} else if (n > 0) {
			int pastas = 0;
			for (int i = 0; i < lista.n; i++) {
				pastas += sel[i] && lista.v[i].pasta;
			}
			if (n == 1) {
				texto = gv_fmt("%d %s · 1 %s selecionad%s", nvis, nvis == 1 ? "item" : "itens", pastas ? "pasta" : "arquivo", pastas ? "a" : "o").chars;
			} else {
				texto = gv_fmt("%d itens · %d selecionados", nvis, n).chars;
			}
		} else {
			texto = gv_fmt("%d %s", nvis, nvis == 1 ? "item" : "itens").chars;
		}
		CLAY(CLAY_ID("status-texto"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, dentro ? gv_t.aviso : gv_t.texto_secundario, GV_PESO_NORMAL);
			if (gv_agora() < desfazer_ate && ar_desfazer_ha(NULL, 0)) {
				if (gv_botao_ext(CLAY_ID("st-desfazer"), &(gv_botao_opcoes){ .icone = "restore", .texto = "Desfazer", .estilo = GV_BOTAO_ELEVADO, .altura = 32 })) {
					desfazer_ultimo();
				}
			}
		}
		if (!modo_lista) {
			gv_icone("minus", 16, gv_t.texto_suave);
			CLAY(CLAY_ID("zoom-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(120) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				float v01 = (float)(cfg.tam_pasta - 64) / 128.0f;
				if (gv_deslizante(CLAY_ID("zoom"), &v01, 5, 16)) {
					cfg.tam_pasta = 64 + (int)(v01 * 128.0f + 0.5f);
					zoom_sujo = true;
				}
			}
			gv_icone("plus", 16, gv_t.texto_suave);
		}
		bool c;
		botao_barra(CLAY_ID("s-grade"), "grid", !modo_lista, false, &c);
		if (c) {
			mudar_vista(AR_VISTA_GRADE);
		}
		botao_barra(CLAY_ID("s-lista"), "list", modo_lista, false, &c);
		if (c) {
			mudar_vista(vista_atual == AR_VISTA_LISTA_GRANDE ? AR_VISTA_LISTA_GRANDE : AR_VISTA_LISTA);
		}
		}
	}
}

/* ---- menus de contexto ---- */

static void separador(const char *nome, uint32_t i) {
	CLAY(CLAY_SIDI(gv_str(nome), i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
}

/* ---- submenus: abrem num popup ao lado do menu (o parâmetro .pai liga os dois) ---- */

/* os submenus do menu de um item e os do menu do espaço vazio (ids diferentes: um menu fechado não pode fechar o submenu do outro) */
static const char *const SUBMENUS_ITEM[] = { "sub-cor", "sub-icone", "sub-fixar", "sub-compart", NULL };
static const char *const SUBMENUS_FUNDO[] = { "sub-cor-f", "sub-icone-f", "sub-ordem", NULL };
static Clay_ElementId id_de(const char *nome) {
	Clay_String cs = { .chars = nome, .length = (int)strlen(nome) };
	return (Clay_ElementId){ .id = Clay__HashString(cs, 0).id };
}

static void fechar_submenus(const char *const *lista) {
	for (int k = 0; lista[k]; k++) {
		gv_popup_fechar(id_de(lista[k]));
	}
}

/* a linha que abre (ou fecha) um submenu; abrir um fecha os outros do mesmo menu */
static void linha_submenu(Clay_ElementId id, const char *ic, const char *texto, const char *sub, const char *const *irmaos) {
	Clay_ElementId sid = id_de(sub);
	bool aberto = gv_popup_aberto(sid);
	if (gv_menu_item(id, ic, texto, aberto ? "‹" : "›")) {
		fechar_submenus(irmaos);
		if (!aberto) {
			gv_popup_abrir(sid);
		}
	}
}

/* as cores de pasta (Padrão, Azul...), com o visto na de agora */
static void submenu_cor(const char *alvo, Clay_ElementId ancora, Clay_ElementId pai, const char *sub) {
	GV_POPUP(id_de(sub), ancora, .pai = pai, .lado = GV_POPUP_DIREITA, .largura = 230, .distancia = 4) {
		uint32_t rgb;
		char emb[32];
		ar_pasta_estilo(alvo, &rgb, emb, sizeof(emb));
		for (int c = 0; c < AR_CORES_N; c++) {
			uint32_t v = AR_CORES[c].rgb;
			Clay_Color cor = v ? (Clay_Color){ (float)((v >> 16) & 255), (float)((v >> 8) & 255), (float)(v & 255), 255 } : gv_t.acento;
			if (gv_menu_item_ext(CLAY_IDI("sub-cor-i", (uint32_t)c), "folder", cor, AR_CORES[c].nome, NULL, v == rgb)) {
				ar_pasta_estilo_definir(alvo, v, emb);
				gv_popup_fechar(pai);
			}
		}
	}
}

/* os emblemas de pasta, numa grade; o de agora fica marcado e tocar nele de novo tira */
static void submenu_icone(const char *alvo, Clay_ElementId ancora, Clay_ElementId pai, const char *sub) {
	GV_POPUP(id_de(sub), ancora, .pai = pai, .lado = GV_POPUP_DIREITA, .largura = 4 * 48 + 12 + 8, .distancia = 4) {
		uint32_t rgb;
		char emb[32];
		ar_pasta_estilo(alvo, &rgb, emb, sizeof(emb));
		int n = 0;
		while (AR_ICONES[n]) {
			n++;
		}
		for (int linha = 0; linha * 4 < n; linha++) {
			CLAY(CLAY_IDI("sub-ic-linha", (uint32_t)linha), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4, .padding = { 4, 4, 0, 0 } } }) {
				for (int i = linha * 4; i < n && i < linha * 4 + 4; i++) {
					Clay_ElementId id = CLAY_IDI("sub-ic", (uint32_t)i);
					bool atual = !strcmp(AR_ICONES[i], emb);
					CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(44), CLAY_SIZING_FIXED(44) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
						.backgroundColor = atual ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
						.border = { .color = atual ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .width = GV_BORDA(atual ? 2 : 0) } }) {
						gv_icone(AR_ICONES[i], 22, atual ? gv_t.texto : gv_t.texto_secundario);
					}
					if (gv_tocado(id)) {
						ar_pasta_estilo_definir(alvo, rgb, atual ? "" : AR_ICONES[i]);
						gv_popup_fechar(pai);
					}
				}
			}
		}
		if (emb[0] && gv_menu_item(CLAY_ID("sub-ic-nenhum"), NULL, "Sem ícone", NULL)) {
			ar_pasta_estilo_definir(alvo, rgb, "");
			gv_popup_fechar(pai);
		}
	}
}

#define FECHAR_MENU() gv_popup_fechar(CLAY_ID("menu-item"))

static void menu_do_item(void) {
	int n = n_selecionados();
	int i = primeiro_selecionado();
	if (n == 0 || i < 0) {
		return;
	}
	bool unico = n == 1;
	bool pasta1 = unico && lista.v[i].pasta;
	bool lix = em_lixeira();
	bool arquivo_compactado = unico && lista.v[i].tipo == AR_COMPACTADO;
	if (lix) {
		if (gv_menu_item(CLAY_ID("mi-restaurar"), "restore", "Restaurar", NULL)) {
			restaurar_selecao();
			FECHAR_MENU();
		}
		if (gv_menu_item(CLAY_ID("mi-apagar"), "trash", "Excluir permanentemente", "Shift+Del")) {
			apagar_selecao(true);
			FECHAR_MENU();
		}
		separador("mi-sep", 9);
		if (gv_menu_item(CLAY_ID("mi-props"), "info", "Propriedades", "Ctrl+I")) {
			pr_abrir();
			FECHAR_MENU();
		}
		return;
	}
	if (gv_menu_item(CLAY_ID("mi-abrir"), pasta1 ? "folder" : "external", "Abrir", "Enter")) {
		for (int j = 0; j < lista.n; j++) {
			if (sel[j]) {
				abrir_item(j);
				if (pasta1) {
					break;
				}
			}
		}
		FECHAR_MENU();
	}
	if (pasta1 && gv_menu_item(CLAY_ID("mi-aba"), "plus", "Abrir em nova aba", NULL)) {
		char *alvo = g_strdup(cam[i]); /* a aba nova relê a lista: o caminho tem de ser uma cópia */
		FECHAR_MENU();
		aba_nova(alvo);
		g_free(alvo);
	}
	if (pasta1 && gv_menu_item(CLAY_ID("mi-janela"), "external", "Abrir em nova janela", NULL)) {
		abrir_nova_janela(cam[i]);
		FECHAR_MENU();
	}
	if (vista != V_PASTA && unico && gv_menu_item(CLAY_ID("mi-local"), "folder", "Mostrar na pasta", NULL)) {
		char *pai = ar_pai(cam[i]);
		char *alvo = g_strdup(cam[i]);
		if (pai) {
			ir_para(pai, true);
			selecionar_caminho(alvo);
		}
		g_free(pai);
		g_free(alvo);
		FECHAR_MENU();
	}
	separador("mi-sep", 1);
	if (gv_menu_item(CLAY_ID("mi-recortar"), "cut", "Recortar", "Ctrl+X")) {
		copiar_selecao(true);
		FECHAR_MENU();
	}
	if (gv_menu_item(CLAY_ID("mi-copiar"), "copy", "Copiar", "Ctrl+C")) {
		copiar_selecao(false);
		FECHAR_MENU();
	}
	if (pasta1 && pode_colar() && gv_menu_item(CLAY_ID("mi-colar"), "paste", "Colar dentro da pasta", "Ctrl+V")) {
		colar_em(cam[i]);
		FECHAR_MENU();
	}
	if (unico && vista == V_PASTA && gv_menu_item(CLAY_ID("mi-renomear"), "edit", "Renomear", "F2")) {
		FECHAR_MENU();
		iniciar_renomear(i);
	}
	separador("mi-sep", 2);
	if (pasta1) {
		linha_submenu(CLAY_ID("mi-cor"), "folder", "Cor da pasta", "sub-cor", SUBMENUS_ITEM);
		submenu_cor(cam[i], CLAY_ID("mi-cor"), CLAY_ID("menu-item"), "sub-cor");
		linha_submenu(CLAY_ID("mi-icone"), "star", "Ícone da pasta", "sub-icone", SUBMENUS_ITEM);
		submenu_icone(cam[i], CLAY_ID("mi-icone"), CLAY_ID("menu-item"), "sub-icone");
		linha_submenu(CLAY_ID("mi-fixar"), "pin", "Fixar na barra lateral", "sub-fixar", SUBMENUS_ITEM);
		GV_POPUP(CLAY_ID("sub-fixar"), CLAY_ID("mi-fixar"), .pai = CLAY_ID("menu-item"), .lado = GV_POPUP_DIREITA, .largura = 240, .distancia = 4) {
			bool e = ar_barra_tem(cam[i], AR_SECAO_ESPACOS), c = ar_barra_tem(cam[i], AR_SECAO_COLECOES), f = ar_eh_favorito(cam[i]);
			if (gv_menu_item(CLAY_ID("mi-fix-espacos"), e ? "check" : "home", "Espaços", NULL)) {
				ar_barra_alternar(cam[i], AR_SECAO_ESPACOS);
				ar_barra_ler(&barra);
				FECHAR_MENU();
			}
			if (gv_menu_item(CLAY_ID("mi-fix-colecoes"), c ? "check" : "folder", "Coleções", NULL)) {
				ar_barra_alternar(cam[i], AR_SECAO_COLECOES);
				ar_barra_ler(&barra);
				FECHAR_MENU();
			}
			if (gv_menu_item(CLAY_ID("mi-fix-favoritos"), f ? "check" : "star", "Favoritos", NULL)) {
				ar_favorito_alternar(cam[i]);
				FECHAR_MENU();
			}
		}
	} else if (unico && gv_menu_item(CLAY_ID("mi-fav"), "star", ar_eh_favorito(cam[i]) ? "Remover dos favoritos" : "Favoritar", NULL)) {
		ar_favorito_alternar(cam[i]);
		if (vista == V_FAVORITOS) {
			carregar(true);
		}
		FECHAR_MENU();
	}
	bool so_arquivos = true;
	for (int j = 0; j < lista.n; j++) {
		if (sel[j] && lista.v[j].pasta) {
			so_arquivos = false;
		}
	}
	if (so_arquivos) {
		bool abrindo = !gv_popup_aberto(CLAY_ID("sub-compart"));
		linha_submenu(CLAY_ID("mi-compart"), "share", "Compartilhar", "sub-compart", SUBMENUS_ITEM);
		if (abrindo && gv_popup_aberto(CLAY_ID("sub-compart"))) {
			celulares_ler();
		}
		GV_POPUP(CLAY_ID("sub-compart"), CLAY_ID("mi-compart"), .pai = CLAY_ID("menu-item"), .lado = GV_POPUP_DIREITA, .largura = 260, .distancia = 4) {
			if (!n_celulares) {
				gv_menu_item(CLAY_ID("mi-cel-nenhum"), "phone", "Nenhum celular conectado", NULL);
			}
			for (int c = 0; c < n_celulares; c++) {
				if (gv_menu_item(CLAY_IDI("mi-cel", (uint32_t)c), "phone", celulares[c].nome, NULL)) {
					enviar_ao_celular(c);
					FECHAR_MENU();
				}
			}
		}
	}
	if (gv_menu_item(CLAY_ID("mi-compactar"), "archive", "Compactar", NULL)) {
		compactar_selecao();
		FECHAR_MENU();
	}
	if (arquivo_compactado && gv_menu_item(CLAY_ID("mi-extrair"), "archive", "Extrair aqui", NULL)) {
		extrair_selecao();
		FECHAR_MENU();
	}
	if (gv_menu_item(CLAY_ID("mi-caminho"), "link", "Copiar caminho", NULL)) {
		if (unico) {
			gv_copiar(cam[i]);
		} else {
			GPtrArray *v = caminhos_selecionados();
			GString *g = g_string_new(NULL);
			for (guint k = 0; k < v->len; k++) {
				g_string_append_printf(g, "%s%s", k ? "\n" : "", (char *)v->pdata[k]);
			}
			gv_copiar(g->str);
			g_string_free(g, TRUE);
			g_ptr_array_free(v, TRUE);
		}
		avisar("Caminho copiado");
		FECHAR_MENU();
	}
	if (unico && gv_menu_item(CLAY_ID("mi-terminal"), "terminal", "Abrir no terminal", NULL)) {
		if (pasta1) {
			abrir_no_terminal(cam[i]);
		} else {
			char *pai = g_path_get_dirname(cam[i]);
			abrir_no_terminal(pai);
			g_free(pai);
		}
		FECHAR_MENU();
	}
	separador("mi-sep", 3);
	if (gv_menu_item(CLAY_ID("mi-props"), "info", "Propriedades", "Ctrl+I")) {
		pr_abrir();
		FECHAR_MENU();
	}
	separador("mi-sep", 4);
	if (gv_menu_item(CLAY_ID("mi-lixeira"), "trash", "Mover para a lixeira", "Del")) {
		apagar_selecao(false);
		FECHAR_MENU();
	}
}

static void fechar_fundo(void) {
	gv_popup_fechar(CLAY_ID("menu-fundo"));
}

static void menu_do_fundo(void) {
	bool real = vista == V_PASTA && !em_lixeira();
	if (real && gv_menu_item(CLAY_ID("mf-pasta"), "folder-plus", "Nova pasta", NULL)) {
		fechar_fundo();
		novo_tipo = 1;
		focar_nome = true;
		snprintf(novo_nome, sizeof(novo_nome), "Nova pasta");
		gv_popup_abrir(CLAY_ID("menu-novo"));
	}
	if (real && gv_menu_item(CLAY_ID("mf-arquivo"), "file", "Novo arquivo de texto", NULL)) {
		fechar_fundo();
		novo_tipo = 2;
		focar_nome = true;
		snprintf(novo_nome, sizeof(novo_nome), "Novo arquivo.txt");
		gv_popup_abrir(CLAY_ID("menu-novo"));
	}
	if (real && pode_colar() && gv_menu_item(CLAY_ID("mf-colar"), "paste", "Colar", "Ctrl+V")) {
		colar();
		fechar_fundo();
	}
	if (real) {
		separador("mf-sep", 1);
	}
	linha_submenu(CLAY_ID("mf-ordenar"), "sliders", "Ordenar por", "sub-ordem", SUBMENUS_FUNDO);
	GV_POPUP(CLAY_ID("sub-ordem"), CLAY_ID("mf-ordenar"), .pai = CLAY_ID("menu-fundo"), .lado = GV_POPUP_DIREITA, .largura = 230, .distancia = 4) {
		for (int o = 0; o < 4; o++) {
			if (gv_menu_item_ext(CLAY_IDI("mf-ord", (uint32_t)o), NULL, gv_t.texto_secundario, NOMES_ORDEM[o], NULL, cfg.ordem == (int)o)) {
				cfg.ordem = (enum ar_ordem)o;
				carregar(true);
				fechar_fundo();
			}
		}
		separador("mf-ord-sep", 0);
		if (gv_menu_item_ext(CLAY_ID("mf-ord-asc"), NULL, gv_t.texto_secundario, "Crescente", NULL, !cfg.decrescente)) {
			cfg.decrescente = false;
			carregar(true);
			fechar_fundo();
		}
		if (gv_menu_item_ext(CLAY_ID("mf-ord-desc"), NULL, gv_t.texto_secundario, "Decrescente", NULL, cfg.decrescente)) {
			cfg.decrescente = true;
			carregar(true);
			fechar_fundo();
		}
	}
	if (gv_menu_item(CLAY_ID("mf-tudo"), "check", "Selecionar tudo", "Ctrl+A")) {
		for (int j = 0; j < nvis; j++) {
			sel[vis[j]] = true;
		}
		fechar_fundo();
	}
	if (gv_menu_item(CLAY_ID("mf-atualizar"), "refresh", "Atualizar", "Ctrl+R")) {
		carregar(true);
		fechar_fundo();
	}
	if (real) {
		separador("mf-sep", 2);
		if (gv_menu_item(CLAY_ID("mf-terminal"), "terminal", "Abrir no terminal aqui", NULL)) {
			abrir_no_terminal(pasta);
			fechar_fundo();
		}
		if (gv_menu_item(CLAY_ID("mf-git"), "code", "Git clone...", NULL)) {
			clonar_aberto = true;
			focar_clonar = true;
			clonar_url[0] = '\0';
			fechar_fundo();
		}
		linha_submenu(CLAY_ID("mf-cor"), "folder", "Cor desta pasta", "sub-cor-f", SUBMENUS_FUNDO);
		submenu_cor(pasta, CLAY_ID("mf-cor"), CLAY_ID("menu-fundo"), "sub-cor-f");
		linha_submenu(CLAY_ID("mf-icone"), "star", "Ícone desta pasta", "sub-icone-f", SUBMENUS_FUNDO);
		submenu_icone(pasta, CLAY_ID("mf-icone"), CLAY_ID("menu-fundo"), "sub-icone-f");
	}
	if (vista == V_PASTA) {
		separador("mf-sep", 3);
		if (gv_menu_item(CLAY_ID("mf-caminho"), "link", "Copiar caminho da pasta", NULL)) {
			gv_copiar(pasta);
			avisar("Caminho copiado");
			fechar_fundo();
		}
		if (gv_menu_item(CLAY_ID("mf-props"), "info", "Propriedades", "Ctrl+I")) {
			pr_abrir();
			fechar_fundo();
		}
	}
	if (em_lixeira() && nvis > 0) {
		separador("mf-sep", 4);
		if (gv_menu_item(CLAY_ID("mf-esvaziar"), "trash", "Esvaziar a lixeira", NULL)) {
			pedir_esvaziar();
			fechar_fundo();
		}
	}
}

/* o menu de contexto abre para baixo do ponto onde se pediu e passa da janela quando preciso: quem o põe na tela é o compositor, que o vira para
 * cima ou o desliza para caber na TELA (não na janela). Só rola se o menu for mais alto que a tela. */
static void lado_do_menu(Clay_ElementId ancora, enum gv_popup_lado *lado, float *altura_max) {
	float tela = gv_app_altura_tela(app);
	*lado = GV_POPUP_ABAIXO;
	*altura_max = tela > 0 ? fmaxf(tela - 96, 480) : 0; /* 96: a sombra do popup e um respiro */
}

static void menu_do_inicio(void);

static void menus_de_contexto(void) {
	enum gv_popup_lado lado;
	float altura_max;
	Clay_ElementId anc_item = gv_menu_ancora(CLAY_ID("menu-item")); /* declara o ponto: uma vez só por quadro */
	lado_do_menu(anc_item, &lado, &altura_max);
	GV_POPUP(CLAY_ID("menu-item"), anc_item, .lado = lado, .alinhar = GV_ALINHAR_INICIO, .largura = 310, .distancia = 2, .altura_max = altura_max) {
		CLAY(CLAY_ID("mi-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			menu_do_item();
		}
	} else {
		fechar_submenus(SUBMENUS_ITEM); /* fechado: a próxima abertura começa com os submenus recolhidos */
	}
	if (vista == V_INICIO) {
		Clay_ElementId anc_ini = gv_menu_ancora(CLAY_ID("menu-inicio"));
		lado_do_menu(anc_ini, &lado, &altura_max);
		GV_POPUP(CLAY_ID("menu-inicio"), anc_ini, .lado = lado, .alinhar = GV_ALINHAR_INICIO, .largura = 280, .distancia = 2, .altura_max = altura_max) {
			CLAY(CLAY_ID("mn-i-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
				menu_do_inicio();
			}
		}
	}
	Clay_ElementId anc_fundo = gv_menu_ancora(CLAY_ID("menu-fundo"));
	lado_do_menu(anc_fundo, &lado, &altura_max);
	GV_POPUP(CLAY_ID("menu-fundo"), anc_fundo, .lado = lado, .alinhar = GV_ALINHAR_INICIO, .largura = 280, .distancia = 2, .altura_max = altura_max) {
		CLAY(CLAY_ID("mf-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			menu_do_fundo();
		}
	} else {
		fechar_submenus(SUBMENUS_FUNDO);
	}
}

/* ---- janelas de diálogo (por cima de tudo): conflito, apagar para sempre, propriedades ---- */

static void dialogo(Clay_ElementId id, float largura, const char *titulo, void (*corpo)(void), void (*botoes)(void)) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 95, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
		CLAY(CLAY_IDI("dlg-cartao", id.id), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(largura), CLAY_SIZING_FIT(0) },
			.padding = CLAY_PADDING_ALL(22), .childGap = 14 }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
			.border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_destaque + 2, gv_t.texto, GV_PESO_MEDIO);
			corpo();
			CLAY(CLAY_IDI("dlg-botoes", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				botoes();
			}
		}
	}
}

static void texto_dialogo(const char *t) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
		gv_texto(gv_str(t), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

/* --- já existe --- */
static void corpo_conflito(void) {
	texto_dialogo(gv_fmt(op_st.pergunta_pasta_destino ? "Já existe uma pasta chamada \"%s\" no destino." : "Já existe um item chamado \"%s\" no destino.", op_st.pergunta_nome).chars);
	CLAY(CLAY_ID("cf-todos-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_interruptor(CLAY_ID("cf-todos"), &para_todos);
		gv_texto(CLAY_STRING("Fazer o mesmo para os próximos"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}
static void botoes_conflito(void) {
	if (gv_botao_ext(CLAY_ID("cf-pular"), &(gv_botao_opcoes){ .texto = "Pular", .estilo = GV_BOTAO_ELEVADO })) {
		ar_op_responder(op, AR_C_PULAR, para_todos);
	}
	if (gv_botao_ext(CLAY_ID("cf-ambos"), &(gv_botao_opcoes){ .texto = "Manter os dois", .estilo = GV_BOTAO_ELEVADO })) {
		ar_op_responder(op, AR_C_MANTER_AMBOS, para_todos);
	}
	if (gv_botao_ext(CLAY_ID("cf-substituir"), &(gv_botao_opcoes){ .texto = "Substituir", .estilo = GV_BOTAO_CHEIO })) {
		ar_op_responder(op, AR_C_SUBSTITUIR, para_todos);
	}
}

/* --- apagar para sempre --- */
static void corpo_apagar(void) {
	guint n = apagar_alvos ? apagar_alvos->len : 0;
	texto_dialogo(n == 1 ? "Este item será apagado para sempre. Não dá para desfazer." : gv_fmt("Estes %u itens serão apagados para sempre. Não dá para desfazer.", n).chars);
}
static void botoes_apagar(void) {
	if (gv_botao_ext(CLAY_ID("ap-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
		confirmar_apagar = false;
	}
	if (gv_botao_ext(CLAY_ID("ap-confirmar"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Apagar", .estilo = GV_BOTAO_PERIGO })) {
		confirmar_apagar = false;
		if (apagar_alvos) {
			iniciar_op(AR_OP_APAGAR, apagar_alvos, NULL);
		}
	}
}

static void corpo_esvaziar(void) {
	texto_dialogo("Tudo o que está na lixeira será apagado para sempre. Não dá para desfazer.");
}
static void esvaziar_agora(void) {
	char *erro = NULL;
	if (ar_lixeira_esvaziar(&erro)) {
		avisar("Lixeira esvaziada");
	} else {
		avisar("%s", erro ? erro : "Não consegui esvaziar");
	}
	g_free(erro);
	ar_desfazer_limpar();
	carregar(true);
}

static void botoes_esvaziar(void) {
	if (gv_botao_ext(CLAY_ID("ev-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
		confirmar_esvaziar = false;
	}
	if (gv_botao_ext(CLAY_ID("ev-confirmar"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Esvaziar", .estilo = GV_BOTAO_PERIGO })) {
		confirmar_esvaziar = false;
		esvaziar_agora();
	}
}

/* pedir para esvaziar: com confirmação, a menos que a pessoa a tenha desligado */
static void pedir_esvaziar(void) {
	if (cfg.confirmar_esvaziar) {
		confirmar_esvaziar = true;
	} else {
		esvaziar_agora();
	}
}

/* --- propriedades ---
 * Quatro abas (Geral, Permissões, Detalhes e, num atalho .desktop, Aplicativo), como no mockup. Abrir lê tudo uma vez (nada de ler o disco a cada quadro);
 * o tamanho de uma pasta é somado numa thread e aparece quando fica pronto. Aplicar renomeia, muda as permissões e o programa que abre o arquivo. */
#define PR_MAX_APPS 24
static int pr_aba;                    /* 0 Geral, 1 Permissões, 2 Detalhes, 3 Aplicativo */
static GPtrArray *pr_caminhos;        /* o que se está vendo (um item: o dele; vários: o total) */
static char pr_nome[256], pr_nome_orig[256];
static ar_entrada pr_e;
static struct stat pr_st;
static char pr_mime[128], pr_tipo_txt[96], pr_destino_link[1024];
static time_t pr_criado;
static int pr_nivel[3];               /* dono, grupo, outros: o nível de acesso escolhido */
static int pr_nivel_orig[3];
static bool pr_exec, pr_exec_orig;    /* arquivo: "executável" */
static ar_grupo pr_grupos[16];
static int pr_ngrupos, pr_grupo_i;
static bool pr_recursivo;
static bool pr_eh_app;
static ar_prop_app pr_app;
static GAppInfo *pr_apps[PR_MAX_APPS];
static int pr_napps, pr_app_i, pr_app_orig;
static bool pr_somente_leitura;       /* na lixeira ou em vários itens: só mostra */
/* o tamanho (somado numa thread) */
static uint32_t pr_geracao;
static bool pr_tam_pronto, pr_tam_completo;
static int64_t pr_tam_bytes;
static int pr_tam_itens;

typedef struct pr_tam_req {
	uint32_t geracao;
	GPtrArray *caminhos;
	int64_t bytes;
	int itens;
	bool completo;
} pr_tam_req;

static void pr_tam_fim(void *dados) {
	pr_tam_req *r = dados;
	if (r->geracao == pr_geracao && propriedades_abertas) {
		pr_tam_bytes = r->bytes;
		pr_tam_itens = r->itens;
		pr_tam_completo = r->completo;
		pr_tam_pronto = true;
		gv_app_redesenhar(app);
	}
	g_ptr_array_free(r->caminhos, TRUE);
	g_free(r);
}

static gpointer pr_tam_thread(gpointer dados) {
	pr_tam_req *r = dados;
	r->completo = true;
	for (guint i = 0; i < r->caminhos->len; i++) {
		const char *c = r->caminhos->pdata[i];
		struct stat st;
		if (lstat(c, &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			int64_t b;
			int it;
			if (!ar_tamanho_pasta(c, 200000, &b, &it)) {
				r->completo = false;
			}
			r->bytes += b;
			r->itens += it;
		} else {
			r->bytes += st.st_size;
		}
	}
	gv_na_linha_principal(pr_tam_fim, r);
	return NULL;
}

static void pr_liberar(void) {
	if (pr_caminhos) {
		g_ptr_array_free(pr_caminhos, TRUE);
		pr_caminhos = NULL;
	}
	for (int i = 0; i < pr_napps; i++) {
		g_clear_object(&pr_apps[i]);
	}
	pr_napps = 0;
}

static void pr_abrir(void) {
	pr_liberar();
	propriedades_abertas = true;
	pr_aba = 0;
	pr_recursivo = false;
	pr_eh_app = false;
	pr_tam_pronto = false;
	pr_geracao++;
	pr_caminhos = g_ptr_array_new_with_free_func(g_free);
	for (int i = 0; i < lista.n; i++) {
		if (sel[i] && cam[i]) {
			g_ptr_array_add(pr_caminhos, g_strdup(cam[i]));
		}
	}
	if (!pr_caminhos->len) {
		g_ptr_array_add(pr_caminhos, g_strdup(pasta));
	}
	pr_somente_leitura = pr_caminhos->len > 1 || em_lixeira();
	/* o tamanho, numa thread (uma pasta grande não trava a janela) */
	pr_tam_req *r = g_new0(pr_tam_req, 1);
	r->geracao = pr_geracao;
	r->caminhos = g_ptr_array_new_with_free_func(g_free);
	for (guint i = 0; i < pr_caminhos->len; i++) {
		g_ptr_array_add(r->caminhos, g_strdup(pr_caminhos->pdata[i]));
	}
	g_thread_unref(g_thread_new("atlas-tamanho", pr_tam_thread, r));
	if (pr_caminhos->len > 1) {
		return;
	}
	const char *c = pr_caminhos->pdata[0];
	memset(&pr_e, 0, sizeof(pr_e));
	if (!ar_entrada_de(c, &pr_e)) {
		pr_somente_leitura = true;
		return;
	}
	snprintf(pr_nome, sizeof(pr_nome), "%s", pr_e.nome);
	snprintf(pr_nome_orig, sizeof(pr_nome_orig), "%s", pr_e.nome);
	if (stat(c, &pr_st) != 0 && lstat(c, &pr_st) != 0) {
		memset(&pr_st, 0, sizeof(pr_st));
	}
	pr_criado = 0;
	struct statx sx;
	if (statx(AT_FDCWD, c, AT_SYMLINK_NOFOLLOW, STATX_BTIME, &sx) == 0 && (sx.stx_mask & STATX_BTIME)) {
		pr_criado = (time_t)sx.stx_btime.tv_sec;
	}
	pr_destino_link[0] = '\0';
	if (pr_e.link) {
		ssize_t n = readlink(c, pr_destino_link, sizeof(pr_destino_link) - 1);
		pr_destino_link[n > 0 ? n : 0] = '\0';
	}
	snprintf(pr_tipo_txt, sizeof(pr_tipo_txt), "%s", ar_tipo_nome(&pr_e));
	/* o tipo MIME e os aplicativos que o abrem */
	pr_mime[0] = '\0';
	if (!pr_e.pasta) {
		char *tipo = g_content_type_guess(c, NULL, 0, NULL);
		snprintf(pr_mime, sizeof(pr_mime), "%s", tipo ? tipo : "");
		g_free(tipo);
		GList *apps = pr_mime[0] ? g_app_info_get_all_for_type(pr_mime) : NULL;
		GAppInfo *padrao = pr_mime[0] ? g_app_info_get_default_for_type(pr_mime, FALSE) : NULL;
		pr_app_i = pr_app_orig = 0;
		for (GList *l = apps; l && pr_napps < PR_MAX_APPS; l = l->next) {
			pr_apps[pr_napps] = g_object_ref(l->data);
			if (padrao && g_app_info_equal(padrao, l->data)) {
				pr_app_i = pr_app_orig = pr_napps;
			}
			pr_napps++;
		}
		g_list_free_full(apps, g_object_unref);
		g_clear_object(&padrao);
	} else {
		snprintf(pr_mime, sizeof(pr_mime), "inode/directory");
	}
	/* permissões */
	int modo = (int)(pr_st.st_mode & 0777);
	for (int k = 0; k < 3; k++) {
		int rwx = (modo >> (6 - 3 * k)) & 7;
		pr_nivel[k] = pr_nivel_orig[k] = pr_e.pasta ? ar_nivel_pasta_de(rwx) : ar_nivel_arq_de(rwx);
	}
	pr_exec = pr_exec_orig = (modo & 0111) != 0;
	pr_ngrupos = ar_grupos_da_pessoa(pr_grupos, 16, pr_st.st_gid);
	pr_grupo_i = 0;
	/* um atalho de aplicativo ganha a aba "Aplicativo" */
	pr_eh_app = !pr_e.pasta && g_str_has_suffix(pr_e.nome, ".desktop") && ar_prop_desktop(c, &pr_app);
}

/* o modo (9 bits) que as escolhas dão */
static mode_t pr_modo_editado(void) {
	mode_t m = 0;
	for (int k = 0; k < 3; k++) {
		int rwx = pr_nivel[k] == (pr_e.pasta ? AR_NP_OUTRO : AR_NA_OUTRO) ? (int)((pr_st.st_mode >> (6 - 3 * k)) & 7)
			: pr_e.pasta ? ar_rwx_da_pasta(pr_nivel[k]) : ar_rwx_do_arq(pr_nivel[k]);
		if (!pr_e.pasta) {
			int x = pr_exec == pr_exec_orig ? (int)((pr_st.st_mode >> (6 - 3 * k)) & 1) : pr_exec ? 1 : 0;
			rwx = (rwx & 6) | x;
		}
		m |= (mode_t)(rwx << (6 - 3 * k));
	}
	return m;
}

typedef struct pr_aplic_req {
	char *caminho;
	mode_t modo;
	bool grupo;
	gid_t gid;
	bool recursivo;
	ar_prop_resultado r;
} pr_aplic_req;

static void pr_aplic_fim(void *dados) {
	pr_aplic_req *q = dados;
	if (q->r.erros) {
		avisar("Permissões aplicadas a %lld %s, %d com erro", (long long)q->r.alterados, q->r.alterados == 1 ? "item" : "itens", q->r.erros);
	} else {
		avisar("Permissões aplicadas a %lld %s", (long long)q->r.alterados, q->r.alterados == 1 ? "item" : "itens");
	}
	carregar(true);
	g_free(q->caminho);
	g_free(q);
}

static gpointer pr_aplic_thread(gpointer dados) {
	pr_aplic_req *q = dados;
	ar_prop_aplicar(q->caminho, q->modo, q->grupo, q->gid, q->recursivo, &q->r);
	gv_na_linha_principal(pr_aplic_fim, q);
	return NULL;
}

/* Aplicar: renomear, permissões e aplicativo padrão; o que não mudou não é tocado */
static void pr_aplicar(void) {
	if (pr_somente_leitura || !pr_caminhos || pr_caminhos->len != 1) {
		return;
	}
	char *c = g_strdup(pr_caminhos->pdata[0]);
	mode_t novo = pr_modo_editado();
	gid_t gid = pr_grupos[pr_grupo_i].gid;
	bool grupo = gid != pr_st.st_gid;
	if (novo != (pr_st.st_mode & 0777) || grupo || pr_recursivo) {
		pr_aplic_req *q = g_new0(pr_aplic_req, 1);
		q->caminho = g_strdup(c);
		q->modo = (mode_t)((pr_st.st_mode & 07000) | novo);
		q->grupo = grupo;
		q->gid = gid;
		q->recursivo = pr_recursivo && pr_e.pasta;
		g_thread_unref(g_thread_new("atlas-permissoes", pr_aplic_thread, q));
	}
	if (pr_napps && pr_app_i != pr_app_orig && pr_mime[0]) {
		GError *err = NULL;
		if (!g_app_info_set_as_default_for_type(pr_apps[pr_app_i], pr_mime, &err)) {
			avisar("Não consegui trocar o aplicativo: %s", err ? err->message : "?");
			g_clear_error(&err);
		}
	}
	if (strcmp(pr_nome, pr_nome_orig) && pr_nome[0]) {
		char *novo_caminho = NULL, *erro = NULL;
		if (ar_renomear(c, pr_nome, &novo_caminho, &erro)) {
			desfazer_ate = gv_agora() + 10;
			snprintf(desfazer_texto, sizeof(desfazer_texto), "Renomear");
			avisar("Renomeado");
			carregar(true);
			selecionar_caminho(novo_caminho);
		} else {
			avisar("%s", erro ? erro : "Não consegui renomear");
		}
		g_free(novo_caminho);
		g_free(erro);
	}
	g_free(c);
	propriedades_abertas = false;
}

static void linha_prop(const char *rotulo, const char *valor) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(valor), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
		}
	}
}

static void titulo_secao(const char *t) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 0 } } }) {
		gv_texto(gv_str(t), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_MEDIO);
	}
}

static const char *pr_data(time_t t) {
	if (!t) {
		return "—";
	}
	struct tm tm;
	localtime_r(&t, &tm);
	char b[48];
	strftime(b, sizeof(b), "%d/%m/%Y, %H:%M", &tm);
	return gv_str(b).chars;
}

static void pr_cabecalho(void) {
	CLAY(CLAY_ID("pr-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		if (pr_caminhos && pr_caminhos->len == 1) {
			if (pr_eh_app) {
				gv_icone_app(pr_app.icone, 48);
			} else {
				ic_dados *d = &ic_pool[ic_n++ % (int)(sizeof(ic_pool) / sizeof(ic_pool[0]))];
				d->tipo = pr_e.pasta ? AR_PASTA : pr_e.tipo;
				d->link = pr_e.link;
				d->cor = 0;
				d->nprevia = 0;
				char emb[32] = "";
				if (pr_e.pasta) {
					ar_pasta_estilo(pr_caminhos->pdata[0], &d->cor, emb, sizeof(emb));
				}
				ar_selo(&pr_e, d->selo, sizeof(d->selo));
				CLAY(CLAY_ID("pr-icone"), gv_livre(desenhar_icone, d, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(52), CLAY_SIZING_FIXED(52) } } })) {}
			}
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_str(pr_eh_app && pr_app.nome[0] ? pr_app.nome : pr_e.nome), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_str(pr_eh_app && pr_app.comentario[0] ? pr_app.comentario : pr_tipo_txt), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		} else {
			gv_icone("folder", 40, gv_t.texto_secundario);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_str(pr_caminhos && pr_caminhos->len > 1 ? gv_fmt("%u itens selecionados", pr_caminhos->len).chars : "Propriedades"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			}
		}
	}
}

static void pr_abas(void) {
	static const char *const NOMES[] = { "Geral", "Permissões", "Detalhes", "Aplicativo" };
	CLAY(CLAY_ID("pr-abas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 }, .border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		for (int i = 0; i < 4; i++) {
			if (i == 3 && !pr_eh_app) {
				continue;
			}
			if (i > 0 && pr_caminhos && pr_caminhos->len > 1) {
				continue; /* vários itens: só o resumo */
			}
			Clay_ElementId id = CLAY_IDI("pr-aba", (uint32_t)i);
			bool atual = pr_aba == i;
			CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = { 14, 14, 8, 0 }, .childGap = 6, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				gv_texto(gv_str(NOMES[i]), gv_t.tamanho_corpo, atual || gv_por_cima(id) ? gv_t.texto : gv_t.texto_secundario, atual ? GV_PESO_MEDIO : GV_PESO_NORMAL);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(2) } }, .backgroundColor = atual ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 } }) {}
			}
			if (gv_tocado(id)) {
				pr_aba = i;
			}
		}
	}
}

static const char *pr_tamanho_texto(void) {
	if (!pr_tam_pronto) {
		return "Calculando…";
	}
	char t[32];
	ar_tamanho_humano(pr_tam_bytes, t, sizeof(t));
	if (pr_e.pasta || (pr_caminhos && pr_caminhos->len > 1)) {
		return gv_fmt("%s%s · %d %s dentro", pr_tam_completo ? "" : "mais de ", t, pr_tam_itens, pr_tam_itens == 1 ? "item" : "itens").chars;
	}
	return gv_fmt("%s (%lld bytes)", t, (long long)pr_e.tamanho).chars;
}

static void pr_aba_geral(void) {
	if (pr_caminhos && pr_caminhos->len > 1) {
		linha_prop("Selecionados", gv_fmt("%u itens", pr_caminhos->len).chars);
		linha_prop("Tamanho total", pr_tamanho_texto());
		return;
	}
	const char *c = pr_caminhos->pdata[0];
	titulo_secao("Identificação");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
			gv_texto(CLAY_STRING("Nome"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			if (pr_somente_leitura) {
				gv_texto(gv_str(pr_e.nome), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			} else if (gv_campo(CLAY_ID("pr-nome"), pr_nome, sizeof(pr_nome), "Nome", GV_CAMPO_TEXTO) & GV_CAMPO_ENTER) {
				pr_aplicar();
			}
		}
	}
	char *pai = ar_pai(c);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(122) } } }) {
			gv_texto(CLAY_STRING("Local"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("pr-local"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 12, 12, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(pai ? pai : "/"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_botao_ext(CLAY_ID("pr-copiar"), &(gv_botao_opcoes){ .icone = "copy", .estilo = GV_BOTAO_ELEVADO, .altura = 40 })) {
			gv_copiar(pai ? pai : "/");
			avisar("Caminho copiado");
		}
		if (gv_botao_ext(CLAY_ID("pr-abrir-pasta"), &(gv_botao_opcoes){ .texto = "Abrir pasta", .estilo = GV_BOTAO_ELEVADO, .altura = 40 })) {
			char *destino = g_strdup(pai ? pai : "/");
			propriedades_abertas = false;
			ir_para(destino, true);
			g_free(destino);
		}
	}
	g_free(pai);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
	linha_prop("Tipo", pr_tipo_txt);
	linha_prop("Tamanho", pr_tamanho_texto());
	linha_prop("Criado em", pr_data(pr_criado));
	linha_prop("Modificado em", pr_data(pr_e.mtime));
	if (!pr_e.pasta && pr_napps && !pr_somente_leitura) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
				gv_texto(CLAY_STRING("Abrir com"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			}
			const char *nomes[PR_MAX_APPS];
			for (int i = 0; i < pr_napps; i++) {
				nomes[i] = gv_str(g_app_info_get_display_name(pr_apps[i])).chars;
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_lista_suspensa(CLAY_ID("pr-abrir-com"), nomes, pr_napps, &pr_app_i);
			}
		}
	}
}

static void pr_aba_permissoes(void) {
	if (pr_e.pasta) {
		titulo_secao("Acesso à pasta");
	} else {
		titulo_secao("Acesso ao arquivo");
	}
	/* dono e grupo */
	struct passwd *pw = getpwuid(pr_st.st_uid);
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
			gv_texto(CLAY_STRING("Proprietário"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		gv_texto(gv_str(pw ? pw->pw_name : "?"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
	}
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(130) } } }) {
			gv_texto(CLAY_STRING("Grupo"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		const char *gn[16];
		for (int i = 0; i < pr_ngrupos; i++) {
			gn[i] = gv_str(pr_grupos[i].nome).chars;
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			if (pr_somente_leitura) {
				gv_texto(gv_str(pr_grupos[0].nome), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			} else {
				gv_lista_suspensa(CLAY_ID("pr-grupo"), gn, pr_ngrupos, &pr_grupo_i);
			}
		}
	}
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
	static const char *const QUEM[3] = { "Proprietário", "Grupo", "Outros" };
	static const char *const ICONES[3] = { "user", "users", "globe" };
	for (int k = 0; k < 3; k++) {
		CLAY(CLAY_IDI("pr-linha", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone(ICONES[k], 20, gv_t.texto_secundario);
				gv_texto(gv_str(QUEM[k]), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			const char *op[5];
			int n;
			int rwx_atual = (int)((pr_st.st_mode >> (6 - 3 * k)) & 7);
			char outro[24];
			if (pr_e.pasta) {
				for (n = 0; n < 4; n++) {
					op[n] = AR_NIVEIS_PASTA[n];
				}
			} else {
				for (n = 0; n < 3; n++) {
					op[n] = AR_NIVEIS_ARQ[n];
				}
			}
			if (pr_nivel_orig[k] == (pr_e.pasta ? AR_NP_OUTRO : AR_NA_OUTRO)) { /* o que já era, sem nome */
				snprintf(outro, sizeof(outro), "%c%c%c", rwx_atual & 4 ? 'r' : '-', rwx_atual & 2 ? 'w' : '-', rwx_atual & 1 ? 'x' : '-');
				op[n++] = gv_str(outro).chars;
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				if (pr_somente_leitura) {
					gv_texto(gv_str(op[pr_nivel[k] < n ? pr_nivel[k] : 0]), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
				} else {
					gv_lista_suspensa(CLAY_IDI("pr-nivel", (uint32_t)k), op, n, &pr_nivel[k]);
				}
			}
		}
	}
	if (!pr_e.pasta && !pr_somente_leitura) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_interruptor(CLAY_ID("pr-exec"), &pr_exec);
			gv_texto(CLAY_STRING("Permitir executar como programa"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
	mode_t m = pr_modo_editado();
	char perm[16];
	ar_permissoes_texto(m, perm, sizeof(perm));
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		gv_texto(CLAY_STRING("Modo atual"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(gv_fmt("%03o  (%s)", (unsigned)(m & 0777), perm), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
	}
	if (pr_e.pasta && !pr_somente_leitura) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_interruptor(CLAY_ID("pr-recursivo"), &pr_recursivo);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Aplicar também aos itens da pasta"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
				gv_texto(CLAY_STRING("Inclui arquivos e subpastas."), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
	}
}

static void pr_aba_detalhes(void) {
	const char *c = pr_caminhos->pdata[0];
	linha_prop("Caminho completo", c);
	linha_prop("Tipo MIME", pr_mime[0] ? pr_mime : "—");
	if (pr_destino_link[0]) {
		linha_prop("Aponta para", pr_destino_link);
	}
	struct passwd *pw = getpwuid(pr_st.st_uid);
	linha_prop("Dono", pr_st.st_uid == getuid() ? gv_fmt("Você (%s)", pw ? pw->pw_name : "?").chars : pw ? pw->pw_name : "?");
	linha_prop("Acessado em", pr_data(pr_st.st_atime));
	linha_prop("Alterado em", pr_data(pr_st.st_ctime));
	linha_prop("Links", gv_fmt("%lu", (unsigned long)pr_st.st_nlink).chars);
	linha_prop("Inode", gv_fmt("%lu", (unsigned long)pr_st.st_ino).chars);
	linha_prop("Ocupa no disco", gv_fmt("%lld bytes", (long long)pr_st.st_blocks * 512).chars);
}

static void pr_aba_aplicativo(void) {
	titulo_secao("Informações do aplicativo");
	linha_prop("Versão", pr_app.versao[0] ? pr_app.versao : "—");
	linha_prop("Pacote", pr_app.pacote[0] ? pr_app.pacote : "—");
	linha_prop("Origem", pr_app.origem[0] ? pr_app.origem : "—");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
	titulo_secao("Instalação");
	const char *c = pr_caminhos->pdata[0];
	for (int i = 0; i < 2; i++) {
		const char *rot = i == 0 ? "Executável" : "Atalho do aplicativo";
		const char *val = i == 0 ? (pr_app.executavel[0] ? pr_app.executavel : pr_app.exec) : c;
		CLAY(CLAY_IDI("pr-inst", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
				gv_texto(gv_str(rot), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 12, 12, 0, 0 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(val), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao_ext(CLAY_IDI("pr-inst-c", (uint32_t)i), &(gv_botao_opcoes){ .icone = "copy", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
				gv_copiar(val);
				avisar("Copiado");
			}
		}
	}
	if (pr_app.executavel[0] && gv_botao_ext(CLAY_ID("pr-local-exec"), &(gv_botao_opcoes){ .icone = "folder", .texto = "Abrir local do executável", .estilo = GV_BOTAO_ELEVADO, .altura = 40 })) {
		char *pai = g_path_get_dirname(pr_app.executavel);
		propriedades_abertas = false;
		ir_para(pai, true);
		g_free(pai);
	}
}

static void corpo_propriedades(void) {
	if (!pr_caminhos) {
		return;
	}
	pr_cabecalho();
	pr_abas();
	CLAY(CLAY_ID("pr-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 6 }, .childGap = 10 } }) {
		if (pr_aba == 1 && pr_caminhos->len == 1) {
			pr_aba_permissoes();
		} else if (pr_aba == 2 && pr_caminhos->len == 1) {
			pr_aba_detalhes();
		} else if (pr_aba == 3 && pr_eh_app) {
			pr_aba_aplicativo();
		} else {
			pr_aba_geral();
		}
	}
}

static void botoes_propriedades(void) {
	if (pr_somente_leitura) {
		if (gv_botao_ext(CLAY_ID("pr-fechar"), &(gv_botao_opcoes){ .texto = "Fechar", .estilo = GV_BOTAO_CHEIO })) {
			propriedades_abertas = false;
		}
		return;
	}
	if (gv_botao_ext(CLAY_ID("pr-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
		propriedades_abertas = false;
	}
	if (gv_botao_ext(CLAY_ID("pr-aplicar"), &(gv_botao_opcoes){ .texto = "Aplicar", .estilo = GV_BOTAO_CHEIO })) {
		pr_aplicar();
	}
}

/* --- git clone --- */
static void iniciar_clone(void) {
	char *u = g_strstrip(g_strdup(clonar_url));
	if (!*u) {
		g_free(u);
		return;
	}
	clonar_aberto = false;
	GPtrArray *v = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(v, u);
	iniciar_op(AR_OP_CLONAR, v, pasta);
	g_ptr_array_free(v, TRUE);
}
static void corpo_clone(void) {
	texto_dialogo(gv_fmt("O repositório vira uma pasta nova em \"%s\".", pasta).chars);
	int r = gv_campo(CLAY_ID("cl-url"), clonar_url, sizeof(clonar_url), "https://github.com/usuario/projeto.git", GV_CAMPO_URL);
	if (focar_clonar) {
		gv_focar(CLAY_ID("cl-url"));
		focar_clonar = false;
	}
	if (r & GV_CAMPO_ENTER) {
		iniciar_clone();
	}
	texto_dialogo("Repositórios que pedem senha ainda não são suportados.");
}
static void botoes_clone(void) {
	if (gv_botao_ext(CLAY_ID("cl-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
		clonar_aberto = false;
	}
	if (gv_botao_ext(CLAY_ID("cl-ok"), &(gv_botao_opcoes){ .icone = "code", .texto = "Clonar", .estilo = GV_BOTAO_CHEIO, .desativado = !clonar_url[0] })) {
		iniciar_clone();
	}
}

static void dialogos(void) {
	if (op && op_st.pergunta) {
		dialogo(CLAY_ID("dlg-conflito"), 460, "Já existe", corpo_conflito, botoes_conflito);
	} else if (confirmar_apagar) {
		dialogo(CLAY_ID("dlg-apagar"), 440, "Apagar para sempre?", corpo_apagar, botoes_apagar);
	} else if (confirmar_esvaziar) {
		dialogo(CLAY_ID("dlg-esvaziar"), 440, "Esvaziar a lixeira?", corpo_esvaziar, botoes_esvaziar);
	} else if (clonar_aberto) {
		dialogo(CLAY_ID("dlg-clone"), 520, "Git clone", corpo_clone, botoes_clone);
	} else if (propriedades_abertas) {
		dialogo(CLAY_ID("dlg-props"), 600, "Propriedades", corpo_propriedades, botoes_propriedades);
	}
}

/* ---- as sugestões do caminho (flutuam por baixo do campo) ---- */

static void sugestoes_do_caminho(void) {
	if (!editando_caminho || !n_sug) {
		return;
	}
	bool ir_sugestao = false;
	Clay_BoundingBox c = gv_caixa(CLAY_ID("caminho"));
	if (c.width <= 0) {
		return;
	}
	CLAY(CLAY_ID("sugestoes"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(c.width), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(8), .childGap = 2 },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 90, .offset = { c.x, c.y + c.height + 6 } },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		bool tem_pastas = false, tem_recentes = false;
		for (int i = 0; i < n_sug; i++) {
			tem_pastas |= !sug[i].recente;
			tem_recentes |= sug[i].recente;
		}
		for (int passo = 0; passo < 2; passo++) {
			if (passo == 0 ? !tem_pastas : !tem_recentes) {
				continue;
			}
			CLAY(CLAY_IDI("sug-titulo", (uint32_t)passo), { .layout = { .padding = { 8, 8, 6, 4 } } }) {
				gv_texto(passo == 0 ? CLAY_STRING("PASTAS CORRESPONDENTES") : CLAY_STRING("RECENTES"), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_MEDIO);
			}
			for (int i = 0; i < n_sug; i++) {
				if (sug[i].recente != (passo == 1)) {
					continue;
				}
				Clay_ElementId id = CLAY_IDI("sug", (uint32_t)i);
				bool marcado = i == sug_sel || gv_por_cima(id);
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 10, 10, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = marcado ? gv_t.selecionado : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
					gv_icone("folder", 22, gv_t.texto_secundario);
					CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
						gv_texto(gv_str(sug[i].nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
						gv_texto(gv_str(sug[i].caminho), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
					}
				}
				if (gv_tocado(id)) {
					sug_sel = i;
					ir_sugestao = true; /* não dá para sair daqui: o Clay quer os elementos fechados */
				}
			}
		}
		CLAY(CLAY_ID("sug-dicas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 8, 8, 2 }, .childGap = 18 }, .border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
			gv_texto(CLAY_STRING("Enter  Abrir"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Tab  Completar"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Esc  Cancelar"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	if (ir_sugestao) {
		editar_caminho_ir();
	}
}

/* ---- teclado ---- */

static void mover_foco(int dk) {
	if (!nvis) {
		return;
	}
	int atual = foco >= 0 ? pos_em_vis(foco) : -1;
	int novo = atual < 0 ? (dk > 0 ? 0 : nvis - 1) : atual + dk;
	novo = novo < 0 ? 0 : novo >= nvis ? nvis - 1 : novo;
	uint32_t mods = gv_modificadores();
	int idx = vis[novo];
	if (mods & GV_MOD_SHIFT && ancora >= 0) {
		clicar_item(idx, GV_MOD_SHIFT);
		foco = idx;
	} else {
		limpar_selecao();
		sel[idx] = true;
		ancora = foco = idx;
	}
	/* mantém o item à vista */
	float ch = modo_lista ? altura_linha() : tam_icone() + 14 + 10 + 6 + (gv_t.tamanho_corpo + 6) * 2 + 20;
	int linha = modo_lista ? novo : novo / colunas;
	float topo = gv_rolagem_posicao(ID_CONTEUDO).y, alt = gv_caixa(ID_CONTEUDO).height;
	if (linha * ch < topo) {
		gv_rolar_para(ID_CONTEUDO, 0, linha * ch, true);
	} else if ((linha + 1) * ch > topo + alt - 12) {
		gv_rolar_para(ID_CONTEUDO, 0, (linha + 1) * ch - alt + 12, true);
	}
}

static void teclado_atalhos(void) {
	bool digitando = gv_focado(CLAY_ID("busca")) || gv_focado(CLAY_ID("mn-nome")) || gv_focado(CLAY_ID("renome")) || gv_focado(CLAY_ID("caminho-campo")) || gv_focado(CLAY_ID("cl-url"));
	uint32_t mods = gv_modificadores();
	bool dialogo_aberto = (op && op_st.pergunta) || confirmar_apagar || confirmar_esvaziar || propriedades_abertas || clonar_aberto || rede_dialogo_aberto();
	for (int i = 0;; i++) {
		uint32_t k = gv_tecla_n(i);
		if (!k) {
			break;
		}
		if (dialogo_aberto) {
			if (k == XKB_KEY_Escape) {
				if (op && op_st.pergunta) {
					ar_op_cancelar(op);
				}
				confirmar_apagar = confirmar_esvaziar = propriedades_abertas = clonar_aberto = false;
			}
			continue;
		}
		if (renomeando >= 0) {
			if (k == XKB_KEY_Escape) {
				renomeando = -1;
			}
			continue;
		}
		if (editando_caminho) {
			if (k == XKB_KEY_Tab || k == XKB_KEY_Up || k == XKB_KEY_Down) {
				gv_teclado_cru(); /* só nestas: o Tab é do caminho (completar) e não anda o foco; o cru inteiro calaria o Enter do campo */
			}
			if (k == XKB_KEY_Escape) {
				editar_caminho_acabar();
			} else if (k == XKB_KEY_Down && n_sug) {
				sug_sel = sug_sel + 1 >= n_sug ? 0 : sug_sel + 1;
			} else if (k == XKB_KEY_Up && n_sug) {
				sug_sel = sug_sel <= 0 ? n_sug - 1 : sug_sel - 1;
			} else if (k == XKB_KEY_Tab) {
				editar_caminho_completar();
			}
			continue;
		}
		if (mods & GV_MOD_ALT) {
			if (k == XKB_KEY_Left) {
				voltar();
			} else if (k == XKB_KEY_Right) {
				avancar();
			} else if (k == XKB_KEY_Up) {
				subir();
			}
			continue;
		}
		if (mods & GV_MOD_CTRL) {
			if (k == XKB_KEY_t || k == XKB_KEY_T) {
				aba_nova(NULL);
				continue;
			}
			if (k == XKB_KEY_w || k == XKB_KEY_W) {
				if (n_abas > 1) {
					aba_fechar(aba_atual);
				} else {
					gv_app_sair(app);
				}
				continue;
			}
			if (k >= XKB_KEY_1 && k <= XKB_KEY_9) { /* o Alt+número é das áreas de trabalho do Orbit; aqui é Ctrl+número, e o 9 é a última aba */
				aba_mostrar(k == XKB_KEY_9 ? n_abas - 1 : (int)(k - XKB_KEY_1));
				continue;
			}
			if (k == XKB_KEY_Tab || k == XKB_KEY_ISO_Left_Tab || k == XKB_KEY_Page_Down || k == XKB_KEY_Page_Up) {
				bool volta = k == XKB_KEY_ISO_Left_Tab || k == XKB_KEY_Page_Up || (k == XKB_KEY_Tab && (mods & GV_MOD_SHIFT));
				if (k == XKB_KEY_Tab || k == XKB_KEY_ISO_Left_Tab) {
					gv_teclado_cru(); /* o Ctrl+Tab é das abas e não anda o foco */
				}
				aba_mostrar((aba_atual + (volta ? n_abas - 1 : 1)) % n_abas);
				continue;
			}
			if (k == XKB_KEY_f) {
				gv_focar(CLAY_ID("busca"));
			} else if (k == XKB_KEY_l) {
				editar_caminho_comecar();
			} else if (k == XKB_KEY_r) {
				carregar(true);
			} else if (k == XKB_KEY_z && !digitando) {
				desfazer_ultimo();
			} else if ((k == XKB_KEY_n || k == XKB_KEY_N) && (mods & GV_MOD_SHIFT) && vista == V_PASTA && !em_lixeira()) {
				novo_tipo = 1;
				focar_nome = true;
				snprintf(novo_nome, sizeof(novo_nome), "Nova pasta");
				gv_popup_abrir(CLAY_ID("menu-novo"));
			} else if (!digitando) {
				if (k == XKB_KEY_h) {
					cfg.ocultos = !cfg.ocultos;
					carregar(true);
				} else if (k == XKB_KEY_a) {
					for (int j = 0; j < nvis; j++) {
						sel[vis[j]] = true;
					}
				} else if (k == XKB_KEY_c) {
					copiar_selecao(false);
				} else if (k == XKB_KEY_x && !em_lixeira()) {
					copiar_selecao(true);
				} else if (k == XKB_KEY_v) {
					colar();
				} else if (k == XKB_KEY_i && n_selecionados() + (vista == V_PASTA ? 1 : 0) > 0) {
					pr_abrir();
				}
			}
			continue;
		}
		if (k == XKB_KEY_Escape) {
			if (busca[0]) {
				busca[0] = '\0';
				refiltrar();
			} else {
				limpar_selecao();
			}
			continue;
		}
		if (digitando) {
			continue;
		}
		int passo = colunas > 0 && !modo_lista ? colunas : 1;
		switch (k) {
		case XKB_KEY_Left: mover_foco(-1); break;
		case XKB_KEY_Right: mover_foco(1); break;
		case XKB_KEY_Up: mover_foco(-(modo_lista ? 1 : passo)); break;
		case XKB_KEY_Down: mover_foco(modo_lista ? 1 : passo); break;
		case XKB_KEY_Home: foco = -1; mover_foco(1); break;
		case XKB_KEY_End: foco = -1; mover_foco(-1); break;
		case XKB_KEY_Return:
		case XKB_KEY_KP_Enter:
			for (int j = 0; j < lista.n; j++) {
				if (sel[j]) {
					abrir_item(j);
					break;
				}
			}
			break;
		case XKB_KEY_BackSpace: subir(); break;
		case XKB_KEY_F2:
			if (n_selecionados() == 1) {
				iniciar_renomear(primeiro_selecionado());
			}
			break;
		case XKB_KEY_Delete:
		case XKB_KEY_KP_Delete:
			apagar_selecao(mods & GV_MOD_SHIFT);
			break;
		default: break;
		}
	}
}

/* ---- a página inicial (Início) ---- */

static void emblema_do_item(const ar_barra_item *it, char *saida, size_t tam) {
	snprintf(saida, tam, "%s", !strcmp(it->icone, "folder") ? "" : it->icone);
}

/* um cartão de pasta principal: a pasta lilás com o emblema do que é, e o nome embaixo */
static bool cartao_pasta(Clay_ElementId id, const ar_barra_item *it, float larg) {
	bool sobre = gv_por_cima(id);
	ic_dados *d = &ic_pool[ic_n++ % (int)(sizeof(ic_pool) / sizeof(ic_pool[0]))];
	d->tipo = AR_PASTA;
	d->link = false;
	d->cor = 0;
	char emblema[32];
	emblema_do_item(it, emblema, sizeof(emblema));
	ar_pasta_estilo(it->caminho, &d->cor, d->selo, 1); /* só a cor: o emblema é o do item */
	d->selo[0] = '\0';
	float tam = larg * 0.5f;
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(larg), CLAY_SIZING_FIT(0) }, .padding = { 8, 8, 12, 10 }, .childGap = 6,
		.childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .backgroundColor = sobre ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		CLAY(CLAY_IDI("cp-ic", id.id), gv_livre(desenhar_icone, d, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(tam), CLAY_SIZING_FIXED(tam) } } })) {
			if (emblema[0]) {
				CLAY_AUTO_ID({ .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { .element = CLAY_ATTACH_POINT_CENTER_CENTER, .parent = CLAY_ATTACH_POINT_CENTER_CENTER },
					.offset = { 0, tam * 0.08f }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {
					gv_icone(emblema, tam * 0.34f, (Clay_Color){ 255, 255, 255, 235 });
				}
			}
		}
		gv_texto(gv_str(it->rotulo), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
	}
	return gv_tocado(id);
}

/* ---- discos e pendrives ligados que ainda não estão montados (a página inicial) ---- */

static struct disco disc_lista[ARMAZ_MAX_DISCOS];
static int disc_n;
static bool disc_lendo;
static double disc_lido_em = -100;
static char disc_montando[48];  /* o /dev do que está sendo montado agora ("": nenhum) */

typedef struct {
	struct disco v[ARMAZ_MAX_DISCOS];
	int n;
} disc_leitura;

static void disc_ler_fim(void *dados) {
	disc_leitura *l = dados;
	memcpy(disc_lista, l->v, sizeof(struct disco) * (size_t)l->n);
	disc_n = l->n;
	disc_lendo = false;
	disc_lido_em = gv_agora();
	g_free(l);
	gv_app_redesenhar(app);
}

static gpointer disc_ler_thread(gpointer dados) {
	disc_leitura *l = dados;
	l->n = ar_discos_para_montar(l->v, ARMAZ_MAX_DISCOS);
	gv_na_linha_principal(disc_ler_fim, l);
	return NULL;
}

/* a cada poucos segundos, enquanto a página inicial está aberta: o que foi ligado ou tirado aparece sozinho (o lsblk roda numa thread) */
static void discos_rever(void) {
	if (!disc_lendo && gv_agora() - disc_lido_em > 3) {
		disc_lendo = true;
		g_thread_unref(g_thread_new("atlas-discos", disc_ler_thread, g_new0(disc_leitura, 1)));
	}
	gv_acordar_em(3.2);
}

typedef struct {
	struct disco d;
	char *ponto;      /* onde ficou montado (g_free); NULL se falhou */
	char erro[300];
} disc_montagem;

static void disc_montou_fim(void *dados) {
	disc_montagem *m = dados;
	disc_montando[0] = '\0';
	disc_lido_em = -100; /* reler já: o disco sai da lista dos "para montar" */
	if (m->ponto) {
		ir_para(m->ponto, true);
	} else {
		avisar("Não consegui montar %s: %s", m->d.nome, m->erro);
	}
	g_free(m->ponto);
	g_free(m);
}

static gpointer disc_montar_thread(gpointer dados) {
	disc_montagem *m = dados;
	m->ponto = ar_disco_montar(&m->d, m->erro, sizeof(m->erro));
	gv_na_linha_principal(disc_montou_fim, m);
	return NULL;
}

static void disc_montar(const struct disco *d) {
	if (disc_montando[0]) {
		return;
	}
	snprintf(disc_montando, sizeof(disc_montando), "%s", d->dev);
	disc_montagem *m = g_new0(disc_montagem, 1);
	m->d = *d;
	g_thread_unref(g_thread_new("atlas-montar", disc_montar_thread, m));
}

typedef struct {
	char ponto[4096];
	char erro[300];
	bool ok;
	char nome[96];
} disc_ejecao;

static void disc_ejetou_fim(void *dados) {
	disc_ejecao *e = dados;
	if (e->ok) {
		avisar("%s pode ser retirado.", e->nome);
	} else {
		avisar("Não consegui ejetar %s: %s", e->nome, e->erro);
	}
	disc_lido_em = -100;
	g_free(e);
}

static gpointer disc_ejetar_thread(gpointer dados) {
	disc_ejecao *e = dados;
	struct disco d;
	if (!ar_disco_do_ponto(e->ponto, &d)) {
		snprintf(e->erro, sizeof(e->erro), "este não é um disco que se ejeta.");
	} else {
		snprintf(e->nome, sizeof(e->nome), "%s", d.nome);
		e->ok = armazenamento_ejetar(&d, e->erro, sizeof(e->erro)) == 0;
	}
	gv_na_linha_principal(disc_ejetou_fim, e);
	return NULL;
}

/* ejeta o que está montado em ponto (a pasta de agora sai de dentro dele antes) */
static void disc_ejetar(const char *ponto) {
	if (!strncmp(pasta, ponto, strlen(ponto))) {
		ir_para(g_get_home_dir(), true);
	}
	disc_ejecao *e = g_new0(disc_ejecao, 1);
	snprintf(e->ponto, sizeof(e->ponto), "%s", ponto);
	snprintf(e->nome, sizeof(e->nome), "O disco");
	g_thread_unref(g_thread_new("atlas-ejetar", disc_ejetar_thread, e));
}

/* um disco ligado que ainda não está montado: tocar monta e abre */
static bool cartao_disco(Clay_ElementId id, const struct disco *d, float larg) {
	bool sobre = gv_por_cima(id);
	bool montando = !strcmp(disc_montando, d->dev);
	char tam[32];
	ar_tamanho_humano(d->tamanho, tam, sizeof(tam));
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(larg), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = sobre ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_icone(d->removivel ? "usb" : "drive", 40, gv_t.texto_secundario);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
			gv_texto(gv_str(d->nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_fmt("%s · %s · não montado", d->fs, tam), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .padding = { 12, 12, 6, 6 } }, .backgroundColor = montando ? gv_t.selecionado : gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
			gv_texto(gv_str(montando ? "Montando…" : "Montar"), gv_t.tamanho_corpo - 1, montando ? gv_t.texto : gv_t.sobre_acento, GV_PESO_MEDIO);
		}
	}
	return gv_tocado(id);
}

static bool cartao_unidade(Clay_ElementId id, const ar_unidade *u, float larg) {
	bool sobre = gv_por_cima(id);
	float prop = u->total > 0 ? (float)((double)u->usado / (double)u->total) : 0;
	char usado[32], total[32];
	ar_tamanho_humano(u->usado, usado, sizeof(usado));
	ar_tamanho_humano(u->total, total, sizeof(total));
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(larg), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = sobre ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_icone(u->removivel ? "usb" : "drive", 40, gv_t.texto_secundario);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
			gv_texto(gv_str(u->rotulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(u->ponto), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(6) } }, .backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_PERCENT(prop < 0.02f ? 0.02f : prop > 1 ? 1 : prop), CLAY_SIZING_GROW(0) } }, .backgroundColor = prop > 0.9f ? gv_t.perigo : gv_t.acento,
					.cornerRadius = CLAY_CORNER_RADIUS(3) }) {}
			}
			gv_texto(gv_fmt("%s de %s usados", usado, total), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	return gv_tocado(id);
}

static int inicio_menu_item = -1;       /* o cartão de pasta do menu de contexto da página inicial (índice na barra lateral) */
static char inicio_menu_caminho[4096];  /* ou o ponto de montagem do cartão de unidade */
static bool inicio_menu_removivel;      /* e se essa unidade é um pendrive ou disco externo (tem "Ejetar") */

/* o menu de um cartão do Início: abrir (aqui, em aba, em janela) e, nas pastas, tirar da página */
static void menu_do_inicio(void) {
	const char *alvo = inicio_menu_item >= 0 && inicio_menu_item < barra.n ? barra.v[inicio_menu_item].caminho : inicio_menu_caminho;
	if (!alvo[0]) {
		return;
	}
	char *copia = g_strdup(alvo); /* a barra pode mudar embaixo do menu */
	if (gv_menu_item(CLAY_ID("mn-i-abrir"), "folder", "Abrir", "Enter")) {
		gv_popup_fechar(CLAY_ID("menu-inicio"));
		ir_para(copia, true);
	}
	if (gv_menu_item(CLAY_ID("mn-i-aba"), "plus", "Abrir em nova aba", NULL)) {
		gv_popup_fechar(CLAY_ID("menu-inicio"));
		aba_nova(copia);
	}
	if (gv_menu_item(CLAY_ID("mn-i-janela"), "external", "Abrir em nova janela", NULL)) {
		gv_popup_fechar(CLAY_ID("menu-inicio"));
		abrir_nova_janela(copia);
	}
	if (inicio_menu_item < 0 && inicio_menu_removivel) {
		separador("mn-i-sep2", 1);
		if (gv_menu_item(CLAY_ID("mn-i-ejetar"), "usb", "Ejetar", NULL)) {
			gv_popup_fechar(CLAY_ID("menu-inicio"));
			disc_ejetar(copia);
		}
	}
	if (inicio_menu_item >= 0 && inicio_menu_item < barra.n) {
		separador("mn-i-sep", 1);
		if (gv_menu_item(CLAY_ID("mn-i-remover"), "close", "Remover da página inicial", NULL)) {
			barra.v[inicio_menu_item].no_inicio = false;
			ar_barra_gravar(&barra);
			inicio_menu_item = -1;
			gv_popup_fechar(CLAY_ID("menu-inicio"));
		}
	}
	g_free(copia);
}

/* o conteúdo da página inicial; preview: a versão pequena da página de configurações (nada clica) */
static void inicio_conteudo(const ar_barra *b, const ar_cfg *c, bool preview, float largura) {
	float larg_cartao = preview ? 86 : 150;
	int por_linha = (int)((largura - 4) / (larg_cartao + 14));
	por_linha = por_linha < 1 ? 1 : por_linha;
	int idx[AR_BARRA_MAX], n = ar_barra_da_secao(b, AR_SECAO_COLECOES, idx);
	if (c->inicio_pastas) {
		gv_texto(CLAY_STRING("Pastas principais"), preview ? gv_t.tamanho_corpo - 1 : gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		int quais[AR_BARRA_MAX], nq = 0;
		for (int k = 0; k < n; k++) {
			if (b->v[idx[k]].no_inicio && b->v[idx[k]].visivel) {
				quais[nq++] = idx[k];
			}
		}
		for (int r = 0; r * por_linha < nq; r++) {
			CLAY(CLAY_SIDI(gv_str(preview ? "ip-linha" : "in-linha"), (uint32_t)r), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
				for (int k = r * por_linha; k < nq && k < (r + 1) * por_linha; k++) {
					Clay_ElementId cid = CLAY_SIDI(gv_str(preview ? "ip-cartao" : "in-cartao"), (uint32_t)quais[k]);
					if (!preview && gv_menu_no_ponteiro(cid, CLAY_ID("menu-inicio"))) { /* o botão direito (ou o dedo parado) num cartão */
						inicio_menu_item = quais[k];
						inicio_menu_caminho[0] = '\0';
						inicio_menu_removivel = false;
					}
					bool clicou = cartao_pasta(cid, &b->v[quais[k]], larg_cartao);
					if (clicou && !preview) {
						ir_para(b->v[quais[k]].caminho, true);
					}
				}
			}
		}
	}
	if (c->inicio_unidades) {
		gv_texto(CLAY_STRING("Unidades e dispositivos"), preview ? gv_t.tamanho_corpo - 1 : gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		ar_unidade un[16];
		int nu = ar_unidades(un, 16);
		if (!preview) {
			discos_rever();
		}
		int nd = preview ? 0 : disc_n; /* os ligados e ainda não montados vêm depois dos montados */
		float lu = preview ? (largura - 14) / 2 : 300;
		int por_linha_u = (int)((largura - 4) / (lu + 14));
		por_linha_u = por_linha_u < 1 ? 1 : por_linha_u;
		for (int r = 0; r * por_linha_u < nu + nd; r++) {
			CLAY(CLAY_SIDI(gv_str(preview ? "ip-ulinha" : "in-ulinha"), (uint32_t)r), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
				for (int k = r * por_linha_u; k < nu + nd && k < (r + 1) * por_linha_u; k++) {
					if (k >= nu) {
						Clay_ElementId did = CLAY_SIDI(CLAY_STRING("in-disco"), (uint32_t)(k - nu));
						if (cartao_disco(did, &disc_lista[k - nu], lu)) {
							disc_montar(&disc_lista[k - nu]);
						}
						continue;
					}
					Clay_ElementId uid = CLAY_SIDI(gv_str(preview ? "ip-unid" : "in-unid"), (uint32_t)k);
					if (!preview && gv_menu_no_ponteiro(uid, CLAY_ID("menu-inicio"))) {
						inicio_menu_item = -1;
						snprintf(inicio_menu_caminho, sizeof(inicio_menu_caminho), "%s", un[k].ponto);
						inicio_menu_removivel = un[k].removivel;
					}
					if (cartao_unidade(uid, &un[k], lu) && !preview) {
						ir_para(un[k].ponto, true);
					}
				}
			}
		}
	}
}

static void conteudo_inicio(void) {
	CLAY(CLAY_ID("principal"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("cabeca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 18, 8 }, .childGap = 4 } }) {
			gv_texto(CLAY_STRING("Início"), 32, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("Suas pastas e unidades"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		CLAY(ID_CONTEUDO, gv_rolagem(ID_CONTEUDO, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 8, 24 }, .childGap = 14 } })) {
			float larg = gv_caixa(ID_CONTEUDO).width - 48 - 12;
			inicio_conteudo(&barra, &cfg, false, larg > 200 ? larg : 800);
		}
	}
}

/* ---- as páginas de configurações ---- */

#define CARTAO { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(18), .childGap = 12 }, \
	.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }

static void titulo_cartao(const char *t) {
	gv_texto(gv_str(t), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
}

/* uma linha com título, explicação e um interruptor; verdadeiro se mudou */
static bool linha_interruptor(Clay_ElementId id, const char *titulo, const char *sub, bool *v) {
	bool mudou = false;
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 6 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			if (sub) {
				gv_texto(gv_str(sub), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		mudou = gv_interruptor(CLAY_IDI("li-i", id.id), v);
	}
	return mudou;
}

/* uma linha com título e uma lista suspensa à direita */
static bool linha_escolha(Clay_ElementId id, const char *titulo, const char *sub, const char *const *opcoes, int n, int *escolha, float largura) {
	bool mudou = false;
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 6 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			if (sub) {
				gv_texto(gv_str(sub), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_IDI("le-c", id.id), { .layout = { .sizing = { CLAY_SIZING_FIXED(largura) } } }) {
			mudou = gv_lista_suspensa(CLAY_IDI("le-l", id.id), opcoes, n, escolha);
		}
	}
	return mudou;
}

static bool radio(Clay_ElementId id, bool marcado, const char *titulo, const char *sub) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 6 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_IDI("rd-o", id.id), { .layout = { .sizing = { CLAY_SIZING_FIXED(22), CLAY_SIZING_FIXED(22) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
			.cornerRadius = CLAY_CORNER_RADIUS(11), .border = { .color = marcado ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(2) } }) {
			if (marcado) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10) } }, .backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
			}
		}
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(gv_str(sub), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	return gv_tocado(id);
}

static const char *const NOMES_VISTA[] = { "Grade", "Lista", "Lista grande" };
static const char *const SUB_VISTA[] = { "Miniaturas em grade", "Itens compactos", "Itens maiores com detalhes" };
static const char *const ICONES_VISTA[] = { "grid", "list", "bars" };
static const char *const OPCOES_ORDEM[] = { "Nome · A-Z", "Nome · Z-A", "Data · mais recentes", "Data · mais antigos", "Tamanho · maiores", "Tamanho · menores", "Tipo · A-Z", "Tipo · Z-A" };
static const char *const OPCOES_TEXTO[] = { "Padrão", "Pequeno", "Grande" };
static const char *const OPCOES_CONFLITO[] = { "Perguntar", "Substituir", "Manter os dois", "Pular" };
static const char *const OPCOES_ABRIR[] = { "Página inicial", "Última pasta", "Uma pasta que eu escolher" };

static int ordem_para_opcao(const ar_cfg *c) {
	static const int mapa[4] = { 0, 2, 4, 6 }; /* nome, data, tamanho, tipo */
	return mapa[c->ordem & 3] + (c->decrescente ? (c->ordem == AR_O_DATA ? 0 : 1) : (c->ordem == AR_O_DATA ? 1 : 0));
}
static void opcao_para_ordem(ar_cfg *c, int o) {
	c->ordem = o / 2;
	bool segunda = o % 2;
	/* data: "mais recentes" é decrescente; nos outros, a segunda opção é a decrescente */
	c->decrescente = c->ordem == AR_O_DATA ? !segunda : segunda;
}

/* a prévia da página Exibição: algumas pastas de verdade, do jeito escolhido */
static void previa_exibicao(void) {
	float f = escala_texto() > 0 ? 1.0f : 1.0f;
	(void)f;
	int idx[AR_BARRA_MAX], n = ar_barra_da_secao(&barra_edit, AR_SECAO_COLECOES, idx);
	int mostrados = 0;
	int vista_p = cfg_edit.vista;
	if (vista_p == AR_VISTA_GRADE) {
		CLAY(CLAY_ID("pv-grade"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
			for (int k = 0; k < n && mostrados < 4; k++) {
				const ar_barra_item *it = &barra_edit.v[idx[k]];
				if (!it->caminho[0] || !it->visivel) {
					continue;
				}
				cartao_pasta(CLAY_IDI("pv-c", (uint32_t)k), it, (float)cfg_edit.tam_pasta * 0.62f);
				mostrados++;
			}
		}
		return;
	}
	for (int k = 0; k < n && mostrados < 4; k++) {
		const ar_barra_item *it = &barra_edit.v[idx[k]];
		if (!it->caminho[0] || !it->visivel) {
			continue;
		}
		bool grande = vista_p == AR_VISTA_LISTA_GRANDE;
		ic_dados *d = &ic_pool[ic_n++ % (int)(sizeof(ic_pool) / sizeof(ic_pool[0]))];
		d->tipo = AR_PASTA;
		d->link = false;
		d->cor = 0;
		d->selo[0] = '\0';
		ar_entrada e;
		ar_entrada_de(it->caminho, &e);
		char data[48], tam[32] = "";
		ar_data_humana(e.mtime, time(NULL), data, sizeof(data));
		int itens = ar_contar_itens(it->caminho, cfg_edit.ocultos);
		CLAY(CLAY_IDI("pv-l", (uint32_t)k), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(grande ? 64 : 38) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			CLAY(CLAY_IDI("pv-li", (uint32_t)k), gv_livre(desenhar_icone, d, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(grande ? 50 : 26), CLAY_SIZING_FIXED(grande ? 50 : 26) } } })) {}
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(it->rotulo), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
				if (grande) {
					gv_texto(gv_str(itens >= 0 ? gv_fmt("%d itens", itens).chars : ""), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			}
			gv_texto(gv_str(data), gv_t.tamanho_corpo - 3, gv_t.texto_secundario, GV_PESO_NORMAL);
			(void)tam;
		}
		mostrados++;
	}
}

static void pagina_exibicao(void) {
	CLAY(CLAY_ID("ex-colunas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		CLAY(CLAY_ID("ex-esq"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16 } }) {
			CLAY(CLAY_ID("ex-vis"), CARTAO) {
				titulo_cartao("Visualização padrão");
				CLAY(CLAY_ID("ex-modos"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
					for (int m = 0; m < 3; m++) {
						Clay_ElementId id = CLAY_IDI("ex-modo", (uint32_t)m);
						bool ativo = cfg_edit.vista == m;
						CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(84) }, .padding = { 12, 10, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
							.backgroundColor = ativo ? gv_alfa(gv_t.acento, 0.18f) : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
							.border = { .color = ativo ? gv_t.acento : gv_t.borda, .width = GV_BORDA(ativo ? 2 : 1) } }) {
							gv_icone(ICONES_VISTA[m], 34, ativo ? gv_t.texto : gv_t.texto_secundario);
							CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
								gv_texto(gv_str(NOMES_VISTA[m]), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
								gv_texto(gv_str(SUB_VISTA[m]), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_NORMAL);
							}
						}
						if (gv_tocado(id)) {
							cfg_edit.vista = m;
						}
					}
				}
				CLAY(CLAY_ID("ex-tam-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 18, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY(CLAY_ID("ex-tam"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
						gv_texto(CLAY_STRING("Tamanho padrão das pastas"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
						CLAY(CLAY_ID("ex-tam-d"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
							float v01 = (float)(cfg_edit.tam_pasta - 64) / 128.0f;
							CLAY(CLAY_ID("ex-tam-s"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
								if (gv_deslizante(CLAY_ID("ex-tam-sl"), &v01, 6, 18)) {
									cfg_edit.tam_pasta = 64 + (int)(v01 * 128.0f / 8.0f + 0.5f) * 8;
								}
							}
							gv_texto(gv_fmt("%d px", cfg_edit.tam_pasta), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
						}
					}
					CLAY(CLAY_ID("ex-txt"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(190) }, .childGap = 8 } }) {
						gv_texto(CLAY_STRING("Tamanho do texto"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
						gv_lista_suspensa(CLAY_ID("ex-txt-l"), OPCOES_TEXTO, 3, &cfg_edit.texto);
					}
				}
			}
			CLAY(CLAY_ID("ex-cont"), CARTAO) {
				titulo_cartao("Conteúdo e detalhes");
				linha_interruptor(CLAY_ID("ex-ocultos"), "Mostrar arquivos ocultos", "Exibe arquivos e pastas ocultos no explorador", &cfg_edit.ocultos);
				linha_interruptor(CLAY_ID("ex-ext"), "Mostrar extensões de arquivo", "Exibe a extensão do tipo de arquivo", &cfg_edit.extensoes);
				linha_interruptor(CLAY_ID("ex-prev"), "Exibir prévias nas pastas", "Mostra o que há dentro das pastas, em miniatura", &cfg_edit.previas);
				linha_interruptor(CLAY_ID("ex-pf"), "Pastas antes dos arquivos", "Sempre exibe as pastas antes dos arquivos na lista", &cfg_edit.pastas_primeiro);
				int o = ordem_para_opcao(&cfg_edit);
				if (linha_escolha(CLAY_ID("ex-ord"), "Ordenar por", NULL, OPCOES_ORDEM, 8, &o, 240)) {
					opcao_para_ordem(&cfg_edit, o);
				}
			}
		}
		CLAY(CLAY_ID("ex-dir"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(420) }, .childGap = 16 } }) {
			CLAY(CLAY_ID("ex-previa"), CARTAO) {
				titulo_cartao("Prévia ao vivo");
				CLAY(CLAY_ID("ex-previa-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(10), .childGap = 4 },
					.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
					previa_exibicao();
				}
				gv_texto(gv_fmt("%s · Pastas de %dpx", NOMES_VISTA[cfg_edit.vista], cfg_edit.tam_pasta), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("ex-pref"), CARTAO) {
				titulo_cartao("Aplicar preferências");
				if (radio(CLAY_ID("ex-r1"), !cfg_edit.lembrar_por_pasta, "Usar como padrão nas novas pastas", "Todas as novas pastas seguirão estas configurações.")) {
					cfg_edit.lembrar_por_pasta = false;
				}
				if (radio(CLAY_ID("ex-r2"), cfg_edit.lembrar_por_pasta, "Lembrar visualização de cada pasta", "Cada pasta manterá sua própria visualização.")) {
					cfg_edit.lembrar_por_pasta = true;
				}
			}
		}
	}
}

static void pagina_avancado(void) {
	CLAY(CLAY_ID("av-colunas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		CLAY(CLAY_ID("av-ops"), CARTAO) {
			titulo_cartao("Operações de arquivo");
			linha_escolha(CLAY_ID("av-conflito"), "Ao encontrar um arquivo com o mesmo nome", "Copiar, mover e colar", OPCOES_CONFLITO, 4, &cfg_edit.conflito, 220);
			linha_interruptor(CLAY_ID("av-apagar"), "Confirmar exclusão permanente", "Pergunta antes de apagar para sempre (Shift+Del)", &cfg_edit.confirmar_apagar);
			linha_interruptor(CLAY_ID("av-esvaziar"), "Confirmar ao esvaziar a lixeira", NULL, &cfg_edit.confirmar_esvaziar);
		}
		CLAY(CLAY_ID("av-diag"), CARTAO) {
			titulo_cartao("Diagnóstico");
			linha_interruptor(CLAY_ID("av-log"), "Registrar erros do Atlas", "Os erros das operações vão para ~/.local/state/lumna/atlas.log", &cfg_edit.registrar_erros);
			CLAY(CLAY_ID("av-log-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
				if (gv_botao_ext(CLAY_ID("av-abrir-log"), &(gv_botao_opcoes){ .texto = "Abrir a pasta dos registros", .estilo = GV_BOTAO_ELEVADO })) {
					char *pasta_log = g_build_filename(g_get_user_state_dir(), "lumna", NULL);
					g_mkdir_with_parents(pasta_log, 0700);
					abrir_nova_janela(pasta_log);
					g_free(pasta_log);
				}
			}
			gv_texto(CLAY_STRING("Os registros ficam neste dispositivo."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
}

static void pagina_inicial(void) {
	CLAY(CLAY_ID("pi-colunas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		CLAY(CLAY_ID("pi-esq"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(480) }, .childGap = 16 } }) {
			CLAY(CLAY_ID("pi-abrir"), CARTAO) {
				titulo_cartao("Ao abrir o Atlas");
				linha_escolha(CLAY_ID("pi-abrir-e"), "Abrir", "Abra o início, a última pasta ou uma pasta personalizada.", OPCOES_ABRIR, 3, &cfg_edit.abrir_em, 250);
				if (cfg_edit.abrir_em == AR_ABRIR_PERSONALIZADA) {
					gv_campo(CLAY_ID("pi-pasta"), cfg_edit.pasta_inicial, sizeof(cfg_edit.pasta_inicial), "/home/voce/Projetos", GV_CAMPO_TEXTO);
				}
			}
			CLAY(CLAY_ID("pi-secoes"), CARTAO) {
				titulo_cartao("Seções do início");
				linha_interruptor(CLAY_ID("pi-s1"), "Pastas principais", NULL, &cfg_edit.inicio_pastas);
				linha_interruptor(CLAY_ID("pi-s2"), "Unidades e dispositivos", NULL, &cfg_edit.inicio_unidades);
			}
			CLAY(CLAY_ID("pi-pastas"), CARTAO) {
				titulo_cartao("Pastas principais");
				int idx[AR_BARRA_MAX], n = ar_barra_da_secao(&barra_edit, AR_SECAO_COLECOES, idx);
				for (int k = 0; k < n; k++) {
					ar_barra_item *it = &barra_edit.v[idx[k]];
					CLAY(CLAY_IDI("pi-p", (uint32_t)idx[k]), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						gv_icone(it->icone, 20, gv_t.texto_secundario);
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
							gv_texto(gv_str(it->rotulo), gv_t.tamanho_corpo, it->no_inicio ? gv_t.texto : gv_t.texto_suave, GV_PESO_NORMAL);
						}
						gv_interruptor(CLAY_IDI("pi-pi", (uint32_t)idx[k]), &it->no_inicio);
					}
				}
				gv_texto(CLAY_STRING("As pastas, a ordem e os nomes vêm da barra lateral (Coleções)."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_ID("pi-dir"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16 } }) {
			CLAY(CLAY_ID("pi-previa"), CARTAO) {
				titulo_cartao("Prévia ao vivo");
				CLAY(CLAY_ID("pi-previa-c"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12), .childGap = 10 },
					.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
					float larg = gv_caixa(CLAY_ID("pi-previa-c")).width - 24;
					inicio_conteudo(&barra_edit, &cfg_edit, true, larg > 200 ? larg : 420);
				}
				gv_texto(CLAY_STRING("As alterações valem apenas para a página inicial."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
	}
}

static const char *const OPCOES_SECAO[] = { "Espaços", "Coleções" };

static void pagina_barra(void) {
	CLAY(CLAY_ID("bl-colunas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		CLAY(CLAY_ID("bl-esq"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16 } }) {
			for (int sec = 0; sec < AR_SECOES_N; sec++) {
				CLAY(CLAY_IDI("bl-sec", (uint32_t)sec), CARTAO) {
					CLAY(CLAY_IDI("bl-sec-t", (uint32_t)sec), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
							titulo_cartao(OPCOES_SECAO[sec]);
						}
						if (gv_botao_ext(CLAY_IDI("bl-add", (uint32_t)sec), &(gv_botao_opcoes){ .icone = "plus", .texto = "Adicionar", .estilo = GV_BOTAO_SIMPLES, .altura = 34 })) {
							if (ar_barra_adicionar(&barra_edit, sec, "Nova pasta", g_get_home_dir())) {
								edit_item = barra_edit.n - 1;
							}
						}
					}
					int idx[AR_BARRA_MAX], n = ar_barra_da_secao(&barra_edit, sec, idx);
					int remover = -1, mover = -1, dir = 0;
					for (int k = 0; k < n; k++) {
						int i = idx[k];
						ar_barra_item *it = &barra_edit.v[i];
						bool fabrica = it->id[0] != '\0';
						CLAY(CLAY_IDI("bl-i", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(42) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
							.backgroundColor = edit_item == i ? gv_alfa(gv_t.acento, 0.12f) : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
							if (gv_botao_redondo(CLAY_IDI("bl-up", (uint32_t)i), "arrow-up", 28, false, false) && k > 0) {
								mover = i;
								dir = -1;
							}
							if (gv_botao_redondo(CLAY_IDI("bl-dn", (uint32_t)i), "arrow-down", 28, false, false) && k < n - 1) {
								mover = i;
								dir = 1;
							}
							gv_icone(it->icone, 22, it->visivel ? gv_t.texto_secundario : gv_t.texto_suave);
							CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
								gv_texto(gv_str(it->rotulo), gv_t.tamanho_corpo, it->visivel ? gv_t.texto : gv_t.texto_suave, GV_PESO_NORMAL);
							}
							gv_interruptor(CLAY_IDI("bl-vis", (uint32_t)i), &it->visivel);
							if (gv_botao_redondo(CLAY_IDI("bl-edit", (uint32_t)i), "edit", 28, edit_item == i, false)) {
								edit_item = edit_item == i ? -1 : i;
							}
							if (!fabrica && gv_botao_redondo(CLAY_IDI("bl-rm", (uint32_t)i), "minus", 28, false, true)) {
								remover = i;
							}
						}
						if (edit_item == i) {
							CLAY(CLAY_IDI("bl-ed", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_BOTTOM } },
								.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.acento, .width = GV_BORDA(1) } }) {
								CLAY(CLAY_IDI("bl-ed-n", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(150) }, .childGap = 4 } }) {
									gv_texto(CLAY_STRING("Nome"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
									gv_campo(CLAY_IDI("bl-ed-nc", (uint32_t)i), it->rotulo, sizeof(it->rotulo), "Nome", GV_CAMPO_TEXTO);
								}
								if (!fabrica) {
									CLAY(CLAY_IDI("bl-ed-p", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
										gv_texto(CLAY_STRING("Caminho"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
										gv_campo(CLAY_IDI("bl-ed-pc", (uint32_t)i), it->caminho, sizeof(it->caminho), "/home/voce/Projetos", GV_CAMPO_TEXTO);
									}
								} else {
									CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
								}
								CLAY(CLAY_IDI("bl-ed-s", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(150) }, .childGap = 4 } }) {
									gv_texto(CLAY_STRING("Categoria"), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
									int sc = it->secao;
									if (gv_lista_suspensa(CLAY_IDI("bl-ed-sl", (uint32_t)i), OPCOES_SECAO, 2, &sc)) {
										it->secao = sc;
									}
								}
							}
						}
					}
					if (mover >= 0) {
						ar_barra_mover(&barra_edit, mover, dir);
						if (edit_item == mover) {
							edit_item = -1;
						}
					}
					if (remover >= 0) {
						ar_barra_remover(&barra_edit, remover);
						edit_item = -1;
					}
				}
			}
			gv_texto(CLAY_STRING("O olho esconde sem apagar; remover um atalho não apaga a pasta. Reordene arrastando na barra lateral, ou com as setas."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("bl-dir"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(300) }, .childGap = 16 } }) {
			CLAY(CLAY_ID("bl-previa"), CARTAO) {
				titulo_cartao("Prévia da barra lateral");
				for (int sec = 0; sec < AR_SECOES_N; sec++) {
					int idx[AR_BARRA_MAX], n = ar_barra_da_secao(&barra_edit, sec, idx);
					int vis = 0;
					for (int k = 0; k < n; k++) {
						vis += barra_edit.v[idx[k]].visivel;
					}
					if (!vis) {
						continue;
					}
					gv_texto(gv_str(OPCOES_SECAO[sec]), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_MEDIO);
					for (int k = 0; k < n; k++) {
						const ar_barra_item *it = &barra_edit.v[idx[k]];
						if (it->visivel) {
							CLAY(CLAY_IDI("bl-pv", (uint32_t)idx[k]), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(30) }, .padding = { 8, 0, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
								gv_icone(it->icone, 18, gv_t.texto_secundario);
								gv_texto(gv_str(it->rotulo), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
							}
						}
					}
				}
			}
		}
	}
}

/* ---- aplicar, cancelar, restaurar ---- */

static void aplicar_cfg_ao_vivo(void) {
	ar_ordenar_pastas_primeiro(cfg.pastas_primeiro);
	ar_op_conflito_padrao((enum ar_conflito)cfg.conflito);
}

static void fechar_configuracoes(void) {
	pagina_cfg = -1;
	edit_item = -1;
	gv_app_redesenhar(app);
}

static void abrir_configuracoes(int pagina) {
	cfg_edit = cfg;
	barra_edit = barra;
	edit_item = -1;
	pagina_cfg = pagina;
}

static void aplicar_configuracoes(void) {
	/* a barra: um item sem caminho (de pasta) ou sem nome não vale */
	for (int i = barra_edit.n - 1; i >= 0; i--) {
		ar_barra_item *it = &barra_edit.v[i];
		if (!it->rotulo[0] || (!it->id[0] && (!it->caminho[0] || !g_file_test(it->caminho, G_FILE_TEST_IS_DIR)))) {
			avisar("Um atalho da barra lateral aponta para uma pasta que não existe: \"%s\"", it->rotulo);
			return;
		}
	}
	bool recarregar = cfg_edit.ocultos != cfg.ocultos || cfg_edit.pastas_primeiro != cfg.pastas_primeiro || cfg_edit.ordem != cfg.ordem || cfg_edit.decrescente != cfg.decrescente;
	bool vista_mudou = cfg_edit.vista != cfg.vista || cfg_edit.lembrar_por_pasta != cfg.lembrar_por_pasta;
	cfg = cfg_edit;
	barra = barra_edit;
	ar_cfg_gravar(&cfg);
	ar_barra_gravar(&barra);
	aplicar_cfg_ao_vivo();
	if (vista_mudou) {
		vista_atual = cfg.vista;
	}
	if (recarregar) {
		carregar(true);
	}
	avisar("Configurações aplicadas");
}

static void restaurar_padrao_da_pagina(void) {
	ar_cfg padrao;
	ar_cfg_padrao(&padrao);
	switch (pagina_cfg) {
	case 0:
		cfg_edit.vista = padrao.vista;
		cfg_edit.tam_pasta = padrao.tam_pasta;
		cfg_edit.texto = padrao.texto;
		cfg_edit.ocultos = padrao.ocultos;
		cfg_edit.extensoes = padrao.extensoes;
		cfg_edit.previas = padrao.previas;
		cfg_edit.pastas_primeiro = padrao.pastas_primeiro;
		cfg_edit.ordem = padrao.ordem;
		cfg_edit.decrescente = padrao.decrescente;
		cfg_edit.lembrar_por_pasta = padrao.lembrar_por_pasta;
		break;
	case 1:
		cfg_edit.abrir_em = padrao.abrir_em;
		cfg_edit.pasta_inicial[0] = '\0';
		cfg_edit.inicio_pastas = padrao.inicio_pastas;
		cfg_edit.inicio_unidades = padrao.inicio_unidades;
		for (int i = 0; i < barra_edit.n; i++) {
			barra_edit.v[i].no_inicio = true;
		}
		break;
	case 2:
		ar_barra_padrao(&barra_edit);
		edit_item = -1;
		break;
	case 3:
		cfg_edit.rede_reconectar = padrao.rede_reconectar;
		cfg_edit.rede_indisponiveis = padrao.rede_indisponiveis;
		break;
	case 4:
		cfg_edit.conflito = padrao.conflito;
		cfg_edit.confirmar_apagar = padrao.confirmar_apagar;
		cfg_edit.confirmar_esvaziar = padrao.confirmar_esvaziar;
		cfg_edit.registrar_erros = padrao.registrar_erros;
		break;
	}
}

/* ---- rede e nuvem ----
 * Os compartilhamentos de rede (o gvfs, numa thread própria: servicos/arquivos_rede.c) e as contas na nuvem (o rclone). A página de Configurações e as
 * duas listas da barra lateral ("Compartilhadas" e "Nuvem") mexem na mesma lista salva em rede.ini. */

static ar_rede rede;
static bool rede_montado[AR_SHARES_MAX];   /* o gvfs tem este compartilhamento montado (lido a cada ~2 s, não a cada quadro) */
static double rede_montado_em;
static bool tem_rclone;
static int rede_proto_i;                    /* o protocolo do formulário "Conectar a um servidor" */
static char rede_url[512];
static int nuvem_sel;                       /* a conta cujos detalhes a página mostra */
static int menu_share = -1, menu_conta = -1; /* o item do menu "..." */

/* a conexão em curso (uma de cada vez) */
static ar_conexao *cx;
static int cx_share = -1;
static bool cx_abrir_ao_fim, cx_desconectando;
static ar_conexao_estado cx_st;
static char cx_usuario[96], cx_senha[256], cx_dominio[96];
static bool cx_lembrar, cx_anonimo, cx_focar;
static int rec_fila[AR_SHARES_MAX], rec_n, rec_i; /* "Reconectar ao abrir": os que estavam conectados */

/* o rclone em curso: sincronizando uma conta, ou a pessoa entrando numa conta nova (o navegador) */
static ar_sync *sy;
static int sy_conta = -1;
static bool sy_oauth, sy_cancelado;
static char sy_remote[48], sy_nome[96], sy_tipo[16];
static time_t sy_tentativa[AR_CONTAS_MAX];  /* a última vez que se tentou (para não repetir a cada quadro quando falha) */
#define SYNC_INTERVALO 600

/* o diálogo "Adicionar conta" */
static bool conta_aberta;
static int conta_tipo_i;
static char conta_nome[96], conta_url[256], conta_usuario[96], conta_senha[256];
static bool conta_focar;

static void rede_redesenhar(void *d) {
	(void)d;
	gv_app_redesenhar(app);
}

static void rede_acordou(void *d) {
	(void)d;
	gv_na_linha_principal(rede_redesenhar, NULL);
}

static void rede_atualizar_montados(void) {
	for (int i = 0; i < rede.n_shares; i++) {
		rede_montado[i] = ar_share_montado(rede.shares[i].uri, NULL, 0);
	}
	rede_montado_em = gv_agora();
}

static void rede_salvar(void) {
	ar_rede_gravar(&rede);
}

static void share_conectar(int i, bool abrir, bool silencioso) {
	if (i < 0 || i >= rede.n_shares) {
		return;
	}
	if (cx) {
		avisar("Já tem uma conexão em andamento");
		return;
	}
	cx = ar_share_conectar(rede.shares[i].uri, silencioso, rede_acordou, NULL);
	cx_share = i;
	cx_abrir_ao_fim = abrir;
	cx_desconectando = false;
	cx_usuario[0] = cx_senha[0] = cx_dominio[0] = '\0';
	cx_lembrar = true;
	cx_anonimo = false;
	cx_focar = true;
	memset(&cx_st, 0, sizeof(cx_st));
	if (!silencioso) {
		avisar("Conectando a %s…", rede.shares[i].nome);
	}
}

static void share_desconectar(int i) {
	if (i < 0 || i >= rede.n_shares) {
		return;
	}
	if (cx) {
		avisar("Já tem uma conexão em andamento");
		return;
	}
	cx = ar_share_desconectar(rede.shares[i].uri, rede_acordou, NULL);
	cx_share = i;
	cx_abrir_ao_fim = false;
	cx_desconectando = true;
}

/* a pasta local do compartilhamento montado, para o Atlas entrar nele */
static void share_abrir(int i) {
	if (i < 0 || i >= rede.n_shares) {
		return;
	}
	char caminho[4096];
	if (ar_share_montado(rede.shares[i].uri, caminho, sizeof(caminho)) && caminho[0] && g_file_test(caminho, G_FILE_TEST_IS_DIR)) {
		ir_para(caminho, true);
	} else {
		avisar("%s está conectado, mas o gvfs não mostrou a pasta no sistema de arquivos (o gvfs-fuse está rodando?)", rede.shares[i].nome);
	}
}

static void share_esquecer(int i) {
	if (i < 0 || i >= rede.n_shares || (cx && cx_share == i)) {
		return;
	}
	ar_rede_remover_share(&rede, i);
	rede_salvar();
	rede_atualizar_montados();
	avisar("Compartilhamento removido da lista");
}

/* a cada quadro: a conexão anda; ao fim, diz o que houve; e vai conectando os que estavam conectados da última vez */
static void rede_rever(void) {
	if (cx) {
		ar_conexao_estado_ler(cx, &cx_st);
		if (cx_st.fase == AR_CX_OK || cx_st.fase == AR_CX_ERRO || cx_st.fase == AR_CX_CANCELADA) {
			int i = cx_share;
			bool abrir = cx_abrir_ao_fim, desc = cx_desconectando;
			char msg[256];
			snprintf(msg, sizeof(msg), "%s", cx_st.mensagem);
			int fase = cx_st.fase;
			ar_conexao_liberar(cx);
			cx = NULL;
			explicit_bzero(cx_senha, sizeof(cx_senha));
			if (i >= 0 && i < rede.n_shares) {
				if (fase == AR_CX_OK) {
					rede.shares[i].conectado_antes = !desc;
					rede_salvar();
					rede_atualizar_montados();
					avisar(desc ? "Desconectado de %s" : "Conectado a %s", rede.shares[i].nome);
					if (abrir && !desc) {
						share_abrir(i);
					}
				} else if (fase == AR_CX_ERRO) {
					rede.shares[i].conectado_antes = false;
					rede_salvar();
					avisar("Não consegui %s %s: %s", desc ? "desconectar de" : "conectar a", rede.shares[i].nome, msg);
				} else {
					avisar("Conexão cancelada");
				}
			}
			cx_share = -1;
			gv_app_redesenhar(app);
		}
	} else if (rec_i < rec_n) { /* os que estavam conectados, um de cada vez e sem pedir senha */
		int i = rec_fila[rec_i++];
		if (i < rede.n_shares && !rede_montado[i]) {
			share_conectar(i, false, true);
		}
	}
	/* o estado de "montado" se renova de tempos em tempos enquanto se olha para a rede */
	if ((pagina_cfg == 3 || vista == V_REDE) && gv_agora() - rede_montado_em > 2) {
		rede_atualizar_montados();
		gv_acordar_em(2.1);
	}
}

/* ---- nuvem ---- */

static void conta_sincronizar(int i, bool avisa) {
	if (i < 0 || i >= rede.n_contas || sy) {
		if (avisa && sy) {
			avisar("Já tem uma sincronização em andamento");
		}
		return;
	}
	if (!tem_rclone) {
		if (avisa) {
			avisar("Falta instalar o rclone (as contas na nuvem dependem dele)");
		}
		return;
	}
	sy = ar_nuvem_sincronizar(&rede.contas[i], rede_acordou, NULL);
	sy_conta = i;
	sy_oauth = false;
	sy_tentativa[i] = time(NULL);
	if (avisa) {
		avisar("Sincronizando %s…", rede.contas[i].nome);
	}
}

static void conta_entrar_oauth(void) {
	char remote[48];
	ar_nuvem_nome_remote(&rede, conta_nome, remote, sizeof(remote));
	snprintf(sy_remote, sizeof(sy_remote), "%s", remote);
	snprintf(sy_nome, sizeof(sy_nome), "%s", conta_nome);
	snprintf(sy_tipo, sizeof(sy_tipo), "%s", AR_NUVEM_TIPOS[conta_tipo_i]);
	sy = ar_nuvem_entrar(remote, sy_tipo, rede_acordou, NULL);
	sy_oauth = true;
	sy_cancelado = false;
	sy_conta = -1;
}

static void sync_rever(void) {
	if (sy) {
		ar_sync_estado st;
		ar_sync_estado_ler(sy, &st);
		if (st.fase != AR_SY_ANDANDO) {
			ar_sync_liberar(sy);
			sy = NULL;
			if (sy_oauth) {
				if (!sy_cancelado && st.fase == AR_SY_OK) {
					int k = ar_nuvem_adicionar_conta(&rede, sy_nome, sy_tipo, sy_remote);
					if (k >= 0) {
						rede_salvar();
						nuvem_sel = k;
						conta_aberta = false;
						avisar("Conta %s adicionada", sy_nome);
						conta_sincronizar(k, false);
					}
				} else if (!sy_cancelado) {
					avisar("Não consegui entrar na conta: %s", st.mensagem);
					conta_aberta = true;
				}
			} else if (sy_conta >= 0 && sy_conta < rede.n_contas) {
				ar_conta *c = &rede.contas[sy_conta];
				if (st.fase == AR_SY_OK) {
					c->ultima_sync = time(NULL);
					c->sincronizada = true;
					c->ultimo_erro[0] = '\0';
				} else {
					snprintf(c->ultimo_erro, sizeof(c->ultimo_erro), "%s", st.mensagem);
				}
				rede_salvar();
				if (pagina_cfg == 3 || vista == V_NUVEM) {
					avisar(st.fase == AR_SY_OK ? "%s sincronizada" : "A sincronização de %s falhou", c->nome);
				}
			}
			sy_conta = -1;
			gv_app_redesenhar(app);
		}
		return;
	}
	if (!tem_rclone) {
		return;
	}
	time_t agora = time(NULL);
	bool alguma = false;
	for (int i = 0; i < rede.n_contas; i++) {
		if (!rede.contas[i].segundo_plano) {
			continue;
		}
		alguma = true;
		time_t ref = rede.contas[i].ultima_sync > sy_tentativa[i] ? rede.contas[i].ultima_sync : sy_tentativa[i];
		if (agora - ref >= SYNC_INTERVALO) {
			conta_sincronizar(i, false);
			break;
		}
	}
	if (alguma) {
		gv_acordar_em(30.3);
	}
}

static const char *conta_status(const ar_conta *c, int i) {
	if (sy && !sy_oauth && sy_conta == i) {
		return "Sincronizando…";
	}
	if (c->ultimo_erro[0]) {
		return "Erro na sincronização";
	}
	if (!c->ultima_sync) {
		return "Ainda não sincronizada";
	}
	return "Sincronizado";
}

static Clay_Color conta_status_cor(const ar_conta *c, int i) {
	if (sy && !sy_oauth && sy_conta == i) {
		return gv_t.acento;
	}
	return c->ultimo_erro[0] ? gv_t.perigo : c->ultima_sync ? (Clay_Color){ 52, 199, 89, 255 } : gv_t.texto_suave;
}

/* ---- as listas da barra lateral (Compartilhadas, Nuvem): cartões, como o Início ---- */

static bool cartao_rede(Clay_ElementId id, const char *ic, const char *titulo, const char *sub, const char *estado, Clay_Color cor_estado, float larg) {
	bool sobre = gv_por_cima(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(larg), CLAY_SIZING_FIT(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = sobre ? gv_t.sob_cursor : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_icone(ic, 40, gv_t.texto_secundario);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 }, .clip = { .horizontal = true } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(sub), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
			gv_texto(gv_str(estado), gv_t.tamanho_legenda + 1, cor_estado, GV_PESO_NORMAL);
		}
	}
	return gv_tocado(id);
}

static void vazio_rede(const char *texto) {
	gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
	if (gv_botao_ext(CLAY_ID("rd-abrir-cfg"), &(gv_botao_opcoes){ .icone = "settings", .texto = "Abrir Rede e nuvem nas configurações", .estilo = GV_BOTAO_ELEVADO })) {
		abrir_configuracoes(3);
	}
}

static void conteudo_rede(void) {
	bool nuvem = vista == V_NUVEM;
	CLAY(CLAY_ID("principal"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("cabeca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 18, 8 }, .childGap = 4 } }) {
			gv_texto(nuvem ? CLAY_STRING("Nuvem") : CLAY_STRING("Compartilhadas"), 32, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(nuvem ? CLAY_STRING("Suas contas na nuvem, sincronizadas com uma pasta daqui") : CLAY_STRING("Pastas compartilhadas na rede (SMB, SFTP, FTP, WebDAV, NFS)"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		CLAY(ID_CONTEUDO, gv_rolagem(ID_CONTEUDO, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM,
			.sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 8, 24 }, .childGap = 14 } })) {
			float larg = gv_caixa(ID_CONTEUDO).width - 48 - 12;
			int por_linha = (int)((larg - 4) / (320 + 14));
			por_linha = por_linha < 1 ? 1 : por_linha;
			float lc = por_linha > 1 ? 320 : (larg > 200 ? larg - 4 : 200);
			int quais[AR_SHARES_MAX > AR_CONTAS_MAX ? AR_SHARES_MAX : AR_CONTAS_MAX], nq = 0;
			if (nuvem) {
				for (int i = 0; i < rede.n_contas; i++) {
					quais[nq++] = i;
				}
			} else {
				for (int i = 0; i < rede.n_shares; i++) {
					if (rede_montado[i] || cfg.rede_indisponiveis || (cx && cx_share == i)) {
						quais[nq++] = i;
					}
				}
			}
			if (!nq) {
				vazio_rede(nuvem ? "Nenhuma conta na nuvem ainda." : rede.n_shares ? "Nenhum compartilhamento conectado. Os desconectados ficam escondidos (dá para mostrá-los em Configurações)." : "Nenhum compartilhamento ainda.");
			}
			for (int r = 0; r * por_linha < nq; r++) {
				CLAY(CLAY_IDI("rd-linha", (uint32_t)r), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
					for (int k = r * por_linha; k < nq && k < (r + 1) * por_linha; k++) {
						int i = quais[k];
						Clay_ElementId id = CLAY_IDI("rd-cartao", (uint32_t)i);
						if (gv_menu_no_ponteiro(id, CLAY_ID("menu-rede"))) {
							menu_share = nuvem ? -1 : i;
							menu_conta = nuvem ? i : -1;
						}
						if (nuvem) {
							const ar_conta *c = &rede.contas[i];
							if (cartao_rede(id, "cloud", c->nome, c->pasta_local, conta_status(c, i), conta_status_cor(c, i), lc)) {
								if (g_file_test(c->pasta_local, G_FILE_TEST_IS_DIR)) {
									ir_para(c->pasta_local, true);
								} else {
									avisar("A pasta local ainda não existe: ela nasce na primeira sincronização");
								}
							}
						} else {
							const ar_share *s = &rede.shares[i];
							bool on = rede_montado[i];
							bool tentando = cx && cx_share == i;
							if (cartao_rede(id, "network", s->nome, gv_fmt("%s · %s", AR_PROTO_NOMES[s->proto], s->uri).chars, tentando ? "Conectando…" : on ? "Conectado" : "Desconectado",
								on ? (Clay_Color){ 52, 199, 89, 255 } : gv_t.texto_suave, lc)) {
								if (on) {
									share_abrir(i);
								} else if (!tentando) {
									share_conectar(i, true, false);
								}
							}
						}
					}
				}
			}
		}
	}
}

/* o conteúdo do menu de um compartilhamento ou de uma conta (o "..." da página e o botão direito nos cartões) */
static void menu_do_share(Clay_ElementId popup) {
	int i = menu_share;
	if (i < 0 || i >= rede.n_shares) {
		return;
	}
	bool on = rede_montado[i];
	if (on && gv_menu_item(CLAY_ID("mr-abrir"), "folder", "Abrir", NULL)) {
		gv_popup_fechar(popup);
		share_abrir(i);
	}
	if (on && gv_menu_item(CLAY_ID("mr-aba"), "plus", "Abrir em nova aba", NULL)) {
		char caminho[4096];
		gv_popup_fechar(popup);
		if (ar_share_montado(rede.shares[i].uri, caminho, sizeof(caminho)) && caminho[0]) {
			aba_nova(caminho);
		}
	}
	if (!on && gv_menu_item(CLAY_ID("mr-conectar"), "plus-circle", "Conectar", NULL)) {
		gv_popup_fechar(popup);
		share_conectar(i, false, false);
	}
	if (on && gv_menu_item(CLAY_ID("mr-desconectar"), "eject", "Desconectar", NULL)) {
		gv_popup_fechar(popup);
		share_desconectar(i);
	}
	separador("mr-sep", 1);
	if (gv_menu_item(CLAY_ID("mr-copiar"), "link", "Copiar endereço", NULL)) {
		gv_popup_fechar(popup);
		gv_copiar(rede.shares[i].uri);
		avisar("Endereço copiado");
	}
	if (gv_menu_item(CLAY_ID("mr-esquecer"), "trash", "Esquecer este compartilhamento", NULL)) {
		gv_popup_fechar(popup);
		share_esquecer(i);
	}
}

static void menu_da_conta(Clay_ElementId popup) {
	int i = menu_conta;
	if (i < 0 || i >= rede.n_contas) {
		return;
	}
	if (gv_menu_item(CLAY_ID("mc-sync"), "refresh", "Sincronizar agora", NULL)) {
		gv_popup_fechar(popup);
		conta_sincronizar(i, true);
	}
	if (gv_menu_item(CLAY_ID("mc-abrir"), "folder", "Abrir a pasta local", NULL)) {
		gv_popup_fechar(popup);
		if (g_file_test(rede.contas[i].pasta_local, G_FILE_TEST_IS_DIR)) {
			ir_para(rede.contas[i].pasta_local, true);
		} else {
			avisar("A pasta local ainda não existe: ela nasce na primeira sincronização");
		}
	}
	separador("mc-sep", 1);
	if (gv_menu_item(CLAY_ID("mc-remover"), "trash", "Remover a conta", NULL)) {
		gv_popup_fechar(popup);
		if (sy && !sy_oauth && sy_conta == i) {
			avisar("Espere a sincronização acabar para remover a conta");
		} else {
			ar_nuvem_remover(&rede, i);
			rede_salvar();
			if (nuvem_sel >= rede.n_contas) {
				nuvem_sel = rede.n_contas - 1;
			}
			avisar("Conta removida");
		}
	}
}

static void rede_menus(void) {
	/* o botão direito (ou o dedo parado) num cartão das listas da barra lateral */
	if (vista == V_REDE || vista == V_NUVEM) {
		enum gv_popup_lado lado;
		float altura_max;
		Clay_ElementId anc = gv_menu_ancora(CLAY_ID("menu-rede"));
		lado_do_menu(anc, &lado, &altura_max);
		GV_POPUP(CLAY_ID("menu-rede"), anc, .lado = lado, .alinhar = GV_ALINHAR_INICIO, .largura = 300, .distancia = 2, .altura_max = altura_max) {
			CLAY(CLAY_ID("mr-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
				if (vista == V_NUVEM) {
					menu_da_conta(CLAY_ID("menu-rede"));
				} else {
					menu_do_share(CLAY_ID("menu-rede"));
				}
			}
		}
	}
}

/* ---- o menu de contexto da barra lateral ---- */

static void menu_lateral_conteudo(void) {
	const char *dest = lat_menu_destino;
	if (!dest[0]) {
		return;
	}
	char *copia = g_strdup(dest);
	Clay_ElementId popup = CLAY_ID("menu-lateral");
	if (gv_menu_item(CLAY_ID("ml-abrir"), "folder", "Abrir", NULL)) {
		gv_popup_fechar(popup);
		ir_para(copia, true);
	}
	if (gv_menu_item(CLAY_ID("ml-aba"), "plus", "Abrir em nova aba", NULL)) {
		gv_popup_fechar(popup);
		aba_nova(copia);
	}
	if (gv_menu_item(CLAY_ID("ml-janela"), "external", "Abrir em nova janela", NULL)) {
		gv_popup_fechar(popup);
		abrir_nova_janela(copia);
	}
	char *lix = ar_lixeira_pasta();
	bool eh_lixeira = !strcmp(copia, lix);
	g_free(lix);
	if (eh_lixeira && gv_menu_item(CLAY_ID("ml-esvaziar"), "trash", "Esvaziar a lixeira", NULL)) {
		gv_popup_fechar(popup);
		pedir_esvaziar();
	}
	if (lat_menu_idx >= 0 && lat_menu_idx < barra.n) {
		separador("ml-sep", 1);
		if (gv_menu_item(CLAY_ID("ml-esconder"), "eye-off", "Esconder da barra lateral", NULL)) {
			gv_popup_fechar(popup);
			barra.v[lat_menu_idx].visivel = false;
			ar_barra_gravar(&barra);
			avisar("Escondido da barra lateral (volta em Configurações > Barra lateral)");
		}
		if (!barra.v[lat_menu_idx].id[0] && gv_menu_item(CLAY_ID("ml-remover"), "close", "Remover da barra lateral", NULL)) {
			gv_popup_fechar(popup);
			ar_barra_remover(&barra, lat_menu_idx);
			ar_barra_gravar(&barra);
			lat_menu_idx = -1;
		}
	}
	separador("ml-sep", 2);
	if (gv_menu_item(CLAY_ID("ml-personalizar"), "settings", "Personalizar barra lateral", NULL)) {
		gv_popup_fechar(popup);
		abrir_configuracoes(2);
	}
	g_free(copia);
}

static void menu_lateral(void) {
	enum gv_popup_lado lado;
	float altura_max;
	Clay_ElementId anc = gv_menu_ancora(CLAY_ID("menu-lateral"));
	lado_do_menu(anc, &lado, &altura_max);
	GV_POPUP(CLAY_ID("menu-lateral"), anc, .lado = lado, .alinhar = GV_ALINHAR_INICIO, .largura = 300, .distancia = 2, .altura_max = altura_max) {
		CLAY(CLAY_ID("ml-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			menu_lateral_conteudo();
		}
	}
}

/* ---- os diálogos: senha (e pergunta) do gvfs, adicionar conta, esperando o navegador ---- */

static bool rede_dialogo_aberto(void) {
	return (cx && (cx_st.fase == AR_CX_PEDINDO_SENHA || cx_st.fase == AR_CX_PERGUNTANDO)) || conta_aberta || (sy && sy_oauth);
}

static void corpo_senha(void) {
	texto_dialogo(cx_st.mensagem[0] ? cx_st.mensagem : "Este servidor pede uma senha.");
	if (cx_st.fase == AR_CX_PERGUNTANDO) {
		return;
	}
	if (cx_st.quer_usuario && !cx_anonimo) {
		if (!cx_usuario[0] && cx_st.usuario[0]) {
			snprintf(cx_usuario, sizeof(cx_usuario), "%s", cx_st.usuario);
		}
		gv_campo(CLAY_ID("cx-usuario"), cx_usuario, sizeof(cx_usuario), "Usuário", GV_CAMPO_TEXTO);
	}
	if (cx_st.quer_dominio && !cx_anonimo) {
		if (!cx_dominio[0] && cx_st.dominio[0]) {
			snprintf(cx_dominio, sizeof(cx_dominio), "%s", cx_st.dominio);
		}
		gv_campo(CLAY_ID("cx-dominio"), cx_dominio, sizeof(cx_dominio), "Domínio", GV_CAMPO_TEXTO);
	}
	if (!cx_anonimo) {
		int r = gv_campo(CLAY_ID("cx-senha"), cx_senha, sizeof(cx_senha), "Senha", GV_CAMPO_SENHA);
		if (cx_focar) {
			gv_focar(cx_st.quer_usuario && !cx_usuario[0] ? CLAY_ID("cx-usuario") : CLAY_ID("cx-senha"));
			cx_focar = false;
		}
		if (r & GV_CAMPO_ENTER) {
			ar_conexao_responder(cx, true, cx_usuario, cx_senha, cx_dominio, cx_lembrar, false);
			explicit_bzero(cx_senha, sizeof(cx_senha));
			cx_senha[0] = '\0';
		}
	}
	if (cx_st.anonimo_possivel) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_interruptor(CLAY_ID("cx-anonimo"), &cx_anonimo);
			gv_texto(CLAY_STRING("Entrar como convidado (sem senha)"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	if (cx_st.pode_lembrar && !cx_anonimo) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_interruptor(CLAY_ID("cx-lembrar"), &cx_lembrar);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(CLAY_STRING("Lembrar a senha"), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
				gv_texto(CLAY_STRING("Guardada no chaveiro do sistema, nunca em arquivo do Atlas."), gv_t.tamanho_legenda, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
	}
}

static void botoes_senha(void) {
	if (!cx) {
		return;
	}
	if (cx_st.fase == AR_CX_PERGUNTANDO) {
		if (gv_botao_ext(CLAY_ID("cx-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
			ar_conexao_escolher(cx, -1);
		}
		for (int k = 0; k < cx_st.n_opcoes; k++) {
			if (gv_botao_ext(CLAY_IDI("cx-opcao", (uint32_t)k), &(gv_botao_opcoes){ .texto = cx_st.opcoes[k], .estilo = k == 0 ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO })) {
				ar_conexao_escolher(cx, k);
			}
		}
		return;
	}
	if (gv_botao_ext(CLAY_ID("cx-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
		ar_conexao_responder(cx, false, "", "", "", false, false);
	}
	if (gv_botao_ext(CLAY_ID("cx-entrar"), &(gv_botao_opcoes){ .texto = "Conectar", .estilo = GV_BOTAO_CHEIO })) {
		ar_conexao_responder(cx, true, cx_usuario, cx_senha, cx_dominio, cx_lembrar, cx_anonimo);
		explicit_bzero(cx_senha, sizeof(cx_senha));
		cx_senha[0] = '\0';
	}
}

static bool conta_eh_oauth(void) {
	return conta_tipo_i >= 2;
}

static void conta_fechar(void) {
	conta_aberta = false;
	explicit_bzero(conta_senha, sizeof(conta_senha));
	conta_senha[0] = '\0';
}

static void conta_confirmar(void) {
	if (!tem_rclone) {
		avisar("Falta instalar o rclone (as contas na nuvem dependem dele)");
		return;
	}
	if (!conta_nome[0]) {
		snprintf(conta_nome, sizeof(conta_nome), "%s", AR_NUVEM_NOMES[conta_tipo_i]);
	}
	if (conta_eh_oauth()) {
		conta_entrar_oauth();
		return;
	}
	char erro[256] = "";
	int k = ar_nuvem_criar_webdav(&rede, conta_nome, AR_NUVEM_TIPOS[conta_tipo_i], conta_url, conta_usuario, conta_senha, erro, sizeof(erro));
	if (k < 0) {
		avisar("%s", erro[0] ? erro : "Não consegui adicionar a conta");
		return;
	}
	rede_salvar();
	nuvem_sel = k;
	conta_fechar();
	avisar("Conta %s adicionada", rede.contas[k].nome);
	conta_sincronizar(k, false);
}

static void corpo_conta(void) {
	if (sy && sy_oauth) {
		texto_dialogo("O rclone abriu uma página no navegador para você entrar na conta. Quando terminar lá, volte aqui: o Atlas continua sozinho.");
		return;
	}
	texto_dialogo("De que tipo é a conta?");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
		if (gv_lista_suspensa(CLAY_ID("ct-tipo"), AR_NUVEM_NOMES, 5, &conta_tipo_i) || !conta_nome[0]) {
			if (!conta_nome[0] || !strcmp(conta_nome, AR_NUVEM_NOMES[0]) || !strcmp(conta_nome, AR_NUVEM_NOMES[1]) || !strcmp(conta_nome, AR_NUVEM_NOMES[2]) || !strcmp(conta_nome, AR_NUVEM_NOMES[3]) || !strcmp(conta_nome, AR_NUVEM_NOMES[4])) {
				snprintf(conta_nome, sizeof(conta_nome), "%s", AR_NUVEM_NOMES[conta_tipo_i]);
			}
		}
	}
	gv_campo(CLAY_ID("ct-nome"), conta_nome, sizeof(conta_nome), "Nome da conta", GV_CAMPO_TEXTO);
	if (!conta_eh_oauth()) {
		gv_campo(CLAY_ID("ct-url"), conta_url, sizeof(conta_url), conta_tipo_i == 0 ? "https://nuvem.exemplo.com" : "https://servidor/dav/", GV_CAMPO_URL);
		gv_campo(CLAY_ID("ct-usuario"), conta_usuario, sizeof(conta_usuario), "Usuário", GV_CAMPO_TEXTO);
		int r = gv_campo(CLAY_ID("ct-senha"), conta_senha, sizeof(conta_senha), conta_tipo_i == 0 ? "Senha (ou senha de aplicativo)" : "Senha", GV_CAMPO_SENHA);
		if (r & GV_CAMPO_ENTER) {
			conta_confirmar();
		}
		texto_dialogo("A senha vai ofuscada para o rclone.conf do Atlas (só você lê). No Nextcloud, prefira uma senha de aplicativo.");
	} else {
		texto_dialogo("Você entra pelo navegador: o Atlas nunca vê a sua senha.");
	}
	if (!tem_rclone) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(CLAY_STRING("O rclone não está instalado neste sistema."), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
		}
	}
	if (conta_focar) {
		gv_focar(CLAY_ID("ct-nome"));
		conta_focar = false;
	}
}

static void botoes_conta(void) {
	if (sy && sy_oauth) {
		if (gv_botao_ext(CLAY_ID("ct-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
			sy_cancelado = true; /* o rclone ainda espera o navegador; quando acabar o resultado é descartado */
			conta_fechar();
		}
		return;
	}
	if (gv_botao_ext(CLAY_ID("ct-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
		conta_fechar();
	}
	if (gv_botao_ext(CLAY_ID("ct-ok"), &(gv_botao_opcoes){ .icone = conta_eh_oauth() ? "globe" : "plus", .texto = conta_eh_oauth() ? "Entrar pelo navegador" : "Adicionar", .estilo = GV_BOTAO_CHEIO })) {
		conta_confirmar();
	}
}

static void rede_dialogos(void) {
	if (gv_tecla() == 0xff1b) { /* Esc cancela o diálogo de cima (a página de configurações também fecha com Esc: ela espera os diálogos) */
		if (cx && (cx_st.fase == AR_CX_PEDINDO_SENHA || cx_st.fase == AR_CX_PERGUNTANDO)) {
			if (cx_st.fase == AR_CX_PERGUNTANDO) {
				ar_conexao_escolher(cx, -1);
			} else {
				ar_conexao_responder(cx, false, "", "", "", false, false);
			}
		} else if (sy && sy_oauth) {
			sy_cancelado = true;
			conta_fechar();
		} else if (conta_aberta) {
			conta_fechar();
		}
	}
	if (cx && (cx_st.fase == AR_CX_PEDINDO_SENHA || cx_st.fase == AR_CX_PERGUNTANDO)) {
		dialogo(CLAY_ID("dlg-senha"), 460, cx_share >= 0 && cx_share < rede.n_shares ? gv_fmt("Conectar a %s", rede.shares[cx_share].nome).chars : "Conectar", corpo_senha, botoes_senha);
	} else if (conta_aberta || (sy && sy_oauth)) {
		dialogo(CLAY_ID("dlg-conta"), 500, "Adicionar conta na nuvem", corpo_conta, botoes_conta);
	}
}

/* ---- a página de Configurações > Rede e nuvem ---- */

static void rede_url_conectar(void) {
	char uri[512], nome[96], erro[256] = "";
	int proto;
	if (!ar_rede_normalizar(rede_proto_i, rede_url, &proto, uri, sizeof(uri), nome, sizeof(nome), erro, sizeof(erro))) {
		avisar("%s", erro);
		return;
	}
	int i = ar_rede_adicionar_share(&rede, nome, proto, uri);
	if (i < 0) {
		avisar("A lista de compartilhamentos está cheia");
		return;
	}
	rede_salvar();
	rede_url[0] = '\0';
	rede_proto_i = proto;
	share_conectar(i, false, false);
}

static bool botao_mais(Clay_ElementId id) {
	return gv_botao_ext(id, &(gv_botao_opcoes){ .icone = "more", .estilo = GV_BOTAO_SIMPLES, .altura = 36 });
}

static void pagina_rede(void) {
	if (gv_agora() - rede_montado_em > 2) {
		rede_atualizar_montados();
	}
	CLAY(CLAY_ID("rd-colunas"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		/* a esquerda: compartilhamentos, o formulário e as preferências */
		CLAY(CLAY_ID("rd-esq"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_PERCENT(0.56f) }, .childGap = 16 } }) {
			CLAY(CLAY_ID("rd-shares"), CARTAO) {
				CLAY(CLAY_ID("rd-shares-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
						titulo_cartao("Compartilhamentos de rede");
					}
					if (gv_botao_ext(CLAY_ID("rd-conectar-topo"), &(gv_botao_opcoes){ .icone = "plus", .texto = "Conectar", .estilo = GV_BOTAO_ELEVADO, .altura = 36 })) {
						gv_focar(CLAY_ID("rd-url"));
					}
				}
				if (!rede.n_shares) {
					gv_texto(CLAY_STRING("Nenhum compartilhamento ainda. Conecte a um servidor abaixo."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
				}
				for (int i = 0; i < rede.n_shares; i++) {
					const ar_share *s = &rede.shares[i];
					bool on = rede_montado[i];
					bool tentando = cx && cx_share == i;
					CLAY(CLAY_IDI("rd-share", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 8, 8 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
						.border = { .color = gv_t.borda, .width = { .top = i ? 1 : 0 } } }) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(48) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
							.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
							gv_icone("network", 28, gv_t.texto_secundario);
						}
						CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
							gv_texto(gv_str(s->nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
							gv_texto(gv_fmt("%s · %s", AR_PROTO_NOMES[s->proto], tentando ? "Conectando…" : on ? "Conectado" : "Desconectado"), gv_t.tamanho_corpo - 2, on ? (Clay_Color){ 52, 199, 89, 255 } : gv_t.texto_suave, GV_PESO_NORMAL);
						}
						if (botao_mais(CLAY_IDI("rd-mais", (uint32_t)i))) {
							menu_share = i;
							gv_popup_alternar(CLAY_ID("menu-share"));
						}
					}
				}
				/* conectar a um servidor */
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
				gv_texto(CLAY_STRING("Conectar a um servidor"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
				CLAY(CLAY_ID("rd-form"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY(CLAY_ID("rd-proto-caixa"), { .layout = { .sizing = { CLAY_SIZING_FIXED(120) } } }) {
						gv_lista_suspensa(CLAY_ID("rd-proto"), AR_PROTO_NOMES, AR_PROTOS_N, &rede_proto_i);
					}
					CLAY(CLAY_ID("rd-url-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
						int r = gv_campo(CLAY_ID("rd-url"), rede_url, sizeof(rede_url), AR_PROTO_DICAS[rede_proto_i], GV_CAMPO_TEXTO);
						if (r & GV_CAMPO_ENTER) {
							rede_url_conectar();
						}
					}
					if (gv_botao_ext(CLAY_ID("rd-conectar"), &(gv_botao_opcoes){ .texto = "Conectar", .estilo = GV_BOTAO_ELEVADO, .desativado = !rede_url[0] || cx != NULL })) {
						rede_url_conectar();
					}
				}
			}
			CLAY(CLAY_ID("rd-prefs"), CARTAO) {
				titulo_cartao("Preferências de rede");
				linha_interruptor(CLAY_ID("rd-reconectar"), "Reconectar ao abrir o Atlas", "Restabelece automaticamente os compartilhamentos de rede.", &cfg_edit.rede_reconectar);
				linha_interruptor(CLAY_ID("rd-indisp"), "Mostrar locais indisponíveis", "Exibe os desconectados na lista de locais de rede.", &cfg_edit.rede_indisponiveis);
			}
		}
		/* a direita: as contas na nuvem */
		CLAY(CLAY_ID("rd-dir"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16 } }) {
			CLAY(CLAY_ID("rd-nuvem"), CARTAO) {
				CLAY(CLAY_ID("rd-nuvem-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
						titulo_cartao("Contas na nuvem");
					}
					if (gv_botao_ext(CLAY_ID("rd-add-conta"), &(gv_botao_opcoes){ .icone = "plus", .texto = "Adicionar conta", .estilo = GV_BOTAO_ELEVADO, .altura = 36 })) {
						conta_aberta = true;
						conta_focar = true;
						conta_tipo_i = 0;
						snprintf(conta_nome, sizeof(conta_nome), "%s", AR_NUVEM_NOMES[0]);
						conta_url[0] = conta_usuario[0] = conta_senha[0] = '\0';
					}
				}
				if (!tem_rclone) {
					gv_texto(CLAY_STRING("O rclone não está instalado: sem ele não há como sincronizar contas na nuvem."), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
				}
				if (!rede.n_contas) {
					gv_texto(CLAY_STRING("Nenhuma conta ainda. Adicione uma (Nextcloud, WebDAV, Google Drive, Dropbox ou OneDrive)."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
				}
				for (int i = 0; i < rede.n_contas; i++) {
					const ar_conta *c = &rede.contas[i];
					Clay_ElementId id = CLAY_IDI("rd-conta", (uint32_t)i);
					bool atual = i == nuvem_sel;
					bool tocou = gv_tocado(id); /* antes do "...": quem pede o toque depois ganha, e o botão vale mais que a linha */
					CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 8, 4, 8, 8 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
						.backgroundColor = atual ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(48), CLAY_SIZING_FIXED(48) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
							.backgroundColor = gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
							gv_icone("cloud", 28, gv_t.texto_secundario);
						}
						CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
							gv_texto(gv_str(c->nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
							CLAY_AUTO_ID({ .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
								CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8) } }, .backgroundColor = conta_status_cor(c, i), .cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
								gv_texto(gv_str(conta_status(c, i)), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
							}
						}
						if (botao_mais(CLAY_IDI("rd-conta-mais", (uint32_t)i))) {
							menu_conta = i;
							nuvem_sel = i;
							gv_popup_alternar(CLAY_ID("menu-conta"));
						}
					}
					if (tocou) {
						nuvem_sel = i;
					}
				}
				if (nuvem_sel >= 0 && nuvem_sel < rede.n_contas) {
					ar_conta *c = &rede.contas[nuvem_sel];
					gv_texto(CLAY_STRING("Pasta local"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					CLAY(CLAY_ID("rd-pasta-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
							if (gv_campo(CLAY_ID("rd-pasta"), c->pasta_local, sizeof(c->pasta_local), "~/Nuvem/Projetos", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
								rede_salvar();
							}
						}
						if (gv_botao_ext(CLAY_ID("rd-pasta-abrir"), &(gv_botao_opcoes){ .icone = "folder", .estilo = GV_BOTAO_ELEVADO, .altura = 40 })) {
							if (g_file_test(c->pasta_local, G_FILE_TEST_IS_DIR)) {
								char *destino = g_strdup(c->pasta_local);
								fechar_configuracoes();
								ir_para(destino, true);
								g_free(destino);
							} else {
								avisar("A pasta ainda não existe: ela nasce na primeira sincronização");
							}
						}
					}
					bool seg = c->segundo_plano;
					if (linha_interruptor(CLAY_ID("rd-seg"), "Sincronizar em segundo plano", "Mantém seus arquivos atualizados enquanto o Atlas está aberto.", &seg)) {
						c->segundo_plano = seg;
						rede_salvar();
					}
					CLAY(CLAY_ID("rd-sync-linha"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
							char quando[48] = "nunca";
							if (c->ultima_sync) {
								ar_data_humana(c->ultima_sync, time(NULL), quando, sizeof(quando));
							}
							gv_texto(gv_fmt("Última sincronização: %s", quando), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
							if (c->ultimo_erro[0]) {
								gv_texto(gv_str(c->ultimo_erro), gv_t.tamanho_legenda, gv_t.perigo, GV_PESO_NORMAL);
							}
						}
						if (gv_botao_ext(CLAY_ID("rd-sync-agora"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Sincronizar agora", .estilo = GV_BOTAO_ELEVADO, .desativado = sy != NULL })) {
							conta_sincronizar(nuvem_sel, true);
						}
					}
				}
			}
		}
	}
	/* os menus dos "..." */
	if (menu_share >= 0) {
		GV_POPUP(CLAY_ID("menu-share"), CLAY_IDI("rd-mais", (uint32_t)menu_share), .alinhar = GV_ALINHAR_FIM, .largura = 300) {
			CLAY(CLAY_ID("ms-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				menu_do_share(CLAY_ID("menu-share"));
			}
		}
	}
	if (menu_conta >= 0) {
		GV_POPUP(CLAY_ID("menu-conta"), CLAY_IDI("rd-conta-mais", (uint32_t)menu_conta), .alinhar = GV_ALINHAR_FIM, .largura = 320) {
			CLAY(CLAY_ID("mc-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				menu_da_conta(CLAY_ID("menu-conta"));
			}
		}
	}
}

static void tela_configuracoes(void) {
	static const struct { const char *rotulo, *icone, *titulo, *sub; } PAGINAS[] = {
		{ "Exibição", "desktop", "Exibição", "Defina como suas pastas e arquivos aparecem." },
		{ "Página inicial", "home", "Página inicial", "Escolha o que aparece quando você abre o Atlas." },
		{ "Barra lateral", "menu", "Barra lateral", "Escolha o que aparece e organize suas categorias." },
		{ "Rede e nuvem", "globe", "Rede e nuvem", "Acesse pastas compartilhadas e gerencie suas conexões." },
		{ "Avançado", "settings", "Avançado", "Ajuste operações de arquivo e diagnóstico." },
	};
	static const int ORDEM_NAV[] = { 0, 1, 2, 3, 4 };
	CLAY(CLAY_ID("cfg-corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("cfg-nav"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(260), CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 14, 12 }, .childGap = 4 },
			.border = { .color = gv_t.borda, .width = { .right = 1 } } }) {
			Clay_ElementId vol = CLAY_ID("cfg-voltar");
			CLAY(vol, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(40) }, .padding = { 8, 8, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_por_cima(vol) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
				gv_icone("arrow-left", 20, gv_t.texto_secundario);
				gv_texto(CLAY_STRING("Voltar aos arquivos"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_tocado(vol)) {
				fechar_configuracoes();
			}
			CLAY(CLAY_ID("cfg-nav-t"), { .layout = { .padding = { 8, 8, 18, 8 } } }) {
				gv_texto(CLAY_STRING("Configurações"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			}
			for (int k = 0; k < 5; k++) {
				int pg = ORDEM_NAV[k];
				Clay_ElementId id = CLAY_IDI("cfg-pg", (uint32_t)pg);
				bool ativo = pagina_cfg == pg;
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .padding = { 12, 8, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = ativo ? gv_t.acento_premido : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
					gv_icone(PAGINAS[pg].icone, 22, ativo ? gv_t.sobre_acento : gv_t.texto_secundario);
					gv_texto(gv_str(PAGINAS[pg].rotulo), gv_t.tamanho_corpo, ativo ? gv_t.sobre_acento : gv_t.texto, GV_PESO_NORMAL);
				}
				if (gv_tocado(id)) {
					pagina_cfg = pg;
					edit_item = -1;
				}
			}
		}
		Clay_ElementId rol = CLAY_ID("cfg-rolagem");
		CLAY(CLAY_ID("cfg-direita"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			CLAY(rol, gv_rolagem(rol, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.padding = { 20, 20, 18, 18 }, .childGap = 16 } })) {
				CLAY(CLAY_ID("cfg-cab"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
					gv_texto(gv_str(PAGINAS[pagina_cfg].titulo), 32, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_str(PAGINAS[pagina_cfg].sub), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
				}
				switch (pagina_cfg) {
				case 0: pagina_exibicao(); break;
				case 1: pagina_inicial(); break;
				case 2: pagina_barra(); break;
				case 3: pagina_rede(); break;
				default: pagina_avancado(); break;
				}
			}
			CLAY(CLAY_ID("cfg-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(64) }, .padding = { 20, 20, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
				if (gv_botao_ext(CLAY_ID("cfg-restaurar"), &(gv_botao_opcoes){ .texto = "Restaurar padrão", .estilo = GV_BOTAO_ELEVADO })) {
					restaurar_padrao_da_pagina();
				}
				CLAY(CLAY_ID("cfg-folga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					if (aviso[0] && gv_agora() < aviso_ate) {
						gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
				}
				if (gv_botao_ext(CLAY_ID("cfg-cancelar"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO })) {
					fechar_configuracoes();
				}
				if (gv_botao_ext(CLAY_ID("cfg-aplicar"), &(gv_botao_opcoes){ .texto = "Aplicar", .estilo = GV_BOTAO_CHEIO })) {
					aplicar_configuracoes();
				}
			}
		}
	}
}

/* Em teste (LUMNA_ATLAS_DEBUG=1): onde está cada coisa (em pixels da janela), para os testes clicarem no lugar certo. Só escreve o que mudou. */
static void debug_posicoes(void) {
	static int ligado = -1;
	if (ligado < 0) {
		ligado = getenv("LUMNA_ATLAS_DEBUG") != NULL;
	}
	if (!ligado) {
		return;
	}
	static GHashTable *antes;
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	GPtrArray *ids = g_ptr_array_new();
	static const char *const FIXOS[] = { "t-voltar", "t-avancar", "t-subir", "busca", "t-novo", "t-ordem", "t-grade", "t-lista", "t-ajustes", "l-inicio", "l-recentes", "l-favoritos",
		"det-fav", "det-fechar", "zoom", "s-grade", "s-lista", "mn-pasta", "mn-nome", "mn-criar", "mi-abrir", "mi-fav", "mi-copiar", "mo-o", "mf-pasta", "mf-atualizar",
		"caminho", "caminho-campo", "caminho-ir", "lix-esvaziar", "lix-restaurar", "st-desfazer", "st-cancelar", "cf-pular", "cf-ambos", "cf-substituir", "cf-todos", "ap-cancelar",
		"ap-confirmar", "ev-cancelar", "ev-confirmar", "pr-fechar", "pr-aplicar", "pr-cancelar", "pr-nome", "pr-grupo", "pr-exec", "pr-recursivo", "pr-copiar", "pr-abrir-pasta", "pr-abrir-com", "pr-local-exec", "l-lixeira", "mi-restaurar", "mi-apagar", "mi-props", "mi-janela", "mi-aba", "l-lixeira", "l-rede", "ml-abrir", "ml-aba", "ml-janela", "ml-esvaziar", "ml-esconder", "ml-remover", "ml-personalizar", "rd-url", "rd-proto", "rd-conectar", "rd-conectar-topo", "rd-add-conta", "rd-pasta", "rd-seg", "rd-sync-agora", "rd-reconectar", "rd-indisp", "rd-abrir-cfg", "l-rede", "l-nuvem", "mr-abrir", "mr-conectar", "mr-desconectar", "mr-esquecer", "mr-copiar", "mc-sync", "mc-abrir", "mc-remover", "cx-usuario", "cx-senha", "cx-entrar", "cx-cancelar", "cx-lembrar", "ct-tipo", "ct-nome", "ct-url", "ct-usuario", "ct-senha", "ct-ok", "ct-cancelar", "mn-i-abrir", "mn-i-aba", "mn-i-janela", "mn-i-remover", "aba-nova", "mi-local", "mi-recortar", "mi-colar",
		"mi-renomear", "mi-fixar", "mi-fix-espacos", "mi-fix-colecoes", "mi-fix-favoritos", "mi-compart", "mi-cel-nenhum", "mi-compactar", "mi-extrair", "mi-caminho",
		"mi-terminal", "mi-lixeira", "mi-cor", "mi-icone", "mf-cor", "mf-icone", "sub-ic-nenhum", "mf-ordenar", "mf-ord/0", "mf-git", "mf-props", "cl-url", "cl-ok", "cl-cancelar", "cfg-aplicar", "cfg-cancelar", "cfg-voltar", "cfg-restaurar", "ex-r1", "ex-r2", "ex-tam-sl", "cfg-corpo", "mf-arquivo", "mf-colar", "mf-tudo", "mf-caminho", "mf-terminal", "mf-esvaziar", "renome", NULL };
	for (int i = 0; FIXOS[i]; i++) {
		g_ptr_array_add(nomes, g_strdup(FIXOS[i]));
		Clay_String cs = { .chars = FIXOS[i], .length = (int)strlen(FIXOS[i]) };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashString(cs, 0).id));
	}
	for (int i = 0; i < 6; i++) {
		g_ptr_array_add(nomes, g_strdup_printf("filtro/%d", i));
		Clay_String cs = { .chars = "filtro", .length = 6 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)i, 0).id));
	}
	static const struct { const char *base; int n; } SERIES[] = { { "mf-ord", 4 }, { "l-it", 16 }, { "cel-mais", 24 }, { "lin-mais", 24 }, { "rd-share", 8 }, { "rd-mais", 8 }, { "rd-conta", 4 }, { "rd-conta-mais", 4 }, { "rd-cartao", 8 }, { "cx-opcao", 4 }, { "pr-aba", 4 }, { "pr-nivel", 3 }, { "sub-cor-i", 9 }, { "sub-ic", 16 }, { "aba", 12 }, { "aba-x", 12 }, { "in-cartao", 16 }, { "in-disco", 4 }, { "in-unid", 4 }, { "ex-modo", 3 }, { "cfg-pg", 5 },
		{ "bl-vis", 20 }, { "bl-up", 20 }, { "bl-dn", 20 }, { "bl-edit", 20 }, { "bl-add", 2 }, { "bl-rm", 20 }, { "pi-pi", 20 } };
	for (size_t k = 0; k < sizeof(SERIES) / sizeof(SERIES[0]); k++) {
		for (int i = 0; i < SERIES[k].n; i++) {
			g_ptr_array_add(nomes, g_strdup_printf("%s/%d", SERIES[k].base, i));
			Clay_String cs = { .chars = SERIES[k].base, .length = (int)strlen(SERIES[k].base) };
			g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)i, 0).id));
		}
	}
	for (int i = 0; i < 4; i++) {
		g_ptr_array_add(nomes, g_strdup_printf("mo-o/%d", i));
		Clay_String cs = { .chars = "mo-o", .length = 4 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)i, 0).id));
	}
	static const char *const LINHAS[] = { "ex-ocultos", "ex-ext", "ex-prev", "ex-pf", "av-apagar", "av-esvaziar", "av-log", "rd-reconectar", "rd-indisp", "rd-seg", "pi-s1", "pi-s2", NULL };
	for (int i = 0; LINHAS[i]; i++) {
		Clay_String base = { .chars = LINHAS[i], .length = (int)strlen(LINHAS[i]) };
		uint32_t h = Clay__HashString(base, 0).id;
		Clay_String cs = { .chars = "li-i", .length = 4 };
		g_ptr_array_add(nomes, g_strdup_printf("li/%s", LINHAS[i]));
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, h, 0).id));
	}
	for (int i = 0; i < 12; i++) {
		g_ptr_array_add(nomes, g_strdup_printf("sug/%d", i));
		Clay_String cs = { .chars = "sug", .length = 3 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)i, 0).id));
	}
	for (int i = 0; i < 4; i++) {
		g_ptr_array_add(nomes, g_strdup_printf("parte/%d", i));
		Clay_String cs = { .chars = "parte", .length = 5 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)i, 0).id));
	}
	for (int k = 0; k < nvis; k++) {
		int idx = vis[k];
		g_ptr_array_add(nomes, g_strdup_printf("item/%s", lista.v[idx].nome));
		Clay_String cs = { .chars = modo_lista ? "lin" : "cel", .length = 3 };
		g_ptr_array_add(ids, GUINT_TO_POINTER(Clay__HashStringWithOffset(cs, (uint32_t)idx, 0).id));
	}
	for (guint i = 0; i < nomes->len; i++) {
		Clay_BoundingBox b = gv_caixa((Clay_ElementId){ .id = GPOINTER_TO_UINT(ids->pdata[i]) });
		if (b.width <= 0) {
			continue;
		}
		char *linha = g_strdup_printf("%.0f %.0f %.0f %.0f", b.x, b.y, b.width, b.height);
		char *antigo = g_hash_table_lookup(antes, nomes->pdata[i]);
		if (!antigo || strcmp(antigo, linha)) {
			printf("pos %s %s\n", (char *)nomes->pdata[i], linha);
			g_hash_table_insert(antes, g_strdup(nomes->pdata[i]), linha);
		} else {
			g_free(linha);
		}
	}
	char *estado = g_strdup_printf("vista=%d pasta=%s nvis=%d sel=%d busca=[%s] ocultos=%d ordem=%d%s lista=%d filtro=%d lixeira=%d editando=%d op=%d nsug=%d pagina=%d abas=%d aba=%d props=%d nshares=%d nmont=%d ncontas=%d cx=%d mc=%d ms=%d mr=%d", vista, vista == V_PASTA ? pasta : "-", nvis,
		n_selecionados(), busca, cfg.ocultos, cfg.ordem, cfg.decrescente ? "-" : "+", vista_atual, filtro, em_lixeira(), editando_caminho, op != NULL, n_sug, pagina_cfg, n_abas, aba_atual, propriedades_abertas, rede.n_shares, ({ int m = 0; for (int q = 0; q < rede.n_shares; q++) { m += rede_montado[q]; } m; }), rede.n_contas, cx ? cx_st.fase : -1, menu_conta, menu_share, gv_popup_aberto(CLAY_ID("menu-rede")));
	static char *estado_antes;
	if (!estado_antes || strcmp(estado_antes, estado)) {
		printf("estado %s\n", estado);
		g_free(estado_antes);
		estado_antes = estado;
	} else {
		g_free(estado);
	}
	/* a ordem em que os itens aparecem */
	GString *ordem_txt = g_string_new("ordem");
	for (int k = 0; k < nvis && k < 40; k++) {
		g_string_append_printf(ordem_txt, " %s%s", lista.v[vis[k]].nome, sel[vis[k]] ? "*" : "");
	}
	static char *ordem_antes;
	if (!ordem_antes || strcmp(ordem_antes, ordem_txt->str)) {
		printf("%s\n", ordem_txt->str);
		g_free(ordem_antes);
		ordem_antes = g_strdup(ordem_txt->str);
	}
	g_string_free(ordem_txt, TRUE);
	fflush(stdout);
	g_ptr_array_free(nomes, TRUE);
	g_ptr_array_free(ids, TRUE);
}

/* ---- a janela ---- */

/* o que a área de transferência tem (os arquivos copiados/recortados), lido de novo só quando o texto muda */
static void area_rever(void) {
	static char *texto_antes;
	const char *t = gv_colar();
	if (g_strcmp0(t, texto_antes) || !area_quadro) {
		g_free(texto_antes);
		texto_antes = g_strdup(t);
		g_strfreev(area_quadro);
		area_quadro = ar_area_ler(t, &area_quadro_recortar);
		if (!area_quadro) {
			area_quadro_recortar = false;
		}
	}
}

static void desenhar(gv_app *a, void *dados) {
	ic_n = 0;
	if (recarga_pendente) { /* pedida no quadro anterior, durante o desenho */
		recarga_pendente = false;
		carregar(recarga_preservar);
	}
	ino_rever();
	texto_aplicar(true);
	op_rever();
	rede_rever();
	sync_rever();
	area_rever();
	if (pagina_cfg >= 0) {
		/* as páginas de configurações no lugar dos arquivos: a mesma janela, com a barra de título */
		CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
			.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_barra_janela(a, CLAY_ID("barra"), "folder", "Atlas");
			tela_configuracoes();
		}
		bool dialogo_rede = rede_dialogo_aberto();
		rede_dialogos();
		if (gv_tecla() == 0xff1b && !dialogo_rede && !gv_focado(CLAY_ID("bl-ed-nc")) && !gv_focado(CLAY_ID("pi-pasta")) && !gv_focado(CLAY_ID("rd-url")) && !gv_focado(CLAY_ID("rd-pasta"))) {
			fechar_configuracoes();
		}
		debug_posicoes();
		texto_aplicar(false);
		return;
	}
	dnd_rever();
	teclado_atalhos();
	CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("barra"), "folder", "Atlas");
		faixa_de_abas();
		barra_ferramentas();
		CLAY(CLAY_ID("corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			barra_lateral();
			recarga_bloqueada++;
			conteudo();
			recarga_bloqueada--;
			if (detalhes && !VISTA_CARTOES && lista.n >= 0) {
				painel_detalhes();
			}
		}
		barra_status();
	}
	if (dnd_alvo[0] && dnd_alvo_idx < 0 && !strcmp(dnd_alvo, pasta)) { /* soltando no fundo da lista: o contorno da lista inteira */
		Clay_BoundingBox b = gv_caixa(ID_CONTEUDO);
		if (b.width > 0) {
			CLAY(CLAY_ID("dnd-contorno"), { .layout = { .sizing = { CLAY_SIZING_FIXED(b.width), CLAY_SIZING_FIXED(b.height) } }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
				.border = { .color = gv_t.acento, .width = GV_BORDA(2) },
				.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 60, .offset = { b.x, b.y }, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH } }) {}
		}
	}
	/* o pedido do menu do espaço vazio vem depois dos itens (cada pedido de menu é de quem pede primeiro: o item ganha do fundo) */
	if (!VISTA_CARTOES && gv_menu_no_ponteiro(ID_CONTEUDO, CLAY_ID("menu-fundo"))) {
		limpar_selecao();
		ancora = foco = -1;
	}
	recarga_bloqueada++; /* tudo daqui até o fim ainda lê lista, cam e sel: a recarga espera o quadro seguinte */
	menus_de_contexto();
	rede_menus();
	menu_lateral();
	sugestoes_do_caminho();
	dialogos();
	rede_dialogos();
	tratar_cliques_conteudo();
	/* o tamanho dos ícones da grade fica gravado quando o dedo solta o controle */
	if (zoom_sujo && !gv_premido(CLAY_ID("zoom"))) {
		zoom_sujo = false;
		salvar_cfg();
	}
	debug_posicoes();
	recarga_bloqueada--;
	texto_aplicar(false);
}

/* Só em teste (LUMNA_ATLAS_DEBUG): no lugar do gvfs, um "servidor" que pede a senha (eric / certa) e uma pasta de mentira como ponto de montagem */
static bool montagem_de_teste(ar_conexao *c, const char *uri, char *erro, size_t tam) {
	if (strstr(uri, "livre")) {
		return true;
	}
	char usuario[96], senha[256];
	if (!ar_conexao_pedir_senha(c, "O servidor pede usuário e senha.", usuario, senha, sizeof(senha))) {
		snprintf(erro, tam, "Operação cancelada");
		return false;
	}
	if (!strcmp(usuario, "eric") && !strcmp(senha, "certa")) {
		return true;
	}
	snprintf(erro, tam, "Usuário ou senha incorretos");
	return false;
}

int main(int argc, char **argv) {
	if (getenv("LUMNA_ATLAS_DEBUG") && getenv("LUMNA_ATLAS_FALSO_DIR")) {
		ar_rede_usar_montagem_falsa(montagem_de_teste);
	}
	/* quem abre uma pasta pelo sistema (xdg-open, "Mostrar na pasta") manda file:///caminho: vira o caminho */
	if (argc > 1 && g_str_has_prefix(argv[1], "file://")) {
		char *c = g_filename_from_uri(argv[1], NULL, NULL);
		if (c) {
			argv[1] = c;
		}
	}
	/* uma janela nova pode abrir numa lista (@recentes, @favoritos, @rede...) */
	const char *especial = argc > 1 && (!strcmp(argv[1], ESPECIAL_INICIO) || !strcmp(argv[1], ESPECIAL_RECENTES) || !strcmp(argv[1], ESPECIAL_FAVORITOS) || !strcmp(argv[1], ESPECIAL_REDE) || !strcmp(argv[1], ESPECIAL_NUVEM)) ? argv[1] : NULL;
	if (especial) {
		argc = 1;
	}
	char inicio[4096];
	snprintf(inicio, sizeof(inicio), "%s", argc > 1 && g_file_test(argv[1], G_FILE_TEST_IS_DIR) ? argv[1] : g_get_home_dir());
	char *real = realpath(inicio, NULL);
	if (real) {
		snprintf(inicio, sizeof(inicio), "%s", real);
		free(real);
	}
	ar_cfg_ler(&cfg);
	ar_rede_ler(&rede);
	tem_rclone = ar_nuvem_disponivel();
	rede_atualizar_montados();
	if (cfg.rede_reconectar) { /* os que estavam conectados da última vez e já não estão: um de cada vez, sem pedir senha */
		for (int i = 0; i < rede.n_shares; i++) {
			if (rede.shares[i].conectado_antes && !rede_montado[i]) {
				rec_fila[rec_n++] = i;
			}
		}
	}
	ar_barra_ler(&barra);
	vista_atual = cfg.vista;
	aplicar_cfg_ao_vivo();
	/* onde abrir: o que a pessoa pediu na linha de comando; senão, o que escolheu em Configurações > Página inicial */
	const char *destino = argc > 1 && g_file_test(argv[1], G_FILE_TEST_IS_DIR) ? inicio : ESPECIAL_INICIO;
	if (argc <= 1) {
		if (cfg.abrir_em == AR_ABRIR_ULTIMA && g_file_test(cfg.ultima_pasta, G_FILE_TEST_IS_DIR)) {
			destino = cfg.ultima_pasta;
		} else if (cfg.abrir_em == AR_ABRIR_PERSONALIZADA && g_file_test(cfg.pasta_inicial, G_FILE_TEST_IS_DIR)) {
			destino = cfg.pasta_inicial;
		}
	}
	if (especial) {
		destino = especial;
	}
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Atlas", .app_id = "lumna-atlas", .largura = 1240, .altura = 760 });
	gv_arraste_aceitar(MIME_URIS);
	if (!app) {
		return 1;
	}
	inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (inotify_fd >= 0) {
		gv_app_vigiar(app, inotify_fd, inotify_pronto, NULL);
	}
	ir_para(destino, true);
	int r = gv_app_correr(app, desenhar, NULL);
	liberar_lista();
	ar_hist_liberar(&hist);
	return r;
}
