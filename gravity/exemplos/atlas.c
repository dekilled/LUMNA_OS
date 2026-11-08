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
#include "arquivos_op.h"
#include "connectc.h"
#include "gravity.h"
#include "json.h"

/* ---- estado ---- */

enum vista { V_PASTA, V_RECENTES, V_FAVORITOS, V_INICIO };
static bool em_lixeira(void);
#define ESPECIAL_RECENTES "@recentes"
#define ESPECIAL_FAVORITOS "@favoritos"
#define ESPECIAL_INICIO "@inicio"

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
static int renomeando = -1;         /* o item que está sendo renomeado (índice em lista.v) */
static char renome_nome[256];
static bool focar_renome;
static int renome_quadros;
static int submenu;                 /* 1 fixar na barra lateral, 2 compartilhar: abertos dentro do menu de contexto */
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
static bool estilo_aberto;          /* o diálogo de cor e ícone da pasta */
static char estilo_alvo[4096];
static int submenu_fundo;           /* 1: Ordenar por, aberto dentro do menu do espaço vazio */
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

/* (Re)lê o que a vista mostra. preservar: mantém a seleção pelos nomes (uma atualização por inotify). */
static void carregar(bool preservar) {
	extern void previas_limpar(void);
	previas_limpar();
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
	} else if (vista == V_INICIO) {
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

static void inotify_pronto(gv_app *a, int fd, void *dados) {
	char buf[4096];
	bool mudou = false;
	while (read(fd, buf, sizeof(buf)) > 0) {
		mudou = true;
	}
	if (mudou && vista == V_PASTA) {
		carregar(true);
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
static const char *nome_exibido(const ar_entrada *e) {
	if (cfg.extensoes || e->pasta) {
		return e->nome;
	}
	const char *ponto = strrchr(e->nome, '.');
	if (!ponto || ponto == e->nome || strlen(ponto) > 9 || !ponto[1]) {
		return e->nome;
	}
	return gv_fmt("%.*s", (int)(ponto - e->nome), e->nome).chars;
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

/* o que há dentro de cada pasta à vista, lembrado até a pasta de agora mudar */
void previas_limpar(void);
static GHashTable *previas_cache;
void previas_limpar(void) {
	if (previas_cache) {
		g_hash_table_remove_all(previas_cache);
	}
}

static int previa_da_pasta(const char *caminho, enum ar_tipo *saida) {
	if (!previas_cache) {
		previas_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	typedef struct { int n; enum ar_tipo t[4]; } previa_t;
	previa_t *p = g_hash_table_lookup(previas_cache, caminho);
	if (!p) {
		p = g_new0(previa_t, 1);
		p->n = ar_previa_pasta(caminho, p->t, 4);
		g_hash_table_insert(previas_cache, g_strdup(caminho), p);
	}
	memcpy(saida, p->t, sizeof(p->t));
	return p->n;
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
			d->nprevia = previa_da_pasta(cam[idx], d->previa);
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
			gv_icone(vista == V_RECENTES ? "clock" : vista == V_FAVORITOS ? "star" : vista == V_INICIO ? "home" : "trash", 20, gv_t.texto_secundario);
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
			const char *ic = !dest ? "folder" : !strcmp(dest, ESPECIAL_INICIO) ? "home" : !strcmp(dest, ESPECIAL_RECENTES) ? "clock" : !strcmp(dest, ESPECIAL_FAVORITOS) ? "star" : "folder";
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

static void item_lateral(Clay_ElementId id, const char *ic, const char *rotulo, bool ativo, const char *destino) {
	bool sobre = gv_por_cima(id);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38) }, .padding = { 12, 10, 0, 0 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = ativo ? gv_t.selecionado : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
		gv_icone(ic, 20, ativo ? gv_t.texto : gv_t.texto_secundario);
		gv_texto(gv_str(rotulo), gv_t.tamanho_corpo - 1, ativo ? gv_t.texto : gv_t.texto_secundario, ativo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
	}
	if (gv_tocado(id)) {
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

static void barra_lateral(void) {
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
						item_lateral(CLAY_IDI("l-it", (uint32_t)idx[k]), it->icone, it->rotulo, item_da_barra_ativo(it), destino_do_item(it));
					}
				}
			}
			CLAY(CLAY_ID("l-d3"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			char *lix = ar_lixeira_pasta();
			item_lateral(CLAY_ID("l-lixeira"), "trash", "Lixeira", em_lixeira(), lix);
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
	bool sobre = gv_por_cima(id);
	bool recortado = item_recortado(idx, area_quadro, area_quadro_recortar);
	Clay_Color fundo = sel[idx] ? gv_alfa(gv_t.acento, 0.18f) : sobre ? gv_t.sob_cursor : (Clay_Color){ 0, 0, 0, 0 };
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(cw), CLAY_SIZING_FIXED(ch) }, .padding = { 6, 6, 10, 6 },
		.childGap = 6, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .backgroundColor = fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = sel[idx] ? gv_t.acento : (Clay_Color){ 0, 0, 0, 0 }, .width = GV_BORDA(2) } }) {
		icone(CLAY_IDI("cel-ic", (uint32_t)idx), idx, tam);
		CLAY(CLAY_IDI("cel-nome", (uint32_t)idx), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
			.clip = { .horizontal = true, .vertical = true } }) {
			CLAY(CLAY_IDI("cel-nome-t", (uint32_t)idx), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0, (gv_t.tamanho_corpo + 6) * 2) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .clip = { .vertical = true } }) {
				if (renomeando == idx) {
					campo_renomear();
				} else {
					gv_texto(gv_str(nome_exibido(e)), gv_t.tamanho_corpo - 1, recortado ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
				}
			}
			if (e->pasta && e->itens >= 0) {
				gv_texto(gv_fmt(e->itens == 1 ? "%d item" : "%d itens", e->itens), gv_t.tamanho_legenda + 1, gv_t.texto_suave, GV_PESO_NORMAL);
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
	bool sobre = gv_por_cima(id);
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
					gv_texto(gv_str(nome_exibido(e)), gv_t.tamanho_corpo + 1, item_recortado(idx, area_quadro, area_quadro_recortar) ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_str(e->pasta ? (e->itens >= 0 ? gv_fmt(e->itens == 1 ? "%d item" : "%d itens", e->itens).chars : "") : ar_tipo_nome(e)), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
				}
			} else {
				gv_texto(gv_str(nome_exibido(e)), gv_t.tamanho_corpo, item_recortado(idx, area_quadro, area_quadro_recortar) ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
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
				float cw = tam + 44, ch = tam + 14 + 10 + 6 + (gv_t.tamanho_corpo + 6) * 2 + 20;
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
	bool fundo = gv_tocado(ID_CONTEUDO); /* primeiro: quem pede o aperto depois ganha, e o item vale mais que o fundo */
	bool algum = false;
	for (int k = 0; k < nvis; k++) {
		int idx = vis[k];
		Clay_ElementId id = modo_lista ? CLAY_IDI("lin", (uint32_t)idx) : CLAY_IDI("cel", (uint32_t)idx);
		if (gv_caixa(id).width > 0 && gv_tocado(id)) {
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

/* uma linha de dentro de um submenu aberto no lugar (um pouco recuada) */
static bool item_sub(Clay_ElementId id, const char *ic, const char *texto) {
	bool r = false;
	CLAY(CLAY_IDI("sub-recuo", id.id), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 22, 0, 0, 0 } } }) {
		r = gv_menu_item(id, ic, texto, NULL);
	}
	return r;
}

static void abrir_estilo_da(const char *caminho) {
	snprintf(estilo_alvo, sizeof(estilo_alvo), "%s", caminho);
	estilo_aberto = true;
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
			propriedades_abertas = true;
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
	if (pasta1 && gv_menu_item(CLAY_ID("mi-estilo"), "brush", "Cor e ícone da pasta", NULL)) {
		abrir_estilo_da(cam[i]);
		FECHAR_MENU();
	}
	if (pasta1) {
		if (gv_menu_item(CLAY_ID("mi-fixar"), "pin", "Fixar na barra lateral", submenu == 1 ? "▾" : "▸")) {
			submenu = submenu == 1 ? 0 : 1;
		}
		if (submenu == 1) {
			bool e = ar_barra_tem(cam[i], AR_SECAO_ESPACOS), c = ar_barra_tem(cam[i], AR_SECAO_COLECOES), f = ar_eh_favorito(cam[i]);
			if (item_sub(CLAY_ID("mi-fix-espacos"), e ? "check" : "home", "Espaços")) {
				ar_barra_alternar(cam[i], AR_SECAO_ESPACOS);
				ar_barra_ler(&barra);
				FECHAR_MENU();
			}
			if (item_sub(CLAY_ID("mi-fix-colecoes"), c ? "check" : "folder", "Coleções")) {
				ar_barra_alternar(cam[i], AR_SECAO_COLECOES);
				ar_barra_ler(&barra);
				FECHAR_MENU();
			}
			if (item_sub(CLAY_ID("mi-fix-favoritos"), f ? "check" : "star", "Favoritos")) {
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
		if (gv_menu_item(CLAY_ID("mi-compart"), "share", "Compartilhar", submenu == 2 ? "▾" : "▸")) {
			submenu = submenu == 2 ? 0 : 2;
			if (submenu == 2) {
				celulares_ler();
			}
		}
		if (submenu == 2) {
			if (!n_celulares) {
				item_sub(CLAY_ID("mi-cel-nenhum"), "phone", "Nenhum celular conectado");
			}
			for (int c = 0; c < n_celulares; c++) {
				if (item_sub(CLAY_IDI("mi-cel", (uint32_t)c), "phone", celulares[c].nome)) {
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
		propriedades_abertas = true;
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
	if (gv_menu_item(CLAY_ID("mf-ordenar"), "sliders", "Ordenar por", submenu_fundo == 1 ? "▾" : "▸")) {
		submenu_fundo = submenu_fundo == 1 ? 0 : 1;
	}
	if (submenu_fundo == 1) {
		for (int o = 0; o < 4; o++) {
			if (item_sub(CLAY_IDI("mf-ord", (uint32_t)o), cfg.ordem == (int)o ? "check" : NULL, NOMES_ORDEM[o])) {
				cfg.ordem = (enum ar_ordem)o;
				carregar(true);
				fechar_fundo();
			}
		}
		if (item_sub(CLAY_ID("mf-ord-asc"), !cfg.decrescente ? "check" : NULL, "Crescente")) {
			cfg.decrescente = false;
			carregar(true);
			fechar_fundo();
		}
		if (item_sub(CLAY_ID("mf-ord-desc"), cfg.decrescente ? "check" : NULL, "Decrescente")) {
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
		if (gv_menu_item(CLAY_ID("mf-estilo"), "brush", "Cor e ícone desta pasta", NULL)) {
			abrir_estilo_da(pasta);
			fechar_fundo();
		}
	}
	if (vista == V_PASTA) {
		separador("mf-sep", 3);
		if (gv_menu_item(CLAY_ID("mf-caminho"), "link", "Copiar caminho da pasta", NULL)) {
			gv_copiar(pasta);
			avisar("Caminho copiado");
			fechar_fundo();
		}
		if (gv_menu_item(CLAY_ID("mf-props"), "info", "Propriedades", "Ctrl+I")) {
			propriedades_abertas = true;
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

/* o menu de contexto abre para baixo do ponto onde se pediu, ou para cima se embaixo não cabe; se ainda assim não couber, rola */
static void lado_do_menu(Clay_ElementId ancora, enum gv_popup_lado *lado, float *altura_max) {
	Clay_BoundingBox a = gv_caixa(ancora);
	float h = gv_app_altura(app);
	float margem = 64; /* a sombra do popup também ocupa lugar na superfície dele */
	float embaixo = h - a.y - margem, em_cima = a.y - margem;
	if (a.width <= 0 || embaixo >= 380 || embaixo >= em_cima) {
		*lado = GV_POPUP_ABAIXO;
		*altura_max = embaixo > 160 ? embaixo : 160;
	} else {
		*lado = GV_POPUP_ACIMA;
		*altura_max = em_cima > 160 ? em_cima : 160;
	}
}

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
		submenu = 0; /* fechado: a próxima abertura começa com os submenus recolhidos */
	}
	Clay_ElementId anc_fundo = gv_menu_ancora(CLAY_ID("menu-fundo"));
	lado_do_menu(anc_fundo, &lado, &altura_max);
	GV_POPUP(CLAY_ID("menu-fundo"), anc_fundo, .lado = lado, .alinhar = GV_ALINHAR_INICIO, .largura = 280, .distancia = 2, .altura_max = altura_max) {
		CLAY(CLAY_ID("mf-corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(6), .childGap = 2 } }) {
			menu_do_fundo();
		}
	} else {
		submenu_fundo = 0;
	}
}

/* ---- janelas de diálogo (por cima de tudo): conflito, apagar para sempre, propriedades ---- */

static void dialogo(Clay_ElementId id, float largura, const char *titulo, void (*corpo)(void), void (*botoes)(void)) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_ROOT, .zIndex = 120, .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE }, .backgroundColor = gv_t.veu }) {
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

/* --- propriedades --- */
static void linha_prop(const char *rotulo, const char *valor) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(110) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(valor), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
		}
	}
}

static void corpo_propriedades(void) {
	int n = n_selecionados();
	int i = primeiro_selecionado();
	if (n > 1) {
		int64_t total = 0;
		int pastas = 0;
		for (int j = 0; j < lista.n; j++) {
			if (sel[j]) {
				pastas += lista.v[j].pasta;
				if (lista.v[j].pasta) {
					int64_t b;
					int it;
					ar_tamanho_pasta(cam[j], 20000, &b, &it);
					total += b;
				} else {
					total += lista.v[j].tamanho;
				}
			}
		}
		char t[32];
		ar_tamanho_humano(total, t, sizeof(t));
		linha_prop("Selecionados", gv_fmt("%d itens (%d %s)", n, pastas, pastas == 1 ? "pasta" : "pastas").chars);
		linha_prop("Tamanho", t);
		return;
	}
	ar_entrada e;
	const char *caminho = i >= 0 ? cam[i] : pasta;
	if (!ar_entrada_de(caminho, &e)) {
		return;
	}
	struct stat st;
	char data[48], t[32];
	linha_prop("Nome", e.nome);
	linha_prop("Tipo", ar_tipo_nome(&e));
	char *pai = ar_pai(caminho);
	linha_prop("Local", pai ? pai : "/");
	g_free(pai);
	if (e.pasta) {
		int64_t b;
		int it;
		bool completo = ar_tamanho_pasta(caminho, 50000, &b, &it);
		ar_tamanho_humano(b, t, sizeof(t));
		linha_prop("Tamanho", gv_fmt("%s%s (%d %s dentro)", completo ? "" : "mais de ", t, it, it == 1 ? "item" : "itens").chars);
	} else {
		ar_tamanho_humano(e.tamanho, t, sizeof(t));
		linha_prop("Tamanho", gv_fmt("%s (%lld bytes)", t, (long long)e.tamanho).chars);
	}
	ar_data_humana(e.mtime, time(NULL), data, sizeof(data));
	linha_prop("Modificado", data);
	if (stat(caminho, &st) == 0) {
		ar_data_humana(st.st_atime, time(NULL), data, sizeof(data));
		linha_prop("Acessado", data);
	}
	char perm[16];
	ar_permissoes_texto(e.modo, perm, sizeof(perm));
	linha_prop("Permissões", gv_fmt("%s (%04o)", perm, (unsigned)(e.modo & 07777)).chars);
	struct passwd *pw = getpwuid(e.uid);
	linha_prop("Dono", e.uid == getuid() ? gv_fmt("Você (%s)", pw ? pw->pw_name : "?").chars : pw ? pw->pw_name : "?");
}
static void botoes_propriedades(void) {
	if (gv_botao_ext(CLAY_ID("pr-fechar"), &(gv_botao_opcoes){ .texto = "Fechar", .estilo = GV_BOTAO_CHEIO })) {
		propriedades_abertas = false;
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

/* --- cor e ícone da pasta (vale na hora) --- */
static void corpo_estilo(void) {
	uint32_t rgb;
	char emblema[32];
	ar_pasta_estilo(estilo_alvo, &rgb, emblema, sizeof(emblema));
	texto_dialogo("Cor");
	CLAY(CLAY_ID("es-cores"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		for (int c = 0; c < AR_CORES_N; c++) {
			Clay_ElementId id = CLAY_IDI("es-cor", (uint32_t)c);
			uint32_t v = AR_CORES[c].rgb;
			Clay_Color cor = v ? (Clay_Color){ (float)((v >> 16) & 255), (float)((v >> 8) & 255), (float)(v & 255), 255 } : gv_t.acento;
			bool atual = v == rgb;
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(34) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = cor, .cornerRadius = CLAY_CORNER_RADIUS(17), .border = { .color = atual ? gv_t.texto : gv_t.borda, .width = GV_BORDA(atual ? 3 : 1) } }) {
				if (atual) {
					gv_icone("check", 16, (Clay_Color){ 255, 255, 255, 255 });
				}
			}
			if (gv_tocado(id)) {
				ar_pasta_estilo_definir(estilo_alvo, v, emblema);
			}
		}
	}
	texto_dialogo("Ícone");
	CLAY(CLAY_ID("es-icones"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
		for (int i = 0; AR_ICONES[i]; i++) {
			if (i == 8) {
				break;
			}
			Clay_ElementId id = CLAY_IDI("es-ic", (uint32_t)i);
			bool atual = !strcmp(AR_ICONES[i], emblema);
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(44), CLAY_SIZING_FIXED(44) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = atual ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
				.border = { .color = atual ? gv_t.acento : gv_t.borda, .width = GV_BORDA(atual ? 2 : 1) } }) {
				gv_icone(AR_ICONES[i], 22, atual ? gv_t.texto : gv_t.texto_secundario);
			}
			if (gv_tocado(id)) {
				ar_pasta_estilo_definir(estilo_alvo, rgb, atual ? "" : AR_ICONES[i]);
			}
		}
	}
	CLAY(CLAY_ID("es-icones2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
		int n = 0;
		while (AR_ICONES[n]) {
			n++;
		}
		for (int i = 8; i < n; i++) {
			Clay_ElementId id = CLAY_IDI("es-ic", (uint32_t)i);
			bool atual = !strcmp(AR_ICONES[i], emblema);
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(44), CLAY_SIZING_FIXED(44) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = atual ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : gv_t.fundo, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
				.border = { .color = atual ? gv_t.acento : gv_t.borda, .width = GV_BORDA(atual ? 2 : 1) } }) {
				gv_icone(AR_ICONES[i], 22, atual ? gv_t.texto : gv_t.texto_secundario);
			}
			if (gv_tocado(id)) {
				ar_pasta_estilo_definir(estilo_alvo, rgb, atual ? "" : AR_ICONES[i]);
			}
		}
	}
}
static void botoes_estilo(void) {
	if (gv_botao_ext(CLAY_ID("es-padrao"), &(gv_botao_opcoes){ .texto = "Voltar ao padrão", .estilo = GV_BOTAO_ELEVADO })) {
		ar_pasta_estilo_definir(estilo_alvo, 0, "");
	}
	if (gv_botao_ext(CLAY_ID("es-fechar"), &(gv_botao_opcoes){ .texto = "Fechar", .estilo = GV_BOTAO_CHEIO })) {
		estilo_aberto = false;
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
	} else if (estilo_aberto) {
		dialogo(CLAY_ID("dlg-estilo"), 460, "Cor e ícone da pasta", corpo_estilo, botoes_estilo);
	} else if (propriedades_abertas) {
		dialogo(CLAY_ID("dlg-props"), 520, "Propriedades", corpo_propriedades, botoes_propriedades);
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
	bool dialogo_aberto = (op && op_st.pergunta) || confirmar_apagar || confirmar_esvaziar || propriedades_abertas || clonar_aberto || estilo_aberto;
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
				confirmar_apagar = confirmar_esvaziar = propriedades_abertas = clonar_aberto = estilo_aberto = false;
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
					propriedades_abertas = true;
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
					bool clicou = cartao_pasta(CLAY_SIDI(gv_str(preview ? "ip-cartao" : "in-cartao"), (uint32_t)quais[k]), &b->v[quais[k]], larg_cartao);
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
		float lu = preview ? (largura - 14) / 2 : 300;
		int por_linha_u = (int)((largura - 4) / (lu + 14));
		por_linha_u = por_linha_u < 1 ? 1 : por_linha_u;
		for (int r = 0; r * por_linha_u < nu; r++) {
			CLAY(CLAY_SIDI(gv_str(preview ? "ip-ulinha" : "in-ulinha"), (uint32_t)r), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
				for (int k = r * por_linha_u; k < nu && k < (r + 1) * por_linha_u; k++) {
					if (cartao_unidade(CLAY_SIDI(gv_str(preview ? "ip-unid" : "in-unid"), (uint32_t)k), &un[k], lu) && !preview) {
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
			gv_texto(CLAY_STRING("O olho esconde sem apagar; remover um atalho não apaga a pasta. Use as setas para reordenar."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
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
	case 4:
		cfg_edit.conflito = padrao.conflito;
		cfg_edit.confirmar_apagar = padrao.confirmar_apagar;
		cfg_edit.confirmar_esvaziar = padrao.confirmar_esvaziar;
		cfg_edit.registrar_erros = padrao.registrar_erros;
		break;
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
	static const int ORDEM_NAV[] = { 0, 1, 2, 4 }; /* a Rede e nuvem ainda não existe: fora da lista */
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
			for (int k = 0; k < 4; k++) {
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
		"ap-confirmar", "ev-cancelar", "ev-confirmar", "pr-fechar", "l-lixeira", "mi-restaurar", "mi-apagar", "mi-props", "mi-janela", "mi-aba", "aba-nova", "mi-local", "mi-recortar", "mi-colar",
		"mi-renomear", "mi-fixar", "mi-fix-espacos", "mi-fix-colecoes", "mi-fix-favoritos", "mi-compart", "mi-cel-nenhum", "mi-compactar", "mi-extrair", "mi-caminho",
		"mi-terminal", "mi-lixeira", "mi-estilo", "mf-ordenar", "mf-ord/0", "mf-git", "mf-estilo", "mf-props", "cl-url", "cl-ok", "cl-cancelar", "es-fechar", "es-padrao", "cfg-aplicar", "cfg-cancelar", "cfg-voltar", "cfg-restaurar", "ex-r1", "ex-r2", "ex-tam-sl", "cfg-corpo", "mf-arquivo", "mf-colar", "mf-tudo", "mf-caminho", "mf-terminal", "mf-esvaziar", "renome", NULL };
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
	static const struct { const char *base; int n; } SERIES[] = { { "es-cor", 9 }, { "es-ic", 16 }, { "mf-ord", 4 }, { "l-it", 16 }, { "aba", 12 }, { "aba-x", 12 }, { "in-cartao", 16 }, { "ex-modo", 3 }, { "cfg-pg", 5 },
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
	static const char *const LINHAS[] = { "ex-ocultos", "ex-ext", "ex-prev", "ex-pf", "av-apagar", "av-esvaziar", "av-log", "pi-s1", "pi-s2", NULL };
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
	char *estado = g_strdup_printf("vista=%d pasta=%s nvis=%d sel=%d busca=[%s] ocultos=%d ordem=%d%s lista=%d filtro=%d lixeira=%d editando=%d op=%d nsug=%d pagina=%d abas=%d aba=%d", vista, vista == V_PASTA ? pasta : "-", nvis,
		n_selecionados(), busca, cfg.ocultos, cfg.ordem, cfg.decrescente ? "-" : "+", vista_atual, filtro, em_lixeira(), editando_caminho, op != NULL, n_sug, pagina_cfg, n_abas, aba_atual);
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
	texto_aplicar(true);
	op_rever();
	area_rever();
	if (pagina_cfg >= 0) {
		/* as páginas de configurações no lugar dos arquivos: a mesma janela, com a barra de título */
		CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
			.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_barra_janela(a, CLAY_ID("barra"), "folder", "Atlas");
			tela_configuracoes();
		}
		if (gv_tecla() == 0xff1b && !gv_focado(CLAY_ID("bl-ed-nc")) && !gv_focado(CLAY_ID("pi-pasta"))) {
			fechar_configuracoes();
		}
		debug_posicoes();
		texto_aplicar(false);
		return;
	}
	teclado_atalhos();
	CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("barra"), "folder", "Atlas");
		faixa_de_abas();
		barra_ferramentas();
		CLAY(CLAY_ID("corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			barra_lateral();
			conteudo();
			if (detalhes && vista != V_INICIO && lista.n >= 0) {
				painel_detalhes();
			}
		}
		barra_status();
	}
	/* o pedido do menu do espaço vazio vem depois dos itens (cada pedido de menu é de quem pede primeiro: o item ganha do fundo) */
	if (vista != V_INICIO && gv_menu_no_ponteiro(ID_CONTEUDO, CLAY_ID("menu-fundo"))) {
		limpar_selecao();
		ancora = foco = -1;
	}
	menus_de_contexto();
	sugestoes_do_caminho();
	dialogos();
	tratar_cliques_conteudo();
	/* o tamanho dos ícones da grade fica gravado quando o dedo solta o controle */
	if (zoom_sujo && !gv_premido(CLAY_ID("zoom"))) {
		zoom_sujo = false;
		salvar_cfg();
	}
	debug_posicoes();
	texto_aplicar(false);
}

int main(int argc, char **argv) {
	/* quem abre uma pasta pelo sistema (xdg-open, "Mostrar na pasta") manda file:///caminho: vira o caminho */
	if (argc > 1 && g_str_has_prefix(argv[1], "file://")) {
		char *c = g_filename_from_uri(argv[1], NULL, NULL);
		if (c) {
			argv[1] = c;
		}
	}
	char inicio[4096];
	snprintf(inicio, sizeof(inicio), "%s", argc > 1 && g_file_test(argv[1], G_FILE_TEST_IS_DIR) ? argv[1] : g_get_home_dir());
	char *real = realpath(inicio, NULL);
	if (real) {
		snprintf(inicio, sizeof(inicio), "%s", real);
		free(real);
	}
	ar_cfg_ler(&cfg);
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
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Arquivos", .app_id = "lumna-atlas", .largura = 1240, .altura = 760 });
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
