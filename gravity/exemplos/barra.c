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

#include "bandeja.h"
#include "bluetooth.h"
#include "emmac.h"
#include "json.h"
#include "gravity.h"
#include "hora.h"
#include "modos.h"
#include "rede.h"
#include "som.h"

/* os menus do sistema (Wi-Fi, som, Bluetooth, calendário) abrem todos no mesmo lugar: presos ao canto
 * direito da barra (o grupo "dir"), com o mesmo vão para a borda e para a barra; os da bandeja ficam
 * junto do ícone de cada app */
#define POPUP_GAP 8
/* a largura é a mesma em todos os menus do sistema; só a altura muda com o conteúdo */
#define POPUP_LARGURA 440
/* a altura nunca passa da área útil: do vão debaixo da barra até a linha de cima da dock, com a margem das notificações */
#define POPUP_MARGEM_BAIXO 12

static float popup_altura_max = 560; /* até saber a da tela */

static void popup_medir(void) {
	int h;
	if (orbit_area_util(NULL, NULL, NULL, NULL, &h) && h > 0) {
		popup_altura_max = (float)(h - POPUP_GAP - POPUP_MARGEM_BAIXO);
	}
}

/* abre um menu do sistema e fecha os outros (um só de cada vez); tocar no que está aberto fecha-o */
static void menu_alternar(Clay_ElementId id) {
	bool estava = gv_popup_aberto(id);
	popup_medir(); /* a dock pode ter mudado de altura ou sumido */
	gv_popup_fechar(CLAY_ID("menu-modo"));
	gv_popup_fechar(CLAY_ID("menu-wifi"));
	gv_popup_fechar(CLAY_ID("menu-som"));
	gv_popup_fechar(CLAY_ID("menu-bt"));
	gv_popup_fechar(CLAY_ID("menu-cal"));
	gv_popup_fechar(CLAY_ID("menu-tray"));
	gv_popup_fechar(CLAY_ID("menu-emma"));
	if (!estava) {
		gv_popup_abrir(id);
	}
}
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

static void modos_atualizar(void);

static void ajustes_mudaram(gv_app *app, int fd, void *dados) {
	char buf[4096];
	while (read(fd, buf, sizeof(buf)) > 0) {
	}
	ler_ajustes();
	gv_app_redesenhar(app);
}

/* um serviço mexeu (rede, Bluetooth, som): desenha de novo */
static void servico_mudou(gv_app *app, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	gv_app_redesenhar(app);
}

/* o Orbit trocou um modo (de qualquer jeito: Super+Tab, este menu, o Painel) */
static void modo_mudou(gv_app *app, int fd, void *dados) {
	modos_assinar_ler(fd);
	modos_atualizar();
	popup_medir();
	gv_app_redesenhar(app);
}
/* ---- os modos (Orbit) ---- */

static struct modos_estado modos_est;
static int modos_fd = -1;

static void modos_atualizar(void) {
	modos_ler(&modos_est);
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
	GV_POPUP(CLAY_ID("menu-wifi"), CLAY_ID("dir"), .alinhar = GV_ALINHAR_FIM, .largura = POPUP_LARGURA, .distancia = POPUP_GAP, .altura_max = popup_altura_max) {
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

/* uma linha de volume: o botão de mudo, o deslizante e a porcentagem */
static void linha_volume(int base, const struct som_no *no, bool entrada) {
	CLAY(CLAY_IDI("sm-vol", (uint32_t)base), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 16, 8, 12 }, .childGap = 14,
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		const char *icone = entrada ? (no->mudo ? "mic-off" : "mic") : (no->mudo ? "volume-off" : "volume");
		if (gv_botao_redondo(CLAY_IDI("sm-mudo", (uint32_t)base), icone, 40, no->mudo, false)) {
			if (entrada) {
				som_mudo_entrada(no->indice, !no->mudo);
			} else {
				som_mudo_saida(no->indice, !no->mudo);
			}
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			float v = no->volume > 1 ? 1 : no->volume;
			if (gv_deslizante(CLAY_IDI("sm-desl", (uint32_t)base), &v, 8, 22)) {
				if (entrada) {
					som_volume_entrada(no->indice, v);
				} else {
					som_volume_saida(no->indice, v);
				}
			}
		}
		gv_texto(gv_fmt("%d%%", (int)(no->volume * 100 + 0.5f)), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void menu_som(void) {
	GV_POPUP(CLAY_ID("menu-som"), CLAY_ID("dir"), .alinhar = GV_ALINHAR_FIM, .largura = POPUP_LARGURA, .distancia = POPUP_GAP, .altura_max = popup_altura_max) {
		const struct som_no *atual = som_saida_atual(&som);
		const struct som_no *mic = som_entrada_atual(&som);
		topo_menu(2, "volume", "Som", NULL);
		if (!som.ligado) {
			mensagem("O servidor de som não está respondendo.");
		}
		if (som.ligado) {
			titulo_secao(CLAY_ID("sm-s1"), "Saída");
			if (!atual) {
				mensagem("Nenhuma saída de som encontrada. Conecte um alto-falante ou fone.");
			} else {
				linha_volume(0, atual, false);
				if (som.nsaidas > 1) {
					for (int i = 0; i < som.nsaidas; i++) {
						const struct som_no *o = &som.saidas[i];
						if (gv_menu_item(CLAY_IDI("sm-saida", i), o->padrao ? "check" : NULL, o->descricao[0] ? o->descricao : o->nome, NULL)) {
							som_saida_padrao(o->nome);
						}
					}
				}
			}
			gv_separador(false, 0);
			titulo_secao(CLAY_ID("sm-e1"), "Entrada");
			if (!mic) {
				mensagem("Nenhum microfone encontrado. Conecte um microfone ou fone com microfone.");
			} else {
				linha_volume(1, mic, true);
				if (som.nentradas > 1) {
					for (int i = 0; i < som.nentradas; i++) {
						const struct som_no *o = &som.entradas[i];
						if (gv_menu_item(CLAY_IDI("sm-entrada", i), o->padrao ? "check" : NULL, o->descricao[0] ? o->descricao : o->nome, NULL)) {
							som_entrada_padrao(o->nome);
						}
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
	GV_POPUP(CLAY_ID("menu-bt"), CLAY_ID("dir"), .alinhar = GV_ALINHAR_FIM, .largura = POPUP_LARGURA, .distancia = POPUP_GAP, .altura_max = popup_altura_max) {
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

static void menu_modo(void) {
	GV_POPUP(CLAY_ID("menu-modo"), CLAY_ID("modo"), .alinhar = GV_ALINHAR_INICIO, .largura = POPUP_LARGURA, .distancia = POPUP_GAP, .altura_max = popup_altura_max) {
		int atual = modos_em_foco(&modos_est);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 12, 6 } } }) {
			gv_texto(CLAY_STRING("Alternar modo"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		}
		if (!modos_est.orbit) {
			mensagem("O Orbit não respondeu.");
		}
		for (int m = 0; modos_est.orbit && m < MODO_N; m++) {
			Clay_ElementId id = CLAY_IDI("md", (uint32_t)m);
			bool tocado = gv_tocado(id);
			CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 10, 10 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = m == atual ? gv_alfa(gv_t.acento, 0.12f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 } }) {
				tocado |= gv_focavel(id, 0);
				gv_icone(modos_icone(m), 26, m == atual ? gv_t.acento : gv_t.icone);
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
					gv_texto(gv_str(modos_nome(m)), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(gv_str(modos_descricao(m)), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				if (m == atual) {
					gv_icone("check", 20, gv_t.acento);
				}
			}
			if (tocado && m != atual) {
				modos_definir(m, NULL);
				gv_popup_fechar(CLAY_ID("menu-modo"));
			}
		}
		if (modos_est.orbit && modos_est.n > 1) {
			gv_separador(false, 0);
			CLAY(CLAY_ID("md-ext"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 10, 10 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
					gv_texto(CLAY_STRING("Estender o modo"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(CLAY_STRING("Todas as telas trocam juntas"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				bool ext = modos_est.estender;
				if (gv_interruptor(CLAY_ID("md-ext-v"), &ext)) {
					modos_definir_layout(ext);
					struct modos_cfg c;
					modos_cfg_ler(&c);
					c.estender = ext;
					modos_cfg_gravar(&c);
				}
			}
		}
		if (modos_est.orbit && modos_est.n > 1 && gv_menu_item(CLAY_ID("md-trocar"), "screens", "Trocar as telas de lugar", NULL)) {
			modos_trocar_telas();
			gv_popup_fechar(CLAY_ID("menu-modo"));
		}
		gv_separador(false, 0);
		if (gv_menu_item(CLAY_ID("md-config"), "settings", "Configurações de modos", NULL)) {
			executar("lumna-painel", "--secao", "telas/modos");
			gv_popup_fechar(CLAY_ID("menu-modo"));
		}
	}
}

/* ---- a bandeja: os apps em segundo plano (StatusNotifierItem) ---- */

static struct bandeja_estado tray;
static struct bandeja_menu tray_menu;
static unsigned tray_menu_visto;
static int tray_ancora = -1;

/* o ícone do item: arquivo (a imagem), ou o nome no tema */
static void icone_tray(Clay_ElementId id, const struct bandeja_item *it) {
	if (it->icone[0] == '/') {
		Clay_ElementId img = CLAY_IDI("tray-img", (uint32_t)(id.offset));
		CLAY(img, gv_imagem(img, it->icone, GV_AJUSTE_CONTER, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20) } } })) {}
	} else if (it->icone[0]) {
		/* o ícone do tema chega depois: o espaço fica reservado */
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(20), CLAY_SIZING_FIXED(20) } } }) {
			gv_icone_app(it->icone, 20);
		}
	} else {
		gv_icone("grid", 20, gv_t.icone);
	}
}

static void bandeja_na_barra(void) {
	for (int i = 0; i < tray.n; i++) {
		const struct bandeja_item *it = &tray.itens[i];
		if (it->estado == BANDEJA_PASSIVO) {
			continue; /* o app pediu para ficar escondido */
		}
		Clay_ElementId id = CLAY_IDI("tray", (uint32_t)i);
		bool menu_contexto = gv_menu_contexto(id);
		bool tocado = gv_tocado(id);
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(32) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_popup_aberto(CLAY_ID("menu-tray")) && tray_ancora == i ? gv_t.selecionado : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
			.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
			tocado |= gv_focavel(id, gv_t.raio_pequeno);
			icone_tray(id, it);
			if (it->estado == BANDEJA_ATENCAO) {
				CLAY_AUTO_ID({ .floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP },
						.offset = { -4, 4 }, .zIndex = 2 },
					.layout = { .sizing = { CLAY_SIZING_FIXED(8), CLAY_SIZING_FIXED(8) } }, .backgroundColor = gv_t.perigo,
					.cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
			}
		}
		Clay_BoundingBox c = gv_caixa(id);
		int x = (int)(c.x + c.width / 2), y = (int)(c.y + c.height);
		if (menu_contexto) {
			tray_ancora = i;
			bandeja_menu_pedir(it->chave, x, y);
		} else if (tocado) {
			tray_ancora = i;
			bandeja_ativar(it->chave, x, y);
		}
	}
}

static void menu_tray(void) {
	if (tray_ancora < 0 || tray_ancora >= tray.n) {
		return;
	}
	GV_POPUP(CLAY_ID("menu-tray"), CLAY_IDI("tray", (uint32_t)tray_ancora), .alinhar = GV_ALINHAR_FIM, .largura = 300, .distancia = POPUP_GAP, .altura_max = popup_altura_max) {
		struct bandeja_menu *m = &tray_menu;
		if (strcmp(m->chave, tray.itens[tray_ancora].chave)) {
			mensagem("Abrindo...");
		} else if (m->n == 0) {
			mensagem("Este app não tem menu.");
		} else {
			for (int i = 0; i < m->n; i++) {
				const struct bandeja_entrada *e = &m->entradas[i];
				if (e->separador) {
					gv_separador(false, 0);
				} else if (e->submenu) {
					CLAY(CLAY_IDI("tray-sub", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { (uint16_t)(14 + e->nivel * 16), 12, 8, 4 } } }) {
						gv_texto(gv_str(e->texto), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
					}
				} else if (e->habilitado) {
					if (gv_menu_item(CLAY_IDI("tray-it", i), e->marcavel && e->marcado ? "check" : NULL,
						    e->nivel ? gv_fmt("%*s%s", e->nivel * 2, "", e->texto).chars : e->texto, NULL)) {
						bandeja_menu_clicar(m->chave, e->id);
						gv_popup_fechar(CLAY_ID("menu-tray"));
					}
				} else {
					CLAY(CLAY_IDI("tray-off", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { (uint16_t)(14 + e->nivel * 16), 12, 10, 10 } } }) {
						gv_texto(gv_str(e->texto), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
					}
				}
			}
		}
	}
}

/* ---- relógio e calendário ---- */

static int cal_mes_desloc; /* 0: o mês de hoje */

static void menu_calendario(const struct tm *hoje) {
	GV_POPUP(CLAY_ID("menu-cal"), CLAY_ID("dir"), .alinhar = GV_ALINHAR_FIM, .largura = POPUP_LARGURA, .distancia = POPUP_GAP, .altura_max = popup_altura_max) {
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
			char hora_cal[24];
			hora_texto(hoje, false, hora_cal, sizeof(hora_cal));
			gv_texto(gv_str(hora_cal), gv_t.tamanho_destaque + 10, gv_t.texto, GV_PESO_NORMAL);
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

/* ---- a Emma (lumna-emma: o serviço que fica o tempo todo) ---- */

static int emma_fd = -1;
static char emma_estado[16] = "offline";   /* offline | ocioso | ouvindo | transcrevendo | pensando | falando */
static double emma_nivel;                  /* o volume do microfone, 0 a 1 */
static bool emma_servidor;                 /* o backend da Emma (a conversa) responde; sem ele só valem os comandos curtos */
static char emma_ouvido[256], emma_resposta[1024], emma_erro[256];
static char emma_confirma_id[24], emma_confirma_nome[96], emma_confirma_desc[256];
static char emma_texto[256];
static double emma_tentar_em;
static gv_app *emma_app;

static void emma_pedir(const char *pedido) {
	char r[512];
	emmac_pedido(pedido, r, sizeof(r));
}

static void emma_evento(const struct json *j) {
	const char *ev = json_chave_texto(j, "evento");
	if (!strcmp(ev, "estado")) {
		g_strlcpy(emma_estado, json_chave_texto(j, "estado"), sizeof(emma_estado));
		emma_servidor = json_bool(json_chave(j, "online"), false);
		if (strcmp(emma_estado, "ouvindo")) {
			emma_nivel = 0;
		}
	} else if (!strcmp(ev, "nivel")) {
		emma_nivel = json_numero(json_chave(j, "nivel"), 0);
	} else if (!strcmp(ev, "ouvido")) {
		g_strlcpy(emma_ouvido, json_chave_texto(j, "texto"), sizeof(emma_ouvido));
		emma_resposta[0] = emma_erro[0] = '\0';
	} else if (!strcmp(ev, "delta")) {
		g_strlcat(emma_resposta, json_chave_texto(j, "texto"), sizeof(emma_resposta));
	} else if (!strcmp(ev, "resposta")) {
		const char *t = json_chave_texto(j, "texto");
		if (*t) {
			g_strlcpy(emma_resposta, t, sizeof(emma_resposta));
		}
	} else if (!strcmp(ev, "erro")) {
		g_strlcpy(emma_erro, json_chave_texto(j, "mensagem"), sizeof(emma_erro));
	} else if (!strcmp(ev, "confirmar")) {
		g_strlcpy(emma_confirma_id, json_chave_texto(j, "id"), sizeof(emma_confirma_id));
		g_strlcpy(emma_confirma_nome, json_chave_texto(j, "nome"), sizeof(emma_confirma_nome));
		g_strlcpy(emma_confirma_desc, json_chave_texto(j, "descricao"), sizeof(emma_confirma_desc));
	} else if (!strcmp(ev, "ferramenta") && emma_confirma_id[0]) {
		emma_confirma_id[0] = '\0';
	}
}

static void emma_caiu(void) {
	if (emma_fd >= 0) {
		gv_app_desvigiar(emma_app, emma_fd);
		close(emma_fd);
	}
	emma_fd = -1;
	g_strlcpy(emma_estado, "offline", sizeof(emma_estado));
	emma_nivel = 0;
	emma_tentar_em = gv_agora() + 4;
}

static void emma_mudou(gv_app *app, int fd, void *dados) {
	bool caiu = false;
	char *linha;
	while ((linha = emmac_assinar_ler(fd, &caiu))) {
		struct json *j = json_ler(linha);
		if (j) {
			emma_evento(j);
			json_liberar(j);
		}
		g_free(linha);
	}
	if (caiu) {
		emma_caiu();
	}
	gv_app_redesenhar(app);
}

static void emma_ligar(gv_app *app) {
	emma_app = app;
	emma_fd = emmac_assinar();
	if (emma_fd >= 0) {
		gv_app_vigiar(app, emma_fd, emma_mudou, NULL);
	} else {
		emma_tentar_em = gv_agora() + 4;
	}
}

static const char *icone_emma(void) {
	return !strcmp(emma_estado, "ouvindo") ? "mic" : !strcmp(emma_estado, "offline") ? "mic-off" : !strcmp(emma_estado, "falando") ? "waves" : "sparkle";
}

static Clay_Color cor_emma(void) {
	if (!strcmp(emma_estado, "ouvindo")) {
		return gv_t.perigo;
	}
	if (!strcmp(emma_estado, "pensando") || !strcmp(emma_estado, "transcrevendo")) {
		return gv_t.acento;
	}
	if (!strcmp(emma_estado, "falando")) {
		return gv_t.sucesso;
	}
	return !strcmp(emma_estado, "offline") || (!emma_servidor && !strcmp(emma_estado, "ocioso")) ? gv_t.texto_suave : gv_t.icone;
}

static const char *emma_frase_estado(void) {
	return !strcmp(emma_estado, "ouvindo") ? "Ouvindo… fale agora" : !strcmp(emma_estado, "transcrevendo") ? "Entendendo o que você disse…" :
		!strcmp(emma_estado, "pensando") ? "Pensando…" : !strcmp(emma_estado, "falando") ? "Falando" : !strcmp(emma_estado, "offline") ? "O serviço da Emma não está rodando" :
		emma_servidor ? "Pronta" : "Só comandos curtos: o servidor da Emma não responde";
}

static void menu_emma(void) {
	GV_POPUP(CLAY_ID("menu-emma"), CLAY_ID("dir"), .alinhar = GV_ALINHAR_FIM, .largura = POPUP_LARGURA, .distancia = POPUP_GAP, .altura_max = popup_altura_max) {
		topo_menu(5, "sparkle", "Emma", NULL);
		bool parada = !strcmp(emma_estado, "offline");
		CLAY(CLAY_ID("emma-estado"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 10, 6 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY(CLAY_ID("emma-ponto"), { .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10) } }, .backgroundColor = cor_emma(), .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				gv_texto(gv_str(emma_frase_estado()), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
		}
		if (!strcmp(emma_estado, "ouvindo")) {
			CLAY(CLAY_ID("emma-nivel"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(6) }, .padding = { 14, 14, 0, 0 } } }) {
				CLAY(CLAY_ID("emma-nivel-f"), { .layout = { .sizing = { CLAY_SIZING_PERCENT((float)MAX(emma_nivel, 0.04)), CLAY_SIZING_GROW(0) } },
					.backgroundColor = gv_t.perigo, .cornerRadius = CLAY_CORNER_RADIUS(3) }) {}
			}
		}
		if (emma_confirma_id[0]) {
			CLAY(CLAY_ID("emma-lic"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 8, 8 }, .childGap = 8 },
				.backgroundColor = gv_alfa(gv_t.aviso, 0.12f) }) {
				gv_texto(gv_fmt("A Emma quer: %s", emma_confirma_nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_str(emma_confirma_desc), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				CLAY(CLAY_ID("emma-lic-b"), { .layout = { .childGap = 10 } }) {
					if (gv_botao(CLAY_ID("emma-nao"), NULL, "Agora não", GV_BOTAO_ELEVADO)) {
						char p[64];
						snprintf(p, sizeof(p), "confirmar %s nao", emma_confirma_id);
						emma_pedir(p);
						emma_confirma_id[0] = '\0';
					}
					if (gv_botao(CLAY_ID("emma-sim"), "check", "Pode", GV_BOTAO_CHEIO)) {
						char p[64];
						snprintf(p, sizeof(p), "confirmar %s sim", emma_confirma_id);
						emma_pedir(p);
						emma_confirma_id[0] = '\0';
					}
				}
			}
		}
		if (emma_ouvido[0]) {
			CLAY(CLAY_ID("emma-voce"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 6, 6 }, .childGap = 3 } }) {
				gv_texto(CLAY_STRING("Você"), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
				gv_texto(gv_str(emma_ouvido), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
		}
		if (emma_resposta[0] || emma_erro[0]) {
			CLAY(CLAY_ID("emma-ela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 14, 6, 8 }, .childGap = 3 } }) {
				gv_texto(CLAY_STRING("Emma"), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
				gv_texto(gv_str(emma_erro[0] ? emma_erro : emma_resposta), gv_t.tamanho_corpo, emma_erro[0] ? gv_t.perigo : gv_t.texto, GV_PESO_NORMAL);
			}
		}
		gv_separador(false, 0);
		CLAY(CLAY_ID("emma-escreve"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 12, 12, 8, 8 }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				if ((gv_campo(CLAY_ID("emma-campo"), emma_texto, sizeof(emma_texto), "Peça algo à Emma", GV_CAMPO_TEXTO) & GV_CAMPO_ENTER) && emma_texto[0] && !parada) {
					char *p = g_strdup_printf("enviar %s", emma_texto);
					emma_pedir(p);
					g_free(p);
					emma_texto[0] = '\0';
				}
			}
			bool ouvindo = !strcmp(emma_estado, "ouvindo");
			if (gv_botao_ext(CLAY_ID("emma-mic"), &(gv_botao_opcoes){ .icone = "mic", .estilo = ouvindo ? GV_BOTAO_PERIGO : GV_BOTAO_CHEIO, .desativado = parada, .altura = 40 })) {
				emma_pedir("ouvir");
			}
		}
		if (!strcmp(emma_estado, "falando") || !strcmp(emma_estado, "pensando")) {
			if (gv_menu_item(CLAY_ID("emma-calar"), "close", "Parar", NULL)) {
				emma_pedir("calar");
			}
		}
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
	(void)dados;
	rede_estado_copiar(&rede);
	bt_estado_copiar(&bt);
	som_ler(&som);
	bandeja_estado_copiar(&tray);
	bandeja_menu_copiar(&tray_menu);
	if (tray_menu.pronto && tray_menu.geracao != tray_menu_visto) {
		/* o menu de um item chegou: abre-o preso a ele */
		tray_menu_visto = tray_menu.geracao;
		for (int i = 0; i < tray.n; i++) {
			if (!strcmp(tray.itens[i].chave, tray_menu.chave)) {
				tray_ancora = i;
				gv_popup_fechar(CLAY_ID("menu-modo"));
				gv_popup_fechar(CLAY_ID("menu-wifi"));
				gv_popup_fechar(CLAY_ID("menu-som"));
				gv_popup_fechar(CLAY_ID("menu-bt"));
				gv_popup_fechar(CLAY_ID("menu-cal"));
				gv_popup_abrir(CLAY_ID("menu-tray"));
			}
		}
	}
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
			Clay_ElementId rotulo = CLAY_ID("modo");
			if (gv_tocado(rotulo)) {
				menu_alternar(CLAY_ID("menu-modo"));
			}
			CLAY(rotulo, { .layout = { .padding = { 10, 8, 0, 0 }, .sizing = { .height = CLAY_SIZING_FIXED(32) }, .childGap = 6,
					.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_popup_aberto(CLAY_ID("menu-modo")) ? gv_t.selecionado : gv_por_cima(rotulo) ? gv_t.sob_cursor : (Clay_Color){ 0 },
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
				gv_focavel(rotulo, gv_t.raio_pequeno);
				gv_texto(gv_str(modos_nome(modos_em_foco(&modos_est))), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
				gv_icone("chevron-down", 14, gv_t.texto_suave);
			}
		}
		/* meio: os espaços de trabalho (só no Desktop, e só se há mais de um) */
		CLAY(CLAY_ID("areas"), { .layout = { .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			const struct modos_saida *foco = NULL;
			for (int i = 0; i < modos_est.n; i++) {
				if (!foco || modos_est.saidas[i].ponteiro) {
					foco = &modos_est.saidas[i];
				}
			}
			bool ver = areas && foco && modos_est.orbit && foco->modo == MODO_DESKTOP && modos_est.areas > 1;
			for (int i = 0; ver && i < modos_est.areas; i++) {
				Clay_ElementId id = CLAY_IDI("area", i);
				bool ativa = foco->espaco == i + 1;
				if (gv_tocado(id) && !ativa) {
					modos_definir_espaco(i + 1, foco->nome);
				}
				float w = gv_anim(id.id, ativa ? 34 : 22, gv_t.duracao_normal);
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(w), CLAY_SIZING_FIXED(16) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(4) } },
						.backgroundColor = ativa ? gv_t.acento : gv_por_cima(id) ? gv_t.texto_suave : gv_t.borda_controle,
						.cornerRadius = CLAY_CORNER_RADIUS(2) }) {}
				}
			}
		}
		/* direita: o estado do sistema e o relógio */
		CLAY(CLAY_ID("dir"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6,
			.childAlignment = { CLAY_ALIGN_X_RIGHT, CLAY_ALIGN_Y_CENTER } } }) {
			bandeja_na_barra();
			/* a Emma: o estado dela no ícone; tocar abre o menu dela (o microfone fica ali) */
			if (emma_fd < 0 && gv_agora() >= emma_tentar_em) {
				emma_ligar(app);
			}
			if (emma_fd < 0) {
				gv_acordar_em(emma_tentar_em - gv_agora() + 0.05);
			}
			{
				Clay_ElementId id = CLAY_ID("emma");
				bool tocado = gv_tocado(id);
				bool ativa = strcmp(emma_estado, "ocioso") && strcmp(emma_estado, "offline");
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(32) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = gv_popup_aberto(CLAY_ID("menu-emma")) ? gv_t.selecionado : ativa ? gv_alfa(cor_emma(), 0.16f) : gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 },
					.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
					tocado |= gv_focavel(id, gv_t.raio_pequeno);
					gv_icone(icone_emma(), 20, cor_emma());
				}
				if (tocado) {
					menu_alternar(CLAY_ID("menu-emma"));
				}
			}
			if (icone_barra(CLAY_ID("wifi"), icone_rede(), gv_popup_aberto(CLAY_ID("menu-wifi")))) {
				menu_alternar(CLAY_ID("menu-wifi"));
				rede_varrer("");
			}
			if (icone_barra(CLAY_ID("som"), icone_som(), gv_popup_aberto(CLAY_ID("menu-som")))) {
				menu_alternar(CLAY_ID("menu-som"));
			}
			if (icone_barra(CLAY_ID("bt"), icone_bt(), gv_popup_aberto(CLAY_ID("menu-bt")))) {
				menu_alternar(CLAY_ID("menu-bt"));
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
				menu_alternar(CLAY_ID("menu-cal"));
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
				char hora_txt[24];
				hora_texto(&t, segundos, hora_txt, sizeof(hora_txt));
				gv_texto(gv_str(hora_txt), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
			}
			/* acorda na virada do segundo (com os segundos) ou do minuto */
			gv_acordar_em(segundos ? 1.0 - fmod(gv_agora(), 1.0) + 0.01 : 60 - t.tm_sec + 0.05);
		}
	}
	menu_modo();
	menu_wifi();
	menu_som();
	menu_bluetooth();
	menu_tray();
	menu_emma();
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
	modos_atualizar();
	popup_medir();
	int fds[4] = { rede_iniciar(), bt_iniciar(), som_iniciar("Barra"), bandeja_iniciar() };
	for (int i = 0; i < 4; i++) {
		if (fds[i] >= 0) {
			gv_app_vigiar(app, fds[i], servico_mudou, NULL);
		}
	}
	modos_fd = modos_assinar();
	if (modos_fd >= 0) {
		gv_app_vigiar(app, modos_fd, modo_mudou, NULL);
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
