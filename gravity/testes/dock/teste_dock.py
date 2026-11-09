#!/usr/bin/env python3
"""Arrastar na dock (o dedo segura meio segundo e anda): reordena os fixos e grava ~/.config/lumna/dock; Esc não existe aqui, então se confere também que
um toque comum ainda abre o app (sem arrastar) e que segurar e soltar sem andar não mexe na ordem.
python3 teste_dock.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
arq = f"{casa}/.config/lumna/dock"
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, stdout=subprocess.DEVNULL)


def pos():
    r = {}
    for l in open(f"{run}/dock.out", encoding="utf-8", errors="replace").read().splitlines():
        m = re.match(r"pos (\S+) (-?\d+) (-?\d+) (\d+) (\d+)", l)
        if m:
            r[m[1]] = tuple(int(m[i]) for i in range(2, 6))
    return r


def saidas():
    out = subprocess.run([f"{orbit_dir}/orbit-msg", "saidas"], env=env, capture_output=True, text=True).stdout
    m = re.search(r'"height":\s*(\d+)', out)
    return int(m[1]) if m else 720


ALTURA_DA_DOCK = 98  # a camada: a pílula de 92 e uma folga
oy = saidas() - ALTURA_DA_DOCK


def centro(nome):
    p = pos()[nome]
    return p[0] + p[2] // 2, oy + p[1] + p[3] // 2


def ordem():
    return [l.split(";")[1].strip() if ";" in l else "-" for l in open(arq).read().splitlines() if l.strip() and not l.startswith("#")]


def arrasta(de, ate_x, parar_antes=False):
    x, y = centro(de)
    msg("toque", "baixo", 1, x, y)
    time.sleep(0.9)
    for i in range(1, 9):
        msg("toque", "move", 1, int(x + (ate_x - x) * i / 8), y)
        time.sleep(0.07)
    time.sleep(0.3)
    msg("toque", "cima", 1)
    time.sleep(0.8)


def espera(cond, seg=4):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


assert pos().get("fixo/3"), "a dock não subiu"
antes = open(arq).read()

# 1. segurar e soltar sem andar não muda a ordem (abre o menu do app)
x, y = centro("fixo/3")
msg("toque", "baixo", 1, x, y)
time.sleep(0.9)
msg("toque", "cima", 1)
time.sleep(0.8)
if open(arq).read() != antes:
    falha("segurar e soltar sem andar não devia mexer na ordem")
msg("toque", "baixo", 1, 5, 5)  # um toque fora fecha o menu
time.sleep(0.1)
msg("toque", "cima", 1)
time.sleep(0.6)

# 2. a Loja (4º) vai para antes do Atlas (2º)
p1 = pos()["fixo/1"]
arrasta("fixo/3", p1[0] + 8)
if not espera(lambda: ordem()[:4] == ["Início", "Loja", "Atlas", "Navegador"]):
    falha(f"arrastar a Loja para antes do Atlas: {ordem()}")

# 3. o separador e a Emma ficam onde estavam
if ordem()[4:] != ["-", "Emma"]:
    falha(f"o separador e a Emma não deviam mudar: {ordem()}")
time.sleep(0.5)

# 4. o Início (1º) vai para o fim, depois da Emma
pe = pos()["fixo/5"]
arrasta("fixo/0", pe[0] + pe[2] + 20)
if not espera(lambda: ordem()[-1] == "Início"):
    falha(f"arrastar o Início para depois da Emma: {ordem()}")

# 5. um toque comum ainda abre o app (sem arrastar nada) e não reordena
antes = open(arq).read()
x, y = centro("fixo/1")
msg("toque", "baixo", 1, x, y)
time.sleep(0.08)
msg("toque", "cima", 1)
time.sleep(0.6)
if open(arq).read() != antes:
    falha("um toque comum não devia reordenar")

# 6. um app aberto que não está fixo (o Atlas, tirado da dock) arrasta-se para a dock e fica fixo onde cair
subprocess.run(["sed", "-i", "/lumna-atlas/d", arq])
time.sleep(1)
env2 = dict(os.environ, LUMNA_ATLAS_DEBUG="")
atlas = subprocess.Popen([os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "atlas")], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
if not espera(lambda: any(k.startswith("aberto/") for k in pos()), 8):
    falha(f"o Atlas aberto não apareceu como app solto na dock: {list(pos())}")
else:
    chave = [k for k in pos() if k.startswith("aberto/")][0]
    p0 = pos()["fixo/1"]
    arrasta(chave, p0[0] + 8)
    if not espera(lambda: "Atlas" in ordem() or any("atlas" in l for l in open(arq).read().splitlines())):
        falha(f"arrastar o app aberto para dentro da dock devia fixá-lo: {open(arq).read()!r}")
    else:
        linhas = [l for l in open(arq).read().splitlines() if l.strip() and not l.startswith("#")]
        if "atlas" not in linhas[1].lower():
            falha(f"o app devia cair na posição 2 (antes do segundo fixo): {linhas}")
atlas.terminate()

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("dock (arrastar para reordenar): ok")
