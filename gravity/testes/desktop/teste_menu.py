#!/usr/bin/env python3
"""O menu de um ícone da área de trabalho. python3 teste_menu.py PASTA_DO_ORBIT RUN"""
import os, re, subprocess, sys, time

orbit_dir, run = sys.argv[1], sys.argv[2]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
out = f"{run}/dk.out"
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)
    subprocess.run(["grim", f"{run}/falha-{len(falhas)}.png"])


def msg(*a):
    subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, stdout=subprocess.DEVNULL)


def log():
    return open(out, encoding="utf-8", errors="replace").read() if os.path.exists(out) else ""


def pos(nome):
    """a última posição conhecida; o log só escreve o que mudou, então um menu fechado não some dele: quem confere desaparecimento usa 'fechado'"""
    r = None
    for l in log().splitlines():
        m = re.match(r"pos (.+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
        if m and m[1] == nome:
            r = tuple(int(m[i]) for i in range(2, 6))
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
        falha(o_que)


def clique(x, y, botao="esquerdo", espera_s=0.5):
    msg("mouse", "mover", x, y)
    time.sleep(0.2)
    if botao == "esquerdo":
        msg("mouse", "botao", "baixo")
        time.sleep(0.08)
        msg("mouse", "botao", "cima")
    else:
        msg("mouse", "botao", "baixo", botao)
        time.sleep(0.08)
        msg("mouse", "botao", "cima", botao)
    time.sleep(espera_s)


def centro(nome):
    espera(lambda: pos(nome) is not None, 4)
    p = pos(nome)
    if not p:
        falha(f"não achei {nome} na tela")
        return None
    return p[0] + p[2] // 2, p[1] + p[3] // 2


def toca(nome, **k):
    c = centro(nome)
    if c:
        clique(*c, **k)


def menu_de(item):
    """fecha o que estiver aberto e abre o menu do ícone (botão direito)"""
    clique(900, 650)
    toca(f"cel/{item}", botao="direito", espera_s=0.8)


def abre_submenu(linha, primeiro):
    pos_antes = pos(primeiro)
    toca(linha, espera_s=0.8)


# o desktop imprime "pos cel/NOME": o teste de menu acrescenta a mesma série "pos menu/NOME"; as células usam o prefixo cel/
def pos_cel(item):
    return pos(f"cel/{item}")


confere(lambda: pos_cel("nota.txt") is not None and pos_cel("Projetos") is not None, "os ícones aparecem")

# 1. um arquivo: Abrir com…, Abrir local do arquivo, Enviar para
menu_de("nota.txt")
confere(lambda: pos("menu/dk-m-com") is not None and pos("menu/dk-m-local") is not None and pos("menu/dk-m-enviar") is not None, "o menu de um arquivo tem Abrir com…, Abrir local e Enviar para")
confere(lambda: pos("menu/dk-m-disp") is not None, "e Enviar para dispositivo…")

# 2. Abrir local do arquivo: o Atlas recebe o arquivo
toca("menu/dk-m-local", espera_s=1.0)
confere(lambda: os.path.exists(f"{run}/atlas-abriu") and open(f"{run}/atlas-abriu").read().strip() == f"{run}/Desktop/nota.txt", "'Abrir local do arquivo' manda o Atlas abrir o arquivo")

# 3. Abrir com…: lista os aplicativos do tipo e abre
menu_de("nota.txt")
toca("menu/dk-m-com", espera_s=1.0)
confere(lambda: pos("menu/dk-ac/0") is not None, "'Abrir com…' lista os aplicativos do tipo")
toca("menu/dk-ac/0", espera_s=1.0)
confere(lambda: os.path.exists(f"{run}/aberto.log") and f"{run}/Desktop/nota.txt" in open(f"{run}/aberto.log").read(), "escolher o aplicativo abre o arquivo com ele", 6)

# 4. Enviar para: Documentos
menu_de("nota.txt")
toca("menu/dk-m-enviar", espera_s=1.0)
confere(lambda: pos("menu/dk-ev-docs") is not None and pos("menu/dk-ev-email") is not None and pos("menu/dk-ev-desktop") is not None, "o submenu tem Área de trabalho, Documentos e E-mail")
confere(lambda: pos("menu/dk-ev-nuvem/0") is not None, "a conta na nuvem aparece")
toca("menu/dk-ev-docs", espera_s=1.0)
confere(lambda: os.path.exists(f"{run}/Documentos/nota.txt"), "enviar para Documentos copia o arquivo", 6)
confere(lambda: os.path.exists(f"{run}/Desktop/nota.txt"), "o original continua na área de trabalho")

# 5. Enviar para a nuvem
menu_de("nota.txt")
toca("menu/dk-m-enviar", espera_s=1.0)
toca("menu/dk-ev-nuvem/0", espera_s=1.0)
confere(lambda: os.path.exists(f"{run}/Nuvem/Conta/nota.txt"), "enviar para a nuvem copia o arquivo para a pasta da conta", 6)

# 6. E-mail
menu_de("nota.txt")
toca("menu/dk-m-enviar", espera_s=1.0)
toca("menu/dk-ev-email", espera_s=1.0)
confere(lambda: os.path.exists(f"{run}/email.log") and f"--attach {run}/Desktop/nota.txt" in open(f"{run}/email.log").read(), "'E-mail' abre o cliente com o arquivo anexado", 6)

# 7. uma pasta: sem Abrir com…, sem E-mail e sem dispositivo (pasta não anexa nem é oferecida), mas com Enviar para
antes = open(f"{run}/dk.out").read().count("pos menu/dk-m-com")
menu_de("Projetos")
confere(lambda: pos("menu/dk-m-enviar") is not None, "o menu de uma pasta tem Enviar para")
toca("menu/dk-m-enviar", espera_s=1.0)
n_com = open(f"{run}/dk.out").read().count("pos menu/dk-m-com")
if n_com != antes:
    falha("o menu de uma pasta não devia ter 'Abrir com…'")
clique(900, 650)
sys.exit(1 if falhas else 0)
