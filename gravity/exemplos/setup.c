/*
 * lumna-setup: o pré-setup, a configuração inicial que vem depois da instalação. Cinco etapas (esboços em docs/esbocos): Boas-vindas
 * (perfil de uso, idioma, teclado, fuso e formato da hora), Rede e som, Contas (e-mail e nuvem), Emma e LUMNA Connect. Toda etapa
 * pode ser pulada, e dá para pedir que ele não abra mais. O que se escolhe é gravado pelo servicos/setup.c e aplicado ao sair de cada
 * etapa, em segundo plano (as que pedem senha de administrador, como o fuso, não travam a tela).
 *
 *   lumna-setup              abre o assistente
 *   lumna-setup --login      o que o login faz: abre na primeira vez, lembra por aviso se ficou pela metade, nada se concluiu
 *   lumna-setup --passo N    abre já na etapa N (1 a 5), para testar
 */
#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "connectc.h"
#include "emma_ativa.h"
#include "emmac.h"
#include "gravity.h"
#include "idioma.h"
#include "instalador.h"
#include "json.h"
#include "rede.h"
#include "setup.h"
#include "som.h"
#include "sons.h"

enum { P_BOASVINDAS, P_REDE, P_CONTAS, P_EMMA, P_CONNECT, N_PASSOS };
static const char *const nomes_passos[N_PASSOS] = { "Boas-vindas", "Rede e som", "Contas", "Emma", "LUMNA Connect" };
static const char *const titulos[N_PASSOS] = { "Bem-vindo ao LUMNA", "Rede e som", "E-mail e nuvem", "Configure a Emma", "Conecte seu celular" };
static const char *const subtitulos[N_PASSOS] = { "Como você vai usar o sistema?", "Conecte-se e teste seus dispositivos de áudio.",
	"Conecte suas contas agora ou faça isso mais tarde.", "Escolha como a assistente funciona no LUMNA.", "Pareie seus dispositivos para interagir com o LUMNA." };
static const char *const ajuda[N_PASSOS] = {
	"O perfil prepara o modo em que o sistema começa e sugere apps. Nada é instalado sem você pedir. Tudo pode ser mudado depois no Painel.",
	"Sem rede agora? Pule: o Wi-Fi fica em Painel > Wi-Fi e na barra de cima. O som padrão fica em Painel > Som.",
	"O e-mail fica guardado para o app de e-mail usar quando existir. A nuvem aparece como uma pasta comum no seu computador.",
	"Desligar a Emma tira o serviço, o chat, os atalhos e o ícone da barra. Dá para ligar de volta em Painel > Emma.",
	"No celular, abra o LUMNA Connect e digite o código. Os recursos podem ser mudados por celular em Painel > Connect.",
};

static const char *const idiomas_id[] = { "pt_BR.UTF-8", "en_US.UTF-8" };
static const char *const idiomas_nome[] = { "Português (Brasil)", "English (US)" };
static const char *const teclados_nome[] = { "Português (Brasil) · ABNT2", "Português (Brasil)", "Inglês (EUA)", "Inglês internacional (com acentos)" };
static const char *const teclados_layout[] = { "br", "br", "us", "us" };
static const char *const teclados_variante[] = { "abnt2", "", "", "intl" };

static gv_app *app;
static int passo;
static struct setup_estado estado;
static bool nao_mostrar_mais, ajuda_aberta;
static char aviso[300];

/* etapa 1 */
static int perfil = PERFIL_DIARIO, idioma, teclado;
static char fuso[64] = "America/Sao_Paulo";
static char **fusos;
static int nfusos;
static bool h24 = true;
/* etapa 2 */
static struct rede_estado rede;
static char ap_escolhido[96], ap_ssid[40], ap_senha[128];
static struct som_estado som;
/* etapa 3 */
static char email[200], imap[80], smtp[80];
static int imap_porta = 993, smtp_porta = 587;
static char imap_porta_t[8] = "993", smtp_porta_t[8] = "587";
static bool email_manual, email_ok;
static int nuvem_sel; /* 0: nenhum; 1.. = provedor + 1 */
static char nuvem_pasta[200] = "~/Nuvem";
static bool nuvem_bg, nuvem_ocupada, nuvem_conectada;
static char nuvem_msg[240];
/* etapa 4 */
static bool emma_on = true, emma_oi;
static int preset;
static char api_url[200], api_chave[200], api_modelo[200];
static bool api_ocupada, api_testada, api_ok;
static char api_msg[240];
static const char *p_ids[8], *p_nomes[9], *p_urls[8], *p_modelos[8];
static int npresets;
/* etapa 5 */
static bool recurso[4] = { true, true, false, false };
static const char *const recurso_nome[4] = { "files", "control", "clipboard", "phone_notifications" };
static char codigo[16], enderecos[200];
static double codigo_ate, proxima_consulta;
static int pareados;
static bool parear_ocupado;
static bool recursos_aplicados, connect_servico = true;

/* ---------------- ajudas ---------------- */

struct tarefa {
	void (*trabalho)(void *);
	void (*pronto)(void *);
	void *dados;
};

static void tarefa_pronta(void *d) {
	struct tarefa *t = d;
	if (t->pronto) {
		t->pronto(t->dados);
	}
	gv_app_redesenhar(app);
	g_free(t);
}

static gpointer tarefa_linha(gpointer d) {
	struct tarefa *t = d;
	t->trabalho(t->dados);
	gv_na_linha_principal(tarefa_pronta, t);
	return NULL;
}

static void em_segundo_plano(void (*trabalho)(void *), void (*pronto)(void *), void *dados) {
	struct tarefa *t = g_new0(struct tarefa, 1);
	t->trabalho = trabalho;
	t->pronto = pronto;
	t->dados = dados;
	g_thread_unref(g_thread_new("setup", tarefa_linha, t));
}

static void titulo_cartao(const char *icone, const char *t) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone(icone, 28, gv_t.acento);
		gv_texto(gv_str(t), gv_t.tamanho_corpo + 4, gv_t.texto, GV_PESO_MEDIO);
	}
}

static void rotulo(const char *t) {
	gv_texto(gv_str(t), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_MEDIO);
}

static void nota(const char *t, Clay_Color cor) {
	if (t && *t) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(t), gv_t.tamanho_corpo - 2, cor, GV_PESO_NORMAL);
		}
	}
}

/* um cartão com o ícone e o título; quem chama enche o corpo: CARTAO(id, "icone", "Título") { ... } */
#define CARTAO(id, icone, titulo)                                                                                                         \
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },                    \
		.padding = CLAY_PADDING_ALL(20), .childGap = 12 }, .backgroundColor = gv_alfa(gv_t.elevada, 0.7f),                               \
		.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } })         \
		for (int cartao_ = (titulo_cartao(icone, titulo), 1); cartao_; cartao_ = 0)

/* ---------------- aplicar ao sair de uma etapa ---------------- */

struct aplicar {
	int etapa;
	int perfil, idioma, teclado;
	char fuso[64];
	bool h24;
	int nuvem;
	char pasta[200];
	bool nuvem_bg;
	bool emma_on, emma_oi;
	char erro[300];
};

static void aplicar_trabalho(void *d) {
	struct aplicar *a = d;
	char e[200] = "";
	if (a->etapa == P_BOASVINDAS) {
		setup_perfil_aplicar(a->perfil, e, sizeof(e));
		setup_hora_formato(a->h24);
		if (!setup_teclado(teclados_layout[a->teclado], teclados_variante[a->teclado], e, sizeof(e))) {
			g_snprintf(a->erro, sizeof(a->erro), "Teclado: %s", e);
		}
		struct idioma_cfg c;
		idioma_ler(&c);
		if (strcmp(c.lang, idiomas_id[a->idioma])) {
			g_strlcpy(c.lang, idiomas_id[a->idioma], sizeof(c.lang));
			if (!idioma_definir(&c, e, sizeof(e)) && *e) {
				g_snprintf(a->erro, sizeof(a->erro), "Idioma: %s", e);
			}
		}
		char atual[64] = "";
		char *l = g_file_read_link("/etc/localtime", NULL);
		if (l && strstr(l, "zoneinfo/")) {
			g_strlcpy(atual, strstr(l, "zoneinfo/") + 9, sizeof(atual));
		}
		g_free(l);
		if (strcmp(atual, a->fuso) && !setup_fuso(a->fuso, e, sizeof(e)) && !*a->erro) {
			g_snprintf(a->erro, sizeof(a->erro), "Fuso horário: %s", e);
		}
	} else if (a->etapa == P_CONTAS) {
		if (a->nuvem > 0 && !setup_nuvem_aplicar(a->nuvem - 1, a->pasta, a->nuvem_bg, e, sizeof(e))) {
			g_snprintf(a->erro, sizeof(a->erro), "Nuvem: %s", e);
		}
	} else if (a->etapa == P_EMMA) {
		if (!setup_emma_definir(a->emma_on, e, sizeof(e))) {
			g_snprintf(a->erro, sizeof(a->erro), "Emma: %s", e);
		} else if (a->emma_on) {
			setup_emma_oi(a->emma_oi);
		}
	}
}

static void aplicar_pronto(void *d) {
	struct aplicar *a = d;
	if (*a->erro) {
		g_strlcpy(aviso, a->erro, sizeof(aviso));
	}
	g_free(a);
}

static void aplicar_etapa(int etapa) {
	if (etapa != P_BOASVINDAS && etapa != P_CONTAS && etapa != P_EMMA) {
		return;
	}
	struct aplicar *a = g_new0(struct aplicar, 1);
	a->etapa = etapa;
	a->perfil = perfil;
	a->idioma = idioma;
	a->teclado = teclado;
	g_strlcpy(a->fuso, fuso, sizeof(a->fuso));
	a->h24 = h24;
	a->nuvem = nuvem_sel;
	g_strlcpy(a->pasta, nuvem_pasta, sizeof(a->pasta));
	a->nuvem_bg = nuvem_bg;
	a->emma_on = emma_on;
	a->emma_oi = emma_oi;
	em_segundo_plano(aplicar_trabalho, aplicar_pronto, a);
}

/* ---------------- etapa 1 ---------------- */

static bool fuso_ok(void) {
	return nfusos == 0 || instalador_fuso_existe(fusos, nfusos, fuso);
}

static void e_boasvindas(void) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		gv_icone("user", 22, gv_t.acento);
		gv_texto(CLAY_STRING("Perfil de uso"), gv_t.tamanho_corpo + 3, gv_t.texto, GV_PESO_MEDIO);
	}
	CLAY(CLAY_ID("perfis"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
		for (int i = 0; i < PERFIL_N; i++) {
			bool sel = perfil == i;
			Clay_ElementId id = CLAY_IDI("perfil", (uint32_t)i);
			CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 8 },
				.backgroundColor = sel ? gv_alfa(gv_t.acento, 0.14f) : gv_alfa(gv_t.elevada, 0.7f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
				.border = { .color = sel ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(sel ? 2 : 1) } }) {
				bool ativou = gv_focavel(id, gv_t.raio_medio);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_LEFT } } }) {
					gv_icone(setup_perfil_icone(i), 34, sel ? gv_t.acento : gv_t.icone);
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
					if (sel) {
						gv_icone("check", 22, gv_t.acento);
					}
				}
				gv_texto(gv_str(setup_perfil_nome(i)), gv_t.tamanho_corpo + 2, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_str(setup_perfil_descricao(i)), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				if (gv_tocado(id) || ativou) {
					perfil = i;
				}
			}
		}
	}
	nota("O perfil prepara os atalhos e as sugestões de apps. Você pode mudar depois.", gv_t.texto_suave);
	CLAY(CLAY_ID("sep1"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
		gv_icone("globe", 22, gv_t.acento);
		gv_texto(CLAY_STRING("Idioma e horário"), gv_t.tamanho_corpo + 3, gv_t.texto, GV_PESO_MEDIO);
	}
	CLAY(CLAY_ID("regiao"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 24 } }) {
		CLAY(CLAY_ID("rg-e"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
			rotulo("Idioma");
			gv_lista_suspensa(CLAY_ID("rg-idioma"), idiomas_nome, 2, &idioma);
			rotulo("Fuso horário");
			gv_campo(CLAY_ID("rg-fuso"), fuso, sizeof(fuso), "America/Sao_Paulo", GV_CAMPO_BUSCA);
			if (!fuso_ok()) {
				char *busca = g_ascii_strdown(fuso, -1);
				int mostrados = 0;
				for (int i = 0; i < nfusos && mostrados < 3; i++) {
					char *baixo = g_ascii_strdown(fusos[i], -1);
					if (*busca && strstr(baixo, busca)) {
						if (gv_botao_ext(CLAY_IDI("rg-sug", (uint32_t)i), &(gv_botao_opcoes){ .texto = fusos[i], .estilo = GV_BOTAO_SIMPLES, .altura = 30 })) {
							g_strlcpy(fuso, fusos[i], sizeof(fuso));
						}
						mostrados++;
					}
					g_free(baixo);
				}
				g_free(busca);
				if (!mostrados) {
					nota("Fuso desconhecido.", gv_t.perigo);
				}
			}
		}
		CLAY(CLAY_ID("rg-d"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
			rotulo("Teclado");
			gv_lista_suspensa(CLAY_ID("rg-teclado"), teclados_nome, 4, &teclado);
			rotulo("Formato de hora");
			CLAY(CLAY_ID("rg-hora"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
				if (gv_botao_ext(CLAY_ID("h24"), &(gv_botao_opcoes){ .icone = h24 ? "check" : NULL, .texto = "24 horas · 16:37", .estilo = h24 ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO, .crescer = true, .altura = 40 })) {
					h24 = true;
				}
				if (gv_botao_ext(CLAY_ID("h12"), &(gv_botao_opcoes){ .icone = !h24 ? "check" : NULL, .texto = "12 horas · 4:37 PM", .estilo = !h24 ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO, .crescer = true, .altura = 40 })) {
					h24 = false;
				}
			}
		}
	}
}

/* ---------------- etapa 2 ---------------- */

static void e_rede(void) {
	rede_estado_copiar(&rede);
	som_ler(&som);
	CLAY(CLAY_ID("rede-cols"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		CARTAO(CLAY_ID("c-wifi"), "wifi", "Wi-Fi") {
			bool ligado = rede.wifi_ligado;
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_interruptor(CLAY_ID("wifi-lig"), &ligado)) {
					rede_wifi_ligar(ligado);
				}
			}
			if (!rede.nm) {
				nota("O NetworkManager não respondeu: sem rede por aqui. Dá para pular.", gv_t.aviso);
			}
			int mostrados = 0;
			for (int i = 0; i < rede.naps && mostrados < 5; i++) {
				const struct rede_ap *ap = &rede.aps[i];
				bool sel = !strcmp(ap_escolhido, ap->caminho);
				Clay_ElementId id = CLAY_IDI("ap", (uint32_t)i);
				CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(46) }, .padding = { 14, 14, 0, 0 }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = sel || ap->atual ? gv_alfa(gv_t.acento, 0.16f) : gv_alfa(gv_t.elevada, 0.5f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
					.border = { .color = sel ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(1) } }) {
					bool ativou = gv_focavel(id, gv_t.raio_pequeno);
					gv_icone("wifi", 22, gv_t.icone);
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
						gv_texto(gv_str(ap->ssid), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
					}
					if (ap->segura) {
						gv_icone("lock", 18, gv_t.icone);
					}
					if (ap->atual) {
						gv_icone("check", 20, gv_t.acento);
					}
					if (gv_tocado(id) || ativou) {
						g_strlcpy(ap_escolhido, ap->caminho, sizeof(ap_escolhido));
						g_strlcpy(ap_ssid, ap->ssid, sizeof(ap_ssid));
						ap_senha[0] = '\0';
						if (!ap->segura || ap->salva) {
							rede_conectar(ap->disp, ap->caminho, "");
						}
					}
				}
				mostrados++;
			}
			if (rede.nm && rede.wifi_ligado && !rede.naps) {
				nota("Procurando redes…", gv_t.texto_suave);
			}
			const struct rede_ap *esc = NULL;
			for (int i = 0; i < rede.naps; i++) {
				if (!strcmp(ap_escolhido, rede.aps[i].caminho)) {
					esc = &rede.aps[i];
				}
			}
			if (esc && esc->segura && !esc->salva && !esc->atual) {
				rotulo(gv_fmt("Senha da rede %s", esc->ssid).chars);
				int r = gv_campo(CLAY_ID("ap-senha"), ap_senha, sizeof(ap_senha), "Senha", GV_CAMPO_SENHA);
				if (gv_botao_ext(CLAY_ID("ap-conectar"), &(gv_botao_opcoes){ .texto = "Conectar", .estilo = GV_BOTAO_CHEIO, .desativado = strlen(ap_senha) < 8 }) || (r & GV_CAMPO_ENTER && strlen(ap_senha) >= 8)) {
					rede_conectar(esc->disp, esc->caminho, ap_senha);
				}
			}
			nota(rede.erro, gv_t.perigo);
			if (rede.internet) {
				nota("Conectado, com internet.", gv_t.sucesso);
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				if (gv_botao_ext(CLAY_ID("rede-depois"), &(gv_botao_opcoes){ .texto = "Configurar rede depois", .estilo = GV_BOTAO_SIMPLES, .altura = 32 })) {
					passo = P_CONTAS;
				}
			}
		}
		CARTAO(CLAY_ID("c-som"), "speaker", "Áudio padrão") {
			if (!som.ligado) {
				nota("O servidor de som não respondeu.", gv_t.aviso);
			}
			const char *nomes[SOM_MAX_SAIDAS];
			int atual = 0;
			for (int i = 0; i < som.nsaidas; i++) {
				nomes[i] = som.saidas[i].descricao;
				if (som.saidas[i].padrao) {
					atual = i;
				}
			}
			rotulo("Saída de som");
			if (som.nsaidas && gv_lista_suspensa(CLAY_ID("som-saida"), nomes, som.nsaidas, &atual)) {
				som_saida_padrao(som.saidas[atual].nome);
			} else if (!som.nsaidas) {
				nota("Nenhuma saída de som encontrada.", gv_t.texto_suave);
			}
			if (gv_botao_ext(CLAY_ID("som-testar"), &(gv_botao_opcoes){ .icone = "play", .texto = "Testar som", .estilo = GV_BOTAO_ELEVADO, .altura = 38 })) {
				struct sons_cfg c;
				sons_ler(&c);
				if (!sons_tocar(c.aviso, 80)) {
					g_strlcpy(aviso, "Não achei um som de teste (falta o sound-theme-freedesktop ou o pw-play).", sizeof(aviso));
				}
			}
			const char *entradas[SOM_MAX_SAIDAS];
			int ent = 0;
			for (int i = 0; i < som.nentradas; i++) {
				entradas[i] = som.entradas[i].descricao;
				if (som.entradas[i].padrao) {
					ent = i;
				}
			}
			rotulo("Entrada de áudio");
			if (som.nentradas && gv_lista_suspensa(CLAY_ID("som-entrada"), entradas, som.nentradas, &ent)) {
				som_entrada_padrao(som.entradas[ent].nome);
			} else if (!som.nentradas) {
				nota("Nenhum microfone encontrado.", gv_t.texto_suave);
			}
		}
	}
}

/* ---------------- etapa 3 ---------------- */

struct nuvem_job {
	int provedor;
	char pasta[200], erro[240];
	bool ok;
};

static void nuvem_trabalho(void *d) {
	struct nuvem_job *j = d;
	j->ok = setup_nuvem_conectar(j->provedor, j->pasta, j->erro, sizeof(j->erro));
}

static void nuvem_pronto(void *d) {
	struct nuvem_job *j = d;
	nuvem_ocupada = false;
	nuvem_conectada = j->ok;
	g_strlcpy(nuvem_msg, j->ok ? "Conta conectada." : j->erro, sizeof(nuvem_msg));
	g_free(j);
}

static void e_contas(void) {
	static const char *nuvens[NUVEM_N + 1] = { "Selecionar provedor", "Google Drive", "OneDrive", "Dropbox" };
	CLAY(CLAY_ID("contas-cols"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		CARTAO(CLAY_ID("c-email"), "mail", "E-mail") {
			rotulo("Endereço de e-mail");
			gv_campo(CLAY_ID("em-end"), email, sizeof(email), "voce@exemplo.com", GV_CAMPO_EMAIL);
			if (email_manual) {
				rotulo("Servidor de entrada (IMAP)");
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
					CLAY(CLAY_ID("em-imap-c"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) { gv_campo(CLAY_ID("em-imap"), imap, sizeof(imap), "imap.exemplo.com", GV_CAMPO_TEXTO); }
					CLAY(CLAY_ID("em-imapp-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(90) } } }) { gv_campo(CLAY_ID("em-imapp"), imap_porta_t, sizeof(imap_porta_t), "993", GV_CAMPO_NUMERO); }
				}
				rotulo("Servidor de saída (SMTP)");
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8 } }) {
					CLAY(CLAY_ID("em-smtp-c"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) { gv_campo(CLAY_ID("em-smtp"), smtp, sizeof(smtp), "smtp.exemplo.com", GV_CAMPO_TEXTO); }
					CLAY(CLAY_ID("em-smtpp-c"), { .layout = { .sizing = { CLAY_SIZING_FIXED(90) } } }) { gv_campo(CLAY_ID("em-smtpp"), smtp_porta_t, sizeof(smtp_porta_t), "587", GV_CAMPO_NUMERO); }
				}
			}
			if (gv_botao_ext(CLAY_ID("em-conectar"), &(gv_botao_opcoes){ .texto = email_ok ? "E-mail guardado" : "Conectar e-mail", .icone = email_ok ? "check" : NULL,
				.estilo = GV_BOTAO_CHEIO, .desativado = !setup_email_valido(email) })) {
				char e[200] = "";
				imap_porta = atoi(imap_porta_t) ? atoi(imap_porta_t) : 993;
				smtp_porta = atoi(smtp_porta_t) ? atoi(smtp_porta_t) : 587;
				email_ok = setup_email_guardar(email, email_manual ? imap : "", imap_porta, email_manual ? smtp : "", smtp_porta, e, sizeof(e));
				g_strlcpy(aviso, e, sizeof(aviso));
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				if (gv_botao_ext(CLAY_ID("em-manual"), &(gv_botao_opcoes){ .texto = email_manual ? "Esconder a configuração manual" : "Configuração manual", .estilo = GV_BOTAO_SIMPLES, .altura = 32 })) {
					email_manual = !email_manual;
				}
			}
			nota(email_ok ? "Guardado: o app de e-mail vai usar este endereço quando chegar." : "Ainda não há app de e-mail no LUMNA: o endereço fica guardado para depois.", gv_t.texto_suave);
		}
		CARTAO(CLAY_ID("c-nuvem"), "cloud", "Armazenamento em nuvem") {
			rotulo("Serviço");
			if (gv_lista_suspensa(CLAY_ID("nv-prov"), nuvens, NUVEM_N + 1, &nuvem_sel)) {
				nuvem_conectada = false;
				nuvem_msg[0] = '\0';
			}
			if (gv_botao_ext(CLAY_ID("nv-conectar"), &(gv_botao_opcoes){ .texto = nuvem_ocupada ? "Aguardando o navegador…" : nuvem_conectada ? "Conta conectada" : "Conectar conta",
				.icone = nuvem_conectada ? "check" : NULL, .estilo = GV_BOTAO_ELEVADO, .desativado = nuvem_sel == 0 || nuvem_ocupada })) {
				struct nuvem_job *j = g_new0(struct nuvem_job, 1);
				j->provedor = nuvem_sel - 1;
				g_strlcpy(j->pasta, nuvem_pasta, sizeof(j->pasta));
				nuvem_ocupada = true;
				nuvem_msg[0] = '\0';
				em_segundo_plano(nuvem_trabalho, nuvem_pronto, j);
			}
			nota(nuvem_msg, nuvem_conectada ? gv_t.sucesso : gv_t.perigo);
			rotulo("Pasta de sincronização");
			gv_campo(CLAY_ID("nv-pasta"), nuvem_pasta, sizeof(nuvem_pasta), "~/Nuvem", GV_CAMPO_TEXTO);
			gv_linha(CLAY_ID("nv-bg"), NULL, "Sincronizar em segundo plano", NULL, &nuvem_bg);
		}
	}
}

/* ---------------- etapa 4 ---------------- */

struct api_job {
	char url[200], chave[200], modelo[200], msg[240];
	bool ok;
};

static void api_trabalho(void *d) {
	struct api_job *j = d;
	GString *p = g_string_new("llm {\"acao\":\"testar\",\"url\":");
	json_escrever_texto(p, j->url);
	g_string_append(p, ",\"chave\":");
	json_escrever_texto(p, j->chave);
	char *m = g_strdup(j->modelo);
	char *virg = strchr(m, ',');
	if (virg) {
		*virg = '\0';
	}
	g_strstrip(m);
	if (*m) {
		g_string_append(p, ",\"modelo\":");
		json_escrever_texto(p, m);
	}
	g_string_append_c(p, '}');
	g_free(m);
	char resp[4096];
	if (!emmac_pedido(p->str, resp, sizeof(resp))) {
		g_strlcpy(j->msg, "O serviço da Emma (lumna-emma) não respondeu. Ligue-o e teste de novo.", sizeof(j->msg));
	} else {
		struct json *r = json_ler(resp);
		j->ok = json_bool(json_chave(r, "chat"), false);
		if (j->ok) {
			g_strlcpy(j->msg, "Conexão funcionando.", sizeof(j->msg));
		} else {
			const char *err = json_chave_texto(r, "error");
			g_strlcpy(j->msg, *err ? err : "O provedor não respondeu como esperado.", sizeof(j->msg));
		}
		json_liberar(r);
	}
	g_string_free(p, TRUE);
}

static void api_pronto(void *d) {
	struct api_job *j = d;
	api_ocupada = false;
	api_testada = true;
	api_ok = j->ok;
	g_strlcpy(api_msg, j->msg, sizeof(api_msg));
	if (j->ok) {
		char e[200] = "";
		setup_emma_provedor(p_ids[preset - 1], p_nomes[preset], j->url, j->chave, j->modelo, e, sizeof(e));
		if (*e) {
			g_snprintf(api_msg, sizeof(api_msg), "Funcionando. %s", e);
		}
	}
	g_free(j);
}

static void e_emma(void) {
	CLAY(CLAY_ID("emma-cols"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		CLAY(CLAY_ID("emma-esq"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 16 } }) {
			CARTAO(CLAY_ID("c-ativ"), "emma", "Ativação") {
				gv_linha(CLAY_ID("emma-on"), NULL, "Ativar Emma no sistema", "Disponível no painel, na barra e por atalhos.", &emma_on);
				if (!emma_on) {
					nota("Desligada: nenhum serviço, chat, atalho ou ícone da Emma. Para ligar de volta: Painel > Emma.", gv_t.aviso);
				}
				bool oi = emma_oi && emma_on;
				if (gv_linha(CLAY_ID("emma-oi"), NULL, "Responder a “Oi Emma”", "Usa o microfone em segundo plano.", &oi) && emma_on) {
					emma_oi = oi;
				}
			}
			CARTAO(CLAY_ID("c-comp"), "sliders", "Comportamento") {
				rotulo("Perfil de conteúdo");
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10 } }) {
					gv_botao_ext(CLAY_ID("pf-sfw"), &(gv_botao_opcoes){ .texto = "SFW", .estilo = GV_BOTAO_CHEIO, .crescer = true, .altura = 38 });
					gv_botao_ext(CLAY_ID("pf-nsfw"), &(gv_botao_opcoes){ .texto = "NSFW", .estilo = GV_BOTAO_ELEVADO, .crescer = true, .altura = 38, .desativado = true });
				}
				nota("O NSFW pede o PIN e liga depois, pelo ícone de perfil na barra.", gv_t.texto_suave);
			}
		}
		CARTAO(CLAY_ID("c-api"), "stack", "API da Emma") {
			if (!emma_on) {
				nota("Ative a Emma para configurar o provedor.", gv_t.texto_suave);
			}
			rotulo("Provedor");
			if (gv_lista_suspensa(CLAY_ID("api-prov"), p_nomes, npresets + 1, &preset) && preset > 0) {
				g_strlcpy(api_url, p_urls[preset - 1], sizeof(api_url));
				g_strlcpy(api_modelo, p_modelos[preset - 1], sizeof(api_modelo));
				api_testada = false;
			}
			rotulo("URL da API");
			gv_campo(CLAY_ID("api-url"), api_url, sizeof(api_url), "https://api.exemplo.com", GV_CAMPO_URL);
			rotulo("Chave da API");
			gv_campo(CLAY_ID("api-chave"), api_chave, sizeof(api_chave), "Chave", GV_CAMPO_SENHA);
			rotulo("Modelo (o primeiro da lista vale)");
			gv_campo(CLAY_ID("api-modelo"), api_modelo, sizeof(api_modelo), "modelo-1, modelo-2", GV_CAMPO_TEXTO);
			if (gv_botao_ext(CLAY_ID("api-testar"), &(gv_botao_opcoes){ .icone = "zap", .texto = api_ocupada ? "Testando…" : "Testar conexão", .estilo = GV_BOTAO_ELEVADO,
				.desativado = !emma_on || preset == 0 || !api_url[0] || api_ocupada })) {
				struct api_job *j = g_new0(struct api_job, 1);
				g_strlcpy(j->url, api_url, sizeof(j->url));
				g_strlcpy(j->chave, api_chave, sizeof(j->chave));
				g_strlcpy(j->modelo, api_modelo, sizeof(j->modelo));
				api_ocupada = true;
				api_testada = false;
				em_segundo_plano(api_trabalho, api_pronto, j);
			}
			if (api_testada) {
				nota(api_msg, api_ok ? gv_t.sucesso : gv_t.perigo);
			}
			if (api_ok) {
				nota("Salvo em Painel > Emma > Provedores.", gv_t.texto_suave);
			}
		}
	}
}

/* ---------------- etapa 5 ---------------- */

struct connect_job {
	bool parear;
	char resp[4096];          /* a resposta do "parear" */
	char estado[64 * 1024];   /* o estado do serviço (os celulares pareados) */
	bool ok;                  /* o serviço respondeu */
};

static void connect_trabalho(void *d) {
	struct connect_job *j = d;
	if (j->parear && !connectc_pedido("parear", j->resp, sizeof(j->resp))) {
		j->resp[0] = '\0';
	}
	j->ok = connectc_pedido("connect", j->estado, sizeof(j->estado));
}

static void aplicar_recursos(const struct json *estado_json) {
	const struct json *ds = json_chave(estado_json, "dispositivos");
	for (int i = 0; ds && i < ds->nfilhos; i++) {
		const char *id = json_chave_texto(json_item(ds, i), "id");
		for (int r = 0; r < 4; r++) {
			char p[160], resp[256];
			g_snprintf(p, sizeof(p), "recurso %s %s %s", id, recurso_nome[r], recurso[r] ? "sim" : "nao");
			connectc_pedido(p, resp, sizeof(resp));
		}
	}
}

static void connect_pronto(void *d) {
	struct connect_job *j = d;
	connect_servico = j->ok;
	if (*j->resp) {
		struct json *r = json_ler(j->resp);
		if (r && !json_bool(json_chave(r, "ok"), true)) {
			g_strlcpy(aviso, json_chave_texto(r, "erro"), sizeof(aviso));
		} else if (r && *json_chave_texto(r, "codigo")) {
			g_strlcpy(codigo, json_chave_texto(r, "codigo"), sizeof(codigo));
			codigo_ate = gv_agora() + json_numero(json_chave(r, "expira"), 300);
			GString *e = g_string_new(NULL);
			const struct json *en = json_chave(r, "enderecos");
			for (int i = 0; en && i < en->nfilhos; i++) {
				g_string_append_printf(e, "%s%s:%d", i ? "  ·  " : "", json_item(en, i)->s, (int)json_numero(json_chave(r, "porta"), 47800));
			}
			g_strlcpy(enderecos, e->str, sizeof(enderecos));
			g_string_free(e, TRUE);
		}
		json_liberar(r);
	}
	if (j->ok) {
		struct json *est = json_ler(j->estado);
		const struct json *ds = json_chave(est, "dispositivos");
		int n = ds ? ds->nfilhos : 0;
		if (n > pareados && !recursos_aplicados) {
			aplicar_recursos(est); /* um celular acabou de parear: ele já nasce com o que foi escolhido aqui */
			recursos_aplicados = true;
		}
		pareados = n;
		json_liberar(est);
	}
	parear_ocupado = false;
	g_free(j);
}

static void connect_pedir(bool parear) {
	if (parear_ocupado) {
		return;
	}
	struct connect_job *j = g_new0(struct connect_job, 1);
	j->parear = parear;
	parear_ocupado = true;
	em_segundo_plano(connect_trabalho, connect_pronto, j);
}

static void e_connect(void) {
	if (!parear_ocupado && gv_agora() >= proxima_consulta) {
		proxima_consulta = gv_agora() + 2.0;
		connect_pedir(false);
	}
	gv_acordar_em(2.0);
	CLAY(CLAY_ID("cc-cols"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 18 } }) {
		CARTAO(CLAY_ID("c-parear"), "phone", "Parear um dispositivo") {
			bool mostrando = codigo[0] && gv_agora() < codigo_ate;
			if (mostrando) {
				CLAY(CLAY_ID("cc-cod"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 6,
					.childAlignment = { .x = CLAY_ALIGN_X_CENTER } }, .backgroundColor = gv_alfa(gv_t.acento, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
					.border = { .color = gv_alfa(gv_t.acento, 0.5f), .width = GV_BORDA(1) } }) {
					gv_texto(CLAY_STRING("No celular, abra o LUMNA Connect e digite:"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
					gv_texto(gv_str(codigo), 44, gv_t.acento, GV_PESO_MEDIO);
					nota(gv_fmt("Vale mais %d min e só uma vez. Se o celular não achar o computador sozinho, digite o endereço: %s", (int)((codigo_ate - gv_agora()) / 60) + 1,
						enderecos[0] ? enderecos : "(sem rede)").chars, gv_t.texto_secundario);
				}
			} else if (!connect_servico) {
				nota("O serviço do celular (lumna-connect) não está rodando. Dá para pular e parear depois, em Painel > Connect.", gv_t.aviso);
			} else {
				nota("Abra o LUMNA Connect no celular, na mesma rede, e gere o código aqui.", gv_t.texto_secundario);
			}
			if (gv_botao_ext(CLAY_ID("cc-gerar"), &(gv_botao_opcoes){ .icone = "dialpad", .texto = mostrando ? "Gerar outro código" : "Usar código de pareamento", .estilo = GV_BOTAO_ELEVADO, .desativado = parear_ocupado })) {
				codigo[0] = '\0';
				connect_pedir(true);
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY(CLAY_ID("cc-ponto"), { .layout = { .sizing = { CLAY_SIZING_FIXED(10), CLAY_SIZING_FIXED(10) } }, .backgroundColor = pareados ? gv_t.sucesso : gv_t.texto_suave, .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
				gv_texto(gv_str(pareados ? gv_fmt("%d dispositivo%s pareado%s", pareados, pareados > 1 ? "s" : "", pareados > 1 ? "s" : "").chars : "Aguardando dispositivo"),
					gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		CARTAO(CLAY_ID("c-apos"), "sliders", "Após o pareamento") {
			nota("Escolha o que você quer habilitar.", gv_t.texto_secundario);
			static const char *const rotulos[4] = { "Transferência de arquivos", "Controle de mídia", "Área de transferência", "Notificações do celular" };
			static const char *const icones[4] = { "file", "play", "clipboard", "bell" };
			for (int i = 0; i < 4; i++) {
				if (gv_linha(CLAY_IDI("rec", (uint32_t)i), icones[i], rotulos[i], NULL, &recurso[i]) && pareados) {
					recursos_aplicados = false; /* já pareado: a escolha vale agora, nos celulares que existem */
					char *buf = g_malloc(64 * 1024);
					if (connectc_pedido("connect", buf, 64 * 1024)) {
						struct json *est = json_ler(buf);
						aplicar_recursos(est);
						json_liberar(est);
					}
					g_free(buf);
				}
			}
			nota("Você pode alterar essas opções por dispositivo, em Painel > Connect.", gv_t.texto_suave);
		}
	}
}

/* ---------------- a janela ---------------- */

static bool pode_continuar(void) {
	return passo != P_BOASVINDAS || fuso_ok();
}

static void sair_do_assistente(bool concluir) {
	estado.concluido = concluir || estado.concluido;
	estado.nao_mostrar = nao_mostrar_mais;
	setup_gravar(&estado);
	gv_app_sair(app);
}

static void ir_para(int novo) {
	if (novo > passo) {
		aplicar_etapa(passo);
	}
	passo = novo;
	aviso[0] = '\0';
	if (passo == P_CONNECT) {
		proxima_consulta = 0;
	}
}

static void depurar(void);

static void desenhar(gv_app *a, void *dados) {
	CLAY(CLAY_ID("janela"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.superficie }) {
		/* à esquerda: a marca e as cinco etapas */
		CLAY(CLAY_ID("lateral"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(330), CLAY_SIZING_GROW(0) }, .padding = { 40, 28, 44, 28 }, .childGap = 18,
			.childAlignment = { .x = CLAY_ALIGN_X_LEFT } }, .backgroundColor = gv_t.fundo }) {
			CLAY(CLAY_ID("marca"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
				gv_icone("lumna", 96, gv_t.acento);
				gv_texto(CLAY_STRING("L U M N A   O S"), gv_t.tamanho_corpo + 2, gv_t.texto_secundario, GV_PESO_MEDIO);
			}
			CLAY(CLAY_ID("lema"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 36, 10 } } }) {
				gv_texto(CLAY_STRING("Seu espaço começa aqui."), gv_t.tamanho_titulo + 2, gv_t.texto, GV_PESO_MEDIO);
			}
			for (int i = 0; i < N_PASSOS; i++) {
				bool ativo = i == passo, feito = i < passo;
				CLAY(CLAY_IDI("ps", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY(CLAY_IDI("ps-p", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(34) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
						.cornerRadius = CLAY_CORNER_RADIUS(17), .border = { .color = ativo ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(ativo ? 2 : 1) } }) {
						gv_texto(gv_fmt("%02d", i + 1), gv_t.tamanho_corpo - 1, ativo ? gv_t.acento : gv_t.texto_secundario, GV_PESO_MEDIO);
					}
					CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
						gv_texto(gv_str(nomes_passos[i]), gv_t.tamanho_corpo + 2, ativo ? gv_t.acento : gv_t.texto, ativo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
					}
					if (feito) {
						gv_icone("check", 18, gv_t.texto_secundario);
					}
				}
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}
			CLAY(CLAY_ID("nao-mostrar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				CLAY(CLAY_ID("nm-t"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
					gv_texto(CLAY_STRING("Não mostrar de novo"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
					gv_texto(CLAY_STRING("Nem ao entrar, nem por aviso."), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				gv_interruptor(CLAY_ID("nm-i"), &nao_mostrar_mais);
			}
		}
		/* à direita: a etapa */
		CLAY(CLAY_ID("direita"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 40, 40, 30, 22 }, .childGap = 14 } }) {
			CLAY(CLAY_ID("topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				gv_texto(CLAY_STRING("C O N F I G U R A Ç Ã O   I N I C I A L"), gv_t.tamanho_corpo - 3, gv_t.texto_suave, GV_PESO_MEDIO);
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
				if (gv_botao_redondo(CLAY_ID("ajuda"), "help", 36, ajuda_aberta, false)) {
					ajuda_aberta = !ajuda_aberta;
				}
			}
			gv_texto(gv_str(titulos[passo]), gv_t.tamanho_titulo + 8, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(subtitulos[passo]), gv_t.tamanho_corpo + 3, gv_t.texto_secundario, GV_PESO_NORMAL);
			if (ajuda_aberta) {
				CLAY(CLAY_ID("ajuda-txt"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(12) }, .backgroundColor = gv_alfa(gv_t.acento, 0.1f),
					.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno) }) {
					gv_texto(gv_str(ajuda[passo]), gv_t.tamanho_corpo - 1, gv_t.texto, GV_PESO_NORMAL);
				}
			}
			nota(aviso, gv_t.perigo);
			Clay_ElementId etapa_id = CLAY_ID("etapa");
			CLAY(etapa_id, gv_rolagem(etapa_id, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
				.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 12 } })) {
				switch (passo) {
				case P_BOASVINDAS: e_boasvindas(); break;
				case P_REDE: e_rede(); break;
				case P_CONTAS: e_contas(); break;
				case P_EMMA: e_emma(); break;
				default: e_connect(); break;
				}
			}
			CLAY(CLAY_ID("sep-rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1) } }, .backgroundColor = gv_t.borda }) {}
			CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
				if (gv_botao_ext(CLAY_ID("sair"), &(gv_botao_opcoes){ .texto = "Fechar", .estilo = GV_BOTAO_SIMPLES, .altura = 40 })) {
					sair_do_assistente(false);
				}
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
				if (passo > 0 && gv_botao_ext(CLAY_ID("voltar"), &(gv_botao_opcoes){ .icone = "arrow-left", .texto = "Voltar", .estilo = GV_BOTAO_ELEVADO, .altura = 42 })) {
					ir_para(passo - 1);
				}
				if (passo > 0 && gv_botao_ext(CLAY_ID("pular"), &(gv_botao_opcoes){ .texto = passo == N_PASSOS - 1 ? "Pular e terminar" : "Pular por agora", .estilo = GV_BOTAO_CONTORNO, .altura = 42 })) {
					if (passo == N_PASSOS - 1) {
						sair_do_assistente(true);
					} else {
						passo++;
						aviso[0] = '\0';
					}
				}
				gv_texto(gv_fmt("Etapa %d de %d", passo + 1, N_PASSOS), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				bool ultimo = passo == N_PASSOS - 1;
				if (gv_botao_ext(CLAY_ID("continuar"), &(gv_botao_opcoes){ .icone = "arrow-right", .texto = ultimo ? "Começar a usar o LUMNA" : "Continuar", .estilo = GV_BOTAO_CHEIO,
					.altura = 42, .desativado = !pode_continuar() })) {
					if (ultimo) {
						aplicar_etapa(passo);
						sair_do_assistente(true);
					} else {
						ir_para(passo + 1);
					}
				}
			}
		}
	}
	depurar();
}

/* ---------------- depuração (LUMNA_SETUP_DEBUG): onde está cada peça e o estado, para os testes ---------------- */

static void depurar(void) {
	static int ligado = -1;
	static GHashTable *antes;
	static char *estado_antes;
	if (ligado < 0) {
		ligado = getenv("LUMNA_SETUP_DEBUG") != NULL;
	}
	if (!ligado) {
		return;
	}
	if (!antes) {
		antes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	GPtrArray *nomes = g_ptr_array_new_with_free_func(g_free);
	GArray *ids = g_array_new(FALSE, FALSE, sizeof(uint32_t));
#define F(x) do { g_ptr_array_add(nomes, g_strdup(x)); Clay_ElementId e_ = CLAY_ID(x); g_array_append_val(ids, e_.id); } while (0)
#define S(base, n) for (int i_ = 0; i_ < (n); i_++) { g_ptr_array_add(nomes, g_strdup_printf("%s/%d", base, i_)); Clay_ElementId e_ = CLAY_IDI(base, (uint32_t)i_); g_array_append_val(ids, e_.id); }
	F("continuar"); F("voltar"); F("pular"); F("sair"); F("ajuda"); F("nm-i"); F("h24"); F("h12"); F("rg-fuso"); F("em-end"); F("em-conectar"); F("em-manual");
	F("em-imap"); F("emma-on"); F("emma-oi"); F("api-testar"); F("api-url"); F("api-chave"); F("api-modelo"); F("cc-gerar"); F("rede-depois"); F("wifi-lig"); F("som-testar");
	F("nv-conectar"); F("nv-pasta"); F("nv-bg"); F("api-prov"); F("nv-prov");
	S("perfil", PERFIL_N); S("rec", 4); S("ap", 5); S("rg-sug", 3);
#undef F
#undef S
	bool mudou = false;
	for (guint i = 0; i < nomes->len; i++) {
		Clay_BoundingBox b = gv_caixa((Clay_ElementId){ .id = g_array_index(ids, uint32_t, i) });
		char *linha = g_strdup_printf("%.0f %.0f %.0f %.0f", b.width > 0 ? b.x : 0, b.width > 0 ? b.y : 0, b.width > 0 ? b.width : 0, b.width > 0 ? b.height : 0);
		char *ant = g_hash_table_lookup(antes, nomes->pdata[i]);
		if ((!ant && b.width > 0) || (ant && strcmp(ant, linha))) {
			printf("pos %s %s\n", (char *)nomes->pdata[i], linha);
			g_hash_table_insert(antes, g_strdup(nomes->pdata[i]), linha);
			mudou = true;
		} else {
			g_free(linha);
		}
	}
	char *est = g_strdup_printf("estado passo=%d perfil=%d h24=%d fuso=%s email_ok=%d emma_on=%d emma_oi=%d api=%d,%d pareados=%d codigo=%s nm=%d aviso=%s concluido=%d",
		passo + 1, perfil, h24, fuso, email_ok, emma_on, emma_oi, api_testada, api_ok, pareados, codigo[0] ? codigo : "-", nao_mostrar_mais, aviso[0] ? "sim" : "nao", estado.concluido);
	if (!estado_antes || strcmp(estado_antes, est) || mudou) {
		printf("%s\n", est);
		g_free(estado_antes);
		estado_antes = est;
		fflush(stdout);
	} else {
		g_free(est);
	}
	g_ptr_array_free(nomes, TRUE);
	g_array_free(ids, TRUE);
}

static void ao_andar_rede(gv_app *a, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	gv_app_redesenhar(a);
}

static void ao_andar_som(gv_app *a, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	gv_app_redesenhar(a);
}

/* ---------------- o login: abre ou lembra ---------------- */

static void lembrete(void) {
	GError *erro = NULL;
	GDBusConnection *c = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &erro);
	if (!c) {
		g_clear_error(&erro);
		return;
	}
	GVariantBuilder acoes, dicas;
	g_variant_builder_init(&acoes, G_VARIANT_TYPE("as"));
	g_variant_builder_add(&acoes, "s", "default");
	g_variant_builder_add(&acoes, "s", "Continuar a configuração");
	g_variant_builder_init(&dicas, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&dicas, "{sv}", "desktop-entry", g_variant_new_string("lumna-setup"));
	GVariant *r = g_dbus_connection_call_sync(c, "org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
		g_variant_new("(susssasa{sv}i)", "LUMNA", 0u, "lumna", "Termine de configurar o seu LUMNA", "Rede, e-mail, Emma e celular: leva poucos minutos e dá para pular o que quiser.",
			&acoes, &dicas, 0), NULL, G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &erro);
	if (r) {
		g_variant_unref(r);
	}
	g_clear_error(&erro);
	g_object_unref(c);
}

int main(int argc, char **argv) {
	setup_ler(&estado);
	bool login = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--login")) {
			login = true;
		} else if (!strcmp(argv[i], "--passo") && i + 1 < argc) {
			passo = atoi(argv[++i]) - 1;
			passo = passo < 0 ? 0 : passo >= N_PASSOS ? N_PASSOS - 1 : passo;
		}
	}
	if (login) {
		int o = setup_no_login(&estado);
		if (o == 2) {
			lembrete();
		}
		if (o != 1) {
			return 0;
		}
		estado.aberturas++;
		setup_gravar(&estado);
	}
	nao_mostrar_mais = estado.nao_mostrar;
	nfusos = instalador_fusos(&fusos);
	char *l = g_file_read_link("/etc/localtime", NULL);
	if (l && strstr(l, "zoneinfo/")) {
		g_strlcpy(fuso, strstr(l, "zoneinfo/") + 9, sizeof(fuso));
	}
	g_free(l);
	if (nfusos > 0 && !instalador_fuso_existe(fusos, nfusos, fuso)) {
		g_strlcpy(fuso, "America/Sao_Paulo", sizeof(fuso)); /* o do sistema não está na lista (UTC puro, por exemplo) */
	}
	struct idioma_cfg ic;
	idioma_ler(&ic);
	idioma = !strncmp(ic.lang, "en", 2) ? 1 : 0;
	if (estado.perfil[0] && setup_perfil_de_id(estado.perfil) >= 0) {
		perfil = setup_perfil_de_id(estado.perfil);
	}
	h24 = true;
	npresets = setup_emma_presets(p_ids, p_nomes + 1, p_urls, p_modelos, 8);
	p_nomes[0] = "Selecionar provedor";
	emma_on = emma_ativa();
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Configuração inicial", .app_id = "lumna-setup", .largura = 1240, .altura = 780 });
	if (!app) {
		return 1;
	}
	int fd = rede_iniciar();
	if (fd >= 0) {
		gv_app_vigiar(app, fd, ao_andar_rede, NULL);
	}
	rede_varrer("");
	int fs = som_iniciar("Configuração inicial");
	if (fs >= 0) {
		gv_app_vigiar(app, fs, ao_andar_som, NULL);
	}
	return gv_app_correr(app, desenhar, NULL);
}
