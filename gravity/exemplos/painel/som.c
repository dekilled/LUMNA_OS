/*
 * Painel > Som: a saída (alto-falante, fones, HDMI: qual é a padrão, o volume e o mudo de cada uma, e um teste), a entrada (os
 * microfones, com o nível ao vivo) e o volume de cada app que está tocando. Tudo pelo servidor de som (servicos/som.c: o PipeWire
 * pela interface do PulseAudio); o que mudar lá fora (as teclas, a topbar) aparece aqui na hora. A Emma fala pela saída padrão e
 * ouve pela entrada padrão: é aqui que se confere por que ela não é ouvida (a saída errada, o mudo, o volume no zero).
 */
#define _GNU_SOURCE
#include "painel.h"

#include <glib.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "som.h"

static struct som_estado som;
static bool servico_ligado;

/* enquanto o dedo arrasta um deslizante vale o valor do dedo (o do servidor chega atrasado) */
static struct {
	uint32_t id;
	float valor;
	double soltou;
} edita;

static void som_mudou(gv_app *app, int fd, void *dados) {
	som_ler(&som);
	gv_app_redesenhar(app);
}

static void garantir(void) {
	if (!servico_ligado && pn_app) {
		int fd = som_iniciar("Painel de controle");
		if (fd >= 0) {
			gv_app_vigiar(pn_app, fd, som_mudou, NULL);
		}
		servico_ligado = true;
	}
	som_ler(&som);
}

void pn_som_abrir(void) {
	garantir();
}

static int pct(float v) {
	return (int)lroundf(fminf(v, 1.0f) * 100);
}

/* um deslizante ligado a um volume do servidor */
static bool volume(Clay_ElementId id, float real, float *novo) {
	float v = edita.id == id.id ? edita.valor : fminf(real, 1);
	bool mudou = gv_deslizante(id, &v, 6, 18);
	if (mudou) {
		edita.id = id.id;
		edita.valor = v;
		edita.soltou = 0;
		*novo = v;
	}
	if (edita.id == id.id && !gv_premido(id)) {
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
	if (tem(tudo, "bluez|bluetooth|fone|headphone|headset|buds")) {
		return "headphones";
	}
	if (tem(tudo, "hdmi|displayport")) {
		return "desktop";
	}
	return "speaker";
}

/* a linha de um dispositivo: ícone, nome, "padrão" ou o botão para usar, o mudo e o volume */
enum { SAIDA, ENTRADA, APP };

static void linha_no(int tipo, int i, const struct som_no *no) {
	bool entrada = tipo == ENTRADA, app = tipo == APP;
	CLAY(CLAY_SID(gv_fmt("so-linha-%d-%d", tipo, i)), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) },
		.padding = CLAY_PADDING_ALL(14), .childGap = 10 }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = no->padrao ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(no->padrao ? 2 : 1) } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			gv_icone(app ? (no->icone[0] && gv_icone_existe(no->icone) ? no->icone : "music") : entrada ? "mic" : icone_saida(no), 24, gv_t.icone);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_str(no->descricao[0] ? no->descricao : no->nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				if (!app) {
					gv_texto(gv_str(no->padrao ? (entrada ? "O microfone que a Emma e os apps usam" : "A saída que a Emma e os apps usam") : "Não é a padrão"),
						gv_t.tamanho_corpo - 2, no->padrao ? gv_t.sucesso : gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			}
			if (!app && !no->padrao) {
				if (gv_botao_ext(CLAY_SID(gv_fmt("so-usar-%d-%d", tipo, i)), &(gv_botao_opcoes){ .texto = "Usar este", .estilo = GV_BOTAO_ELEVADO, .altura = 32 })) {
					if (entrada) {
						som_entrada_padrao(no->nome);
					} else {
						som_saida_padrao(no->nome);
					}
				}
			}
			if (gv_botao_redondo(CLAY_SID(gv_fmt("so-mudo-%d-%d", tipo, i)), no->mudo ? "volume-off" : "volume", 36, no->mudo, no->mudo)) {
				if (app) {
					som_mudo_app(no->indice, !no->mudo);
				} else if (entrada) {
					som_mudo_entrada(no->indice, !no->mudo);
				} else {
					som_mudo_saida(no->indice, !no->mudo);
				}
			}
		}
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			float novo = 0;
			CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
				if (volume(CLAY_SID(gv_fmt("so-vol-%d-%d", tipo, i)), no->volume, &novo)) {
					if (app) {
						som_volume_app(no->indice, novo);
					} else if (entrada) {
						som_volume_entrada(no->indice, novo);
					} else {
						som_volume_saida(no->indice, novo);
					}
				}
			}
			gv_texto(gv_str(no->mudo ? "mudo" : gv_fmt("%d%%", pct(no->volume)).chars), gv_t.tamanho_corpo, no->mudo ? gv_t.perigo : gv_t.texto, GV_PESO_MEDIO);
		}
	}
}

static void sem_servidor(void) {
	gv_texto(CLAY_STRING("Procurando o servidor de som… Se nada aparecer, falta o PipeWire com o pipewire-pulse ligado (systemctl --user enable --now pipewire pipewire-pulse wireplumber)."),
		gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
}

/* ---- o som de teste: um tom de 0,8 s, tocado pela saída padrão ---- */

static void tom_de_teste(void) {
	char caminho[256];
	snprintf(caminho, sizeof(caminho), "%s/lumna-teste-som.wav", g_get_user_runtime_dir());
	enum { TAXA = 44100, N = TAXA * 8 / 10 };
	static short pcm[N];
	for (int i = 0; i < N; i++) {
		double t = (double)i / TAXA, env = fmin(1.0, fmin(t / 0.03, (0.8 - t) / 0.08));
		pcm[i] = (short)(sin(2 * M_PI * 440 * t) * 9000 * env);
	}
	FILE *f = fopen(caminho, "wb");
	if (!f) {
		return;
	}
	unsigned dados = sizeof(pcm), riff = 36 + dados, taxa = TAXA, bps = TAXA * 2, fmt = 16;
	unsigned short pcm_tipo = 1, canais = 1, alinha = 2, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
	fwrite(&pcm_tipo, 2, 1, f); fwrite(&canais, 2, 1, f); fwrite(&taxa, 4, 1, f); fwrite(&bps, 4, 1, f);
	fwrite(&alinha, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dados, 4, 1, f);
	fwrite(pcm, 1, sizeof(pcm), f);
	fclose(f);
	char *playpw[] = { "pw-play", caminho, NULL }, *paplay[] = { "paplay", caminho, NULL };
	if (!g_spawn_async(NULL, playpw, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL)) {
		g_spawn_async(NULL, paplay, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
	}
}

/* ---- Saída ---- */

void pn_som_saida_desenhar(void) {
	garantir();
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		gv_texto(CLAY_STRING("Por onde sai o som. A Emma e todos os apps usam a saída padrão: se você não ouve a Emma, confira se é a certa, se não está no mudo e se o volume não está no zero. "
			"O teste toca um tom curto pela padrão."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (!som.ligado) {
			sem_servidor();
		} else if (som.nsaidas == 0) {
			gv_texto(CLAY_STRING("Nenhuma saída de som encontrada. Veja se o alto-falante ou o fone está ligado e reconhecido (pavucontrol ou wpctl status mostram o que o sistema vê)."),
				gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		} else {
			const struct som_no *p = som_saida_atual(&som);
			if (!p) {
				gv_texto(CLAY_STRING("Nenhuma saída está marcada como padrão: escolha uma abaixo."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_MEDIO);
			} else if (p->mudo || p->volume < 0.02f) {
				gv_texto(gv_str(gv_fmt("A saída padrão (%s) está %s: por isso nada se ouve.", p->descricao, p->mudo ? "no mudo" : "com o volume no zero").chars), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_MEDIO);
			}
			for (int i = 0; i < som.nsaidas; i++) {
				linha_no(SAIDA, i, &som.saidas[i]);
			}
			if (gv_botao_ext(CLAY_ID("so-testar"), &(gv_botao_opcoes){ .icone = "play", .texto = "Testar o som", .estilo = GV_BOTAO_CHEIO, .altura = 38 })) {
				tom_de_teste();
			}
		}
	}
}

/* ---- Entrada, com o nível ao vivo (parec, só enquanto a página está aberta) ---- */

static GPid parec_pid;
static int parec_fd = -1;
static float nivel;      /* 0..1, o pico recente */
static double visto;     /* a última vez que a página se desenhou */
static char parec_fonte[128];

static void parar_medidor(void) {
	if (parec_pid) {
		kill(parec_pid, SIGTERM);
		g_spawn_close_pid(parec_pid);
		parec_pid = 0;
	}
	if (parec_fd >= 0) {
		close(parec_fd);
		parec_fd = -1;
	}
	nivel = 0;
}

static void medidor_leu(gv_app *app, int fd, void *dados) {
	if (fd != parec_fd) {
		return;
	}
	short buf[800];
	ssize_t n = read(fd, buf, sizeof(buf));
	if (n <= 0 || gv_agora() - visto > 1.5) {
		parar_medidor(); /* a página fechou, ou o parec acabou */
		return;
	}
	double soma = 0;
	for (ssize_t i = 0; i < n / 2; i++) {
		soma += (double)buf[i] * buf[i];
	}
	double rms = n >= 2 ? sqrt(soma / (n / 2)) / 32768.0 : 0;
	float alvo = (float)fmin(1.0, rms * 5.0);
	nivel = alvo > nivel ? alvo : nivel * 0.85f + alvo * 0.15f;
	gv_app_redesenhar(app);
}

static void ligar_medidor(const char *fonte) {
	if (parec_pid && !strcmp(parec_fonte, fonte)) {
		return;
	}
	parar_medidor();
	char *argv[] = { "parec", "-d", (char *)fonte, "--raw", "--format=s16le", "--rate=8000", "--channels=1", "--latency-msec=60", NULL };
	int saida = -1;
	if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &parec_pid, NULL, &saida, NULL, NULL)) {
		parec_pid = 0;
		return;
	}
	parec_fd = saida;
	g_strlcpy(parec_fonte, fonte, sizeof(parec_fonte));
	gv_app_vigiar(pn_app, parec_fd, medidor_leu, NULL);
}

void pn_som_entrada_desenhar(void) {
	garantir();
	visto = gv_agora();
	const struct som_no *padrao = som_entrada_atual(&som);
	if (som.ligado && padrao && !padrao->mudo) {
		ligar_medidor(padrao->nome);
	} else if (parec_pid) {
		parar_medidor();
	}
	gv_acordar_em(0.5);
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		gv_texto(CLAY_STRING("Os microfones. A Emma ouve pelo microfone padrão: fale e veja a barra mexer. Se ela não se mexe, o microfone está mudo, o volume no zero ou é outro o aparelho."),
			gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (!som.ligado) {
			sem_servidor();
		} else if (som.nentradas == 0) {
			gv_texto(CLAY_STRING("Nenhum microfone encontrado."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		} else {
			CLAY(CLAY_ID("so-nivel"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 8 },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				gv_texto(CLAY_STRING("Nível ao vivo"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
				CLAY(CLAY_ID("so-nivel-trilho"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(10) } }, .backgroundColor = gv_t.sob_cursor,
					.cornerRadius = CLAY_CORNER_RADIUS(5) }) {
					CLAY(CLAY_ID("so-nivel-barra"), { .layout = { .sizing = { CLAY_SIZING_PERCENT(fmaxf(0.01f, nivel)), CLAY_SIZING_GROW(0) } },
						.backgroundColor = nivel > 0.85f ? gv_t.perigo : gv_t.acento, .cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
				}
				gv_texto(gv_str(!padrao ? "Sem microfone padrão" : padrao->mudo ? "O microfone padrão está no mudo" : nivel > 0.03f ? "Ouvindo você" : "Silêncio"),
					gv_t.tamanho_corpo - 2, padrao && padrao->mudo ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			for (int i = 0; i < som.nentradas; i++) {
				linha_no(ENTRADA, i, &som.entradas[i]);
			}
		}
	}
}

/* ---- Apps ---- */

void pn_som_apps_desenhar(void) {
	garantir();
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		gv_texto(CLAY_STRING("O volume de cada app que está tocando agora. Quando nenhuma toca, a lista fica vazia."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (!som.ligado) {
			sem_servidor();
		} else if (som.napps == 0) {
			gv_texto(CLAY_STRING("Nenhum app está tocando."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		} else {
			for (int i = 0; i < som.napps; i++) {
				linha_no(APP, i, &som.apps[i]);
			}
		}
	}
}
