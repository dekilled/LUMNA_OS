#!/usr/bin/env python3
"""Clicar numa música para tocar começa do 0:00. A fila salva no meio de uma faixa (0:43) não pode vazar para as faixas que se tocam depois."""
import os, subprocess, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *

bin_ = os.environ["MUSICA_BIN"]


def e(k):
    return estado().get(k)


def gd(*args):
    return subprocess.run(["gdbus", "call", "--session", "--dest", "org.mpris.MediaPlayer2.lumna_music", "--object-path", "/org/mpris/MediaPlayer2", "--method", *args],
                          capture_output=True, text=True).stdout


def pidm():
    r = subprocess.run(["pgrep", "-x", "musica"], capture_output=True, text=True).stdout.split()
    return r[0] if r else None


confere(lambda: e("vivo") == "1" and float(e("dur") or 0) > 100, "toca a faixa longa", 12)
# vai para 0:43 e fecha com a fila salva ali
gd("org.mpris.MediaPlayer2.Player.Seek", "43000000")
confere(lambda: 42 < float(e("pos") or 0) < 47, f"a faixa está em ~0:43 ({e('pos')})", 6)
time.sleep(12)   # a fila é salva de 10 em 10 s tocando: guarda ~0:55
confere(lambda: 50 < float(e("pos") or 0) < 62, f"seguiu tocando até ~0:55 ({e('pos')})")
gd("org.mpris.MediaPlayer2.Player.Pause")
time.sleep(0.5)
subprocess.run(["pkill", "-x", "-TERM", "musica"])
time.sleep(1.5)
open(f"{run}/g.out", "w").close()
novo = subprocess.Popen([bin_], env=dict(os.environ, LUMNA_MUSICA_DEBUG="1"), stdout=open(f"{run}/g.out", "a"), stderr=open(f"{run}/g2.err", "a"))
confere(lambda: e("nfaixas") == "9" and e("carregando") == "0", "reabriu", 30)
confere(lambda: e("nfila") != "0" and e("tocando") == "0", "a fila voltou parada")
# Tocar continua de onde parou (0:43)...
toca("mu-play")
confere(lambda: e("tocando") == "1" and 50 < float(e("pos") or 0) < 70, f"Tocar retoma perto de onde parou (~0:55) ({e('pos')})", 8)
# ...e as faixas que se tocam DEPOIS começam do zero (o início pedido não pode ficar valendo para todas)

for termo, titulo in (("aurora", "Aurora lenta"), ("entre", "Entre órbitas"), ("noturna", "Maré noturna")):
    toca("mu-busca")
    tecla("a", mod="ctrl")
    digita(termo)
    confere(lambda: e("vista") == "6" and int(e("nbusca")) >= 1, f"acha {termo}")
    toca("mu-lin/0")
    t0 = time.time()
    posicoes = []
    while time.time() - t0 < 1.2:
        try:
            posicoes.append(float(e("pos") or 0))
        except ValueError:
            pass
        time.sleep(0.1)
    confere(lambda: e("titulo") == titulo.replace(" ", "_"), f"tocou {titulo} ({e('titulo')})", 6)
    confere(lambda: posicoes and max(posicoes) < 3.0, f"{titulo} começa do 0:00, não de onde a outra parou (posições vistas: {posicoes[:4]}…{posicoes[-1:]})")
    tecla("a", mod="ctrl")
    tecla("BackSpace")
    time.sleep(3.5)   # deixa a faixa de teste (3 s) acabar

novo.terminate()
print("musica (posição ao tocar): ok" if not falhas else f"musica posição: {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
