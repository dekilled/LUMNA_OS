#!/usr/bin/env python3
"""Arrastar arquivos no Atlas (o Orbit de teste injeta o ponteiro): um arquivo para cima de uma pasta da grade, para um atalho da barra lateral e para o fundo da
lista de outra pasta (copia sempre; o original fica).
python3 teste_dnd.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])


def arrasta_mouse(de, ate, dy=0, dx=0):
    x, y = centro(de)
    ox, oy = origem()
    p = pos(ate)
    x2, y2 = ox + p[0] + p[2] // 2 + dx, oy + p[1] + p[3] // 2 + dy
    msg("mouse", "mover", x, y)
    time.sleep(0.3)
    msg("mouse", "botao", "baixo")
    time.sleep(0.2)
    for i in range(1, 9):
        msg("mouse", "mover", int(x + (x2 - x) * i / 8), int(y + (y2 - y) * i / 8))
        time.sleep(0.07)
    time.sleep(0.4)
    msg("mouse", "botao", "cima")
    time.sleep(1.2)


vai_para(docs)
os.makedirs(f"{docs}/DestinoA", exist_ok=True)
open(f"{docs}/arraste1.txt", "w").write("um")
open(f"{docs}/arraste2.txt", "w").write("dois")
time.sleep(1)

import configparser


def indice_home():
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(f"{casa}/.config/lumna/atlas/barra.ini", encoding="utf-8")
    if not cp.sections():
        return 3  # a barra de fábrica: Início, Recentes, Favoritos, Home
    for k, sec in enumerate(cp.sections()):
        if cp[sec]["caminho"] == casa:
            return k
    return 3


# 1. um arquivo para cima de uma pasta da grade
seleciona("arraste1.txt")  # (traz o item para a vista)
time.sleep(0.4)
arrasta_mouse("item/arraste1.txt", "item/DestinoA")
confere(lambda: os.path.exists(f"{docs}/DestinoA/arraste1.txt"), "soltar um arquivo em cima de uma pasta copia para dentro dela")
if not os.path.exists(f"{docs}/arraste1.txt"):
    falha("o original devia ficar (a ação é copiar)")
confere(lambda: estado().get("pasta") == docs, "soltar não navega")

# 2. um arquivo para o atalho de pasta da barra lateral (Home)
seleciona("arraste2.txt")
time.sleep(0.4)
arrasta_mouse("item/arraste2.txt", f"l-it/{indice_home()}")
confere(lambda: os.path.exists(f"{casa}/arraste2.txt"), "soltar num atalho da barra lateral copia para a pasta dele")

# 3. soltar na própria pasta (já está lá) não faz nada nem cria "(2)"
n_antes = len(os.listdir(docs))
seleciona("arraste1.txt")
time.sleep(0.4)
arrasta_mouse("item/arraste1.txt", "item/arraste1.txt", dy=0, dx=60)  # um pouco ao lado, ainda no fundo da lista da mesma pasta
time.sleep(0.5)
if len(os.listdir(docs)) != n_antes:
    falha("soltar na pasta de onde veio não devia criar nada")

# 4. o clique simples continua selecionando (nenhum arraste começou)
toca("item/arraste1.txt")
confere(lambda: estado().get("sel") == "1", "um clique simples continua selecionando")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (arrastar arquivos): ok")
