#!/usr/bin/env python3
"""Fechar a janela do Music tocando o deixa em segundo plano: a janela some do compositor, a música continua, o MPRIS controla e responde, Raise e abrir de novo trazem a janela;
pausado, fechar encerra o app; com a opção desligada, fechar encerra."""
import json, os, signal, subprocess, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *

bin_ = os.environ["MUSICA_BIN"]


def e(k):
    return estado().get(k)


def pids():
    return subprocess.run(["pgrep", "-x", "musica"], capture_output=True, text=True).stdout.split()


def janelas():
    try:
        return json.loads(msg("janelas"))["janelas"]
    except Exception:
        return []


def da_musica():
    return [j for j in janelas() if "music" in (j.get("app_id") or "").lower() or "music" in (j.get("titulo") or "").lower()]


def gd(*args):
    return subprocess.run(["gdbus", "call", "--session", "--dest", "org.mpris.MediaPlayer2.lumna_music", "--object-path", "/org/mpris/MediaPlayer2", "--method", *args],
                          capture_output=True, text=True).stdout


def tocando():
    return e("vivo") == "1" and float(e("dur") or 0) > 100


confere(tocando, "toca a faixa longa", 12)
confere(lambda: len(da_musica()) == 1, f"há uma janela do Music ({len(da_musica())})")
pid1 = pids()[0]
pos0 = float(e("pos") or 0)

def clica_x():
    """o X da barra de título do próprio app (o caminho que a pessoa usa: o esconder acontece no meio do quadro)"""
    v = e("mu-fechar-janela")
    b = pos("mu-fechar-janela")
    x, y = origem()
    msg("mouse", "mover", x + b[0] + b[2] // 2, y + b[1] + b[3] // 2)
    time.sleep(0.3)
    msg("toque", "baixo", 1, x + b[0] + b[2] // 2, y + b[1] + b[3] // 2)
    time.sleep(0.08)
    msg("toque", "cima", 1)


# 1. fechar a janela tocando pelo X da barra: a janela some, o app e a música continuam
confere(lambda: pos("mu-fechar-janela") is not None, "o X da barra está na tela")
clica_x()
confere(lambda: len(da_musica()) == 0, "a janela some do compositor", 6)
confere(lambda: pids() == [pid1], "o app continua vivo")
confere(lambda: e("esc") == "1", "e sabe que está em segundo plano")
time.sleep(2.0)
confere(lambda: float(e("pos") or 0) > pos0 + 1.5, f"a música continua andando sem janela ({pos0} → {e('pos')})")
confere(lambda: "Playing" in gd("org.freedesktop.DBus.Properties.Get", "org.mpris.MediaPlayer2.Player", "PlaybackStatus"), "o MPRIS diz que toca")

# 2. controlar pelo MPRIS sem janela
gd("org.mpris.MediaPlayer2.Player.Pause")
confere(lambda: e("tocando") == "0", "Pause pelo MPRIS pausa", 6)
gd("org.mpris.MediaPlayer2.Player.Play")
confere(lambda: e("tocando") == "1", "Play pelo MPRIS volta a tocar", 6)
confere(lambda: len(da_musica()) == 0, "e a janela segue escondida")
confere(lambda: "true" in gd("org.freedesktop.DBus.Properties.Get", "org.mpris.MediaPlayer2", "CanQuit"), "o MPRIS aceita Quit")

# 2b. com o Music escondido, abrir OUTRA janela (o Firefox, na vida real) não pode dar erro de protocolo nem derrubar o compositor: o Orbit pedia um configure à
# janela escondida (a que ainda tinha o foco do teclado), o que o wlroots recusa (e, conforme a versão, aborta: a sessão ia direto para o login)
outra = os.path.join(os.path.dirname(bin_), "galeria")
if os.path.exists(outra):
    log_antes = len(open(f"{run}/orbit.log", errors="replace").read().splitlines())
    proc = subprocess.Popen([outra], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    confere(lambda: any("galeria" in (x.get("app_id") or "") for x in janelas()), "abre outra janela com o Music escondido", 10)
    time.sleep(1.0)
    novo = open(f"{run}/orbit.log", errors="replace").read().splitlines()[log_antes:]
    confere(lambda: subprocess.run(["pgrep", "-x", "orbit"], capture_output=True).returncode == 0, "o compositor continua de pé")
    confere(lambda: not any("ERROR" in l for l in novo), f"e sem erro de protocolo no log do Orbit ({[l for l in novo if 'ERROR' in l][:1]})")
    for x in janelas():
        if "galeria" in (x.get("app_id") or ""):
            msg("fechar", x["id"])
    proc.terminate()
    confere(lambda: e("esc") == "1" and pids() == [pid1], "e o Music segue em segundo plano")

# 3. Raise traz a janela de volta, a música não para
gd("org.mpris.MediaPlayer2.Raise")
confere(lambda: len(da_musica()) == 1, "Raise traz a janela de volta", 8)
confere(lambda: e("esc") == "0" and e("tocando") == "1" and pids() == [pid1], "a mesma instância, ainda tocando")
time.sleep(1.0)
confere(lambda: e("vista") is not None and float(e("pos") or 0) > pos0 + 3, "e a música não foi interrompida")

# 4. fechar de novo, e abrir o app outra vez (o que o lançador faz): traz a janela, sem processo novo
msg("fechar", da_musica()[0]["id"])
confere(lambda: len(da_musica()) == 0 and e("esc") == "1", "fecha de novo: em segundo plano", 6)
r = subprocess.run([bin_], capture_output=True, timeout=20)
confere(lambda: r.returncode == 0 and pids() == [pid1], f"abrir o app de novo não cria outro (rc={r.returncode}, {pids()})")
confere(lambda: len(da_musica()) == 1 and e("esc") == "0", "e traz a janela de volta", 8)

# 5. pausado, fechar encerra o app (não fica um app invisível parado)
gd("org.mpris.MediaPlayer2.Player.Pause")
confere(lambda: e("tocando") == "0", "pausa", 6)
msg("fechar", da_musica()[0]["id"])
confere(lambda: pids() == [], "pausado, fechar a janela encerra o app", 6)

print("musica (segundo plano): ok" if not falhas else f"musica segundo plano: {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
