#!/usr/bin/env python3
"""Gera o tema do GRUB do LUMNA (packages/lumna-boot/grub/lumna/) a partir da arte iso/branding/GRUB_BACKGROUND.png (1920x1080: o fundo, o logo, o painel
com o título e as dicas já desenhados; o painel vem VAZIO). O GRUB só desenha por cima os itens do menu, com o destaque em gradiente e os ícones.

Fica versionado o resultado (imagens e fontes .pf2): a ISO não roda isto. Precisa de Pillow e do grub-mkfont (grub-common) só para gerar.
Uso: python3 gera-tema.py [arte.png] [pasta_de_saida]
"""
import glob
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw

AQUI = os.path.dirname(os.path.abspath(__file__))
ARTE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(AQUI, "..", "GRUB_BACKGROUND.png")
SAIDA = sys.argv[2] if len(sys.argv) > 2 else os.path.join(AQUI, "..", "..", "..", "packages", "lumna-boot", "grub", "lumna")
os.makedirs(os.path.join(SAIDA, "icons"), exist_ok=True)

# a área dos itens dentro do painel (pixels de uma tela 1920x1080; o tema usa porcentagens, então vale em telas maiores com a mesma proporção)
ITENS_X, ITENS_Y, ITENS_L, ITENS_A = 186, 215, 574, 669
ALTURA_ITEM, ESPACO_ITEM = 68, 28   # o destaque tem 30 px a mais que o item (15 em cima e 15 embaixo): 98 de altura; o passo entre itens é 96 (7 entradas cabem no painel)
FONTE = 24

Image.open(ARTE).convert("RGB").save(os.path.join(SAIDA, "background.png"), optimize=True)

# ---- o destaque (gradiente azul -> roxo, cantos arredondados) em nove partes
Wd, Ht, R = 400, 64, 16
grad = Image.new("RGBA", (Wd, Ht))
px = grad.load()
for x in range(Wd):
    t = x / (Wd - 1)
    cor = (int(46 + (116 - 46) * t), int(66 + (58 - 66) * t), int(200 + (188 - 200) * t))
    for y in range(Ht):
        sh = 1.0 + 0.10 * (0.5 - y / Ht)
        px[x, y] = tuple(min(255, int(c * sh)) for c in cor) + (255,)
mask = Image.new("L", (Wd * 4, Ht * 4), 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, Wd * 4 - 1, Ht * 4 - 1), radius=R * 4, fill=255)
grad.putalpha(mask.resize((Wd, Ht), Image.LANCZOS))
borda = Image.new("RGBA", (Wd, Ht), (0, 0, 0, 0))
ImageDraw.Draw(borda).rounded_rectangle((0, 0, Wd - 1, Ht - 1), radius=R, outline=(150, 160, 255, 150), width=1)
grad = Image.alpha_composite(grad, borda)
cortes = {"nw": (0, 0, R, R), "n": (R, 0, Wd - R, R), "ne": (Wd - R, 0, Wd, R), "w": (0, R, R, Ht - R), "c": (R, R, Wd - R, Ht - R),
          "e": (Wd - R, R, Wd, Ht - R), "sw": (0, Ht - R, R, Ht), "s": (R, Ht - R, Wd - R, Ht), "se": (Wd - R, Ht - R, Wd, Ht)}
for nome, caixa in cortes.items():
    grad.crop(caixa).save(os.path.join(SAIDA, f"select_{nome}.png"))

# ---- os ícones (32 px, traço lilás): o GRUB os escolhe pela classe da entrada (--class)
S = 128
COR = (190, 180, 255, 255)
LARG = 8


def icone(nomes, desenho):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    desenho(ImageDraw.Draw(im))
    im = im.resize((32, 32), Image.LANCZOS)
    for n in nomes:
        im.save(os.path.join(SAIDA, "icons", f"{n}.png"))


def estrela(d):
    c = S // 2
    d.polygon([(c, 6), (c + 15, c - 15), (S - 6, c), (c + 15, c + 15), (c, S - 6), (c - 15, c + 15), (6, c), (c - 15, c - 15)], fill=COR)


def avancadas(d):
    for x in (46, 82):
        d.line((x, 18, x, S - 18), fill=COR, width=LARG)
    for y in (46, 82):
        d.line((18, y, S - 18, y), fill=COR, width=LARG)


def firmware(d):
    d.rounded_rectangle((28, 28, S - 28, S - 28), radius=9, outline=COR, width=LARG)
    d.rectangle((50, 50, S - 50, S - 50), outline=COR, width=LARG)
    for t in (44, 64, 84):
        d.line((t, 10, t, 26), fill=COR, width=7)
        d.line((t, S - 26, t, S - 10), fill=COR, width=7)
        d.line((10, t, 26, t), fill=COR, width=7)
        d.line((S - 26, t, S - 10, t), fill=COR, width=7)


def janelas(d):
    m = S // 2
    for (x, y) in ((14, 16), (m + 4, 16), (14, m + 4), (m + 4, m + 4)):
        d.rectangle((x, y, x + m - 22, y + m - 22), fill=COR)


def ubuntu(d):
    d.ellipse((26, 26, S - 26, S - 26), outline=COR, width=LARG)
    for (x, y) in ((20, S // 2), (S - 22, 38), (S - 22, S - 38)):
        d.ellipse((x - 13, y - 13, x + 13, y + 13), fill=COR)


def linux(d):
    d.ellipse((32, 12, S - 32, S - 14), outline=COR, width=LARG)
    d.ellipse((48, 44, 60, 58), fill=COR)
    d.ellipse((68, 44, 80, 58), fill=COR)
    d.polygon([(54, 70), (74, 70), (64, 80)], fill=COR)


icone(["lumna"], estrela)
icone(["advanced"], avancadas)
icone(["firmware"], firmware)
icone(["windows"], janelas)
icone(["ubuntu"], ubuntu)
icone(["gnu-linux", "linux", "biglinux", "manjaro", "arch", "debian", "fedora", "os"], linux)

# ---- as fontes (Inter) em .pf2
for estilo in ("Regular", "Medium"):
    ttf = f"/usr/share/fonts/opentype/inter/Inter-{estilo}.otf"
    if not os.path.exists(ttf):
        ttf = (glob.glob(f"/usr/share/fonts/**/Inter-{estilo}.*", recursive=True) or [None])[0]
    destino = os.path.join(SAIDA, f"inter-{estilo.lower()}-{FONTE}.pf2")
    if ttf and shutil.which("grub-mkfont"):
        subprocess.run(["grub-mkfont", "-s", str(FONTE), "-o", destino, ttf], check=True, capture_output=True)
    elif not os.path.exists(destino):
        sys.exit("faltam a fonte Inter e o grub-mkfont para gerar " + destino)

# ---- o tema
W, H = 1920, 1080
def pc(v, tot):
    """o GRUB só aceita porcentagem inteira: N% mais ou menos alguns pixels (N%+px)"""
    n = (v * 100) // tot
    return "%d%%%+d" % (n, v - (n * tot) // 100)
open(os.path.join(SAIDA, "theme.txt"), "w").write(f"""# Tema do GRUB do LUMNA OS. Gerado por iso/branding/grub/gera-tema.py: não edite à mão.
# O fundo (estrelas, logo, painel, título e dicas) é uma imagem; o GRUB desenha só os itens do menu por cima.
desktop-image: "background.png"
desktop-image-scale-method: "stretch"
desktop-color: "#07060f"
title-text: ""
terminal-font: "Inter Regular {FONTE}"

+ boot_menu {{
	left = {pc(ITENS_X, W)}
	top = {pc(ITENS_Y, H)}
	width = {pc(ITENS_L, W)}
	height = {pc(ITENS_A, H)}
	item_font = "Inter Regular {FONTE}"
	selected_item_font = "Inter Medium {FONTE}"
	item_color = "#aeb0cc"
	selected_item_color = "#ffffff"
	icon_width = 32
	icon_height = 32
	item_icon_space = 24
	item_height = {ALTURA_ITEM}
	item_padding = 22
	item_spacing = {ESPACO_ITEM}
	selected_item_pixmap_style = "select_*.png"
	scrollbar = false
}}
""")
print("pronto em", SAIDA, "—", sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(SAIDA) for f in fs) // 1024, "KB")
