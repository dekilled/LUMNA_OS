#!/usr/bin/env python3
"""A área de trabalho (lumna-desktop) num Orbit headless. python3 teste_app.py PASTA_DO_ORBIT RUN PASTA_GRAVITY"""
import os, re, subprocess, sys, time

orbit_dir, run, gravity = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
desk = f"{run}/Desktop"
out = f"{run}/dk.out"
falhas = []
verificacoes = 0
sh = None


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)
    subprocess.run(["grim", f"{run}/falha-{len(falhas)}.png"])


def msg(*a):
    subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, stdout=subprocess.DEVNULL)


def log():
    return open(out, encoding="utf-8", errors="replace").read() if os.path.exists(out) else ""


def pos(nome):
    r = None
    for l in log().splitlines():
        m = re.match(r"pos cel/(.+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
        if m and m[1] == nome:
            r = tuple(int(m[i]) for i in range(2, 6))
    return r


def estado():
    r = {}
    for l in log().splitlines():
        if l.startswith("estado "):
            r = dict(re.findall(r"(\w+)=(\[[^\]]*\]|\S+)", l))
    return r


def lugares():
    t = estado().get("lugares", "[]")[1:-1]
    r = {}
    for p in t.split("|"):
        if "@" in p:
            n, c = p.rsplit("@", 1)
            r[n] = tuple(int(x) for x in c.split(","))
    return r


def sel():
    t = estado().get("sel", "[]")[1:-1]
    return sorted(t.split("|")) if t else []


def espera(cond, seg=4):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def confere(cond, o_que, seg=4):
    global verificacoes
    verificacoes += 1
    if not espera(cond, seg):
        falha(o_que + f"  (estado: {estado()})")


def centro(nome):
    espera(lambda: pos(nome) is not None, 4)
    p = pos(nome)
    if not p:
        falha(f"não achei {nome} na tela")
        return None
    return p[0] + p[2] // 2, p[1] + p[3] // 2


def clique(x, y, botao="esquerdo", espera_s=0.4):
    msg("mouse", "mover", x, y)
    time.sleep(0.15)
    msg("mouse", "botao", "baixo") if botao == "esquerdo" else msg("mouse", "botao", "baixo", botao)
    time.sleep(0.08)
    msg("mouse", "botao", "cima") if botao == "esquerdo" else msg("mouse", "botao", "cima", botao)
    time.sleep(espera_s)


def toca(nome, **k):
    c = centro(nome)
    if c:
        clique(*c, **k)


def duplo(nome):
    c = centro(nome)
    if c:
        clique(*c, espera_s=0.1)
        clique(*c, espera_s=0.6)


def arrasta(x0, y0, x1, y1, passos=8):
    msg("mouse", "mover", x0, y0)
    time.sleep(0.3)
    msg("mouse", "botao", "baixo")
    time.sleep(0.3)
    for i in range(1, passos + 1):
        msg("mouse", "mover", x0 + (x1 - x0) * i // passos, y0 + (y1 - y0) * i // passos)
        time.sleep(0.1)
    time.sleep(0.3)
    msg("mouse", "botao", "cima")
    time.sleep(0.8)


def tecla(*t, mod=None):
    cmd = ["wtype", "-s", "120"]
    for m in (mod.split("+") if mod else []):
        cmd += ["-M", m]
    for k in t:
        cmd += ["-k", k]
    for m in (mod.split("+") if mod else []):
        cmd += ["-m", m]
    subprocess.run(cmd, stdout=subprocess.DEVNULL)
    time.sleep(0.4)


def digita(txt):
    subprocess.run(["wtype", "-s", "60", txt], stdout=subprocess.DEVNULL)
    time.sleep(0.4)


# --- 1. o que aparece ---
confere(lambda: estado().get("total") == "6", "os 6 itens da pasta aparecem")
l = lugares()
confere(lambda: sorted(lugares()) == sorted(["Fotos", "Projetos", "meuapp.desktop", "nota.txt", "relatorio.pdf", "um arquivo com um nome bem comprido que quebra.png"]), "todos têm lugar")
confere(lambda: len(set(lugares().values())) == 6, "nenhum ícone em cima de outro")
confere(lambda: lugares().get("Fotos") == (0, 0) and lugares().get("Projetos") == (0, 1), "pastas primeiro, de cima para baixo na primeira coluna")
confere(lambda: os.path.exists(f"{run}/.config/lumna/desktop.ini"), "os lugares ficam guardados em desktop.ini")
# --- 2. selecionar ---
toca("nota.txt")
confere(lambda: sel() == ["nota.txt"], "clique seleciona")
toca("relatorio.pdf")
confere(lambda: sel() == ["relatorio.pdf"], "outro clique troca a seleção")
clique(700, 600)
confere(lambda: sel() == [], "clique no vazio tira a seleção")
# --- 3. retângulo ---
arrasta(600, 650, 30, 30)
confere(lambda: len(sel()) == 6, "o retângulo em volta seleciona os 6")
clique(700, 600)
arrasta(400, 340, 100, 120)
confere(lambda: sel() == ["Projetos", "meuapp.desktop"] or sel() == sorted(["Projetos", "meuapp.desktop"]) or set(sel()) >= {"Projetos"}, "o retângulo só pega o que toca (Projetos)")
clique(700, 600)
# --- 4. duplo clique ---
duplo("meuapp.desktop")
confere(lambda: os.path.exists(f"{run}/lancou"), "duplo clique num atalho .desktop o executa")
duplo("Projetos")
confere(lambda: os.path.exists(f"{run}/atlas-abriu") and open(f"{run}/atlas-abriu").read().strip() == f"{desk}/Projetos", "duplo clique numa pasta abre o Atlas nela")
# --- 5. renomear ---
toca("nota.txt")
tecla("F2")
confere(lambda: estado().get("ren") == "[nota.txt]", "F2 renomeia")
tecla("a", mod="ctrl")
digita("ideias.txt")
tecla("Return")
confere(lambda: os.path.exists(f"{desk}/ideias.txt") and not os.path.exists(f"{desk}/nota.txt"), "o arquivo foi renomeado no disco")
confere(lambda: lugares().get("ideias.txt") == (0, 3), "o ícone mantém o lugar depois de renomear")
# --- 6. lixeira e desfazer ---
toca("relatorio.pdf")
tecla("Delete")
confere(lambda: not os.path.exists(f"{desk}/relatorio.pdf") and os.path.exists(f"{run}/.local/share/Trash/files/relatorio.pdf"), "Delete manda para a lixeira")
confere(lambda: "relatorio.pdf" not in lugares(), "o ícone some da área de trabalho")
tecla("z", mod="ctrl")
confere(lambda: os.path.exists(f"{desk}/relatorio.pdf") and "relatorio.pdf" in lugares(), "Ctrl+Z traz de volta")
# --- 7. nova pasta ---
tecla("n", mod="ctrl+shift")
confere(lambda: estado().get("ren") == "[Nova pasta]", "Ctrl+Shift+N cria uma pasta e já pede o nome")
tecla("a", mod="ctrl")
digita("Estudos")
tecla("Return")
confere(lambda: os.path.isdir(f"{desk}/Estudos") and "Estudos" in lugares(), "a pasta nova ganhou o nome e um lugar")
# --- 8. arrastar para outro lugar ---
c = centro("ideias.txt")
if c:
    arrasta(c[0], c[1], 800, 400)
    confere(lambda: lugares().get("ideias.txt") == (7, 3), "arrastar muda o ícone de lugar (encaixa na grade)")
    confere(lambda: os.path.exists(f"{desk}/ideias.txt"), "o arquivo continua na Área de trabalho")
# --- 9. arrastar para dentro de uma pasta ---
c = centro("ideias.txt")
d = centro("Projetos")
if c and d:
    arrasta(c[0], c[1], d[0], d[1])
    confere(lambda: os.path.exists(f"{desk}/Projetos/ideias.txt") and not os.path.exists(f"{desk}/ideias.txt"), "soltar em cima de uma pasta move para dentro dela")
# --- 10. mudanças de fora ---
open(f"{desk}/chegou.txt", "w").write("x")
confere(lambda: "chegou.txt" in lugares(), "um arquivo criado de fora aparece")
os.remove(f"{desk}/chegou.txt")
confere(lambda: "chegou.txt" not in lugares(), "um arquivo apagado de fora some")

print(f"desktop (app): {verificacoes} verificações, {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
