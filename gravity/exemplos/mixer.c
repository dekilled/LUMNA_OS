/*
 * O mixer de áudio do LUMNA (o MobileVolume.vue do shell), em Gravity, com o som de verdade
 * (servicos/som.c: o PipeWire pelo pipewire-pulse). Mostra a saída, o volume dela e o de cada
 * app tocando agora; o que mudar lá fora (as teclas, outra app) aparece aqui na hora.
 *
 *   ./mixer            numa janela
 *   ./mixer --camada   como no celular: uma camada por cima de tudo, com o véu atrás
 */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"
#include "som.h"

static struct som_estado som;
static bool lista;

/* Enquanto o dedo arrasta um deslizante, vale o valor do dedo (o do servidor chega atrasado). */
static struct {
	uint32_t id;
	float valor;
	double soltou;
} edita;

static int pct(float v) {
	return (int)lroundf(fminf(v, 1) * 100);
}

static void som_mudou(gv_app *app, int fd, void *dados) {
	som_ler(&som);
}

/* ---- nomes e ícones, como o shell ---- */

static bool tem(const char *texto, const char *palavras) {
	char copia[256];
	snprintf(copia, sizeof(copia), "%s", palavras);
	for (char *p = strtok(copia, "|"); p; p = strtok(NULL, "|")) {
		if (strcasestr(texto, p)) {
			return true;
		}
	}
	return false;
}

static const char *icone_saida(const struct som_no *s) {
	char tudo[300];
	snprintf(tudo, sizeof(tudo), "%s %s", s->nome, s->descricao);
	if (tem(tudo, "bluez|bluetooth|fone|headphone|headset|jbl|buds")) {
		return "headphones";
	}
	if (tem(tudo, "hdmi|displayport")) {
		return "desktop";
	}
	return "speaker";
}

static const char *nome_saida(const struct som_no *s) {
	char tudo[300];
	snprintf(tudo, sizeof(tudo), "%s %s", s->nome, s->descricao);
	return tem(tudo, "speaker|alto-falante|alto falante|built-in|built in|interno|analog") ? "Alto-falante" : s->descricao;
}

struct cara {
	const char *nome;
	const char *icone;
};

static struct cara cara_app(const struct som_no *a) {
	if (!strcasecmp(a->descricao, "emma") || tem(a->descricao, "pw-play|paplay")) {
		return (struct cara){ "Emma", "waves" };
	}
	if (tem(a->descricao, "lumna music")) {
		return (struct cara){ "Música", "music" };
	}
	if (tem(a->descricao, "firefox|chromium|chrome|epiphany|navegador|browser")) {
		return (struct cara){ a->descricao, "globe" };
	}
	if (tem(a->descricao, "mpv|vlc|video|vídeo")) {
		return (struct cara){ a->descricao, "play" };
	}
	return (struct cara){ a->descricao, gv_icone_existe(a->icone) ? a->icone : "music" };
}

/* um deslizante ligado a um volume do servidor */
static bool volume(Clay_ElementId id, float real, float *novo, float trilho, float puxador) {
	float v = edita.id == id.id ? edita.valor : fminf(real, 1);
	bool mudou = gv_deslizante(id, &v, trilho, puxador);
	if (mudou) {
		edita.id = id.id;
		edita.valor = v;
		edita.soltou = 0;
		*novo = v;
	}
	if (edita.id == id.id && !gv_premido(id)) {
		/* soltou: espera o servidor confirmar (ou desiste em meio segundo) */
		if (!edita.soltou) {
			edita.soltou = gv_agora();
		}
		if (fabsf(fminf(real, 1) - edita.valor) < 0.011f || gv_agora() - edita.soltou > 0.5) {
			edita.id = 0;
		} else {
			gv_app_redesenhar(NULL);
		}
	}
	return mudou;
}

/* ---- partes ---- */

static void cabecalho(gv_app *app) {
	CLAY(CLAY_ID("cabecalho"), {
		.layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 16, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
	}) {
		gv_icone("volume", 32, gv_t.icone);
		CLAY(CLAY_ID("titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(CLAY_STRING("Mixer de áudio"), gv_t.tamanho_titulo, gv_t.texto, GV_PESO_SEMINEGRITO);
		}
		Clay_ElementId fechar = CLAY_ID("fechar");
		bool sobre = gv_por_cima(fechar);
		CLAY(fechar, {
			.layout = { .sizing = { CLAY_SIZING_FIXED(40), CLAY_SIZING_FIXED(40) },
				.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_cor(0xffffffff), gv_anim(fechar.id, sobre ? 0.08f : 0, gv_t.duracao_rapida)),
			.cornerRadius = CLAY_CORNER_RADIUS(20),
		}) {
			gv_icone("close", 24, gv_t.icone);
		}
		if (gv_tocado(fechar)) {
			gv_app_sair(app);
		}
	}
}

static void saida(const struct som_no *atual) {
	CLAY(CLAY_ID("linha-saida"), {
		.layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .top = 22 }, .childGap = 12,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
	}) {
		CLAY(CLAY_ID("rotulo-saida"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(CLAY_STRING("Saída"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		Clay_ElementId chip = CLAY_ID("chip-saida");
		if (gv_tocado(chip) && som.nsaidas > 0) {
			lista = !lista;
		}
		CLAY(chip, {
			.layout = { .sizing = { .width = CLAY_SIZING_FIT(0, 240), .height = CLAY_SIZING_FIXED(50) },
				.padding = { 16, 16, 0, 0 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_cor(0xffffffff), gv_premido(chip) ? 0.1f : 0.04f),
			.cornerRadius = CLAY_CORNER_RADIUS(25),
			.border = { .color = gv_alfa(gv_cor(0xffffffff), 0.12f), .width = GV_BORDA(1) },
			.clip = { .horizontal = true },
		}) {
			gv_icone(atual ? icone_saida(atual) : "speaker", 20, gv_t.icone);
			gv_texto(gv_str(atual ? nome_saida(atual) : "Nenhuma"), 14, gv_t.texto, GV_PESO_NORMAL);
			gv_icone(lista ? "chevron-down" : "forward", 16, gv_t.icone);

			/* a lista das saídas, flutuando por baixo do chip */
			if (lista) {
				CLAY(CLAY_ID("lista-saidas"), {
					.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIT(230, 320) },
						.padding = CLAY_PADDING_ALL(6) },
					.backgroundColor = gv_cor(0x1c1828ff),
					.cornerRadius = CLAY_CORNER_RADIUS(14),
					.border = { .color = gv_alfa(gv_t.acento, 0.2f), .width = GV_BORDA(1) },
					.floating = { .attachTo = CLAY_ATTACH_TO_PARENT, .offset = { 0, 6 }, .zIndex = 10,
						.attachPoints = { .element = CLAY_ATTACH_POINT_RIGHT_TOP, .parent = CLAY_ATTACH_POINT_RIGHT_BOTTOM } },
					.userData = gv_sombra(24, 14, 0.5f),
				}) {
					for (int i = 0; i < som.nsaidas; i++) {
						const struct som_no *s = &som.saidas[i];
						Clay_ElementId item = CLAY_IDI("saida", i);
						if (gv_tocado(item)) {
							som_saida_padrao(s->nome);
							lista = false;
						}
						CLAY(item, {
							.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44) }, .padding = { 10, 10, 0, 0 },
								.childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
							.backgroundColor = s->padrao ? gv_alfa(gv_t.acento, 0.14f)
								: gv_alfa(gv_cor(0xffffffff), gv_por_cima(item) ? 0.05f : 0),
							.cornerRadius = CLAY_CORNER_RADIUS(10),
						}) {
							gv_icone(icone_saida(s), 18, gv_t.icone);
							CLAY(CLAY_IDI("saida-nome", i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
								gv_texto(gv_str(nome_saida(s)), 14, gv_t.texto, GV_PESO_NORMAL);
							}
							if (s->padrao) {
								gv_icone("check", 16, gv_t.acento);
							}
						}
					}
				}
			}
		}
	}
}

static void volume_global(const struct som_no *atual) {
	CLAY(CLAY_ID("global"), {
		.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
			.padding = { .top = 18, .bottom = 16 }, .childGap = 8 },
		.border = { .color = gv_alfa(gv_cor(0xffffffff), 0.08f), .width = { .bottom = 1 } },
	}) {
		gv_texto(CLAY_STRING("Volume global"), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		CLAY(CLAY_ID("global-linha"), {
			.layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		}) {
			Clay_ElementId id = CLAY_ID("global-volume");
			float novo;
			if (volume(id, atual ? atual->volume : 0, &novo, 12, 28) && atual) {
				som_volume_saida(atual->indice, novo);
				if (atual->mudo) {
					som_mudo_saida(atual->indice, false);
				}
			}
			float mostrado = edita.id == id.id ? edita.valor : atual ? atual->volume : 0;
			CLAY(CLAY_ID("global-pct"), {
				.layout = { .sizing = { CLAY_SIZING_FIXED(52) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } },
			}) {
				if (atual && atual->mudo) {
					gv_texto(CLAY_STRING("Mudo"), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
				} else {
					gv_texto(gv_fmt("%d%%", pct(mostrado)), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
				}
			}
		}
	}
}

static void apps(void) {
	if (som.napps == 0) {
		CLAY(CLAY_ID("nenhum"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .top = 16, .bottom = 6 } } }) {
			gv_texto(CLAY_STRING("Nenhum app tocando som agora. Quando tocar, ele aparece aqui com o volume dele."),
				14, gv_t.texto_fraco, GV_PESO_NORMAL);
		}
		return;
	}
	for (int i = 0; i < som.napps; i++) {
		const struct som_no *a = &som.apps[i];
		struct cara c = cara_app(a);
		CLAY(CLAY_IDI("app", i), {
			.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(62) }, .childGap = 12,
				.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.border = { .color = gv_alfa(gv_cor(0xffffffff), 0.06f), .width = { .bottom = 1 } },
		}) {
			CLAY(CLAY_IDI("app-icone", i), {
				.layout = { .sizing = { CLAY_SIZING_FIXED(34), CLAY_SIZING_FIXED(34) },
					.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_alfa(gv_t.acento, 0.12f),
				.cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
			}) {
				gv_icone(c.icone, 22, gv_t.icone);
			}
			CLAY(CLAY_IDI("app-nome", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(92) } }, .clip = { .horizontal = true } }) {
				gv_texto(gv_str(c.nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
			if (gv_botao_redondo(CLAY_IDI("app-mudo", i), a->mudo ? "mute" : "volume", 44, a->mudo, true)) {
				som_mudo_app(a->indice, !a->mudo);
			}
			Clay_ElementId id = CLAY_IDI("app-volume", i);
			float novo;
			if (volume(id, a->mudo ? 0 : a->volume, &novo, 10, 24)) {
				som_volume_app(a->indice, novo);
				if (a->mudo) {
					som_mudo_app(a->indice, false);
				}
			}
			float mostrado = edita.id == id.id ? edita.valor : a->mudo ? 0 : a->volume;
			CLAY(CLAY_IDI("app-pct", i), {
				.layout = { .sizing = { CLAY_SIZING_FIXED(44) }, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } },
			}) {
				gv_texto(gv_fmt("%d%%", pct(mostrado)), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			}
		}
	}
}

static void rodape(gv_app *app) {
	CLAY(CLAY_ID("rodape"), {
		.layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .top = 16 }, .childGap = 10,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
	}) {
		if (gv_botao(CLAY_ID("redefinir"), "refresh", "Redefinir volumes", GV_BOTAO_CONTORNO)) {
			for (int i = 0; i < som.napps; i++) {
				som_volume_app(som.apps[i].indice, 1);
				if (som.apps[i].mudo) {
					som_mudo_app(som.apps[i].indice, false);
				}
			}
		}
		gv_separador(true, 40);
		if (gv_botao(CLAY_ID("ajustes"), "settings", "Ajustes de áudio", GV_BOTAO_SIMPLES)) {
			if (fork() == 0) {
				execlp("lumna-ctl", "lumna-ctl", "settings", "som", (char *)NULL);
				_exit(1);
			}
			gv_app_sair(app);
		}
	}
}

static void desenhar(gv_app *app, void *dados) {
	(void)dados;
	if (gv_tecla() == XKB_KEY_Escape) {
		gv_app_sair(app);
	}
	const struct som_no *atual = som_saida_atual(&som);
	/* a entrada do painel: cresce de 97% e aparece (o "pop" do shell) */
	float entrada = gv_anim(1, 1, gv_t.duracao_normal);
	Clay_ElementId veu = CLAY_ID("veu");
	Clay_ElementId cartao = CLAY_ID("cartao");
	if (gv_tocado(veu) && !gv_por_cima(cartao)) {
		if (lista) {
			lista = false;
		} else {
			gv_app_sair(app);
		}
	}
	CLAY(veu, {
		.layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 20, 20, 20, 20 },
			.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_alfa(gv_t.fundo, gv_t.fundo.a / 255.0f * entrada),
	}) {
		Clay_ElementDeclaration c = gv_cartao();
		c.layout.sizing = (Clay_Sizing){ CLAY_SIZING_GROW(0, 420 * (0.97f + 0.03f * entrada)) };
		c.layout.padding = (Clay_Padding){ 20, 20, 22, 16 };
		c.backgroundColor = gv_alfa(c.backgroundColor, c.backgroundColor.a / 255.0f * entrada);
		CLAY(cartao, c) {
			cabecalho(app);
			if (!som.ligado) {
				CLAY(CLAY_ID("sem-som"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { .top = 22, .bottom = 8 } } }) {
					gv_texto(CLAY_STRING("Procurando o servidor de som…"), 14, gv_t.texto_fraco, GV_PESO_NORMAL);
				}
			} else {
				saida(atual);
				volume_global(atual);
				apps();
				rodape(app);
			}
		}
	}
}

int main(int argc, char **argv) {
	bool camada = argc > 1 && strcmp(argv[1], "--camada") == 0;
	gv_app *app = gv_app_criar(&(gv_opcoes){
		.titulo = "Mixer de áudio",
		.app_id = "lumna-mixer",
		.tipo = camada ? GV_CAMADA : GV_JANELA,
		.largura = camada ? 0 : 460,
		.altura = camada ? 0 : 660,
		.nivel = GV_NIVEL_OVERLAY,
		.ancoras = GV_ANCORA_TUDO,
		.teclado = true,
	});
	if (!app) {
		return 1;
	}
	int aviso = som_iniciar("Mixer de áudio");
	if (aviso >= 0) {
		gv_app_vigiar(app, aviso, som_mudou, NULL);
	}
	int codigo = gv_app_correr(app, desenhar, NULL);
	som_parar();
	return codigo;
}
