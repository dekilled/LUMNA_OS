#!/usr/bin/env python3
"""
O encaixe de janelas (float.c) num Orbit headless com 2 monitores de 1280x720 lado a lado, e o menu_gtk como janela (a barra é a do GTK).
  python3 teste_encaixe.py PASTA_DO_ORBIT PASTA_COM_menu_gtk
Confere: a posição do CURSOR na lateral escolhe a metade ou o canto; a borda da janela encostando não encaixa; arrastar a divisa entre janelas
encaixadas redimensiona as duas; as divisas voltam ao meio sem janela encaixada.
"""
import json, os, subprocess, sys, tempfile, time

orbit_dir, gtk_dir = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
run = tempfile.mkdtemp()
os.chmod(run, 0o700)
env = dict(os.environ, XDG_RUNTIME_DIR=run, HOME=run, WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1", WLR_BACKENDS="headless",
           WLR_HEADLESS_OUTPUTS="2", GDK_BACKEND="wayland", XDG_CONFIG_HOME=run + "/cfg")
os.makedirs(run + "/cfg")
env.pop("WAYLAND_DISPLAY", None)
env.pop("DISPLAY", None)
orbit = subprocess.Popen([orbit_dir + "/orbit"], cwd=orbit_dir, env=env, stdout=open(run + "/orbit.log", "w"), stderr=subprocess.STDOUT)
time.sleep(2.5)
env["WAYLAND_DISPLAY"] = "wayland-0"
sock = [f for f in os.listdir(run) if f.startswith("orbit.") and f.endswith(".sock")][0]
env["ORBIT_SOCK"] = f"{run}/{sock}"
falhas = 0
filhos = []


def msg(*a):
    r = subprocess.run([orbit_dir + "/orbit-msg", *a], env=env, capture_output=True, text=True)
    try:
        return json.loads(r.stdout)
    except Exception:
        return {}


def janelas():
    return msg("janelas").get("janelas", [])


def abre():
    n = len(janelas())
    filhos.append(subprocess.Popen([gtk_dir + "/menu_gtk", run + "/menu.txt"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    for _ in range(40):
        time.sleep(0.2)
        if len(janelas()) > n:
            break
    time.sleep(0.5)
    return janelas()[-1]["id"]


def jan(i):
    return [j for j in janelas() if j["id"] == i][0]


def mover(x, y):
    msg("mouse", "mover", str(int(x)), str(int(y)))


def arrasta(de, para, parado=0.3, passos=12):
    """aperta em `de`, anda até `para` e solta"""
    mover(*de)
    time.sleep(0.2)
    msg("mouse", "botao", "baixo")
    time.sleep(0.15)
    for k in range(1, passos + 1):
        mover(de[0] + (para[0] - de[0]) * k / passos, de[1] + (para[1] - de[1]) * k / passos)
        time.sleep(0.04)
    time.sleep(parado)
    msg("mouse", "botao", "cima")
    time.sleep(0.6)


def barra(j):
    """um ponto na barra de título (do GTK) da janela"""
    return (j["x"] + j["w"] // 2 - 60, j["y"] + 14)


def confere(cond, nome):
    global falhas
    if cond:
        print("ok     ", nome)
    else:
        falhas += 1
        print("FALHOU ", nome)


def perto(a, b, tol=3):
    return abs(a - b) <= tol


def caixa(j):
    return (j["x"], j["y"], j["w"], j["h"])


try:
    GAP = 8
    W, H = 1280, 720
    meia = (W - 3 * GAP) // 2          # 628
    quarto = (H - 3 * GAP) // 2        # 348
    a = abre()
    j0 = jan(a)
    print("janela livre:", caixa(j0))
    confere(j0["flutuante"] and not j0["maximizada"], "a janela abre livre")

    # 1. cursor no meio da lateral esquerda: a metade da esquerda
    arrasta(barra(j0), (3, H // 2))
    x, y, w, h = caixa(jan(a))
    print("meio da lateral esquerda:", (x, y, w, h))
    confere(perto(x, GAP) and perto(y, GAP) and perto(w, meia) and perto(h, H - 2 * GAP), "cursor no meio da lateral: metade da esquerda, altura inteira")

    # 2. perto do topo da lateral: o canto de cima, um quarto da tela
    arrasta(barra(jan(a)), (3, 40))
    x, y, w, h = caixa(jan(a))
    print("topo da lateral esquerda:", (x, y, w, h))
    confere(perto(x, GAP) and perto(y, GAP) and perto(w, meia) and perto(h, quarto), "cursor perto do topo da lateral: canto de cima (50% x 50%)")

    # 3. perto do fim da lateral: o canto de baixo
    arrasta(barra(jan(a)), (3, H - 40))
    x, y, w, h = caixa(jan(a))
    print("fim da lateral esquerda:", (x, y, w, h))
    confere(perto(x, GAP) and perto(y, GAP + quarto + GAP) and perto(w, meia) and perto(h, quarto) and perto(y + h, H - GAP),
            "cursor perto do fim da lateral: canto de baixo, que termina a um gap da borda da tela")

    # 4. lateral direita: a metade da direita
    arrasta(barra(jan(a)), (W - 4, H // 2))
    x, y, w, h = caixa(jan(a))
    print("meio da lateral direita:", (x, y, w, h))
    confere(perto(x, 2 * GAP + meia) and perto(w, meia) and perto(h, H - 2 * GAP), "lateral direita, no meio: metade da direita")

    # 5. a borda da janela encostando na da tela, com o cursor longe dela, NÃO encaixa
    arrasta(barra(jan(a)), (400, 300))   # solta do encaixe, vai para o meio
    j = jan(a)
    confere(not j["maximizada"] and j["w"] > meia + 20, "arrastada para o meio, a janela volta ao tamanho livre")
    gx = j["x"] + 40                      # agarra a 40 px da borda esquerda da janela
    arrasta((gx, j["y"] + 14), (40, 400))  # a borda da janela chega a x=0, mas o cursor fica em x=40 (fora da zona de 24 px)
    j = jan(a)
    print("borda encostando, cursor em x=40:", caixa(j))
    confere(j["w"] > meia + 20 and not j["maximizada"] and j["x"] <= 20, "a borda da janela encostando na da tela (cursor longe) não encaixa")

    # 6. meio da tela (nada) e cima (maximiza)
    arrasta(barra(jan(a)), (640, 3))
    j = jan(a)
    confere(j["maximizada"], "soltar no topo, fora dos cantos, maximiza")
    arrasta(barra(jan(a)), (640, 300))
    confere(not jan(a)["maximizada"], "arrastar uma maximizada a solta")

    # 7. duas janelas encaixadas lado a lado e a divisa entre elas
    arrasta(barra(jan(a)), (3, H // 2))
    b = abre()   # abre no meio, por cima do espaço livre
    arrasta(barra(jan(b)), (W - 4, H // 2))
    ja, jb = jan(a), jan(b)
    print("duas metades:", caixa(ja), caixa(jb))
    confere(perto(ja["x"] + ja["w"] + GAP, jb["x"]), "duas janelas encaixadas, uma em cada metade")
    # a divisa fica entre as duas: arrasta pela borda de dentro da janela da esquerda (6 px dentro)
    divisa_x = ja["x"] + ja["w"] - 3
    arrasta((divisa_x, H // 2), (800, H // 2), passos=15)
    ja, jb = jan(a), jan(b)
    print("depois de arrastar a divisa até x=800:", caixa(ja), caixa(jb))
    confere(perto(ja["x"] + ja["w"], 800 - GAP // 2, 6), "a janela da esquerda cresceu até a divisa (cursor)")
    confere(perto(ja["x"] + ja["w"] + GAP, jb["x"]) and perto(jb["x"] + jb["w"], W - GAP), "e a da direita encolheu junto, com o mesmo espaço entre as duas")
    confere(ja["w"] > meia + 100 and jb["w"] < meia - 100, "as duas mudaram de tamanho")
    # o mínimo: arrastar até o outro lado não esmaga a vizinha
    ja = jan(a)
    arrasta((ja["x"] + ja["w"] - 3, H // 2), (W - 10, H // 2), passos=15)
    jb = jan(b)
    confere(jb["w"] >= 240, "a vizinha não encolhe abaixo do tamanho mínimo")

    # 8. uma janela em cada canto da coluna da direita e a divisa horizontal
    arrasta(barra(jb), (W - 4, 40))
    c = abre()
    arrasta(barra(jan(c)), (W - 4, H - 40))
    jb, jc = jan(b), jan(c)
    print("coluna da direita:", caixa(jb), caixa(jc))
    confere(perto(jb["x"], jc["x"]) and perto(jb["h"], quarto) and perto(jc["h"], quarto), "dois cantos na mesma coluna: 50% de altura cada um")
    divisa_y = jb["y"] + jb["h"] - 3
    arrasta((jb["x"] + jb["w"] // 2, divisa_y), (jb["x"] + jb["w"] // 2, 500), passos=12)
    jb, jc = jan(b), jan(c)
    print("depois de arrastar a divisa horizontal até y=500:", caixa(jb), caixa(jc))
    confere(perto(jb["y"] + jb["h"], 500 - GAP // 2, 6) and perto(jb["y"] + jb["h"] + GAP, jc["y"]) and perto(jc["y"] + jc["h"], H - GAP),
            "arrastar a divisa horizontal redimensiona o canto de cima e o de baixo juntos")
    confere(perto(jan(a)["x"], GAP) and perto(jan(a)["h"], H - 2 * GAP), "a janela da outra coluna não muda")

    # 9. soltar todas: as divisas voltam ao meio
    for i in (a, b, c):
        msg("foco", str(i))
        msg("acao", "restaurar")
        time.sleep(0.3)
    d = abre()
    arrasta(barra(jan(d)), (3, H // 2))
    x, y, w, h = caixa(jan(d))
    confere(perto(w, meia), "sem janelas encaixadas, a divisa volta ao meio")
finally:
    for f in filhos:
        f.kill()
    orbit.terminate()
    time.sleep(0.5)
    orbit.kill()
print("FALHAS:", falhas)
sys.exit(1 if falhas else 0)
