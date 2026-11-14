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

# O desenho (iso/branding/GRUB_MOCKUP.png: o esboço do Eric, 1920x1080) e o que o GRUB desenha por cima da arte vazia (GRUB_BACKGROUND.png): o título, a lista com o destaque
# em gradiente, os ícones e a barra de início automático com a contagem. Medidas tiradas do esboço, em pixels de uma tela 1920x1080 (o tema usa porcentagens + pixels).
BARRA_X, BARRA_Y, BARRA_L, BARRA_A = 137, 183, 482, 71       # o destaque do item escolhido
PASSO = 84                                                     # de um item ao seguinte
ITENS_A = 8 * PASSO                                            # a lista aguenta 8 entradas
TITULO_X, TITULO_Y, TITULO_L, TITULO_A = 137, 120, 482, 40
PROG_X, PROG_Y, PROG_L, PROG_A = 137, 981, 552, 12              # a barra de início automático
CONT_Y, CONT_A = 997, 24                                       # o texto da contagem, embaixo da barra
FONTE_ITEM, FONTE_TITULO, FONTE_CONT = (int(os.environ.get(k, v)) for k, v in (("F_ITEM", 19), ("F_TITULO", 24), ("F_CONT", 16)))
# a lista: o destaque é a área do item aumentada pelas bordas (RX dos lados, RY em cima e embaixo), e o passo entre itens é item_spacing + 41 (calibrado olhando a tela)
ALT_ITEM, PAD_ITEM, ESP_ITEM, ESP_ICONE = (int(os.environ.get(k, v)) for k, v in (("A_ITEM", 47), ("P_ITEM", 0), ("E_ITEM", 43), ("E_ICONE", 28)))
PROG_H = int(os.environ.get("PROG_H", PROG_A))
PROG_MODO = os.environ.get("PROG_MODO", "so_destaque")
PROG_ESTILO = {"pixmaps": '\tbar_style = "progress_bar_*.png"\n\thighlight_style = "progress_highlight_*.png"', "so_destaque": '\thighlight_style = "progress_highlight_*.png"', "cores": '\tbg_color = "#3e3e3e"\n\tfg_color = "#5e18ec"\n\tborder_color = "#3e3e3e"'}[PROG_MODO]
DX, DY, DL, PDY = (int(os.environ.get(k, 0)) for k in ("DX", "DY", "DL", "PDY"))
DL = DL or 4   # (a largura do destaque no esboço: 484)
PDY = PDY or 1   # (o GRUB põe a barra 1 px acima do pedido)   # ajustes finos da lista (calibração)

fundo = Image.open(ARTE).convert("RGB")
# o trilho cinza da barra de início fica no fundo (o GRUB dá à barra uma borda interna que impediria o preenchimento de ocupar o trilho inteiro)
_trilho = Image.new("RGB", (PROG_L * 4, PROG_A * 4), (62, 62, 62))
_mascara = Image.new("L", (PROG_L * 4, PROG_A * 4), 0)
ImageDraw.Draw(_mascara).rounded_rectangle((0, 0, PROG_L * 4 - 1, PROG_A * 4 - 1), radius=6 * 4, fill=255)
fundo.paste(_trilho.resize((PROG_L, PROG_A), Image.LANCZOS), (PROG_X, PROG_Y), _mascara.resize((PROG_L, PROG_A), Image.LANCZOS))
fundo.save(os.path.join(SAIDA, "background.png"), optimize=True)


def nove_partes(img, r_x, r_y, prefixo):
    """corta uma imagem nas nove partes que o GRUB espera (prefixo_nw.png ... prefixo_c.png); r_x e r_y são as bordas"""
    Wd, Ht = img.size
    cortes = {"nw": (0, 0, r_x, r_y), "n": (r_x, 0, Wd - r_x, r_y), "ne": (Wd - r_x, 0, Wd, r_y), "w": (0, r_y, r_x, Ht - r_y), "c": (r_x, r_y, Wd - r_x, Ht - r_y),
              "e": (Wd - r_x, r_y, Wd, Ht - r_y), "sw": (0, Ht - r_y, r_x, Ht), "s": (r_x, Ht - r_y, Wd - r_x, Ht), "se": (Wd - r_x, Ht - r_y, Wd, Ht)}
    for nome, caixa in cortes.items():
        img.crop(caixa).save(os.path.join(SAIDA, f"{prefixo}_{nome}.png"))


def retangulo_arredondado(w, h, raio, cor_de, cor_para=None):
    """w x h com cantos arredondados; cor_para dá um gradiente da esquerda para a direita"""
    im = Image.new("RGBA", (w, h))
    px = im.load()
    for x in range(w):
        t = x / max(1, w - 1)
        cor = cor_de if cor_para is None else tuple(int(cor_de[i] + (cor_para[i] - cor_de[i]) * t) for i in range(3))
        for y in range(h):
            px[x, y] = tuple(cor) + (255,)
    mask = Image.new("L", (w * 4, h * 4), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, w * 4 - 1, h * 4 - 1), radius=raio * 4, fill=255)
    im.putalpha(mask.resize((w, h), Image.LANCZOS))
    return im


# ---- o destaque: do azul-marinho (35, 41, 85) ao violeta (86, 3, 188), como no esboço
R = 12           # o raio dos cantos
RX, RY = 20, 12  # as bordas do destaque: ele passa da área do item por RX dos lados e RY em cima e embaixo; o ícone fica a RX da borda do destaque (no esboço, 20)
nove_partes(retangulo_arredondado(BARRA_L, BARRA_A, R, (35, 41, 85), (86, 3, 188)), RX, RY, "select")
# ---- a barra de início automático: o trilho cinza fica no fundo (acima) e o GRUB desenha só o preenchimento violeta, de pontas redondas, com a mesma altura do trilho (12 px).
# (Com bar_style o GRUB põe uma borda interna e uma altura mínima que deixam o preenchimento menor que o trilho; sem ele o preenchimento ocupa a barra inteira.)
# (O GRUB dá à barra de início no mínimo 28 px de altura, e o "height" do tema não baixa disso. A imagem do preenchimento tem esses 28 px, com margem transparente em cima e embaixo, e só os 12 do meio aparecem, no lugar do trilho que está no fundo.)
PROG_CAIXA = 28
PROG_IMG = PROG_CAIXA
PROG_M = (PROG_IMG - PROG_A) // 2
_hl = Image.new("RGBA", (40, PROG_IMG), (0, 0, 0, 0))
_hl.paste(retangulo_arredondado(40, PROG_A, 6, (94, 24, 236)), (0, PROG_M))
nove_partes(_hl, PROG_IMG // 2 - 1, PROG_IMG // 2 - 1, "progress_highlight")
for velha in glob.glob(os.path.join(SAIDA, "progress_bar_*.png")):
    os.remove(velha)

# ---- os ícones (32 px, traço lilás): o GRUB os escolhe pela classe da entrada (--class)
S = 128
COR = (240, 240, 248, 255)
LARG = 7


def icone(nomes, desenho, girar=0):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    desenho(ImageDraw.Draw(im))
    if girar:
        im = im.rotate(girar, resample=Image.BICUBIC)
    im = im.resize((32, 32), Image.LANCZOS)
    for n in nomes:
        im.save(os.path.join(SAIDA, "icons", f"{n}.png"))


def estrela(d):
    """o losango do LUMNA: lilás, de pontas arredondadas (o quadrado vira losango na rotação, feita em icone)"""
    c = S // 2
    r = 42   # (o quadrado de 2r de lado, girado 45 graus, cabe no ícone: a diagonal 2r vezes raiz de 2 tem de ser menor que 128)
    d.rounded_rectangle((c - r, c - r, c + r, c + r), radius=12, fill=(140, 96, 228, 255))


def avancadas(d):
    """a engrenagem (contorno branco, como no esboço)"""
    import math
    c = S // 2
    pts = []
    dentes = 8
    for i in range(dentes * 4):
        ang = (math.pi * 2) * i / (dentes * 4) - math.pi / 2
        # quatro pontos por dente: raiz, raiz, ponta, ponta
        r = 58 if (i % 4) in (1, 2) else 44
        pts.append((c + r * math.cos(ang), c + r * math.sin(ang)))
    d.line(pts + [pts[0]], fill=COR, width=LARG, joint="curve")
    d.ellipse((c - 20, c - 20, c + 20, c + 20), outline=COR, width=LARG)


def firmware(d):
    d.rounded_rectangle((28, 28, S - 28, S - 28), radius=9, outline=COR, width=LARG)
    d.rectangle((50, 50, S - 50, S - 50), outline=COR, width=LARG)
    for t in (44, 64, 84):
        d.line((t, 10, t, 26), fill=COR, width=7)
        d.line((t, S - 26, t, S - 10), fill=COR, width=7)
        d.line((10, t, 26, t), fill=COR, width=7)
        d.line((S - 26, t, S - 10, t), fill=COR, width=7)


def janelas(d):
    """a bandeira do Windows: quatro quadrados azuis (#00a4ef) que ocupam o ícone, como no esboço"""
    azul = (0, 164, 239, 255)
    for (x0, y0, x1, y1) in ((8, 12, 58, 60), (66, 12, 120, 60), (8, 68, 58, 116), (66, 68, 120, 116)):
        d.rectangle((x0, y0, x1, y1), fill=azul)


def ubuntu(d):
    d.ellipse((26, 26, S - 26, S - 26), outline=COR, width=LARG)
    for (x, y) in ((20, S // 2), (S - 22, 38), (S - 22, S - 38)):
        d.ellipse((x - 13, y - 13, x + 13, y + 13), fill=COR)


def linux(d):
    d.ellipse((32, 12, S - 32, S - 14), outline=COR, width=LARG)
    d.ellipse((48, 44, 60, 58), fill=COR)
    d.ellipse((68, 44, 80, 58), fill=COR)
    d.polygon([(54, 70), (74, 70), (64, 80)], fill=COR)


icone(["lumna"], estrela, girar=45)
icone(["advanced"], avancadas)
icone(["firmware"], firmware)
icone(["windows"], janelas)
icone(["ubuntu"], ubuntu)
icone(["gnu-linux", "linux", "biglinux", "manjaro", "arch", "debian", "fedora", "os"], linux)

# ---- as fontes (Open Sans, a do esboço) em .pf2: itens (regular), título (regular) e contagem (negrito)
FONTE_BARRA = int(os.environ.get("FONTE_BARRA", 2))   # a barra de início tem altura mínima igual à da fonte do texto dela (que não mostramos): uma fonte minúscula deixa a barra ter 12 px
FONTES = [("Regular", FONTE_ITEM), ("Regular", FONTE_TITULO), ("Bold", FONTE_CONT), ("Regular", FONTE_BARRA)]
for velha in glob.glob(os.path.join(SAIDA, "opensans-*.pf2")):
    os.remove(velha)  # as de tamanhos que o tema não usa mais
for estilo, tam in FONTES:
    ttf = f"/usr/share/fonts/truetype/open-sans/OpenSans-{estilo}.ttf"
    if not os.path.exists(ttf):
        ttf = (glob.glob(f"/usr/share/fonts/**/OpenSans-{estilo}.ttf", recursive=True) or [None])[0]
    destino = os.path.join(SAIDA, f"opensans-{estilo.lower()}-{tam}.pf2")
    if ttf and shutil.which("grub-mkfont"):
        subprocess.run(["grub-mkfont", "-s", str(tam), "-o", destino, ttf], check=True, capture_output=True)
    elif not os.path.exists(destino):
        sys.exit("faltam a fonte Open Sans (fonts-open-sans) e o grub-mkfont para gerar " + destino)
for velha in glob.glob(os.path.join(SAIDA, "inter-*.pf2")):
    os.remove(velha)  # a fonte da versão anterior do tema

# ---- o tema
W, H = 1920, 1080
def pc(v, tot):
    """o GRUB só aceita porcentagem inteira: N% mais ou menos alguns pixels (N%+px)"""
    n = (v * 100) // tot
    return "%d%%%+d" % (n, v - (n * tot) // 100)
open(os.path.join(SAIDA, "theme.txt"), "w").write(f"""# Tema do GRUB do LUMNA OS. Gerado por iso/branding/grub/gera-tema.py: não edite à mão.
# O fundo (a nebulosa, o logo, o painel escuro, a faixa "Selecione o seu sistema" e as dicas) é uma imagem; o GRUB desenha por cima o título, a lista com o destaque,
# os ícones e a barra de início automático.
desktop-image: "background.png"
desktop-image-scale-method: "stretch"
desktop-color: "#1b1b1b"
title-text: ""
terminal-font: "Open Sans Regular {FONTE_ITEM}"

+ label {{
	left = {pc(TITULO_X, W)}
	top = {pc(TITULO_Y, H)}
	width = {pc(TITULO_L, W)}
	height = {pc(TITULO_A, H)}
	text = "MENU DE INICIALIZAÇÃO"
	font = "Open Sans Regular {FONTE_TITULO}"
	color = "#ffffff"
	align = "left"
}}

+ boot_menu {{
	left = {pc(BARRA_X + DX, W)}
	top = {pc(BARRA_Y + DY, H)}
	width = {pc(BARRA_L + DL, W)}
	height = {pc(ITENS_A, H)}
	item_font = "Open Sans Regular {FONTE_ITEM}"
	selected_item_font = "Open Sans Regular {FONTE_ITEM}"
	item_color = "#ffffff"
	selected_item_color = "#ffffff"
	icon_width = 32
	icon_height = 32
	item_icon_space = {ESP_ICONE}
	item_height = {ALT_ITEM}
	item_padding = {PAD_ITEM}
	item_spacing = {ESP_ITEM}
	selected_item_pixmap_style = "select_*.png"
	scrollbar = false
}}

+ progress_bar {{
	id = "__timeout__"
	left = {pc(PROG_X, W)}
	top = {pc(PROG_Y - PROG_M + PDY, H)}
	width = {pc(PROG_L, W)}
	height = {PROG_CAIXA}
	show_text = false
	font = "Open Sans Regular {FONTE_BARRA}"
{PROG_ESTILO}
}}

+ label {{
	id = "__timeout__"
	left = {pc(PROG_X, W)}
	top = {pc(CONT_Y, H)}
	width = {pc(PROG_L, W)}
	height = {CONT_A}
	text = "INÍCIO AUTOMÁTICO EM %d SEGUNDOS"
	font = "Open Sans Bold {FONTE_CONT}"
	color = "#ffffff"
	align = "center"
}}
""")
print("pronto em", SAIDA, "—", sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(SAIDA) for f in fs) // 1024, "KB")
