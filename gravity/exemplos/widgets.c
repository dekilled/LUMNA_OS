/*
 * Os widgets da área de trabalho do LUMNA (lumna-widgets): cartões numa camada atrás das janelas, no canto de cima à direita.
 *
 * Por ora há um: o do LUMNA Music. Quando o Music está tocando em segundo plano (a janela foi fechada, o app segue), ele vira este cartão: capa, título, artista, o tempo
 * e os botões anterior, tocar/pausar e próxima. Tocar no cartão (fora dos botões) abre o Music de novo, e o cartão some.
 *
 * É uma camada pequena: 1 px quando não há nada para mostrar, e só o cartão recebe o ponteiro; o resto da área de trabalho fica livre para as janelas e os cliques.
 * Quem sabe do que toca é o MPRIS (servicos/mpris.c); a janela do Music, o foreign-toplevel (gv_janelas).
 */
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gravity.h"
#include "mpris.h"

#define LARGURA 372
#define ALTURA 148
#define MARGEM_CIMA 16
#define MARGEM_DIREITA 24

static gv_app *app;
static struct mpris_estado musica;
static bool visivel;

static void musica_mudou(gv_app *a, int fd, void *dados) {
	mpris_ler(&musica);
	gv_app_redesenhar(a);
}

/* o Music tem uma janela aberta? (o foreign-toplevel do compositor) */
static bool music_com_janela(void) {
	const gv_janela *js[64];
	int n = gv_janelas(js, 64);
	for (int i = 0; i < n; i++) {
		if (js[i]->app_id && !strcmp(js[i]->app_id, "lumna-music")) {
			return true;
		}
	}
	return false;
}

static void abrir_music(void) {
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0) {
			execlp("lumna-music", "lumna-music", (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		waitpid(p, NULL, 0);
	}
}

static const char *tempo(int64_t us) {
	int s = (int)(us / 1000000);
	return gv_fmt("%d:%02d", s / 60, s % 60).chars;
}

static bool botao(Clay_ElementId id, const char *icone, float tamanho, bool cheio) {
	bool tocado = gv_tocado(id);
	float caixa = tamanho + (cheio ? 20 : 12);
	CLAY(id, { .layout = { .sizing = { CLAY_SIZING_FIXED(caixa), CLAY_SIZING_FIXED(caixa) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = cheio ? (gv_por_cima(id) ? gv_t.acento_cursor : gv_t.acento) : gv_por_cima(id) ? gv_alfa(gv_t.texto, 0.12f) : (Clay_Color){ 0 },
		.cornerRadius = CLAY_CORNER_RADIUS(caixa / 2) }) {
		tocado |= gv_focavel(id, caixa / 2);
		gv_icone(icone, tamanho, cheio ? gv_t.sobre_acento : gv_t.texto);
	}
	return tocado;
}

static void depurar(void) {
	static int ligado = -1;
	if (ligado < 0) {
		ligado = getenv("LUMNA_WIDGETS_DEBUG") != NULL;
	}
	if (!ligado) {
		return;
	}
	static char antes[1024];
	char l[1024];
	static const char *const nomes[] = { "w-cartao", "w-ant", "w-play", "w-prox" };
	char *p = l;
	p += snprintf(p, sizeof(l), "estado w_visivel=%d w_tem=%d w_tocando=%d w_titulo=[%s] w_lumna=%d", visivel, musica.tem, musica.tocando, musica.titulo, musica.lumna_music);
	for (size_t i = 0; i < G_N_ELEMENTS(nomes); i++) {
		Clay_BoundingBox b = gv_caixa(Clay__HashString((Clay_String){ .length = (int)strlen(nomes[i]), .chars = nomes[i] }, 0));
		p += snprintf(p, sizeof(l) - (size_t)(p - l), " %s=%.0f,%.0f,%.0f,%.0f", nomes[i], b.x, b.y, b.width, b.height);
	}
	static int extras;
	if (strcmp(l, antes)) {
		printf("%s\n", l);
		fflush(stdout);
		snprintf(antes, sizeof(antes), "%s", l);
		extras = 3;
	}
	if (extras > 0) {
		extras--;
		gv_app_redesenhar(app);
	}
}

static void desenhar(gv_app *a, void *dados) {
	bool quer = musica.tem && musica.lumna_music && !music_com_janela();
	if (quer != visivel) {
		visivel = quer;
		gv_app_camada(a, quer ? LARGURA : 1, quer ? ALTURA : 1, 0);
		gv_app_entrada_em(a, quer ? CLAY_ID("w-cartao") : CLAY_ID("w-vazio"));
	}
	gv_acordar_em(musica.lumna_music && musica.tocando ? 0.5 : 1.0);   /* a janela do Music pode abrir ou fechar sem o MPRIS mudar */
	if (!visivel) {
		CLAY(CLAY_ID("w-vazio"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}
		depurar();
		return;
	}
	CLAY(CLAY_ID("w-raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
		CLAY(CLAY_ID("w-cartao"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.elevada, 0.92f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
			bool abrir = gv_tocado(CLAY_ID("w-cartao"));   /* antes dos botões: o último candidato sob o ponteiro vence */
			Clay_ElementId capa = CLAY_ID("w-capa");
			if (musica.capa[0]) {
				CLAY(capa, gv_imagem(capa, musica.capa, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){ .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(96) } }, .cornerRadius = CLAY_CORNER_RADIUS(12) })) {}
			} else {
				CLAY(capa, { .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(96) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
					.backgroundColor = gv_alfa(gv_t.acento, 0.25f), .cornerRadius = CLAY_CORNER_RADIUS(12) }) {
					gv_icone("music", 40, gv_t.texto);
				}
			}
			CLAY(CLAY_ID("w-meio"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 6 } }) {
				CLAY(CLAY_ID("w-titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(musica.titulo[0] ? musica.titulo : "LUMNA Music"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
				}
				CLAY(CLAY_ID("w-artista"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } }, .clip = { .horizontal = true } }) {
					gv_texto(gv_str(musica.artista[0] ? musica.artista : "Em segundo plano"), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				int64_t pos = mpris_posicao(&musica);
				float frac = musica.duracao_us > 0 ? CLAMP((float)((double)pos / (double)musica.duracao_us), 0.0f, 1.0f) : 0.0f;
				CLAY(CLAY_ID("w-tempo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					gv_texto(gv_str(tempo(pos)), gv_t.tamanho_legenda, gv_t.texto_secundario, GV_PESO_NORMAL);
					CLAY(CLAY_ID("w-trilho"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(4) } }, .backgroundColor = gv_t.borda_controle, .cornerRadius = CLAY_CORNER_RADIUS(2) }) {
						CLAY(CLAY_ID("w-cheio"), { .layout = { .sizing = { CLAY_SIZING_PERCENT(frac), CLAY_SIZING_FIXED(4) } }, .backgroundColor = gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(2) }) {}
					}
					gv_texto(gv_str(musica.duracao_us > 0 ? tempo(musica.duracao_us) : "--:--"), gv_t.tamanho_legenda, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
				CLAY(CLAY_ID("w-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 8, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
					bool ant = botao(CLAY_ID("w-ant"), "skip-back", 20, false);
					bool play = botao(CLAY_ID("w-play"), musica.tocando ? "pause" : "play", 22, true);
					bool prox = botao(CLAY_ID("w-prox"), "skip-forward", 20, false);
					if (ant && musica.pode_anterior) {
						mpris_anterior();
					} else if (play) {
						mpris_tocar_pausar();
					} else if (prox && musica.pode_proxima) {
						mpris_proxima();
					} else if (abrir && !gv_por_cima(CLAY_ID("w-ant")) && !gv_por_cima(CLAY_ID("w-play")) && !gv_por_cima(CLAY_ID("w-prox"))) {
						abrir_music();
					}
				}
			}
		}
	}
	depurar();
}

int main(void) {
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Widgets", .app_id = "lumna-widgets", .tipo = GV_CAMADA, .nivel = GV_NIVEL_BAIXO, .ancoras = GV_ANCORA_CIMA | GV_ANCORA_DIREITA,
		.largura = 1, .altura = 1, .margens = { MARGEM_CIMA, MARGEM_DIREITA, 0, 0 }, .saida = "@principal" });
	if (!app) {
		return 1;
	}
	int fd = mpris_iniciar();
	if (fd >= 0) {
		gv_app_vigiar(app, fd, musica_mudou, NULL);
	}
	return gv_app_correr(app, desenhar, NULL);
}
