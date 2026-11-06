/*
 * Sistema → Energia:
 *   Perfil de energia  economia, equilibrado ou desempenho (power-profiles-daemon) e trocar sozinho ao ir para a bateria
 *   Bateria            a carga, o que falta, a saúde e os ciclos (lidos do /sys) e a porcentagem na barra
 *   Tela e suspensão   apagar a tela e suspender depois de um tempo parado (quem faz é o lumna-ocioso)
 *   Botão de ligar     o que o botão faz (o Orbit roda o lumna-botao-ligar)
 * O que é de tempo e de botão vai para ~/.config/lumna/energia.ini no Aplicar; o perfil vale na hora.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bateria.h"
#include "perfil.h"

bool pn_barra_bateria_pct(void);
void pn_barra_bateria_definir(bool ligar);

/* ---- energia.ini ---- */

struct energia_ini {
	int tela;   /* minutos (0: nunca) */
	int susp;
	bool perfil_auto;
	int botao;  /* índice em botoes[] */
};
static const char *const botoes_id[] = { "perguntar", "desligar", "suspender", "hibernar", "nada" };
static const char *const botoes_nome[] = { "Perguntar o que fazer", "Desligar", "Suspender", "Hibernar", "Nada" };
#define NBOTOES 5
static const int tela_op[] = { 0, 1, 2, 3, 5, 10, 15, 30 };
static const int susp_op[] = { 0, 5, 10, 15, 20, 30, 45, 60 };
#define NOP 8
static struct energia_ini cfg, gravada;
static const struct energia_ini fabrica = { .tela = 10, .susp = 30, .perfil_auto = false, .botao = 0 };

static void ler_ini(void) {
	gravada = fabrica;
	char c[1024], *texto = NULL;
	pn_caminho(c, sizeof(c), "energia.ini");
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			char *k = g_strstrip(*l), *v = g_strstrip(igual + 1);
			if (!strcmp(k, "tela_apagar")) {
				gravada.tela = atoi(v);
			} else if (!strcmp(k, "suspender")) {
				gravada.susp = atoi(v);
			} else if (!strcmp(k, "perfil_auto")) {
				gravada.perfil_auto = !strcmp(v, "sim");
			} else if (!strcmp(k, "botao")) {
				for (int i = 0; i < NBOTOES; i++) {
					if (!strcmp(v, botoes_id[i])) {
						gravada.botao = i;
					}
				}
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	cfg = gravada;
}

bool pn_energia_pendente(void) {
	return memcmp(&cfg, &gravada, sizeof(cfg)) != 0;
}

void pn_energia_aplicar(void) {
	char texto[400];
	snprintf(texto, sizeof(texto), "# Energia (o Painel de controle grava este arquivo; o lumna-ocioso e o lumna-botao-ligar leem)\ntela_apagar = %d\nsuspender = %d\nperfil_auto = %s\nbotao = %s\n",
		cfg.tela, cfg.susp, cfg.perfil_auto ? "sim" : "nao", botoes_id[cfg.botao]);
	if (pn_gravar("energia.ini", texto)) {
		gravada = cfg;
	}
}

void pn_energia_descartar(void) {
	cfg = gravada;
}

void pn_energia_restaurar(void) {
	cfg = fabrica;
}

static void nota(const char *id, const char *texto, bool erro) {
	if (texto && *texto) {
		CLAY(CLAY_SID(gv_fmt("%s", id)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 4 } } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, erro ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static struct perfil_info perfil;
static struct bateria_info bateria;
static double bateria_lida;
static char aviso[240];
static bool aviso_erro;

void pn_energia_abrir(void) {
	ler_ini();
	perfil_ler(&perfil);
	bateria_ler(&bateria);
	bateria_lida = gv_agora();
	aviso[0] = '\0';
}

/* ---- Perfil de energia ---- */

void pn_perfil_desenhar(void) {
	if (!perfil.existe) {
		nota("pe-sem", "O serviço de perfis de energia (power-profiles-daemon) não está instalado ou não respondeu. Este computador pode não oferecer perfis.", true);
		return;
	}
	pn_subtitulo(CLAY_ID("pe-s0"), "Perfil");
	static const char *const descr[] = { "Mais rápido, gasta mais bateria e esquenta mais", "O padrão: bom para o dia a dia", "Gasta menos bateria e deixa tudo um pouco mais lento" };
	for (int i = 0; i < perfil.n; i++) {
		const char *id = perfil.lista[i];
		bool atual = !strcmp(id, perfil.atual);
		int d = !strcmp(id, "performance") ? 0 : !strcmp(id, "balanced") ? 1 : 2;
		pn_linha_comecar(CLAY_IDI("pe-perfil", i), !strcmp(id, "power-saver") ? "moon" : !strcmp(id, "performance") ? "zap" : "sliders", perfil_nome(id), descr[d]);
		if (atual) {
			gv_icone("check", 22, gv_t.acento);
		} else if (gv_botao(CLAY_IDI("pe-usar", i), NULL, "Usar", GV_BOTAO_ELEVADO)) {
			char erro[200] = "";
			aviso_erro = !perfil_definir(id, erro, sizeof(erro));
			g_strlcpy(aviso, aviso_erro ? erro : "", sizeof(aviso));
			perfil_ler(&perfil);
		}
		pn_linha_acabar();
	}
	if (perfil.degradado) {
		nota("pe-deg", "O desempenho está limitado agora (calor ou bateria baixa).", false);
	}
	nota("pe-aviso", aviso, aviso_erro);
	pn_subtitulo(CLAY_ID("pe-s1"), "Automático");
	pn_linha_comecar(CLAY_ID("pe-auto"), "battery", "Economia ao usar a bateria", "Troca para Economia quando tira da tomada e volta ao perfil de antes quando liga");
	gv_interruptor(CLAY_ID("pe-auto-v"), &cfg.perfil_auto);
	pn_linha_acabar();
}

/* ---- Bateria ---- */

static const char *duracao(int min) {
	return min >= 60 ? gv_fmt("%d h %02d min", min / 60, min % 60).chars : gv_fmt("%d min", min).chars;
}

void pn_bateria_desenhar(void) {
	if (gv_agora() - bateria_lida > 10) {
		bateria_ler(&bateria);
		bateria_lida = gv_agora();
	}
	gv_acordar_em(10);
	if (!bateria.existe) {
		nota("ba-sem", "Este computador não tem bateria.", false);
		return;
	}
	CLAY(CLAY_ID("ba-grande"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 18 }, .childGap = 4 } }) {
		gv_texto(gv_fmt("%d%%", bateria.pct), 44, gv_t.texto, GV_PESO_MEDIO);
		gv_texto(gv_str(bateria.estado), gv_t.tamanho_corpo + 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		/* a barra da carga */
		CLAY(CLAY_ID("ba-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(8) } }, .backgroundColor = gv_t.borda_controle, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_PERCENT(bateria.pct / 100.0f), CLAY_SIZING_GROW(0) } },
				.backgroundColor = bateria.pct <= 15 && !bateria.na_tomada ? gv_t.perigo : gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
		}
	}
	if (bateria.minutos >= 0) {
		pn_linha_comecar(CLAY_ID("ba-resta"), "clock", bateria.carregando ? "Falta para carregar" : "Dura ainda", duracao(bateria.minutos));
		pn_linha_acabar();
	}
	if (bateria.watts > 0) {
		pn_linha_comecar(CLAY_ID("ba-watts"), "zap", bateria.carregando ? "Entrando" : "Gastando", gv_fmt("%.1f W", bateria.watts).chars);
		pn_linha_acabar();
	}
	pn_subtitulo(CLAY_ID("ba-s1"), "A saúde da bateria");
	if (bateria.saude_pct >= 0) {
		pn_linha_comecar(CLAY_ID("ba-saude"), "battery", "Capacidade de hoje", gv_fmt("%d%% do que ela segurava de fábrica", bateria.saude_pct).chars);
		pn_linha_acabar();
	}
	if (bateria.ciclos >= 0) {
		pn_linha_comecar(CLAY_ID("ba-ciclos"), "refresh", "Ciclos de carga", gv_fmt("%d", bateria.ciclos).chars);
		pn_linha_acabar();
	}
	if (bateria.tecnologia[0]) {
		pn_linha_comecar(CLAY_ID("ba-tec"), "info", "Tipo", bateria.tecnologia);
		pn_linha_acabar();
	}
	pn_subtitulo(CLAY_ID("ba-s2"), "Na barra");
	static bool lido, mostra;
	if (!lido) {
		mostra = pn_barra_bateria_pct();
		lido = true;
	}
	pn_linha_comecar(CLAY_ID("ba-pct"), "battery", "Mostrar a porcentagem na barra", "O ícone da bateria fica sempre; isto liga o número ao lado");
	if (gv_interruptor(CLAY_ID("ba-pct-v"), &mostra)) {
		pn_barra_bateria_definir(mostra);
	}
	pn_linha_acabar();
}

/* ---- Tela e suspensão ---- */

static int indice(const int *op, int valor) {
	for (int i = 0; i < NOP; i++) {
		if (op[i] == valor) {
			return i;
		}
	}
	return 0;
}

static void nomes_tempo(const int *op, const char **nomes, char buf[][24]) {
	for (int i = 0; i < NOP; i++) {
		if (op[i] == 0) {
			g_strlcpy(buf[i], "Nunca", 24);
		} else if (op[i] >= 60 && op[i] % 60 == 0) {
			g_snprintf(buf[i], 24, "%d h", op[i] / 60);
		} else {
			g_snprintf(buf[i], 24, "%d min", op[i]);
		}
		nomes[i] = buf[i];
	}
}

void pn_suspensao_desenhar(void) {
	static char buf_t[NOP][24], buf_s[NOP][24];
	const char *nt[NOP], *ns[NOP];
	nomes_tempo(tela_op, nt, buf_t);
	nomes_tempo(susp_op, ns, buf_s);
	int it = indice(tela_op, cfg.tela), is = indice(susp_op, cfg.susp);
	pn_subtitulo(CLAY_ID("su-s0"), "Quando o computador fica parado");
	if (pn_ajuste_lista(CLAY_ID("su-tela"), "screens", "Apagar a tela depois de", "Qualquer movimento do mouse ou tecla acende de novo", nt, NOP, &it)) {
		cfg.tela = tela_op[it];
	}
	if (pn_ajuste_lista(CLAY_ID("su-susp"), "moon", "Suspender depois de", "O computador dorme (a tela de bloqueio aparece ao acordar)", ns, NOP, &is)) {
		cfg.susp = susp_op[is];
	}
	nota("su-dica", "Um vídeo tocando segura a tela acesa e impede a suspensão. A tela de bloqueio tem o tempo dela em Aparência → Tela de bloqueio.", false);
	if (cfg.susp > 0 && cfg.tela > 0 && cfg.susp <= cfg.tela) {
		nota("su-aviso", "O computador vai dormir antes de a tela apagar: aumente o tempo de suspensão ou diminua o da tela.", false);
	}
}

/* ---- Botão de ligar ---- */

void pn_botao_desenhar(void) {
	pn_subtitulo(CLAY_ID("bo-s0"), "O que o botão de ligar faz");
	pn_ajuste_lista(CLAY_ID("bo-acao"), "power", "Ao apertar o botão", "O botão do computador, ou a tecla de ligar do teclado", botoes_nome, NBOTOES, &cfg.botao);
	nota("bo-dica", "Perguntar abre o menu de desligar (bloquear, suspender, reiniciar e desligar). Segurar o botão por alguns segundos desliga na força, como sempre.", false);
}
