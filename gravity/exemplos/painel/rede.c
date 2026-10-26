/*
 * Rede e internet: Wi-Fi, Cabo, Adaptadores, Proxy e Várias conexões (mesclar.c), tudo pelo
 * NetworkManager (servicos/rede.h). O que se faz aqui vale na hora (ligar, conectar, esquecer):
 * não há Aplicar, a não ser no Proxy. A topbar guarda os atalhos rápidos; o Painel cuida do resto.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rede.h"

#define N(lista) (int)(sizeof(lista) / sizeof(lista[0]))

/* ---- o serviço, ligado uma vez e vigiado pelo laço do Painel ---- */

static bool servico_ligado;

static void servico_mudou(gv_app *app, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	gv_app_redesenhar(app);
}

void pn_rede_servico(void) {
	/* ao abrir direto numa seção (--secao) a janela ainda não existe: fica para o primeiro desenho */
	if (servico_ligado || !pn_app) {
		return;
	}
	int fd = rede_iniciar();
	if (fd >= 0) {
		gv_app_vigiar(pn_app, fd, servico_mudou, NULL);
	}
	servico_ligado = true;
	rede_varrer("");
}

/* ---- ajudas ---- */

static const char *relativo(int64_t t) {
	if (t <= 0) {
		return "nunca";
	}
	int64_t d = (int64_t)time(NULL) - t;
	if (d < 90) {
		return "agora há pouco";
	}
	if (d < 3600) {
		return gv_fmt("há %d min", (int)(d / 60)).chars;
	}
	if (d < 86400) {
		return gv_fmt("há %d h", (int)(d / 3600)).chars;
	}
	if (d < 86400 * 60) {
		return gv_fmt("há %d dia%s", (int)(d / 86400), d >= 86400 * 2 ? "s" : "").chars;
	}
	return "há muito tempo";
}

static void aviso_nm(const struct rede_estado *e) {
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16),
			.childGap = 6 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_texto(CLAY_STRING("O NetworkManager não está respondendo"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		gv_texto(CLAY_STRING("A rede do LUMNA usa o NetworkManager, que já vem no Arch. Para ligar:"), gv_t.tamanho_corpo,
			gv_t.texto_secundario, GV_PESO_NORMAL);
		gv_texto(CLAY_STRING("sudo pacman -S --needed networkmanager"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		gv_texto(CLAY_STRING("sudo systemctl enable --now NetworkManager"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
	}
}

/* o erro do último comando, com um botão para dispensar */
static void erro_banner(const struct rede_estado *e) {
	if (!e->erro[0]) {
		return;
	}
	CLAY(CLAY_ID("rede-erro"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 8, 10, 10 }, .childGap = 10,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_alfa(gv_t.perigo, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = gv_alfa(gv_t.perigo, 0.5f), .width = GV_BORDA(1) } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			char curto[160];
			snprintf(curto, sizeof(curto), "%s", e->erro);
			char *nl = strchr(curto, '\n');
			if (nl) {
				*nl = '\0';
			}
			gv_texto(gv_str(gv_fmt("%s", curto).chars), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_botao(CLAY_ID("rede-erro-x"), "close", NULL, GV_BOTAO_SIMPLES)) {
			rede_erro_limpar();
		}
	}
}

static void vazio(const char *titulo, const char *dica) {
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 16, 16, 18, 18 },
			.childGap = 6 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(titulo), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		gv_texto(gv_str(dica), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

/* um rótulo pequeno de informação ("Onboard (PCI)") */
static void etiqueta(const char *texto, bool destaque) {
	CLAY_AUTO_ID({ .layout = { .padding = { 8, 8, 2, 3 } },
		.backgroundColor = destaque ? gv_alfa(gv_t.acento, 0.18f) : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = destaque ? gv_alfa(gv_t.acento, 0.5f) : gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo - 2, destaque ? gv_t.acento : gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

/* ---- Wi-Fi ---- */

static int wifi_escolhido;           /* qual adaptador de Wi-Fi mostra, se houver mais de um */
static char ap_aberto[96];           /* o ponto de acesso com a senha em edição */
static char ap_senha[128];
static char salva_aberta[40];        /* a conexão guardada com os detalhes abertos */
static char salva_dns[96];
static bool manual_aberto;
static char man_ssid[40], man_senha[128];
static int man_seguranca = 1;
static bool man_oculta;

void pn_wifi_abrir(void) {
	pn_rede_servico();
	rede_erro_limpar();
	ap_aberto[0] = ap_senha[0] = salva_aberta[0] = '\0';
	manual_aberto = false;
	man_ssid[0] = man_senha[0] = '\0';
	rede_varrer("");
}

static bool disp_de_wifi(const struct rede_estado *e, int n, const struct rede_disp **saida) {
	int k = 0;
	for (int i = 0; i < e->ndisp; i++) {
		if (e->disp[i].tipo == REDE_WIFI) {
			if (k == n) {
				*saida = &e->disp[i];
				return true;
			}
			k++;
		}
	}
	return false;
}

static int contar(const struct rede_estado *e, enum rede_tipo tipo) {
	int k = 0;
	for (int i = 0; i < e->ndisp; i++) {
		k += e->disp[i].tipo == tipo;
	}
	return k;
}

static void conectado_card(const struct rede_estado *e, const struct rede_disp *d) {
	CLAY(CLAY_ID("wifi-atual"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 16, 12, 12, 12 }, .childGap = 14,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_alfa(gv_t.acento, 0.10f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
		.border = { .color = gv_alfa(gv_t.acento, 0.45f), .width = GV_BORDA(1) } }) {
		gv_icone("wifi", 26, gv_t.acento);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			gv_texto(gv_str(d->conexao), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(gv_fmt("Conectado%s%s%s", d->ip[0] ? " · " : "", d->ip, e->internet ? " · com internet" : " · sem internet").chars),
				gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		if (gv_botao(CLAY_ID("wifi-desconectar"), NULL, "Desconectar", GV_BOTAO_PERIGO)) {
			rede_desconectar(d->caminho);
		}
	}
}

static void ap_linha(const struct rede_estado *e, const struct rede_ap *a, int i) {
	Clay_ElementId id = CLAY_IDI("ap", (uint32_t)i);
	bool aberto = !strcmp(ap_aberto, a->caminho);
	pn_linha_comecar(id, a->segura ? "lock" : "wifi", a->ssid,
		gv_fmt("%d%%%s%s", a->sinal, a->salva ? " · guardada" : "", a->segura ? "" : " · aberta").chars);
	if (a->atual) {
		etiqueta("Conectada", true);
	} else if (!aberto) {
		bool precisa_senha = a->segura && !a->salva;
		if (gv_botao(CLAY_IDI("ap-ligar", (uint32_t)i), NULL, "Conectar", GV_BOTAO_ELEVADO)) {
			if (precisa_senha) {
				snprintf(ap_aberto, sizeof(ap_aberto), "%s", a->caminho);
				ap_senha[0] = '\0';
				gv_focar(CLAY_ID("ap-senha"));
			} else {
				rede_conectar(a->disp, a->caminho, "");
			}
		}
	}
	pn_linha_acabar();
	if (aberto) {
		CLAY(CLAY_IDI("ap-form", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 44, 0, 4, 12 }, .childGap = 10,
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(280) } } }) {
				if (gv_campo(CLAY_ID("ap-senha"), ap_senha, sizeof(ap_senha), "Senha da rede", GV_CAMPO_SENHA) & GV_CAMPO_ENTER) {
					if (ap_senha[0]) {
						rede_conectar(a->disp, a->caminho, ap_senha);
						ap_aberto[0] = '\0';
					}
				}
			}
			if (gv_botao_ext(CLAY_ID("ap-entrar"), &(gv_botao_opcoes){ .texto = "Conectar", .estilo = GV_BOTAO_CHEIO,
				    .desativado = ap_senha[0] == '\0' })) {
				rede_conectar(a->disp, a->caminho, ap_senha);
				ap_aberto[0] = '\0';
				memset(ap_senha, 0, sizeof(ap_senha));
			}
			if (gv_botao(CLAY_ID("ap-cancelar"), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
				ap_aberto[0] = '\0';
				memset(ap_senha, 0, sizeof(ap_senha));
			}
		}
	}
}

static void salva_linha(const struct rede_estado *e, const struct rede_salva *s, int i) {
	Clay_ElementId id = CLAY_IDI("salva", (uint32_t)i);
	bool aberta = !strcmp(salva_aberta, s->uuid);
	pn_linha_comecar(id, s->tipo == REDE_WIFI ? "wifi" : "ethernet", s->nome,
		gv_fmt("Última vez: %s%s%s", s->ativa ? "agora" : relativo(s->ultimo_uso), s->dns[0] ? " · DNS " : "", s->dns).chars);
	if (s->ativa) {
		etiqueta("Ligada", true);
	} else if (gv_botao(CLAY_IDI("salva-ligar", (uint32_t)i), NULL, "Conectar", GV_BOTAO_ELEVADO)) {
		rede_conectar_salva(s->uuid, "");
	}
	if (gv_botao(CLAY_IDI("salva-mais", (uint32_t)i), aberta ? "chevron-up" : "chevron-down", NULL, GV_BOTAO_SIMPLES)) {
		if (aberta) {
			salva_aberta[0] = '\0';
		} else {
			snprintf(salva_aberta, sizeof(salva_aberta), "%s", s->uuid);
			snprintf(salva_dns, sizeof(salva_dns), "%s", s->dns);
		}
	}
	pn_linha_acabar();
	if (aberta) {
		CLAY(CLAY_IDI("salva-detalhe", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
				.padding = { 44, 0, 4, 14 }, .childGap = 10 } }) {
			gv_texto(CLAY_STRING("DNS (separe por vírgula; vazio usa os da rede)"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(300) } } }) {
					gv_campo(CLAY_ID("salva-dns"), salva_dns, sizeof(salva_dns), "Ex.: 1.1.1.1, 1.0.0.1", GV_CAMPO_TEXTO);
				}
				if (gv_botao(CLAY_ID("salva-dns-ok"), NULL, "Salvar DNS", GV_BOTAO_ELEVADO)) {
					rede_conexao_ajustar(s->uuid, salva_dns, -2, -1);
				}
				if (gv_botao(CLAY_ID("dns-cloudflare"), NULL, "Cloudflare", GV_BOTAO_SIMPLES)) {
					snprintf(salva_dns, sizeof(salva_dns), "1.1.1.1, 1.0.0.1");
				}
				if (gv_botao(CLAY_ID("dns-google"), NULL, "Google", GV_BOTAO_SIMPLES)) {
					snprintf(salva_dns, sizeof(salva_dns), "8.8.8.8, 8.8.4.4");
				}
			}
			CLAY_AUTO_ID({ .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				bool autom = s->automatica;
				gv_texto(CLAY_STRING("Conectar sozinho quando estiver ao alcance"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				if (gv_interruptor(CLAY_ID("salva-auto"), &autom)) {
					rede_conexao_ajustar(s->uuid, s->dns, -2, autom ? 1 : 0);
				}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
				if (gv_botao(CLAY_ID("salva-esquecer"), "trash", "Esquecer esta rede", GV_BOTAO_PERIGO)) {
					rede_esquecer(s->uuid);
					salva_aberta[0] = '\0';
				}
			}
		}
	}
}

static void form_manual(const struct rede_disp *d) {
	CLAY(CLAY_ID("man-form"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16),
			.childGap = 12 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
		.border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(340) } } }) {
			gv_campo(CLAY_ID("man-ssid"), man_ssid, sizeof(man_ssid), "Nome da rede (SSID)", GV_CAMPO_TEXTO);
		}
		CLAY_AUTO_ID({ .layout = { .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			static const char *const seg[] = { "Aberta", "WPA2", "WPA3" };
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(200) } } }) {
				gv_lista_suspensa(CLAY_ID("man-seg"), seg, 3, &man_seguranca);
			}
			if (man_seguranca > 0) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(300) } } }) {
					gv_campo(CLAY_ID("man-senha"), man_senha, sizeof(man_senha), "Senha", GV_CAMPO_SENHA);
				}
			}
		}
		CLAY_AUTO_ID({ .layout = { .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_interruptor(CLAY_ID("man-oculta"), &man_oculta);
			gv_texto(CLAY_STRING("Rede oculta (o nome não aparece na busca)"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		bool pronto = man_ssid[0] && (man_seguranca == 0 || strlen(man_senha) >= 8);
		CLAY_AUTO_ID({ .layout = { .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			if (gv_botao_ext(CLAY_ID("man-ok"), &(gv_botao_opcoes){ .texto = "Adicionar e conectar", .estilo = GV_BOTAO_CHEIO, .desativado = !pronto })) {
				rede_adicionar(d ? d->caminho : "", man_ssid, man_seguranca, man_senha, man_oculta);
				manual_aberto = false;
				memset(man_senha, 0, sizeof(man_senha));
				man_ssid[0] = '\0';
			}
			if (man_seguranca > 0 && man_senha[0] && strlen(man_senha) < 8) {
				gv_texto(CLAY_STRING("A senha tem de ter pelo menos 8 caracteres"), gv_t.tamanho_corpo - 1, gv_t.aviso, GV_PESO_NORMAL);
			}
		}
	}
}

void pn_wifi_desenhar(void) {
	pn_rede_servico();
	struct rede_estado e;
	rede_estado_copiar(&e);
	if (!e.nm) {
		aviso_nm(&e);
		return;
	}
	erro_banner(&e);
	int nwifi = contar(&e, REDE_WIFI);
	pn_linha_comecar(CLAY_ID("wifi-ligado"), "wifi", "Wi-Fi", e.wifi_ligado ? "Ligado" : "Desligado: nenhuma antena emite");
	bool ligado = e.wifi_ligado;
	if (gv_interruptor(CLAY_ID("wifi-ligado-v"), &ligado)) {
		rede_wifi_ligar(ligado);
	}
	pn_linha_acabar();
	if (nwifi == 0) {
		vazio("Nenhum adaptador Wi-Fi encontrado",
			"Este computador não tem placa Wi-Fi, ou ela não foi reconhecida. Pode conectar uma antena USB e ela aparece aqui sozinha.");
		return;
	}
	if (wifi_escolhido >= nwifi) {
		wifi_escolhido = 0;
	}
	const struct rede_disp *d = NULL;
	disp_de_wifi(&e, wifi_escolhido, &d);
	if (nwifi > 1) {
		const char *nomes[REDE_MAX_DISP];
		for (int i = 0; i < nwifi; i++) {
			const struct rede_disp *x = NULL;
			disp_de_wifi(&e, i, &x);
			nomes[i] = gv_fmt("%s (%s)", x->iface, rede_barramento_nome(x->barramento)).chars;
		}
		pn_linha_comecar(CLAY_ID("wifi-qual"), "usb", "Adaptador", "Qual antena mostrar e usar");
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(300) } } }) {
			gv_lista_suspensa(CLAY_ID("wifi-qual-v"), nomes, nwifi, &wifi_escolhido);
		}
		pn_linha_acabar();
		disp_de_wifi(&e, wifi_escolhido, &d);
	}
	if (!d->gerenciado) {
		vazio("Este adaptador está desativado", "Ligue-o em Rede e internet → Adaptadores para usar.");
		return;
	}
	if (d->estado >= REDE_LIGADO && d->conexao[0]) {
		conectado_card(&e, d);
	} else if (d->estado >= 40 && d->estado < REDE_LIGADO) {
		gv_texto(CLAY_STRING("Conectando..."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}

	CLAY(CLAY_ID("wifi-titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 18, 8 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Redes disponíveis"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		if (gv_botao(CLAY_ID("wifi-varrer"), "refresh", "Procurar de novo", GV_BOTAO_SIMPLES)) {
			rede_varrer(d->caminho);
		}
	}
	int mostradas = 0;
	for (int i = 0; i < e.naps; i++) {
		if (!strcmp(e.aps[i].disp, d->caminho)) {
			ap_linha(&e, &e.aps[i], i);
			mostradas++;
		}
	}
	if (!mostradas) {
		gv_texto(e.wifi_ligado ? CLAY_STRING("Nenhuma rede por perto (toque em Procurar de novo).") : CLAY_STRING("Ligue o Wi-Fi para ver as redes."),
			gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
	}

	CLAY(CLAY_ID("wifi-manual-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 18, 8 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Outra rede"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		if (gv_botao(CLAY_ID("wifi-manual"), manual_aberto ? "close" : "plus", manual_aberto ? "Fechar" : "Adicionar manualmente", GV_BOTAO_ELEVADO)) {
			manual_aberto = !manual_aberto;
			if (manual_aberto) {
				gv_focar(CLAY_ID("man-ssid"));
			}
		}
	}
	if (manual_aberto) {
		form_manual(d);
	}

	CLAY(CLAY_ID("wifi-salvas-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 18, 4 } } }) {
		gv_texto(CLAY_STRING("Redes guardadas (histórico)"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
	}
	int ns = 0;
	for (int i = 0; i < e.nsalvas; i++) {
		if (e.salvas[i].tipo == REDE_WIFI) {
			salva_linha(&e, &e.salvas[i], i);
			ns++;
		}
	}
	if (!ns) {
		gv_texto(CLAY_STRING("Nenhuma rede guardada ainda. As que você conectar ficam aqui."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}

/* ---- Cabo ---- */

void pn_cabo_abrir(void) {
	pn_rede_servico();
	rede_erro_limpar();
	salva_aberta[0] = '\0';
}

void pn_cabo_desenhar(void) {
	pn_rede_servico();
	struct rede_estado e;
	rede_estado_copiar(&e);
	if (!e.nm) {
		aviso_nm(&e);
		return;
	}
	erro_banner(&e);
	int n = contar(&e, REDE_CABO);
	if (!n) {
		vazio("Nenhuma placa de rede por cabo encontrada",
			"Conecte um adaptador (um USB-Ethernet funciona) ou verifique se a placa foi reconhecida. Ele aparece aqui sozinho.");
		return;
	}
	for (int i = 0; i < e.ndisp; i++) {
		const struct rede_disp *d = &e.disp[i];
		if (d->tipo != REDE_CABO) {
			continue;
		}
		CLAY(CLAY_IDI("cabo", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
				.padding = CLAY_PADDING_ALL(16), .childGap = 10 },
			.backgroundColor = d->estado >= REDE_LIGADO ? gv_alfa(gv_t.acento, 0.10f) : gv_t.elevada,
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = d->estado >= REDE_LIGADO ? gv_alfa(gv_t.acento, 0.45f) : gv_t.borda, .width = GV_BORDA(1) } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_icone("ethernet", 26, d->estado >= REDE_LIGADO ? gv_t.acento : gv_t.icone);
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
					gv_texto(gv_str(d->modelo[0] ? d->modelo : d->iface), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(gv_fmt("%s%s%s%s", d->iface, d->barramento[0] ? " · " : "", d->barramento[0] ? rede_barramento_nome(d->barramento) : "",
						d->mac[0] ? gv_fmt(" · %s", d->mac).chars : "").chars), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				etiqueta(rede_estado_nome(d->estado), d->estado >= REDE_LIGADO);
				if (!d->gerenciado) {
					etiqueta("Desativado", false);
				} else if (d->estado >= REDE_LIGADO) {
					if (gv_botao(CLAY_IDI("cabo-desc", (uint32_t)i), NULL, "Desconectar", GV_BOTAO_PERIGO)) {
						rede_desconectar(d->caminho);
					}
				} else if (d->estado >= REDE_DESCONECTADO) {
					if (gv_botao(CLAY_IDI("cabo-lig", (uint32_t)i), NULL, "Conectar", GV_BOTAO_ELEVADO)) {
						/* a conexão guardada do cabo, se houver; senão o NM cria uma */
						const char *uuid = "";
						for (int k = 0; k < e.nsalvas; k++) {
							if (e.salvas[k].tipo == REDE_CABO) {
								uuid = e.salvas[k].uuid;
							}
						}
						rede_conectar_salva(uuid, d->caminho);
					}
				}
			}
			if (d->estado >= REDE_LIGADO) {
				gv_texto(gv_str(gv_fmt("IP %s · gateway %s · DNS %s%s", d->ip[0] ? d->ip : "—", d->gateway[0] ? d->gateway : "—",
					d->dns[0] ? d->dns : "—", d->velocidade ? gv_fmt(" · %u Mb/s", d->velocidade).chars : "").chars),
					gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			} else if (d->estado == REDE_INDISPONIVEL && d->gerenciado) {
				gv_texto(CLAY_STRING("Sem cabo ligado."), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
	}
	CLAY(CLAY_ID("cabo-salvas-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 18, 4 } } }) {
		gv_texto(CLAY_STRING("Conexões guardadas"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
	}
	int ns = 0;
	for (int i = 0; i < e.nsalvas; i++) {
		if (e.salvas[i].tipo == REDE_CABO) {
			salva_linha(&e, &e.salvas[i], i);
			ns++;
		}
	}
	if (!ns) {
		gv_texto(CLAY_STRING("Nenhuma ainda: o NetworkManager guarda uma na primeira vez que o cabo conecta."), gv_t.tamanho_corpo,
			gv_t.texto_suave, GV_PESO_NORMAL);
	}
}

/* ---- Adaptadores ---- */

void pn_adaptadores_abrir(void) {
	pn_rede_servico();
	rede_erro_limpar();
}

void pn_adaptadores_desenhar(void) {
	pn_rede_servico();
	struct rede_estado e;
	rede_estado_copiar(&e);
	if (!e.nm) {
		aviso_nm(&e);
		return;
	}
	erro_banner(&e);
	gv_texto(CLAY_STRING("Placas de rede e antenas deste computador. Desativar um adaptador faz o LUMNA parar de usá-lo."),
		gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	if (!e.ndisp) {
		vazio("Nenhum adaptador de rede encontrado",
			"Conecte uma placa, um adaptador USB-Ethernet ou uma antena Wi-Fi e ele aparece aqui sozinho.");
		return;
	}
	for (int i = 0; i < e.ndisp; i++) {
		const struct rede_disp *d = &e.disp[i];
		bool usb = !strcmp(d->barramento, "USB");
		CLAY(CLAY_IDI("adp", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 2, 14, 14 }, .childGap = 14,
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_t.borda, .width = { .top = i ? 1 : 0 } } }) {
			gv_icone(d->tipo == REDE_WIFI ? "wifi" : "ethernet", 24, d->gerenciado ? gv_t.icone : gv_t.texto_suave);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 4 } }) {
				CLAY_AUTO_ID({ .layout = { .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_texto(gv_str(d->modelo[0] ? d->modelo : d->iface), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
					etiqueta(d->tipo == REDE_WIFI ? "Wi-Fi" : "Cabo", false);
					if (d->barramento[0]) {
						etiqueta(usb ? "Antena USB" : "Onboard", usb);
					}
				}
				gv_texto(gv_str(gv_fmt("%s · driver %s · %s%s", d->iface, d->driver[0] ? d->driver : "?", d->mac[0] ? d->mac : "sem MAC",
					d->conexao[0] ? gv_fmt(" · %s", d->conexao).chars : "").chars), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			bool ativo = d->gerenciado;
			if (gv_interruptor(CLAY_IDI("adp-v", (uint32_t)i), &ativo)) {
				rede_disp_ativar(d->caminho, ativo);
			}
		}
	}
}

/* ---- Proxy ---- */

struct proxy_cfg {
	int modo;                /* 0 nenhum, 1 manual, 2 automático (um PAC) */
	char http[96], https[96], socks[96], ignorar[160], pac[160];
};
static struct proxy_cfg proxy, proxy_gravado;
static const struct proxy_cfg proxy_fabrica = { .ignorar = "localhost, 127.0.0.0/8, ::1" };

static void ler_proxy(void) {
	proxy_gravado = proxy_fabrica;
	char c[1024];
	pn_caminho(c, sizeof(c), "proxy.ini");
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
				proxy_gravado.modo = !strcmp(v, "manual") ? 1 : !strcmp(v, "automatico") ? 2 : 0;
			} else if (!strcmp(k, "http")) {
				snprintf(proxy_gravado.http, sizeof(proxy_gravado.http), "%s", v);
			} else if (!strcmp(k, "https")) {
				snprintf(proxy_gravado.https, sizeof(proxy_gravado.https), "%s", v);
			} else if (!strcmp(k, "socks")) {
				snprintf(proxy_gravado.socks, sizeof(proxy_gravado.socks), "%s", v);
			} else if (!strcmp(k, "ignorar")) {
				snprintf(proxy_gravado.ignorar, sizeof(proxy_gravado.ignorar), "%s", v);
			} else if (!strcmp(k, "pac")) {
				snprintf(proxy_gravado.pac, sizeof(proxy_gravado.pac), "%s", v);
			}
		}
		g_strfreev(linhas);
		g_free(texto);
	}
	proxy = proxy_gravado;
}

void pn_proxy_abrir(void) {
	ler_proxy();
}

bool pn_proxy_pendente(void) {
	return memcmp(&proxy, &proxy_gravado, sizeof(proxy)) != 0;
}

/* host:porta → host e porta separados, para o gsettings */
static void host_porta(const char *hp, char *host, size_t th, int *porta) {
	snprintf(host, th, "%s", hp);
	*porta = 0;
	char *dois = strrchr(host, ':');
	if (dois) {
		*porta = atoi(dois + 1);
		*dois = '\0';
	}
	/* tira um "http://" de quem colou a URL inteira */
	char *barras = strstr(host, "://");
	if (barras) {
		memmove(host, barras + 3, strlen(barras + 3) + 1);
	}
}

static void gs(const char *esquema, const char *chave, const char *valor) {
	char *cmd = g_strdup_printf("gsettings set %s %s %s 2>/dev/null", esquema, chave, valor);
	if (system(cmd) != 0) {
	}
	g_free(cmd);
}

void pn_proxy_aplicar(void) {
	GString *s = g_string_new("# O proxy (o Painel de controle grava este arquivo)\n");
	g_string_append_printf(s, "modo = %s\nhttp = %s\nhttps = %s\nsocks = %s\nignorar = %s\npac = %s\n",
		proxy.modo == 1 ? "manual" : proxy.modo == 2 ? "automatico" : "nenhum", proxy.http, proxy.https, proxy.socks, proxy.ignorar, proxy.pac);
	bool ok = pn_gravar("proxy.ini", s->str);
	g_string_free(s, TRUE);
	if (!ok) {
		return;
	}
	proxy_gravado = proxy;
	/* os programas da sessão (a sessão nova lê o environment.d; o GIO/GNOME lê o gsettings) */
	GString *env = g_string_new("# O proxy do LUMNA (gerado pelo Painel de controle)\n");
	if (proxy.modo == 1) {
		const char *h = proxy.http[0] ? proxy.http : proxy.https;
		if (h[0]) {
			g_string_append_printf(env, "http_proxy=http://%s\nHTTP_PROXY=http://%s\n", h, h);
		}
		const char *hs = proxy.https[0] ? proxy.https : proxy.http;
		if (hs[0]) {
			g_string_append_printf(env, "https_proxy=http://%s\nHTTPS_PROXY=http://%s\n", hs, hs);
		}
		if (proxy.socks[0]) {
			g_string_append_printf(env, "all_proxy=socks5://%s\nALL_PROXY=socks5://%s\n", proxy.socks, proxy.socks);
		}
		if (proxy.ignorar[0]) {
			char *sem = g_strdelimit(g_strdup(proxy.ignorar), " ", ',');
			char **partes = g_strsplit(sem, ",", -1);
			GString *lista = g_string_new("");
			for (char **p = partes; *p; p++) {
				if ((*p)[0]) {
					g_string_append_printf(lista, "%s%s", lista->len ? "," : "", *p);
				}
			}
			g_string_append_printf(env, "no_proxy=%s\nNO_PROXY=%s\n", lista->str, lista->str);
			g_string_free(lista, TRUE);
			g_strfreev(partes);
			g_free(sem);
		}
	}
	char *pasta = g_build_filename(g_get_user_config_dir(), "environment.d", NULL);
	g_mkdir_with_parents(pasta, 0755);
	char *arq = g_build_filename(pasta, "lumna-proxy.conf", NULL);
	if (proxy.modo == 1 && env->len > 60) {
		g_file_set_contents(arq, env->str, -1, NULL);
	} else {
		unlink(arq);
	}
	g_free(arq);
	g_free(pasta);
	g_string_free(env, TRUE);
	gs("org.gnome.system.proxy", "mode", proxy.modo == 1 ? "'manual'" : proxy.modo == 2 ? "'auto'" : "'none'");
	if (proxy.modo == 1) {
		struct { const char *esquema, *valor; } alvo[] = { { "org.gnome.system.proxy.http", proxy.http }, { "org.gnome.system.proxy.https", proxy.https },
			{ "org.gnome.system.proxy.socks", proxy.socks } };
		for (int i = 0; i < 3; i++) {
			char host[96], quoted[110], porta[12];
			int p;
			host_porta(alvo[i].valor, host, sizeof(host), &p);
			snprintf(quoted, sizeof(quoted), "'%s'", host);
			snprintf(porta, sizeof(porta), "%d", p);
			gs(alvo[i].esquema, "host", quoted);
			gs(alvo[i].esquema, "port", porta);
		}
	}
	if (proxy.modo == 2) {
		char q[200];
		snprintf(q, sizeof(q), "'%s'", proxy.pac);
		gs("org.gnome.system.proxy", "autoconfig-url", q);
	}
}

void pn_proxy_descartar(void) {
	proxy = proxy_gravado;
}

void pn_proxy_restaurar(void) {
	proxy = proxy_fabrica;
}

static void proxy_campo(int indice, const char *rotulo, char *buf, size_t tam, const char *dica) {
	CLAY(CLAY_IDI("px-linha", (uint32_t)indice), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 2, 6, 6 }, .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(380) } } }) {
			gv_campo(CLAY_IDI("px-campo", (uint32_t)indice), buf, tam, dica, GV_CAMPO_TEXTO);
		}
	}
}

void pn_proxy_desenhar(void) {
	static const char *const modos[] = { "Nenhum", "Manual", "Automático" };
	pn_linha_comecar(CLAY_ID("proxy-modo"), "globe", "Como usar o proxy", "Vale para os programas do LUMNA, o terminal e os que seguem o GNOME");
	gv_lista_suspensa(CLAY_ID("proxy-modo-v"), modos, 3, &proxy.modo);
	pn_linha_acabar();
	if (proxy.modo == 1) {
		proxy_campo(0, "Proxy HTTP", proxy.http, sizeof(proxy.http), "servidor:porta (ex.: 192.168.0.1:3128)");
		proxy_campo(1, "Proxy HTTPS", proxy.https, sizeof(proxy.https), "o mesmo do HTTP, se ficar vazio");
		proxy_campo(2, "Proxy SOCKS", proxy.socks, sizeof(proxy.socks), "servidor:porta");
		proxy_campo(3, "Não usar para", proxy.ignorar, sizeof(proxy.ignorar), "localhost, 192.168.0.0/16");
	} else if (proxy.modo == 2) {
		proxy_campo(4, "Endereço do PAC", proxy.pac, sizeof(proxy.pac), "http://servidor/proxy.pac");
	}
}

/* ---- as seções (as ainda por fazer seguem o plano) ---- */

extern void pn_mesclar_abrir(void);
extern void pn_mesclar_desenhar(void);
extern bool pn_mesclar_pendente(void);
extern void pn_mesclar_aplicar(void);
extern void pn_mesclar_descartar(void);
extern void pn_mesclar_restaurar(void);

static const pn_secao rede[] = {
	{ "wifi", "wifi", "Wi-Fi", "As redes sem fio por perto, as guardadas e a conexão manual.", pn_wifi_desenhar, NULL, NULL, NULL, NULL, NULL, pn_wifi_abrir },
	{ "cabo", "ethernet", "Cabo", "A ligação por cabo de rede.", pn_cabo_desenhar, NULL, NULL, NULL, NULL, NULL, pn_cabo_abrir },
	{ "adaptadores", "usb", "Adaptadores", "As placas e antenas: de onde vêm, o modelo e ligar ou desativar.", pn_adaptadores_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_adaptadores_abrir },
	{ "mesclar", "stack", "Várias conexões", "Usar duas internets ao mesmo tempo (Wi-Fi com Wi-Fi, ou Wi-Fi com cabo).", pn_mesclar_desenhar, NULL, pn_mesclar_pendente,
		pn_mesclar_aplicar, pn_mesclar_descartar, pn_mesclar_restaurar, pn_mesclar_abrir },
	{ "vpn", "shield", "VPN", "Redes privadas.", NULL, "Adicionar uma VPN (WireGuard, OpenVPN);Ligar e desligar" },
	{ "proxy", "globe", "Proxy", "Um proxy para a internet.", pn_proxy_desenhar, NULL, pn_proxy_pendente, pn_proxy_aplicar, pn_proxy_descartar,
		pn_proxy_restaurar, pn_proxy_abrir },
	{ "ponto", "signal", "Ponto de acesso", "Partilhar a internet deste aparelho.", NULL, "Nome e senha;Ligar e desligar" },
};
const pn_categoria pn_rede = { "rede", "wifi", "Rede e internet", "Wi-Fi, cabo e conexões", PN_GERAL, rede, N(rede) };
