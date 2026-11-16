/*
 * Sistema → Sobre: o computador (modelo, processador, memória...), as atualizações do sistema (o que o
 * checkupdates acha, e o histórico do pacman) e os avisos legais. Os dados vêm de servicos/sistema.c.
 */
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "sistema.h"

/* ---- Este computador ---- */

static struct sistema_info info;
static bool info_lida;

void pn_sobre_pc_abrir(void) {
	sistema_ler(&info);
	info_lida = true;
}

static void linha_info(uint32_t k, const char *nome, const char *valor) {
	pn_linha_comecar(CLAY_IDI("sb-info", k), NULL, nome, NULL);
	CLAY(CLAY_IDI("sb-info-v", k), { .layout = { .sizing = { CLAY_SIZING_FIT(0, 520) } } }) {
		gv_texto(gv_str(valor), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	pn_linha_acabar();
}

static const char *duracao(int64_t s) {
	if (s < 3600) {
		return gv_fmt("%d min", (int)(s / 60)).chars;
	}
	if (s < 86400) {
		return gv_fmt("%d h %d min", (int)(s / 3600), (int)(s % 3600 / 60)).chars;
	}
	return gv_fmt("%d dias e %d h", (int)(s / 86400), (int)(s % 86400 / 3600)).chars;
}

void pn_sobre_pc_desenhar(void) {
	if (!info_lida) {
		pn_sobre_pc_abrir();
	}
	CLAY(CLAY_ID("sb-capa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 18 }, .childGap = 18, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_ID("sb-logo"), { .layout = { .sizing = { CLAY_SIZING_FIXED(72), CLAY_SIZING_FIXED(72) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_alfa(gv_t.acento, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande) }) {
			gv_icone("lumna", 40, gv_t.acento);
		}
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4 } }) {
			gv_texto(gv_fmt("LUMNA OS %s", info.lumna), 26, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(info.host), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	uint32_t k = 0;
	linha_info(k++, "Nome do computador", info.host);
	if (info.modelo[0]) {
		linha_info(k++, "Modelo", info.modelo);
	}
	linha_info(k++, "Processador", info.cpu[0] ? gv_fmt("%s · %d núcleos", info.cpu, info.nucleos).chars : "—");
	linha_info(k++, "Memória", gv_fmt("%s (%s livres agora)", pn_tamanho(info.memoria_total), pn_tamanho(info.memoria_livre)).chars);
	if (info.gpu[0]) {
		linha_info(k++, "Placa de vídeo", info.gpu);
	}
	linha_info(k++, "Disco do sistema", gv_fmt("%s (%s livres)", pn_tamanho(info.disco_total), pn_tamanho(info.disco_livre)).chars);
	linha_info(k++, "Sistema base", gv_fmt("%s · %s", info.distro, info.arquitetura).chars);
	linha_info(k++, "Kernel", info.kernel);
	linha_info(k++, "Versão do LUMNA", info.lumna);
	linha_info(k++, "Ligado há", duracao(info.ligado_ha_s));
	CLAY(CLAY_ID("sb-copiar"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 18, 0 }, .childGap = 12 } }) {
		static double copiado_em;
		if (gv_botao(CLAY_ID("sb-copiar-bt"), "clipboard", "Copiar informações", GV_BOTAO_ELEVADO)) {
			char resumo[1500];
			sistema_resumo(&info, resumo, sizeof(resumo));
			gv_copiar(resumo);
			copiado_em = gv_agora();
		}
		if (gv_agora() - copiado_em < 2.5 && copiado_em > 0) {
			gv_texto(CLAY_STRING("Copiado: é só colar onde precisar."), gv_t.tamanho_corpo - 1, gv_t.sucesso, GV_PESO_NORMAL);
			gv_acordar_em(2.6 - (gv_agora() - copiado_em));
		}
	}
}

/* ---- Atualizações do sistema ---- */

static struct sistema_atualizacao atu[SISTEMA_MAX_ATUALIZACOES];
/* quantos snapshots de antes de uma atualização existem (a pasta é legível por todos: só os nomes) */
static int n_snapshots(void) {
	const char *pasta = g_getenv("LUMNA_SNAPDIR");
	GDir *d = g_dir_open(pasta && *pasta ? pasta : "/.snapshots", 0, NULL);
	int n = 0;
	const char *nome;
	while (d && (nome = g_dir_read_name(d))) {
		n += g_str_has_prefix(nome, "antes-");
	}
	if (d) {
		g_dir_close(d);
	}
	return n;
}

static int nesc = -2;       /* a lista "Escolher pacotes": -2: ainda não analisou; -1: não deu para analisar */
static int natu = -2;       /* -2: ainda não procurou; -1: não deu para saber (sem checkupdates nem pacman) */
static bool procurando;
static struct sistema_evento hist[24];
static int nhist = -1;
static int natu_tarefa;

static void procurar_trabalho(void *dados) {
	natu_tarefa = sistema_atualizacoes(atu, SISTEMA_MAX_ATUALIZACOES);
}

static void procurar_pronto(void *dados) {
	natu = natu_tarefa;
	procurando = false;
}

static void procurar(void) {
	if (!procurando) {
		procurando = true;
		nesc = -2; /* a análise dos pacotes é de antes: refaz quando pedirem */
		pn_tarefa(procurar_trabalho, procurar_pronto, NULL);
	}
}

/* ---- escolher pacotes ---- */
/* A lista com caixinhas: cada atualização é testada sozinha pelo lumna-pkg-analisar (sem root). Só se marca o que o teste liberou; o resto mostra o motivo. */
static struct sistema_pacote esc[SISTEMA_MAX_ATUALIZACOES];
static bool analisando, modo_escolha;
static int nesc_tarefa;

static void analisar_trabalho(void *dados) {
	nesc_tarefa = sistema_analisar_pacotes(esc, SISTEMA_MAX_ATUALIZACOES);
}

static void analisar_pronto(void *dados) {
	nesc = nesc_tarefa;
	analisando = false;
}

static void escolher_abrir(void) {
	modo_escolha = true;
	if (!analisando && nesc == -2) {
		analisando = true;
		pn_tarefa(analisar_trabalho, analisar_pronto, NULL);
	}
}

static int esc_marcados(void) {
	int n = 0;
	for (int i = 0; i < nesc && i < SISTEMA_MAX_ATUALIZACOES; i++) {
		n += esc[i].marcado && esc[i].situacao == SISTEMA_PACOTE_OK;
	}
	return n;
}

static const char *esc_situacao_texto(const struct sistema_pacote *p) {
	switch (p->situacao) {
	case SISTEMA_PACOTE_OK: return p->marcado ? "Vai atualizar" : "Pode atualizar sozinho";
	case SISTEMA_PACOTE_ARRASTA: return "Puxa outros pacotes";
	case SISTEMA_PACOTE_QUEBRA: return "Quebraria uma dependência";
	default: return "Sistema base";
	}
}

/* a caixinha: marcada (acento + visto), livre (contorno) ou bloqueada (cadeado) */
static void esc_caixa(const struct sistema_pacote *p, uint32_t i) {
	bool livre = p->situacao == SISTEMA_PACOTE_OK;
	CLAY(CLAY_IDI("es-caixa", i), { .layout = { .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) }, .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = p->marcado ? gv_t.acento : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(6),
		.border = { .color = livre ? (p->marcado ? gv_t.acento : gv_t.borda_controle) : gv_t.borda, .width = CLAY_BORDER_OUTSIDE(2) } }) {
		if (p->marcado) {
			gv_icone("check", 16, gv_t.sobre_acento);
		} else if (!livre) {
			gv_icone("lock", 14, gv_t.texto_suave);
		}
	}
}

static void esc_desenhar(void) {
	CLAY(CLAY_ID("es-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 8 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			gv_texto(CLAY_STRING("Escolha o que atualizar"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Cada pacote é testado sozinho. O que fica bloqueado precisa de \"Atualizar tudo\": o Arch não aceita atualização parcial disso."),
				gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		int marc = esc_marcados();
		if (gv_botao_ext(CLAY_ID("es-todos"), &(gv_botao_opcoes){ .icone = "check", .texto = "Marcar os liberados", .estilo = GV_BOTAO_ELEVADO, .desativado = analisando || nesc <= 0 })) {
			for (int i = 0; i < nesc; i++) {
				esc[i].marcado = esc[i].situacao == SISTEMA_PACOTE_OK;
			}
		}
		if (gv_botao_ext(CLAY_ID("es-instalar"), &(gv_botao_opcoes){ .icone = "download", .texto = marc ? gv_fmt("Atualizar %d", marc).chars : "Atualizar",
			    .estilo = GV_BOTAO_CHEIO, .desativado = marc == 0 || analisando })) {
			GString *cmd = g_string_new("pkexec /usr/lib/lumna/lumna-pkg upgrade-pacotes");
			for (int i = 0; i < nesc; i++) {
				if (esc[i].marcado && esc[i].situacao == SISTEMA_PACOTE_OK) {
					char *q = g_shell_quote(esc[i].nome);
					g_string_append_printf(cmd, " %s", q);
					g_free(q);
				}
			}
			g_string_append(cmd, " && { echo; echo 'Pronto. Os outros pacotes continuam como estavam.'; }");
			pn_terminal_atualizar(cmd->str);
			g_string_free(cmd, true);
		}
		if (gv_botao_ext(CLAY_ID("es-voltar"), &(gv_botao_opcoes){ .texto = "Voltar", .estilo = GV_BOTAO_SIMPLES })) {
			modo_escolha = false;
		}
	}
	if (analisando) {
		gv_texto(CLAY_STRING("Testando os pacotes um por um… pode levar alguns minutos."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	if (nesc < 0) {
		gv_texto(CLAY_STRING("Não consegui analisar as atualizações (falta o checkupdates ou o pacman não respondeu)."), gv_t.tamanho_corpo, gv_t.aviso, GV_PESO_NORMAL);
		return;
	}
	if (nesc == 0) {
		gv_texto(CLAY_STRING("Nada para atualizar."), gv_t.tamanho_corpo, gv_t.sucesso, GV_PESO_NORMAL);
		return;
	}
	for (int i = 0; i < nesc && i < 200; i++) {
		struct sistema_pacote *p = &esc[i];
		bool livre = p->situacao == SISTEMA_PACOTE_OK;
		Clay_ElementId id = CLAY_IDI("es-linha", (uint32_t)i);
		if (livre && gv_tocado(id)) {
			p->marcado = !p->marcado;
		}
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 10, 14, 8, 8 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = livre && gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = gv_t.borda, .width = { .bottom = 1 } } }) {
			esc_caixa(p, (uint32_t)i);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_str(p->repo[0] && strcmp(p->repo, "-") ? gv_fmt("%s  ·  %s", p->nome, p->repo).chars : p->nome), gv_t.tamanho_corpo + 1, livre ? gv_t.texto : gv_t.texto_secundario, GV_PESO_NORMAL);
				gv_texto(gv_fmt("%s  →  %s", p->antiga, p->nova), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
				if (p->motivo[0]) {
					gv_texto(gv_str(p->motivo), gv_t.tamanho_corpo - 2, livre ? gv_t.texto_suave : gv_t.aviso, GV_PESO_NORMAL);
				}
			}
			gv_texto(gv_str(esc_situacao_texto(p)), gv_t.tamanho_corpo - 1, livre ? (p->marcado ? gv_t.sucesso : gv_t.texto_secundario) : gv_t.aviso, GV_PESO_NORMAL);
		}
	}
}

/* ---- a atualização agendada ---- */
static struct sistema_agenda ag;
static bool ag_lida, ag_ocupada, ag_erro;
static char ag_aviso[260];
static const char *const ag_dia_curto[7] = { "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb", "Dom" };
static const char *const ag_dia_val[7] = { "seg", "ter", "qua", "qui", "sex", "sab", "dom" };
static char ag_hora_txt[48][6];
static const char *ag_hora_ptr[48];
static bool ag_iniciada;

/* os dias marcados em ag.dias ("todos" ou "seg,qua,sex") */
static void ag_dias_marcados(bool m[7]) {
	for (int i = 0; i < 7; i++) {
		m[i] = !strcmp(ag.dias, "todos");
	}
	char **partes = g_strsplit(ag.dias, ",", -1);
	for (char **q = partes; *q; q++) {
		for (int i = 0; i < 7; i++) {
			if (!strcmp(*q, ag_dia_val[i])) {
				m[i] = true;
			}
		}
	}
	g_strfreev(partes);
}

/* os dias marcados como o lumna-pkg entende: "todos" se são os sete, senão a lista */
static void ag_dias_texto(const bool m[7], char *saida, size_t tam) {
	int n = 0;
	saida[0] = '\0';
	for (int i = 0; i < 7; i++) {
		if (m[i]) {
			n++;
			g_strlcat(saida, saida[0] ? "," : "", tam);
			g_strlcat(saida, ag_dia_val[i], tam);
		}
	}
	if (n == 7) {
		g_strlcpy(saida, "todos", tam);
	}
}

static void ag_ler(void) {
	sistema_agenda_ler(&ag);
	ag_lida = true;
}

struct ag_pedido {
	char args[4][40];
	bool ok;
	char erro[240];
};

static const char *ag_ajudante(void) {
	const char *e = g_getenv("LUMNA_PKG");
	return e && *e ? e : "/usr/lib/lumna/lumna-pkg";
}

/* o lumna-pkg agendar, com root (pkexec: a senha é pedida na hora), numa tarefa */
static void ag_trabalho(void *dados) {
	struct ag_pedido *p = dados;
	char *argv[10];
	int k = 0;
	if (!g_getenv("LUMNA_PKG_SEM_PKEXEC")) { /* testes: sem a senha do administrador */
		argv[k++] = "pkexec";
	}
	argv[k++] = (char *)ag_ajudante();
	argv[k++] = "agendar";
	for (int i = 0; i < 4; i++) {
		argv[k++] = p->args[i];
	}
	argv[k] = NULL;
	char *err = NULL;
	int estado = 0;
	bool rodou = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL, NULL, &err, &estado, NULL);
	p->ok = rodou && WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
	g_strlcpy(p->erro, err ? g_strstrip(err) : "", sizeof(p->erro));
	g_free(err);
}

static void ag_pronto(void *dados) {
	struct ag_pedido *p = dados;
	if (p->ok) {
		ag_aviso[0] = '\0';
	} else {
		g_snprintf(ag_aviso, sizeof(ag_aviso), "Não deu para agendar: %s", p->erro[0] ? p->erro : "faltou a senha do administrador.");
	}
	ag_erro = !p->ok;
	ag_ocupada = false;
	ag_ler();
	g_free(p);
}

static void ag_aplicar(bool ligado, const char *escopo, const char *hora, const char *dias) {
	if (ag_ocupada) {
		return;
	}
	struct ag_pedido *p = g_new0(struct ag_pedido, 1);
	g_strlcpy(p->args[0], ligado ? "ligado" : "desligado", sizeof(p->args[0]));
	g_strlcpy(p->args[1], escopo, sizeof(p->args[1]));
	g_strlcpy(p->args[2], hora, sizeof(p->args[2]));
	g_strlcpy(p->args[3], dias, sizeof(p->args[3]));
	ag_ocupada = true;
	pn_tarefa(ag_trabalho, ag_pronto, p);
}

static const char *ag_resultado_texto(void) {
	if (!ag.ultimo) {
		return "Ainda não rodou.";
	}
	const char *oq = !strcmp(ag.ultimo_escopo, "lumna") ? "só o LUMNA" : "tudo";
	if (!strcmp(ag.resultado, "ok")) {
		return gv_fmt("Última vez: %s (%s) — atualizou%s%s", pn_dia(ag.ultimo), oq, ag.pacotes[0] ? ": " : ".", ag.pacotes).chars;
	}
	if (!strcmp(ag.resultado, "nada")) {
		return gv_fmt("Última vez: %s (%s) — não havia nada para atualizar.", pn_dia(ag.ultimo), oq).chars;
	}
	if (!strcmp(ag.resultado, "precisa-completa")) {
		return gv_fmt("Última vez: %s — o LUMNA pedia pacotes do Arch mais novos; nada foi instalado. Use \"Atualizar tudo\".", pn_dia(ag.ultimo)).chars;
	}
	return gv_fmt("Última vez: %s (%s) — falhou. O motivo está em /var/log/lumna-atualizar.log.", pn_dia(ag.ultimo), oq).chars;
}

static void ag_desenhar(void) {
	if (!ag_iniciada) {
		ag_iniciada = true;
		for (int i = 0; i < 48; i++) {
			snprintf(ag_hora_txt[i], sizeof(ag_hora_txt[i]), "%02d:%02d", i / 2, (i % 2) * 30);
			ag_hora_ptr[i] = ag_hora_txt[i];
		}
	}
	if (!ag_lida) {
		ag_ler();
	}
	pn_subtitulo(CLAY_ID("at-ag-t"), "Atualização agendada");
	pn_linha_comecar(CLAY_ID("at-ag"), "clock", "Atualizar sozinho no horário marcado",
		ag.ligado ? "O LUMNA tira um snapshot antes (dá para desfazer) e atualiza no horário; se o PC estiver desligado, atualiza ao ligar."
			  : "Desligado: você atualiza quando quiser. Ligue para escolher um horário.");
	bool lig = ag.ligado;
	if (gv_interruptor(CLAY_ID("at-ag-v"), &lig) && !ag_ocupada) {
		ag_aplicar(lig, ag.escopo, ag.hora, ag.dias);
	}
	pn_linha_acabar();
	if (ag.ligado) {
		int esc = !strcmp(ag.escopo, "lumna") ? 1 : 0;
		pn_linha_comecar(CLAY_ID("at-ag-esc"), "download", "O que atualizar",
			esc ? "Só os pacotes do LUMNA; o resto do Arch fica como está (se faltar algo do Arch, nada é instalado e a página avisa)." : "O sistema inteiro (o LUMNA e o Arch).");
		static const char *const escopos[] = { "Tudo", "Só o LUMNA" };
		if (pn_segmentos(CLAY_ID("at-ag-seg"), escopos, 2, &esc) && !ag_ocupada) {
			ag_aplicar(true, esc ? "lumna" : "tudo", ag.hora, ag.dias);
		}
		pn_linha_acabar();
		/* os dias: sete botões (quais quiser); sempre fica ao menos um */
		bool marc[7];
		ag_dias_marcados(marc);
		pn_linha_comecar(CLAY_ID("at-ag-dias"), "clock", "Dias", NULL);
		for (int i = 0; i < 7; i++) {
			if (gv_botao_ext(CLAY_IDI("at-ag-dia", (uint32_t)i), &(gv_botao_opcoes){ .texto = ag_dia_curto[i], .estilo = marc[i] ? GV_BOTAO_CHEIO : GV_BOTAO_ELEVADO }) && !ag_ocupada) {
				bool novo[7];
				memcpy(novo, marc, sizeof(novo));
				novo[i] = !novo[i];
				int qtos = 0;
				for (int k = 0; k < 7; k++) {
					qtos += novo[k];
				}
				if (qtos > 0) {
					char dias[40];
					ag_dias_texto(novo, dias, sizeof(dias));
					ag_aplicar(true, ag.escopo, ag.hora, dias);
				}
			}
		}
		pn_linha_acabar();
		int hora = 6;
		for (int i = 0; i < 48; i++) {
			if (!strcmp(ag.hora, ag_hora_txt[i])) {
				hora = i;
			}
		}
		if (pn_ajuste_lista(CLAY_ID("at-ag-hora"), "clock", "Horário", NULL, ag_hora_ptr, 48, &hora) && !ag_ocupada) {
			ag_aplicar(true, ag.escopo, ag_hora_txt[hora], ag.dias);
		}
	}
	if (ag_aviso[0]) {
		gv_texto(gv_str(ag_aviso), gv_t.tamanho_corpo - 1, ag_erro ? gv_t.aviso : gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	gv_texto(gv_str(ag_resultado_texto()), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
}

void pn_sobre_atu_abrir(void) {
	nhist = sistema_historico(hist, 24);
	ag_ler();
}

static int geracao_vista = -1;

void pn_sobre_atu_desenhar(void) {
	if (nhist < 0) {
		pn_sobre_atu_abrir();
	}
	/* o terminal de atualizar terminou: procura de novo (os botões voltam a "sem atualização") e relê o histórico */
	int g = pn_atualizacao_geracao();
	if (geracao_vista < 0) {
		geracao_vista = g;
	} else if (g != geracao_vista) {
		geracao_vista = g;
		natu = -2;
		nesc = -2;
		modo_escolha = false;
		nhist = sistema_historico(hist, 24);
		procurar();
	}
	CLAY(CLAY_ID("at-topo"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 14 }, .childGap = 12 } }) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 3 } }) {
			if (procurando) {
				gv_texto(CLAY_STRING("Procurando atualizações…"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(CLAY_STRING("Isso pode levar alguns segundos."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			} else if (natu == -2) {
				gv_texto(CLAY_STRING("Ainda não procurei"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(CLAY_STRING("Toque em Procurar para ver o que há de novo no sistema."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			} else if (natu < 0) {
				gv_texto(CLAY_STRING("Não consegui procurar"), gv_t.tamanho_destaque, gv_t.aviso, GV_PESO_MEDIO);
				gv_texto(CLAY_STRING("Faltou o checkupdates (pacote pacman-contrib) ou o pacman não respondeu."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			} else if (natu == 0) {
				gv_texto(CLAY_STRING("Tudo em dia"), gv_t.tamanho_destaque, gv_t.sucesso, GV_PESO_MEDIO);
			} else {
				int dolumna = 0;
				for (int i = 0; i < natu && i < SISTEMA_MAX_ATUALIZACOES; i++) {
					dolumna += atu[i].lumna;
				}
				gv_texto(gv_fmt("%d atualizações disponíveis", natu), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_fmt("%d do LUMNA e %d do Arch e de outros programas", dolumna, natu - dolumna), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
		}
		CLAY(CLAY_ID("at-botoes"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		if (gv_botao_ext(CLAY_ID("at-procurar"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Procurar", .estilo = GV_BOTAO_ELEVADO, .desativado = procurando })) {
			procurar();
		}
		int dolumna_n = 0;
		for (int i = 0; i < natu && i < SISTEMA_MAX_ATUALIZACOES; i++) {
			dolumna_n += atu[i].lumna;
		}
		if (natu > 0 && gv_botao(CLAY_ID("at-escolher"), "check", "Escolher", GV_BOTAO_ELEVADO)) {
			escolher_abrir();
		}
		if (dolumna_n > 0 && gv_botao(CLAY_ID("at-lumna"), "download", "Só o LUMNA", GV_BOTAO_ELEVADO)) {
			/* só os pacotes do [lumna]; se isso exigir pacotes do Arch mais novos, o lumna-pkg não instala nada e diz para atualizar tudo */
			pn_terminal_atualizar("pkexec /usr/lib/lumna/lumna-pkg upgrade-lumna && { echo; echo 'Pronto. Se o desktop mudou, saia da sessão e entre de novo.'; }");
		}
		if (natu > 0 && gv_botao(CLAY_ID("at-instalar"), "download", "Atualizar tudo", GV_BOTAO_CHEIO)) {
			/* num terminal, para ver o que o pacman faz. O lumna-pkg tira um snapshot do sistema antes (dá para voltar), e o yay, se
			 * houver, cuida do que vem do AUR */
			pn_terminal_atualizar("pkexec /usr/lib/lumna/lumna-pkg upgrade && { if command -v yay >/dev/null 2>&1; then yay -Sua; fi; echo; "
				    "echo 'Atualizado. Se o desktop mudou, saia da sessão e entre de novo para usar a versão nova.'; }");
		}
		/* se há snapshots de antes das atualizações (o lumna-pkg deixa os 5 últimos), dá para desfazer a última */
		if (n_snapshots() > 0 && pn_confirmar(CLAY_ID("at-voltar"), "restore", "Desfazer a última atualização",
			    "Voltar o sistema ao estado de antes da última atualização? (reinicia depois)", true)) {
			pn_terminal("pkexec /usr/lib/lumna/lumna-pkg voltar");
		}
		}
	}
	if (modo_escolha) {
		esc_desenhar();
		return;
	}
	ag_desenhar();
	if (natu > 0) {
		for (int i = 0; i < natu && i < 40; i++) {
			pn_linha_comecar(CLAY_IDI("at-linha", (uint32_t)i), NULL, atu[i].lumna ? gv_fmt("%s  ·  LUMNA", atu[i].nome).chars : atu[i].nome, NULL);
			gv_texto(gv_fmt("%s  →  %s", atu[i].antiga, atu[i].nova), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			pn_linha_acabar();
		}
		if (natu > 40) {
			gv_texto(gv_fmt("e mais %d", natu - 40), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	pn_subtitulo(CLAY_ID("at-hist"), "Histórico");
	if (nhist <= 0) {
		gv_texto(CLAY_STRING("Nada registrado ainda (o histórico vem do pacman.log)."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	for (int i = 0; i < nhist; i++) {
		const struct sistema_evento *e = &hist[i];
		pn_linha_comecar(CLAY_IDI("at-h", (uint32_t)i), NULL, gv_fmt("%s %s", e->nome, e->acao).chars, pn_dia(e->quando));
		gv_texto(gv_str(e->de[0] ? gv_fmt("%s  →  %s", e->de, e->para).chars : e->para), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		pn_linha_acabar();
	}
}

/* ---- Avisos legais ---- */

void pn_sobre_legal_desenhar(void) {
	static const struct { const char *nome, *licenca; } itens[] = {
		{ "LUMNA OS (Orbit, Gravity e o shell)", "Os termos do projeto LUMNA" },
		{ "wlroots e Wayland", "MIT" },
		{ "Clay (o layout da interface)", "zlib" },
		{ "GLib, Pango, Cairo, GdkPixbuf, libsoup", "LGPL 2.1 ou mais nova" },
		{ "xkbcommon", "MIT" },
		{ "PulseAudio / PipeWire (cliente)", "LGPL 2.1 ou mais nova" },
		{ "NetworkManager e BlueZ (usados por D-Bus)", "GPL 2 ou mais nova" },
		{ "Mozilla Firefox (a Mídia)", "MPL 2.0" },
		{ "Linux", "GPL 2" },
	};
	gv_texto(CLAY_STRING("O LUMNA é feito de software livre de muita gente. Estes são os principais, com a licença de cada um. "
		"O texto completo de cada licença vem com o pacote do programa, em /usr/share/licenses."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	for (size_t i = 0; i < sizeof(itens) / sizeof(itens[0]); i++) {
		pn_linha_comecar(CLAY_IDI("lg", (uint32_t)i), NULL, itens[i].nome, NULL);
		gv_texto(gv_str(itens[i].licenca), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		pn_linha_acabar();
	}
	CLAY(CLAY_ID("lg-pasta"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 16, 0 } } }) {
		if (gv_botao(CLAY_ID("lg-abrir"), "folder", "Abrir a pasta das licenças", GV_BOTAO_ELEVADO)) {
			pn_executar("xdg-open /usr/share/licenses");
		}
	}
}
