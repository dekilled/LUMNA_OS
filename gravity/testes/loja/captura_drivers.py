#!/usr/bin/env python3
"""Capturas da página Drivers e kernels. TESTE=captura_drivers.py (com o ambiente do teste_drivers.sh)"""
import os, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *
dest = os.environ.get("CAPTURAS", "/tmp/loja-capturas")
os.makedirs(dest, exist_ok=True)
def foto(n):
    time.sleep(0.8)
    subprocess.run(["grim", f"{dest}/{n}.png"], env=dict(env, WAYLAND_DISPLAY="wayland-0", XDG_RUNTIME_DIR=run))
confere(lambda: estado().get("carregando") == "0", "carrega", 10)
toca("lj-nav/4"); foto("d1-drivers")
toca("lj-d-aba/1"); foto("d2-kernels")
toca("lj-d-ver/1"); foto("d3-detalhe")
toca("lj-dd-fechar")
toca("lj-d-aba/0"); toca("lj-d-hw"); foto("d4-hardware")
