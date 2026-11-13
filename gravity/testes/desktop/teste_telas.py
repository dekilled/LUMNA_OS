#!/usr/bin/env python3
"""A área de trabalho em dois monitores e a pasta própria do Desktop. python3 teste_telas.py PASTA_DO_ORBIT RUN PASTA_GRAVITY"""
import json, os, re, subprocess, sys, time

orbit_dir, run, gravity = sys.argv[1], sys.argv[2], sys.argv[3]
sock = [f for f in os.listdir(run) if f.startswith("orbit.") and f.endswith(".sock")][0]
out = f"{run}/dk.out"
desk = f"{run}/home/Desktop"
falhas, verificacoes = [], 0


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    r = subprocess.run([f"{orbit_dir}/orbit-msg", "-s", f"{run}/{sock}", *map(str, a)], capture_output=True, text=True)
    try:
        return json.loads(r.stdout)
    except Exception:
        return {}


def linhas():
    return open(out, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(out) else []


def estado(tela=None):
    """o último "estado" da instância da tela (None: a principal, sem prefixo)"""
    pref = f"[{tela}] " if tela else ""
    r = {}
    for l in linhas():
        if l.startswith(pref + "estado ") and (tela or not l.startswith("[")):
            r = dict(re.findall(r"(\w+)=(\[[^\]]*\]|\S+)", l[len(pref):]))
    return r


def pos(nome, tela=None):
    pref = f"[{tela}] " if tela else ""
    r = None
    for l in linhas():
        if tela is None and l.startswith("["):
            continue
        m = re.match(re.escape(pref) + r"pos cel/(.+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
        if m and m[1] == nome:
            r = tuple(int(m[i]) for i in range(2, 6))
    return r


def lugares(tela=None):
    t = estado(tela).get("lugares", "[]")[1:-1]
    return {p.rsplit("@", 1)[0]: tuple(int(x) for x in p.rsplit("@", 1)[1].split(",")) for p in t.split("|") if "@" in p}


def espera(cond, seg=6):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.2)
    return cond()


def confere(cond, o_que, seg=6):
    global verificacoes
    verificacoes += 1
    if not espera(cond, seg):
        falha(o_que)


def arrasta(x0, y0, x1, y1, passos=10):
    msg("mouse", "mover", x0, y0)
    time.sleep(0.3)
    msg("mouse", "botao", "baixo")
    time.sleep(0.3)
    for i in range(1, passos + 1):
        msg("mouse", "mover", x0 + (x1 - x0) * i // passos, y0 + (y1 - y0) * i // passos)
        time.sleep(0.1)
    time.sleep(0.3)
    msg("mouse", "botao", "cima")
    time.sleep(1)


saidas = {s["nome"]: s for s in msg("saidas").get("saidas", [])}
# a principal é a que tem o ponteiro/a primeira; a instância sem prefixo é a dela
principal = next((n for n, s in saidas.items() if s.get("ponteiro")), sorted(saidas)[0])
outra = next(n for n in saidas if n != principal)
ox = {n: s["x"] for n, s in saidas.items()}
print(f"(principal {principal}, outra {outra}; x {ox})", file=sys.stderr)

# --- 1. a pasta do Desktop é própria e começa vazia (o xdg-user-dirs apontava para a pasta pessoal inteira) ---
confere(lambda: estado().get("total") == "0", "a área de trabalho começa vazia: a pasta pessoal (com o cache e os programas) não vira ícones")
confere(lambda: os.path.isdir(desk), "a pasta ~/Desktop foi criada")
# --- 2. um arquivo novo aparece na principal; a outra tela não mostra ---
open(f"{desk}/um.txt", "w").write("1")
open(f"{desk}/dois.txt", "w").write("2")
confere(lambda: estado().get("total") == "2", "os arquivos da pasta aparecem na principal")
confere(lambda: estado(outra).get("total") == "0", "e a outra tela não repete os ícones (cada ícone mora numa tela só)")
confere(lambda: estado(outra) != {}, "a outra tela tem a sua área de trabalho")
# --- 3. arrastar da principal para a outra tela ---
p = pos("um.txt")
if p:
    arrasta(ox[principal] + p[0] + p[2] // 2, p[1] + p[3] // 2, ox[outra] + 400, 300)
confere(lambda: estado(outra).get("total") == "1" and "um.txt" in lugares(outra), "arrastar para a outra tela leva o ícone para ela")
confere(lambda: lugares(outra).get("um.txt") == (3, 2), "e ele fica na célula onde foi solto")
confere(lambda: estado().get("total") == "1" and "um.txt" not in lugares(), "e some da tela de origem")
confere(lambda: os.path.exists(f"{desk}/um.txt"), "o arquivo continua na pasta (só o ícone mudou de tela)")
ini = open(f"{run}/cfg/lumna/desktop.ini").read()
confere(lambda: f"[posicoes {outra}]" in open(f"{run}/cfg/lumna/desktop.ini").read(), "o desktop.ini guarda os lugares por tela")
# --- 4. e de volta ---
p = pos("um.txt", outra)
if p:
    arrasta(ox[outra] + p[0] + p[2] // 2, p[1] + p[3] // 2, ox[principal] + 700, 400)
confere(lambda: "um.txt" in lugares() and estado(outra).get("total") == "0", "arrastar de volta devolve o ícone à principal")
# --- 5. um arquivo novo criado depois aparece na principal, não na outra ---
open(f"{desk}/tres.txt", "w").write("3")
confere(lambda: "tres.txt" in lugares() and estado(outra).get("total") == "0", "um arquivo novo nasce na principal")
# --- 6. soltar o ícone de uma tela numa PASTA da outra move o arquivo ---
os.mkdir(f"{desk}/Caixa")
confere(lambda: "Caixa" in lugares(), "a pasta Caixa aparece")
p = pos("Caixa")
q = pos("dois.txt")
if p and q:
    arrasta(ox[principal] + q[0] + q[2] // 2, q[1] + q[3] // 2, ox[principal] + p[0] + p[2] // 2, p[1] + p[3] // 2)
confere(lambda: os.path.exists(f"{desk}/Caixa/dois.txt") and not os.path.exists(f"{desk}/dois.txt"), "soltar em cima de uma pasta move o arquivo para dentro")

print(f"desktop (telas): {verificacoes} verificações, {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
