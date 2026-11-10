#!/usr/bin/env python3
"""A lista das janelas de um app na dock: fechar uma janela pela lista encolhe a lista (o compositor a reposiciona) e o hover e o clique têm de seguir as
linhas onde elas estão agora. Antes o deslocamento do ponteiro dentro do popup só era calculado quando o ponteiro entrava nele: depois de a lista
encolher, o hover caía uns 48 px abaixo da linha (e o clique, junto), até o mouse sair da lista e voltar.
python3 teste_pilha.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    return subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, capture_output=True, text=True).stdout


def mexe(x, y, t=0.12):
    msg("mouse", "mover", int(x), int(y))
    time.sleep(t)


def clique():
    msg("mouse", "botao", "baixo")
    time.sleep(0.08)
    msg("mouse", "botao", "cima")
    time.sleep(0.9)


def saida():
    return open(f"{run}/dock.out", encoding="utf-8", errors="replace").read().splitlines()


def altura_da_tela():
    m = re.search(r'"height":\s*(\d+)', msg("saidas"))
    return int(m[1]) if m else 720


def pos_icone(nome):
    r = None
    for l in saida():
        m = re.match(r"pos (\S+) (-?\d+) (-?\d+) (\d+) (\d+)", l)
        if m and m[1] == nome:
            r = tuple(int(m[i]) for i in range(2, 6))
    return r


def linhas():
    """as linhas da lista agora: {id: (x, y, w, h)} (em pixels da camada da dock)"""
    r = {}
    for l in saida():
        if l.startswith("previa-linhas:"):
            r = {}
            for it in l.split(":", 1)[1].split():
                m = re.match(r"(\d+):(-?\d+),(-?\d+),(\d+),(\d+)", it)
                if m:
                    r[int(m[1])] = tuple(int(m[i]) for i in range(2, 6))
    return r


def espera(cond, seg=4):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def hover_em(janela):
    return [l for l in saida() if l == f"previa: hover na janela {janela}"]


oy = altura_da_tela() - 92  # a camada da dock: a pílula de 92 colada embaixo
ic = pos_icone("fixo/1")
if not ic:
    falha("a dock não subiu")
    sys.exit(1)
cx, cy = ic[0] + ic[2] // 2, oy + ic[1] + ic[3] // 2

# 1. passar o mouse no ícone abre a lista com as janelas
mexe(900, 300, 0.6)
mexe(cx, cy)
mexe(cx + 1, cy, 1.4)
if not espera(lambda: len(linhas()) >= 3, 4):
    falha(f"a lista das janelas não abriu com as janelas do app ({linhas()})")
    sys.exit(1)
inicial = linhas()

# 2. fecha a primeira linha pelo X dela: a lista encolhe
primeira = min(inicial, key=lambda i: inicial[i][1])
x, y, w, h = inicial[primeira]
for px, py in [(cx, cy - 40), (x + w // 2, y + h + 30), (x + w - 40, y + h // 2), (x + w - 24, y + h // 2)]:
    mexe(px, oy + py if py < 0 or py < 200 else py)
# (as coordenadas da lista são da camada; o Y de tela é oy + y)
mexe(x + w - 24, oy + y + h // 2, 0.4)
clique()
if not espera(lambda: len(linhas()) == len(inicial) - 1, 4):
    falha(f"fechar uma janela pela lista não encolheu a lista (antes {len(inicial)}, agora {len(linhas())})")
    sys.exit(1)
time.sleep(0.8)
restantes = linhas()

# 3. o mouse (que ficou dentro da lista) passa sobre cada linha restante: o hover tem de cair nela, no meio e perto das bordas
for jid, (x, y, w, h) in sorted(restantes.items(), key=lambda kv: kv[1][1]):
    for frac in (0.3, 0.5, 0.7):
        antes = len(hover_em(jid))
        alvo_y = oy + y + int(h * frac)
        mexe(x + w // 3, alvo_y - 6, 0.15)
        mexe(x + w // 3, alvo_y, 0.35)
        if len(hover_em(jid)) == antes and not (len(hover_em(jid)) and saida()[-1] == f"previa: hover na janela {jid}"):
            falha(f"o mouse sobre a linha da janela {jid} (a {int(frac * 100)}% dela) não acendeu o hover dela: o ponteiro ficou deslocado depois de a lista encolher")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("dock (lista das janelas: o hover acompanha a lista que encolhe): ok")
