/*
 * lumna-midia: a Mídia do LUMNA. Um programa, vários papéis:
 *   lumna-midia servico        o serviço (dono do Firefox, do catálogo e dos recentes); sobe com a sessão
 *   lumna-midia inicio         a tela de Início: serviços e recentes
 *   lumna-midia capa ID        a capa de loading sobre o serviço que está abrindo (some com fade)
 *   lumna-midia seletor        o seletor de serviços (Super+M) sobre o que estiver passando
 *   lumna-midia <pedido>       qualquer pedido do serviço pela linha de comando (midiac.h)
 *
 * As telas só desenham o estado que o serviço manda e pedem coisas a ele: quem abre o Firefox, traz
 * a janela para a frente e tira a capa é o serviço. Teclado (setas, Enter, Esc) e toque funcionam;
 * o celular (lumna-controle) manda as mesmas teclas.
 */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"
#include "json.h"
#include "midiac.h"
#include "midiad.h"

/* ---- o que as telas dividem ---- */

/* tudo se mede em U(): 1 px numa TV de 1920 de largura, e encolhe ou cresce com a tela */
static float K = 1.0f;
#define U(n) ((float)(n) * K)

static struct midia_estado est;
static const char *papel;     /* "inicio", "capa" ou "seletor" */
static char capa_servico[32]; /* o serviço da capa, pelo argumento */

static void estado_rever(void) {
	if (!midiac_estado(&est)) {
		est.servico = false;
	}
}

static void ao_evento(gv_app *app, int fd, void *dados) {
	if (!midiac_assinar_vivo(fd) && !strcmp(papel, "capa")) {
		gv_app_sair(app);
		return;
	}
	estado_rever();
	gv_app_redesenhar(app);
}

static void escala_rever(gv_app *app) {
	float k = gv_app_largura(app) / 1920.0f;
	K = k < 0.5f ? 0.5f : k > 1.8f ? 1.8f : k;
}

static Clay_Color cor_hex(const char *hex, float alfa) {
	unsigned v = 0x8B7CF6;
	if (hex && hex[0] == '#') {
		v = (unsigned)strtoul(hex + 1, NULL, 16);
	}
	return (Clay_Color){ (float)((v >> 16) & 255), (float)((v >> 8) & 255), (float)(v & 255), 255 * alfa };
}

static bool tecla(uint32_t sym) {
	return gv_tecla_carregada(sym);
}

static const char *ha_quanto_tempo(gint64 quando) {
	gint64 d = g_get_real_time() / G_USEC_PER_SEC - quando;
	if (!quando || d < 0) {
		return "";
	}
	if (d < 90) {
		return "agora há pouco";
	}
	if (d < 3600) {
		return gv_fmt("há %d min", (int)(d / 60)).chars;
	}
	if (d < 86400) {
		return gv_fmt("há %d h", (int)(d / 3600)).chars;
	}
	if (d < 2 * 86400) {
		return "ontem";
	}
	return gv_fmt("há %d dias", (int)(d / 86400)).chars;
}

/* a "marca" de um serviço: o ícone do usuário/sistema, ou o nome na cor da marca */
static void marca(const struct midia_servico *s, float tamanho, float alfa) {
	char *icone = midia_icone(s->id);
	if (icone) {
		CLAY_AUTO_ID(gv_imagem(CLAY_ID("marca"), icone, GV_AJUSTE_CONTER, (Clay_ElementDeclaration){
			.layout = { .sizing = { CLAY_SIZING_FIXED(tamanho * 4), CLAY_SIZING_FIXED(tamanho * 1.6f) } } })) {}
		g_free(icone);
		return;
	}
	gv_texto(gv_str(s->nome), tamanho, cor_hex(s->cor, alfa), GV_PESO_NEGRITO);
}

/* ---- o Início ---- */

enum { LINHA_CONTROLE, LINHA_SERVICOS, LINHA_RECENTES };
static int foco_linha = LINHA_SERVICOS, foco_col;
static bool modal_pareamento, modal_adicionar;
static char adic_nome[48], adic_url[300], adic_erro[80];
static char pareamento_json[512];
static double pareamento_em;

static int servicos_ativos(int *indices) {
	int n = 0;
	for (int i = 0; i < est.n; i++) {
		if (est.serv[i].s.ativo) {
			indices[n++] = i;
		}
	}
	return n;
}

static void abrir_servico(const char *id) {
	char pedido[80], r[256];
	snprintf(pedido, sizeof(pedido), "abrir %s", id);
	midiac_pedido(pedido, r, sizeof(r));
}

/* o código de pareamento vem do lumna-controle */
static void pareamento_pedir(void) {
	pareamento_json[0] = '\0';
	pareamento_em = gv_agora();
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	snprintf(a.sun_path, sizeof(a.sun_path), "%s/lumna-controle.sock", g_get_user_runtime_dir());
	if (fd >= 0 && connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
		if (write(fd, "parear\n", 7) == 7) {
			ssize_t n = read(fd, pareamento_json, sizeof(pareamento_json) - 1);
			pareamento_json[n > 0 ? n : 0] = '\0';
		}
	}
	if (fd >= 0) {
		close(fd);
	}
}

static void chip_tecla(const char *teclas) {
	CLAY_AUTO_ID({ .layout = { .padding = { U(10), U(10), U(4), U(4) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.08f), .cornerRadius = CLAY_CORNER_RADIUS(U(6)),
		.border = { .color = gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.12f), .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(teclas), U(15), gv_t.texto, GV_PESO_MEDIO);
	}
}

static void dica(const char *teclas, const char *faz) {
	CLAY_AUTO_ID({ .layout = { .childGap = U(10), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		chip_tecla(teclas);
		gv_texto(gv_str(faz), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void barra_de_cima(void) {
	CLAY(CLAY_ID("topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(56)) }, .padding = { U(40), U(40), 0, 0 }, .childGap = U(14),
		.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("lumna", U(24), gv_t.acento);
		gv_texto(CLAY_STRING("LUMNA"), U(20), gv_t.texto, GV_PESO_SEMINEGRITO);
		gv_separador(true, U(22));
		gv_texto(CLAY_STRING("Mídia"), U(19), gv_t.texto_secundario, GV_PESO_NORMAL);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		gv_icone("volume", U(22), gv_t.texto_secundario);
		gv_icone("wifi", U(22), gv_t.texto_secundario);
		time_t agora = time(NULL);
		struct tm t;
		localtime_r(&agora, &t);
		gv_texto(gv_fmt("%02d:%02d", t.tm_hour, t.tm_min), U(20), gv_t.texto, GV_PESO_MEDIO);
		gv_icone("power", U(22), gv_t.texto_secundario);
	}
}

static void cartao_servico(int idx, int col, bool foco_aqui) {
	const struct midia_vivo *v = &est.serv[idx];
	Clay_ElementId id = CLAY_IDI("serv", (uint32_t)idx);
	bool em_foco = foco_aqui && foco_linha == LINHA_SERVICOS && foco_col == col;
	if (gv_tocado(id)) {
		abrir_servico(v->s.id);
	}
	if (gv_por_cima(id) && gv_ponteiro().x >= 0) {
		foco_linha = LINHA_SERVICOS;
		foco_col = col;
	}
	float cresce = gv_anim(id.id, em_foco ? 1.0f : 0.0f, 140);
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(212 + 8 * cresce)), CLAY_SIZING_FIXED(U(112 + 6 * cresce)) },
			.childGap = U(6), .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = em_foco ? gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.10f) : gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.05f),
		.cornerRadius = CLAY_CORNER_RADIUS(U(14)),
		.border = { .color = em_foco ? gv_t.acento : gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.10f), .width = GV_BORDA(em_foco ? 3 : 1) } }) {
		marca(&v->s, U(30), 1.0f);
		if (v->aberto) {
			CLAY_AUTO_ID({ .layout = { .padding = { U(8), U(8), U(2), U(2) } }, .backgroundColor = gv_alfa(gv_t.sucesso, 0.18f), .cornerRadius = CLAY_CORNER_RADIUS(U(8)) }) {
				gv_texto(CLAY_STRING("aberto"), U(13), gv_t.sucesso, GV_PESO_MEDIO);
			}
		}
	}
}

static void cartao_adicionar(int col) {
	Clay_ElementId id = CLAY_ID("adicionar");
	bool em_foco = foco_linha == LINHA_SERVICOS && foco_col == col;
	if (gv_tocado(id)) {
		modal_adicionar = true;
		adic_nome[0] = adic_url[0] = adic_erro[0] = '\0';
	}
	if (gv_por_cima(id) && gv_ponteiro().x >= 0) {
		foco_linha = LINHA_SERVICOS;
		foco_col = col;
	}
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(212)), CLAY_SIZING_FIXED(U(112)) }, .childGap = U(6),
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = em_foco ? gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.10f) : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(U(14)),
		.border = { .color = em_foco ? gv_t.acento : gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.16f), .width = GV_BORDA(em_foco ? 3 : 1) } }) {
		gv_icone("plus", U(30), gv_t.texto_secundario);
		gv_texto(CLAY_STRING("Adicionar serviço"), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void cartao_recente(int idx, bool primeiro) {
	const struct midia_recente *r = &est.rec[idx];
	const struct midia_vivo *v = midiac_achar(&est, r->servico);
	Clay_ElementId id = CLAY_IDI("rec", (uint32_t)idx);
	bool em_foco = foco_linha == LINHA_RECENTES && foco_col == idx;
	if (gv_tocado(id)) {
		abrir_servico(r->servico);
	}
	if (gv_por_cima(id) && gv_ponteiro().x >= 0) {
		foco_linha = LINHA_RECENTES;
		foco_col = idx;
	}
	CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(500)), CLAY_SIZING_FIT(U(0)) }, .childGap = U(12) } }) {
		Clay_ElementId capa = CLAY_IDI("rec-capa", (uint32_t)idx);
		Clay_ElementDeclaration d = {
			.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(260)) } },
			.backgroundColor = v ? cor_hex(v->s.cor, 0.22f) : gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.06f),
			.cornerRadius = CLAY_CORNER_RADIUS(U(14)),
			.border = { .color = em_foco ? gv_t.acento : gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.10f), .width = GV_BORDA(em_foco ? 3 : 1) },
			.clip = { .horizontal = true, .vertical = true },
		};
		if (r->miniatura[0]) {
			CLAY(capa, gv_imagem(capa, r->miniatura, GV_AJUSTE_COBRIR, d)) {}
		} else {
			CLAY(capa, d) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
					if (v) {
						marca(&v->s, U(34), 0.9f);
					}
				}
			}
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(14), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(46)), CLAY_SIZING_FIXED(U(46)) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = v ? cor_hex(v->s.cor, 0.25f) : gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.08f), .cornerRadius = CLAY_CORNER_RADIUS(U(10)) }) {
				gv_texto(gv_fmt("%.1s", v ? v->s.nome : "?"), 24, v ? cor_hex(v->s.cor, 1) : gv_t.texto, GV_PESO_NEGRITO);
			}
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(2) } }) {
				gv_texto(gv_str(r->titulo[0] ? r->titulo : (v ? v->s.nome : r->servico)), U(20), gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_fmt("%s%s%s", v && v->aberto ? "Aberto agora" : "Última página", ha_quanto_tempo(r->quando)[0] ? " · " : "", ha_quanto_tempo(r->quando)),
					U(16), gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (primeiro || em_foco) {
				CLAY_AUTO_ID({ .layout = { .padding = { U(22), U(22), U(11), U(11) } }, .backgroundColor = em_foco ? gv_t.acento : gv_alfa(gv_t.acento, 0.55f),
					.cornerRadius = CLAY_CORNER_RADIUS(U(10)) }) {
					gv_texto(CLAY_STRING("Retomar"), U(18), gv_t.sobre_acento, GV_PESO_MEDIO);
				}
			}
		}
	}
}

static void modal_pareamento_desenhar(gv_app *app) {
	CLAY(CLAY_ID("veu"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP }, .zIndex = 50,
			.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE },
		.backgroundColor = { 6, 5, 16, 215 } }) {
		CLAY(CLAY_ID("cartao-pareamento"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(620)), CLAY_SIZING_FIT(U(0)) },
				.padding = CLAY_PADDING_ALL(U(40)), .childGap = U(18), .childAlignment = { .x = CLAY_ALIGN_X_CENTER } },
			.backgroundColor = { 22, 20, 44, 255 }, .cornerRadius = CLAY_CORNER_RADIUS(U(22)), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_icone("phone", U(40), gv_t.acento);
			gv_texto(CLAY_STRING("Controle pelo celular"), U(30), gv_t.texto, GV_PESO_SEMINEGRITO);
			struct json *j = json_ler(pareamento_json);
			const char *codigo = json_chave_texto(j, "codigo");
			if (*codigo) {
				gv_texto(gv_fmt("%.3s %s", codigo, codigo + 3), U(64), gv_t.acento, GV_PESO_NEGRITO);
				gv_texto(CLAY_STRING("Abra o app LUMNA Controle e digite este código."), U(20), gv_t.texto_secundario, GV_PESO_NORMAL);
				const struct json *ends = json_chave(j, "enderecos");
				GString *lista = g_string_new(NULL);
				for (int i = 0; ends && i < ends->nfilhos; i++) {
					g_string_append_printf(lista, "%s%s", i ? " · " : "", json_texto(ends->filhos[i]));
				}
				if (lista->len) {
					gv_texto(gv_fmt("Se o celular não achar sozinho: %s, porta %d", lista->str, (int)json_numero(json_chave(j, "porta"), 47800)), 17,
						gv_t.texto_suave, GV_PESO_NORMAL);
				}
				g_string_free(lista, TRUE);
				int resta = (int)json_numero(json_chave(j, "expira"), 300) - (int)(gv_agora() - pareamento_em);
				gv_texto(resta > 0 ? gv_fmt("Vale por mais %d:%02d", resta / 60, resta % 60) : CLAY_STRING("O código venceu: feche e abra de novo"), U(17), gv_t.texto_suave,
					GV_PESO_NORMAL);
				gv_acordar_em(1);
			} else {
				gv_texto(CLAY_STRING("O serviço do controle (lumna-controle) não está rodando."), U(20), gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			json_liberar(j);
			if (gv_botao(CLAY_ID("pareamento-fechar"), NULL, "Fechar", GV_BOTAO_ELEVADO) || tecla(XKB_KEY_Escape) || tecla(XKB_KEY_Return)) {
				modal_pareamento = false;
			}
		}
	}
}

static void modal_adicionar_desenhar(gv_app *app) {
	CLAY(CLAY_ID("veu2"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .attachPoints = { CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP }, .zIndex = 50,
			.pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE },
		.backgroundColor = { 6, 5, 16, 215 } }) {
		CLAY(CLAY_ID("cartao-adicionar"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(U(640)), CLAY_SIZING_FIT(U(0)) },
				.padding = CLAY_PADDING_ALL(U(36)), .childGap = U(16) },
			.backgroundColor = { 22, 20, 44, 255 }, .cornerRadius = CLAY_CORNER_RADIUS(U(22)), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Adicionar serviço"), U(28), gv_t.texto, GV_PESO_SEMINEGRITO);
			gv_texto(CLAY_STRING("O nome que aparece e o endereço do site. Dá para digitar pelo celular (LUMNA Controle)."), U(17), gv_t.texto_secundario, GV_PESO_NORMAL);
			static bool focou;
			if (!focou) {
				gv_focar(CLAY_ID("adic-nome"));
				focou = true;
			}
			gv_campo(CLAY_ID("adic-nome"), adic_nome, sizeof(adic_nome), "Nome (ex.: Crunchyroll)", GV_CAMPO_TEXTO);
			int c = gv_campo(CLAY_ID("adic-url"), adic_url, sizeof(adic_url), "Endereço (https://...)", GV_CAMPO_URL);
			if (adic_erro[0]) {
				gv_texto(gv_str(adic_erro), U(16), gv_t.perigo, GV_PESO_NORMAL);
			}
			bool ok = false;
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(12), .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
				if (gv_botao(CLAY_ID("adic-cancelar"), NULL, "Cancelar", GV_BOTAO_ELEVADO)) {
					modal_adicionar = false;
					focou = false;
				}
				ok = gv_botao(CLAY_ID("adic-ok"), "plus", "Adicionar", GV_BOTAO_CHEIO) || (c & GV_CAMPO_ENTER);
			}
			if (ok) {
				char pedido[512], r[256];
				snprintf(pedido, sizeof(pedido), "servico_adicionar %s %s", adic_url, adic_nome);
				if (adic_nome[0] && adic_url[0] && midiac_pedido(pedido, r, sizeof(r)) && strstr(r, "\"ok\":true")) {
					modal_adicionar = false;
					focou = false;
					estado_rever();
				} else {
					snprintf(adic_erro, sizeof(adic_erro), "Confira o nome e o endereço (começa com https://).");
				}
			}
			if (tecla(XKB_KEY_Escape)) {
				modal_adicionar = false;
				focou = false;
			}
		}
	}
}

static void inicio_navegar(int nserv, int nrec) {
	if (modal_pareamento || modal_adicionar) {
		return;
	}
	int cols = nserv + 1; /* + o "adicionar" */
	if (tecla(XKB_KEY_Left)) {
		foco_col = foco_col > 0 ? foco_col - 1 : foco_col;
	}
	if (tecla(XKB_KEY_Right)) {
		int max = foco_linha == LINHA_SERVICOS ? cols - 1 : foco_linha == LINHA_RECENTES ? nrec - 1 : 0;
		foco_col = foco_col < max ? foco_col + 1 : foco_col;
	}
	if (tecla(XKB_KEY_Down)) {
		if (foco_linha == LINHA_CONTROLE) {
			foco_linha = LINHA_SERVICOS;
			foco_col = 0;
		} else if (foco_linha == LINHA_SERVICOS && nrec) {
			foco_linha = LINHA_RECENTES;
			foco_col = MIN(foco_col, nrec - 1);
		}
	}
	if (tecla(XKB_KEY_Up)) {
		if (foco_linha == LINHA_RECENTES) {
			foco_linha = LINHA_SERVICOS;
		} else if (foco_linha == LINHA_SERVICOS) {
			foco_linha = LINHA_CONTROLE;
		}
	}
	if (tecla(XKB_KEY_Return) || tecla(XKB_KEY_KP_Enter)) {
		int idx[MIDIA_MAX_SERVICOS];
		int n = servicos_ativos(idx);
		if (foco_linha == LINHA_CONTROLE) {
			modal_pareamento = true;
			pareamento_pedir();
		} else if (foco_linha == LINHA_SERVICOS) {
			if (foco_col < n) {
				abrir_servico(est.serv[idx[foco_col]].s.id);
			} else {
				modal_adicionar = true;
				adic_nome[0] = adic_url[0] = adic_erro[0] = '\0';
			}
		} else if (foco_col < nrec) {
			abrir_servico(est.rec[foco_col].servico);
		}
	}
}

static void desenhar_inicio(gv_app *app, void *dados) {
	escala_rever(app);
	int idx[MIDIA_MAX_SERVICOS];
	int n = servicos_ativos(idx);
	/* só os recentes de serviços que ainda estão no catálogo */
	int rec[MIDIA_MAX_RECENTES], nrec = 0;
	for (int i = 0; i < est.nrec && nrec < 3; i++) {
		const struct midia_vivo *v = midiac_achar(&est, est.rec[i].servico);
		if (v && v->s.ativo) {
			rec[nrec++] = i;
		}
	}
	inicio_navegar(n, nrec);
	CLAY(CLAY_ID("raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = { 14, 12, 31, 255 } }) {
		barra_de_cima();
		CLAY(CLAY_ID("corpo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) },
			.padding = { U(56), U(56), U(12), U(12) }, .childGap = U(26) } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childAlignment = { .y = CLAY_ALIGN_Y_TOP } } }) {
				CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(4) } }) {
					gv_texto(CLAY_STRING("Início"), U(52), gv_t.texto, GV_PESO_NEGRITO);
					gv_texto(CLAY_STRING("Seus serviços e últimas páginas, em um só lugar."), U(22), gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				Clay_ElementId bt = CLAY_ID("controle-celular");
				bool em_foco = foco_linha == LINHA_CONTROLE;
				if (gv_tocado(bt)) {
					modal_pareamento = true;
					pareamento_pedir();
				}
				CLAY(bt, { .layout = { .padding = { U(24), U(24), U(16), U(16) }, .childGap = U(12), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = gv_alfa((Clay_Color){ 255, 255, 255, 255 }, em_foco || gv_por_cima(bt) ? 0.10f : 0.05f), .cornerRadius = CLAY_CORNER_RADIUS(U(14)),
					.border = { .color = em_foco ? gv_t.acento : gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.10f), .width = GV_BORDA(em_foco ? 3 : 1) } }) {
					gv_icone("phone", U(26), gv_t.texto);
					gv_texto(CLAY_STRING("Controle pelo celular"), U(21), gv_t.texto, GV_PESO_MEDIO);
				}
			}
			gv_texto(CLAY_STRING("Serviços"), U(28), gv_t.texto, GV_PESO_SEMINEGRITO);
			CLAY(CLAY_ID("linha-servicos"), gv_rolagem(CLAY_ID("linha-servicos"), GV_ROLAGEM_HORIZONTAL, (Clay_ElementDeclaration){
				.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(U(130)) }, .childGap = U(16), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .padding = { U(4), U(4), U(4), U(4) } } })) {
				for (int i = 0; i < n; i++) {
					cartao_servico(idx[i], i, true);
				}
				cartao_adicionar(n);
			}
			if (nrec) {
				gv_texto(CLAY_STRING("Recentes"), U(28), gv_t.texto, GV_PESO_SEMINEGRITO);
				CLAY(CLAY_ID("linha-recentes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(28) } }) {
					for (int i = 0; i < nrec; i++) {
						cartao_recente(rec[i], i == 0);
					}
				}
			} else {
				gv_texto(CLAY_STRING("Abra um serviço: o que você assistir aparece aqui para retomar de onde parou."), U(20), gv_t.texto_suave, GV_PESO_NORMAL);
			}
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}
			CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(28), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, .padding = { 0, 0, 0, U(18) } } }) {
				dica("← → ↑ ↓", "Navegar");
				dica("Enter", "Abrir");
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
				dica("Super + M", "Trocar serviço");
			}
		}
		if (!est.servico) {
			CLAY(CLAY_ID("sem-servico"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .zIndex = 40 }, .backgroundColor = { 14, 12, 31, 235 } }) {
				gv_texto(CLAY_STRING("O serviço da Mídia (lumna-midia servico) não está rodando."), U(24), gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		if (modal_pareamento) {
			modal_pareamento_desenhar(app);
		}
		if (modal_adicionar) {
			modal_adicionar_desenhar(app);
		}
	}
}

/* ---- a capa de loading ---- */

static double capa_saindo_desde = -1;

static void desenhar_capa(gv_app *app, void *dados) {
	escala_rever(app);
	const struct midia_vivo *v = midiac_achar(&est, capa_servico);
	bool sai = est.servico && !est.capa_visivel;
	if (sai && capa_saindo_desde < 0) {
		capa_saindo_desde = gv_agora();
	}
	if (!est.capa_visivel && est.servico && strcmp(est.capa, capa_servico)) {
		sai = true; /* outra capa tomou o lugar */
	}
	/* a opacidade: 1 enquanto carrega, 0 em 400 ms depois que o serviço manda sair */
	float alfa = 1.0f;
	if (capa_saindo_desde >= 0) {
		alfa = 1.0f - (float)((gv_agora() - capa_saindo_desde) / 0.4);
		if (alfa <= 0) {
			gv_app_sair(app);
			return;
		}
		gv_acordar_em(0.016);
	}
	CLAY(CLAY_ID("raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = U(36),
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = (Clay_Color){ 10, 9, 24, 255 * alfa } }) {
		if (v) {
			marca(&v->s, U(64), alfa);
		} else {
			gv_texto(gv_str(capa_servico), U(64), gv_alfa(gv_t.texto, alfa), GV_PESO_NEGRITO);
		}
		/* três pontos que respiram */
		CLAY_AUTO_ID({ .layout = { .childGap = U(16) } }) {
			double t = gv_agora();
			for (int i = 0; i < 3; i++) {
				float pulso = 0.35f + 0.65f * (0.5f + 0.5f * (float)sin(t * 4.0 - i * 0.9));
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(U(14)), CLAY_SIZING_FIXED(U(14)) } },
					.backgroundColor = gv_alfa(gv_t.acento, pulso * alfa), .cornerRadius = CLAY_CORNER_RADIUS(U(7)) }) {}
			}
		}
		gv_texto(gv_fmt("Abrindo %s", v ? v->s.nome : capa_servico), U(22), gv_alfa(gv_t.texto_secundario, alfa), GV_PESO_NORMAL);
		gv_acordar_em(0.03);
	}
}

/* ---- o seletor de serviços ---- */

static int sel_col;

static void desenhar_seletor(gv_app *app, void *dados) {
	escala_rever(app);
	int idx[MIDIA_MAX_SERVICOS];
	int n = servicos_ativos(idx);
	int itens = n + 1; /* o Início e os serviços */
	if (tecla(XKB_KEY_Escape)) {
		gv_app_sair(app);
		return;
	}
	if (tecla(XKB_KEY_Left) && sel_col > 0) {
		sel_col--;
	}
	if (tecla(XKB_KEY_Right) && sel_col < itens - 1) {
		sel_col++;
	}
	bool aciona = tecla(XKB_KEY_Return) || tecla(XKB_KEY_KP_Enter);
	CLAY(CLAY_ID("raiz"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { U(40), U(40), U(28), U(22) }, .childGap = U(18) },
		.backgroundColor = { 16, 14, 34, 246 }, .cornerRadius = { .topLeft = 22, .topRight = 22 }, .border = { .color = gv_t.borda, .width = { .top = 1 } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(est.ativo[0] ? gv_fmt("Reproduzindo no %s", midiac_achar(&est, est.ativo) ? midiac_achar(&est, est.ativo)->s.nome : est.ativo) : CLAY_STRING("Trocar de serviço"), 20,
				gv_t.texto_secundario, GV_PESO_NORMAL);
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
			gv_texto(CLAY_STRING("Esc · Voltar ao vídeo"), U(17), gv_t.texto_suave, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("itens"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(U(0)) }, .childGap = U(16) } }) {
			for (int i = 0; i < itens; i++) {
				bool inicio = i == 0;
				const struct midia_vivo *v = inicio ? NULL : &est.serv[idx[i - 1]];
				Clay_ElementId id = CLAY_IDI("sel", (uint32_t)i);
				bool em_foco = i == sel_col;
				bool em_uso = v && !strcmp(est.ativo, v->s.id);
				if (gv_por_cima(id) && gv_ponteiro().x >= 0) {
					sel_col = i;
				}
				bool toque = gv_tocado(id);
				if (toque || (aciona && em_foco)) {
					if (inicio) {
						char r[256];
						midiac_pedido("inicio", r, sizeof(r));
					} else {
						abrir_servico(v->s.id);
					}
					gv_app_sair(app);
					return;
				}
				CLAY(CLAY_IDI("sel-col", (uint32_t)i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(8),
						.childAlignment = { .x = CLAY_ALIGN_X_CENTER } } }) {
					CLAY(id, { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(U(104)) }, .childGap = U(6),
							.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
						.backgroundColor = gv_alfa((Clay_Color){ 255, 255, 255, 255 }, em_foco ? 0.12f : 0.05f), .cornerRadius = CLAY_CORNER_RADIUS(U(14)),
						.border = { .color = em_foco ? gv_t.acento : gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.10f), .width = GV_BORDA(em_foco ? 3 : 1) } }) {
						if (inicio) {
							CLAY_AUTO_ID({ .layout = { .childGap = U(10), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
								gv_icone("home", U(30), gv_t.texto);
								gv_texto(CLAY_STRING("Início"), U(24), gv_t.texto, GV_PESO_MEDIO);
							}
						} else {
							marca(&v->s, U(26), 1.0f);
							if (em_uso) {
								CLAY_AUTO_ID({ .layout = { .padding = { U(10), U(10), U(2), U(2) } }, .backgroundColor = gv_alfa((Clay_Color){ 255, 255, 255, 255 }, 0.10f),
									.cornerRadius = CLAY_CORNER_RADIUS(U(8)) }) {
									gv_texto(CLAY_STRING("Em uso"), U(14), gv_t.texto_secundario, GV_PESO_NORMAL);
								}
							}
						}
					}
					if (em_foco && v) {
						const struct midia_recente *r = NULL;
						for (int k = 0; k < est.nrec; k++) {
							if (!strcmp(est.rec[k].servico, v->s.id)) {
								r = &est.rec[k];
							}
						}
						gv_texto(CLAY_STRING(""), U(1), gv_t.texto, GV_PESO_NORMAL);
						gv_texto(em_uso ? CLAY_STRING("Voltar ao vídeo") : v->aberto ? CLAY_STRING("Voltar de onde parou") : r ? CLAY_STRING("Retomar última página") : CLAY_STRING("Abrir"),
							U(17), gv_t.acento, GV_PESO_NORMAL);
					}
				}
			}
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(24), .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = U(24), .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
				dica("← →", "Escolher");
				dica("Enter", "Trocar");
				dica("Esc", "Fechar");
			}
		}
	}
}

/* uma só tela de cada vez (o serviço abre o Início ao entrar na Mídia; quem pedir de novo só a traz para a frente) */
static bool primeira_instancia(const char *nome) {
	char *c = g_strdup_printf("%s/lumna-midia-%s.lock", g_get_user_runtime_dir(), nome);
	int fd = open(c, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	g_free(c);
	return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0; /* o descritor fica aberto: o trinco vale até a tela sair */
}

/* ---- main ---- */

static int pedido_cli(int argc, char **argv) {
	GString *p = g_string_new(NULL);
	for (int i = 1; i < argc; i++) {
		g_string_append_printf(p, "%s%s", i > 1 ? " " : "", argv[i]);
	}
	char r[65536];
	bool ok = midiac_pedido(p->str, r, sizeof(r));
	g_string_free(p, TRUE);
	if (!ok) {
		fprintf(stderr, "lumna-midia: o serviço não está rodando\n");
		return 1;
	}
	printf("%s", r);
	return 0;
}

int main(int argc, char **argv) {
	char exe[512];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	exe[n > 0 ? n : 0] = '\0';
	if (argc < 2) {
		fprintf(stderr, "uso: lumna-midia servico | inicio | capa ID | seletor | <pedido>\n");
		return 1;
	}
	papel = argv[1];
	if (!strcmp(papel, "servico")) {
		return midiad_correr(n > 0 ? exe : "lumna-midia");
	}
	gv_opcoes o = { 0 };
	gv_desenho desenho = NULL;
	if ((!strcmp(papel, "inicio") || !strcmp(papel, "seletor")) && !primeira_instancia(papel)) {
		return 0;
	}
	if (!strcmp(papel, "inicio")) {
		o = (gv_opcoes){ .titulo = "Mídia", .app_id = MIDIA_APP_INICIO, .largura = 1280, .altura = 720 };
		desenho = desenhar_inicio;
	} else if (!strcmp(papel, "capa") && argc > 2) {
		g_strlcpy(capa_servico, argv[2], sizeof(capa_servico));
		o = (gv_opcoes){ .titulo = "Abrindo", .app_id = "lumna-midia-capa", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY, .ancoras = GV_ANCORA_TUDO,
			.zona_exclusiva = -1 };
		desenho = desenhar_capa;
	} else if (!strcmp(papel, "seletor")) {
		o = (gv_opcoes){ .titulo = "Serviços", .app_id = "lumna-midia-seletor", .tipo = GV_CAMADA, .nivel = GV_NIVEL_OVERLAY,
			.ancoras = GV_ANCORA_BAIXO | GV_ANCORA_ESQUERDA | GV_ANCORA_DIREITA, .altura = 250, .teclado = true, .teclado_exclusivo = true };
		desenho = desenhar_seletor;
	} else {
		return pedido_cli(argc, argv);
	}
	estado_rever();
	gv_app *app = gv_app_criar(&o);
	if (!app) {
		return 1;
	}
	int fd = midiac_assinar();
	if (fd >= 0) {
		gv_app_vigiar(app, fd, ao_evento, NULL);
	}
	return gv_app_correr(app, desenho, NULL);
}
