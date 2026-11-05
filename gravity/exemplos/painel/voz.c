/*
 * Painel > Emma > Voz: como a Emma ouve e fala. O backend tem dois jeitos para cada um: no PC (Whisper para ouvir, Pocket TTS para
 * falar: de graça, sem internet depois de baixados) ou o Fish Audio (um serviço com chave de API). Aqui se escolhe, se instala o que
 * é do PC (o backend baixa na hora, em segundo plano) e se põe a chave do Fish, se for esse o caminho. Cada mudança vale na hora.
 */
#include "painel.h"

#include <string.h>

#include "emmar.h"
#include "json.h"

#define MAX_LISTA 8

struct estado {
	bool lido, servidor, disponivel;
	char tts[8], stt[8], whisper[16], voz_local[32], idioma[8], voice_id[120], tts_modelo[48], chave_mascara[24];
	bool leitura_automatica, tem_chave, fala_pronta, escuta_pronta;
	bool stt_instalado, tts_instalado;
	char tamanhos[MAX_LISTA][16], vozes[MAX_LISTA][32];
	int ntamanhos, nvozes;
	char job_estado[12], job_o_que[8], job_erro[300], job_linha[200];
};

static struct estado est;
static bool ocupado, consultando, editando_fish;
static char f_chave[200], f_voice_id[120], f_modelo[48];
static char aviso[300];
static double proxima;

struct job {
	int acao; /* 0 ler, 1 mudar (corpo), 2 instalar (nome: stt | tts) */
	char nome[8];
	char corpo[700];
	struct estado est;
	bool ok;
	char erro[300];
};

static void copiar(char *dest, size_t n, const struct json *o, const char *chave) {
	g_strlcpy(dest, json_chave_texto(o, chave), n);
}

static void ler_estado(const struct json *c, const struct json *lv, struct estado *e) {
	const struct json *v = json_chave(c, "voice");
	e->servidor = true;
	copiar(e->tts, sizeof(e->tts), v, "tts_engine");
	copiar(e->stt, sizeof(e->stt), v, "stt_engine");
	copiar(e->whisper, sizeof(e->whisper), v, "whisper_model");
	copiar(e->voz_local, sizeof(e->voz_local), v, "local_voice");
	copiar(e->idioma, sizeof(e->idioma), v, "language");
	copiar(e->voice_id, sizeof(e->voice_id), v, "voice_id");
	copiar(e->tts_modelo, sizeof(e->tts_modelo), v, "tts_model");
	copiar(e->chave_mascara, sizeof(e->chave_mascara), v, "api_key_masked");
	e->leitura_automatica = json_bool(json_chave(v, "auto_read"), false);
	e->tem_chave = json_bool(json_chave(v, "has_key"), false);
	e->fala_pronta = json_bool(json_chave(v, "server_tts"), false);
	e->escuta_pronta = json_bool(json_chave(v, "server_stt"), false);
	if (!e->stt[0]) {
		g_strlcpy(e->stt, "fish", sizeof(e->stt));
	}
	if (!e->tts[0]) {
		g_strlcpy(e->tts, "fish", sizeof(e->tts));
	}
	e->disponivel = json_bool(json_chave(lv, "available"), false);
	const struct json *stt = json_chave(lv, "stt"), *tts = json_chave(lv, "tts"), *job = json_chave(lv, "job");
	e->stt_instalado = json_bool(json_chave(stt, "installed"), false);
	e->tts_instalado = json_bool(json_chave(tts, "installed"), false);
	const struct json *t = json_chave(stt, "sizes"), *vz = json_chave(tts, "voices");
	e->ntamanhos = t ? MIN(t->nfilhos, MAX_LISTA) : 0;
	for (int i = 0; i < e->ntamanhos; i++) {
		g_strlcpy(e->tamanhos[i], json_texto(json_item(t, i)) ? json_texto(json_item(t, i)) : "", sizeof(e->tamanhos[i]));
	}
	e->nvozes = vz ? MIN(vz->nfilhos, MAX_LISTA) : 0;
	for (int i = 0; i < e->nvozes; i++) {
		g_strlcpy(e->vozes[i], json_texto(json_item(vz, i)) ? json_texto(json_item(vz, i)) : "", sizeof(e->vozes[i]));
	}
	copiar(e->job_estado, sizeof(e->job_estado), job, "state");
	copiar(e->job_o_que, sizeof(e->job_o_que), job, "what");
	copiar(e->job_erro, sizeof(e->job_erro), job, "error");
	const struct json *log = json_chave(job, "log");
	e->job_linha[0] = '\0';
	if (log && log->nfilhos > 0) {
		g_strlcpy(e->job_linha, json_texto(json_item(log, log->nfilhos - 1)) ? json_texto(json_item(log, log->nfilhos - 1)) : "", sizeof(e->job_linha));
	}
}

static void trabalho(void *d) {
	struct job *j = d;
	struct emmar r;
	bool ok = true;
	if (j->acao == 1) {
		ok = emmar_chamar("PUT", "/api/settings", j->corpo, 0, &r);
		if (!ok) {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
	} else if (j->acao == 2) {
		char *c = g_strdup_printf("{\"what\":\"%s\"}", j->nome);
		ok = emmar_chamar("POST", "/api/local-voice/install", c, 0, &r);
		g_free(c);
		if (!ok) {
			g_strlcpy(j->erro, r.msg, sizeof(j->erro));
		}
		emmar_liberar(&r);
	}
	j->ok = ok;
	struct emmar s, lv;
	if (emmar_chamar("GET", "/api/settings", NULL, 0, &s)) {
		bool tem_lv = emmar_chamar("GET", "/api/local-voice", NULL, 0, &lv);
		ler_estado(s.corpo, tem_lv ? lv.corpo : NULL, &j->est);
		j->est.lido = true;
		if (tem_lv) {
			emmar_liberar(&lv);
		}
	} else if (!j->erro[0]) {
		g_strlcpy(j->erro, s.msg, sizeof(j->erro));
		j->ok = false;
	}
	emmar_liberar(&s);
}

static void pronto(void *d) {
	struct job *j = d;
	ocupado = consultando = false;
	if (j->est.servidor) {
		bool editando = editando_fish;
		est = j->est;
		est.lido = true;
		if (!editando) {
			g_strlcpy(f_voice_id, est.voice_id, sizeof(f_voice_id));
			g_strlcpy(f_modelo, est.tts_modelo, sizeof(f_modelo));
		}
	} else {
		est.lido = true;
		est.servidor = false;
	}
	if (j->acao != 0) {
		g_strlcpy(aviso, j->ok ? "" : j->erro, sizeof(aviso));
	}
	g_free(j);
}

static void tarefa(int acao, const char *nome, const char *corpo) {
	if (ocupado || (acao == 0 && consultando)) {
		return;
	}
	struct job *j = g_new0(struct job, 1);
	j->acao = acao;
	g_strlcpy(j->nome, nome ? nome : "", sizeof(j->nome));
	g_strlcpy(j->corpo, corpo ? corpo : "", sizeof(j->corpo));
	if (acao == 0) {
		consultando = true;
	} else {
		ocupado = true;
	}
	pn_tarefa(trabalho, pronto, j);
}

void pn_emma_voz_abrir(void) {
	est.lido = false;
	aviso[0] = '\0';
	editando_fish = false;
	f_chave[0] = '\0';
	proxima = 0;
}

static void mudar(const char *campo, const char *valor) {
	GString *g = g_string_new("{\"voice\":{\"");
	g_string_append(g, campo);
	g_string_append(g, "\":");
	json_escrever_texto(g, valor);
	g_string_append(g, "}}");
	tarefa(1, "", g->str);
	g_string_free(g, TRUE);
}

static void escolher_motor(const char *campo, const char *motor, const char *atual) {
	if (strcmp(motor, atual)) {
		mudar(campo, motor);
	}
}

static bool instalando(const char *o_que) {
	return !strcmp(est.job_estado, "running") && !strcmp(est.job_o_que, o_que);
}

/* a linha de um motor: "No PC" ou "Fish Audio", e debaixo o estado do que o PC precisa baixar */
static void bloco_motor(const char *id, const char *titulo, const char *dica, const char *campo, const char *atual, const char *rotulo_pc, bool instalado,
		const char *o_que, bool pronto_o_servidor) {
	CLAY(CLAY_SID(gv_fmt("voz-%s", id)), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 10 },
		.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
		gv_texto(gv_str(titulo), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
		gv_texto(gv_str(dica), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
		static const char *const motores[] = { "local", "fish" };
		CLAY_AUTO_ID({ .layout = { .childGap = 8 } }) {
			for (int i = 0; i < 2; i++) {
				bool sel = !strcmp(atual, motores[i]);
				const char *rot = i == 0 ? rotulo_pc : "Fish Audio (serviço com chave)";
				if (gv_botao_ext(CLAY_SIDI(gv_fmt("voz-m-%s", id), (uint32_t)i), &(gv_botao_opcoes){ .texto = rot, .estilo = sel ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO,
					.altura = 34, .desativado = ocupado }) && !sel) {
					escolher_motor(campo, motores[i], atual);
				}
			}
		}
		if (!strcmp(atual, "local")) {
			if (!est.disponivel) {
				gv_texto(CLAY_STRING("A voz do PC funciona com o servidor da Emma rodando neste computador."), gv_t.tamanho_corpo - 2, gv_t.perigo, GV_PESO_NORMAL);
			} else if (instalado) {
				gv_texto(CLAY_STRING("Instalado: funciona sem internet e sem custo."), gv_t.tamanho_corpo - 2, gv_t.sucesso, GV_PESO_NORMAL);
			} else if (instalando(o_que)) {
				gv_texto(gv_str(est.job_linha[0] ? gv_fmt("Baixando e instalando… %s", est.job_linha).chars : "Baixando e instalando…"), gv_t.tamanho_corpo - 2,
					gv_t.texto_secundario, GV_PESO_NORMAL);
			} else {
				if (!strcmp(est.job_estado, "error") && !strcmp(est.job_o_que, o_que) && est.job_erro[0]) {
					gv_texto(gv_str(est.job_erro), gv_t.tamanho_corpo - 2, gv_t.perigo, GV_PESO_NORMAL);
				}
				gv_texto(CLAY_STRING("Ainda não está instalado. É um download grande (modelos e bibliotecas); acontece uma vez."), gv_t.tamanho_corpo - 2,
					gv_t.texto_secundario, GV_PESO_NORMAL);
				if (gv_botao_ext(CLAY_SID(gv_fmt("voz-i-%s", id)), &(gv_botao_opcoes){ .icone = "download", .texto = "Instalar no PC", .estilo = GV_BOTAO_CHEIO,
					.altura = 34, .desativado = ocupado || !strcmp(est.job_estado, "running") })) {
					tarefa(2, o_que, NULL);
				}
			}
		} else if (!pronto_o_servidor) {
			gv_texto(CLAY_STRING("Falta a chave do Fish Audio (abaixo)."), gv_t.tamanho_corpo - 2, gv_t.perigo, GV_PESO_NORMAL);
		}
	}
}

void pn_emma_voz_desenhar(void) {
	double agora = gv_agora();
	bool rodando = !strcmp(est.job_estado, "running");
	if (!ocupado && !consultando && (!est.lido || agora >= proxima) && !editando_fish) {
		proxima = agora + (rodando ? 1.5 : 4.0);
		tarefa(0, "", NULL);
	}
	gv_acordar_em(rodando ? 1.5 : 4.0);
	CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 12 } }) {
		gv_texto(CLAY_STRING("A Emma ouve pelo microfone e fala pelo alto-falante. Cada um pode ser feito aqui no PC (de graça, sem internet) ou por um serviço de fora. "
			"O que você muda vale na hora."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		if (aviso[0]) {
			gv_texto(gv_str(aviso), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
		}
		if (est.lido && !est.servidor) {
			gv_texto(CLAY_STRING("O servidor da Emma não está respondendo."), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
		} else if (!est.lido) {
			gv_texto(CLAY_STRING("Consultando…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
		if (est.lido && est.servidor) {
		/* a linha de cima: o que a Emma consegue agora */
		gv_texto(gv_str(gv_fmt("Agora a Emma %s e %s.", est.escuta_pronta ? "consegue ouvir" : "não consegue ouvir ainda",
			est.fala_pronta ? "consegue falar" : "não consegue falar ainda").chars), gv_t.tamanho_corpo, est.escuta_pronta && est.fala_pronta ? gv_t.sucesso : gv_t.texto, GV_PESO_MEDIO);

		bloco_motor("ouvir", "Ouvir (transcrever o que você fala)", "Whisper roda no processador do PC e entende português bem.", "stt_engine", est.stt,
			"No PC (Whisper)", est.stt_instalado, "stt", est.escuta_pronta);
		if (!strcmp(est.stt, "local") && est.ntamanhos > 0) {
			const char *tams[MAX_LISTA];
			int ti = 0;
			for (int i = 0; i < est.ntamanhos; i++) {
				tams[i] = est.tamanhos[i];
				if (!strcmp(est.tamanhos[i], est.whisper)) {
					ti = i;
				}
			}
			if (pn_ajuste_lista(CLAY_ID("voz-tamanho"), "sliders", "Tamanho do modelo", "Maior entende melhor, mas demora mais para responder", tams, est.ntamanhos, &ti) && !ocupado) {
				mudar("whisper_model", est.tamanhos[ti]);
			}
		}

		bloco_motor("falar", "Falar (ler as respostas em voz alta)", "Pocket TTS fala português no processador do PC, com uma voz do catálogo (ou a sua, clonada).", "tts_engine", est.tts,
			"No PC (Pocket TTS)", est.tts_instalado, "tts", est.fala_pronta);
		if (!strcmp(est.tts, "local") && est.nvozes > 0) {
			const char *vozes[MAX_LISTA];
			int vi = 0;
			for (int i = 0; i < est.nvozes; i++) {
				vozes[i] = est.vozes[i];
				if (!strcmp(est.vozes[i], est.voz_local)) {
					vi = i;
				}
			}
			if (pn_ajuste_lista(CLAY_ID("voz-voz"), "mic", "Voz", "A voz do catálogo que a Emma usa para falar", vozes, est.nvozes, &vi) && !ocupado) {
				mudar("local_voice", est.vozes[vi]);
			}
		}

		static const char *const idiomas_nome[] = { "Português", "English", "Español", "Français", "Deutsch" };
		static const char *const idiomas_cod[] = { "pt", "en", "es", "fr", "de" };
		int ii = 0;
		for (int i = 0; i < 5; i++) {
			if (!strcmp(est.idioma, idiomas_cod[i])) {
				ii = i;
			}
		}
		if (pn_ajuste_lista(CLAY_ID("voz-idioma"), "globe", "Idioma que você fala", "Ajuda a transcrição a acertar as palavras", idiomas_nome, 5, &ii) && !ocupado) {
			mudar("language", idiomas_cod[ii]);
		}
		pn_linha_comecar(CLAY_ID("voz-auto"), "volume", "Ler as respostas em voz alta", "Sem precisar pedir: toda resposta da Emma é falada");
		{
			bool v = est.leitura_automatica;
			if (gv_interruptor(CLAY_ID("voz-auto-v"), &v) && !ocupado) {
				GString *g = g_string_new("{\"voice\":{\"auto_read\":");
				g_string_append(g, v ? "true" : "false");
				g_string_append(g, "}}");
				tarefa(1, "", g->str);
				g_string_free(g, TRUE);
			}
		}
		pn_linha_acabar();

		bool usa_fish = !strcmp(est.stt, "fish") || !strcmp(est.tts, "fish");
		if (usa_fish) {
			CLAY(CLAY_ID("voz-fish"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 8 },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno), .border = { .color = gv_t.borda_controle, .width = GV_BORDA(1) } }) {
				gv_texto(CLAY_STRING("Fish Audio"), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				if (gv_campo(CLAY_ID("voz-chave"), f_chave, sizeof(f_chave), est.tem_chave ? gv_fmt("Chave de API (guardada %s; deixe vazio para manter)", est.chave_mascara).chars : "Chave de API do Fish Audio", GV_CAMPO_SENHA) & GV_CAMPO_MUDOU) {
					editando_fish = true;
				}
				if (gv_campo(CLAY_ID("voz-voice-id"), f_voice_id, sizeof(f_voice_id), "Identificador da voz (opcional)", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
					editando_fish = true;
				}
				if (gv_campo(CLAY_ID("voz-modelo"), f_modelo, sizeof(f_modelo), "Modelo de voz (vazio: o gratuito de testes)", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
					editando_fish = true;
				}
				if (gv_botao_ext(CLAY_ID("voz-aplicar"), &(gv_botao_opcoes){ .icone = "check", .texto = "Aplicar", .estilo = GV_BOTAO_CHEIO, .altura = 36,
					.desativado = ocupado || !editando_fish })) {
					GString *g = g_string_new("{\"voice\":{\"voice_id\":");
					json_escrever_texto(g, f_voice_id);
					g_string_append(g, ",\"tts_model\":");
					json_escrever_texto(g, f_modelo);
					if (f_chave[0]) {
						g_string_append(g, ",\"api_key\":");
						json_escrever_texto(g, f_chave);
					}
					g_string_append(g, "}}");
					tarefa(1, "", g->str);
					g_string_free(g, TRUE);
					f_chave[0] = '\0';
					editando_fish = false;
				}
			}
		}
		}
	}
}
