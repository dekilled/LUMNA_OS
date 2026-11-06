/*
 * Sistema → Idioma e região:
 *   Idioma               o idioma do sistema (LANG) e adicionar mais (gera o idioma, com a senha do administrador)
 *   Formatos regionais   como saem a data, os números, o dinheiro, o papel e as medidas (LC_*), com uma amostra
 *   Métodos de entrada   escrever noutros alfabetos: o fcitx5 e o ibus (ligar ao entrar, abrir a configuração, instalar)
 * Quem grava é o localectl (servicos/idioma.h). O que se muda vale a partir do próximo login; o LUMNA em si está em português.
 */
#define _GNU_SOURCE
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "autostart.h"
#include "idioma.h"

static struct idioma_cfg cfg, gravada;
static char (*geradas)[48];
static char (*nomes_buf)[96];
static const char **nomes;
static int ngeradas;
static char aviso[300];
static bool aviso_erro, ocupado;

static void nota(const char *id, const char *texto, bool erro) {
	if (texto && *texto) {
		CLAY(CLAY_SID(gv_fmt("%s", id)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 4 } } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, erro ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static void ler_tudo(void) {
	if (!geradas) {
		geradas = g_malloc((size_t)IDIOMA_MAX * 48);
		nomes_buf = g_malloc((size_t)(IDIOMA_MAX + 1) * 96);
		nomes = g_new0(const char *, IDIOMA_MAX + 1);
	}
	ngeradas = idioma_geradas(geradas, IDIOMA_MAX);
	idioma_ler(&gravada);
	cfg = gravada;
	/* o idioma de agora precisa estar na lista, mesmo que não esteja gerado (senão a lista não o mostra) */
	bool tem = !cfg.lang[0];
	for (int i = 0; i < ngeradas; i++) {
		tem |= !strcmp(geradas[i], cfg.lang);
	}
	if (!tem && ngeradas < IDIOMA_MAX) {
		g_strlcpy(geradas[ngeradas++], cfg.lang, 48);
	}
}

void pn_idioma_abrir(void) {
	ler_tudo();
	aviso[0] = '\0';
}

bool pn_idioma_pendente(void) {
	return memcmp(&cfg, &gravada, sizeof(cfg)) != 0;
}

struct pedido {
	struct idioma_cfg c;
	bool ok;
	char erro[240];
};

static void definir_trabalho(void *d) {
	struct pedido *p = d;
	p->ok = idioma_definir(&p->c, p->erro, sizeof(p->erro));
}

static void definir_pronto(void *d) {
	struct pedido *p = d;
	g_strlcpy(aviso, p->ok ? "Pronto. Vale a partir do próximo login." : p->erro, sizeof(aviso));
	aviso_erro = !p->ok;
	ocupado = false;
	idioma_ler(&gravada);
	if (!p->ok) {
		cfg = gravada;
	}
	g_free(p);
}

void pn_idioma_aplicar(void) {
	if (ocupado) {
		return;
	}
	struct pedido *p = g_new0(struct pedido, 1);
	p->c = cfg;
	ocupado = true;
	aviso[0] = '\0';
	gravada = cfg; /* o rodapé some; se falhar, ele volta */
	pn_tarefa(definir_trabalho, definir_pronto, p);
}

void pn_idioma_descartar(void) {
	cfg = gravada;
}

/* a lista de nomes do idioma (a posição de cada um em geradas[], com um "O mesmo do idioma" na frente se pedir) */
static int montar_nomes(bool com_igual) {
	int n = 0;
	if (com_igual) {
		g_strlcpy(nomes_buf[n], "O mesmo do idioma", 96);
		nomes[n] = nomes_buf[n];
		n++;
	}
	for (int i = 0; i < ngeradas; i++) {
		idioma_nome(geradas[i], nomes_buf[n], 96);
		nomes[n] = nomes_buf[n];
		n++;
	}
	return n;
}

static int indice_de(const char *locale, bool com_igual) {
	if (!*locale) {
		return 0;
	}
	for (int i = 0; i < ngeradas; i++) {
		if (!strcmp(geradas[i], locale)) {
			return i + (com_igual ? 1 : 0);
		}
	}
	return 0;
}

/* ---- Idioma ---- */

static char busca[64];
static bool adicionando;
static char (*suportados)[48];
static int nsuportados;

struct gerar_pedido {
	char locale[48];
	bool ok;
	char erro[240];
};

static void gerar_trabalho(void *d) {
	struct gerar_pedido *p = d;
	p->ok = idioma_gerar(p->locale, p->erro, sizeof(p->erro));
}

static void gerar_pronto(void *d) {
	struct gerar_pedido *p = d;
	char nome[96];
	idioma_nome(p->locale, nome, sizeof(nome));
	if (p->ok) {
		g_snprintf(aviso, sizeof(aviso), "%s foi adicionado. Já pode escolher.", nome);
		adicionando = false;
	} else {
		g_snprintf(aviso, sizeof(aviso), "Não deu para adicionar: %s", p->erro);
	}
	aviso_erro = !p->ok;
	ocupado = false;
	int antes = ngeradas;
	ler_tudo(); /* as escolhas pendentes se perdem: o idioma novo entra na lista */
	(void)antes;
	g_free(p);
}

void pn_idioma_desenhar(void) {
	int n = montar_nomes(false);
	int sel = indice_de(cfg.lang, false);
	pn_subtitulo(CLAY_ID("id-s0"), "O idioma do sistema");
	if (n == 0) {
		nota("id-vazio", "Nenhum idioma foi encontrado (o locale -a não respondeu).", true);
	} else if (pn_ajuste_lista(CLAY_ID("id-lang"), "globe", "Idioma", "Para os apps que seguem o idioma do sistema", nomes, n, &sel)) {
		g_strlcpy(cfg.lang, geradas[sel], sizeof(cfg.lang));
	}
	nota("id-dica", "O LUMNA em si está em português. O idioma muda os apps de fora (o navegador, os programas), os nomes de pastas e o teclado de fábrica.", false);
	pn_subtitulo(CLAY_ID("id-s1"), "Adicionar um idioma");
	if (gv_botao(CLAY_ID("id-add"), "plus", adicionando ? "Fechar a lista" : "Ver os idiomas que dá para adicionar", GV_BOTAO_ELEVADO)) {
		adicionando = !adicionando;
		busca[0] = '\0';
		if (adicionando && !suportados) {
			suportados = g_malloc((size_t)IDIOMA_MAX * 48);
		}
		if (adicionando) {
			nsuportados = idioma_suportados(suportados, IDIOMA_MAX);
		}
	}
	if (adicionando) {
		CLAY(CLAY_ID("id-busca-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 8 } } }) {
			gv_campo(CLAY_ID("id-busca"), busca, sizeof(busca), "Buscar (ex.: japonês, francês)", GV_CAMPO_BUSCA);
		}
		int mostrados = 0;
		for (int i = 0; i < nsuportados && mostrados < 8; i++) {
			char nome[96];
			idioma_nome(suportados[i], nome, sizeof(nome));
			if (*busca && !strcasestr(nome, busca) && !strcasestr(suportados[i], busca)) {
				continue;
			}
			mostrados++;
			pn_linha_comecar(CLAY_IDI("id-sup", i), "globe", nome, suportados[i]);
			if (gv_botao_ext(CLAY_IDI("id-sup-add", i), &(gv_botao_opcoes){ .texto = "Adicionar", .estilo = GV_BOTAO_ELEVADO, .desativado = ocupado })) {
				struct gerar_pedido *p = g_new0(struct gerar_pedido, 1);
				g_strlcpy(p->locale, suportados[i], sizeof(p->locale));
				ocupado = true;
				aviso[0] = '\0';
				pn_tarefa(gerar_trabalho, gerar_pronto, p);
			}
			pn_linha_acabar();
		}
		if (mostrados == 0) {
			nota("id-nada", nsuportados == 0 ? "Este sistema não tem a lista de idiomas para gerar (/usr/share/i18n/SUPPORTED)." : "Nenhum idioma com esse nome.", false);
		}
	}
	if (ocupado) {
		nota("id-ocupado", "Esperando a senha do administrador...", false);
	}
	nota("id-aviso", aviso, aviso_erro);
}

/* ---- Formatos regionais ---- */

static bool um_por_um;

void pn_formatos_desenhar(void) {
	int n = montar_nomes(true);
	/* o grupo: se os cinco são iguais, a lista mestra os mostra; senão "ajustar um por um" já abre */
	char *const campos[5] = { cfg.tempo, cfg.numeros, cfg.moeda, cfg.papel, cfg.medidas };
	bool iguais = true;
	for (int i = 1; i < 5; i++) {
		iguais &= !strcmp(campos[i], campos[0]);
	}
	if (!iguais) {
		um_por_um = true;
	}
	pn_subtitulo(CLAY_ID("fo-s0"), "A região dos formatos");
	if (!um_por_um) {
		int sel = indice_de(cfg.tempo, true);
		if (pn_ajuste_lista(CLAY_ID("fo-todos"), "text", "Região", "Data, hora, números, dinheiro, papel e medidas", nomes, n, &sel)) {
			for (int i = 0; i < 5; i++) {
				g_strlcpy(campos[i], sel == 0 ? "" : geradas[sel - 1], 48);
			}
		}
	} else {
		static const char *const titulos[5] = { "Data e hora", "Números", "Dinheiro", "Papel", "Medidas" };
		static const char *const subs[5] = { "Como saem as datas e as horas", "O ponto e a vírgula", "O símbolo e a forma", "A4 ou carta", "Métrico ou imperial" };
		for (int i = 0; i < 5; i++) {
			int sel = indice_de(campos[i], true);
			if (pn_ajuste_lista(CLAY_IDI("fo-um", i), "text", titulos[i], subs[i], nomes, n, &sel)) {
				g_strlcpy(campos[i], sel == 0 ? "" : geradas[sel - 1], 48);
			}
		}
	}
	if (gv_botao(CLAY_ID("fo-modo"), NULL, um_por_um ? "Usar uma região só" : "Ajustar um por um", GV_BOTAO_SIMPLES)) {
		um_por_um = !um_por_um;
		if (!um_por_um) {
			for (int i = 1; i < 5; i++) {
				g_strlcpy(campos[i], campos[0], 48); /* volta a uma só: a de cima vale para todos */
			}
		}
	}
	/* a amostra: como saem com o que está escolhido */
	pn_subtitulo(CLAY_ID("fo-s1"), "Amostra");
	const char *efetivas[3] = { *cfg.tempo ? cfg.tempo : cfg.lang, *cfg.numeros ? cfg.numeros : cfg.lang, *cfg.moeda ? cfg.moeda : cfg.lang };
	char d[64], num[64], mo[64], tmp1[64], tmp2[64], tmp3[64];
	idioma_amostra(efetivas[0], d, tmp1, tmp2, sizeof(d));
	idioma_amostra(efetivas[1], tmp1, num, tmp3, sizeof(num));
	idioma_amostra(efetivas[2], tmp1, tmp2, mo, sizeof(mo));
	pn_linha_comecar(CLAY_ID("fo-a-data"), "clock", "Data", d[0] ? d : "(este idioma ainda não está gerado)");
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("fo-a-num"), "text", "Número", num[0] ? num : "(este idioma ainda não está gerado)");
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("fo-a-din"), "card", "Dinheiro", mo[0] ? mo : "(este idioma ainda não está gerado)");
	pn_linha_acabar();
	nota("fo-aviso", aviso, aviso_erro);
	nota("fo-dica", "Vale a partir do próximo login. O formato 12 ou 24 horas do relógio do LUMNA fica em Data e hora.", false);
}

/* ---- Métodos de entrada ---- */

struct ime {
	const char *id, *nome, *sub, *programa, *comando, *config, *pacotes;
};

static const struct ime imes[] = {
	{ "fcitx5", "Fcitx5", "Chinês, japonês, coreano e outros alfabetos. Recomendado: funciona direto com o Orbit.", "fcitx5", "fcitx5 -d",
		"fcitx5-configtool", "fcitx5 fcitx5-configtool fcitx5-gtk fcitx5-qt" },
	{ "ibus", "IBus", "O outro sistema de métodos de entrada.", "ibus-daemon", "ibus-daemon -drx", "ibus-setup", "ibus" },
};

static struct autostart_item auto_itens[AUTOSTART_MAX];
static int nauto;

void pn_ime_abrir(void) {
	nauto = autostart_listar(auto_itens, AUTOSTART_MAX);
	aviso[0] = '\0';
}

static const struct autostart_item *auto_de(const char *id) {
	char arq[64];
	g_snprintf(arq, sizeof(arq), "%s.desktop", id);
	for (int i = 0; i < nauto; i++) {
		if (!strcmp(auto_itens[i].arquivo, arq)) {
			return &auto_itens[i];
		}
	}
	return NULL;
}

void pn_ime_desenhar(void) {
	pn_subtitulo(CLAY_ID("im-s0"), "Teclados");
	pn_linha_comecar(CLAY_ID("im-lay"), "keyboard", "Layouts de teclado", "Português, inglês e outros: adicionar, tirar e escolher a tecla que troca");
	if (gv_botao(CLAY_ID("im-lay-ir"), NULL, "Abrir", GV_BOTAO_ELEVADO)) {
		pn_ir("teclado/teclado");
	}
	pn_linha_acabar();
	pn_subtitulo(CLAY_ID("im-s1"), "Para escrever noutros alfabetos");
	for (size_t k = 0; k < sizeof(imes) / sizeof(imes[0]); k++) {
		const struct ime *m = &imes[k];
		bool instalado = g_find_program_in_path(m->programa) != NULL;
		const struct autostart_item *a = auto_de(m->id);
		pn_linha_comecar(CLAY_IDI("im-ime", (int)k), "keyboard", m->nome, instalado ? m->sub : gv_fmt("%s Não está instalado.", m->sub).chars);
		if (instalado) {
			bool liga = a && a->ligado;
			if (gv_interruptor(CLAY_IDI("im-liga", (int)k), &liga)) {
				if (liga && !a) {
					autostart_adicionar(m->nome, m->comando, "keyboard", NULL, 0);
				} else if (a) {
					autostart_definir(a->arquivo, liga);
				}
				pn_ime_abrir();
			}
			if (g_find_program_in_path(m->config) && gv_botao(CLAY_IDI("im-cfg", (int)k), NULL, "Configurar", GV_BOTAO_SIMPLES)) {
				pn_executar(m->config);
			}
		} else {
			char cmd[200];
			g_snprintf(cmd, sizeof(cmd), "sudo pacman -S --needed %s", m->pacotes);
			if (gv_botao(CLAY_IDI("im-inst", (int)k), "download", "Instalar", GV_BOTAO_ELEVADO)) {
				pn_terminal(cmd);
			}
		}
		pn_linha_acabar();
	}
	nota("im-dica", "Ligado, o programa abre sozinho ao entrar (em Apps ao iniciar também aparece). Depois de instalar, abra a Configurar e adicione o idioma que você quer.", false);
}
