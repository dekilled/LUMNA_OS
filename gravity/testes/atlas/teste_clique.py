#!/usr/bin/env python3
"""O mouse nos itens do explorador: a seleção aparece na hora (não uns 500 ms depois), o duplo clique abre mesmo com o mouse andando uns pixels entre os
dois cliques (não vira arraste) e apertar no vazio e andar seleciona por área (o retângulo).
python3 teste_clique.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])


def clique(x, y, seg=0.06):
    msg("mouse", "mover", x, y)
    time.sleep(0.15)
    msg("mouse", "botao", "baixo")
    time.sleep(seg)
    msg("mouse", "botao", "cima")


def pixel(x, y):
    r = subprocess.run(["grim", "-g", f"{x},{y} 1x1", "-t", "ppm", "-"], capture_output=True)
    return r.stdout[-3:]


def caixa(nome):
    p = pos(nome)
    ox, oy = origem()
    return ox + p[0], oy + p[1], p[2], p[3]


vai_para(docs)
tecla("Escape")
time.sleep(0.5)

# 1. a seleção por um clique aparece na hora, mesmo numa pasta que o painel de detalhes demora para medir (a leitura lenta é a de uma pasta grande de verdade: o
#    quadro que mostrava a seleção só vinha quando a thread do painel terminava, ou no despertar de ~0,5 s depois do aperto)
open(f"{casa}/.atlas_lento_ms", "w").write("1500")
time.sleep(0.5)
x, y, w, h = caixa("item/Pessoal")
msg("mouse", "mover", x + w // 2, y + h // 2)
time.sleep(0.5)  # o mouse já em cima: o fundo de "passar o mouse" já está desenhado, a diferença é só a seleção
antes = pixel(x + 8, y + 8)
msg("mouse", "botao", "baixo")
time.sleep(0.05)
msg("mouse", "botao", "cima")
t0 = time.time()
while time.time() - t0 < 1.5 and pixel(x + 8, y + 8) == antes:
    time.sleep(0.02)
lat = time.time() - t0
print(f"latência da seleção: {lat*1000:.0f} ms")
if lat > 0.3:
    falha(f"a seleção por clique levou {lat*1000:.0f} ms para aparecer (devia ser imediata)")
confere(lambda: estado().get("sel") == "1", "o clique seleciona o item")
os.remove(f"{casa}/.atlas_lento_ms")

# 2. duplo clique com o mouse andando uns pixels entre os dois cliques: abre a pasta (não vira arraste)
tecla("Escape")
x, y, w, h = caixa("item/Projetos")
cx, cy = x + w // 2, y + h // 2
clique(cx, cy)
time.sleep(0.12)
msg("mouse", "mover", cx + 9, cy + 7)
time.sleep(0.05)
msg("mouse", "botao", "baixo")
time.sleep(0.05)
msg("mouse", "botao", "cima")
confere(lambda: estado().get("pasta", "").endswith("/Projetos"), "o duplo clique (com o mouse andando uns pixels entre os cliques) abre a pasta, sem virar arraste", 3)
vai_para(docs)
tecla("Escape")
time.sleep(0.5)

# 3. a área de seleção: apertar no vazio abaixo dos itens e andar até cobrir uns itens os seleciona
nomes = [n.rstrip("*") for n in ordem()]
caixas = {n: caixa("item/" + n) for n in nomes if pos("item/" + n)}
if len(caixas) < 6:
    falha(f"poucos itens à vista para testar a área de seleção: {len(caixas)}")
else:
    # começa na margem vazia à direita das colunas (a grade de teste cobre a janela de cima a baixo) e anda até o meio do segundo item
    ordenados = sorted(caixas.items(), key=lambda kv: (kv[1][1], kv[1][0]))
    direita = max(c[0] + c[2] for c in caixas.values())
    linha2 = ordenados[3][1]
    x_ini, y_ini = direita + 40, linha2[1] + linha2[3] // 2
    alvo = ordenados[1][1]
    print("GEO", origem(), "ini", x_ini, y_ini, "alvo", alvo, "itens", {n: c for n, c in list(caixas.items())[:20]}, flush=True)
    msg("mouse", "mover", x_ini, y_ini)
    time.sleep(0.15)
    msg("mouse", "botao", "baixo")
    time.sleep(0.1)
    for i in range(1, 9):
        msg("mouse", "mover", x_ini + (alvo[0] + alvo[2] // 2 - x_ini) * i // 8, y_ini + (alvo[1] + alvo[3] // 2 - y_ini) * i // 8)
        time.sleep(0.06)
    time.sleep(0.2)
    n_meio = int(estado().get("sel", "0"))
    subprocess.run(["grim", f"{run}/banda.png"])
    msg("mouse", "botao", "cima")
    time.sleep(0.5)
    ax, ay = alvo[0] + alvo[2] // 2, alvo[1] + alvo[3] // 2
    esperado = [n for n, c in caixas.items() if c[0] < max(x_ini, ax) and c[0] + c[2] > min(x_ini, ax) and c[1] < max(y_ini, ay) and c[1] + c[3] > min(y_ini, ay)]
    confere(lambda: int(estado().get("sel", "0")) == len(esperado) and len(esperado) >= 3, f"a área seleciona os itens que cobre (esperava {len(esperado)}: {esperado})")
    if n_meio < 3:
        falha(f"a seleção deve acompanhar o retângulo enquanto se arrasta (tinha {n_meio} no meio)")
    # soltar a área não limpa a seleção, e clicar no vazio sem andar limpa
    time.sleep(0.3)
    confere(lambda: int(estado().get("sel", "0")) >= 3, "soltar a área mantém a seleção")
    clique(x_ini, y_ini)
    confere(lambda: estado().get("sel") == "0", "um clique no vazio (sem andar) limpa a seleção")

sys.exit(1 if falhas else 0)
