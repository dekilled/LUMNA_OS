#!/usr/bin/env python3
"""O editor de fotos da Galeria num Orbit headless: Ajustar, Recortar, Girar, Filtros, desfazer/refazer, comparar, zoom, salvar a cópia, descartar.
TESTE=teste_editor.py sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY   (ou: python3 teste_editor.py PASTA_DO_ORBIT PASTA_RUN CASA)"""
import hashlib, os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from comum import *
from PIL import Image

imgs = f"{casa}/Imagens"


def tela():
    return estado().get("tela")


def rgb(chave):
    return tuple(int(v) for v in estado().get(chave, "-1,-1,-1").split(","))


def num(chave):
    return float(estado().get(chave, "nan"))


def perto(c, alvo, tol=14):
    return all(abs(a - b) <= tol for a, b in zip(c, alvo))


def pronto(seg=8):
    confere(lambda: estado().get("ed_carregando") == "0" and estado().get("ed_busy") == "0" and estado().get("ed_prev", "0x0") != "0x0", "a prévia fica pronta", seg)


def abrir_editor(nome, por="botao"):
    toca("g-busca")  # a foto pode estar abaixo da dobra: a busca a põe no topo
    tecla("a", mod="ctrl")
    digita(nome.rsplit(".", 1)[0])
    confere(lambda: nome in itens(), f"a busca acha {nome}")
    toca(f"g-cel/{itens().index(nome)}")
    confere(lambda: tela() == "1", f"abrir {nome} no visualizador")
    if por == "botao":
        toca("vz-editar")
    else:
        tecla(por)
    confere(lambda: tela() == "3", f"o editor abre ({nome})")
    pronto()


def arrasta(x1, y1, x2, y2, passos=8, espera=0.07):
    aponta(x1, y1)
    time.sleep(0.2)
    msg("mouse", "botao", "baixo")
    time.sleep(0.2)
    for i in range(1, passos + 1):
        msg("mouse", "mover", int(x1 + (x2 - x1) * i / passos), int(y1 + (y2 - y1) * i / passos))
        time.sleep(espera)
    time.sleep(0.25)


def solta():
    msg("mouse", "botao", "cima")
    time.sleep(0.5)


def desliza(i, de, para, soltar=True):
    b = pos(f"ed-sl/{i}")
    ox, oy = origem()
    y = oy + b[1] + b[3] // 2
    arrasta(ox + b[0] + int(b[2] * de), y, ox + b[0] + int(b[2] * para), y)
    if soltar:
        solta()


def sha(p):
    return hashlib.sha1(open(p, "rb").read()).hexdigest()


def retangulo_da_foto():
    """onde a foto cai na prévia (espelha retangulo_da_foto de editor.c, com zoom 1)"""
    cx, cy, cw, ch = pos("ed-canvas")
    iw, ih = (int(v) for v in estado()["ed_prev"].split("x"))
    fit = min((cw - 48) / iw, (ch - 48) / ih)
    w, h = iw * fit, ih * fit
    ox, oy = origem()
    return ox + cx + cw / 2 - w / 2, oy + cy + ch / 2 - h / 2, w, h


def recorte():
    return [float(v) for v in estado()["ed_rec"].split(",")]


# ---- as fotos de mentira: quatro quadrantes (vermelho, verde / azul, amarelo) ----
cantos = Image.new("RGB", (800, 600))
for (x, y, cor) in ((0, 0, (220, 30, 30)), (400, 0, (30, 200, 30)), (0, 300, (30, 30, 220)), (400, 300, (230, 220, 30))):
    cantos.paste(cor, (x, y, x + 400, y + 300))
cantos.save(f"{imgs}/canto.png")
os.utime(f"{imgs}/canto.png", (1700000000, 1700000000))
orig_canto = sha(f"{imgs}/canto.png")
orig_lago = sha(f"{imgs}/lago.jpg")

confere(lambda: "canto.png" in itens() and estado().get("carregando") == "0", "a biblioteca vê a foto nova", 15)

# ===== 1. abrir =====
abrir_editor("lago.jpg")
e = estado()
confere(lambda: perto(rgb("ed_px"), (30, 90, 160), 6), f"a prévia mostra a foto (centro {rgb('ed_px')})")
confere(lambda: estado().get("ed_alt") == "0" and estado().get("ed_hist") == "0/1", "sem mexer em nada: sem alteração e sem histórico")
for a in range(4):
    confere(lambda a=a: pos(f"ed-aba/{a}") is not None, f"a aba {a} está na tela")
toca("ed-salvar")
time.sleep(0.5)
confere(lambda: estado().get("ed_salvando") == "0" and not os.path.exists(f"{imgs}/lago-editada.jpg"), "Salvar cópia sem alterações não faz nada")

# ===== 2. Ajustar: arrastar a exposição =====
antes = rgb("ed_px")
desliza(0, 0.5, 0.9, soltar=False)
confere(lambda: num("ed_exp") > 1.5, f"arrastar a exposição para a direita (ed_exp={estado().get('ed_exp')})")
confere(lambda: estado().get("ed_hist") == "0/1", "enquanto arrasta, ainda não entrou no histórico")
solta()
confere(lambda: estado().get("ed_hist") == "1/2", f"soltar põe no histórico ({estado().get('ed_hist')})")
pronto()
mais = rgb("ed_px")
confere(lambda: all(m > a for m, a in zip(mais, antes)), f"a prévia clareou ({antes} -> {mais})")
confere(lambda: estado().get("ed_alt") == "1", "a foto agora está alterada")

# ===== 3. tocar no valor zera =====
toca("ed-chip/0")
confere(lambda: num("ed_exp") == 0 and estado().get("ed_hist") == "2/3", f"tocar no valor zera a exposição ({estado().get('ed_exp')}, {estado().get('ed_hist')})")
pronto()
confere(lambda: perto(rgb("ed_px"), antes, 3), "e a prévia volta ao que era")

# ===== 4. cada deslizante mexe no que diz =====
nomes = {1: ("ed_con", -1), 2: ("ed_rea", -1), 3: ("ed_som", 1), 4: ("ed_tem", 1), 5: ("ed_sat", 1)}
for i, (chave, sinal) in nomes.items():
    desliza(i, 0.5, 0.5 + 0.35 * sinal)
    confere(lambda chave=chave, sinal=sinal: int(estado().get(chave, "0")) * sinal > 20, f"o deslizante {i} muda {chave} ({estado().get(chave)})")
confere(lambda: estado().get("ed_hist") == "7/8", f"cinco ajustes, cinco passos no histórico ({estado().get('ed_hist')})")

# ===== 5. desfazer e refazer =====
toca("ed-undo")
confere(lambda: estado().get("ed_hist") == "6/8" and int(estado().get("ed_sat")) == 0, "Desfazer tira o último ajuste (saturação)")
toca("ed-redo")
confere(lambda: estado().get("ed_hist") == "7/8" and int(estado().get("ed_sat")) > 20, "Refazer devolve")
tecla("z", mod="ctrl")
confere(lambda: estado().get("ed_hist") == "6/8", "Ctrl+Z desfaz")
tecla("z", mod="ctrl+shift")
confere(lambda: estado().get("ed_hist") == "7/8", "Ctrl+Shift+Z refaz")
tecla("z", mod="ctrl")
tecla("z", mod="ctrl")
confere(lambda: estado().get("ed_hist") == "5/8", "dois desfazeres")
desliza(5, 0.5, 0.2)
confere(lambda: estado().get("ed_hist") == "6/7" and int(estado().get("ed_sat")) < -20, f"um ajuste novo depois de desfazer descarta o que se podia refazer ({estado().get('ed_hist')})")

# ===== 6. Restaurar ajustes =====
toca("ed-restaurar")
confere(lambda: all(int(estado().get(k, "1")) == 0 for k in ("ed_con", "ed_rea", "ed_som", "ed_tem", "ed_sat")) and num("ed_exp") == 0, "Restaurar ajustes zera tudo")
confere(lambda: estado().get("ed_alt") == "0", "e a foto volta a ser a original")
pronto()
confere(lambda: perto(rgb("ed_px"), (30, 90, 160), 4), "a prévia também")

# ===== 7. Comparar original =====
desliza(0, 0.5, 0.8)
pronto()
claro = rgb("ed_px")
b = pos("ed-comparar")
ox, oy = origem()
aponta(ox + b[0] + b[2] // 2, oy + b[1] + b[3] // 2)
msg("mouse", "botao", "baixo")
confere(lambda: estado().get("ed_cmp") == "1", "segurar Comparar original mostra a original")
msg("mouse", "botao", "cima")
confere(lambda: estado().get("ed_cmp") == "0", "soltar volta à editada")

# ===== 8. zoom =====
toca("ed-zoom-mais")
confere(lambda: num("ed_zoom") == 2, f"zoom + dobra ({estado().get('ed_zoom')})")
toca("ed-zoom-mais")
toca("ed-zoom-menos")
toca("ed-zoom-menos")
confere(lambda: num("ed_zoom") == 1, "zoom - volta a ajustar à tela")

# ===== 9. salvar a cópia (JPEG: o formato do original) =====
copia = f"{imgs}/lago-editada.jpg"
toca("ed-salvar")
confere(lambda: os.path.exists(copia), "a cópia aparece ao lado do original", 10)
confere(lambda: tela() == "1", "e a Galeria volta ao visualizador")
confere(lambda: "lago-editada.jpg" in itens(), "a cópia entra na biblioteca", 10)
confere(lambda: sha(f"{imgs}/lago.jpg") == orig_lago, "o original não mudou nem um byte")
if os.path.exists(copia):
    c = Image.open(copia).convert("RGB")
    o = Image.open(f"{imgs}/lago.jpg").convert("RGB")
    cp, op = c.getpixel((320, 240)), o.getpixel((320, 240))
    confere(lambda: c.size == o.size and all(a > b for a, b in zip(cp, op)), f"a cópia tem o tamanho do original e é mais clara ({op} -> {cp})")
tecla("Escape")
confere(lambda: tela() == "0", "Esc volta à galeria")

# ===== 10. girar e espelhar (quadrantes) =====
abrir_editor("canto.png", por="e")  # a tecla E no visualizador
confere(lambda: perto(rgb("ed_tl"), (220, 30, 30)) and perto(rgb("ed_tr"), (30, 200, 30)) and perto(rgb("ed_bl"), (30, 30, 220)) and perto(rgb("ed_br"), (230, 220, 30)), f"os quadrantes de partida ({[rgb(k) for k in ('ed_tl', 'ed_tr', 'ed_bl', 'ed_br')]})")
toca("ed-aba/2")
confere(lambda: estado().get("ed_aba") == "2", "a aba Girar")
toca("ed-girar-d")
pronto()
confere(lambda: estado().get("ed_giro") == "1" and estado().get("ed_prev") == "600x800", f"girar à direita: 600x800 ({estado().get('ed_prev')})")
confere(lambda: perto(rgb("ed_tl"), (30, 30, 220)) and perto(rgb("ed_tr"), (220, 30, 30)) and perto(rgb("ed_bl"), (230, 220, 30)) and perto(rgb("ed_br"), (30, 200, 30)),
        f"cada quadrante foi para o lugar certo ({[rgb(k) for k in ('ed_tl', 'ed_tr', 'ed_bl', 'ed_br')]})")
toca("ed-esp-h")
pronto()
confere(lambda: estado().get("ed_eh") == "1" and perto(rgb("ed_tl"), (220, 30, 30)) and perto(rgb("ed_tr"), (30, 30, 220)), "espelhar na horizontal troca esquerda e direita")
toca("ed-undo")
toca("ed-undo")
pronto()
confere(lambda: estado().get("ed_giro") == "0" and estado().get("ed_prev") == "800x600" and perto(rgb("ed_tl"), (220, 30, 30)), "dois desfazeres voltam à foto de partida")
toca("ed-girar-e")
pronto()
confere(lambda: estado().get("ed_giro") == "3" and perto(rgb("ed_tl"), (30, 200, 30)), "girar à esquerda")
toca("ed-esp-v")
pronto()
confere(lambda: estado().get("ed_ev") == "1" and perto(rgb("ed_tl"), (220, 30, 30)) and perto(rgb("ed_br"), (230, 220, 30)), f"girar à esquerda e espelhar na vertical: vermelho em cima à esquerda ({rgb('ed_tl')})")
toca("ed-girar-restaurar")
pronto()
confere(lambda: estado().get("ed_giro") == "0" and estado().get("ed_eh") == "0" and estado().get("ed_ev") == "0" and estado().get("ed_alt") == "0", "Restaurar giro")

# ===== 11. recortar =====
toca("ed-aba/1")
confere(lambda: estado().get("ed_aba") == "1", "a aba Recortar")
pronto()
toca("ed-prop/2")  # 1:1
r = recorte()
confere(lambda: abs(r[2] * 800 - r[3] * 600) < 2 and r[3] > 0.99, f"1:1: quadrado de 600x600 ({r})")
toca("ed-prop/5")  # 16:9
r = recorte()
confere(lambda: abs((r[2] * 800) / (r[3] * 600) - 16 / 9) < 0.02 and r[2] > 0.99, f"16:9 ({r})")
toca("ed-inverter")
r = recorte()
confere(lambda: abs((r[2] * 800) / (r[3] * 600) - 9 / 16) < 0.02, f"Inverter: 9:16 ({r})")
toca("ed-prop/0")  # Livre
toca("ed-recorte-restaurar")
confere(lambda: recorte() == [0.0, 0.0, 1.0, 1.0], "Restaurar recorte")
# arrastar o canto de baixo à direita para dentro (livre)
fx, fy, fw, fh = retangulo_da_foto()
arrasta(int(fx + fw - 2), int(fy + fh - 2), int(fx + fw - 2 - fw * 0.30), int(fy + fh - 2 - fh * 0.20))
solta()
r = recorte()
confere(lambda: abs(r[2] - 0.70) < 0.06 and abs(r[3] - 0.80) < 0.06 and r[0] == 0 and r[1] == 0, f"arrastar o canto encolhe o recorte ({r})")
confere(lambda: estado().get("ed_alt") == "1", "e a foto fica alterada")
# arrastar por dentro move
fx, fy, fw, fh = retangulo_da_foto()
r0 = recorte()
cx0, cy0 = fx + fw * (r0[0] + r0[2] / 2), fy + fh * (r0[1] + r0[3] / 2)
arrasta(int(cx0), int(cy0), int(cx0 + fw * 0.15), int(cy0 + fh * 0.10))
solta()
r1 = recorte()
confere(lambda: abs(r1[0] - 0.15) < 0.05 and abs(r1[1] - 0.10) < 0.05 and abs(r1[2] - r0[2]) < 1e-3, f"arrastar por dentro move o recorte ({r0} -> {r1})")
# a borda da direita, com 4:3 travado, mantém a proporção
toca("ed-prop/3")
r = recorte()
fx, fy, fw, fh = retangulo_da_foto()
bx, by = fx + fw * (r[0] + r[2]), fy + fh * (r[1] + r[3] / 2)
arrasta(int(bx), int(by), int(bx - fw * 0.10), int(by))
solta()
r = recorte()
confere(lambda: abs((r[2] * 800) / (r[3] * 600) - 4 / 3) < 0.03, f"com 4:3 travado a proporção se mantém ao arrastar a borda ({r}: {(r[2] * 800) / (r[3] * 600):.3f})")
confere(lambda: r[0] >= -1e-6 and r[1] >= -1e-6 and r[0] + r[2] <= 1 + 1e-6 and r[1] + r[3] <= 1 + 1e-6, "e o recorte fica dentro da foto")
final = recorte()

# ===== 12. filtros =====
toca("ed-aba/3")
confere(lambda: estado().get("ed_aba") == "3", "a aba Filtros")
pronto()
toca("ed-filtro/1")  # P&B
pronto()
t = rgb("ed_tl")
confere(lambda: estado().get("ed_filtro") == "1" and max(t) - min(t) <= 2, f"P&B: o recorte sai cinza ({t})")
confere(lambda: pos("ed-sl/6") is not None, "com filtro aparece a Intensidade")
desliza(6, 1.0, 0.5)
pronto()
t = rgb("ed_tl")
confere(lambda: 40 <= int(estado()["ed_int"]) <= 60 and max(t) - min(t) > 15, f"a intensidade a ~50% mistura com a cor ({estado()['ed_int']}, {t})")
toca("ed-filtro/2")  # Sépia
pronto()
t = rgb("ed_tl")
confere(lambda: estado().get("ed_filtro") == "2" and t[0] >= t[1] >= t[2], f"Sépia: vermelho >= verde >= azul ({t})")
toca("ed-filtro/0")
confere(lambda: estado().get("ed_filtro") == "0", "Original tira o filtro")

# ===== 13. salvar com giro, recorte e ajuste (PNG: o formato do original) =====
toca("ed-aba/2")
toca("ed-girar-d")  # gira (o recorte é desfeito: vale na foto de antes)
pronto()
confere(lambda: recorte() == [0.0, 0.0, 1.0, 1.0], "girar desfaz o recorte")
toca("ed-aba/1")
pronto()
toca("ed-prop/2")  # 1:1 na foto girada (600x800): 600x600
r = recorte()
toca("ed-aba/0")
desliza(0, 0.5, 0.65)
pronto()
toca("ed-salvar")
copia = f"{imgs}/canto-editada.png"
confere(lambda: os.path.exists(copia), "a segunda cópia (PNG) é salva", 10)
confere(lambda: tela() == "1", "volta ao visualizador (veio de lá)")
if os.path.exists(copia):
    c = Image.open(copia).convert("RGB")
    confere(lambda: c.size == (600, 600), f"a cópia tem o tamanho do recorte 1:1 da foto girada ({c.size})")
    # girada à direita: em cima, azul (esq) e vermelho (dir); embaixo, amarelo e verde; o recorte 1:1 centraliza: pega 100 px de cima a menos
    tl = c.getpixel((20, 20))
    confere(lambda: tl[2] > 200 and tl[0] < 120, f"o canto de cima à esquerda da cópia é azul (girado) {tl}")
    import os as _os
    mt = int(_os.stat(copia).st_mtime)
    confere(lambda: mt == 1700000000, f"a cópia tem a data do original ({mt})")
confere(lambda: sha(f"{imgs}/canto.png") == orig_canto, "o original (PNG) não mudou")
tecla("Escape")

# ===== 14. descartar =====
abrir_editor("lago.jpg")
n_antes = len([f for f in os.listdir(imgs) if "editada" in f])
tecla("Escape")  # sem alterações: sai direto
confere(lambda: tela() == "1", "Esc sem alterações sai na hora")
toca("vz-editar")
confere(lambda: tela() == "3", "abre de novo")
pronto()
desliza(2, 0.5, 0.8)
toca("ed-cancelar")
confere(lambda: estado().get("ed_dlg") == "1" and pos("ed-dlg-descartar") is not None, "Cancelar com alterações pergunta")
toca("ed-dlg-continuar")
confere(lambda: estado().get("ed_dlg") == "0" and tela() == "3" and int(estado()["ed_rea"]) != 0, "Continuar editando mantém tudo")
tecla("Escape")
confere(lambda: estado().get("ed_dlg") == "1", "Esc também pergunta")
tecla("Escape")
confere(lambda: estado().get("ed_dlg") == "0" and tela() == "3", "Esc de novo fecha a pergunta")
toca("ed-voltar")
confere(lambda: estado().get("ed_dlg") == "1", "Voltar à galeria também pergunta")
toca("ed-dlg-descartar")
confere(lambda: tela() == "1", "Descartar sai")
confere(lambda: len([f for f in os.listdir(imgs) if "editada" in f]) == n_antes, "e nada foi salvo")
tecla("Escape")

# ===== 15. uma foto que não abre =====
toca("g-busca")
tecla("a", mod="ctrl")
digita("quebrada")
confere(lambda: "quebrada.jpg" in itens(), "a busca acha a foto quebrada")
if "quebrada.jpg" in itens():
    toca(f"g-cel/{itens().index('quebrada.jpg')}")
    time.sleep(0.5)
    if tela() == "1":
        tecla("e")
        confere(lambda: tela() != "3" or estado().get("ed_carregando") == "1", "foto quebrada: o editor não fica preso")
        confere(lambda: tela() != "3", "foto quebrada: volta sozinho", 8)
        tecla("Escape")

# o Galeria continua viva
if not subprocess.run(["pgrep", "-x", "galeria"], capture_output=True).stdout:
    falha("a Galeria caiu durante o teste do editor")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("galeria (editor de fotos: ajustar, recortar, girar, filtros, desfazer, comparar, zoom, salvar e descartar): ok")
