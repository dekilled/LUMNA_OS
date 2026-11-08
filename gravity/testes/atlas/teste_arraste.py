#!/usr/bin/env python3
"""Arrastar para reordenar a barra lateral (o dedo segura meio segundo e anda): sobe um item, leva um item de uma seção para a outra, Esc cancela
e a ordem fica gravada no barra.ini.
python3 teste_arraste.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json, configparser

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

barra_ini = f"{casa}/.config/lumna/atlas/barra.ini"


def itens():
    """[(rótulo, seção)] na ordem do arquivo"""
    cp = configparser.ConfigParser(interpolation=None)
    cp.read(barra_ini, encoding="utf-8")
    return [(cp[s]["rotulo"], int(cp[s]["secao"])) for s in cp.sections()]


def arrasta(de, ate, dy, esc=False):
    """segura `de` meio segundo, anda até a altura de `ate` (+dy) e solta (ou aperta Esc antes)"""
    x, y = centro(de)
    ox, oy = origem()
    p = pos(ate)
    y2 = oy + p[1] + dy
    msg("toque", "baixo", 1, x, y)
    time.sleep(0.9)
    for i in range(1, 9):
        msg("toque", "move", 1, x, int(y + (y2 - y) * i / 8))
        time.sleep(0.07)
    time.sleep(0.3)
    if esc:
        tecla("Escape")
    msg("toque", "cima", 1)
    time.sleep(0.7)


vai_para(docs)
# parte da barra de fábrica: Configurações > Barra lateral > Restaurar padrão > Aplicar
toca("t-ajustes")
toca("cfg-pg/2")
toca("cfg-restaurar")
toca("cfg-aplicar")
toca("cfg-voltar")
confere(lambda: estado().get("pagina") == "-1", "volta aos arquivos depois de restaurar a barra")
toca("l-it/0")  # o Início
confere(lambda: estado().get("vista") == "3", "o Início abre")
time.sleep(0.4)

# 1. Esc cancela: nada se grava
arrasta("l-it/2", "l-it/0", 5, esc=True)
if [r for r, _ in itens()][:3] != ["Início", "Recentes", "Favoritos"]:
    falha(f"Esc devia cancelar o arraste: {itens()}")
confere(lambda: estado().get("vista") == "3", "o arraste cancelado não navega")

# 2. Favoritos sobe para antes de Início
arrasta("l-it/2", "l-it/0", 5)
confere(lambda: [r for r, _ in itens()][:3] == ["Favoritos", "Início", "Recentes"], "arrastar Favoritos para cima de Início grava a ordem nova")
confere(lambda: estado().get("vista") == "3", "arrastar não navega (continua no Início)")

# 3. Home (Coleções) vai para o fim de Espaços
ids = {r: i for i, (r, _) in enumerate(itens())}
arrasta("l-it/3", "l-it/2", 36)  # logo abaixo de Recentes (o último de Espaços)
confere(lambda: dict(itens()).get("Home") == 0, "arrastar Home para o fim de Espaços muda a seção dele")
confere(lambda: [r for r, s in itens() if s == 0] == ["Favoritos", "Início", "Recentes", "Home"], f"e cai no fim da seção ({itens()})")

# 4. de volta: Home para Coleções (abaixo, na outra seção) e um clique simples ainda navega
toca("l-it/3")  # o Home, que agora é o último de Espaços
confere(lambda: estado().get("vista") == "0" and estado().get("pasta") == casa, "clicar num item (sem segurar) continua navegando")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (arrastar a barra lateral): ok")
