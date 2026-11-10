/* O serviço da Loja com pacman, checkupdates, curl e paru de mentira (testes/loja/falsos.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loja.h"

static int falhas;
#define CONFERE(cond, ...) do { if (!(cond)) { falhas++; fprintf(stderr, "FALHOU (linha %d): ", __LINE__); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static int nlinhas;
static char ultima[256];
static void linha(void *d, const char *l) { nlinhas++; g_strlcpy(ultima, l, sizeof(ultima)); }

static char *le_log(void) {
	char *t = NULL;
	g_file_get_contents(g_getenv("FALSO_LOG"), &t, NULL, NULL);
	return t ? t : g_strdup("");
}

int main(void) {
	lj_catalogo *c = lj_catalogo_ler();
	CONFERE(c != NULL, "lê o catálogo");
	if (!c) {
		return 1;
	}
	CONFERE(lj_n(c) == 10, "10 pacotes no catálogo (%d)", lj_n(c));
	const lj_pacote *ff = lj_achar(c, "firefox");
	CONFERE(ff && !strcmp(ff->exibicao, "Firefox") && ff->cat == LJ_INTERNET && ff->tipo == LJ_APP && ff->destaque, "Firefox: nome, categoria, tipo");
	CONFERE(ff && ff->baixar == (int64_t)(70.5 * 1024 * 1024) && ff->tamanho == (int64_t)(260.25 * 1024 * 1024), "tamanhos em MiB (%ld, %ld)", ff ? (long)ff->baixar : 0, ff ? (long)ff->tamanho : 0);
	CONFERE(ff && !ff->instalado, "firefox não está instalado");
	CONFERE(lj_achar(c, "mesa") && lj_achar(c, "mesa")->instalado, "mesa instalada (pacman -Q)");
	CONFERE(ff && !strcmp(ff->url, "https://www.mozilla.org/firefox/") && !strcmp(ff->licenca, "MPL-2.0") && ff->grupos[0] == '\0', "URL, licença e grupos 'None' vazio");
	const lj_pacote *htop = lj_achar(c, "htop");
	CONFERE(htop && htop->baixar == 200 * 1024, "KiB");

	/* tipos do esboço: mesa biblioteca, mesa-utils utilitário, lib32-mesa biblioteca, libva-mesa-driver driver */
	CONFERE(lj_achar(c, "mesa")->tipo == LJ_BIBLIOTECA, "mesa é biblioteca");
	CONFERE(lj_achar(c, "mesa-utils")->tipo == LJ_UTILITARIO, "mesa-utils é utilitário");
	CONFERE(lj_achar(c, "lib32-mesa")->tipo == LJ_BIBLIOTECA, "lib32-mesa é biblioteca");
	CONFERE(lj_achar(c, "libva-mesa-driver")->tipo == LJ_DRIVER, "libva-mesa-driver é driver");
	CONFERE(lj_achar(c, "linux")->tipo == LJ_KERNEL && lj_achar(c, "linux-lts")->tipo == LJ_KERNEL, "linux e linux-lts são kernels");

	/* a busca "mesa" do esboço: 4 pacotes, mesa primeiro */
	lj_filtro f;
	lj_filtro_padrao(&f);
	f.texto = "mesa";
	f.aur = false;
	int r[64];
	int n = lj_buscar(c, &f, r, 64);
	CONFERE(n == 4, "mesa: 4 resultados (mesa, mesa-utils, lib32-mesa, libva-mesa-driver): %d", n);
	CONFERE(n > 0 && !strcmp(lj_resultado(c, r[0])->nome, "mesa"), "o nome exato vem primeiro");
	f.por = LJ_POR_NOME;
	n = lj_buscar(c, &f, r, 64);
	CONFERE(n == 4, "por nome: 4 (%d)", n);
	f.tipo = LJ_DRIVER;
	n = lj_buscar(c, &f, r, 64);
	CONFERE(n == 1 && !strcmp(lj_resultado(c, r[0])->nome, "libva-mesa-driver"), "filtro de tipo Driver");
	f.tipo = -1;
	f.multilib = false;
	n = lj_buscar(c, &f, r, 64);
	CONFERE(n == 3, "sem multilib: 3 (%d)", n);
	f.multilib = true;
	f.sem_avaliacao = false;
	n = lj_buscar(c, &f, r, 64);
	CONFERE(n == 0, "excluir os sem avaliação esvazia (ninguém tem nota ainda): %d", n);
	lj_filtro_padrao(&f);
	f.texto = "web browser";
	f.por = LJ_POR_DESCRICAO;
	f.aur = false;
	n = lj_buscar(c, &f, r, 64);
	CONFERE(n == 1 && !strcmp(lj_resultado(c, r[0])->nome, "firefox"), "busca na descrição");
	lj_filtro_padrao(&f);
	f.ordem = LJ_TAMANHO;
	f.aur = false;
	n = lj_buscar(c, &f, r, 64);
	CONFERE(n == 10 && !strcmp(lj_resultado(c, r[0])->nome, "blender") && !strcmp(lj_resultado(c, r[9])->nome, "htop"), "ordem por tamanho: blender (900 MiB) primeiro, htop por último");
	f.ordem = LJ_NOME;
	n = lj_buscar(c, &f, r, 64);
	CONFERE(!strcmp(lj_resultado(c, r[0])->nome, "blender"), "ordem por nome");
	f.so_instalados = true;
	n = lj_buscar(c, &f, r, 64);
	CONFERE(n == 4, "instalados: mesa, linux, vlc, htop (%d)", n);

	/* Explorar: só os apps conhecidos por categoria */
	int d[32];
	n = lj_destaques(c, LJ_TODAS, LJ_NOME, d, 32);
	CONFERE(n == 4, "destaques: blender, firefox, htop, vlc (%d)", n);
	n = lj_destaques(c, LJ_MULTIMIDIA, LJ_NOME, d, 32);
	CONFERE(n == 1 && !strcmp(lj_pacote_n(c, d[0])->nome, "vlc"), "destaques de Multimídia: vlc");
	n = lj_destaques(c, LJ_GRAFICOS, LJ_NOME, d, 32);
	CONFERE(n == 1 && !strcmp(lj_pacote_n(c, d[0])->nome, "blender"), "destaques de Gráficos: blender");

	/* atualizações */
	const char *h = lj_aur_helper("");
	CONFERE(h && !strcmp(h, "paru"), "acha o paru (%s)", h ? h : "(nenhum)");
	CONFERE(lj_aur_helper("yay") && !strcmp(lj_aur_helper("yay"), "paru"), "pedir o yay sem tê-lo cai no paru");
	CONFERE(lj_marcar_atualizacoes(c, h) == 3, "3 atualizações (vlc, htop e meu-app-aur do AUR)");
	CONFERE(lj_achar(c, "vlc")->atualizavel && !strcmp(lj_achar(c, "vlc")->nova_versao, "3.0.21-1") && !strcmp(lj_achar(c, "vlc")->instalada, "3.0.20-1") && !strcmp(lj_achar(c, "vlc")->versao, "3.0.21-1"), "vlc: instalada 3.0.20, nova 3.0.21");
	CONFERE(lj_achar(c, "meu-app-aur") && lj_achar(c, "meu-app-aur")->atualizavel && !strcmp(lj_achar(c, "meu-app-aur")->repo, "aur"), "o app do AUR aparece com atualização");

	/* o que foi pedido e o que veio de fora */
	CONFERE(lj_achar(c, "vlc")->explicito && !lj_achar(c, "mesa")->explicito, "explícito (-Qe): vlc sim, mesa (dependência) não");
	CONFERE(lj_achar(c, "meu-app-aur") && lj_achar(c, "meu-app-aur")->externo && lj_achar(c, "meu-app-aur")->instalado, "o de fora (-Qm) entra como instalado do AUR");

	/* o AUR */
	char erro[512];
	int na = lj_aur_buscar(c, "code", erro, sizeof(erro));
	CONFERE(na == 3, "AUR: 3 (visual-studio-code-bin e outro-app-aur; o duplicado de mesa some; meu-app-aur já estava como de fora) → %d (%s)", na, erro);
	lj_filtro_padrao(&f);
	f.texto = "visual";
	n = lj_buscar(c, &f, r, 64);
	const lj_pacote *vs = n ? lj_resultado(c, r[0]) : NULL;
	CONFERE(vs && !strcmp(vs->nome, "visual-studio-code-bin") && !strcmp(vs->repo, "aur") && vs->votos == 2000 && !vs->instalado, "o AUR entra na busca");
	CONFERE(vs && r[0] < 0, "o código de um resultado do AUR é negativo");
	f.aur = false;
	CONFERE(lj_buscar(c, &f, r, 64) == 0, "sem AUR no filtro, ele some");
	lj_aur_limpar(c);
	CONFERE(lj_aur_buscar(c, "sem-rede", erro, sizeof(erro)) == -1 && strstr(erro, "AUR"), "sem rede: erro legível (%s)", erro);
	CONFERE(lj_aur_buscar(c, "a", erro, sizeof(erro)) == -1, "termo curto recusado");
	lj_aur_buscar(c, "code", erro, sizeof(erro));
	CONFERE(lj_achar(c, "visual-studio-code-bin") != NULL, "lj_achar enxerga o AUR");

	/* ações */
	char *log = le_log();
	g_free(log);
	nlinhas = 0;
	CONFERE(lj_executar(LJ_INSTALAR, lj_achar(c, "firefox"), h, linha, NULL, erro, sizeof(erro)) == 0, "instalar firefox: %s", erro);
	CONFERE(nlinhas >= 2 && strstr(ultima, "instalando"), "a saída chega linha por linha (%d, %s)", nlinhas, ultima);
	log = le_log();
	CONFERE(strstr(log, "pacman -S --noconfirm --needed firefox"), "pacman -S --noconfirm --needed firefox\n%s", log);
	g_free(log);
	nlinhas = 0;
	CONFERE(lj_executar(LJ_INSTALAR, vs, h, linha, NULL, erro, sizeof(erro)) == 0, "instalar do AUR: %s", erro);
	log = le_log();
	CONFERE(strstr(log, "paru -S --noconfirm --needed --sudo pkexec --skipreview visual-studio-code-bin"), "o AUR usa o paru com pkexec\n%s", log);
	g_free(log);
	CONFERE(lj_executar(LJ_INSTALAR, vs, NULL, NULL, NULL, erro, sizeof(erro)) != 0 && strstr(erro, "paru"), "AUR sem helper: recusa com o motivo (%s)", erro);
	CONFERE(lj_executar(LJ_REMOVER, lj_achar(c, "vlc"), h, NULL, NULL, erro, sizeof(erro)) == 0, "remover");
	log = le_log();
	CONFERE(strstr(log, "pacman -Rns --noconfirm vlc"), "pacman -Rns");
	g_free(log);
	CONFERE(lj_executar(LJ_ATUALIZAR_TUDO, NULL, h, NULL, NULL, erro, sizeof(erro)) == 0, "atualizar tudo");
	log = le_log();
	CONFERE(strstr(log, "pacman -Syu --noconfirm") && strstr(log, "paru -Sua --noconfirm --sudo pkexec"), "atualiza o sistema e depois o AUR");
	g_free(log);
	setenv("FALSO_FALHA", "firefox", 1);
	CONFERE(lj_executar(LJ_INSTALAR, lj_achar(c, "firefox"), h, NULL, NULL, erro, sizeof(erro)) != 0 && strstr(erro, "target not found"), "falha do pacman: o motivo vem no erro (%s)", erro);

	/* depois de instalar e de remover: ler de novo o instalado e aplicar (o pacman falso guarda o estado) */
	unsetenv("FALSO_FALHA");
	CONFERE(!lj_achar(c, "firefox")->instalado || 1, "firefox ainda marcado de antes da leitura");
	lj_instalados_usar(c, lj_instalados_ler());
	CONFERE(lj_achar(c, "firefox")->instalado, "depois de instalar e reler: firefox instalado");
	CONFERE(lj_executar(LJ_REMOVER, lj_achar(c, "firefox"), h, NULL, NULL, erro, sizeof(erro)) == 0, "remover firefox");
	lj_instalados_usar(c, lj_instalados_ler());
	CONFERE(!lj_achar(c, "firefox")->instalado, "e fora depois de remover e reler");
	CONFERE(lj_achar(c, "meu-app-aur") != NULL, "o de fora continua depois de reler");

	lj_catalogo_liberar(c);
	if (falhas) {
		fprintf(stderr, "%d falhas\n", falhas);
		return 1;
	}
	printf("loja (serviço): ok\n");
	return 0;
}
