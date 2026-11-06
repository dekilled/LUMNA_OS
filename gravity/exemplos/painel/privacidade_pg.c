/*
 * As páginas de apps e privacidade que mexem no que o app pode ou lembra:
 *   Aplicativos → Apps padrão            que app abre cada tipo de coisa (servicos/appspadrao.h, o mimeapps.list)
 *   Aplicativos → Permissões             o que os apps pediram, por permissão (servicos/permissoes.h, os portais)
 *   Privacidade → Permissões dos apps    o mesmo, por app
 *   Privacidade → Histórico e recentes   lembrar ou não os recentes, limpar os recentes e a área de transferência
 *   Privacidade → Diagnóstico            os relatórios (logs) deste computador: ver o tamanho e apagar
 */
#define _GNU_SOURCE
#include "painel.h"

#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "appspadrao.h"
#include "permissoes.h"
#include "privacidade.h"

static void nota(const char *id, const char *texto, bool erro) {
	if (texto && *texto) {
		CLAY(CLAY_SID(gv_fmt("%s", id)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 4 } } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, erro ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

/* ================= Apps padrão ================= */

#define NCAT 8
static struct padrao_categoria cats[NCAT];
static int escolha[NCAT], gravada[NCAT]; /* o índice em opcoes[] (-1: nenhum) */
static char aviso_padrao[200];

void pn_padrao_abrir(void) {
	for (int i = 0; i < NCAT && i < padrao_n(); i++) {
		padrao_ler(i, &cats[i]);
		escolha[i] = gravada[i] = -1;
		for (int k = 0; k < cats[i].nopcoes; k++) {
			if (!strcmp(cats[i].opcoes[k].id, cats[i].atual)) {
				escolha[i] = gravada[i] = k;
			}
		}
	}
	aviso_padrao[0] = '\0';
}

bool pn_padrao_pendente(void) {
	return memcmp(escolha, gravada, sizeof(escolha)) != 0;
}

void pn_padrao_aplicar(void) {
	bool tudo = true;
	for (int i = 0; i < NCAT && i < padrao_n(); i++) {
		if (escolha[i] != gravada[i] && escolha[i] >= 0) {
			tudo &= padrao_definir(i, cats[i].opcoes[escolha[i]].id);
		}
	}
	pn_padrao_abrir();
	g_strlcpy(aviso_padrao, tudo ? "" : "Algum app não pôde ser definido (ele ainda existe?).", sizeof(aviso_padrao));
}

void pn_padrao_descartar(void) {
	memcpy(escolha, gravada, sizeof(escolha));
}

void pn_padrao_desenhar(void) {
	for (int i = 0; i < NCAT && i < padrao_n(); i++) {
		struct padrao_categoria *c = &cats[i];
		if (c->nopcoes == 0) {
			pn_linha_comecar(CLAY_IDI("pd-vazio", i), c->icone, c->nome, "Nenhum app instalado sabe abrir isto");
			pn_linha_acabar();
			continue;
		}
		const char *nomes[PADRAO_MAX_OPCOES + 1];
		int n = 0;
		/* sem escolha: a lista começa por "Nenhum escolhido" (e o índice 0 dela) */
		int deslocar = escolha[i] < 0 ? 1 : 0;
		if (deslocar) {
			nomes[n++] = "Nenhum escolhido";
		}
		for (int k = 0; k < c->nopcoes; k++) {
			nomes[n++] = c->opcoes[k].nome;
		}
		int sel = escolha[i] < 0 ? 0 : escolha[i];
		if (pn_ajuste_lista(CLAY_IDI("pd-cat", i), c->icone, c->nome, c->sub, nomes, n, &sel)) {
			escolha[i] = sel - deslocar;
		}
	}
	nota("pd-aviso", aviso_padrao, true);
	nota("pd-dica", "Vale para todo o sistema: o que você escolhe aqui é o que abre quando você clica num link ou num arquivo.", false);
}

/* ================= Permissões (por permissão e por app) ================= */

static struct perm_entrada perms[PERM_MAX];
static int nperms; /* -1: o serviço não respondeu */

void pn_perm_abrir(void) {
	nperms = perm_listar(perms, PERM_MAX);
}

static const char *nome_do_app(const char *id, char *buf, size_t tam) {
	char *desktop = g_strconcat(id, ".desktop", NULL);
	GDesktopAppInfo *info = g_desktop_app_info_new(desktop);
	g_free(desktop);
	g_strlcpy(buf, info ? g_app_info_get_name(G_APP_INFO(info)) : id, tam);
	if (info) {
		g_object_unref(info);
	}
	return buf;
}

/* uma linha: o app (ou a permissão) à esquerda, o interruptor e o botão de tirar à direita */
static void linha_permissao(int i, bool por_app) {
	struct perm_entrada *e = &perms[i];
	char nome[200];
	const char *titulo = por_app ? e->nome : nome_do_app(e->app, nome, sizeof(nome));
	pn_linha_comecar(CLAY_IDI("pp-linha", i), por_app ? e->icone : "grid", titulo, por_app || !strcmp(titulo, e->app) ? NULL : e->app);
	if (e->muda) {
		bool pode = e->pode;
		if (gv_interruptor(CLAY_IDI("pp-liga", i), &pode)) {
			perm_definir(e, pode);
			pn_perm_abrir();
		}
	} else {
		gv_texto(gv_str(e->pode ? "Permitido" : "Negado"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	if (pn_confirmar(CLAY_IDI("pp-tira", i), "trash", "Tirar", "Tirar a permissão?", true)) {
		perm_tirar(e);
		pn_perm_abrir();
	}
	pn_linha_acabar();
}

static void perm_vazio(void) {
	if (nperms < 0) {
		nota("pp-sem", "O serviço de permissões não respondeu (o xdg-desktop-portal não está rodando).", true);
	} else if (nperms == 0) {
		nota("pp-nenhuma", "Nenhum app pediu permissão ainda.", false);
	}
	nota("pp-dica", "Aparecem aqui os apps em sandbox (Flatpak) e os que usam os portais do sistema, quando pedem a câmera, o microfone, a localização e outras coisas. "
			"Um app comum, instalado direto no sistema, não passa por aqui: o LUMNA não consegue limitá-lo.", false);
}

void pn_perm_por_permissao_desenhar(void) {
	/* agrupa por permissão: uma passada por tipo, na ordem em que aparecem */
	static const char *const ids[] = { "camera", "microphone", "speakers", "location", "screenshot", "notification", "background" };
	for (size_t t = 0; t < sizeof(ids) / sizeof(ids[0]); t++) {
		bool titulo = false;
		for (int i = 0; i < nperms; i++) {
			if (strcmp(perms[i].id, ids[t])) {
				continue;
			}
			if (!titulo) {
				pn_subtitulo(CLAY_IDI("pp-sub", (int)t), perms[i].nome);
				titulo = true;
			}
			linha_permissao(i, false);
		}
	}
	perm_vazio();
}

void pn_perm_por_app_desenhar(void) {
	const char *atual = "";
	int grupo = 0;
	for (int i = 0; i < nperms; i++) {
		if (strcmp(perms[i].app, atual)) {
			atual = perms[i].app;
			char nome[200];
			pn_subtitulo(CLAY_IDI("pa-sub", grupo++), nome_do_app(atual, nome, sizeof(nome)));
		}
		linha_permissao(i, true);
	}
	perm_vazio();
}

/* ================= Histórico e recentes ================= */

static struct privacidade priv;
static int nrecentes;
static char aviso_hist[200];

void pn_hist_abrir(void) {
	privacidade_ler(&priv);
	nrecentes = privacidade_recentes_n();
	aviso_hist[0] = '\0';
}

void pn_hist_desenhar(void) {
	pn_subtitulo(CLAY_ID("hi-s0"), "O que o LUMNA lembra");
	pn_linha_comecar(CLAY_ID("hi-lembrar"), "clock", "Lembrar os arquivos recentes", "Aparecem no lançador e nos apps que usam a lista de recentes do sistema");
	bool v = priv.lembrar_recentes;
	if (gv_interruptor(CLAY_ID("hi-lembrar-v"), &v)) {
		priv.lembrar_recentes = v;
		privacidade_gravar(&priv);
		if (!v) {
			privacidade_limpar_recentes();
			nrecentes = 0;
		}
	}
	pn_linha_acabar();
	pn_subtitulo(CLAY_ID("hi-s1"), "Limpar");
	pn_linha_comecar(CLAY_ID("hi-rec"), "trash", "Arquivos recentes",
		nrecentes > 0 ? gv_fmt("%d arquivo(s) na lista", nrecentes).chars : "A lista está vazia");
	if (nrecentes > 0 && pn_confirmar(CLAY_ID("hi-rec-limpar"), NULL, "Limpar", "Limpar a lista?", true)) {
		bool ok = privacidade_limpar_recentes();
		nrecentes = privacidade_recentes_n();
		g_strlcpy(aviso_hist, ok ? "Os recentes foram limpos." : "Não deu para limpar os recentes.", sizeof(aviso_hist));
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("hi-cop"), "clipboard", "Histórico da área de transferência", "Os últimos textos e imagens copiados (ficam só na memória)");
	if (pn_confirmar(CLAY_ID("hi-cop-limpar"), NULL, "Limpar", "Limpar o histórico?", true)) {
		g_strlcpy(aviso_hist, privacidade_limpar_copiados() ? "O histórico da área de transferência foi limpo." : "O Orbit não respondeu.", sizeof(aviso_hist));
	}
	pn_linha_acabar();
	nota("hi-aviso", aviso_hist, false);
}

/* ================= Diagnóstico ================= */

static struct log_arquivo logs[LOGS_MAX];
static int nlogs;
static char aviso_diag[200];

void pn_diag_abrir(void) {
	nlogs = logs_listar(logs, LOGS_MAX);
	aviso_diag[0] = '\0';
}

void pn_diag_desenhar(void) {
	nota("di-s0", "Os relatórios de erro do LUMNA ficam só neste computador. Nada é enviado a ninguém.", false);
	pn_subtitulo(CLAY_ID("di-s1"), "Relatórios deste computador");
	if (nlogs == 0) {
		nota("di-vazio", "Nenhum relatório guardado.", false);
	}
	int64_t soma = 0;
	for (int i = 0; i < nlogs; i++) {
		soma += logs[i].tamanho;
		pn_linha_comecar(CLAY_IDI("di-log", i), "info", logs[i].nome, gv_fmt("%s · %s", pn_tamanho(logs[i].tamanho), pn_dia(logs[i].quando)).chars);
		if (pn_confirmar(CLAY_IDI("di-apaga", i), "trash", "Apagar", "Apagar este relatório?", true)) {
			logs_apagar(logs[i].nome);
			pn_diag_abrir();
			break;
		}
		pn_linha_acabar();
	}
	if (nlogs > 0) {
		pn_linha_comecar(CLAY_ID("di-todos"), "trash", "Apagar todos", gv_fmt("%s no total", pn_tamanho(soma)).chars);
		if (pn_confirmar(CLAY_ID("di-todos-b"), NULL, "Apagar todos", "Apagar todos os relatórios?", true)) {
			logs_apagar("");
			pn_diag_abrir();
		}
		pn_linha_acabar();
	}
	pn_linha_comecar(CLAY_ID("di-pasta"), "folder", "Abrir a pasta dos relatórios", NULL);
	if (gv_botao(CLAY_ID("di-pasta-b"), NULL, "Abrir", GV_BOTAO_ELEVADO)) {
		char *pasta = logs_pasta(), *cmd = g_strdup_printf("xdg-open '%s'", pasta);
		g_mkdir_with_parents(pasta, 0755);
		pn_executar(cmd);
		g_free(cmd);
		g_free(pasta);
	}
	pn_linha_acabar();
	nota("di-aviso", aviso_diag, false);
}
