/*
 * Rede e internet → Várias conexões: usar duas internets ao mesmo tempo. Três jeitos:
 *   Dividir      cada conexão nova (um site, um download) sai por uma das redes, na proporção que se
 *                escolhe (rota multipath do kernel, pelo session/lumna-rede-mesclar, que pede a senha);
 *   Redundância  uma rede principal e outra de reserva: se a principal cair, a outra assume
 *                (as métricas de rota do NetworkManager, sem root);
 *   Somar        somar a velocidade de UMA conexão: precisa de um servidor do outro lado (MPTCP).
 *                Fica para quando houver o servidor.
 * Guarda a escolha em ~/.config/lumna/mesclar.ini.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rede.h"

extern void pn_rede_servico(void);

struct mesclar_cfg {
	int modo; /* 0 desligado, 1 dividir, 2 redundância, 3 somar (ainda não) */
	char principal[32], secundaria[32];
	int proporcao; /* índice em proporcoes[] */
};
static struct mesclar_cfg cfg, gravada;
static const struct mesclar_cfg fabrica = { 0 };

static const char *const modos[] = { "Desligado", "Dividir as conexões", "Redundância (reserva)", "Somar velocidades (em breve)" };
static const char *const proporcoes[] = { "Igual (1 : 1)", "A principal leva o dobro (2 : 1)", "A principal leva o triplo (3 : 1)",
	"A secundária leva o dobro (1 : 2)", "A secundária leva o triplo (1 : 3)" };
static const int pesos[][2] = { { 1, 1 }, { 2, 1 }, { 3, 1 }, { 1, 2 }, { 1, 3 } };

void pn_mesclar_abrir(void) {
	pn_rede_servico();
	rede_erro_limpar();
	gravada = fabrica;
	char c[1024];
	pn_caminho(c, sizeof(c), "mesclar.ini");
	char *texto = NULL;
	if (g_file_get_contents(c, &texto, NULL, NULL)) {
		char **linhas = g_strsplit(texto, "\n", -1);
		for (char **l = linhas; *l; l++) {
			char *igual = strchr(*l, '=');
			if (!igual || **l == '#') {
				continue;
			}
			*igual = '\0';
			char *k = g_strstrip(*l), *v = g_strstrip(igual + 1);
			if (!strcmp(k, "modo")) {
				gravada.modo = !strcmp(v, "dividir") ? 1 : !strcmp(v, "redundancia") ? 2 : 0;
			} else if (!strcmp(k, "principal")) {
				snprintf(gravada.principal, sizeof(gravada.principal), "%s", v);
			} else if (!strcmp(k, "secundaria")) {
				snprintf(gravada.secundaria, sizeof(gravada.secundaria), "%s", v);
			} else if (!strcmp(k, "proporcao")) {
				gravada.proporcao = atoi(v) % 5;
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	cfg = gravada;
}

bool pn_mesclar_pendente(void) {
	return cfg.modo != 3 && memcmp(&cfg, &gravada, sizeof(cfg)) != 0;
}

static const struct rede_disp *achar(const struct rede_estado *e, const char *iface) {
	for (int i = 0; i < e->ndisp; i++) {
		if (!strcmp(e->disp[i].iface, iface)) {
			return &e->disp[i];
		}
	}
	return NULL;
}

static const char *dns_da_conexao(const struct rede_estado *e, const char *uuid) {
	for (int i = 0; i < e->nsalvas; i++) {
		if (!strcmp(e->salvas[i].uuid, uuid)) {
			return e->salvas[i].dns;
		}
	}
	return "";
}

void pn_mesclar_aplicar(void) {
	struct rede_estado e;
	rede_estado_copiar(&e);
	const struct rede_disp *p = achar(&e, cfg.principal), *s = achar(&e, cfg.secundaria);
	/* as métricas de antes voltam ao automático (de quem estava na escolha gravada) */
	const struct rede_disp *antes[2] = { achar(&e, gravada.principal), achar(&e, gravada.secundaria) };
	if (gravada.modo == 2) {
		for (int i = 0; i < 2; i++) {
			if (antes[i] && antes[i]->conexao_uuid[0]) {
				rede_conexao_ajustar(antes[i]->conexao_uuid, dns_da_conexao(&e, antes[i]->conexao_uuid), -1, -1);
			}
		}
	}
	if (cfg.modo == 1 && p && s) {
		char cmd[300];
		snprintf(cmd, sizeof(cmd), "pkexec lumna-rede-mesclar ativar %s %s %d %d", p->iface, s->iface, pesos[cfg.proporcao][0], pesos[cfg.proporcao][1]);
		pn_executar(cmd);
	} else if (cfg.modo == 2 && p && s && p->conexao_uuid[0] && s->conexao_uuid[0]) {
		if (gravada.modo == 1) {
			pn_executar("pkexec lumna-rede-mesclar desligar");
		}
		/* a principal ganha na rota (métrica menor); a outra fica de reserva */
		rede_conexao_ajustar(p->conexao_uuid, dns_da_conexao(&e, p->conexao_uuid), 100, -1);
		rede_conexao_ajustar(s->conexao_uuid, dns_da_conexao(&e, s->conexao_uuid), 600, -1);
		rede_conectar_salva(p->conexao_uuid, p->caminho);
		rede_conectar_salva(s->conexao_uuid, s->caminho);
	} else if (cfg.modo == 0) {
		pn_executar("pkexec lumna-rede-mesclar desligar");
	}
	char texto[400];
	snprintf(texto, sizeof(texto), "# Várias conexões (o Painel de controle grava este arquivo)\nmodo = %s\nprincipal = %s\nsecundaria = %s\nproporcao = %d\n",
		cfg.modo == 1 ? "dividir" : cfg.modo == 2 ? "redundancia" : "desligado", cfg.principal, cfg.secundaria, cfg.proporcao);
	if (pn_gravar("mesclar.ini", texto)) {
		gravada = cfg;
	}
}

void pn_mesclar_descartar(void) {
	cfg = gravada;
}

void pn_mesclar_restaurar(void) {
	cfg = fabrica;
}

void pn_mesclar_desenhar(void) {
	pn_rede_servico();
	struct rede_estado e;
	rede_estado_copiar(&e);
	if (!e.nm) {
		gv_texto(CLAY_STRING("O NetworkManager não está respondendo. Veja Rede e internet → Wi-Fi."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	/* as ligadas, com gateway */
	const char *nomes[REDE_MAX_DISP];
	const char *ifaces[REDE_MAX_DISP];
	int n = 0;
	for (int i = 0; i < e.ndisp; i++) {
		if (e.disp[i].estado >= REDE_LIGADO && e.disp[i].gerenciado) {
			ifaces[n] = e.disp[i].iface;
			nomes[n++] = gv_fmt("%s · %s%s%s", e.disp[i].iface, e.disp[i].tipo == REDE_WIFI ? "Wi-Fi" : "Cabo",
				e.disp[i].barramento[0] ? " · " : "", e.disp[i].barramento[0] ? rede_barramento_nome(e.disp[i].barramento) : "").chars;
		}
	}
	pn_ajuste_lista(CLAY_ID("mc-modo"), "stack", "Como usar duas conexões", "Escolha o jeito; o que vale é o que está ligado agora", modos, 4, &cfg.modo);
	if (cfg.modo == 3) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 6 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Somar a velocidade de uma conexão só"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Para um download de verdade correr pelas duas redes ao mesmo tempo, é preciso um servidor do outro lado que"
				" junte os pedaços (MPTCP). Ainda não tem: por enquanto use Dividir, que reparte as conexões."), gv_t.tamanho_corpo,
				gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		return;
	}
	if (cfg.modo == 0) {
		gv_texto(CLAY_STRING("Ligar o modo Dividir ou Redundância precisa de duas redes conectadas ao mesmo tempo (por exemplo, "
			"Wi-Fi e cabo, ou duas antenas Wi-Fi)."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	if (n < 2) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 6 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Só há uma rede conectada"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Conecte uma segunda: outro adaptador Wi-Fi (uma antena USB serve), ou o cabo."), gv_t.tamanho_corpo,
				gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		return;
	}
	int ip = 0, is = n > 1 ? 1 : 0;
	for (int i = 0; i < n; i++) {
		if (!strcmp(ifaces[i], cfg.principal)) {
			ip = i;
		}
		if (!strcmp(ifaces[i], cfg.secundaria)) {
			is = i;
		}
	}
	if (ip == is) {
		is = (ip + 1) % n;
	}
	if (pn_ajuste_lista(CLAY_ID("mc-principal"), "signal", cfg.modo == 2 ? "Rede principal" : "Primeira rede", NULL, nomes, n, &ip) || !cfg.principal[0]) {
		snprintf(cfg.principal, sizeof(cfg.principal), "%s", ifaces[ip]);
	}
	if (pn_ajuste_lista(CLAY_ID("mc-secundaria"), "signal", cfg.modo == 2 ? "Rede de reserva" : "Segunda rede", NULL, nomes, n, &is) || !cfg.secundaria[0]) {
		snprintf(cfg.secundaria, sizeof(cfg.secundaria), "%s", ifaces[is]);
	}
	if (!strcmp(cfg.principal, cfg.secundaria)) {
		snprintf(cfg.secundaria, sizeof(cfg.secundaria), "%s", ifaces[(ip + 1) % n]);
	}
	if (cfg.modo == 1) {
		pn_ajuste_lista(CLAY_ID("mc-prop"), "sliders", "Proporção", "Quanto de cada rede entra nas conexões novas", proporcoes, 5, &cfg.proporcao);
		gv_texto(CLAY_STRING("Cada conexão fica numa rede só; várias juntas (abas, downloads, jogos) usam as duas. O Aplicar pede a senha de administrador."),
			gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	} else {
		gv_texto(CLAY_STRING("A rede principal fica com tudo; se ela cair, a de reserva assume sozinha e a principal volta quando reconectar."),
			gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}
