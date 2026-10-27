/*
 * Aplicativos → Instalados e Atualizações: a lista dos apps (pacman, AUR e AppImages) e, tocando num, a página dele:
 * origem, versão, tamanho, pasta, datas, o que ele precisa e quem precisa dele, se há atualização, desativar e
 * desinstalar (mostrando antes o que sairia junto e o que ficaria quebrado). Os dados vêm de servicos/pacotes.c.
 * O Armazenamento leva para cá com pn_apps_selecionar() + pn_ir("aplicativos/instalados").
 */
#define _GNU_SOURCE
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pacotes.h"
#include "sistema.h"

/* ---- o que está carregado ---- */

static struct pacotes_bd *bd;
static const struct pacote **apps;
static int napps;
static bool carregando;
static char selecionado[512];   /* o pacote da página aberta ("" : a lista) */
static char mensagem[400];      /* o resultado da última ação */
static bool mensagem_erro;
static bool trabalhando;

/* o que o checkupdates achou (guardado: o banco é relido depois de uma remoção) */
static struct sistema_atualizacao atu[SISTEMA_MAX_ATUALIZACOES];
static int natu = -2;       /* -2: não procurou; -1: não deu para saber */
static bool procurando;
static int natu_tarefa;

static void marcar_atualizacoes(void) {
	for (int i = 0; bd && i < natu && natu > 0; i++) {
		pacotes_marcar_atualizacao(bd, atu[i].nome, atu[i].nova);
	}
}

static void carregar_trabalho(void *dados) {
	*(struct pacotes_bd **)dados = pacotes_carregar();
}

static struct pacotes_bd *novo_bd;

static void carregar_pronto(void *dados) {
	pacotes_liberar(bd);
	bd = novo_bd;
	novo_bd = NULL;
	apps = NULL;
	napps = bd ? pacotes_apps(bd, &apps) : 0;
	carregando = false;
	marcar_atualizacoes(); /* a atualização que já se descobriu volta a valer no banco novo */
}

static void carregar(void) {
	if (!carregando) {
		carregando = true;
		pn_tarefa(carregar_trabalho, carregar_pronto, &novo_bd);
	}
}

static void procurar_trabalho(void *dados) {
	natu_tarefa = sistema_atualizacoes(atu, SISTEMA_MAX_ATUALIZACOES);
}

static void procurar_pronto(void *dados) {
	natu = natu_tarefa;
	procurando = false;
	marcar_atualizacoes();
}

static void procurar_atualizacoes(void) {
	if (!procurando) {
		procurando = true;
		pn_tarefa(procurar_trabalho, procurar_pronto, NULL);
	}
}

void pn_apps_selecionar(const char *pacote) {
	g_strlcpy(selecionado, pacote ? pacote : "", sizeof(selecionado));
	mensagem[0] = '\0';
}

/* os aplicativos já lidos (o Armazenamento mostra os maiores); NULL se ainda lendo */
int pn_apps_dados(const struct pacote ***lista) {
	if (!bd && !carregando) {
		carregar();
	}
	*lista = apps;
	return bd ? napps : 0;
}

void pn_apps_abrir(void) {
	mensagem[0] = '\0'; /* a lista começa a ser lida no primeiro quadro (a janela pode ainda não existir aqui) */
}

/* ---- a lista ---- */

static char busca[64];
static int filtro, ordem;

static int comparar_tamanho(const void *a, const void *b) {
	int64_t x = (*(const struct pacote *const *)a)->tamanho, y = (*(const struct pacote *const *)b)->tamanho;
	return x < y ? 1 : x > y ? -1 : 0;
}

static int comparar_data(const void *a, const void *b) {
	int64_t x = (*(const struct pacote *const *)a)->instalado_em, y = (*(const struct pacote *const *)b)->instalado_em;
	return x < y ? 1 : x > y ? -1 : 0;
}

static const char *origem_curta(const struct pacote *p) {
	return p->origem == PAC_APPIMAGE ? "AppImage" : p->fonte;
}

static const char *nome_do_app(const struct pacote *p) {
	return p->app_nome[0] ? p->app_nome : p->nome;
}

static void lista_desenhar(void) {
	if (carregando && !bd) {
		gv_texto(CLAY_STRING("Lendo os aplicativos instalados…"), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	if (!bd) {
		gv_texto(CLAY_STRING("Não achei o banco do pacman neste sistema, nem AppImages nas pastas de sempre."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	static const char *const filtros[] = { "Todos", "Pacman", "AUR", "AppImage" };
	static const char *const ordens[] = { "Nome", "Maiores", "Mais novos" };
	CLAY(CLAY_ID("ap-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 0, 10 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_ID("ap-busca-c"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_campo(CLAY_ID("ap-busca"), busca, sizeof(busca), "Buscar um aplicativo", GV_CAMPO_BUSCA);
		}
		CLAY(CLAY_ID("ap-filtro-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(140) } } }) {
			gv_lista_suspensa(CLAY_ID("ap-filtro"), filtros, 4, &filtro);
		}
		CLAY(CLAY_ID("ap-ordem-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			gv_lista_suspensa(CLAY_ID("ap-ordem"), ordens, 3, &ordem);
		}
	}
	/* a lista ordenada e filtrada (ponteiros para o banco) */
	const struct pacote **v = g_new(const struct pacote *, napps > 0 ? napps : 1);
	int n = 0;
	int64_t total = 0;
	for (int i = 0; i < napps; i++) {
		const struct pacote *p = apps[i];
		bool filtra = (filtro == 1 && p->origem != PAC_PACMAN) || (filtro == 2 && p->origem != PAC_AUR) || (filtro == 3 && p->origem != PAC_APPIMAGE);
		if (filtra || (busca[0] && !strcasestr(nome_do_app(p), busca) && !strcasestr(p->nome, busca))) {
			continue;
		}
		v[n++] = p;
		total += p->tamanho;
	}
	if (ordem == 1) {
		qsort(v, (size_t)n, sizeof(v[0]), comparar_tamanho);
	} else if (ordem == 2) {
		qsort(v, (size_t)n, sizeof(v[0]), comparar_data);
	}
	gv_texto(gv_fmt("%d aplicativos · %s", n, pn_tamanho(total)), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	if (mensagem[0]) {
		gv_texto(gv_str(mensagem), gv_t.tamanho_corpo - 1, mensagem_erro ? gv_t.perigo : gv_t.sucesso, GV_PESO_NORMAL);
	}
	for (int i = 0; i < n; i++) {
		const struct pacote *p = v[i];
		Clay_ElementId id = CLAY_IDI("ap-linha", (uint32_t)i);
		if (gv_tocado(id)) {
			g_strlcpy(selecionado, p->nome, sizeof(selecionado));
			mensagem[0] = '\0';
		}
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 10, 14, 8, 8 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			gv_focavel(id, gv_t.raio_medio);
			gv_icone_app(p->icone, 36);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_str(nome_do_app(p)), gv_t.tamanho_corpo + 1, p->desativado ? gv_t.texto_suave : gv_t.texto, GV_PESO_NORMAL);
				gv_texto(gv_fmt("%s · %s%s", origem_curta(p), pn_tamanho(p->tamanho), p->versao[0] ? gv_fmt(" · %s", p->versao).chars : ""), gv_t.tamanho_corpo - 2,
					gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (p->tem_atualizacao) {
				CLAY_AUTO_ID({ .layout = { .padding = { 8, 8, 3, 3 } }, .backgroundColor = gv_alfa(gv_t.acento, 0.18f), .cornerRadius = CLAY_CORNER_RADIUS(8) }) {
					gv_texto(CLAY_STRING("atualização"), gv_t.tamanho_corpo - 3, gv_t.acento, GV_PESO_MEDIO);
				}
			}
			if (p->desativado) {
				CLAY_AUTO_ID({ .layout = { .padding = { 8, 8, 3, 3 } }, .backgroundColor = gv_alfa(gv_t.texto_suave, 0.18f), .cornerRadius = CLAY_CORNER_RADIUS(8) }) {
					gv_texto(CLAY_STRING("desativado"), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_MEDIO);
				}
			}
			gv_icone("forward", 18, gv_t.texto_suave);
		}
	}
	g_free(v);
}

/* ---- a página de um app ---- */

static bool desinstalando;                 /* o painel "o que acontece" aberto */
static bool cascata;
static struct pacotes_remocao previa;
static bool tem_previa;

static void previa_liberar(void) {
	if (tem_previa) {
		pacotes_remocao_liberar(&previa);
	}
	tem_previa = false;
}

static void previa_calcular(const struct pacote *p) {
	previa_liberar();
	pacotes_previa_remocao(bd, p, cascata, &previa);
	tem_previa = true;
}

static void linha_info(uint32_t k, const char *nome, const char *valor) {
	pn_linha_comecar(CLAY_IDI("pg-info", k), NULL, nome, NULL);
	CLAY(CLAY_IDI("pg-info-v", k), { .layout = { .sizing = { CLAY_SIZING_FIT(0, 560) } } }) {
		gv_texto(gv_str(valor), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	pn_linha_acabar();
}

/* os nomes de uma lista, cada um um botãozinho que leva à página dele */
static void chips(uint32_t base, const char *titulo, GPtrArray *nomes) {
	if (!nomes->len) {
		return;
	}
	pn_subtitulo(CLAY_IDI("pg-ch-t", base), titulo);
	CLAY(CLAY_IDI("pg-ch", base), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6, .padding = { 4, 4, 0, 6 } } }) {
		/* uma linha de até uns 5 de cada vez (o Clay não quebra linha sozinho) */
		for (guint i = 0; i < nomes->len; i += 5) {
			CLAY(CLAY_IDI("pg-ch-l", base * 100 + i), { .layout = { .childGap = 8 } }) {
				for (guint k = i; k < nomes->len && k < i + 5; k++) {
					if (gv_botao(CLAY_IDI("pg-chip", base * 1000 + k), NULL, nomes->pdata[k], GV_BOTAO_ELEVADO)) {
						g_strlcpy(selecionado, nomes->pdata[k], sizeof(selecionado));
						desinstalando = false;
						previa_liberar();
						mensagem[0] = '\0';
					}
				}
			}
		}
	}
}

struct acao_remover {
	struct pacote p;
	bool cascata;
	int codigo;
	char erro[400];
};

static void remover_trabalho(void *dados) {
	struct acao_remover *a = dados;
	a->codigo = a->p.origem == PAC_APPIMAGE ? pacotes_remover_appimage(&a->p, a->erro, sizeof(a->erro)) : pacotes_remover(&a->p, a->cascata, a->erro, sizeof(a->erro));
}

static void remover_pronto(void *dados) {
	struct acao_remover *a = dados;
	if (a->codigo == 0) {
		g_snprintf(mensagem, sizeof(mensagem), "%s foi desinstalado.", a->p.app_nome[0] ? a->p.app_nome : a->p.nome);
		mensagem_erro = false;
		selecionado[0] = '\0';
		desinstalando = false;
		previa_liberar();
		carregar(); /* a lista e as contas mudaram */
	} else {
		g_snprintf(mensagem, sizeof(mensagem), "Não deu para desinstalar: %s", a->erro[0] ? a->erro : "o sistema recusou (a senha foi cancelada?)");
		mensagem_erro = true;
	}
	trabalhando = false;
	g_free(a);
}

static void abrir_o_app(const struct pacote *p) {
	/* o Exec do .desktop sem os códigos (%U, %f...) */
	char *cmd = g_strdup(p->exec);
	for (char *c = cmd; *c; c++) {
		if (*c == '%' && c[1]) {
			c[0] = c[1] = ' ';
		}
	}
	pn_executar(g_strstrip(cmd));
	g_free(cmd);
}

static const char *origem_longa(const struct pacote *p) {
	if (p->origem == PAC_APPIMAGE) {
		return "AppImage: um arquivo solto, fora do pacman";
	}
	if (p->origem == PAC_AUR) {
		return gv_fmt("%s: de fora dos repositórios oficiais", p->fonte).chars;
	}
	return "Repositório oficial (pacman)";
}

static void pagina_desenhar(const struct pacote *p) {
	bool appimage = p->origem == PAC_APPIMAGE;
	if (gv_botao(CLAY_ID("pg-voltar"), "back", "Todos os aplicativos", GV_BOTAO_SIMPLES)) {
		selecionado[0] = '\0';
		desinstalando = false;
		previa_liberar();
		return;
	}
	CLAY(CLAY_ID("pg-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 14 }, .childGap = 18, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone_app(p->icone, 64);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			gv_texto(gv_str(nome_do_app(p)), 26, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_fmt("%s%s%s", p->versao, p->versao[0] ? " · " : "", origem_curta(p)), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			if (p->descricao[0]) {
				gv_texto(gv_str(p->descricao), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
		if (p->exec[0] && !p->desativado && gv_botao(CLAY_ID("pg-abrir"), "play", "Abrir", GV_BOTAO_CHEIO)) {
			abrir_o_app(p);
		}
	}
	if (mensagem[0]) {
		gv_texto(gv_str(mensagem), gv_t.tamanho_corpo - 1, mensagem_erro ? gv_t.perigo : gv_t.sucesso, GV_PESO_NORMAL);
	}

	/* atualização */
	if (!appimage) {
		pn_linha_comecar(CLAY_ID("pg-atu"), "refresh", p->tem_atualizacao ? gv_fmt("Atualização disponível: %s", p->nova_versao).chars : "Atualizações",
			procurando ? "Procurando…" : p->tem_atualizacao ? "O Arch atualiza o sistema inteiro junto, para não quebrar nada" :
			natu == -2 ? "Ainda não procurei" : natu == -1 ? "Não consegui procurar (falta o checkupdates)" : "Este aplicativo está em dia");
		if (p->tem_atualizacao) {
			if (gv_botao(CLAY_ID("pg-atualizar"), "download", "Atualizar", GV_BOTAO_CHEIO)) {
				pn_terminal(p->origem == PAC_AUR ? "yay -Syu" : "if command -v yay >/dev/null 2>&1; then yay -Syu; else sudo pacman -Syu; fi");
			}
		} else if (gv_botao_ext(CLAY_ID("pg-procurar"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Procurar", .estilo = GV_BOTAO_ELEVADO, .desativado = procurando })) {
			procurar_atualizacoes();
		}
		pn_linha_acabar();
	}

	/* dados */
	uint32_t k = 0;
	linha_info(k++, "Origem", origem_longa(p));
	if (!appimage) {
		linha_info(k++, "Pacote", gv_fmt("%s %s", p->nome, p->versao).chars);
	} else {
		linha_info(k++, "Arquivo", p->caminho);
	}
	linha_info(k++, "Tamanho", pn_tamanho(p->tamanho));
	linha_info(k++, "Pasta de instalação", p->dir);
	linha_info(k++, appimage ? "Baixado em" : "Instalado em", pn_dia(p->instalado_em));
	if (!appimage) {
		linha_info(k++, "Compilado em", pn_dia(p->compilado_em));
	}
	linha_info(k++, "Última modificação", pn_dia(p->modificado_em));
	if (!appimage) {
		linha_info(k++, "Instalado como", p->dependencia ? "Dependência de outro pacote" : "Escolhido por você");
		if (p->licenca[0]) {
			linha_info(k++, "Licença", p->licenca);
		}
		if (p->empacotador[0]) {
			linha_info(k++, "Empacotado por", p->empacotador);
		}
	}
	if (p->url[0]) {
		pn_linha_comecar(CLAY_ID("pg-site"), "globe", "Site", NULL);
		if (gv_botao(CLAY_ID("pg-site-bt"), NULL, p->url, GV_BOTAO_SIMPLES)) {
			char *cmd = g_strdup_printf("xdg-open %s", g_shell_quote(p->url));
			pn_executar(cmd);
			g_free(cmd);
		}
		pn_linha_acabar();
	}

	/* o grafo */
	if (!appimage) {
		GPtrArray *dep = pacotes_depende_de(bd, p), *req = pacotes_necessario_por(bd, p), *opt = pacotes_opcional_para(bd, p);
		chips(1, "Depende de", dep);
		chips(2, "Necessário para (se sair, estes quebram)", req);
		chips(3, "Opcional para (perdem um recurso se sair)", opt);
		g_ptr_array_free(dep, TRUE);
		g_ptr_array_free(req, TRUE);
		g_ptr_array_free(opt, TRUE);
	}

	/* desativar e desinstalar */
	pn_subtitulo(CLAY_ID("pg-ac"), "Ações");
	if (p->app_id[0]) {
		pn_linha_comecar(CLAY_ID("pg-des"), "power", p->desativado ? "Ativar" : "Desativar",
			p->desativado ? "Volta a aparecer no lançador" : "Some do lançador, mas continua instalado (dá para ativar depois)");
		if (gv_botao(CLAY_ID("pg-des-bt"), NULL, p->desativado ? "Ativar" : "Desativar", GV_BOTAO_ELEVADO)) {
			if (pacotes_definir_ativo(p, p->desativado)) {
				((struct pacote *)p)->desativado = !p->desativado;
			} else {
				g_strlcpy(mensagem, "Não consegui mudar o lançador.", sizeof(mensagem));
				mensagem_erro = true;
			}
		}
		pn_linha_acabar();
	}
	if (!desinstalando) {
		pn_linha_comecar(CLAY_ID("pg-rm"), "trash", "Desinstalar", appimage ? "Apaga o arquivo do AppImage" : "Mostra antes o que sairia junto e o que ficaria quebrado");
		if (gv_botao(CLAY_ID("pg-rm-bt"), "trash", "Desinstalar…", GV_BOTAO_PERIGO)) {
			desinstalando = true;
			cascata = false;
			previa_calcular(p);
		}
		pn_linha_acabar();
		return;
	}
	/* o painel "o que acontece" */
	CLAY(CLAY_ID("pg-prev"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(18), .childGap = 10 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_texto(gv_fmt("O que acontece ao desinstalar %s", nome_do_app(p)), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		GString *lista = g_string_new(NULL);
		for (guint i = 0; i < previa.removidos->len; i++) {
			const struct pacote *q = appimage ? p : pacotes_achar(bd, previa.removidos->pdata[i]);
			g_string_append_printf(lista, "%s%s (%s)", i ? ", " : "", appimage ? p->caminho : (char *)previa.removidos->pdata[i], q ? pn_tamanho(q->tamanho) : "?");
		}
		gv_texto(gv_fmt("Sai: %s", lista->str), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
		g_string_free(lista, TRUE);
		gv_texto(gv_fmt("Libera %s no disco.", pn_tamanho(previa.liberado)), gv_t.tamanho_corpo - 1, gv_t.sucesso, GV_PESO_NORMAL);
		if (!appimage && previa.removidos->len > 1) {
			gv_texto(CLAY_STRING("Os pacotes que vão junto são dependências que só ele usava."), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		bool bloqueado = false;
		if (!appimage) {
			GPtrArray *req = pacotes_necessario_por(bd, p);
			if (req->len) {
				GString *q = g_string_new(NULL);
				for (guint i = 0; i < req->len; i++) {
					g_string_append_printf(q, "%s%s", i ? ", " : "", (char *)req->pdata[i]);
				}
				gv_texto(gv_fmt("Atenção: %s %s dele.", q->str, req->len > 1 ? "dependem" : "depende"), gv_t.tamanho_corpo - 1, gv_t.aviso, GV_PESO_MEDIO);
				bool c = cascata;
				pn_linha_comecar(CLAY_ID("pg-casc"), "info", "Remover também quem depende dele",
					"Sem isso o pacman recusa, porque quebraria os pacotes acima. Com isso, eles saem junto.");
				if (gv_interruptor(CLAY_ID("pg-casc-v"), &c) && c != cascata) {
					cascata = c;
					previa_calcular(p);
				}
				pn_linha_acabar();
				bloqueado = !cascata;
				g_string_free(q, TRUE);
			}
			g_ptr_array_free(req, TRUE);
		}
		CLAY(CLAY_ID("pg-prev-bt"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT, .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (trabalhando) {
				gv_texto(CLAY_STRING("Desinstalando… confirme a senha se o sistema pedir."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (gv_botao_ext(CLAY_ID("pg-rm-nao"), &(gv_botao_opcoes){ .texto = "Cancelar", .estilo = GV_BOTAO_ELEVADO, .desativado = trabalhando })) {
				desinstalando = false;
				previa_liberar();
			}
			if (gv_botao_ext(CLAY_ID("pg-rm-sim"), &(gv_botao_opcoes){ .icone = "trash", .texto = "Desinstalar", .estilo = GV_BOTAO_PERIGO, .desativado = bloqueado || trabalhando })) {
				struct acao_remover *a = g_new0(struct acao_remover, 1);
				a->p = *p;
				a->cascata = cascata;
				trabalhando = true;
				mensagem[0] = '\0';
				pn_tarefa(remover_trabalho, remover_pronto, a);
			}
		}
	}
}

void pn_apps_desenhar(void) {
	if (!bd && !carregando) {
		carregar();
	}
	if (selecionado[0] && bd) {
		const struct pacote *p = pacotes_achar(bd, selecionado);
		if (p) {
			pagina_desenhar(p);
			return;
		}
		selecionado[0] = '\0';
	}
	lista_desenhar();
}

/* ---- Atualizações ---- */

void pn_apps_atu_desenhar(void) {
	if (!bd && !carregando) {
		carregar();
	}
	CLAY(CLAY_ID("aa-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 14 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			if (procurando) {
				gv_texto(CLAY_STRING("Procurando atualizações…"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			} else if (natu == -2) {
				gv_texto(CLAY_STRING("Ainda não procurei"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			} else if (natu < 0) {
				gv_texto(CLAY_STRING("Não consegui procurar"), gv_t.tamanho_destaque, gv_t.aviso, GV_PESO_MEDIO);
			} else if (natu == 0) {
				gv_texto(CLAY_STRING("Tudo em dia"), gv_t.tamanho_destaque, gv_t.sucesso, GV_PESO_MEDIO);
			} else {
				int deapps = 0;
				for (int i = 0; i < napps; i++) {
					deapps += apps[i]->tem_atualizacao;
				}
				gv_texto(gv_fmt("%d atualizações (%d de aplicativos)", natu, deapps), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			}
		}
		if (gv_botao_ext(CLAY_ID("aa-procurar"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Procurar", .estilo = GV_BOTAO_ELEVADO, .desativado = procurando })) {
			procurar_atualizacoes();
		}
		if (natu > 0 && gv_botao(CLAY_ID("aa-todas"), "download", "Atualizar tudo", GV_BOTAO_CHEIO)) {
			pn_terminal("if command -v yay >/dev/null 2>&1; then yay -Syu; else sudo pacman -Syu; fi");
		}
	}
	for (int i = 0; i < napps; i++) {
		const struct pacote *p = apps[i];
		if (!p->tem_atualizacao) {
			continue;
		}
		Clay_ElementId id = CLAY_IDI("aa-linha", (uint32_t)i);
		if (gv_tocado(id)) {
			g_strlcpy(selecionado, p->nome, sizeof(selecionado));
			pn_ir("aplicativos/instalados");
		}
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 10, 14, 8, 8 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			gv_icone_app(p->icone, 36);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_str(nome_do_app(p)), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
				gv_texto(gv_fmt("%s  →  %s", p->versao, p->nova_versao), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			gv_icone("forward", 18, gv_t.texto_suave);
		}
	}
}
