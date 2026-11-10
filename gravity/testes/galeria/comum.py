"""Ajudantes dos testes de interface da Galeria (Orbit headless, mouse virtual e a saída de depuração do app)."""
import os, re, subprocess, sys, time

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    return subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, capture_output=True, text=True).stdout


def log():
    return open(f"{run}/g.out", encoding="utf-8", errors="replace").read()


def pos(nome):
    r = None
    for l in log().splitlines():
        m = re.match(r"pos (.+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
        if m and m[1] == nome:
            r = tuple(int(m[i]) for i in range(2, 6))
    return r


def estado():
    r = {}
    for l in log().splitlines():
        if l.startswith("estado "):
            r = dict(re.findall(r"(\w+)=(\[[^\]]*\]|\S+)", l[7:]))
    return r


def espera(cond, seg=4):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def confere(cond, o_que, seg=4):
    if not espera(cond, seg):
        e = estado()
        falha(o_que + f" (estado {({k: e.get(k) for k in ('tela', 'vista', 'nvis', 'nsel', 'busca', 'albuns', 'aberta')})})")


def origem():
    import json
    j = json.loads(msg("janelas"))["janelas"]
    return j[0]["x"], j[0]["y"]


def centro(nome):
    p = pos(nome)
    fim = time.time() + 3
    while not p and time.time() < fim:   # a tela acabou de trocar: as posições chegam um quadro depois
        time.sleep(0.1)
        p = pos(nome)
    if not p:
        raise RuntimeError(f"não achei {nome} na tela")
    ox, oy = origem()
    return ox + p[0] + p[2] // 2, oy + p[1] + p[3] // 2


def aponta(x, y):
    msg("mouse", "mover", x, y)
    time.sleep(0.15)


def toca(nome, espera_s=0.45):
    x, y = centro(nome)
    aponta(x, y)
    msg("toque", "baixo", 1, x, y)
    time.sleep(0.08)
    msg("toque", "cima", 1)
    time.sleep(espera_s)


def segura(nome):
    x, y = centro(nome)
    aponta(x, y)
    msg("toque", "baixo", 1, x, y)
    time.sleep(0.9)
    msg("toque", "cima", 1)
    time.sleep(0.6)


def passa(nome):
    """o mouse passa por cima (dois movimentos: o primeiro, que entra na janela, ainda não acende o que está embaixo)"""
    x, y = centro(nome)
    aponta(x, y)
    aponta(x + 1, y + 1)
    time.sleep(0.4)


def marca(k):
    """marca a mídia k pelo quadradinho dela"""
    passa(f"g-cel/{k}")
    espera(lambda: pos(f"g-chk/{k}") is not None, 3)
    toca(f"g-chk/{k}")


def tecla(*teclas, mod=None):
    cmd = ["wtype", "-s", "250"]
    if mod:
        for m in mod.split("+"):
            cmd += ["-M", m]
    for t in teclas:
        cmd += ["-k", t]
    if mod:
        for m in mod.split("+"):
            cmd += ["-m", m]
    subprocess.run(cmd, stdout=subprocess.DEVNULL)
    time.sleep(0.4)


def digita(txt):
    subprocess.run(["wtype", "-s", "250", txt], stdout=subprocess.DEVNULL)
    time.sleep(0.4)


def le(p):
    try:
        return open(p, encoding="utf-8").read()
    except FileNotFoundError:
        return ""


def itens():
    return [x for x in estado().get("itens", "[]")[1:-1].split(",") if x]
