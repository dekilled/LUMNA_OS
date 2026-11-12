#!/usr/bin/env python3
"""A tela Serviços e o atalho da Inicialização. python3 teste_servicos.py PASTA_DO_ORBIT RUN PASTA_GRAVITY"""
import os, re, subprocess, sys, time

orbit_dir, run, gravity = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
falhas = []
out = f"{run}/janela.out"


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


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


def sv():
    """escopo, filtro, visíveis, total (da última linha 'estado')"""
    r = None
    for l in log().splitlines():
        m = re.search(r" sv=(-?\d+),(-?\d+),(\d+),(\d+)", l) if l.startswith("estado ") else None
        if m:
            r = tuple(int(v) for v in m.groups())
    return r


def espera(cond, seg=6):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def confere(cond, o_que, seg=6):
    if not espera(cond, seg):
        falha(o_que)
        subprocess.run(["grim", f"{run}/falha-{len(falhas)}.png"])


def toca(nome, espera_s=0.6):
    espera(lambda: pos(nome) is not None, 5)
    p = pos(nome)
    if not p:
        falha(f"não achei {nome} na tela")
        return
    x, y = p[0] + p[2] // 2, p[1] + p[3] // 2
    msg("mouse", "mover", x, y); time.sleep(0.15)
    msg("mouse", "botao", "baixo"); time.sleep(0.08)
    msg("mouse", "botao", "cima"); time.sleep(espera_s)


jan = subprocess.Popen([f"{gravity}/processos", "janela"], stdout=open(out, "w"), stderr=open(f"{run}/jan.err", "w"))
confere(lambda: pos("aba/servicos") is not None, "a janela devia abrir com a aba Serviços")
toca("aba/servicos", 1.5)
# os serviços do sistema: 11 (8 serviços e 3 timers; not-found e masked ficam de fora)
confere(lambda: sv() == (0, -1, 11, 11), f"o escopo do sistema devia listar 11 serviços ({sv()})")
toca("sv/chip/3")   # Em pausa
confere(lambda: sv() and sv()[1] == 3 and sv()[2] == 1, f"'Em pausa' devia mostrar 1 serviço (o congelado) ({sv()})")
toca("sv/chip/2")   # Programados
confere(lambda: sv() and sv()[1] == 2 and sv()[2] == 2, f"'Programados' devia mostrar os 2 timers ativos ({sv()})")
toca("sv/chip/1")   # Parados
confere(lambda: sv() and sv()[1] == 1 and sv()[2] == 5, f"'Parados' devia mostrar 5 (cups, backup que falhou, 2 serviços de timer e o timer desligado) ({sv()})")
toca("sv/chip/0")   # Em execução
confere(lambda: sv() and sv()[1] == 0 and sv()[2] == 3, f"'Em execução' devia mostrar 3 ({sv()})")
toca("sv/chip/-1")  # Todos
confere(lambda: sv() and sv()[1] == -1 and sv()[2] == 11, f"'Todos' devia voltar aos 11 ({sv()})")
# a busca
toca("sv/busca")
subprocess.run(["wtype", "-s", "250", "network"], stdout=subprocess.DEVNULL)
confere(lambda: sv() and sv()[2] == 1, f"buscar 'network' devia achar só o NetworkManager ({sv()})")
subprocess.run(["wtype", "-s", "250", "-M", "ctrl", "-k", "a", "-m", "ctrl", "-k", "BackSpace"], stdout=subprocess.DEVNULL)
confere(lambda: sv() and sv()[2] == 11, f"limpar a busca devia voltar aos 11 ({sv()})")
# uma linha mostra a descrição, o estado em português e o PID
primeira = pos("sv/linha/0")
if not primeira:
    falha("a primeira linha da lista devia aparecer")
# o atalho da Inicialização: chama o Painel na seção certa e fecha o gerenciador (que cobriria o Painel)
toca("aba/inicializacao", 1.5)
confere(lambda: os.path.exists(f"{run}/painel.args"), "o atalho devia chamar o lumna-painel")
args = open(f"{run}/painel.args").read().strip() if os.path.exists(f"{run}/painel.args") else ""
if args != "--secao inicializacao/apps":
    falha(f"o atalho devia abrir o Painel em 'inicializacao/apps' (veio {args!r})")
confere(lambda: jan.poll() is not None, "o gerenciador devia fechar ao abrir o Painel", 4)
sys.exit(1 if falhas else 0)
