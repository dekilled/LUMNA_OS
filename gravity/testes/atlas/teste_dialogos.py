#!/usr/bin/env python3
"""O que só aparece de verdade na tela: os diálogos (propriedades, com abas e Aplicar), os submenus ao lado do menu de contexto e o menu e os cartões
da página Início. Aqui não basta o elemento existir na árvore: confere-se o pixel (um diálogo invisível que engole os cliques parece uma janela travada).
python3 teste_dialogos.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

barra_ini = f"{casa}/.config/lumna/atlas/barra.ini"


def pixel(x, y):
    out = subprocess.run(["grim", "-g", f"{x},{y} 1x1", "-t", "ppm", "-"], capture_output=True).stdout
    return tuple(out[-3:])


def brilho(x, y):
    return sum(pixel(x, y))


def segura_xy(x, y):
    ox, oy = origem()
    msg("toque", "baixo", 1, ox + x, oy + y)
    time.sleep(0.9)
    msg("toque", "cima", 1)
    time.sleep(0.6)


vai_para(docs)
os.makedirs(f"{docs}/ZProp", exist_ok=True)
open(f"{docs}/ZProp/a.txt", "w").write("x")
time.sleep(0.8)
ox, oy = origem()
sonda = (ox + 60, oy + 500)  # um canto da barra lateral, que o véu do diálogo escurece
claro = brilho(*sonda)

# 1. propriedades: Ctrl+I mostra o diálogo (o véu escurece a janela) e Esc fecha
seleciona("ZProp")
tecla("i", mod="ctrl")
confere(lambda: pos("pr-aplicar") is not None, "Ctrl+I abre as propriedades")
time.sleep(0.5)
if not brilho(*sonda) < claro:
    falha(f"o diálogo de propriedades não aparece na tela (brilho {brilho(*sonda)} contra {claro})")
for i in (1, 2):
    toca(f"pr-aba/{i}")
    time.sleep(0.3)
toca("pr-aba/0")
# renomear pelo campo Nome e Aplicar
toca("pr-nome")
tecla("a", mod="ctrl")
digita("ZPropNovo")
toca("pr-aplicar")
confere(lambda: os.path.isdir(f"{docs}/ZPropNovo") and not os.path.exists(f"{docs}/ZProp"), "Aplicar renomeia pelo campo Nome")
time.sleep(0.6)
if brilho(*sonda) < claro - 6:
    falha("o diálogo devia ter fechado ao Aplicar")

# 2. permissões: tirar o acesso de "Outros" e aplicar também aos itens
os.chmod(f"{docs}/ZPropNovo", 0o755)
os.chmod(f"{docs}/ZPropNovo/a.txt", 0o644)
seleciona("ZPropNovo")
tecla("i", mod="ctrl")
confere(lambda: pos("pr-aba/1") is not None, "propriedades da pasta renomeada")
toca("pr-aba/1")
toca("pr-nivel/2")  # a lista suspensa abre; o primeiro item é "Sem acesso" (logo abaixo)
time.sleep(0.4)
x, y = centro("pr-nivel/2")
msg("toque", "baixo", 1, x, y + 54)
time.sleep(0.08)
msg("toque", "cima", 1)
time.sleep(0.5)
toca("pr-recursivo")
toca("pr-aplicar")
confere(lambda: (os.stat(f"{docs}/ZPropNovo").st_mode & 0o777) == 0o750, "Aplicar muda as permissões da pasta (Outros sem acesso)", 6)
confere(lambda: (os.stat(f"{docs}/ZPropNovo/a.txt").st_mode & 0o777) == 0o640, "e as dos itens de dentro, com 'aplicar também aos itens'", 6)
tecla("Escape")
time.sleep(0.4)

# 3. Esc fecha o diálogo
seleciona("ZPropNovo")
tecla("i", mod="ctrl")
confere(lambda: pos("pr-aplicar") is not None and brilho(*sonda) < claro, "o diálogo abre de novo")
tecla("Escape")
confere(lambda: brilho(*sonda) >= claro - 6, "Esc fecha o diálogo")

# 4. submenus ao lado do menu do espaço vazio: a cor, e o pixel do submenu é de um popup (não do fundo da janela)
segura_xy(900, 560)
toca("mf-cor")
confere(lambda: pos("sub-cor-i/1") is not None, "o submenu das cores aparece ao lado")
p = pos("sub-cor-i/1")
antes_popup = brilho(ox + p[0] + 150, oy + p[1] + 22)
if antes_popup < 30:
    falha("o submenu das cores devia ter fundo de popup, e não ficar transparente")
tecla("Escape")
time.sleep(0.6)

# 5. a página Início: o clique entra na pasta e o botão direito abre o menu, com "Remover da página inicial"
toca("l-it/0")
confere(lambda: estado().get("vista") == "3", "o Início abre")
time.sleep(0.5)
toca("in-cartao/3")
confere(lambda: estado().get("vista") == "0", "clicar num cartão de pasta entra nela")
toca("l-it/0")
confere(lambda: estado().get("vista") == "3", "de volta ao Início")
time.sleep(0.5)
segura("in-cartao/3")
confere(lambda: pos("mn-i-remover") is not None and pos("mn-i-aba") is not None, "o menu do cartão abre (Abrir em nova aba, Remover da página inicial)")
toca("mn-i-remover")
time.sleep(0.6)
if "inicio=false" not in open(barra_ini).read():
    falha("Remover da página inicial não gravou no barra.ini: " + open(barra_ini).read()[:300])
vai_para(docs)

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (diálogos, submenus e Início): ok")
