#!/usr/bin/env python3
"""A janela do gerenciador de processos num Orbit headless. python3 teste_app.py PASTA_DO_ORBIT RUN PID_SERVICO PID_OCUPADO PASTA_GRAVITY"""
import os, re, signal, subprocess, sys, time

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


def janela_estado():
    e = estado()
    m = re.match(r"(-?\d+),(-?\d+),(\d+),(\d+)", e.get("win", ""))
    return tuple(int(v) for v in m.groups()) if m else None


def arrasta(nome_ini, dx, dy, ox=6, oy=6):
    """aperta no ponto (a caixa registrada + o deslocamento) e arrasta por dx,dy em passos"""
    espera(lambda: pos(nome_ini) is not None, 3)
    p = pos(nome_ini)
    if not p:
        falha(f"não achei {nome_ini}")
        return
    x, y = p[0] + ox, p[1] + oy
    msg("mouse", "mover", x, y); time.sleep(0.3)
    msg("mouse", "botao", "baixo"); time.sleep(0.2)
    for i in range(1, 6):
        msg("mouse", "mover", x + dx * i // 5, y + dy * i // 5); time.sleep(0.08)
    msg("mouse", "botao", "cima"); time.sleep(0.6)


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

# sem erro do layout (o Clay tem teto de elementos por quadro: a lista só desenha as linhas à vista)
if "out of bounds" in log():
    falha("o layout não devia estourar o teto de elementos do Clay (a lista devia ser virtualizada)")
# a lista cabe na janela (com o rodapé à vista) e rola: arrastar a barra de rolagem leva a lista para baixo
confere(lambda: pos("d/rodape") is not None and pos("d/cartao") is not None, "a janela devia mostrar o rodapé da lista")
cartao, rodape, rolagem = pos("d/cartao"), pos("d/rodape"), pos("d/rolagem")
if cartao and rodape and rodape[1] + rodape[3] > cartao[1] + cartao[3]:
    falha(f"o rodapé devia ficar dentro do cartão (a lista estourava a janela): rodapé {rodape} cartão {cartao}")
if rolagem:
    bx, by = rolagem[0] + rolagem[2] - 6, rolagem[1] + 30
    msg("mouse", "mover", bx, by); time.sleep(0.3)
    msg("mouse", "botao", "baixo"); time.sleep(0.2)
    for dy in range(30, 330, 40):
        msg("mouse", "mover", bx, rolagem[1] + dy); time.sleep(0.1)
    msg("mouse", "botao", "cima"); time.sleep(0.8)
    rol = float(estado().get("rol", "0"))
    if rol < 200:
        falha(f"arrastar a barra devia rolar a lista (rolou {rol})")
    msg("mouse", "mover", bx, rolagem[1] + 20); time.sleep(0.2)
    msg("mouse", "botao", "baixo"); time.sleep(0.15); msg("mouse", "botao", "cima"); time.sleep(0.2)
    for dy in range(20, 5, -5):
        pass
# a janela: redimensionar pelos cantos, mover pela barra, maximizar, restaurar, minimizar e voltar; a posição fica guardada
w0 = janela_estado()
if not w0:
    falha("o estado da janela devia constar")
else:
    arrasta("borda/BR", -80, -50)
    w1 = janela_estado()
    if not (w1[2] == w0[2] - 80 and w1[3] == w0[3] - 50 and w1[0] == w0[0] and w1[1] == w0[1]):
        falha(f"o canto inferior direito devia encolher a janela em 80x50 ({w0} -> {w1})")
    arrasta("borda/TL", -30, -20)
    w2 = janela_estado()
    if not (w2[2] == w1[2] + 30 and w2[3] == w1[3] + 20 and w2[0] == w1[0] - 30 and w2[1] == w1[1] - 20):
        falha(f"o canto superior esquerdo devia crescer para cima e para a esquerda ({w1} -> {w2})")
    arrasta("barra", 40, 30, ox=100, oy=20)
    w3 = janela_estado()
    if not (w3[0] == w2[0] + 40 and w3[1] == w2[1] + 30 and w3[2:] == w2[2:]):
        falha(f"arrastar a barra devia mover a janela em 40,30 sem mudar o tamanho ({w2} -> {w3})")
    # o tamanho mínimo vale
    arrasta("borda/BR", -2000, -2000)
    w4 = janela_estado()
    if w4[2] < 960 or w4[3] < 540:
        falha(f"a janela não pode ficar menor que 960x540 ({w4})")
    toca("btn/max")
    confere(lambda: estado().get("max") == "1", "o botão maximizar devia maximizar")
    c = pos("janela/cartao")
    if not c or c[0] != 0 or c[1] != 0 or c[2] != 1280 or c[3] != 720:
        falha(f"maximizada, o cartão devia ocupar a tela inteira ({c})")
    toca("btn/max")
    confere(lambda: estado().get("max") == "0", "o botão restaurar devia voltar ao tamanho de antes")
    if janela_estado() != w4:
        falha(f"restaurar devia voltar à posição e ao tamanho de antes ({w4} -> {janela_estado()})")
    # o duplo clique na barra maximiza
    p = pos("barra")
    bx, by = p[0] + 100, p[1] + 20
    msg("mouse", "mover", bx, by); time.sleep(0.3)
    for _ in range(2):
        msg("mouse", "botao", "baixo"); time.sleep(0.06); msg("mouse", "botao", "cima"); time.sleep(0.12)
    confere(lambda: estado().get("max") == "1", "o duplo clique na barra devia maximizar")
    toca("btn/max")
    confere(lambda: estado().get("max") == "0", "restaurar de novo")
    # minimizar: vira a pastilha (a barra e a lista somem da cena), e voltar pela pastilha
    toca("btn/min")
    confere(lambda: estado().get("mini") == "1" and pos("pastilha") is not None, "minimizar devia virar a pastilha")
    toca("pastilha")
    confere(lambda: estado().get("mini") == "0", "tocar na pastilha devia voltar a janela")
    # o atalho (SIGUSR1): minimizada, volta; aberta, fecha. Aqui: minimiza, atalho volta
    toca("btn/min")
    confere(lambda: estado().get("mini") == "1", "minimizar de novo")
    os.kill(jan.pid, signal.SIGUSR1)
    confere(lambda: estado().get("mini") == "0", "o atalho (SIGUSR1) com a janela minimizada devia trazê-la de volta")
    ini = open(f"{run}/.config/lumna/processos.ini").read() if os.path.exists(f"{run}/.config/lumna/processos.ini") else ""
    if f"w={w4[2]}" not in ini or f"h={w4[3]}" not in ini:
        falha(f"a posição e o tamanho da janela deviam ficar guardados ({ini!r})")
# o clique num cabeçalho muda a ordem (prova que o toque chega à camada)
toca("col/mem")
confere(lambda: estado().get("ordem", "").startswith("1"), "tocar em 'Memória' devia ordenar por memória")
toca("col/mem")
# a busca acha o processo ocupado e só ele
toca("busca")
subprocess.run(["wtype", "-s", "250", str(ocupado)], stdout=subprocess.DEVNULL)
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
toca("busca")
subprocess.run(["wtype", "-s", "250", "-M", "ctrl", "-k", "a", "-m", "ctrl", "-k", "BackSpace"], stdout=subprocess.DEVNULL)
subprocess.run(["wtype", "-s", "250", str(novo.pid)], stdout=subprocess.DEVNULL)
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
subprocess.run(["wtype", "-s", "250", str(infra.pid)], stdout=subprocess.DEVNULL)
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
# "Fechar jogo": um jogo de mentira (SteamGameId no ambiente) filho de um "steam" de mentira; fecha só o jogo
shutil.copy("/bin/sleep", f"{run}/jogofalso")
steam = subprocess.Popen(["sh", "-c", 'SteamGameId=730 "$0" 600; sleep 60', f"{run}/jogofalso"])
confere(lambda: estado().get("jogo") == "730" and pos("fechar-jogo") is not None, "um processo com SteamGameId devia aparecer como 'Jogo em execução'", 6)
gpid = int(subprocess.run(["pgrep", "-f", f"{run}/jogofalso 600"], capture_output=True, text=True).stdout.split()[0])
toca("fechar-jogo", 1.5)
confere(lambda: not viva(gpid), "'Fechar jogo' devia encerrar o jogo na hora", 6)
if steam.poll() is not None:
    falha("'Fechar jogo' não devia derrubar o 'steam' (o pai do jogo)")
confere(lambda: estado().get("jogo") == "0", "depois de fechar, o aviso do jogo devia sumir", 6)
steam.kill()
# Desempenho: a aba abre os gráficos (cpu, memória, gpu, disco, rede) e volta para Processos
toca("aba/desempenho", 1.0)
confere(lambda: estado().get("tela") == "1", "a aba Desempenho devia abrir")
time.sleep(2.5)  # duas amostras
confere(lambda: all(pos(f"graf/{g}") is not None and pos(f"graf/{g}")[2] > 100 and pos(f"graf/{g}")[3] > 40 for g in ("cpu", "mem", "gpu", "disco", "rede")), "os cinco gráficos de Desempenho deviam aparecer com tamanho útil")
subprocess.run(["grim", f"{run}/desempenho.png"])
toca("aba/processos", 0.8)
confere(lambda: estado().get("tela") == "0", "a aba Processos devia voltar")
# o atalho com a janela aberta (não minimizada) a fecha
os.kill(jan.pid, signal.SIGUSR1)
confere(lambda: jan.poll() is not None, "o atalho (SIGUSR1) com a janela aberta devia fechá-la", 4)
jan = subprocess.Popen([f"{gravity}/processos", "janela"], stdout=open(out, "w"), stderr=open(f"{run}/jan.err", "a"))
confere(lambda: estado().get("tela") is not None, "a janela devia abrir de novo", 6)
# Esc fecha a janela (limpa a busca primeiro)
tecla("Escape")
tecla("Escape")
confere(lambda: jan.poll() is not None, "Esc devia fechar a janela", 4)
sys.exit(1 if falhas else 0)
