/*
 * O serviço da Mídia (lumna-midia servico): dono do Firefox, do catálogo de serviços e dos recentes.
 * As telas (Início, capa de loading, seletor) e o controle do celular pedem coisas a ele pelo
 * socket (ver midiac.h). Cuida de levar a janela certa para a frente no Orbit, de pôr a capa de
 * loading sobre um serviço que está abrindo e de tirá-la quando a página carregou.
 */
#ifndef LUMNA_MIDIAD_H
#define LUMNA_MIDIAD_H

#include <stdbool.h>

#define MIDIA_APP_INICIO "lumna-midia-inicio"

int midiad_correr(const char *programa); /* o caminho deste programa, para abrir as telas */

#endif
