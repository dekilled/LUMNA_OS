#!/bin/sh
# Testa os serviços do Painel: sistema.c (Sobre), pacotes.c (Aplicativos e Armazenamento) e hora.c (formato 12/24 h),
# com árvores falsas (testes/painel/): nada toca no sistema de verdade.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0)"
LG="$(pkg-config --libs glib-2.0)"

# pacotes
cc $CF -o "$T/testa_pacotes" testes/painel/testa_pacotes.c servicos/pacotes.c $LG
ENVV=$(python3 testes/painel/monta_pacman_falso.py "$T/pac")
env -u XDG_DATA_HOME -u XDG_CACHE_HOME -u XDG_CONFIG_HOME $ENVV "$T/testa_pacotes"

# sistema (Sobre)
cc $CF -o "$T/testa_sistema" testes/painel/testa_sistema.c servicos/sistema.c $LG
python3 testes/painel/monta_sistema_falso.py "$T/raiz"
LUMNA_RAIZ_SISTEMA="$T/raiz" PATH="$T/raiz/bin:$PATH" "$T/testa_sistema"

# hora (formato)
cc $CF -o "$T/testa_hora" testes/painel/testa_hora.c servicos/hora.c $LG
mkdir -p "$T/cfg/lumna"
env -u XDG_DATA_HOME XDG_CONFIG_HOME="$T/cfg" "$T/testa_hora" "$T/cfg/lumna/hora.ini"
# entrada e telas (Teclado e mouse, Monitores)
cc $CF -o "$T/testa_entrada" testes/painel/testa_entrada.c servicos/entrada.c servicos/telas.c servicos/json.c servicos/modos.c $LG -lm
mkdir -p "$T/cfg2"
env -u XDG_DATA_HOME XDG_CONFIG_HOME="$T/cfg2" LUMNA_XKB_LST="$AQUI/testes/painel/evdev-falso.lst" env -u XKB_DEFAULT_LAYOUT "$T/testa_entrada"
echo "(arquivos de teste em $T)"
