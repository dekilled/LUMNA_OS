/*
 * Sistema → Usuários:
 *   Sua conta         o nome, a foto (~/.face) e se é administrador
 *   Senha e PIN       trocar a senha do Linux (no terminal, pelo passwd) e o PIN do LUMNA
 *   Outros usuários   ver, adicionar, tirar e dar ou tirar o poder de administrador (servicos/contas.h, pelo pkexec)
 */
#define _GNU_SOURCE
#include "painel.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contas.h"

void pn_pin_secao(void);

static struct conta contas[CONTAS_MAX];
static int ncontas;
static char aviso[300];
static bool aviso_erro, ocupado;

static void nota(const char *id, const char *texto, bool erro) {
	if (texto && *texto) {
		CLAY(CLAY_SID(gv_fmt("%s", id)), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 4 } } }) {
			gv_texto(gv_str(texto), gv_t.tamanho_corpo - 1, erro ? gv_t.perigo : gv_t.texto_secundario, GV_PESO_NORMAL);
		}
	}
}

static char nome_campo[128];

void pn_usuarios_abrir(void) {
	ncontas = contas_listar(contas, CONTAS_MAX);
	aviso[0] = '\0';
	g_strlcpy(nome_campo, ncontas > 0 && contas[0].atual ? contas[0].nome : "", sizeof(nome_campo));
	pn_suas_carregar();
}

/* uma mudança de conta, numa tarefa (o pkexec espera a senha do administrador) */
struct pedido {
	int op; /* 1 adicionar, 2 remover, 3 admin, 4 nome */
	char login[64], nome[128], senha[256];
	bool flag;
	bool ok;
	char erro[240];
};

static void pedido_trabalho(void *d) {
	struct pedido *p = d;
	switch (p->op) {
	case 1: p->ok = contas_adicionar(p->login, p->nome, p->senha, p->flag, p->erro, sizeof(p->erro)); break;
	case 2: p->ok = contas_remover(p->login, p->flag, p->erro, sizeof(p->erro)); break;
	case 3: p->ok = contas_admin(p->login, p->flag, p->erro, sizeof(p->erro)); break;
	case 4: p->ok = contas_nome(p->login, p->nome, p->erro, sizeof(p->erro)); break;
	}
	memset(p->senha, 0, sizeof(p->senha));
}

static void pedido_pronto(void *d) {
	struct pedido *p = d;
	g_strlcpy(aviso, p->ok ? "Pronto." : p->erro, sizeof(aviso));
	aviso_erro = !p->ok;
	ocupado = false;
	ncontas = contas_listar(contas, CONTAS_MAX);
	g_free(p);
}

static void pedir(int op, const char *login, const char *nome, const char *senha, bool flag) {
	if (ocupado) {
		return;
	}
	struct pedido *p = g_new0(struct pedido, 1);
	p->op = op;
	g_strlcpy(p->login, login ? login : "", sizeof(p->login));
	g_strlcpy(p->nome, nome ? nome : "", sizeof(p->nome));
	g_strlcpy(p->senha, senha ? senha : "", sizeof(p->senha));
	p->flag = flag;
	ocupado = true;
	aviso[0] = '\0';
	pn_tarefa(pedido_trabalho, pedido_pronto, p);
}

/* ---- Sua conta ---- */

void pn_conta_desenhar(void) {
	const struct conta *eu = ncontas > 0 && contas[0].atual ? &contas[0] : NULL;
	const char *foto = contas_foto();
	CLAY(CLAY_ID("co-topo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 8, 18 }, .childGap = 20, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		Clay_ElementId f = CLAY_ID("co-foto");
		if (foto) {
			CLAY(f, gv_imagem(f, foto, GV_AJUSTE_COBRIR, (Clay_ElementDeclaration){
				.layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(96) } }, .cornerRadius = CLAY_CORNER_RADIUS(48) })) {}
		} else {
			CLAY(f, { .layout = { .sizing = { CLAY_SIZING_FIXED(96), CLAY_SIZING_FIXED(96) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(48), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
				gv_icone("user", 48, gv_t.icone);
			}
		}
		CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .childGap = 4 } }) {
			gv_texto(gv_str(eu && eu->nome[0] ? eu->nome : contas_eu()), 26, gv_t.texto, GV_PESO_MEDIO);
			gv_texto(gv_str(contas_eu()), gv_t.tamanho_corpo + 1, gv_t.texto_secundario, GV_PESO_NORMAL);
			gv_texto(gv_str(eu && eu->admin ? "Administrador" : "Usuário comum"), gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
		}
	}
	pn_subtitulo(CLAY_ID("co-s0"), "Foto");
	pn_linha_comecar(CLAY_ID("co-foto-l"), "image", "A sua foto", "Aparece no Painel e na tela de bloqueio");
	if (gv_botao(CLAY_ID("co-foto-escolher"), NULL, "Escolher uma", GV_BOTAO_ELEVADO)) {
		pn_suas_carregar();
		gv_popup_alternar(CLAY_ID("co-suas"));
	}
	if (foto && gv_botao(CLAY_ID("co-foto-tirar"), NULL, "Tirar", GV_BOTAO_SIMPLES)) {
		contas_foto_tirar();
		g_strlcpy(aviso, "A foto saiu.", sizeof(aviso));
		aviso_erro = false;
	}
	pn_linha_acabar();
	const char *escolhida = pn_suas_popup(CLAY_ID("co-suas"), CLAY_ID("co-foto-escolher"), NULL);
	if (escolhida) {
		char erro[200] = "";
		aviso_erro = !contas_foto_definir(escolhida, erro, sizeof(erro));
		g_strlcpy(aviso, aviso_erro ? erro : "Foto trocada.", sizeof(aviso));
	}
	pn_subtitulo(CLAY_ID("co-s1"), "Nome");
	CLAY(CLAY_ID("co-nome-caixa"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 4, 4, 4, 4 }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) } } }) {
			gv_campo(CLAY_ID("co-nome"), nome_campo, sizeof(nome_campo), "O seu nome completo", GV_CAMPO_TEXTO);
		}
		if (gv_botao_ext(CLAY_ID("co-nome-salvar"), &(gv_botao_opcoes){ .texto = "Salvar", .estilo = GV_BOTAO_CHEIO, .desativado = ocupado || !eu || !strcmp(nome_campo, eu->nome) })) {
			pedir(4, contas_eu(), nome_campo, NULL, false);
		}
	}
	if (ocupado) {
		nota("co-ocupado", "Esperando a senha do administrador...", false);
	}
	nota("co-aviso", aviso, aviso_erro);
}

/* ---- Senha e PIN ---- */

void pn_senha_desenhar(void) {
	pn_subtitulo(CLAY_ID("se-s0"), "Senha do Linux");
	pn_linha_comecar(CLAY_ID("se-senha"), "lock", "Trocar a senha", "Abre um terminal e pede a senha de agora e a nova (é a senha de entrar e de desbloquear)");
	if (gv_botao(CLAY_ID("se-senha-b"), NULL, "Trocar", GV_BOTAO_ELEVADO)) {
		pn_terminal("passwd");
	}
	pn_linha_acabar();
	pn_subtitulo(CLAY_ID("se-s1"), "PIN do LUMNA");
	pn_pin_secao();
	nota("se-dica", "O PIN desbloqueia a tela de bloqueio. Para entrar no computador (a tela de login) vale a senha do Linux.", false);
}

/* ---- Outros usuários ---- */

static bool adicionando, novo_admin;
static char n_nome[128], n_login[64], n_senha[128], n_senha2[128];
static char confirmar_remocao[64];

void pn_outros_desenhar(void) {
	int outros = 0;
	for (int i = 0; i < ncontas; i++) {
		const struct conta *c = &contas[i];
		if (c->atual) {
			continue;
		}
		outros++;
		pn_linha_comecar(CLAY_IDI("ou-linha", i), "user", c->nome[0] ? c->nome : c->login, gv_fmt("%s · %s", c->login, c->admin ? "Administrador" : "Usuário comum").chars);
		bool adm = c->admin;
		if (gv_interruptor(CLAY_IDI("ou-adm", i), &adm) && !ocupado) {
			pedir(3, c->login, NULL, NULL, adm);
		}
		if (strcmp(confirmar_remocao, c->login)) {
			if (gv_botao(CLAY_IDI("ou-tira", i), "trash", "Tirar", GV_BOTAO_PERIGO)) {
				g_strlcpy(confirmar_remocao, c->login, sizeof(confirmar_remocao));
			}
		}
		pn_linha_acabar();
		if (!strcmp(confirmar_remocao, c->login)) {
			CLAY(CLAY_IDI("ou-conf", i), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 10 },
				.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.perigo, .width = GV_BORDA(1) } }) {
				gv_texto(gv_str(gv_fmt("Tirar a conta de %s?", c->nome[0] ? c->nome : c->login).chars), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(CLAY_STRING("Os arquivos da pessoa podem ficar no computador ou ser apagados junto."), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_NORMAL);
				CLAY_AUTO_ID({ .layout = { .childGap = 10 } }) {
					if (gv_botao(CLAY_IDI("ou-conf-manter", i), NULL, "Tirar e guardar os arquivos", GV_BOTAO_ELEVADO)) {
						pedir(2, c->login, NULL, NULL, false);
						confirmar_remocao[0] = '\0';
					}
					if (gv_botao(CLAY_IDI("ou-conf-apagar", i), "trash", "Tirar e apagar tudo", GV_BOTAO_PERIGO)) {
						pedir(2, c->login, NULL, NULL, true);
						confirmar_remocao[0] = '\0';
					}
					if (gv_botao(CLAY_IDI("ou-conf-nao", i), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
						confirmar_remocao[0] = '\0';
					}
				}
			}
		}
	}
	if (outros == 0) {
		nota("ou-vazio", "Só você usa este computador.", false);
	}
	pn_subtitulo(CLAY_ID("ou-s1"), "Adicionar");
	if (!adicionando) {
		if (gv_botao(CLAY_ID("ou-novo"), "plus", "Adicionar um usuário", GV_BOTAO_ELEVADO)) {
			adicionando = true;
			n_nome[0] = n_login[0] = n_senha[0] = n_senha2[0] = '\0';
			novo_admin = false;
		}
	} else {
		bool senhas_iguais = n_senha[0] && !strcmp(n_senha, n_senha2);
		CLAY(CLAY_ID("ou-form"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(16), .childGap = 10 },
			.backgroundColor = gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
			gv_campo(CLAY_ID("ou-f-nome"), n_nome, sizeof(n_nome), "Nome completo", GV_CAMPO_TEXTO);
			gv_campo(CLAY_ID("ou-f-login"), n_login, sizeof(n_login), "Nome de usuário (letras minúsculas e números)", GV_CAMPO_TEXTO);
			gv_campo(CLAY_ID("ou-f-senha"), n_senha, sizeof(n_senha), "Senha", GV_CAMPO_SENHA);
			gv_campo(CLAY_ID("ou-f-senha2"), n_senha2, sizeof(n_senha2), "Repita a senha", GV_CAMPO_SENHA);
			pn_linha_comecar(CLAY_ID("ou-f-adm"), "shield", "Administrador", "Pode instalar programas e mudar o sistema");
			gv_interruptor(CLAY_ID("ou-f-adm-v"), &novo_admin);
			pn_linha_acabar();
			if (n_senha2[0] && !senhas_iguais) {
				gv_texto(CLAY_STRING("As senhas não são iguais."), gv_t.tamanho_corpo - 1, gv_t.perigo, GV_PESO_NORMAL);
			}
			CLAY_AUTO_ID({ .layout = { .childGap = 10 } }) {
				if (gv_botao_ext(CLAY_ID("ou-f-criar"), &(gv_botao_opcoes){ .texto = "Criar", .estilo = GV_BOTAO_CHEIO, .desativado = ocupado || !n_login[0] || !senhas_iguais })) {
					pedir(1, n_login, n_nome, n_senha, novo_admin);
					memset(n_senha, 0, sizeof(n_senha));
					memset(n_senha2, 0, sizeof(n_senha2));
					adicionando = false;
				}
				if (gv_botao(CLAY_ID("ou-f-cancelar"), NULL, "Cancelar", GV_BOTAO_SIMPLES)) {
					memset(n_senha, 0, sizeof(n_senha));
					memset(n_senha2, 0, sizeof(n_senha2));
					adicionando = false;
				}
			}
		}
	}
	if (ocupado) {
		nota("ou-ocupado", "Esperando a senha do administrador...", false);
	}
	nota("ou-aviso", aviso, aviso_erro);
}
