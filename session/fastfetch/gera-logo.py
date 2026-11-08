#!/usr/bin/env python3
"""Gera o logo.txt do fastfetch do LUMNA: um losango com o LL no meio e uma órbita inclinada com um planeta. Roda uma vez; o resultado vai commitado.
O losango cresce 2 colunas por linha (a célula do terminal é duas vezes mais alta que larga: assim o losango sai quadrado de verdade). $1 é o lilás, $2 o
claro (as letras) e $3 o apagado (a órbita), as cores do config.jsonc."""
import math, os

LARG, ALT = 46, 17          # a área do desenho, em colunas e linhas
cx = 23
metade = 8                   # linhas até a parte mais larga
g = [[' '] * LARG for _ in range(ALT)]
marca = [[0] * LARG for _ in range(ALT)]

def h(r):
    return r if r < metade else (2 * metade - 1) - r   # 0..7..0

for r in range(2 * metade):
    k = h(r)
    esq, dir = cx - 1 - 2 * k, cx + 2 * k
    g[r][esq] = '/' if r < metade else '\\'
    g[r][dir] = '\\' if r < metade else '/'
    for x in range(esq + 1, dir):
        g[r][x] = ':' if (x - esq) % 2 == 0 else ' '

# o LL do LUMNA, no meio: LL / LL / LLLL
for y, t in {6: "LL", 7: "LL", 8: "LLLL"}.items():
    x0 = cx - 3
    for i, c in enumerate(t):
        g[y][x0 + i] = c
        marca[y][x0 + i] = 1

# a órbita: uma elipse inclinada (sobe para a direita) que passa por trás do losango
A, B, phi = 21.5, 2.6 * 2, math.radians(-16)
cy = 8.0
pontos = []
for k in range(80):
    t = 2 * math.pi * k / 80
    ux, uy = A * math.cos(t), B * math.sin(t)
    X = ux * math.cos(phi) - uy * math.sin(phi)
    Yu = ux * math.sin(phi) + uy * math.cos(phi)
    x, y = int(round(cx + X)), int(round(cy + Yu / 2))
    if 0 <= x < LARG and 0 <= y < ALT and (x, y) not in pontos:
        k2 = h(y) if y < 2 * metade else 99
        if y >= 2 * metade or not (cx - 1 - 2 * k2 - 1 <= x <= cx + 2 * k2 + 1):
            pontos.append((x, y))
for x, y in pontos:
    if g[y][x] == ' ':
        g[y][x] = '.'
        marca[y][x] = 2
cand = [p for p in pontos if p[0] < cx - 12]
px, py = max(cand, key=lambda p: (p[1], -p[0]))
g[py][px] = '*'
marca[py][px] = 3

cor = {0: 1, 1: 2, 2: 3, 3: 2}
linhas = []
for y in range(ALT):
    s, atual = '$1', 1
    for x in range(LARG):
        c = cor[marca[y][x]]
        if c != atual:
            s += '$%d' % c
            atual = c
        s += g[y][x]
    linhas.append(s.rstrip())
while linhas and linhas[-1] in ('$1', ''):
    linhas.pop()
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "logo.txt"), "w").write('\n'.join(linhas) + '\n')
print('\n'.join(''.join(l).rstrip() for l in g if ''.join(l).strip()))
