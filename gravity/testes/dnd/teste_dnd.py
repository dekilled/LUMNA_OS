#!/usr/bin/env python3
"""Arrastar e soltar entre aplicativos: um app do Início é arrastado até a dock (que fica por baixo do Início transparente) e fica fixo onde cair; soltar fora
cancela e o Início volta; vale com o dedo (segurar e andar) e com o mouse (apertar e andar).
python3 teste_dnd.py PASTA_DO_ORBIT PASTA_RUN CASA"""
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


def linhas():
    return [l.split(";")[1].strip() if ";" in l else "-" for l in open(arq).read().splitlines() if l.strip() and not l.startswith("#")]


def pixel(x, y):
    out = subprocess.run(["grim", "-g", f"{x},{y} 1x1", "-t", "ppm", "-"], capture_output=True).stdout
    return sum(out[-3:])


def espera(cond, seg=4):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def toca(x, y):
    msg("toque", "baixo", 1, x, y)
    time.sleep(0.1)
    msg("toque", "cima", 1)
    time.sleep(0.6)


def inicio_visivel():
    return pixel(900, 130) > 150  # a caixa de busca do Início (clara); sem o Início, só o fundo escuro


TAB_TRABALHO = (214, 232)
CARTAO = (181, 348)
DOCK = [(250, 400), (350, 500), (450, 600), (540, 650), (580, 668), (600, 668)]

# 0. o Início está aberto e mostra o app de teste na aba Trabalho
toca(*TAB_TRABALHO)
if not inicio_visivel():
    falha("o Início devia estar à vista no começo")

# 1. com o dedo: segurar o cartão e levar até a dock
msg("toque", "baixo", 1, *CARTAO)
time.sleep(0.9)
for p in DOCK:
    msg("toque", "move", 1, *p)
    time.sleep(0.15)
time.sleep(0.4)
if inicio_visivel():
    falha("durante o arraste o Início devia ficar transparente, para a dock aparecer")
msg("toque", "cima", 1)
if not espera(lambda: "App de Teste" in linhas()):
    falha(f"o app arrastado com o dedo devia ficar fixo na dock: {linhas()}")
elif linhas().index("App de Teste") != 1:
    falha(f"devia cair entre o Início e o Atlas: {linhas()}")
time.sleep(1)
if inicio_visivel():
    falha("com o app aceito, o Início devia fechar")

# 2. soltar fora da dock cancela e o Início volta (o app já está fixo: tira do arquivo e abre o Início de novo)
subprocess.run(["sed", "-i", "/teste-app/d", arq])
time.sleep(1)
subprocess.Popen([f"{os.path.dirname(os.path.abspath(__file__))}/../../lancador"], env=dict(os.environ, **env), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
time.sleep(2.5)
toca(*TAB_TRABALHO)
msg("toque", "baixo", 1, *CARTAO)
time.sleep(0.9)
for p in [(300, 380), (400, 300), (500, 250)]:  # para cima, longe da dock
    msg("toque", "move", 1, *p)
    time.sleep(0.15)
msg("toque", "cima", 1)
time.sleep(1.5)
if "App de Teste" in linhas():
    falha("soltar longe da dock não devia fixar nada")
if not inicio_visivel():
    falha("com o arraste cancelado, o Início devia voltar")

# 3. com o mouse: apertar o cartão e levar até a dock
msg("mouse", "mover", *CARTAO)
time.sleep(0.3)
msg("mouse", "botao", "baixo")
time.sleep(0.2)
for p in [(190, 360)] + DOCK:
    msg("mouse", "mover", *p)
    time.sleep(0.15)
time.sleep(0.4)
msg("mouse", "botao", "cima")
if not espera(lambda: "App de Teste" in linhas()):
    falha(f"o app arrastado com o mouse devia ficar fixo na dock: {linhas()}")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("arrastar e soltar entre aplicativos (Início para a dock): ok")
