#!/usr/bin/env python3
"""O app lumna-teclado inteiro, com um Orbit headless: um campo de texto de mentira pede o teclado, o teclado sobe, e toques de verdade (orbit-msg toque,
vários dedos juntos) escrevem, apagam, seguram para as variantes, trocam de página e fecham. Confere o que o campo recebeu.
python3 teste_app.py PASTA_DO_ORBIT PASTA_RUN"""
import re, subprocess, sys, time, os

orbit_dir, run = sys.argv[1], sys.argv[2]
sock = f"{run}/orbit.wayland-0.sock"
env = dict(os.environ, ORBIT_SOCK=sock)
falhas = []


def falha(msg):
    falhas.append(msg)
    print("FALHOU:", msg, file=sys.stderr)


def msg(*args):
    subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, args)], env=env, stdout=subprocess.DEVNULL)


def ler(arq):
    try:
        return open(f"{run}/{arq}", encoding="utf-8", errors="replace").read()
    except FileNotFoundError:
        return ""


def espera(cond, segundos=6):
    fim = time.time() + segundos
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def teclas():
    """As caixas do teclado mais recentes (nome → x y w h), na ordem em que o app as mostrou."""
    saida = {}
    for l in ler("teclado.out").splitlines():
        m = re.match(r"tecla (\S+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
        if m:
            saida[m[1]] = tuple(int(m[i]) for i in range(2, 6))
    return saida


def caixa():
    ult = None
    for l in ler("teclado.out").splitlines():
        m = re.match(r"caixa (\d+) (\d+) (\d+) (\d+) camada (\d+)", l)
        if m:
            ult = tuple(int(m[i]) for i in range(1, 6))
    return ult


ALTURA_TELA = 720


def centro(nome):
    cx, cy, cw, ch, camada = caixa()
    x, y, w, h = teclas()[nome]
    return cx + x + w // 2, ALTURA_TELA - camada + cy + y + h // 2


def baixo(dedo, nome):
    x, y = centro(nome)
    msg("toque", "baixo", dedo, x, y)


def cima(dedo):
    msg("toque", "cima", dedo)


def toca(nome, dedo=1):
    baixo(dedo, nome)
    time.sleep(0.12)
    cima(dedo)
    time.sleep(0.25)


def texto_do_campo():
    ult = None
    for l in ler("cliente.out").splitlines():
        m = re.match(r"texto \[(.*)\]$", l)
        if m:
            ult = m[1]
    return ult


def confere(esperado, o_que):
    espera(lambda: texto_do_campo() == esperado, 3)
    if texto_do_campo() != esperado:
        falha(f"{o_que}: o campo devia ter [{esperado}] e tem [{texto_do_campo()}]")


# o teclado sobe sozinho (sem teclado físico num Orbit headless)
if not espera(lambda: "mostrar" in ler("teclado.out") and caixa() and "tecla q" in ler("teclado.out").lower() or "tecla Q" in ler("teclado.out")):
    falha("o teclado não subiu quando o campo abriu: " + ler("teclado.out")[:400])
    print(ler("teclado.out")[-1500:])
    sys.exit(1)
time.sleep(0.6)

# 1. a primeira letra da frase é maiúscula sozinha (Shift ligado); a segunda, minúscula
toca("O")
confere("O", "primeira letra maiúscula")
espera(lambda: "q" in teclas())
toca("l")
confere("Ol", "segunda letra minúscula")

# 2. dois dedos ao mesmo tempo: as duas teclas entram, na ordem em que sobem
baixo(1, "a")
baixo(2, "m")
time.sleep(0.15)
cima(1)
time.sleep(0.1)
cima(2)
time.sleep(0.3)
confere("Olam", "dois dedos juntos")

# 3. apagar
toca("⌫")
confere("Ola", "apagar")

# 4. segurar o a: abre as variantes e soltar sem escolher escreve o a
baixo(1, "a")
time.sleep(0.8)
if "variantes" not in ler("teclado.out"):
    falha("segurar o a não abriu as variantes")
cima(1)
time.sleep(0.4)
confere("Olaa", "soltar sem escolher escreve a tecla")
toca("⌫")

# 5. segurar o a e escorregar até o ã
baixo(1, "a")
time.sleep(0.8)
alts = {}
for l in ler("teclado.out").splitlines():
    m = re.match(r"variante (\S+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
    if m:
        alts[m[1]] = tuple(int(m[i]) for i in range(2, 6))
if "ã" not in alts:
    falha(f"as variantes do a não têm ã: {alts}")
else:
    cx, cy, cw, ch, camada = caixa()
    x, y, w, h = alts["ã"]
    msg("toque", "move", 1, cx + x + w // 2, ALTURA_TELA - camada + cy + y + h // 2)
    time.sleep(0.3)
    cima(1)
    time.sleep(0.4)
    confere("Olaã", "variante escolhida")

# 6. espaço e Enter (Enter vai como tecla)
toca("espaco")
confere("Olaã ", "espaço")
toca("↵")
if not espera(lambda: "tecla 28 press" in ler("cliente.out")):
    falha("o Enter não chegou como tecla")

# 7. símbolos e volta
toca("?123")
espera(lambda: "@" in teclas())
toca("@")
confere("Olaã @", "símbolo")
toca("ABC")
espera(lambda: "q" in teclas())

# 8. emojis
toca("😀")  # a tecla da página de emojis tem o próprio emoji
time.sleep(0.5)
ts = teclas()
if "😃" not in ts:
    falha("a página de emojis não abriu: " + str(list(ts)[:10]))
else:
    toca("😃")
    confere("Olaã @😃", "emoji")
toca("ABC")
espera(lambda: "q" in teclas())

# 9. fechar (⌄) esconde; o campo continua aberto
toca("⌄")
if not espera(lambda: ler("teclado.out").count("esconder") >= 1):
    falha("a tecla ⌄ não escondeu o teclado")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("teclado na tela (app inteiro): ok")
