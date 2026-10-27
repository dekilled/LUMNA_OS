/*
 * Armazenamento, como o do Android: o disco do sistema numa barra (aplicativos, lixeira, temporários, cache
 * de pacotes, o resto e o livre), os aplicativos que mais ocupam (tocar leva à página do app em Aplicativos),
 * a limpeza (lixeira, temporários, cache do pacman, sempre com confirmação) e os outros discos e pendrives.
 */
#include "painel.h"

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "armazenamento.h"
#include "pacotes.h"

static struct armazenamento arm;
static bool lido, lendo;
static char mensagem[300];
static bool mensagem_erro;
static bool ocupado;

static struct armazenamento arm_novo;

static void ler_trabalho(void *dados) {
	armazenamento_ler(&arm_novo);
}

static void ler_pronto(void *dados) {
	arm = arm_novo;
	lido = true;
	lendo = false;
}

static void ler(void) {
	if (!lendo) {
		lendo = true;
		pn_tarefa(ler_trabalho, ler_pronto, NULL);
	}
}

void pn_armaz_abrir(void) {
	/* sem pedir tarefas aqui: ao abrir direto numa seção a janela ainda não existe; quem desenha começa a ler */
	mensagem[0] = '\0';
	lido = false;
}

/* ---- ações (numa tarefa, com o resultado na mensagem) ---- */

enum acao { A_LIXEIRA, A_TEMP, A_CACHE, A_MONTAR, A_EJETAR };

struct pedido {
	enum acao acao;
	struct disco disco;
	int codigo;
	char erro[300];
};

static void acao_trabalho(void *dados) {
	struct pedido *p = dados;
	switch (p->acao) {
	case A_LIXEIRA: p->codigo = armazenamento_esvaziar_lixeira(p->erro, sizeof(p->erro)); break;
	case A_TEMP: p->codigo = armazenamento_limpar_temporarios(p->erro, sizeof(p->erro)); break;
	case A_CACHE: p->codigo = armazenamento_limpar_cache_pacman(p->erro, sizeof(p->erro)); break;
	case A_MONTAR: p->codigo = armazenamento_montar(&p->disco, p->erro, sizeof(p->erro)); break;
	case A_EJETAR: p->codigo = armazenamento_ejetar(&p->disco, p->erro, sizeof(p->erro)); break;
	}
}

static void acao_pronto(void *dados) {
	struct pedido *p = dados;
	static const char *const feito[] = { "Lixeira esvaziada.", "Arquivos temporários apagados.", "Cache de pacotes limpo.", "Disco montado.",
		"Pode tirar o disco com segurança." };
	if (p->codigo == 0) {
		g_strlcpy(mensagem, feito[p->acao], sizeof(mensagem));
		mensagem_erro = false;
	} else {
		g_snprintf(mensagem, sizeof(mensagem), "Não deu certo: %s", p->erro[0] ? p->erro : "o sistema recusou (a senha foi cancelada?)");
		mensagem_erro = true;
	}
	ocupado = false;
	g_free(p);
	ler();
}

static void pedir(enum acao a, const struct disco *d) {
	struct pedido *p = g_new0(struct pedido, 1);
	p->acao = a;
	if (d) {
		p->disco = *d;
	}
	ocupado = true;
	mensagem[0] = '\0';
	pn_tarefa(acao_trabalho, acao_pronto, p);
}

static void aviso(void) {
	if (mensagem[0]) {
		gv_texto(gv_str(mensagem), gv_t.tamanho_corpo - 1, mensagem_erro ? gv_t.perigo : gv_t.sucesso, GV_PESO_NORMAL);
	}
}

/* ---- Uso do disco ---- */

static int comparar_tamanho(const void *a, const void *b) {
	int64_t x = (*(const struct pacote *const *)a)->tamanho, y = (*(const struct pacote *const *)b)->tamanho;
	return x < y ? 1 : x > y ? -1 : 0;
}

static void segmento(uint32_t k, int64_t bytes, int64_t total, Clay_Color cor) {
	float f = total > 0 ? (float)((double)bytes / (double)total) : 0;
	if (f <= 0.001f) {
		return;
	}
	CLAY(CLAY_IDI("ar-seg", k), { .layout = { .sizing = { CLAY_SIZING_PERCENT(f), CLAY_SIZING_GROW(0) } }, .backgroundColor = cor }) {}
}

static void legenda(uint32_t k, Clay_Color cor, const char *nome, int64_t bytes) {
	CLAY(CLAY_IDI("ar-leg", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 5, 5 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY(CLAY_IDI("ar-leg-c", k), { .layout = { .sizing = { CLAY_SIZING_FIXED(12), CLAY_SIZING_FIXED(12) } }, .backgroundColor = cor, .cornerRadius = CLAY_CORNER_RADIUS(6) }) {}
		CLAY(CLAY_IDI("ar-leg-n", k), { .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_texto(gv_str(nome), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		gv_texto(gv_str(pn_tamanho(bytes)), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

void pn_armaz_uso_desenhar(void) {
	if (!lido && !lendo) {
		ler();
	}
	if (!lido) {
		gv_texto(CLAY_STRING("Medindo o disco…"), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	const struct pacote **apps = NULL;
	int napps = pn_apps_dados(&apps);
	int64_t dos_apps = 0;
	for (int i = 0; i < napps; i++) {
		dos_apps += apps[i]->tamanho;
	}
	int64_t resto = arm.usado - dos_apps - arm.lixeira - arm.temporarios - arm.cache_pacman;
	if (resto < 0) {
		resto = 0;
	}
	Clay_Color c_apps = gv_t.acento, c_lix = gv_t.aviso, c_tmp = gv_t.perigo, c_cache = gv_t.sucesso, c_resto = gv_alfa(gv_t.texto_suave, 0.55f);

	gv_texto(gv_fmt("%s usados", pn_tamanho(arm.usado)), 28, gv_t.texto, GV_PESO_MEDIO);
	gv_texto(gv_fmt("de %s · %s livres", pn_tamanho(arm.total), pn_tamanho(arm.livre)), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	CLAY(CLAY_ID("ar-folga"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(8) } } }) {}
	CLAY(CLAY_ID("ar-barra"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(18) } }, .backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(9),
		.clip = { .horizontal = true } }) {
		segmento(0, dos_apps, arm.total, c_apps);
		segmento(1, arm.cache_pacman, arm.total, c_cache);
		segmento(2, arm.temporarios, arm.total, c_tmp);
		segmento(3, arm.lixeira, arm.total, c_lix);
		segmento(4, resto, arm.total, c_resto);
	}
	CLAY(CLAY_ID("ar-legendas"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 10, 14 } } }) {
		legenda(0, c_apps, "Aplicativos", dos_apps);
		legenda(1, c_cache, "Cache de pacotes", arm.cache_pacman);
		legenda(2, c_tmp, "Arquivos temporários", arm.temporarios);
		legenda(3, c_lix, "Lixeira", arm.lixeira);
		legenda(4, c_resto, "Seus arquivos e o sistema", resto);
		legenda(5, gv_t.elevada, "Livre", arm.livre);
	}
	aviso();

	pn_subtitulo(CLAY_ID("ar-ap-t"), "Aplicativos que mais ocupam");
	if (!napps) {
		gv_texto(CLAY_STRING("Lendo os aplicativos…"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		return;
	}
	const struct pacote **v = g_new(const struct pacote *, napps);
	memcpy(v, apps, sizeof(v[0]) * (size_t)napps);
	qsort(v, (size_t)napps, sizeof(v[0]), comparar_tamanho);
	for (int i = 0; i < napps && i < 12; i++) {
		const struct pacote *p = v[i];
		Clay_ElementId id = CLAY_IDI("ar-app", (uint32_t)i);
		if (gv_tocado(id)) {
			pn_apps_selecionar(p->nome);
			pn_ir("aplicativos/instalados");
		}
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 10, 14, 8, 8 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = gv_por_cima(id) ? gv_t.sob_cursor : (Clay_Color){ 0 }, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio) }) {
			gv_focavel(id, gv_t.raio_medio);
			gv_icone_app(p->icone, 36);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_str(p->app_nome[0] ? p->app_nome : p->nome), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_NORMAL);
				gv_texto(gv_str(p->origem == PAC_APPIMAGE ? "AppImage" : p->fonte), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			gv_texto(gv_str(pn_tamanho(p->tamanho)), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			gv_icone("forward", 18, gv_t.texto_suave);
		}
	}
	g_free(v);
}

/* ---- Limpeza ---- */

static void limpeza_linha(uint32_t k, const char *icone, const char *nome, const char *sub, int64_t bytes, enum acao a, const char *aviso_texto) {
	pn_linha_comecar(CLAY_IDI("ar-lp", k), icone, nome, sub);
	gv_texto(gv_str(pn_tamanho(bytes)), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	if (ocupado || bytes <= 0) {
		gv_botao_ext(CLAY_IDI("ar-lp-n", k), &(gv_botao_opcoes){ .icone = "trash", .texto = "Limpar", .estilo = GV_BOTAO_ELEVADO, .desativado = true });
	} else if (pn_confirmar(CLAY_IDI("ar-lp-b", k), "trash", "Limpar", aviso_texto, true)) {
		pedir(a, NULL);
	}
	pn_linha_acabar();
}

void pn_armaz_limpeza_desenhar(void) {
	if (!lido && !lendo) {
		ler();
	}
	if (!lido) {
		gv_texto(CLAY_STRING("Medindo o que dá para limpar…"), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	aviso();
	if (ocupado) {
		gv_texto(CLAY_STRING("Limpando… confirme a senha se o sistema pedir."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
	limpeza_linha(0, "trash", "Lixeira", "Os arquivos que você apagou. Não dá para recuperar depois.", arm.lixeira, A_LIXEIRA, "Apagar de vez?");
	limpeza_linha(1, "refresh", "Arquivos temporários", "O cache dos programas e os restos em /tmp. Eles se refazem sozinhos.", arm.temporarios, A_TEMP,
		"Apagar o cache?");
	limpeza_linha(2, "download", "Cache de pacotes", "Pacotes baixados de versões que já saíram. Os que estão em uso ficam.", arm.cache_pacman, A_CACHE,
		"Limpar o cache?");
	if (gv_botao_ext(CLAY_ID("ar-reler"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Medir de novo", .estilo = GV_BOTAO_SIMPLES, .desativado = lendo || ocupado })) {
		ler();
	}
}

/* ---- Discos e pendrives ---- */

void pn_armaz_discos_desenhar(void) {
	if (!lido && !lendo) {
		ler();
	}
	if (!lido) {
		gv_texto(CLAY_STRING("Procurando discos…"), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	aviso();
	if (!arm.ndiscos) {
		gv_texto(CLAY_STRING("Não encontrei discos (falta o lsblk?)."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		return;
	}
	for (int i = 0; i < arm.ndiscos; i++) {
		const struct disco *d = &arm.discos[i];
		char sub[400];
		if (d->montado) {
			g_snprintf(sub, sizeof(sub), "%s · %s de %s em uso · %s", d->fs, pn_tamanho(d->usado), pn_tamanho(d->tamanho), d->ponto);
		} else {
			g_snprintf(sub, sizeof(sub), "%s · %s · não montado", d->fs, pn_tamanho(d->tamanho));
		}
		pn_linha_comecar(CLAY_IDI("ar-dk", (uint32_t)i), d->removivel ? "usb" : "drive", d->nome, sub);
		if (d->montado && d->ponto[0]) {
			if (gv_botao(CLAY_IDI("ar-dk-ab", (uint32_t)i), NULL, "Abrir", GV_BOTAO_SIMPLES)) {
				char *cmd = g_strdup_printf("xdg-open %s", g_shell_quote(d->ponto));
				pn_executar(cmd);
				g_free(cmd);
			}
		}
		if (d->do_sistema) {
			gv_texto(CLAY_STRING("do sistema"), gv_t.tamanho_corpo - 2, gv_t.texto_suave, GV_PESO_NORMAL);
		} else if (!d->montado) {
			if (gv_botao_ext(CLAY_IDI("ar-dk-m", (uint32_t)i), &(gv_botao_opcoes){ .texto = "Montar", .estilo = GV_BOTAO_ELEVADO, .desativado = ocupado })) {
				pedir(A_MONTAR, d);
			}
		} else if (gv_botao_ext(CLAY_IDI("ar-dk-e", (uint32_t)i), &(gv_botao_opcoes){ .texto = d->removivel ? "Ejetar" : "Desmontar", .estilo = GV_BOTAO_ELEVADO,
				.desativado = ocupado })) {
			pedir(A_EJETAR, d);
		}
		pn_linha_acabar();
	}
}
