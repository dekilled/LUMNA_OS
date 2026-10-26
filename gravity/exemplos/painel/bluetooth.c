/*
 * Bluetooth: Dispositivos (ligar, procurar, parear, conectar, esquecer) e Nome e visibilidade, pelo
 * BlueZ (servicos/bluetooth.h). Sem Aplicar: o que se faz vale na hora. A topbar guarda o liga/desliga
 * rápido; aqui fica o resto.
 */
#include "painel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bluetooth.h"
#include "rede.h"

#define N(lista) (int)(sizeof(lista) / sizeof(lista[0]))

static bool servico_ligado;
static char nome_novo[64];
static char nome_adapt[64];
static int adapt_escolhido;

static void servico_mudou(gv_app *app, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	gv_app_redesenhar(app);
}

static void garantir(void) {
	if (!servico_ligado && pn_app) {
		int fd = bt_iniciar();
		if (fd >= 0) {
			gv_app_vigiar(pn_app, fd, servico_mudou, NULL);
		}
		servico_ligado = true;
	}
}

void pn_bt_abrir(void) {
	garantir();
	if (servico_ligado) {
		bt_erro_limpar();
	}
	nome_adapt[0] = '\0';
}

static void banner(const struct bt_estado *e) {
	if (!e->erro[0]) {
		return;
	}
	CLAY(CLAY_ID("bt-erro"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 14, 8, 10, 10 }, .childGap = 10,
			.childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
		.backgroundColor = gv_alfa(gv_t.perigo, 0.14f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_pequeno),
		.border = { .color = gv_alfa(gv_t.perigo, 0.5f), .width = GV_BORDA(1) } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			char curto[160];
			snprintf(curto, sizeof(curto), "%s", e->erro);
			char *nl = strchr(curto, '\n');
			if (nl) {
				*nl = '\0';
			}
			gv_texto(gv_str(gv_fmt("%s", curto).chars), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		if (gv_botao(CLAY_ID("bt-erro-x"), "close", NULL, GV_BOTAO_SIMPLES)) {
			bt_erro_limpar();
		}
	}
}

/* o que mostrar quando falta o BlueZ ou o adaptador */
static bool sem_adaptador(const struct bt_estado *e) {
	if (!e->bluez) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 6 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("O serviço de Bluetooth não está respondendo"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("O Bluetooth usa o BlueZ, que vem no Arch. Para ligar:"), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("sudo pacman -S --needed bluez bluez-utils"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
			gv_texto(CLAY_STRING("sudo systemctl enable --now bluetooth"), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		}
		return true;
	}
	if (!e->nadapt) {
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 6 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_texto(CLAY_STRING("Nenhum adaptador Bluetooth encontrado"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(CLAY_STRING("Este computador não tem Bluetooth, ou ele não foi reconhecido. Conecte um adaptador USB e ele aparece aqui sozinho."),
				gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		return true;
	}
	return false;
}

static const char *icone_do_tipo(const char *icone) {
	if (!strncmp(icone, "audio", 5)) {
		return "headphones";
	}
	if (!strncmp(icone, "input-keyboard", 14)) {
		return "keyboard";
	}
	if (!strncmp(icone, "input-mouse", 11)) {
		return "mouse";
	}
	if (!strncmp(icone, "input-gaming", 12)) {
		return "gamepad";
	}
	if (!strncmp(icone, "phone", 5)) {
		return "phone";
	}
	if (!strncmp(icone, "computer", 8)) {
		return "computer";
	}
	return "bluetooth";
}

static void disp_linha(const struct bt_disp *d, int i) {
	pn_linha_comecar(CLAY_IDI("btd", (uint32_t)i), icone_do_tipo(d->icone), d->nome[0] ? d->nome : d->endereco,
		gv_fmt("%s%s%s%s", bt_tipo_nome(d->icone), d->conectado ? " · conectado" : d->pareado ? " · pareado" : "",
			d->bateria >= 0 ? gv_fmt(" · bateria %d%%", d->bateria).chars : "",
			!d->pareado && d->sinal ? gv_fmt(" · sinal %d dBm", d->sinal).chars : "").chars);
	if (d->conectado) {
		if (gv_botao(CLAY_IDI("btd-desc", (uint32_t)i), NULL, "Desconectar", GV_BOTAO_PERIGO)) {
			bt_desconectar(d->caminho);
		}
	} else if (d->pareado) {
		if (gv_botao(CLAY_IDI("btd-con", (uint32_t)i), NULL, "Conectar", GV_BOTAO_ELEVADO)) {
			bt_conectar(d->caminho);
		}
	} else if (gv_botao(CLAY_IDI("btd-par", (uint32_t)i), NULL, "Parear", GV_BOTAO_ELEVADO)) {
		bt_parear(d->caminho);
	}
	if (d->pareado && gv_botao(CLAY_IDI("btd-esq", (uint32_t)i), "trash", NULL, GV_BOTAO_SIMPLES)) {
		bt_esquecer(d->caminho);
	}
	pn_linha_acabar();
}

static const struct bt_adapt *adaptador(const struct bt_estado *e) {
	if (adapt_escolhido >= e->nadapt) {
		adapt_escolhido = 0;
	}
	return &e->adapt[adapt_escolhido];
}

static void escolher_adaptador(const struct bt_estado *e) {
	if (e->nadapt < 2) {
		return;
	}
	const char *nomes[BT_MAX_ADAPT];
	for (int i = 0; i < e->nadapt; i++) {
		nomes[i] = gv_fmt("%s (%s)", e->adapt[i].nome, rede_barramento_nome(e->adapt[i].barramento)).chars;
	}
	pn_linha_comecar(CLAY_ID("bt-qual"), "usb", "Adaptador", "Qual mostrar e usar");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(300) } } }) {
		gv_lista_suspensa(CLAY_ID("bt-qual-v"), nomes, e->nadapt, &adapt_escolhido);
	}
	pn_linha_acabar();
}

void pn_bt_dispositivos_desenhar(void) {
	garantir();
	struct bt_estado e;
	bt_estado_copiar(&e);
	banner(&e);
	if (sem_adaptador(&e)) {
		return;
	}
	escolher_adaptador(&e);
	const struct bt_adapt *a = adaptador(&e);
	pn_linha_comecar(CLAY_ID("bt-lig"), "bluetooth", "Bluetooth", a->ligado ? "Ligado" : "Desligado");
	bool ligado = a->ligado;
	if (gv_interruptor(CLAY_ID("bt-lig-v"), &ligado)) {
		bt_ligar(a->caminho, ligado);
	}
	pn_linha_acabar();
	if (!a->ligado) {
		gv_texto(CLAY_STRING("Ligue o Bluetooth para ver e conectar aparelhos."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
		return;
	}
	CLAY(CLAY_ID("bt-titulo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 18, 8 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Seus aparelhos"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {}
		if (gv_botao(CLAY_ID("bt-procurar"), a->procurando ? "close" : "refresh", a->procurando ? "Parar de procurar" : "Procurar aparelhos", GV_BOTAO_ELEVADO)) {
			bt_procurar(a->caminho, !a->procurando);
		}
	}
	int pareados = 0;
	for (int i = 0; i < e.ndisp; i++) {
		if (!strcmp(e.disp[i].adapt, a->caminho) && e.disp[i].pareado) {
			disp_linha(&e.disp[i], i);
			pareados++;
		}
	}
	if (!pareados) {
		gv_texto(CLAY_STRING("Nenhum aparelho pareado ainda."), gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
	}
	CLAY(CLAY_ID("bt-perto-t"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 18, 8 }, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		gv_texto(CLAY_STRING("Por perto"), gv_t.tamanho_destaque, gv_t.texto, GV_PESO_MEDIO);
		if (a->procurando) {
			gv_texto(CLAY_STRING("  procurando..."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
	int perto = 0;
	for (int i = 0; i < e.ndisp; i++) {
		if (!strcmp(e.disp[i].adapt, a->caminho) && !e.disp[i].pareado) {
			disp_linha(&e.disp[i], i);
			perto++;
		}
	}
	if (!perto) {
		gv_texto(a->procurando ? CLAY_STRING("Nada por perto ainda. Deixe o aparelho em modo de pareamento.")
				      : CLAY_STRING("Toque em Procurar aparelhos."),
			gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
	}
}

void pn_bt_nome_desenhar(void) {
	garantir();
	struct bt_estado e;
	bt_estado_copiar(&e);
	banner(&e);
	if (sem_adaptador(&e)) {
		return;
	}
	escolher_adaptador(&e);
	const struct bt_adapt *a = adaptador(&e);
	if (!nome_adapt[0] || strcmp(nome_adapt, a->caminho)) {
		snprintf(nome_adapt, sizeof(nome_adapt), "%s", a->caminho);
		snprintf(nome_novo, sizeof(nome_novo), "%s", a->nome);
	}
	pn_linha_comecar(CLAY_ID("bt-nome"), "edit", "O nome deste computador", "Como os outros aparelhos o veem");
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(240) } } }) {
		gv_campo(CLAY_ID("bt-nome-v"), nome_novo, sizeof(nome_novo), "Nome", GV_CAMPO_TEXTO);
	}
	if (gv_botao_ext(CLAY_ID("bt-nome-ok"), &(gv_botao_opcoes){ .texto = "Salvar", .estilo = GV_BOTAO_ELEVADO,
		    .desativado = !nome_novo[0] || !strcmp(nome_novo, a->nome) })) {
		bt_renomear(a->caminho, nome_novo);
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("bt-vis"), "eye", "Visível para parear", "Outros aparelhos conseguem achar este computador");
	bool visivel = a->visivel;
	if (gv_interruptor(CLAY_ID("bt-vis-v"), &visivel)) {
		bt_visivel(a->caminho, visivel);
	}
	pn_linha_acabar();
	pn_linha_comecar(CLAY_ID("bt-hw"), a->barramento[0] && !strcmp(a->barramento, "USB") ? "usb" : "computer", "O adaptador",
		gv_fmt("%s · %s", a->endereco, rede_barramento_nome(a->barramento)).chars);
	pn_linha_acabar();
}

static const pn_secao bluetooth[] = {
	{ "dispositivos", "bluetooth", "Dispositivos", "Os aparelhos pareados e os que estão por perto.", pn_bt_dispositivos_desenhar, NULL, NULL, NULL,
		NULL, NULL, pn_bt_abrir },
	{ "visibilidade", "eye", "Nome e visibilidade", "Como os outros aparelhos veem este.", pn_bt_nome_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_bt_abrir },
	{ "arquivos", "download", "Receber arquivos", "Arquivos mandados por Bluetooth.", NULL, "Aceitar e onde guardar" },
};
const pn_categoria pn_bluetooth = { "bluetooth", "bluetooth", "Bluetooth", "Dispositivos e acessórios", PN_GERAL, bluetooth, N(bluetooth) };
