#!/usr/bin/env python3
"""A janela do gerenciador de processos num Orbit headless. python3 teste_app.py PASTA_DO_ORBIT RUN PID_SERVICO PID_OCUPADO PASTA_GRAVITY"""
import os, re, subprocess, sys, time

orbit_dir, run, srv, ocupado, gravity = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
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


def estado():
    r = {}
    for l in log().splitlines():
        if l.startswith("estado "):
            r = dict(re.findall(r"(\w+)=(\[[^\]]*\]|\S+)", l))
    return r


def espera(cond, seg=5):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def confere(cond, o_que, seg=5):
    if not espera(cond, seg):
        falha(o_que)
        subprocess.run(["grim", f"{run}/falha-{len(falhas)}.png"])


def toca(nome, espera_s=0.5):
    espera(lambda: pos(nome) is not None, 4)  # a posição só sai no quadro seguinte (a lista redesenha a cada 2 s)
    p = pos(nome)
    if not p:
        falha(f"não achei {nome} na tela")
        return
    x, y = p[0] + p[2] // 2, p[1] + p[3] // 2
    msg("mouse", "mover", x, y)
    time.sleep(0.15)
    msg("mouse", "botao", "baixo")
    time.sleep(0.08)
    msg("mouse", "botao", "cima")
    time.sleep(espera_s)


def tecla(*t, mod=None):
    cmd = ["wtype", "-s", "250"]
    for m in (mod.split("+") if mod else []):
        cmd += ["-M", m]
    for k in t:
        cmd += ["-k", k]
    for m in (mod.split("+") if mod else []):
        cmd += ["-m", m]
    subprocess.run(cmd, stdout=subprocess.DEVNULL)
    time.sleep(0.4)


def viva(p):
    try:
        os.kill(p, 0)
        return open(f"/proc/{p}/stat").read().split(") ")[1][0] != "Z"
    except OSError:
        return False


# o serviço: tempo real e fora do OOM killer
pol = open(f"/proc/{srv}/stat").read().split(") ")[1].split()
if pol[38] not in ("1", "2"):
    falha(f"o serviço devia ter prioridade de tempo real (política {pol[38]})")
oom = open(f"/proc/{srv}/oom_score_adj").read().strip()
if oom != "-900":
    print(f"aviso: oom_score_adj {oom} (este ambiente pode não deixar baixar abaixo de 0)")

# o chamador abre a janela pelo serviço
subprocess.Popen([f"{gravity}/processos"], stdout=open(out, "w"), stderr=subprocess.DEVNULL)  # sem serviço no caminho isto abriria sozinho; com ele, só alterna
time.sleep(2)
janela = subprocess.run(["pgrep", "-f", "processos janela"], capture_output=True, text=True).stdout.split()
if not janela:
    falha("'lumna-processos' devia pedir ao serviço que abrisse a janela")
    sys.exit(1)
# a janela do serviço escreve no stdout do serviço (não no nosso arquivo): reabre a janela com a saída capturada
subprocess.run([f"{gravity}/processos", "fechar"])
time.sleep(1)
subprocess.run(["pkill", "-f", "processos janela"])
time.sleep(0.5)
jan = subprocess.Popen([f"{gravity}/processos", "janela"], stdout=open(out, "w"), stderr=open(f"{run}/jan.err", "w"))
confere(lambda: estado().get("n") is not None, "a janela devia abrir e mostrar a lista")
confere(lambda: pos("busca") is not None and pos("encerrar") is not None, "a janela devia ter a busca e 'Encerrar processo'")
n0 = int(estado().get("n", 0))
if n0 < 2:
    falha(f"a lista devia ter processos (tem {n0})")

# o clique num cabeçalho muda a ordem (prova que o toque chega à camada)
toca("col/mem")
confere(lambda: estado().get("ordem", "").startswith("1"), "tocar em 'Memória' devia ordenar por memória")
toca("col/mem")
# a busca acha o processo ocupado e só ele
toca("busca")
subprocess.run(["wtype", "-s", "250", "ocupadoteste"], stdout=subprocess.DEVNULL)
time.sleep(0.8)
confere(lambda: str(ocupado) in [x.rstrip("*") for l in log().splitlines() if l.startswith("ordem") for x in l.split()[1:]] and int(estado().get("n", 99)) <= 4, "a busca devia achar o processo ocupado")
# marca a linha e encerra
toca(f"linha/{ocupado}")
confere(lambda: estado().get("sel") == str(ocupado), "tocar na linha devia selecioná-la")
toca("encerrar", 1.5)
confere(lambda: not viva(ocupado), "'Encerrar processo' devia encerrar o processo ocupado", 6)
# o processo é do próprio usuário: não pede confirmação
if estado().get("confirmar") != "0":
    falha("um processo do próprio usuário não devia pedir confirmação")

# prioridade pelo menu: um novo processo ocupado, "Baixa" = nice 10
novo = subprocess.Popen([f"{run}/ocupadoteste", "600"])
confere(lambda: str(novo.pid) in [x.rstrip("*") for l in log().splitlines() if l.startswith("ordem") for x in l.split()[1:]], "o processo novo devia aparecer na lista (atualiza sozinha)", 6)
toca(f"linha/{novo.pid}")
toca("mais")
toca("prio/0", 1.5)
nice = lambda p: int(open(f"/proc/{p}/stat").read().split(") ")[1].split()[16])
confere(lambda: nice(novo.pid) == 10, f"'Baixa' devia pôr nice 10 (nice {nice(novo.pid)})")
toca(f"linha/{novo.pid}")
toca("mais")
toca("prio/4", 1.5)
pol2 = lambda p: open(f"/proc/{p}/stat").read().split(") ")[1].split()[38]
confere(lambda: pol2(novo.pid) in ("1", "2"), f"'Tempo real' devia pôr SCHED_FIFO/RR (política {pol2(novo.pid)})")
# processo da sessão (nome de infraestrutura) pede confirmação; Cancelar não encerra, Encerrar encerra
import shutil
shutil.copy("/bin/sleep", f"{run}/lumna-barra")
infra = subprocess.Popen([f"{run}/lumna-barra", "600"])
toca("busca")
subprocess.run(["wtype", "-s", "250", "-M", "ctrl", "-k", "a", "-m", "ctrl", "-k", "BackSpace"], stdout=subprocess.DEVNULL)
subprocess.run(["wtype", "-s", "250", "lumna-barra"], stdout=subprocess.DEVNULL)
confere(lambda: pos(f"linha/{infra.pid}") is not None, "a busca devia mostrar o processo da sessão")
toca(f"linha/{infra.pid}")
toca("encerrar", 0.8)
confere(lambda: estado().get("confirmar") == "1", "encerrar um processo da sessão devia pedir confirmação")
toca("confirmar-nao", 0.6)
confere(lambda: estado().get("confirmar") == "0", "Cancelar devia fechar a confirmação")
if not viva(infra.pid):
    falha("Cancelar não devia encerrar o processo")
toca("encerrar", 0.8)
toca("confirmar-sim", 1.5)
confere(lambda: infra.poll() is not None, "confirmar devia encerrar o processo da sessão", 6)
novo.kill()
# Esc fecha a janela (limpa a busca primeiro)
tecla("Escape")
tecla("Escape")
confere(lambda: jan.poll() is not None, "Esc devia fechar a janela", 4)
sys.exit(1 if falhas else 0)
