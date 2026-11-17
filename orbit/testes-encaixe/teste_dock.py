#!/usr/bin/env python3
"""
A dock se esconde quando há janela encaixada (como na maximizada): a área útil da tela volta a ser a tela inteira e a janela encaixada vai até a
borda de baixo, sem a faixa vazia da dock.
  python3 teste_dock.py PASTA_DO_ORBIT PASTA_COM_menu_gtk CAMINHO_DO_dock
"""
import json, os, subprocess, sys, tempfile, time

orbit_dir, gtk_dir, dock = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])
run = tempfile.mkdtemp()
os.chmod(run, 0o700)
env = dict(os.environ, XDG_RUNTIME_DIR=run, HOME=run, WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1", WLR_BACKENDS="headless",
           WLR_HEADLESS_OUTPUTS="1", GDK_BACKEND="wayland", XDG_CONFIG_HOME=run + "/cfg")
os.makedirs(run + "/cfg/lumna")
open(run + "/cfg/lumna/dock.ini", "w").write("ao_maximizar = ocultar\n")
env.pop("WAYLAND_DISPLAY", None)
env.pop("DISPLAY", None)
orbit = subprocess.Popen([orbit_dir + "/orbit"], cwd=orbit_dir, env=env, stdout=open(run + "/orbit.log", "w"), stderr=subprocess.STDOUT)
time.sleep(2.5)
env["WAYLAND_DISPLAY"] = "wayland-0"
env["ORBIT_SOCK"] = run + "/" + [f for f in os.listdir(run) if f.startswith("orbit.") and f.endswith(".sock")][0]
falhas = 0
filhos = []


def msg(*a):
    r = subprocess.run([orbit_dir + "/orbit-msg", *a], env=env, capture_output=True, text=True)
    try:
        return json.loads(r.stdout)
    except Exception:
        return {}


def util():
    return msg("saidas")["saidas"][0]["util"]


def confere(cond, nome):
    global falhas
    if cond:
        print("ok     ", nome)
    else:
        falhas += 1
        print("FALHOU ", nome)


def espera(f, t=6):
    fim = time.time() + t
    while time.time() < fim:
        if f():
            return True
        time.sleep(0.2)
    return f()


try:
    filhos.append(subprocess.Popen([dock], env=env, stdout=subprocess.DEVNULL, stderr=open(run + "/dock.err", "w")))
    confere(espera(lambda: util()["altura"] < 720), "com a dock à vista, a área útil perde a altura dela")
    com_dock = util()["altura"]
    print("área útil com a dock:", util())
    filhos.append(subprocess.Popen([gtk_dir + "/menu_gtk", run + "/m.txt"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    time.sleep(2)
    confere(util()["altura"] == com_dock, "uma janela livre não esconde a dock")
    msg("acao", "encaixar_esquerda")
    confere(espera(lambda: util()["altura"] == 720), "encaixada, a dock se esconde: a área útil é a tela inteira")
    time.sleep(1)
    j = msg("janelas")["janelas"][0]
    print("janela encaixada:", (j["x"], j["y"], j["w"], j["h"]))
    confere(j["y"] + j["h"] == 720 - 8, "a janela encaixada termina a um gap da borda de baixo da tela (sem a faixa da dock)")
    msg("acao", "restaurar")
    confere(espera(lambda: util()["altura"] == com_dock), "ao soltar o encaixe a dock volta")
finally:
    for f in filhos:
        f.kill()
    orbit.terminate()
    time.sleep(0.5)
    orbit.kill()
print("FALHAS:", falhas)
sys.exit(1 if falhas else 0)
