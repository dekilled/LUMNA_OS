#!/usr/bin/env python3
"""
O menu de contexto de uma janela X11 (XWayland: o Firefox, os apps antigos) nasce junto do cursor, também depois de a janela ser arrastada para o
segundo monitor ou encaixada. O app X11 tira as coordenadas do menu da posição que o X acha que a janela tem (o Orbit precisa contá-la ao X a cada
movimento); com a posição velha, o menu caía longe da janela, no outro monitor. Usa o menu_gtk em GDK_BACKEND=x11 (o menu GTK é uma janela X
override-redirect) e lê no log do Orbit onde o menu mapeou.
  python3 teste_menu_x11.py PASTA_DO_ORBIT PASTA_COM_menu_gtk
"""
import json, os, re, subprocess, sys, tempfile, time

orbit_dir, gtk_dir = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
run = tempfile.mkdtemp()
os.chmod(run, 0o700)
env = dict(os.environ, XDG_RUNTIME_DIR=run, HOME=run, WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1", WLR_BACKENDS="headless",
           WLR_HEADLESS_OUTPUTS="2", XDG_CONFIG_HOME=run + "/cfg")
os.makedirs(run + "/cfg")
env.pop("WAYLAND_DISPLAY", None)
env.pop("DISPLAY", None)
orbit = subprocess.Popen([orbit_dir + "/orbit"], cwd=orbit_dir, env=env, stdout=open(run + "/orbit.log", "w"), stderr=subprocess.STDOUT)
time.sleep(3)
env["WAYLAND_DISPLAY"] = "wayland-0"
env["ORBIT_SOCK"] = run + "/" + [f for f in os.listdir(run) if f.startswith("orbit.") and f.endswith(".sock")][0]
display = re.findall(r"DISPLAY=(:\d+)", open(run + "/orbit.log").read())[-1]
falhas = 0


def msg(*a):
    r = subprocess.run([orbit_dir + "/orbit-msg", *a], env=env, capture_output=True, text=True)
    try:
        return json.loads(r.stdout)
    except Exception:
        return {}


def confere(cond, nome):
    global falhas
    if cond:
        print("ok     ", nome)
    else:
        falhas += 1
        print("FALHOU ", nome)


def mover(x, y):
    msg("mouse", "mover", str(int(x)), str(int(y)))


def arrasta(de, para):
    mover(*de)
    time.sleep(0.2)
    msg("mouse", "botao", "baixo")
    time.sleep(0.2)
    for k in range(1, 21):
        mover(de[0] + (para[0] - de[0]) * k / 20, de[1] + (para[1] - de[1]) * k / 20)
        time.sleep(0.08)
    time.sleep(0.3)
    msg("mouse", "botao", "cima")
    time.sleep(0.8)


def clica_direito(x, y):
    mover(x, y)
    time.sleep(0.3)
    msg("mouse", "botao", "baixo", "direito")
    time.sleep(0.1)
    msg("mouse", "botao", "cima", "direito")
    time.sleep(1.2)


def teste(nome, preparar):
    global falhas
    n_antes = len(re.findall(r"mapeou sem moldura", open(run + "/orbit.log").read()))
    j = msg("janelas")["janelas"][-1]
    preparar(j)
    j = msg("janelas")["janelas"][-1]
    cx, cy = j["x"] + j["w"] // 2, j["y"] + j["h"] // 2
    clica_direito(cx, cy)
    linhas = re.findall(r"mapeou sem moldura[^\n]*", open(run + "/orbit.log").read())
    novas = linhas[n_antes:]
    print(f"{nome}: janela em ({j['x']},{j['y']}) {j['w']}x{j['h']}, cursor em ({cx},{cy}); menus mapeados: {novas[-1:] }")
    ok = False
    for l in novas:
        m = re.search(r"pos=(-?\d+),(-?\d+)", l)
        if m and abs(int(m.group(1)) - cx) < 200 and abs(int(m.group(2)) - cy) < 200:
            ok = True
    confere(ok and novas, nome + ": o menu nasce junto do cursor (a menos de 200 px)")
    msg("mouse", "botao", "baixo")
    msg("mouse", "botao", "cima")
    time.sleep(0.4)


try:
    genv = dict(env, DISPLAY=display, GDK_BACKEND="x11")
    filho = subprocess.Popen([gtk_dir + "/menu_gtk", run + "/m.txt"], env=genv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        time.sleep(0.25)
        if msg("janelas").get("janelas"):
            break
    time.sleep(1)
    print("janela:", msg("janelas")["janelas"][-1])

    teste("janela livre, como abriu", lambda j: None)
    # arrastada para o segundo monitor (o de x >= 1280)
    teste("depois de arrastar para o segundo monitor", lambda j: arrasta((j["x"] + 100, j["y"] - 30), (j["x"] + 100 + 1300, j["y"] + 70)))
    teste("e encaixada na metade da direita do segundo monitor", lambda j: msg("acao", "encaixar_direita") and time.sleep(1))
    teste("e solta do encaixe", lambda j: (msg("acao", "restaurar"), time.sleep(1)))
    filho.kill()
finally:
    orbit.terminate()
    time.sleep(0.5)
    orbit.kill()
print("FALHAS:", falhas)
sys.exit(1 if falhas else 0)
