/*
 * A janela de Rede das telas novas, em Gravity: as conexões em cartões que abrem (Ethernet com
 * IP, DNS e proxy em listas suspensas), o Wi-Fi e a VPN, e os botões em baixo. As conexões ainda
 * são de mentira: o NetworkManager vem com os serviços do sistema; mudar e Aplicar sai no
 * terminal.
 *   ./rede
 */
#include <stdio.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"

static const char *const ips[] = { "Automática (DHCP)", "Manual", "Somente local", "Desativado" };
static const char *const dnss[] = { "Automático", "Cloudflare (1.1.1.1)", "Google (8.8.8.8)", "Manual" };
static const char *const proxies[] = { "Desativado", "Automático (PAC)", "Manual" };
static int ip, dns, proxy;
static int aberto = 0; /* o cartão aberto (-1: nenhum) */

static void ponto(Clay_Color cor) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(9), CLAY_SIZING_FIXED(9) } }, .backgroundColor = cor,
		.cornerRadius = CLAY_CORNER_RADIUS(4.5f) }) {}
}

/* o cabeçalho de um cartão de conexão: ícone, nome, estado e a seta */
static bool cabeca(Clay_ElementId id, const char *icone, const char *nome, Clay_Color cor_estado, const char *estado,
		const char *seta) {
	bool tocado = gv_tocado(id);
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(80) }, .padding = { 36, 28, 0, 0 }, .childGap = 28,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande - 1),
	}) {
		gv_icone(icone, 32, gv_t.icone);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
			gv_texto(gv_str(nome), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (cor_estado.a > 0) {
					ponto(cor_estado);
				}
				gv_texto(gv_str(estado), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		gv_icone(seta, 20, gv_t.texto_secundario);
	}
	return tocado;
}

static Clay_ElementDeclaration cartao(bool destacado) {
	return (Clay_ElementDeclaration){
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) } },
		.backgroundColor = destacado ? gv_t.elevada : gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = destacado ? gv_alfa(gv_t.acento, 0.45f) : gv_t.borda, .width = GV_BORDA(1) },
	};
}

static void ajuste(Clay_ElementId id, const char *nome, const char *const *opcoes, int n, int *escolha) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(70) }, .padding = { 36, 32, 0, 0 },
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(nome), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_PERCENT(0.5f) } } }) {
			Clay_ElementId lista = Clay__HashStringWithOffset(id.stringId, 7000001u, id.baseId);
			if (gv_lista_suspensa(lista, opcoes, n, escolha)) {
				printf("%s: %s\n", nome, opcoes[*escolha]);
			}
		}
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	CLAY(CLAY_ID("janela"), {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie,
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) },
	}) {
		gv_barra_janela(app, CLAY_ID("barra"), "settings", "Rede");
		Clay_ElementId conteudo = CLAY_ID("conteudo");
		CLAY(conteudo, gv_rolagem(conteudo, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
			.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
				.padding = { 24, 24, 18, 12 }, .childGap = 12 } })) {
			CLAY(CLAY_ID("lead"), { .layout = { .padding = { 14, 0, 4, 6 } } }) {
				gv_texto(CLAY_STRING("Conexões e acesso à internet"), gv_t.tamanho_destaque, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			CLAY(CLAY_ID("ethernet"), cartao(aberto == 0)) {
				if (cabeca(CLAY_ID("eth-cabeca"), "ethernet", "Ethernet", gv_t.sucesso, "Conectado",
						aberto == 0 ? "chevron-up" : "chevron-down")) {
					aberto = aberto == 0 ? -1 : 0;
				}
				if (aberto == 0) {
					gv_separador(false, 0);
					ajuste(CLAY_ID("ip"), "Configuração de IP", ips, 4, &ip);
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 36, 32, 0, 0 } } }) {
						gv_separador(false, 0);
					}
					ajuste(CLAY_ID("dns"), "DNS", dnss, 4, &dns);
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 36, 32, 0, 0 } } }) {
						gv_separador(false, 0);
					}
					ajuste(CLAY_ID("proxy"), "Proxy", proxies, 3, &proxy);
					CLAY_AUTO_ID({ .layout = { .sizing = { .height = CLAY_SIZING_FIXED(8) } } }) {}
				}
			}
			CLAY(CLAY_ID("wifi"), cartao(false)) {
				if (cabeca(CLAY_ID("wifi-cabeca"), "wifi", "Wi-Fi", gv_t.texto_suave, "Disponível", "forward")) {
					printf("abrir: Wi-Fi\n");
				}
			}
			CLAY(CLAY_ID("vpn"), cartao(false)) {
				if (cabeca(CLAY_ID("vpn-cabeca"), "lock", "VPN", (Clay_Color){ 0 }, "Adicionar uma conexão VPN", "forward")) {
					printf("abrir: VPN\n");
				}
			}
		}
		CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 24, 24, 4, 20 }, .childGap = 12,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao(CLAY_ID("adicionar"), NULL, "Adicionar conexão", GV_BOTAO_ELEVADO)) {
				printf("adicionar conexão\n");
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			if (gv_botao(CLAY_ID("detalhes"), NULL, "Detalhes da conexão", GV_BOTAO_ELEVADO)) {
				printf("detalhes da conexão\n");
			}
			if (gv_botao(CLAY_ID("aplicar"), NULL, "Aplicar", GV_BOTAO_CHEIO)) {
				printf("aplicar: IP %s, DNS %s, proxy %s\n", ips[ip], dnss[dns], proxies[proxy]);
			}
		}
	}
	fflush(stdout);
	if (gv_tecla() == XKB_KEY_Escape) {
		gv_app_sair(app);
	}
}

int main(void) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Rede", .app_id = "lumna-rede", .largura = 860, .altura = 680 });
	if (!app) {
		return 1;
	}
	return gv_app_correr(app, desenhar, NULL);
}
