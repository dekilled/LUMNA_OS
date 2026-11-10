#!/usr/bin/env python3
"""O LUMNA Music como app padrão: aberto com um arquivo (linha de comando, como o gerenciador de arquivos faz) toca na hora; abrir outro arquivo com o app aberto
o manda para a mesma janela (instância única); abrir várias vezes a mesma música não cria app nem mpv a mais; um mpv não sobrevive ao app."""
import os, signal, subprocess, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *

musicas = f"{casa}/Músicas"
a1 = f"{musicas}/Horizonte violeta/01 Entre órbitas.mp3"
a2 = f"{musicas}/Horizonte violeta/02 Maré noturna.mp3"
a3 = f"{musicas}/Luz distante/01 Aurora lenta.flac"
bin_ = os.environ["MUSICA_BIN"]


def e(k):
    return estado().get(k)


def pids():
    return subprocess.run(["pgrep", "-x", "musica"], capture_output=True, text=True).stdout.split()


def mpvs(pid):
    r = subprocess.run(["ps", "-eo", "args"], capture_output=True, text=True).stdout
    return sum(1 for l in r.splitlines() if l.startswith("mpv ") and f"lumna-musica-{pid}-" in l)


def abre(*args):
    t0 = time.time()
    r = subprocess.run([bin_, *args], capture_output=True, timeout=20)
    return r.returncode, time.time() - t0


# 1. aberto com um arquivo, ele toca (e a fila salva de antes não volta por cima)
def tocou():
    """o motor carregou a faixa (as de teste têm 3 s: pode já ter acabado)"""
    return e("vivo") == "1" and float(e("dur") or 0) > 1


confere(tocou, "aberto com um arquivo, o app já toca", 10)
confere(lambda: e("nfila") == "1" and "Entre_órbitas" in (e("fila") or ""), f"a fila é só o que foi aberto ({e('nfila')}: {e('fila')})", 6)
p = pids()
confere(lambda: len(pids()) == 1, "um app só")
pid1 = pids()[0] if pids() else "0"
confere(lambda: mpvs(pid1) == 1, f"e um mpv só ({mpvs(pid1)})")

# 2. abrir outro arquivo com o app aberto: vai para a mesma janela e toca; este processo sai logo
rc, dt = abre(a2)
confere(lambda: rc == 0 and dt < 8, f"o segundo processo sai logo e sem erro (rc={rc}, {dt:.1f}s)")
confere(lambda: tocou() and e("nfila") == "1" and "Maré_noturna" in (e("fila") or ""), f"o app aberto passa a tocar o novo arquivo ({e('nfila')}: {e('fila')})", 8)
confere(lambda: pids() == [pid1], f"continua um app só, o mesmo ({pids()})")

# 3. a mesma música aberta várias vezes (como clicar de novo e de novo): um app e um mpv
for _ in range(6):
    abre(a2)
time.sleep(1.0)
confere(lambda: pids() == [pid1] and mpvs(pid1) == 1, f"seis aberturas da mesma música: um app e um mpv ({pids()}, {mpvs(pid1)} mpv)")
confere(lambda: tocou() and e("nfila") == "1", "e ela toca")

# 4. vários arquivos de uma vez (uma seleção): entram juntos na fila e o primeiro toca
rc, dt = abre(a1, a3)
confere(lambda: e("nfila") == "2" and e("atual") == "0", f"dois arquivos juntos: a fila tem os dois, o primeiro toca ({e('nfila')}, atual {e('atual')})", 8)
confere(lambda: mpvs(pid1) == 1, "ainda um mpv")

# 5. abrir o app sem arquivo (pelo menu) só traz a janela que já existe
rc, dt = abre()
confere(lambda: rc == 0 and dt < 8 and pids() == [pid1], f"abrir de novo sem arquivo não cria outro app (rc={rc}, {pids()})")

# 6. o app cai à força: o mpv vai junto
os.kill(int(pid1), signal.SIGKILL)
time.sleep(1.5)
confere(lambda: mpvs(pid1) == 0, f"o app foi morto à força e não sobrou mpv invisível ({mpvs(pid1)})")

print("musica (abrir arquivo, instância única): ok" if not falhas else f"musica abrir: {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
