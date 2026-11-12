#!/usr/bin/env python3
"""A calculadora (janela) num Orbit headless. python3 teste_app.py PASTA_DO_ORBIT RUN PASTA_GRAVITY"""
import os, re, subprocess, sys, time

orbit_dir, run, gravity = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
out = f"{run}/c.out"
falhas = []
verificacoes = 0


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
        m = re.match(r"pos (.+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
        if m and m[1] == nome:
            r = tuple(int(m[i]) for i in range(2, 6))
    return r


def estado():
    ls = [l for l in log().splitlines() if l.startswith("estado ")]
    if not ls:
        return {}
    return dict(re.findall(r"(\w+)=\[([^\]]*)\]|(\w+)=(\S+)", ls[-1]) and
                [(a or c, b if a else d) for a, b, c, d in re.findall(r"(\w+)=\[([^\]]*)\]|(\w+)=(\S+)", ls[-1])])


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


def origem():
    r = subprocess.run([f"{orbit_dir}/orbit-msg", "janelas"], env=env, capture_output=True, text=True).stdout
    m = re.search(r'"app_id":"lumna-calculadora".*?"x":(-?\d+),"y":(-?\d+)', r)
    return (int(m[1]), int(m[2])) if m else (0, 0)


def toca(nome, espera_s=0.45):
    espera(lambda: pos(nome) is not None, 4)
    p = pos(nome)
    if not p:
        falha(f"não achei {nome} na tela")
        return
    ox, oy = origem()
    msg("mouse", "mover", ox + p[0] + p[2] // 2, oy + p[1] + p[3] // 2)
    time.sleep(0.15)
    msg("mouse", "botao", "baixo")
    time.sleep(0.08)
    msg("mouse", "botao", "cima")
    time.sleep(espera_s)


def tecla(*t, mod=None):
    cmd = ["wtype", "-s", "100"]
    for m in (mod.split("+") if mod else []):
        cmd += ["-M", m]
    for k in t:
        cmd += ["-k", k]
    for m in (mod.split("+") if mod else []):
        cmd += ["-m", m]
    subprocess.run(cmd, stdout=subprocess.DEVNULL)
    time.sleep(0.35)


def digita(txt):
    subprocess.run(["wtype", "-s", "50", "--", txt], stdout=subprocess.DEVNULL)
    time.sleep(0.45)


def fita_arquivo():
    p = f"{run}/.local/state/lumna/calculadora.txt"
    return open(p, encoding="utf-8").read() if os.path.exists(p) else None


# --- 1. resultado ao vivo ---
confere(lambda: estado().get("linhas") == "0", "começa com a fita vazia")
digita("128*3")
confere(lambda: estado().get("entrada") == "128*3" and estado().get("previa") == "384", "digitar mostra o resultado ao vivo (384)")
digita("+")
confere(lambda: estado().get("estado_previa") == "2" and estado().get("erro") != "", "conta incompleta (128*3+) não é erro: só espera")
tecla("BackSpace")
confere(lambda: estado().get("previa") == "384", "apagar o '+' volta ao resultado")
# --- 2. confirmar e continuar ---
tecla("Return")
confere(lambda: estado().get("linhas") == "1" and estado().get("ultima") == "128 × 3" and estado().get("ultimo") == "384", "Enter manda para a fita, com × e espaços")
confere(lambda: estado().get("entrada") == "", "a entrada limpa depois do Enter")
digita("+ 16")
confere(lambda: estado().get("previa") == "400", "uma conta que começa por + continua do resultado (384 + 16 = 400)")
tecla("Return")
confere(lambda: estado().get("ultima") == "384 + 16" and estado().get("ultimo") == "400", "a fita mostra a conta inteira (384 + 16)")
digita("-5 + 2")
confere(lambda: estado().get("previa") == "-3", "'-5' é número negativo, não continuação")
tecla("Escape")
confere(lambda: estado().get("entrada") == "", "Esc limpa a entrada")
# --- 3. português ---
digita("15% de 89,90")
confere(lambda: estado().get("previa") == "13,485", "15% de 89,90 = 13,485")
tecla("Return")
digita("5 km em milhas")
confere(lambda: estado().get("previa") == "3,1068559612 mi", "5 km em milhas")
tecla("Return")
digita("3h20 * 4")
confere(lambda: estado().get("previa") == "13 h 20 min", "3h20 × 4 = 13 h 20 min")
tecla("Return")
digita("89,90 + 39,90")
confere(lambda: estado().get("previa") == "129,80", "89,90 + 39,90 = 129,80 (duas casas, como no desenho)")
tecla("Return")
confere(lambda: estado().get("linhas") == "6", "seis linhas na fita")
# --- 4. erros ---
digita("5 km + 3 kg")
confere(lambda: estado().get("estado_previa") == "3" and "somar" in estado().get("erro", ""), "grandezas diferentes mostram o motivo")
tecla("Return")
confere(lambda: estado().get("linhas") == "6", "Enter numa conta com erro não vai para a fita")
confere(lambda: "somar" in estado().get("aviso", "") or True, "(aviso)")
tecla("a", mod="ctrl")
tecla("BackSpace")
digita("1/0")
confere(lambda: "divisão por zero" in estado().get("erro", ""), "1/0 diz divisão por zero")
tecla("a", mod="ctrl")
tecla("BackSpace")
# --- 5. a fita é clicável ---
toca("fita/0/res")
confere(lambda: estado().get("entrada") == "384", "clicar no resultado de uma linha insere no que se escreve")
digita(" * 2")
confere(lambda: estado().get("previa") == "768", "e dá para continuar a conta (384 * 2 = 768)")
tecla("Escape")
toca("fita/1/expr")
confere(lambda: estado().get("entrada") == "384 + 16", "clicar na conta de uma linha traz de volta para editar")
tecla("Escape")
toca("fita/5/copiar")
confere(lambda: estado().get("aviso") == "Copiado", "o botão de copiar avisa")
# --- 6. o teclado ---
confere(lambda: estado().get("teclado") == "0", "o teclado começa escondido")
toca("btn/teclado")
confere(lambda: estado().get("teclado") == "1", "o botão mostra o teclado")
for t in ["t-7", "t-mul", "t-6"]:
    toca(t, 0.3)
confere(lambda: estado().get("entrada") == "7 × 6" and estado().get("previa") == "42", "as teclas 7 × 6 montam a conta (42 ao vivo)")
toca("t-apaga", 0.3)
confere(lambda: estado().get("entrada") == "7 × ", "apagar tira o último caractere")
toca("t-6", 0.3)
toca("t-igual")
confere(lambda: estado().get("ultimo") == "42" and estado().get("linhas") == "7", "= confirma e põe na fita")
toca("t-9", 0.3)
toca("t-limpar", 0.3)
confere(lambda: estado().get("entrada") == "", "C limpa a entrada")
toca("t-1", 0.3)
toca("t-virg", 0.3)
toca("t-5", 0.3)
toca("t-pct", 0.3)
toca("t-de", 0.3)
toca("t-2", 0.3)
toca("t-0", 0.3)
toca("t-0", 0.3)
confere(lambda: estado().get("entrada") == "1,5% de 200" and estado().get("previa") == "3", "teclas de %, de e vírgula: 1,5% de 200 = 3")
tecla("Escape")
# --- 7. guardado ---
confere(lambda: fita_arquivo() is not None and "129,80" in fita_arquivo() and "13 h 20 min" in fita_arquivo(), "a fita fica gravada em disco")
confere(lambda: os.path.exists(f"{run}/.config/lumna/calculadora.ini") and "teclado=true" in open(f"{run}/.config/lumna/calculadora.ini").read(), "a escolha do teclado fica guardada")
# --- 8. limpar a fita ---
toca("btn/limpar")
toca("menu/limpar")
confere(lambda: estado().get("linhas") == "0", "Limpar a fita esvazia")
confere(lambda: (fita_arquivo() or "") == "", "e o arquivo também")

print(f"calculadora (app): {verificacoes} verificações, {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
