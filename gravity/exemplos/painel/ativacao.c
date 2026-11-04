/*
 * Painel > Emma > Oi Emma: chamar a Emma pela voz. O servidor ouve o microfone na sua máquina, offline (reconhecimento Vosk só da frase "Oi Emma"
 * / "Ei Emma"); nada é gravado nem enviado até ela ouvir o nome. Aqui se liga e desliga, baixa o modelo de voz (~50 MB, uma vez) e vê o que falta.
 */
#include "painel.h"

#include <string.h>

#include "emmar.h"
#include "json.h"

struct estado {
	bool lido, servidor, disponivel, dep_vosk, dep_audio, modelo, ligado, rodando;
	char erro[300], baixando[16], baixar_erro[300];
	int progresso;
};

static struct estado est;
static bool ocupado, consultando;
static char aviso[300];
static double proxima_consulta;

struct job {
	int acao; /* 0 ler, 1 ligar/desligar, 2 baixar o modelo */
	bool ligar, ok;
	struct estado est;
	char erro[300];
};

static void ler_estado(const struct json *c, struct estado *e) {
	const struct json *deps = json_chave(c, "deps"), *baixa = json_chave(c, "download");
	e->servidor = true;
	e->disponivel = json_bool(json_chave(c, "available"), false);
	e->dep_vosk = json_bool(json_chave(deps, "vosk"), false);
	e->dep_audio = json_bool(json_chave(deps, "sounddevice"), false);
	e->modelo = json_bool(json_chave(c, "model"), false);
	e->ligado = json_bool(json_chave(c, "enabled"), false);
	e->rodando = json_bool(json_chave(c, "running"), false);
	g_strlcpy(e->erro, json_chave_texto(c, "error"), sizeof(e->erro));
	g_strlcpy(e->baixando, json_chave_texto(baixa, "state"), sizeof(e->baixando));
	g_strlcpy(e->baixar_erro, json_chave_texto(baixa, "error"), sizeof(e->baixar_erro));
	e->progresso = (int)json_numero(json_chave(baixa, "progress"), 0);
}

static void trabalho(void *d) {
	struct job *j = d;
	struct emmar r;
	bool ok;
	if (j->acao == 1) {
		ok = emmar_chamar("PUT", "/api/wake", j->ligar ? "{\"enabled\":true}" : "{\"enabled\":false}", 0, &r);
	} else if (j->acao == 2) {
		ok = emmar_chamar("POST", "/api/wake/model", "{}", 0, &r);
	} else {
		ok = emmar_chamar("GET", "/api/wake", NULL, 0, &r);
	}
	j->ok = ok;
	if (ok) {
		ler_estado(r.corpo, &j->est);
	} else {
		g_strlcpy(j->erro, r.msg, sizeof(j->erro));
	}
	emmar_liberar(&r);
}

static void pronto(void *d) {
	struct job *j = d;
	ocupado = consultando = false;
	if (j->ok) {
		est = j->est;
		est.lido = true;
		if (j->acao != 0) {
			aviso[0] = '\0';
		}
	} else {
		est.lido = true;
		est.servidor = false;
		g_strlcpy(aviso, j->erro, sizeof(aviso));
	}
	g_free(j);
}

static void consultar(int acao, bool ligar) {
	if (acao == 0 && consultando) {
		return;
	}
	struct job *j = g_new0(struct job, 1);
	j->acao = acao;
	j->ligar = ligar;
	if (acao == 0) {
		consultando = true;
	} else {
		ocupado = true;
	}
	pn_tarefa(trabalho, pronto, j);
}

void pn_emma_ativ_abrir(void) {
	est.lido = false;
	aviso[0] = '\0';
	proxima_consulta = 0;
}

static void linha(const char *rotulo, bool ok, const char *dica) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone(ok ? "check" : "info", 18, ok ? gv_t.sucesso : gv_t.texto_suave);
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			if (!ok && dica && dica[0]) {
				gv_texto(gv_str(dica), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
		}
	}
}

void pn_emma_ativ_desenhar(void) {
	double agora = gv_agora();
	bool baixando = !strcmp(est.baixando, "downloading");
	if (!ocupado && (!est.lido || agora >= proxima_consulta)) {
		proxima_consulta = agora + (baixando ? 1.0 : 3.0); /* aberta, acompanha o servidor (e o download) */
		consultar(0, false);
	}
	gv_acordar_em(baixando ? 1.0 : 3.0);
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 14 } }) {
		gv_texto(CLAY_STRING("Chame a Emma falando “Oi Emma” ou “Ei Emma”, de qualquer lugar: a popup abre e ela já passa a ouvir. O reconhecimento roda aqui "
				     "na sua máquina, sem internet; nada é gravado nem enviado até ela ouvir o nome."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (aviso[0]) {
			gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
		}
		/* nada de return dentro de um CLAY: o elemento ficaria aberto e o layout quebra */
		bool sem_servidor = est.lido && !est.servidor, consultando_ainda = !est.lido;
		if (sem_servidor) {
			gv_texto(CLAY_STRING("O servidor da Emma não está respondendo."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		} else if (consultando_ainda) {
			gv_texto(CLAY_STRING("Consultando…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		if (!sem_servidor && !consultando_ainda) {
		bool pronto_tudo = est.disponivel && est.dep_vosk && est.dep_audio && est.modelo;
		linha("Roda direto no computador", est.disponivel, "A escuta só funciona com o LUMNA rodando direto no PC, não dentro de um container.");
		linha("Reconhecimento (vosk)", est.dep_vosk, "Falta instalar: no Python do servidor, pip install vosk. O instalador tenta isso uma vez ao ligar.");
		linha("Microfone (sounddevice)", est.dep_audio, "Falta o sounddevice e a biblioteca de áudio do sistema (libportaudio2 no Debian/Ubuntu, portaudio no Arch).");
		linha("Modelo de voz em português (~50 MB)", est.modelo, NULL);
		if (!est.modelo && est.disponivel) {
			if (baixando) {
				CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
					CLAY(CLAY_ID("ativ-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(8) } }, .backgroundColor = gv_t.borda_controle, .cornerRadius = CLAY_CORNER_RADIUS(4) }) {
						CLAY(CLAY_ID("ativ-barra-i"), { .layout = { .sizing = { CLAY_SIZING_PERCENT(est.progresso / 100.0f), CLAY_SIZING_GROW(0) } }, .backgroundColor = gv_t.acento,
							.cornerRadius = CLAY_CORNER_RADIUS(4) }) {}
					}
					gv_texto(gv_fmt("Baixando… %d%%", est.progresso), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				}
			} else if (gv_botao_ext(CLAY_ID("ativ-baixar"), &(gv_botao_opcoes){ .icone = "download", .texto = "Baixar o modelo de voz", .estilo = GV_BOTAO_ELEVADO, .altura = 38,
				    .desativado = ocupado })) {
				consultar(2, false);
			}
			if (est.baixar_erro[0] && !strcmp(est.baixando, "error")) {
				gv_texto(gv_fmt("O download falhou: %s", est.baixar_erro), gv_t.tamanho_corpo - 2, gv_t.perigo, GV_PESO_NORMAL);
			}
		}
		gv_separador(false, 0);
		bool ligado = est.ligado;
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(CLAY_STRING("Ouvir o “Oi Emma”"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_str(!pronto_tudo ? "Complete os itens acima para ligar." : ligado && est.rodando ? "Ouvindo agora." : ligado ? "Ligado, mas parou: veja o aviso abaixo." : "Desligado."),
					gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
			}
			if ((pronto_tudo || ligado) && !ocupado && gv_interruptor(CLAY_ID("ativ-liga"), &ligado)) {
				consultar(1, ligado);
			}
		}
		if (ligado && est.erro[0]) {
			gv_texto(gv_fmt("Parou: %s", est.erro), gv_t.tamanho_corpo - 2, gv_t.perigo, GV_PESO_NORMAL);
		}
		}
	}
}
