#!/bin/sh
# O explorador de arquivos: o serviço (listar, ordenar, buscar, histórico, favoritos) numa árvore de teste e, com a pasta do build do Orbit
# (sh arquivos.sh PASTA_DO_ORBIT), o app inteiro num Orbit headless (testes/atlas/teste_ui.sh).
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0)"
cc $CF -o "$T/testa_arquivos" testes/atlas/testa_arquivos.c servicos/arquivos.c $(pkg-config --libs glib-2.0)
mkdir -p "$T/casa"
env HOME="$T/casa" XDG_CONFIG_HOME="$T/casa/c" XDG_DATA_HOME="$T/casa/d" XDG_DOCUMENTS_DIR= "$T/testa_arquivos"
cc $CF $(pkg-config --cflags gio-2.0) -o "$T/testa_op" testes/atlas/testa_op.c servicos/arquivos_op.c $(pkg-config --libs glib-2.0 gio-2.0)
env HOME="$T/casa" XDG_CONFIG_HOME="$T/casa/c" XDG_DATA_HOME="$T/casa/d" "$T/testa_op"
cc $CF -o "$T/testa_cfg" testes/atlas/testa_cfg.c servicos/arquivos_cfg.c $(pkg-config --libs glib-2.0)
env HOME="$T/casa" XDG_CONFIG_HOME="$T/casa/c2" XDG_DATA_HOME="$T/casa/d2" "$T/testa_cfg"
cc $CF -o "$T/testa_prop" testes/atlas/testa_prop.c servicos/arquivos_prop.c $(pkg-config --libs glib-2.0)
"$T/testa_prop"
cc $CF $(pkg-config --cflags gio-2.0) -o "$T/testa_rede" testes/atlas/testa_rede.c servicos/arquivos_rede.c $(pkg-config --libs glib-2.0 gio-2.0) -lpthread
env HOME="$T/casa" XDG_CONFIG_HOME="$T/casa/c3" "$T/testa_rede"
# discos e pendrives (lsblk e udisksctl de mentira)
cc $CF $(pkg-config --cflags gio-2.0) -o "$T/testa_discos" testes/atlas/testa_discos.c servicos/arquivos_discos.c servicos/armazenamento.c $(pkg-config --libs glib-2.0 gio-2.0)
mkdir -p "$T/fdiscos"
PATH="$AQUI/testes/atlas/bin:$PATH" FAKE_DISCOS_DIR="$T/fdiscos" "$T/testa_discos"
if [ -n "${1:-}" ]; then
	sh testes/atlas/teste_ui.sh "$1" "$AQUI"
fi
