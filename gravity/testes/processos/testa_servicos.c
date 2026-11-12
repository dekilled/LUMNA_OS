/* O serviço de leitura dos serviços do systemd contra um systemctl de mentira (testes/processos/bin/systemctl): os quatro estados, o que se ignora, os timers e a falha. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "sdservicos.h"

static int falhas;
#define CONFERE(c, m) do { if (!(c)) { fprintf(stderr, "FALHOU: %s\n", m); falhas++; } } while (0)

static const struct sv_item *acha(const struct sv_lista *l, const char *u) {
	for (int i = 0; i < l->n; i++) {
		if (!strcmp(l->v[i].unidade, u)) {
			return &l->v[i];
		}
	}
	return NULL;
}

int main(void) {
	struct sv_lista l;
	char erro[200] = "";
	CONFERE(sv_listar(false, &l, erro, sizeof(erro)) == 0, "o systemctl de mentira devia responder");
	const struct sv_item *nm = acha(&l, "NetworkManager.service");
	CONFERE(nm && nm->estado == SV_EXECUCAO && nm->pid == 812 && nm->memoria == 33554432 && !strcmp(nm->descricao, "Network Manager"), "NetworkManager: em execução, PID, memória e a descrição inteira");
	const struct sv_item *md = acha(&l, "systemd-modules-load.service");
	CONFERE(md && md->estado == SV_EXECUCAO && md->pid == 0 && md->memoria == -1 && !strcmp(sv_sub_texto(md), "concluído"), "um oneshot concluído (exited) segue em execução, sem PID nem memória");
	const struct sv_item *cu = acha(&l, "cups.service");
	CONFERE(cu && cu->estado == SV_PARADO && !cu->falhou, "cups: parado");
	const struct sv_item *bk = acha(&l, "backup.service");
	CONFERE(bk && bk->estado == SV_PARADO && bk->falhou && !strcmp(sv_sub_texto(bk), "falhou"), "backup: parado e marcado como falhou");
	const struct sv_item *jg = acha(&l, "jogo-sync.service");
	CONFERE(jg && jg->estado == SV_PAUSA && !strcmp(sv_sub_texto(jg), "em pausa") && jg->pid == 901, "um serviço congelado (FreezerState=frozen) está em pausa");
	const struct sv_item *ft = acha(&l, "fstrim.timer");
	CONFERE(ft && ft->estado == SV_PROGRAMADO && ft->proximo == 1900003600 && ft->ultimo == 1899400000 && !strcmp(ft->ativa, "fstrim.service"), "fstrim.timer: programado, com próxima e última execução e a unidade que dispara");
	const struct sv_item *lr = acha(&l, "logrotate.timer");
	CONFERE(lr && lr->estado == SV_PROGRAMADO && lr->proximo == 0 && lr->ultimo == 0, "timer sem horário conhecido segue programado, sem datas");
	const struct sv_item *an = acha(&l, "antigo.timer");
	CONFERE(an && an->estado == SV_PARADO, "timer inativo não é programado");
	CONFERE(!acha(&l, "fantasma.service") && !acha(&l, "sumiu.timer") && !acha(&l, "sshd.service"), "unidades not-found e masked ficam de fora");
	for (int i = 1; i < l.n; i++) {
		CONFERE(strcasecmp(l.v[i - 1].unidade, l.v[i].unidade) <= 0, "a lista vem em ordem de nome");
	}
	int cont[SV_ESTADOS] = { 0 };
	for (int i = 0; i < l.n; i++) {
		cont[l.v[i].estado]++;
	}
	CONFERE(cont[SV_EXECUCAO] == 3 && cont[SV_PARADO] == 5 && cont[SV_PROGRAMADO] == 2 && cont[SV_PAUSA] == 1, "as contagens por estado (3 rodando, 5 parados, 2 programados, 1 em pausa)");
	sv_liberar(&l);

	CONFERE(sv_listar(true, &l, erro, sizeof(erro)) == 0, "os serviços da sessão (--user) também");
	const struct sv_item *br = acha(&l, "lumna-barra.service");
	CONFERE(br && br->estado == SV_EXECUCAO && br->pid == 4242, "lumna-barra (sessão): em execução");
	const struct sv_item *pw = acha(&l, "pipewire.service");
	CONFERE(pw && pw->memoria == -1, "a memória 18446744073709551615 do systemd quer dizer 'não medido'");
	const struct sv_item *sc = acha(&l, "sincroniza.timer");
	CONFERE(sc && sc->estado == SV_PROGRAMADO && sc->proximo == 1900000000, "timer da sessão");
	sv_liberar(&l);

	setenv("FAKE_SD_FALHA", "1", 1);
	CONFERE(sv_listar(false, &l, erro, sizeof(erro)) == -1 && l.n == 0 && strstr(erro, "bus"), "sem o systemd, devolve -1 com o motivo e a lista vazia");
	sv_liberar(&l);
	unsetenv("FAKE_SD_FALHA");

	if (falhas) {
		return 1;
	}
	puts("serviços do systemd: ok");
	return 0;
}
