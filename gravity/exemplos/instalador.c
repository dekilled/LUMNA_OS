/*
 * lumna-instalador: instala o LUMNA OS no disco, a partir do pendrive (o "Instalar LUMNA OS" da sessão live). Seis passos, como
 * no instalador antigo: Boas-vindas, Região (idioma, teclado e fuso), Disco (com o aviso de que ele é apagado inteiro), Você
 * (nome, usuário, senha e nome do computador), Resumo e a Instalação com o andamento. Quem instala é o
 * /usr/lib/lumna/lumna-install, pelo pkexec (o lumna-polkit pede a senha do administrador); aqui só se junta o que ele precisa
 * (servicos/instalador.h). Só instala no disco inteiro e só em UEFI, como o script.
 *
 *   lumna-instalador [--passo N]    abrir já num passo (para testar)
 */
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "gravity.h"
#include "instalador.h"

enum { P_BOASVINDAS, P_REGIAO, P_DISCO, P_VOCE, P_RESUMO, P_INSTALACAO, N_PASSOS };
static const char *const nomes_passos[N_PASSOS] = { "Boas-vindas", "Região", "Disco", "Você", "Resumo", "Instalação" };

static const char *const idiomas_nome[] = { "Português (Brasil)", "English (US)" };
static const char *const idiomas_id[] = { "pt_BR.UTF-8", "en_US.UTF-8" };
static const char *const teclados_nome[] = { "Português (ABNT2)", "Inglês (EUA)", "Inglês internacional (com acentos)" };
static const char *const teclados_id[] = { "br abnt2", "us", "us intl" };

static gv_app *app;
static int passo, idioma, teclado;
static char fuso[64] = "America/Sao_Paulo";
static char **fusos;
static int nfusos;
static struct inst_disco discos[16];
static int ndiscos = -1, escolhido = -1;
static bool entendi;
static char nome[128], usuario[40], senha[128], confirma[128], host[64] = "lumna";
static bool usuario_mexido;
static bool instalando;

static void carregar_discos(void) {
	ndiscos = instalador_discos(discos, 16);
	if (escolhido >= ndiscos) {
		escolhido = -1;
	}
}

static const char *tamanho_texto(int64_t bytes) {
	return bytes >= 1000000000000LL ? gv_fmt("%.1f TB", (double)bytes / 1e12).chars : gv_fmt("%.0f GB", (double)bytes / 1e9).chars;
}

static const char *transporte(const char *t) {
	return !strcmp(t, "nvme") ? "NVMe" : !strcmp(t, "sata") ? "SATA" : !strcmp(t, "usb") ? "USB" : !strcmp(t, "mmc") ? "Cartão" : t;
}

static bool nome_preenchido(void) {
	char *n = g_strstrip(g_strdup(nome));
	bool ok = n[0] != '\0';
	g_free(n);
	return ok;
}

static bool pode_continuar(void) {
	switch (passo) {
	case P_REGIAO:
		return fuso[0] && instalador_fuso_existe(fusos, nfusos, fuso);
	case P_DISCO:
		return escolhido >= 0 && entendi;
	case P_VOCE:
		return nome_preenchido() && usuario[0] && !*instalador_erro_usuario(usuario) && senha[0] && !strcmp(senha, confirma)
			&& !*instalador_erro_senha(senha, confirma) && !*instalador_erro_host(host);
	}
	return true;
}

static void instalar(void) {
	struct inst_plano p = { 0 };
	g_strlcpy(p.disco, discos[escolhido].caminho, sizeof(p.disco));
	g_strlcpy(p.usuario, usuario, sizeof(p.usuario));
	char *n = g_strstrip(g_strdup(nome));
	g_strlcpy(p.nome, n, sizeof(p.nome));
	g_free(n);
	g_strlcpy(p.host, host, sizeof(p.host));
	g_strlcpy(p.fuso, fuso, sizeof(p.fuso));
	g_strlcpy(p.teclado, teclados_id[teclado], sizeof(p.teclado));
	g_strlcpy(p.idioma, idiomas_id[idioma], sizeof(p.idioma));
	passo = P_INSTALACAO;
	instalando = instalador_iniciar(&p, senha);
}

static void ao_andar(gv_app *a, int fd, void *dados) {
	uint64_t v;
	if (read(fd, &v, sizeof(v)) < 0) {
	}
	gv_app_redesenhar(a);
}

/* ---- peças ---- */

static void titulo(const char *t, const char *sub) {
	gv_texto(gv_str(t), gv_t.tamanho_titulo, gv_t.texto, GV_PESO_SEMINEGRITO);
	if (sub) {
		gv_texto(gv_str(sub), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	}
}

static void rotulo_campo(const char *t) {
	gv_texto(gv_str(t), gv_t.tamanho_corpo - 1, gv_t.texto_secundario, GV_PESO_MEDIO);
}

static void erro_campo(const char *t) {
	if (t[0]) {
		gv_texto(gv_str(t), gv_t.tamanho_corpo - 2, gv_t.perigo, GV_PESO_NORMAL);
	}
}

/* ---- os passos ---- */

static void p_boasvindas(void) {
	CLAY(CLAY_ID("bv"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 18,
		.childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
		gv_icone("lumna", 84, gv_t.acento);
		gv_texto(CLAY_STRING("Bem-vindo ao LUMNA OS"), gv_t.tamanho_titulo + 6, gv_t.texto, GV_PESO_SEMINEGRITO);
		CLAY(CLAY_ID("bv-t"), { .layout = { .sizing = { CLAY_SIZING_FIXED(480) } } }) {
			gv_texto(CLAY_STRING("Você está usando o LUMNA direto do pendrive. Para deixá-lo no computador, este assistente prepara um disco, copia o "
					     "sistema e cria a sua conta. Leva alguns minutos e não precisa de internet."), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		CLAY(CLAY_ID("bv-b"), { .layout = { .childGap = 12 } }) {
			if (gv_botao_ext(CLAY_ID("bv-sair"), &(gv_botao_opcoes){ .texto = "Continuar testando", .estilo = GV_BOTAO_ELEVADO })) {
				gv_app_sair(app);
			}
			if (gv_botao_ext(CLAY_ID("bv-ir"), &(gv_botao_opcoes){ .texto = "Instalar", .estilo = GV_BOTAO_CHEIO })) {
				passo = P_REGIAO;
			}
		}
	}
}

static void p_regiao(void) {
	titulo("Região", "Idioma, teclado e fuso horário do sistema que vai ser instalado.");
	rotulo_campo("Idioma");
	gv_lista_suspensa(CLAY_ID("rg-idioma"), idiomas_nome, 2, &idioma);
	rotulo_campo("Teclado");
	gv_lista_suspensa(CLAY_ID("rg-teclado"), teclados_nome, 3, &teclado);
	rotulo_campo("Fuso horário");
	gv_campo(CLAY_ID("rg-fuso"), fuso, sizeof(fuso), "America/Sao_Paulo", GV_CAMPO_BUSCA);
	if (fuso[0] && !instalador_fuso_existe(fusos, nfusos, fuso)) {
		/* sugestões: os que têm o que foi digitado, no máximo seis */
		char *busca = g_ascii_strdown(fuso, -1);
		int mostrados = 0;
		for (int i = 0; i < nfusos && mostrados < 6; i++) {
			char *baixo = g_ascii_strdown(fusos[i], -1);
			if (strstr(baixo, busca)) {
				if (gv_botao_ext(CLAY_IDI("rg-sug", (uint32_t)i), &(gv_botao_opcoes){ .texto = fusos[i], .estilo = GV_BOTAO_SIMPLES, .altura = 32 })) {
					g_strlcpy(fuso, fusos[i], sizeof(fuso));
				}
				mostrados++;
			}
			g_free(baixo);
		}
		g_free(busca);
		if (!mostrados) {
			erro_campo("Fuso desconhecido.");
		}
	}
}

static void p_disco(void) {
	titulo("Disco", "Onde instalar. O disco escolhido é apagado inteiro.");
	if (ndiscos < 0) {
		carregar_discos();
	}
	if (ndiscos == 0) {
		gv_texto(CLAY_STRING("Não achei nenhum disco para instalar (o pendrive do LUMNA não conta)."), gv_t.tamanho_corpo, gv_t.aviso, GV_PESO_NORMAL);
	}
	for (int i = 0; i < ndiscos; i++) {
		bool sel = escolhido == i;
		Clay_ElementId id = CLAY_IDI("dk", (uint32_t)i);
		CLAY(id, { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 16, 16, 12, 12 }, .childGap = 14, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
			.backgroundColor = sel ? gv_t.selecionado : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio),
			.border = { .color = sel ? gv_t.acento : gv_t.borda_controle, .width = GV_BORDA(sel ? 2 : 1) } }) {
			bool ativou = gv_focavel(id, gv_t.raio_medio);
			gv_icone(!strcmp(discos[i].transporte, "usb") ? "usb" : "drive", 28, sel ? gv_t.acento : gv_t.icone);
			CLAY_AUTO_ID({ .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 2 } }) {
				gv_texto(gv_fmt("%s · %s", discos[i].modelo, tamanho_texto(discos[i].tamanho)), gv_t.tamanho_corpo + 1, gv_t.texto, GV_PESO_MEDIO);
				gv_texto(gv_fmt("%s · %s · %s", discos[i].caminho, transporte(discos[i].transporte), discos[i].conteudo), gv_t.tamanho_corpo - 2, gv_t.texto_secundario, GV_PESO_NORMAL);
			}
			if (sel) {
				gv_icone("check", 22, gv_t.acento);
			}
			if (gv_tocado(id) || ativou) {
				escolhido = i;
				entendi = false;
			}
		}
	}
	if (gv_botao_ext(CLAY_ID("dk-reler"), &(gv_botao_opcoes){ .icone = "refresh", .texto = "Procurar de novo", .estilo = GV_BOTAO_SIMPLES, .altura = 32 })) {
		carregar_discos();
	}
	if (escolhido >= 0) {
		CLAY(CLAY_ID("dk-aviso"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(14), .childGap = 10 },
			.backgroundColor = gv_alfa(gv_t.perigo, 0.12f), .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_medio), .border = { .color = gv_alfa(gv_t.perigo, 0.5f), .width = GV_BORDA(1) } }) {
			gv_texto(gv_fmt("Tudo em %s (%s) será apagado e não dá para desfazer.", discos[escolhido].modelo, discos[escolhido].caminho), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_MEDIO);
			gv_linha(CLAY_ID("dk-entendi"), "info", "Entendi: apagar este disco inteiro", NULL, &entendi);
		}
	}
}

static void p_voce(void) {
	titulo("Você", "A sua conta no computador.");
	rotulo_campo("Seu nome");
	if (gv_campo(CLAY_ID("vc-nome"), nome, sizeof(nome), "Maria da Silva", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU && !usuario_mexido) {
		instalador_sugerir_usuario(nome, usuario, sizeof(usuario));
	}
	rotulo_campo("Nome de usuário");
	if (gv_campo(CLAY_ID("vc-usuario"), usuario, sizeof(usuario), "maria", GV_CAMPO_TEXTO) & GV_CAMPO_MUDOU) {
		usuario_mexido = true;
	}
	erro_campo(instalador_erro_usuario(usuario));
	rotulo_campo("Senha");
	gv_campo(CLAY_ID("vc-senha"), senha, sizeof(senha), "Senha", GV_CAMPO_SENHA);
	rotulo_campo("Repita a senha");
	gv_campo(CLAY_ID("vc-confirma"), confirma, sizeof(confirma), "Repita a senha", GV_CAMPO_SENHA);
	erro_campo(instalador_erro_senha(senha, confirma));
	rotulo_campo("Nome do computador");
	gv_campo(CLAY_ID("vc-host"), host, sizeof(host), "lumna", GV_CAMPO_TEXTO);
	erro_campo(instalador_erro_host(host));
}

static void linha_resumo(const char *rotulo, const char *valor) {
	CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 0, 0, 6, 6 }, .childGap = 16 } }) {
		CLAY_AUTO_ID({ .layout = { .sizing = { CLAY_SIZING_FIXED(150) } } }) {
			gv_texto(gv_str(rotulo), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
		}
		gv_texto(gv_str(valor), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
	}
}

static void p_resumo(void) {
	titulo("Resumo", "Confira antes de começar.");
	linha_resumo("Disco", gv_fmt("%s (%s), apagado inteiro", discos[escolhido].modelo, discos[escolhido].caminho).chars);
	linha_resumo("Conta", gv_fmt("%s (%s)", nome, usuario).chars);
	linha_resumo("Computador", host);
	linha_resumo("Idioma e teclado", gv_fmt("%s · %s", idiomas_nome[idioma], teclados_nome[teclado]).chars);
	linha_resumo("Fuso horário", fuso);
	gv_texto(CLAY_STRING("Não desligue o computador nem tire o pendrive durante a instalação. O administrador vai pedir a sua senha antes de começar."),
		gv_t.tamanho_corpo - 1, gv_t.texto_suave, GV_PESO_NORMAL);
	if (gv_botao_ext(CLAY_ID("rs-instalar"), &(gv_botao_opcoes){ .icone = "download", .texto = "Instalar agora", .estilo = GV_BOTAO_CHEIO })) {
		instalar();
	}
}

static void reiniciar(void) {
	char *a[] = { "systemctl", "reboot", NULL };
	g_spawn_async(NULL, a, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
}

static void p_instalacao(void) {
	struct inst_estado e;
	instalador_estado(&e);
	bool falhou = e.terminou && !e.ok, pronto = e.terminou && e.ok;
	titulo(pronto ? "Pronto!" : falhou ? "A instalação parou" : "Instalando o LUMNA OS", pronto ? "O LUMNA está no disco." : falhou ? NULL : "Isto leva alguns minutos. Não desligue o computador.");
	if (!falhou) {
		CLAY(CLAY_ID("in-trilho"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(10) } }, .backgroundColor = gv_t.elevada,
			.cornerRadius = CLAY_CORNER_RADIUS(5) }) {
			CLAY(CLAY_ID("in-barra"), { .layout = { .sizing = { CLAY_SIZING_PERCENT(e.percentual / 100.0f), CLAY_SIZING_GROW(0) } }, .backgroundColor = pronto ? gv_t.sucesso : gv_t.acento,
				.cornerRadius = CLAY_CORNER_RADIUS(5) }) {}
		}
		gv_texto(gv_fmt("%d%% · %s", e.percentual, e.texto), gv_t.tamanho_corpo, gv_t.texto_secundario, GV_PESO_NORMAL);
	} else {
		gv_texto(gv_str(e.erro), gv_t.tamanho_corpo, gv_t.perigo, GV_PESO_NORMAL);
	}
	if (pronto) {
		gv_texto(CLAY_STRING("Tire o pendrive e reinicie para entrar no LUMNA instalado."), gv_t.tamanho_corpo, gv_t.texto, GV_PESO_NORMAL);
		CLAY(CLAY_ID("in-b"), { .layout = { .childGap = 12 } }) {
			if (gv_botao_ext(CLAY_ID("in-depois"), &(gv_botao_opcoes){ .texto = "Continuar testando", .estilo = GV_BOTAO_ELEVADO })) {
				gv_app_sair(app);
			}
			if (gv_botao_ext(CLAY_ID("in-reiniciar"), &(gv_botao_opcoes){ .icone = "power", .texto = "Reiniciar agora", .estilo = GV_BOTAO_CHEIO })) {
				reiniciar();
			}
		}
	}
	if (falhou && gv_botao_ext(CLAY_ID("in-voltar"), &(gv_botao_opcoes){ .texto = "Voltar", .estilo = GV_BOTAO_ELEVADO })) {
		passo = P_RESUMO;
		instalando = false;
	}
}

static void desenhar(gv_app *a, void *dados) {
	CLAY(CLAY_ID("janela"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
		.backgroundColor = gv_t.superficie, .cornerRadius = CLAY_CORNER_RADIUS(gv_t.raio_grande), .border = { .color = gv_t.borda, .width = GV_BORDA(1) } }) {
		gv_barra_janela(a, CLAY_ID("barra"), "download", "Instalar o LUMNA OS");
		CLAY(CLAY_ID("corpo"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
			/* os passos, à esquerda */
			CLAY(CLAY_ID("passos"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_FIXED(210), CLAY_SIZING_GROW(0) }, .padding = CLAY_PADDING_ALL(20), .childGap = 14 },
				.backgroundColor = gv_t.fundo }) {
				for (int i = 0; i < N_PASSOS; i++) {
					bool ativo = i == passo, feito = i < passo;
					CLAY(CLAY_IDI("ps", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .childGap = 10, .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } } }) {
						CLAY(CLAY_IDI("ps-p", (uint32_t)i), { .layout = { .sizing = { CLAY_SIZING_FIXED(24), CLAY_SIZING_FIXED(24) }, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } },
							.backgroundColor = ativo ? gv_t.acento : feito ? gv_alfa(gv_t.acento, 0.35f) : gv_t.elevada, .cornerRadius = CLAY_CORNER_RADIUS(12) }) {
							if (feito) {
								gv_icone("check", 14, gv_t.texto);
							} else {
								gv_texto(gv_fmt("%d", i + 1), gv_t.tamanho_corpo - 2, ativo ? gv_t.sobre_acento : gv_t.texto_secundario, GV_PESO_MEDIO);
							}
						}
						gv_texto(gv_str(nomes_passos[i]), gv_t.tamanho_corpo, ativo ? gv_t.texto : gv_t.texto_secundario, ativo ? GV_PESO_MEDIO : GV_PESO_NORMAL);
					}
				}
			}
			/* o passo */
			Clay_ElementId conteudo = CLAY_ID("conteudo");
			CLAY(CLAY_ID("direita"), { .layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {
				CLAY(conteudo, gv_rolagem(conteudo, GV_ROLAGEM_VERTICAL, (Clay_ElementDeclaration){
					.layout = { .layoutDirection = CLAY_TOP_TO_BOTTOM, .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .padding = { 36, 36, 28, 12 }, .childGap = 10 } })) {
					switch (passo) {
					case P_BOASVINDAS: p_boasvindas(); break;
					case P_REGIAO: p_regiao(); break;
					case P_DISCO: p_disco(); break;
					case P_VOCE: p_voce(); break;
					case P_RESUMO: p_resumo(); break;
					default: p_instalacao(); break;
					}
				}
				if (passo > P_BOASVINDAS && passo < P_INSTALACAO) {
					CLAY(CLAY_ID("rodape"), { .layout = { .sizing = { CLAY_SIZING_GROW(0) }, .padding = { 36, 36, 14, 18 }, .childGap = 10, .childAlignment = { .x = CLAY_ALIGN_X_RIGHT } } }) {
						if (gv_botao_ext(CLAY_ID("voltar"), &(gv_botao_opcoes){ .texto = "Voltar", .estilo = GV_BOTAO_ELEVADO })) {
							passo--;
						}
						if (passo < P_RESUMO && gv_botao_ext(CLAY_ID("continuar"), &(gv_botao_opcoes){ .texto = "Continuar", .estilo = GV_BOTAO_CHEIO, .desativado = !pode_continuar() })) {
							passo++;
						}
					}
				}
			}
		}
	}
}

int main(int argc, char **argv) {
	for (int i = 1; i + 1 < argc; i++) {
		if (!strcmp(argv[i], "--passo")) {
			passo = atoi(argv[i + 1]);
		}
	}
	nfusos = instalador_fusos(&fusos);
	app = gv_app_criar(&(gv_opcoes){ .titulo = "Instalar o LUMNA OS", .app_id = "lumna-instalador", .largura = 960, .altura = 660 });
	if (!app) {
		return 1;
	}
	gv_app_vigiar(app, instalador_fd(), ao_andar, NULL);
	return gv_app_correr(app, desenhar, NULL);
}
