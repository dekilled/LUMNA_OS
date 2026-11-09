#!/usr/bin/env python3
"""O menu de contexto abre fora dos limites da janela (o compositor o põe na tela): com a janela baixa, ele inteiro continua aparecendo, sem rolagem.
python3 teste_menu.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])


def pixel(x, y):
    out = subprocess.run(["grim", "-g", f"{x},{y} 1x1", "-t", "ppm", "-"], env=env, capture_output=True).stdout
    return tuple(out[-3:])


def janela():
    j = json.loads(subprocess.run([f"{orbit_dir}/orbit-msg", "janelas"], env=env, capture_output=True, text=True).stdout)["janelas"][0]
    return j["x"], j["y"], j["w"], j["h"]


vai_para(docs)
open(f"{docs}/menu1.txt", "w").write("um")
time.sleep(1)

# baixa a janela: arrasta a borda de baixo (6 px por dentro) até a metade da tela
x0, y0, w0, h0 = janela()
h0_orig = h0
msg("mouse", "mover", x0 + w0 // 2, y0 + h0 - 3)
time.sleep(0.3)
msg("mouse", "botao", "baixo")
for y in range(y0 + h0 - 60, y0 + 330, -60):
    msg("mouse", "mover", x0 + w0 // 2, y)
    time.sleep(0.1)
msg("mouse", "botao", "cima")
time.sleep(1.2)
x0, y0, w0, h0 = janela()
if h0 > 450:
    falha(f"a janela devia ter ficado baixa (tem {h0} px)")

# o menu de um arquivo (dedo parado): deve passar da borda de baixo da janela e continuar inteiro
time.sleep(0.4)
ox, oy = x0, y0
pa = pos("item/Arquivados")  # a primeira pasta: a que a janela baixa ainda mostra
if not pa:
    falha("não achei item/Arquivados na tela")
    sys.exit(1)
cx, cy = ox + pa[0] + pa[2] // 2, oy + pa[1] + 40
msg("toque", "baixo", 0, cx, cy)
time.sleep(1.0)
msg("toque", "cima", 0)
time.sleep(1.0)
abaixo = 690  # perto do fim da tela: só o menu que o compositor encaixou na TELA chega até aqui (preso à janela, acabaria uns 140 px antes)
fundo_area = pixel(1250, 710)  # a área vazia da tela
dentro_menu = pixel(cx + 60, abaixo)
if dentro_menu == fundo_area:
    subprocess.run(["grim", f"{run}/falha.png"], env=env)
    falha(f"o menu devia passar da borda da janela (pixel {dentro_menu} igual ao da tela vazia {fundo_area})")
else:
    print("ok: o menu passa dos limites da janela")

# devolve a janela ao tamanho de antes (os testes seguintes contam com ela)
_, _, _, h1 = janela()
msg("toque", "baixo", 0, 5, 5)  # (fecha o menu, se ainda aberto: um toque fora)
msg("toque", "cima", 0)
time.sleep(0.5)
x1, y1, w1, h1 = janela()
msg("mouse", "mover", x1 + w1 // 2, y1 + h1 - 3)
time.sleep(0.3)
msg("mouse", "botao", "baixo")
for y in range(y1 + h1, y0 + h0_orig, 60):
    msg("mouse", "mover", x1 + w1 // 2, y)
    time.sleep(0.1)
msg("mouse", "mover", x1 + w1 // 2, y0 + h0_orig - 3)
time.sleep(0.2)
msg("mouse", "botao", "cima")
time.sleep(1.2)

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (menu fora da janela): ok")
