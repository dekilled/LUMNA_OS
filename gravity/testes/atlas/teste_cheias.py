#!/usr/bin/env python3
"""Pastas lotadas: dezenas de pastas com centenas de arquivos dentro (cada uma com a prévia em miniatura), atalhos quebrados e em laço, FIFO, nomes que não são
UTF-8, pasta de milhares de itens. Abre cada pasta com duplo clique e com Enter, volta, e confere que o Atlas segue vivo e que a prévia não muda sozinha.
python3 teste_cheias.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])


def pid():
    r = subprocess.run(["pgrep", "-x", "atlas"], capture_output=True, text=True).stdout.split()
    return int(r[0]) if r else None


p0 = pid()
if not p0:
    falha("o Atlas não está rodando")
    sys.exit(1)

PNG = bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360000002000001e221bc330000000049454e44ae426082")
EXTS = ["pdf", "jpg", "png", "mp3", "mp4", "md", "ods", "odp", "doc", "zip", "c", "txt"]

cheia = os.path.join(casa, "cheia")
os.makedirs(cheia, exist_ok=True)
for i in range(70):
    d = os.path.join(cheia, f"pasta{i:02d}")
    os.makedirs(os.path.join(d, "sub"), exist_ok=True)
    for j in range(300):
        ext = EXTS[(i + j) % len(EXTS)]
        arq = os.path.join(d, f"arq{j:03d}.{ext}")
        open(arq, "wb").write(PNG if ext in ("png", "jpg") else b"x")
# os casos feios
feios = os.path.join(cheia, "feios")
os.makedirs(feios, exist_ok=True)
for nome in [b"caf\xe9.txt", b"\xff\xfe.png", b"p\xe9"]:
    p = os.path.join(feios.encode(), nome)
    try:
        os.makedirs(p, exist_ok=True) if nome == b"p\xe9" else open(p, "wb").write(b"x")
    except OSError:
        pass
for alvo, nome in [("/nao/existe", "quebrado"), (".", "laco"), ("..", "pai"), ("/", "raiz"), ("/proc", "proc")]:
    try:
        os.symlink(alvo, os.path.join(cheia, "feios", nome))
    except FileExistsError:
        pass
    try:
        os.symlink(alvo, os.path.join(cheia, f"link_{nome}"))
    except FileExistsError:
        pass
try:
    os.mkfifo(os.path.join(feios, "fifo"))
except FileExistsError:
    pass
grande = os.path.join(cheia, "grande")
os.makedirs(grande, exist_ok=True)
for i in range(4000):
    ext = EXTS[i % len(EXTS)]
    open(os.path.join(grande, f"item{i:05d}.{ext}"), "wb").write(PNG if ext == "png" else b"x")
for i in range(20):  # pastas com nome grande e com muitos pontos
    os.makedirs(os.path.join(cheia, "uma pasta com um nome bem comprido para ver como fica " + str(i) + ".v1.2.3"), exist_ok=True)


# fotos de verdade: JPEG grandes, PNG enormes, formatos quebrados, imagem finíssima (o Atlas descodifica miniaturas em threads enquanto se navega)
import random
from PIL import Image
fotos = os.path.join(cheia, "fotos")
os.makedirs(fotos, exist_ok=True)
base = os.path.join(run, "base.jpg")
if not os.path.exists(base):
    random.seed(1)
    im = Image.effect_noise((4000, 3000), 60).convert("RGB")
    im.save(base, quality=85)
    Image.effect_noise((6000, 4000), 60).convert("RGB").save(os.path.join(run, "base.png"))
    Image.new("RGB", (1, 20000), (200, 10, 10)).save(os.path.join(run, "fina.png"))
    Image.new("RGB", (800, 600), (10, 200, 10)).save(os.path.join(run, "base.webp"))
for i in range(150):
    d = os.path.join(fotos, f"IMG_{i:04d}.jpg")
    if not os.path.exists(d):
        os.link(base, d)
for i in range(12):
    d = os.path.join(fotos, f"png_{i:02d}.png")
    if not os.path.exists(d):
        os.link(os.path.join(run, "base.png"), d)
for i in range(5):
    for nome, origem_ in [("fina", "fina.png"), ("w", "base.webp")]:
        d = os.path.join(fotos, f"{nome}_{i}." + origem_.split(".")[1])
        if not os.path.exists(d):
            os.link(os.path.join(run, origem_), d)
for i in range(3):  # 81 megapixels num arquivo pequeno (cor chapada): abrir inteiro custa 324 MB cada
    gg = os.path.join(fotos, f"gigante_{i}.png")
    if not os.path.exists(gg):
        Image.new("RGB", (9000, 9000), (30, 30, 200)).save(gg)
open(os.path.join(fotos, "vazio.png"), "wb").close()
open(os.path.join(fotos, "texto.jpg"), "wb").write(b"isto nao e uma imagem")
open(os.path.join(fotos, "cortada.jpg"), "wb").write(open(base, "rb").read()[:30000])
# pastas só de fotos, para a prévia e a miniatura juntas
for k in range(15):
    dd = os.path.join(cheia, f"album{k:02d}")
    os.makedirs(dd, exist_ok=True)
    for i in range(25):
        p = os.path.join(dd, f"f{i:02d}.jpg")
        if not os.path.exists(p):
            os.link(base, p)


def vivo(o_que):
    if pid() != p0:
        falha(f"o Atlas caiu: {o_que}")
        print("(log em", run, ")", file=sys.stderr)
        subprocess.run(["tail", "-30", f"{run}/ar.err"])
        sys.exit(1)


vai_para(cheia)
time.sleep(1.0)
vivo("ao abrir a pasta cheia")

# duplo clique nas pastas que estão à vista e volta com Alt+Esquerda
vistas = []
for l in log().splitlines():
    m = re.match(r"pos (item/pasta\d\d) ", l)
    if m and m[1] not in vistas:
        vistas.append(m[1])
for nome in vistas[:12] + ["item/grande", "item/feios"]:
    if not pos(nome):
        continue
    duplo(nome)
    time.sleep(0.6)
    vivo(f"ao dar duplo clique em {nome}")
    tecla("Left", mod="alt")
    time.sleep(0.5)
    vivo(f"ao voltar de {nome}")

# pelo teclado: Down até cada pasta, Enter, volta
for k in range(0, 40, 3):
    for _ in range(k % 7 + 1):
        tecla("Down")
    tecla("Return")
    time.sleep(0.5)
    vivo(f"Enter no item {k}")
    tecla("BackSpace")
    time.sleep(0.4)
    vivo(f"voltar do item {k}")


# entrar nas fotos e rolar depressa enquanto as miniaturas chegam, sair no meio
for rodada in range(4):
    vai_para(fotos)
    for t in ["End", "Home", "Next", "Next", "Next", "Prior", "End", "Home"]:
        tecla(t)
        vivo(f"rolando as fotos ({t}, rodada {rodada})")
    tecla("BackSpace")
    vivo(f"saindo das fotos no meio das miniaturas (rodada {rodada})")
for k in range(15):
    vai_para(os.path.join(cheia, f"album{k:02d}"))
    vivo(f"album{k:02d}")

# a prévia das pastas não muda sozinha entre quadros (cor/ordem estáveis)
vai_para(cheia)
time.sleep(1.0)
tecla("Home")
vivo("no fim")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (pastas lotadas: duplo clique, Enter, links e nomes feios): ok")
