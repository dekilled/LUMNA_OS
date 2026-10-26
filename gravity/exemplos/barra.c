/*
 * A topbar do desktop novo, em Gravity: o LUMNA e o modo à esquerda, as áreas de trabalho ao
 * meio, e à direita Wi-Fi, som, Bluetooth, o sino dos avisos e o relógio. Cada um abre o seu menu num
 * popup que desce da barra (uma camada de 40 px; o menu sai dela): redes (com senha ali mesmo), volume
 * e saídas, aparelhos Bluetooth e o calendário do mês. Tudo de verdade: a rede (NetworkManager), o
 * Bluetooth (BlueZ) e o som (PipeWire) são os mesmos serviços do Painel de controle. Aqui fica o
 * rápido; o fino vai para o Painel ("Configurações de ...").
 * O sino abre a central dos avisos (lumna-avisos --centro).
 *   ./barra
 */
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "bluetooth.h"
#include "gravity.h"
#include "rede.h"
#include "som.h"

static int area;
/* o que a barra mostra (~/.config/lumna/barra.ini, gravado pelo Painel; vigiado) */
static bool segundos, com_data, areas = true;

static char *barra_ini(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	return config && *config ? g_build_filename(config, "lumna", "barra.ini", NULL)
				 : g_build_filename(g_get_home_dir(), ".config", "lumna", "barra.ini", NULL);
}

static void ler_ajustes(void) {
	segundos = com_data = false;
	areas = true;
	char *caminho = barra_ini();
	FILE *f = fopen(caminho, "r");
	g_free(caminho);
	if (!f) {
		return;
	}
	char linha[256];
	while (fgets(linha, sizeof(linha), f)) {
		char chave[64], valor[64];
		if (sscanf(linha, " %63[^= ] = %63s", chave, valor) == 2) {
			bool sim = !strcmp(valor, "sim");
			if (!strcmp(chave, "segundos")) {
				segundos = sim;
			} else if (!strcmp(chave, "data")) {
				com_data = sim;
			} else if (!strcmp(chave, "areas")) {
				areas = sim;
			}
		}
	}
	fclose(f);
}

static void ler_modo(void);

static void ajustes_mudaram(gv_app *app, int fd, void *dados) {
	char buf[4096];
	while (read(fd, buf, sizeof(buf)) > 0) {
	}
	ler_ajustes();
	ler_modo();
	gv_app_redesenhar(app);
}

/* um serviço mexeu (rede, Bluetooth, som): desenha de novo */
static void servico_mudou(gv_app *app, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	gv_app_redesenhar(app);
}
static char modo_nome[16] = "Desktop";

static void ler_modo(void) {
	const char *config = getenv("XDG_CONFIG_HOME");
	char *caminho = config && *config ? g_build_filename(config, "lumna", "modo", NULL)
					  : g_build_filename(g_get_home_dir(), ".config", "lumna", "modo", NULL);
	char *texto = NULL;
	snprintf(modo_nome, sizeof(modo_nome), "Desktop");
	if (g_file_get_contents(caminho, &texto, NULL, NULL)) {
		g_strstrip(texto);
		if (!strcmp(texto, "gaming")) {
			snprintf(modo_nome, sizeof(modo_nome), "Gaming");
		} else if (!strcmp(texto, "midia")) {
			snprintf(modo_nome, sizeof(modo_nome), "Mídia");
		}
		g_free(texto);
	}
	g_free(caminho);
}

/* corre sem esperar (o filho do filho fica com o init: sem zumbis na barra) */
static void executar(const char *programa, const char *arg, const char *arg2);

static void titulo_secao(Clay_ElementId id, const char *texto) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 10, 6 } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void aparelho(Clay_ElementId id, const char *icone, const char *nome, const char *estado) {
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(56) }, .padding = { 12, 6, 6, 6 }, .childGap = 16,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone(icone, 24, gv_t.icone);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			gv_texto(gv_str(nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
			if (estado && estado[0]) {
				gv_texto(gv_str(estado), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
	}
}

/* o topo de um menu: ícone, título e (se ligado != NULL) o interruptor */
static bool topo_menu(int base, const char *icone, const char *titulo, bool *ligado) {
	bool mudou = false;
	CLAY(CLAY_IDI("menu-topo", (uint32_t)base), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(60) }, .padding = { 12, 10, 0, 0 },
		.childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone(icone, 24, gv_t.icone);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(titulo), gv_t.tamanho_destaque + 1, gv_t.texto, GV_PESO_NORMAL);
		}
		if (ligado) {
			mudou = gv_interruptor(CLAY_IDI("menu-topo-i", (uint32_t)base), ligado);
		}
	}
	gv_separador(false, 0);
	return mudou;
}

static void mensagem(const char *texto) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 16, 16 } } }) {
		gv_texto(gv_str(texto), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

/* ---- Wi-Fi ---- */

static struct rede_estado rede;
static char wf_ap[96], wf_senha[128];

static const struct rede_disp *wifi_principal(void) {
	for (int i = 0; i < rede.ndisp; i++) {
		if (rede.disp[i].tipo == REDE_WIFI && rede.disp[i].gerenciado) {
			return &rede.disp[i];
		}
	}
	return NULL;
}

static void menu_wifi(void) {
	GV_POPUP(CLAY_ID("menu-wifi"), CLAY_ID("wifi"), .alinhar = GV_ALINHAR_FIM, .largura = 440, .distancia = 10) {
		bool ligado = rede.wifi_ligado;
		if (topo_menu(1, "wifi", "Wi-Fi", rede.nm ? &ligado : NULL)) {
			rede_wifi_ligar(ligado);
		}
		const struct rede_disp *w = wifi_principal();
		if (!rede.nm) {
			mensagem("O NetworkManager não está respondendo.");
		} else {
			for (int i = 0; i < rede.ndisp; i++) {
				if (rede.disp[i].tipo == REDE_CABO && rede.disp[i].estado >= REDE_LIGADO) {
					aparelho(CLAY_IDI("wf-cabo", i), "ethernet", "Cabo de rede", gv_fmt("Conectado%s%s", rede.disp[i].ip[0] ? " · " : "", rede.disp[i].ip).chars);
				}
			}
			if (!w) {
				mensagem("Nenhum adaptador Wi-Fi. Conecte uma antena USB e ela aparece aqui.");
			} else if (!rede.wifi_ligado) {
				mensagem("O Wi-Fi está desligado.");
			} else {
				if (w->estado >= REDE_LIGADO && w->conexao[0]) {
					CLAY(CLAY_ID("wf-atual"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
						.backgroundColor = gv_alfa(gv_t.acento, 0.10f) }) {
						aparelho(CLAY_ID("wf-atual-ap"), "wifi", w->conexao, gv_fmt("Conectado%s", rede.internet ? "" : " · sem internet").chars);
						if (gv_botao(CLAY_ID("wf-desc"), NULL, "Desconectar", GV_BOTAO_SIMPLES)) {
							rede_desconectar(w->caminho);
						}
					}
					gv_separador(false, 0);
				}
				titulo_secao(CLAY_ID("wf-s1"), "Redes disponíveis");
				Clay_ElementId lista = CLAY_ID("wf-lista");
				CLAY(lista, gv_rolagem(lista, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
					.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0, 300) } } })) {
					int n = 0;
					for (int i = 0; i < rede.naps; i++) {
						const struct rede_ap *a = &rede.aps[i];
						if (strcmp(a->disp, w->caminho) || a->atual) {
							continue;
						}
						n++;
						bool aberto = !strcmp(wf_ap, a->caminho);
						CLAY(CLAY_IDI("wf-ap", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .right = 8 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
							aparelho(CLAY_IDI("wf-ap-i", i), a->segura ? "lock" : "wifi", a->ssid,
								gv_fmt("%d%%%s", a->sinal, a->salva ? " · guardada" : "").chars);
							if (!aberto && gv_botao_ext(CLAY_IDI("wf-lig", i), &(gv_botao_opcoes){ .texto = "Conectar", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
								if (a->segura && !a->salva) {
									snprintf(wf_ap, sizeof(wf_ap), "%s", a->caminho);
									wf_senha[0] = '\0';
									gv_focar(CLAY_ID("wf-senha"));
								} else {
									rede_conectar(a->disp, a->caminho, "");
									gv_popup_fechar(CLAY_ID("menu-wifi"));
								}
							}
						}
						if (aberto) {
							CLAY(CLAY_IDI("wf-form", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 52, 12, 0, 10 }, .childGap = 8,
								.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
								CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
									if (gv_campo(CLAY_ID("wf-senha"), wf_senha, sizeof(wf_senha), "Senha", GV_CAMPO_SENHA) & GV_CAMPO_ENTER) {
										if (wf_senha[0]) {
											rede_conectar(a->disp, a->caminho, wf_senha);
											wf_ap[0] = '\0';
										}
									}
								}
								if (gv_botao_ext(CLAY_ID("wf-ok"), &(gv_botao_opcoes){ .texto = "Entrar", .estilo = GV_BOTAO_CHEIO, .altura = 38,
									    .desativado = !wf_senha[0] })) {
									rede_conectar(a->disp, a->caminho, wf_senha);
									wf_ap[0] = '\0';
									memset(wf_senha, 0, sizeof(wf_senha));
								}
							}
						}
					}
					if (!n) {
						mensagem("Nenhuma outra rede por perto.");
					}
				}
			}
			if (rede.erro[0]) {
				CLAY(CLAY_ID("wf-erro"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 8, 8, 8 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					char curto[160];
					snprintf(curto, sizeof(curto), "%s", rede.erro);
					gv_texto(gv_str(gv_fmt("%s", curto).chars), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
				}
			}
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("wf-config"), "settings", "Configurações de rede", NULL)) {
			executar("lumna-painel", "--secao", "rede/wifi");
			gv_popup_fechar(CLAY_ID("menu-wifi"));
		}
	}
}

/* o ícone da barra segue a rede */
static const char *icone_rede(void) {
	if (!rede.nm) {
		return "offline";
	}
	for (int i = 0; i < rede.ndisp; i++) {
		if (rede.disp[i].estado >= REDE_LIGADO && rede.disp[i].gerenciado) {
			return rede.disp[i].tipo == REDE_CABO ? "ethernet" : "wifi";
		}
	}
	return rede.wifi_ligado && wifi_principal() ? "wifi" : "offline";
}

/* ---- som ---- */

static struct som_estado som;

static void menu_som(void) {
	GV_POPUP(CLAY_ID("menu-som"), CLAY_ID("som"), .alinhar = GV_ALINHAR_FIM, .largura = 420, .distancia = 10) {
		const struct som_no *atual = som_saida_atual(&som);
		topo_menu(2, "volume", "Som", NULL);
		if (!som.ligado || !atual) {
			mensagem(som.ligado ? "Nenhuma saída de som encontrada. Conecte um alto-falante ou fone." : "O servidor de som não está respondendo.");
		} else {
			CLAY(CLAY_ID("sm-vol"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 16, 12, 12 }, .childGap = 14,
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (gv_botao_redondo(CLAY_ID("sm-mudo"), atual->mudo ? "volume-off" : "volume", 40, atual->mudo, false)) {
					som_mudo_saida(atual->indice, !atual->mudo);
				}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					float v = atual->volume > 1 ? 1 : atual->volume;
					if (gv_deslizante(CLAY_ID("sm-desl"), &v, 8, 22)) {
						som_volume_saida(atual->indice, v);
					}
				}
				gv_texto(gv_fmt("%d%%", (int)(atual->volume * 100 + 0.5f)), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (som.nsaidas > 1) {
				gv_separador(false, 0);
				titulo_secao(CLAY_ID("sm-s1"), "Saída");
				for (int i = 0; i < som.nsaidas; i++) {
					const struct som_no *o = &som.saidas[i];
					if (gv_menu_item(CLAY_IDI("sm-saida", i), o->padrao ? "check" : NULL, o->descricao[0] ? o->descricao : o->nome, NULL)) {
						som_saida_padrao(o->nome);
					}
				}
			}
			if (som.napps > 0) {
				gv_separador(false, 0);
				titulo_secao(CLAY_ID("sm-s2"), "Tocando agora");
				for (int i = 0; i < som.napps && i < 4; i++) {
					const struct som_no *a = &som.apps[i];
					CLAY(CLAY_IDI("sm-app", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 16, 6, 6 }, .childGap = 12,
						.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(120) } } }) {
							gv_texto(gv_str(a->descricao), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
						}
						CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
							float v = a->volume > 1 ? 1 : a->volume;
							if (gv_deslizante(CLAY_IDI("sm-app-d", i), &v, 6, 16)) {
								som_volume_app(a->indice, v);
							}
						}
					}
				}
			}
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("sm-config"), "settings", "Mixer de áudio", NULL)) {
			executar("lumna-mixer", NULL, NULL);
			gv_popup_fechar(CLAY_ID("menu-som"));
		}
	}
}

static const char *icone_som(void) {
	const struct som_no *a = som_saida_atual(&som);
	if (!a) {
		return "volume-off";
	}
	return a->mudo || a->volume <= 0.001f ? "volume-off" : "volume";
}

/* ---- Bluetooth ---- */

static struct bt_estado bt;

static const char *bt_icone(const char *icone) {
	if (!strncmp(icone, "audio", 5)) {
		return "headphones";
	}
	if (!strncmp(icone, "input-keyboard", 14)) {
		return "keyboard";
	}
	if (!strncmp(icone, "input-mouse", 11)) {
		return "mouse";
	}
	if (!strncmp(icone, "input-gaming", 12)) {
		return "gamepad";
	}
	if (!strncmp(icone, "phone", 5)) {
		return "phone";
	}
	return "bluetooth";
}

static void menu_bluetooth(void) {
	GV_POPUP(CLAY_ID("menu-bt"), CLAY_ID("bt"), .alinhar = GV_ALINHAR_FIM, .largura = 440, .distancia = 10) {
		const struct bt_adapt *a = bt.nadapt ? &bt.adapt[0] : NULL;
		bool ligado = a && a->ligado;
		if (topo_menu(3, "bluetooth", "Bluetooth", a ? &ligado : NULL) && a) {
			bt_ligar(a->caminho, ligado);
		}
		if (!bt.bluez) {
			mensagem("O serviço de Bluetooth não está respondendo.");
		} else if (!a) {
			mensagem("Nenhum adaptador Bluetooth. Conecte um adaptador USB e ele aparece aqui.");
		} else if (!a->ligado) {
			mensagem("O Bluetooth está desligado.");
		} else {
			titulo_secao(CLAY_ID("bt-s1"), "Seus aparelhos");
			int n = 0;
			for (int i = 0; i < bt.ndisp; i++) {
				const struct bt_disp *d = &bt.disp[i];
				if (!d->pareado) {
					continue;
				}
				n++;
				CLAY(CLAY_IDI("bt-con", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .right = 8 }, .childGap = 10,
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					aparelho(CLAY_IDI("bt-con-ap", i), bt_icone(d->icone), d->nome[0] ? d->nome : d->endereco,
						gv_fmt("%s", d->conectado ? "Conectado" : "Pareado").chars);
					if (d->bateria >= 0) {
						gv_icone("battery", 20, gv_t.texto_secundario);
						gv_texto(gv_fmt("%d%%", d->bateria), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
					if (gv_botao_ext(CLAY_IDI("bt-acao", i), &(gv_botao_opcoes){ .texto = d->conectado ? "Desconectar" : "Conectar",
						    .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
						if (d->conectado) {
							bt_desconectar(d->caminho);
						} else {
							bt_conectar(d->caminho);
						}
					}
				}
			}
			if (!n) {
				mensagem("Nenhum aparelho pareado.");
			}
			gv_separador(false, 0);
			CLAY(CLAY_ID("bt-s2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					titulo_secao(CLAY_ID("bt-s2-titulo"), a->procurando ? "Por perto (procurando...)" : "Por perto");
				}
				if (gv_botao(CLAY_ID("bt-buscar"), a->procurando ? "close" : "refresh", a->procurando ? "Parar" : "Buscar", GV_BOTAO_SIMPLES)) {
					bt_procurar(a->caminho, !a->procurando);
				}
			}
			int perto = 0;
			for (int i = 0; i < bt.ndisp && perto < 5; i++) {
				const struct bt_disp *d = &bt.disp[i];
				if (d->pareado) {
					continue;
				}
				perto++;
				CLAY(CLAY_IDI("bt-disp", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .right = 8 },
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					aparelho(CLAY_IDI("bt-disp-ap", i), bt_icone(d->icone), d->nome[0] ? d->nome : d->endereco, bt_tipo_nome(d->icone));
					if (gv_botao_ext(CLAY_IDI("bt-parear", i), &(gv_botao_opcoes){ .texto = "Parear", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
						bt_parear(d->caminho);
					}
				}
			}
			if (!perto) {
				mensagem(a->procurando ? "Nada por perto ainda." : "Toque em Buscar para procurar aparelhos.");
			}
			if (bt.erro[0]) {
				CLAY(CLAY_ID("bt-erro"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 8, 8, 8 } } }) {
					char curto[160];
					snprintf(curto, sizeof(curto), "%s", bt.erro);
					gv_texto(gv_str(gv_fmt("%s", curto).chars), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
				}
			}
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("bt-config"), "settings", "Configurações de Bluetooth", NULL)) {
			executar("lumna-painel", "--secao", "bluetooth/dispositivos");
			gv_popup_fechar(CLAY_ID("menu-bt"));
		}
	}
}

static const char *icone_bt(void) {
	return bt.nadapt && bt.adapt[0].ligado ? "bluetooth" : "bluetooth";
}

/* ---- relógio e calendário ---- */

static int cal_mes_desloc; /* 0: o mês de hoje */

static void menu_calendario(const struct tm *hoje) {
	GV_POPUP(CLAY_ID("menu-cal"), CLAY_ID("relogio"), .alinhar = GV_ALINHAR_FIM, .largura = 380, .distancia = 10) {
		static const char *const meses[] = { "Janeiro", "Fevereiro", "Março", "Abril", "Maio", "Junho", "Julho", "Agosto", "Setembro",
			"Outubro", "Novembro", "Dezembro" };
		static const char *const dias_sem[] = { "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb" };
		static const char *const dias_longos[] = { "Domingo", "Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado" };
		int mes = hoje->tm_mon + cal_mes_desloc, ano = hoje->tm_year + 1900;
		ano += mes >= 0 ? mes / 12 : -((11 - mes) / 12);
		mes = ((mes % 12) + 12) % 12;
		struct tm primeiro = { .tm_year = ano - 1900, .tm_mon = mes, .tm_mday = 1, .tm_hour = 12 };
		mktime(&primeiro);
		struct tm ultimo = { .tm_year = ano - 1900, .tm_mon = mes + 1, .tm_mday = 0, .tm_hour = 12 };
		mktime(&ultimo);
		int ndias = ultimo.tm_mday;
		CLAY(CLAY_ID("cal-hoje"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 18, 18, 16, 8 }, .childGap = 2 } }) {
			gv_texto(gv_str(gv_fmt("%02d:%02d", hoje->tm_hour, hoje->tm_min).chars), gv_t.tamanho_destaque + 10, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(gv_str(gv_fmt("%s, %d de %s de %d", dias_longos[hoje->tm_wday], hoje->tm_mday, meses[hoje->tm_mon], hoje->tm_year + 1900).chars),
				gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		gv_separador(false, 0);
		CLAY(CLAY_ID("cal-cab"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 10, 10, 4 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(gv_fmt("%s %d", meses[mes], ano).chars), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			}
			if (cal_mes_desloc != 0 && gv_botao(CLAY_ID("cal-hoje-b"), NULL, "Hoje", GV_BOTAO_SIMPLES)) {
				cal_mes_desloc = 0;
			}
			if (gv_botao(CLAY_ID("cal-ant"), "chevron-up", NULL, GV_BOTAO_SIMPLES)) {
				cal_mes_desloc--;
			}
			if (gv_botao(CLAY_ID("cal-seg"), "chevron-down", NULL, GV_BOTAO_SIMPLES)) {
				cal_mes_desloc++;
			}
		}
		CLAY(CLAY_ID("cal-grade"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 0, 14 }, .childGap = 4 } }) {
			CLAY(CLAY_ID("cal-semana"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				for (int c = 0; c < 7; c++) {
					CLAY(CLAY_IDI("cal-ds", c), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(26) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
						gv_texto(gv_str(dias_sem[c]), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
					}
				}
			}
			int dia = 1 - primeiro.tm_wday;
			for (int l = 0; l < 6 && dia <= ndias; l++) {
				CLAY(CLAY_IDI("cal-l", l), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
					for (int c = 0; c < 7; c++, dia++) {
						bool dentro = dia >= 1 && dia <= ndias;
						bool eh_hoje = dentro && cal_mes_desloc == 0 && dia == hoje->tm_mday;
						CLAY(CLAY_IDI("cal-d", l * 7 + c), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(38) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
							if (dentro) {
								CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(34) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
									.backgroundColor = eh_hoje ? gv_t.acento : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(17) }) {
									gv_texto(gv_str(gv_fmt("%d", dia).chars), gv_t.tamanho_corpo, eh_hoje ? gv_t.sobre_acento : (c == 0 ? gv_t.texto_secundario : gv_t.texto),
										eh_hoje ? GV_PESO_MEDIO : GV_PESO_NORMAL);
								}
							}
						}
					}
				}
			}
		}
	}
}

/* corre sem esperar (o filho do filho fica com o init: sem zumbis na barra) */
static void executar(const char *programa, const char *arg, const char *arg2) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			setsid();
			execlp(programa, programa, arg, arg2, (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

static bool icone_barra(Clay_ElementId id, const char *icone, bool aberto) {
	bool tocado = gv_tocado(id);
	CLAY(id, {
		.layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(32) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = aberto ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
	}) {
		tocado |= gv_focavel(id, gv_t.raio_pequeno);
		gv_icone(icone, 20, gv_t.icone);
	}
	return tocado;
}

static void desenhar(gv_app *app, void *dados) {
	(void)app;
	(void)dados;
	rede_estado_copiar(&rede);
	bt_estado_copiar(&bt);
	som_ler(&som);
	CLAY(CLAY_ID("barra"), {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 18, 14, 0, 0 },
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_t.fundo,
		.border = { .color = gv_t.borda, .width = { .bottom = 1 } },
	}) {
		/* esquerda: o LUMNA e o modo */
		CLAY(CLAY_ID("esq"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone("lumna", 20, gv_t.acento);
			gv_texto(CLAY_STRING("L U M N A"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			gv_separador(true, 18);
			gv_texto(gv_str(modo_nome), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		/* meio: as áreas de trabalho */
		CLAY(CLAY_ID("areas"), { .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			for (int i = 0; areas && i < 3; i++) {
				Clay_ElementId id = CLAY_IDI("area", i);
				if (gv_tocado(id)) {
					area = i;
				}
				float w = gv_anim(id.id, area == i ? 34 : 22, gv_t.duracao_normal);
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(16) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(4) } },
						.backgroundColor = area == i ? gv_t.acento : gv_t.borda_controle, .cornerRadius = CLAY_CORNER_RADIUS(2) }) {}
				}
			}
		}
		/* direita: o estado do sistema e o relógio */
		CLAY(CLAY_ID("dir"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6,
			.childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } } }) {
			if (icone_barra(CLAY_ID("wifi"), icone_rede(), gv_popup_aberto(CLAY_ID("menu-wifi")))) {
				gv_popup_alternar(CLAY_ID("menu-wifi"));
				rede_varrer("");
			}
			if (icone_barra(CLAY_ID("som"), icone_som(), gv_popup_aberto(CLAY_ID("menu-som")))) {
				gv_popup_alternar(CLAY_ID("menu-som"));
			}
			if (icone_barra(CLAY_ID("bt"), icone_bt(), gv_popup_aberto(CLAY_ID("menu-bt")))) {
				gv_popup_alternar(CLAY_ID("menu-bt"));
			}
			if (icone_barra(CLAY_ID("avisos"), "bell", false)) {
				executar("lumna-avisos", "--centro", NULL);
			}
			time_t agora = time(NULL);
			struct tm t;
			localtime_r(&agora, &t);
			Clay_ElementId rel = CLAY_ID("relogio");
			if (gv_tocado(rel)) {
				cal_mes_desloc = 0;
				gv_popup_alternar(CLAY_ID("menu-cal"));
			}
			CLAY(rel, { .layout = { .padding = { 10, 10, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_popup_aberto(CLAY_ID("menu-cal")) ? gv_t.selecionado : gv_por_cima(rel) ? gv_t.sob_cursor : (Clay_Color){ 0 },
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
				gv_focavel(rel, gv_t.raio_pequeno);
				if (com_data) {
					static const char *const dias[] = { "Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb" };
					static const char *const meses[] = { "jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez" };
					gv_texto(gv_fmt("%s, %d de %s", dias[t.tm_wday], t.tm_mday, meses[t.tm_mon]), gv_t.tamanho_corpo,
						gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				if (segundos) {
					gv_texto(gv_fmt("%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				} else {
					gv_texto(gv_fmt("%02d:%02d", t.tm_hour, t.tm_min), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				}
			}
			/* acorda na virada do segundo (com os segundos) ou do minuto */
			gv_acordar_em(segundos ? 1.0 - fmod(gv_agora(), 1.0) + 0.01 : 60 - t.tm_sec + 0.05);
		}
	}
	menu_wifi();
	menu_som();
	menu_bluetooth();
	struct tm hoje;
	time_t agora2 = time(NULL);
	localtime_r(&agora2, &hoje);
	menu_calendario(&hoje);
	fflush(stdout);
}

int main(void) {
	gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Barra", .app_id = "lumna-barra", .tipo = GV_CAMADA,
		.nivel = GV_NIVEL_CIMA, .ancoras = GV_ANCORA_CIMA | GV_ANCORA_ESQUERDA | GV_ANCORA_DIREITA,
		.altura = 40, .zona_exclusiva = 40 });
	if (!app) {
		return 1;
	}
	ler_ajustes();
	ler_modo();
	int fds[3] = { rede_iniciar(), bt_iniciar(), som_iniciar("Barra") };
	for (int i = 0; i < 3; i++) {
		if (fds[i] >= 0) {
			gv_app_vigiar(app, fds[i], servico_mudou, NULL);
		}
	}
	char *ini = barra_ini();
	char *pasta = g_path_get_dirname(ini);
	g_mkdir_with_parents(pasta, 0755);
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd >= 0 && inotify_add_watch(fd, pasta, IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE) >= 0) {
		gv_app_vigiar(app, fd, ajustes_mudaram, NULL);
	}
	g_free(pasta);
	g_free(ini);
	return gv_app_correr(app, desenhar, NULL);
}
