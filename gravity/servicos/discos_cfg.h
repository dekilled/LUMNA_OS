/*
 * Montagem automática de discos e pendrives (lumna-discos): o que a pessoa escolheu e a regra de quando montar.
 * Sem desenho e sem GIO, para testar sem tela. As escolhas ficam em ~/.config/lumna/discos.ini:
 *   [discos]
 *   montar_ao_conectar = true   # um pendrive ou disco externo que é ligado já é montado
 *   montar_ao_entrar   = true   # os que já estão ligados ao entrar na sessão são montados
 * Só se monta o que é removível (pendrives, discos externos, cartões); as outras partições do computador continuam no "Montar" manual.
 */
#ifndef LUMNA_DISCOS_CFG_H
#define LUMNA_DISCOS_CFG_H

#include <stdbool.h>

typedef struct discos_cfg {
	bool ao_conectar;
	bool ao_entrar;
} discos_cfg;

/* lê (sem o arquivo: tudo desligado) */
void discos_cfg_ler(discos_cfg *c);
/* grava; falso se não deu */
bool discos_cfg_gravar(const discos_cfg *c);

enum discos_evento { DISCOS_CONECTOU, DISCOS_ENTROU };

/* montar este volume agora? removivel: pendrive, disco externo ou cartão; montado: já está; pode_automontar: o sistema não o escondeu (fstab, etc.) */
bool discos_deve_montar(const discos_cfg *c, enum discos_evento ev, bool removivel, bool montado, bool pode_automontar);

#endif
