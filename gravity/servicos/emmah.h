/*
 * As histórias do Eric com a Emma: uma pasta por história em ~/Emma/Histórias/NOME/, com a "bíblia" (biblia.md: premissa, personagens, mundo, tom,
 * linha do tempo e "Onde paramos") e os capítulos em capitulos/NN-titulo.md. Tudo dentro da pasta dela, então escreve direto, sem pedir aprovação
 * (o que for substituído fica copiado em ~/Emma/.copias, como em qualquer arquivo dela).
 *
 *   historia.listar | criar | abrir | salvar_capitulo | atualizar_biblia
 */
#ifndef LUMNA_EMMAH_H
#define LUMNA_EMMAH_H

#include "emmaa.h"

bool emmah_e_dela(const char *nome_acao);
void emmah_executar(const char *nome_acao, const struct json *args, struct emmaa_res *r);

/* a seção `secao` da bíblia (texto em markdown) com este corpo: troca o que havia, ou acrescenta ao fim dela; cria a seção se não existe (g_free) */
char *emmah_secao_definir(const char *biblia, const char *secao, const char *corpo, bool acrescentar);

#endif
