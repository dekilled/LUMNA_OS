#!/usr/bin/env python3
"""Tira capturas das telas da Loja (grim) em $CAPTURAS (padrão /tmp/loja-capturas), para conferir com o esboço. TESTE=captura.py sh teste_ui.sh ..."""
import os, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *

dest = os.environ.get("CAPTURAS", "/tmp/loja-capturas")
os.makedirs(dest, exist_ok=True)


def foto(nome):
    time.sleep(0.8)
    subprocess.run(["grim", f"{dest}/{nome}.png"], env=dict(env, WAYLAND_DISPLAY="wayland-0", XDG_RUNTIME_DIR=run))


confere(lambda: estado().get("carregando") == "0", "carrega", 10)
foto("1-explorar")
toca("lj-busca")
digita("mesa")
time.sleep(1.5)
foto("2-pacotes")
toca("lj-nav/3"); foto("3-atualizacoes")
toca("lj-nav/2"); foto("4-instalados")
toca("lj-nav/4"); foto("5-drivers")
toca("lj-nav/5"); foto("6-config")
toca("lj-nav/0")
toca("lj-cartao/1"); foto("7-detalhe")
sys.exit(0)
