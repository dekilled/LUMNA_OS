/*
 * Inicialização: o que acontece ao ligar e ao entrar.
 *   Ao entrar        o modo com que cada tela começa e reabrir os apps da última sessão (inicio.ini e modos.ini; quem faz é o lumna-inicio)
 *   Apps ao iniciar  os .desktop de ~/.config/autostart (servicos/autostart.h): ligar, desligar, adicionar e tirar
 *   Entrada          login automático e o desenho da tela de login (pelo session/lumna-entrada-ajustar, com pkexec) e como se entra
 *   Em segundo plano os serviços do usuário (systemd --user): ligam ao entrar e rodam agora (servicos/segundoplano.h)
 */
#define _GNU_SOURCE
#include "painel.h"

#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "autostart.h"
#include "entrar.h"
#include "greetd.h"
#include "modos.h"
#include "pacotes.h"
#include "papelini.h"
#include "segundoplano.h"

/* uma linha de aviso (o resultado da última ação) */
static void nota(const char *id, const char *texto, bool erro) {
	if (texto && *texto) {
		CLAY(CLAY_SID(gv_fmt("%s", id)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 4 } } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, erro ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

/* ================= Ao entrar ================= */

struct entrar_ajustes {
	int modo;
	bool reabrir;
};
static struct entrar_ajustes ent, ent_gravada;

static bool reabrir_ler(void) {
	char c[1024], *texto = NULL;
	bool r = false;
	pn_caminho(c, sizeof(c), "inicio.ini");
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (igual && **l != '#') {
				*igual = '\0';
				if (!strcmp(g_strstrip(*l), "reabrir")) {
					r = !strcmp(g_strstrip(igual + 1), "sim");
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	return r;
}

void pn_entrar_abrir(void) {
	struct modos_cfg c;
	modos_cfg_ler(&c);
	ent_gravada.modo = c.ao_ligar;
	ent_gravada.reabrir = reabrir_ler();
	ent = ent_gravada;
}

bool pn_entrar_pendente(void) {
	return memcmp(&ent, &ent_gravada, sizeof(ent)) != 0;
}

void pn_entrar_aplicar(void) {
	struct modos_cfg c;
	modos_cfg_ler(&c);
	c.ao_ligar = ent.modo;
	bool ok = modos_cfg_gravar(&c);
	char texto[200];
	snprintf(texto, sizeof(texto), "# O que o lumna-inicio faz (o Painel de controle grava este arquivo)\nreabrir = %s\n", ent.reabrir ? "sim" : "nao");
	if (ok && pn_gravar("inicio.ini", texto)) {
		ent_gravada = ent;
	}
}

void pn_entrar_descartar(void) {
	ent = ent_gravada;
}

void pn_entrar_restaurar(void) {
	ent = (struct entrar_ajustes){ .modo = MODO_DESKTOP, .reabrir = false };
}

void pn_entrar_desenhar(void) {
	static const char *nomes[MODO_N];
	for (int m = 0; m < MODO_N; m++) {
		nomes[m] = modos_nome(m);
	}
	pn_subtitulo(CLAY_ID("en-s0"), "O modo com que o LUMNA abre");
	pn_ajuste_lista(CLAY_ID("en-modo"), "desktop", "Modo inicial", "O Desktop, o Gaming ou a Mídia na primeira tela ao entrar", nomes, MODO_N, &ent.modo);
	pn_subtitulo(CLAY_ID("en-s1"), "As janelas");
	pn_linha_comecar(CLAY_ID("en-reabrir"), "stack", "Reabrir as janelas da última sessão",
		"Abre de novo os apps que estavam abertos quando você saiu (os apps, não o que estava escrito neles)");
	gv_interruptor(CLAY_ID("en-reabrir-v"), &ent.reabrir);
	pn_linha_acabar();
	nota("en-n0", "Os apps que abrem sempre ficam em Apps ao iniciar.", false);
}

/* ================= Apps ao iniciar ================= */

static struct autostart_item itens[AUTOSTART_MAX];
static int nitens;
static int adicionando; /* 0 nada, 1 escolher um app instalado, 2 um comando */
static char busca[64], novo_nome[96], novo_cmd[300];
static char aviso_apps[240];
static bool aviso_apps_erro;

static void itens_ler(void) {
	nitens = autostart_listar(itens, AUTOSTART_MAX);
}

void pn_apps_ini_abrir(void) {
	itens_ler();
	adicionando = 0;
	busca[0] = novo_nome[0] = novo_cmd[0] = aviso_apps[0] = '\0';
}

static bool contem(const char *texto, const char *trecho) {
	return !*trecho || strcasestr(texto, trecho) != NULL;
}

/* um app instalado vira uma entrada: o nome e a linha de comando saem do .desktop dele */
static void adicionar_instalado(const struct pacote *p) {
	GDesktopAppInfo *info = p->app_id[0] ? g_desktop_app_info_new(p->app_id) : NULL;
	const char *cmd = info ? g_app_info_get_commandline(G_APP_INFO(info)) : p->exec;
	char *icone = info ? g_desktop_app_info_get_string(info, "Icon") : NULL;
	if (cmd && *cmd && autostart_adicionar(p->app_nome[0] ? p->app_nome : p->nome, cmd, icone, NULL, 0)) {
		g_strlcpy(aviso_apps, "Pronto: abre sozinho ao entrar.", sizeof(aviso_apps));
		aviso_apps_erro = false;
		adicionando = 0;
	} else {
		g_strlcpy(aviso_apps, "Não deu para adicionar esse app.", sizeof(aviso_apps));
		aviso_apps_erro = true;
	}
	g_free(icone);
	if (info) {
		g_object_unref(info);
	}
	itens_ler();
}

void pn_apps_ini_desenhar(void) {
	pn_subtitulo(CLAY_ID("ai-s0"), "Abrem sozinhos ao entrar");
	if (nitens == 0) {
		nota("ai-vazio", "Nenhum app abre sozinho. Adicione um abaixo.", false);
	}
	for (int i = 0; i < nitens; i++) {
		struct autostart_item *it = &itens[i];
		char sub[640];
		g_snprintf(sub, sizeof(sub), "%s%s", it->do_sistema && !it->do_usuario ? "Do sistema · " : "", it->comando);
		pn_linha_comecar(CLAY_IDI("ai-linha", i), it->icone[0] && !strchr(it->icone, '/') ? it->icone : "grid", it->nome, sub);
		bool ligado = it->ligado;
		if (gv_interruptor(CLAY_IDI("ai-liga", i), &ligado)) {
			if (autostart_definir(it->arquivo, ligado)) {
				aviso_apps[0] = '\0';
			}
			itens_ler();
		}
		if (pn_confirmar(CLAY_IDI("ai-tira", i), "trash", it->do_sistema && !it->do_usuario ? "Desligar" : "Tirar",
			    it->do_sistema && !it->do_usuario ? "Desligar para sempre?" : "Tirar da lista?", true)) {
			autostart_remover(it->arquivo);
			itens_ler();
		}
		pn_linha_acabar();
		if (i >= nitens) {
			break;
		}
	}
	pn_subtitulo(CLAY_ID("ai-s1"), "Adicionar");
	CLAY(CLAY_ID("ai-botoes"), { .layout = { .childGap = 10, .padding = { 4, 4, 4, 8 } } }) {
		if (gv_botao(CLAY_ID("ai-b-app"), "grid", "Escolher um app instalado", adicionando == 1 ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO)) {
			adicionando = adicionando == 1 ? 0 : 1;
			busca[0] = '\0';
		}
		if (gv_botao(CLAY_ID("ai-b-cmd"), "terminal", "Um comando meu", adicionando == 2 ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO)) {
			adicionando = adicionando == 2 ? 0 : 2;
		}
	}
	if (adicionando == 1) {
		const struct pacote **apps = NULL;
		int n = pn_apps_dados(&apps);
		CLAY(CLAY_ID("ai-busca-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 0, 8 } } }) {
			gv_campo(CLAY_ID("ai-busca"), busca, sizeof(busca), "Buscar entre os apps instalados", GV_CAMPO_BUSCA);
		}
		if (n == 0) {
			nota("ai-lendo", "Lendo os apps instalados...", false);
		}
		int mostrados = 0;
		for (int i = 0; i < n && mostrados < 8; i++) {
			const char *nome = apps[i]->app_nome[0] ? apps[i]->app_nome : apps[i]->nome;
			if (!apps[i]->app_id[0] || !contem(nome, busca)) {
				continue;
			}
			mostrados++;
			pn_linha_comecar(CLAY_IDI("ai-app", i), apps[i]->icone[0] && !strchr(apps[i]->icone, '/') ? apps[i]->icone : "grid", nome, apps[i]->descricao[0] ? apps[i]->descricao : NULL);
			if (gv_botao(CLAY_IDI("ai-app-add", i), "plus", "Adicionar", GV_BOTAO_ELEVADO)) {
				adicionar_instalado(apps[i]);
			}
			pn_linha_acabar();
		}
		if (n > 0 && mostrados == 0) {
			nota("ai-nada", "Nenhum app com esse nome.", false);
		}
	} else if (adicionando == 2) {
		CLAY(CLAY_ID("ai-cmd-caixa"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 0, 8 }, .childGap = 8 } }) {
			gv_campo(CLAY_ID("ai-nome"), novo_nome, sizeof(novo_nome), "Nome (como vai aparecer na lista)", GV_CAMPO_TEXTO);
			gv_campo(CLAY_ID("ai-cmd"), novo_cmd, sizeof(novo_cmd), "Comando (ex.: syncthing --no-browser)", GV_CAMPO_TEXTO);
			if (gv_botao_ext(CLAY_ID("ai-cmd-add"), &(gv_botao_opcoes){ .texto = "Adicionar", .estilo = GV_BOTAO_CHEIO, .desativado = !novo_nome[0] || !novo_cmd[0] })) {
				if (autostart_adicionar(novo_nome, novo_cmd, NULL, NULL, 0)) {
					g_strlcpy(aviso_apps, "Pronto: abre sozinho ao entrar.", sizeof(aviso_apps));
					aviso_apps_erro = false;
					novo_nome[0] = novo_cmd[0] = '\0';
					adicionando = 0;
				} else {
					g_strlcpy(aviso_apps, "Não deu para guardar (a pasta ~/.config/autostart aceita escrita?).", sizeof(aviso_apps));
					aviso_apps_erro = true;
				}
				itens_ler();
			}
		}
	}
	nota("ai-aviso", aviso_apps, aviso_apps_erro);
	nota("ai-dica", "Quem roda é o lumna-inicio, 2 s depois de a sessão subir.", false);
}

/* ================= Entrada ================= */

static char usuario_auto[64];    /* o login automático de agora ("" : desligado) */
static char layout_atual[16];    /* "centro" ou "lateral" */
static bool papel_proprio;       /* a tela de login tem um papel só dela */
static int layout_escolha;
static char aviso_ent[300];
static bool aviso_ent_erro, ocupado_ent;
static struct greetd_usuario pessoas[16];
static int npessoas, pessoa;

static const char *caminho_greetd(void) {
	const char *e = getenv("LUMNA_ETC");
	static char c[256];
	snprintf(c, sizeof(c), "%s/greetd/config.toml", e && *e ? e : "/etc");
	return c;
}

static const char *caminho_entrada_ini(void) {
	const char *e = getenv("LUMNA_ENTRADA_INI");
	static char c[256];
	if (e && *e) {
		return e;
	}
	snprintf(c, sizeof(c), "%s/lumna/entrada.ini", getenv("LUMNA_ETC") ? getenv("LUMNA_ETC") : "/etc");
	return c;
}

static void entrada_ler(void) {
	usuario_auto[0] = '\0';
	char *texto = NULL;
	if (g_file_get_contents(caminho_greetd(), &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		bool no_bloco = false;
		for (char **l = linhas; *l; l++) {
			char *t = g_strstrip(*l);
			if (*t == '[') {
				no_bloco = !strcmp(t, "[initial_session]");
			} else if (no_bloco && g_str_has_prefix(t, "user")) {
				char *a = strchr(t, '"'), *b = a ? strchr(a + 1, '"') : NULL;
				if (a && b) {
					g_strlcpy(usuario_auto, a + 1, (gsize)(b - a) < sizeof(usuario_auto) ? (gsize)(b - a) : sizeof(usuario_auto));
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	g_strlcpy(layout_atual, "centro", sizeof(layout_atual));
	papel_proprio = false;
	if (g_file_get_contents(caminho_entrada_ini(), &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (igual && **l != '#') {
				*igual = '\0';
				char *k = g_strstrip(*l), *v = g_strstrip(igual + 1);
				if (!strcmp(k, "layout")) {
					g_strlcpy(layout_atual, v, sizeof(layout_atual));
				} else if (!strcmp(k, "papel")) {
					papel_proprio = *v != '\0';
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	layout_escolha = !strcmp(layout_atual, "lateral") ? 1 : 0;
}

void pn_entrada_abrir(void) {
	entrada_ler();
	npessoas = greetd_usuarios(pessoas, 16);
	pessoa = 0;
	for (int i = 0; i < npessoas; i++) {
		if (!strcmp(pessoas[i].login, g_get_user_name())) {
			pessoa = i;
		}
	}
	aviso_ent[0] = '\0';
}

/* o ajudante com root, pelo pkexec (a senha é pedida na hora; o resultado volta numa tarefa) */
struct ajuste_pedido {
	char args[3][400];
	int n;
	bool ok;
	char erro[240];
};

static const char *ajudante(void) {
	const char *e = getenv("LUMNA_ENTRADA_AJUSTAR");
	return e && *e ? e : "/usr/lib/lumna/lumna-entrada-ajustar";
}

static void ajuste_trabalho(void *dados) {
	struct ajuste_pedido *p = dados;
	char *argv[8];
	int k = 0;
	const char *direto = getenv("LUMNA_ENTRADA_SEM_PKEXEC"); /* testes: sem a senha do administrador */
	if (!direto) {
		argv[k++] = "pkexec";
	}
	argv[k++] = (char *)ajudante();
	for (int i = 0; i < p->n; i++) {
		argv[k++] = p->args[i];
	}
	argv[k] = NULL;
	char *err = NULL;
	int estado = 0;
	bool rodou = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, NULL);
	p->ok = rodou && WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
	g_strlcpy(p->erro, err ? g_strstrip(err) : "", sizeof(p->erro));
	g_free(err);
}

static void ajuste_pronto(void *dados) {
	struct ajuste_pedido *p = dados;
	if (p->ok) {
		g_strlcpy(aviso_ent, "Pronto.", sizeof(aviso_ent));
	} else {
		g_snprintf(aviso_ent, sizeof(aviso_ent), "Não deu: %s", p->erro[0] ? p->erro : "faltou a senha do administrador ou o ajudante do sistema.");
	}
	aviso_ent_erro = !p->ok;
	ocupado_ent = false;
	entrada_ler();
	g_free(p);
}

static void ajustar(const char *a, const char *b, const char *c) {
	if (ocupado_ent) {
		return;
	}
	struct ajuste_pedido *p = g_new0(struct ajuste_pedido, 1);
	const char *v[3] = { a, b, c };
	for (int i = 0; i < 3; i++) {
		if (v[i]) {
			g_strlcpy(p->args[p->n++], v[i], sizeof(p->args[0]));
		}
	}
	ocupado_ent = true;
	aviso_ent[0] = '\0';
	pn_tarefa(ajuste_trabalho, ajuste_pronto, p);
}

void pn_entrada_desenhar(void) {
	pn_subtitulo(CLAY_ID("sa-s0"), "Login automático");
	bool auto_ligado = usuario_auto[0] != '\0';
	pn_linha_comecar(CLAY_ID("sa-auto"), "enter", "Entrar sozinho ao ligar o computador",
		auto_ligado ? gv_fmt("Entra como %s sem pedir a senha", usuario_auto).chars : "Desligado: a tela de login pede o usuário e a senha");
	bool v = auto_ligado;
	if (gv_interruptor(CLAY_ID("sa-auto-v"), &v) && !ocupado_ent) {
		if (v && npessoas > 0) {
			ajustar("login-automatico", "ligar", pessoas[pessoa].login);
		} else if (!v) {
			ajustar("login-automatico", "desligar", NULL);
		}
	}
	pn_linha_acabar();
	if (!auto_ligado && npessoas > 1) {
		static const char *nomes[16];
		for (int i = 0; i < npessoas && i < 16; i++) {
			nomes[i] = pessoas[i].nome[0] ? pessoas[i].nome : pessoas[i].login;
		}
		pn_ajuste_lista(CLAY_ID("sa-quem"), "user", "Entrar como", "Quem entra sozinho quando o interruptor acima é ligado", nomes, npessoas, &pessoa);
	}
	nota("sa-aviso-auto", "Quem ligar o computador entra na sua conta. A tela de bloqueio continua pedindo a senha ou o PIN.", false);

	pn_subtitulo(CLAY_ID("sa-s1"), "Como você entra");
	int pin = entrar_pin_tamanho();
	pn_linha_comecar(CLAY_ID("sa-como"), "lock", "Senha e PIN",
		pin > 0 ? gv_fmt("A senha do Linux e um PIN de %d números (o PIN vale na tela de bloqueio)", pin).chars : "A senha do Linux (sem PIN)");
	if (gv_botao(CLAY_ID("sa-como-ir"), NULL, "Trocar", GV_BOTAO_ELEVADO)) {
		pn_ir("usuarios/senha");
	}
	pn_linha_acabar();

	pn_subtitulo(CLAY_ID("sa-s2"), "A tela de login");
	static const char *const layouts[] = { "Centralizado (o cartão ao meio)", "Painel lateral (o painel à esquerda)" };
	if (pn_ajuste_lista(CLAY_ID("sa-layout"), "screens", "Desenho", "Muda a tela de login para todos os usuários", layouts, 2, &layout_escolha)) {
		ajustar("layout", layout_escolha == 1 ? "lateral" : "centro", NULL);
	}
	pn_linha_comecar(CLAY_ID("sa-papel"), "image", "Papel de parede da tela de login",
		papel_proprio ? "Uma imagem só dela" : "O de fábrica do LUMNA");
	struct papel_cfg cfg;
	papel_ler(&cfg);
	const char *saidas[8];
	int ns = pn_app ? gv_saidas(pn_app, saidas, 8) : 0;
	const struct papel_item *it = papel_para2(&cfg, papel_modo_atual(), "@principal", ns ? saidas[0] : "");
	const char *imagem = it && it->imagem[0] ? it->imagem : papel_padrao();
	if (imagem && *imagem && gv_botao(CLAY_ID("sa-papel-usar"), NULL, "Usar o do desktop", GV_BOTAO_ELEVADO)) {
		ajustar("papel", imagem, NULL);
	}
	if (papel_proprio && gv_botao(CLAY_ID("sa-papel-fab"), NULL, "De fábrica", GV_BOTAO_SIMPLES)) {
		ajustar("papel", "-", NULL);
	}
	pn_linha_acabar();
	if (ocupado_ent) {
		nota("sa-ocupado", "Esperando a senha do administrador...", false);
	}
	nota("sa-aviso", aviso_ent, aviso_ent_erro);
}

/* ================= Em segundo plano ================= */

static struct fundo_servico fundo[SEGUNDOPLANO_MAX];
static int nfundo;
static char busca_fundo[64];
static char aviso_fundo[240];
static bool aviso_fundo_erro;

static void fundo_ler(void) {
	nfundo = fundo_listar(fundo, SEGUNDOPLANO_MAX);
}

void pn_fundo_abrir(void) {
	fundo_ler();
	busca_fundo[0] = aviso_fundo[0] = '\0';
}

void pn_fundo_desenhar(void) {
	CLAY(CLAY_ID("sf-busca-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 10 } } }) {
		gv_campo(CLAY_ID("sf-busca"), busca_fundo, sizeof(busca_fundo), "Buscar um serviço", GV_CAMPO_BUSCA);
	}
	if (nfundo == 0) {
		nota("sf-vazio", "Nenhum serviço do usuário que se possa ligar ou desligar (ou o systemd do usuário não respondeu).", false);
	}
	int mostrados = 0;
	for (int i = 0; i < nfundo; i++) {
		struct fundo_servico *s = &fundo[i];
		if (!contem(s->unidade, busca_fundo) && !contem(s->descricao, busca_fundo)) {
			continue;
		}
		mostrados++;
		char nome[140];
		g_strlcpy(nome, s->unidade, sizeof(nome));
		char *ponto = strrchr(nome, '.');
		if (ponto) {
			*ponto = '\0';
		}
		pn_linha_comecar(CLAY_IDI("sf-linha", i), s->rodando ? "check" : "stack", nome,
			gv_fmt("%s · %s", s->rodando ? "Rodando" : "Parado", s->descricao[0] ? s->descricao : "sem descrição").chars);
		if (gv_botao(CLAY_IDI("sf-rodar", i), NULL, s->rodando ? "Parar" : "Iniciar", GV_BOTAO_SIMPLES)) {
			char erro[200] = "";
			aviso_fundo_erro = !fundo_rodar(s->unidade, !s->rodando, erro, sizeof(erro));
			g_strlcpy(aviso_fundo, aviso_fundo_erro ? erro : "", sizeof(aviso_fundo));
			fundo_ler();
		}
		bool liga = s->ligado;
		if (gv_interruptor(CLAY_IDI("sf-liga", i), &liga)) {
			char erro[200] = "";
			aviso_fundo_erro = !fundo_ligar(s->unidade, liga, erro, sizeof(erro));
			g_strlcpy(aviso_fundo, aviso_fundo_erro ? erro : "", sizeof(aviso_fundo));
			fundo_ler();
		}
		pn_linha_acabar();
	}
	if (nfundo > 0 && mostrados == 0) {
		nota("sf-nada", "Nenhum serviço com esse nome.", false);
	}
	nota("sf-aviso", aviso_fundo, aviso_fundo_erro);
	nota("sf-dica", "O interruptor liga o serviço ao entrar (e agora); Parar e Iniciar só mexem no que está rodando.", false);
}
