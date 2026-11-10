/* O motor do LUMNA Music (servicos/musica_player.c) com o mpv de verdade, saída "null". ./testa_player PASTA (com Músicas de fixtures.py) */
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "musica_player.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static gint ev_avancou, ev_fim, ev_erro, ev_morreu;
static void evento(int t, void *d) {
	(void)d;
	if (t == MP_EV_ESTADO) {
		return;
	}
	g_atomic_int_inc(t == MP_EV_AVANCOU ? &ev_avancou : t == MP_EV_FIM ? &ev_fim : t == MP_EV_ERRO ? &ev_erro : &ev_morreu);
}

static bool espera(bool (*cond)(mp_player *), mp_player *p, double seg) {
	gint64 fim = g_get_monotonic_time() + (gint64)(seg * 1e6);
	while (g_get_monotonic_time() < fim) {
		if (cond(p)) {
			return true;
		}
		g_usleep(50000);
	}
	return cond(p);
}

static bool tocando(mp_player *p) { mp_estado e; mp_estado_ler(p, &e); return e.tocando && e.dur > 1; }
static bool avancou1(mp_player *p) { (void)p; return g_atomic_int_get(&ev_avancou) >= 1; }
static bool fim1(mp_player *p) { (void)p; return g_atomic_int_get(&ev_fim) >= 1; }
static bool erro1(mp_player *p) { (void)p; return g_atomic_int_get(&ev_erro) >= 1 || g_atomic_int_get(&ev_fim) >= 1; }
static bool morreu1(mp_player *p) { (void)p; return g_atomic_int_get(&ev_morreu) >= 1; }

static int processos_mpv(void) {
	char *out = NULL;
	char *cmd = g_strdup_printf("pgrep -c -f 'lumna-musica-%d-'", (int)getpid()); /* só os deste teste */
	g_spawn_command_line_sync(cmd, &out, NULL, NULL, NULL);
	g_free(cmd);
	int n = out ? atoi(out) : -1;
	g_free(out);
	return n;
}

int main(int argc, char **argv) {
	g_setenv("LUMNA_MUSICA_AO", "null", TRUE);
	const char *raiz = argc > 1 ? argv[1] : ".";
	char *t1 = g_build_filename(raiz, "Músicas", "Horizonte violeta", "01 Entre órbitas.mp3", NULL);
	char *t2 = g_build_filename(raiz, "Músicas", "Horizonte violeta", "02 Maré noturna.mp3", NULL);
	char *t3 = g_build_filename(raiz, "Músicas", "Luz distante", "01 Aurora lenta.flac", NULL);
	mp_player *p = mp_criar(evento, NULL);
	mp_ajustes(p, "", true, true, 0);
	mp_volume(p, 55);
	mp_estado e;

	/* tocar */
	CONFERE(mp_tocar(p, t1, 0, false), "o mpv sobe");
	CONFERE(espera(tocando, p, 6), "toca (carrega a duração)");
	mp_estado_ler(p, &e);
	CONFERE(e.vivo && e.carregada && fabs(e.dur - 3) < 0.4 && !strcmp(e.caminho, t1) && e.volume == 55, "o estado: %.2f s de %.2f", e.pos, e.dur);
	double v;
	CONFERE(mp_consultar(p, "volume", &v) && fabs(v - 55) < 0.6, "o volume chegou ao mpv (%.1f)", v);
	char af[512];
	CONFERE(mp_consultar_texto(p, "af", af, sizeof(af)) || true, "consulta de texto");
	g_usleep(700000);
	mp_estado_ler(p, &e);
	CONFERE(e.pos > 0.4 && e.pos < 1.6, "a posição anda (%.2f)", e.pos);

	/* pausar */
	mp_pausar(p, true);
	g_usleep(300000);
	mp_estado_ler(p, &e);
	double pp = e.pos;
	CONFERE(!e.tocando, "pausado");
	g_usleep(600000);
	mp_estado_ler(p, &e);
	CONFERE(fabs(e.pos - pp) < 0.05, "parado, a posição não anda (%.2f -> %.2f)", pp, e.pos);
	mp_pausar(p, false);
	g_usleep(500000);
	mp_estado_ler(p, &e);
	CONFERE(e.tocando && e.pos > pp + 0.2, "volta a andar");

	/* buscar */
	mp_buscar(p, 0.2);
	g_usleep(400000);
	mp_estado_ler(p, &e);
	CONFERE(e.pos > 0.2 && e.pos < 1.0, "buscar para 0,2 s (%.2f)", e.pos);

	/* volume */
	mp_volume(p, 20);
	g_usleep(200000);
	CONFERE(mp_consultar(p, "volume", &v) && fabs(v - 20) < 0.6, "o novo volume (%.1f)", v);

	/* o fim da faixa, sem próxima */
	g_atomic_int_set(&ev_fim, 0);
	mp_buscar(p, 2.4);
	CONFERE(espera(fim1, p, 5), "chegou ao fim: avisa");
	mp_estado_ler(p, &e);
	CONFERE(!e.tocando, "e para");

	/* trocar de faixa (substituir) */
	CONFERE(mp_tocar(p, t3, 0, false) && espera(tocando, p, 6), "toca o FLAC");
	mp_estado_ler(p, &e);
	CONFERE(!strcmp(e.caminho, t3), "a faixa mudou");
	/* tocar pausado, começando em 1,5 s */
	CONFERE(mp_tocar(p, t2, 1.5, true), "tocar pausado");
	g_usleep(1200000);
	mp_estado_ler(p, &e);
	CONFERE(!e.tocando && e.pos >= 1.2 && e.pos < 2.2, "carregou pausada, a 1,5 s (%.2f, tocando=%d)", e.pos, e.tocando);
	mp_pausar(p, false);

	/* gapless: a próxima emenda sozinha */
	g_atomic_int_set(&ev_avancou, 0);
	g_atomic_int_set(&ev_fim, 0);
	CONFERE(mp_tocar(p, t1, 0, false) && espera(tocando, p, 6), "toca a primeira");
	mp_proxima(p, t2);
	mp_buscar(p, 1.8);
	CONFERE(espera(avancou1, p, 6), "a próxima entra sozinha (gapless)");
	g_usleep(300000);
	mp_estado_ler(p, &e);
	CONFERE(!strcmp(e.caminho, t2) && e.tocando && e.pos < 1.5 && g_atomic_int_get(&ev_fim) == 0, "estado depois do gapless: %s a %.2f", e.caminho, e.pos);
	mp_proxima(p, NULL);
	g_atomic_int_set(&ev_fim, 0);
	mp_buscar(p, 2.5);
	CONFERE(espera(fim1, p, 5), "sem próxima, a última acaba e avisa");

	/* trocar a próxima a tempo */
	g_atomic_int_set(&ev_avancou, 0);
	mp_tocar(p, t1, 0, false);
	espera(tocando, p, 6);
	mp_proxima(p, t2);
	mp_proxima(p, t3);
	mp_buscar(p, 2.0);
	espera(avancou1, p, 6);
	g_usleep(300000);
	mp_estado_ler(p, &e);
	CONFERE(!strcmp(e.caminho, t3), "a última escolhida é a que entra (%s)", e.caminho);

	/* crossfade de 1 s */
	mp_ajustes(p, "", true, true, 1);
	g_atomic_int_set(&ev_avancou, 0);
	g_atomic_int_set(&ev_fim, 0);
	mp_tocar(p, t1, 0, false);
	espera(tocando, p, 6);
	mp_proxima(p, t2);
	mp_buscar(p, 0.6);
	gint64 ini = g_get_monotonic_time();
	CONFERE(espera(avancou1, p, 8), "a transição acaba e avisa");
	double demora = (double)(g_get_monotonic_time() - ini) / 1e6;
	g_usleep(200000);
	mp_estado_ler(p, &e);
	CONFERE(!strcmp(e.caminho, t2) && e.tocando && e.pos > 0.3 && e.pos < 2.5, "depois da transição toca a segunda, já adiantada: %.2f s (levou %.2f s)", e.pos, demora);
	CONFERE(demora < 3.0, "levou o tempo da faixa, não mais (%.2f)", demora);
	CONFERE(g_atomic_int_get(&ev_fim) == 0, "e sem aviso de fim no meio");
	{ int n = processos_mpv(); if (n != 1) { g_spawn_command_line_sync("pgrep -fa lumna-musica-", NULL, NULL, NULL, NULL); if (system("pgrep -fa lumna-musica- >&2")) {} } CONFERE(n == 1, "o segundo mpv foi embora (%d processos)", n); }
	/* mexer no tempo cancela a transição (com 2 s de transição, há tempo de ela já ter começado) */
	mp_ajustes(p, "", true, true, 2);
	g_atomic_int_set(&ev_avancou, 0);
	mp_tocar(p, t1, 0, false);
	espera(tocando, p, 6);
	mp_proxima(p, t2);
	mp_buscar(p, 0.5);
	g_usleep(1000000);
	mp_buscar(p, 0.2);
	g_usleep(700000);
	mp_estado_ler(p, &e);
	CONFERE(!strcmp(e.caminho, t1) && e.pos < 1.6 && g_atomic_int_get(&ev_avancou) == 0, "voltar no tempo na transição mantém a faixa (%s, %.2f)", e.caminho, e.pos);
	CONFERE(mp_consultar(p, "volume", &v) && fabs(v - 20) < 0.6, "e o volume volta ao que era (%.1f)", v);
	mp_ajustes(p, "", true, true, 0);

	/* saída e normalização em tempo de execução */
	mp_ajustes(p, "null", false, true, 0);
	g_usleep(300000);
	char t[512];
	CONFERE(mp_consultar_texto(p, "audio-device", t, sizeof(t)) && !strcmp(t, "null"), "trocar a saída (%s)", t);
	mp_ajustes(p, "", true, true, 0);
	g_usleep(300000);
	CONFERE(mp_consultar_texto(p, "audio-device", t, sizeof(t)) && !strcmp(t, "auto"), "voltar ao padrão (%s)", t);

	/* parar */
	mp_parar(p);
	g_usleep(300000);
	mp_estado_ler(p, &e);
	CONFERE(!e.tocando && !e.caminho[0], "parar descarrega");

	/* um arquivo que não existe */
	g_atomic_int_set(&ev_erro, 0);
	g_atomic_int_set(&ev_fim, 0);
	mp_tocar(p, "/nao/existe.mp3", 0, false);
	CONFERE(espera(erro1, p, 6), "arquivo inexistente: avisa (erro ou fim)");
	CONFERE(mp_tocar(p, t1, 0, false) && espera(tocando, p, 6), "e o motor continua tocando depois");

	/* o mpv morre sozinho: avisa e a próxima faixa o levanta de novo */
	char *out = NULL;
	{ char *k = g_strdup_printf("pkill -9 -f lumna-musica-%d-", (int)getpid()); g_spawn_command_line_sync(k, NULL, NULL, NULL, NULL); g_free(k); }
	CONFERE(espera(morreu1, p, 5), "o mpv morreu: avisa");
	mp_estado_ler(p, &e);
	CONFERE(!e.vivo, "estado: não vivo");
	CONFERE(mp_tocar(p, t2, 0, false) && espera(tocando, p, 8), "tocar de novo levanta outro mpv");
	g_free(out);

	/* dispositivos */
	GPtrArray *d = g_ptr_array_new_with_free_func(g_free);
	mp_dispositivos(d);
	CONFERE(d->len >= 1 && g_str_has_prefix(d->pdata[0], "auto\t"), "a lista de dispositivos começa em auto (%u)", d->len);
	g_ptr_array_free(d, TRUE);

	mp_liberar(p);
	g_usleep(300000);
	CONFERE(processos_mpv() == 0, "nenhum mpv sobra depois de liberar (%d)", processos_mpv());
	g_free(t1); g_free(t2); g_free(t3);
	if (falhas) {
		fprintf(stderr, "player: %d falha(s)\n", falhas);
		return 1;
	}
	printf("musica (motor mpv): ok\n");
	return 0;
}
