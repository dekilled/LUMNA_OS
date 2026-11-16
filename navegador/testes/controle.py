#!/usr/bin/env python3
"""O controle (gamepad) e o Super+M na Mídia, de ponta a ponta: o serviço lumna-gamepad REAL com um controle de mentira (FIFO), o Orbit sem tela, o
navegador e uma página "sonda" que anota no título tudo o que recebe (teclas, mouse, cliques, rolagem). Sem mocks do navegador.

  python3 navegador/testes/controle.py <pasta do orbit> <pasta stage do navegador> <pasta do gravity (com o binário "gamepad" compilado)>

O que confere:
  - a janela tem app_id "lumna-navegador" (é como o Orbit a reconhece);
  - `lumna-navegador --estado|--menu|--inicio` (o Super+M do Orbit) falam com a instância que está rodando;
  - no início e no menu: o direcional e o analógico navegam, A confirma, B volta, Start abre/fecha o menu, Select vai ao início;
  - no site: o direcional manda setas, X manda Espaço, Y manda "f", A clica, o analógico esquerdo move o ponteiro de verdade, o direito rola;
  - quem não está em foco não recebe o controle (aqui: o serviço sozinho não manda tecla nenhuma, o navegador é quem age).
"""
import atexit, json, os, re, shutil, socket, struct, subprocess, sys, tempfile, time

orbit_dir, stage, gravity = [os.path.abspath(a) for a in sys.argv[1:4]]
aqui = os.path.dirname(os.path.abspath(__file__))
falhas = []
procs = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def espera(cond, seg=8.0):
    fim = time.time() + seg
    while time.time() < fim:
        try:
            if cond():
                return True
        except Exception:
            pass
        time.sleep(0.1)
    try:
        return bool(cond())
    except Exception:
        return False


run = tempfile.mkdtemp()
os.chmod(run, 0o700)
for d in ("cfg", "home", "dados", "falsos"):
    os.makedirs(f"{run}/{d}")
env = dict(os.environ, HOME=f"{run}/home", XDG_CONFIG_HOME=f"{run}/cfg", XDG_RUNTIME_DIR=run)
env.pop("WAYLAND_DISPLAY", None)


def sai():
    for p in procs:
        try:
            p.kill()
        except Exception:
            pass


atexit.register(sai)

# um servidor para as páginas e a lista de serviços: o Alfa (a.html) e a sonda
sock_http = socket.socket()
sock_http.bind(("127.0.0.1", 0))
porta = sock_http.getsockname()[1]
sock_http.close()
procs.append(subprocess.Popen([sys.executable, f"{aqui}/servidor.py", str(porta), f"{aqui}/paginas"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
json.dump([
    {"id": "alfa", "nome": "Serviço Alfa", "url": f"http://127.0.0.1:{porta}/a.html"},
    {"id": "sonda", "nome": "Sonda", "url": f"http://127.0.0.1:{porta}/sonda.html", "modo": "cursor"},   # no modo "cursor" o analógico é o ponteiro (as seções 4-6)
    {"id": "nav", "nome": "Setas", "url": f"http://127.0.0.1:{porta}/nav.html"},                          # o padrão: modo "setas" (a seção 7c)
    {"id": "tv", "nome": "TV", "url": f"http://127.0.0.1:{porta}/eco-ua", "ua_tv": True, "voltar": "esc"},  # interface de TV: User-Agent de TV, B = Escape
    {"id": "eco", "nome": "Eco", "url": f"http://127.0.0.1:{porta}/eco-ua"},                               # igual, sem o User-Agent de TV
    {"id": "login", "nome": "Login", "url": f"http://127.0.0.1:{porta}/login.html"},                      # abre popups (window.open), como o "Entrar com o Google"
], open(f"{run}/servicos.json", "w"))

# o Orbit sem tela
envo = dict(env, LUMNA_TESTE_TECLADOS="1", WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1")
orb = subprocess.Popen(["./orbit"], cwd=orbit_dir, env=envo, stdout=open(f"{run}/orbit.log", "w"), stderr=subprocess.STDOUT)
procs.append(orb)
time.sleep(2)
m = re.search(r"DISPLAY=(:\d+)", open(f"{run}/orbit.log").read())
if not m:
    print("controle: dispensado (esta wlroots não tem XWayland)")
    sys.exit(0)
display = m.group(1)
orbit_sock = [f for f in os.listdir(run) if re.match(r"orbit\..*\.sock", f)][0]


def orbit(*a):
    return subprocess.run([f"{orbit_dir}/orbit-msg", "-s", f"{run}/{orbit_sock}", *a], capture_output=True, text=True).stdout


def janela(app_id="lumna-navegador"):
    for j in json.loads(orbit("janelas") or "{}").get("janelas", []):
        if j.get("app_id") == app_id:
            return j
    return None


# o controle de mentira e o serviço lumna-gamepad (o de verdade)
xbox = f"{run}/falsos/xbox.ev"
os.mkfifo(xbox)
open(f"{run}/falsos/xbox.dev", "w").write("[controle]\nnome=Xbox\nuniq=AA:BB:CC:DD:EE:01\nbus=usb\nrumble=nao\n")
envg = dict(env, LUMNA_GAMEPAD_FALSOS=f"{run}/falsos", LUMNA_GAMEPAD_FOCO="outro-app", LUMNA_POWER_DIR=f"{run}/falsos")
os.makedirs(f"{run}/bin")
for n in ("wtype", "pgrep", "pkill"):
    open(f"{run}/bin/{n}", "w").write("#!/bin/sh\nexit 0\n")
    os.chmod(f"{run}/bin/{n}", 0o755)
envg["PATH"] = f"{run}/bin:" + os.environ["PATH"]
procs.append(subprocess.Popen([f"{gravity}/gamepad"], env=envg, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))

# o navegador, aberto de OUTRA pasta
envn = dict(env, DISPLAY=display, LUMNA_NAV_DEBUG="1", LUMNA_NAV_SEM_GPU="1", LUMNA_NAV_DADOS=f"{run}/dados", LUMNA_NAV_SERVICOS=f"{run}/servicos.json")
nav_log = f"{run}/nav.log"
nav = subprocess.Popen([f"{stage}/lumna-navegador"], cwd=run, env=envn, stdout=open(nav_log, "w"), stderr=subprocess.STDOUT)
procs.append(nav)


def log():
    return open(nav_log, errors="replace").read()


def conta(pad):
    return len(re.findall(pad, log()))


def evento(tipo, codigo, valor):
    with open(xbox, "wb", buffering=0) as f:
        f.write(struct.pack("qqHHi", 0, 0, tipo, codigo, valor))


EV_KEY, EV_ABS = 1, 3
BTN = {"A": 0x130, "B": 0x131, "X": 0x133, "Y": 0x134, "LB": 0x136, "RB": 0x137, "SELECT": 0x13a, "START": 0x13b}
ABS_X, ABS_Y, ABS_RX, ABS_RY, HAT0X, HAT0Y = 0x00, 0x01, 0x03, 0x04, 0x10, 0x11


def aperta(nome, seg=0.08):
    evento(EV_KEY, BTN[nome], 1)
    time.sleep(seg)
    evento(EV_KEY, BTN[nome], 0)
    time.sleep(0.15)


def dpad(dir_):
    eixo, v = {"CIMA": (HAT0Y, -1), "BAIXO": (HAT0Y, 1), "ESQ": (HAT0X, -1), "DIR": (HAT0X, 1)}[dir_]
    evento(EV_ABS, eixo, v)
    time.sleep(0.08)
    evento(EV_ABS, eixo, 0)
    time.sleep(0.15)


def estado():
    r = subprocess.run([f"{stage}/lumna-navegador", "--estado"], cwd=run, env=envn, capture_output=True, text=True)
    return json.loads(r.stdout) if r.stdout.strip() else {}


def titulo():
    j = janela()
    return j["titulo"] if j else ""


def campo(nome):
    mm = re.search(rf"\|{nome}=([^|]*)", titulo())
    return mm.group(1) if mm else None


# ---------------------------------------------------------------------------------------------------------------------------- 1. a janela e o modo cliente
if not espera(lambda: "shell: desenhou" in log(), 30):
    falha("a interface não abriu: " + log()[-300:])
espera(lambda: janela() is not None and janela().get("foco"), 15)
j = janela()
if not j:
    falha(f"a janela devia ter o app_id lumna-navegador (janelas: {orbit('janelas')[:300]})")
if not espera(lambda: estado().get("estado") == "inicio", 5):
    falha(f"lumna-navegador --estado devia dizer inicio: {estado()}")
time.sleep(1.0)

# ---------------------------------------------------------------------------------------------------------------------------- 2. o Super+M (lumna-navegador --menu) e o menu
# (sem serviço aberto, o menu não abre: o seletor é "sobre o que estiver passando")
subprocess.run([f"{stage}/lumna-navegador", "--menu"], cwd=run, env=envn, capture_output=True)
time.sleep(0.5)
if estado().get("estado") != "inicio":
    falha(f"--menu sem serviço aberto não abre o menu: {estado()}")

# ---------------------------------------------------------------------------------------------------------------------------- 3. no início: direcional + A
n0 = conta(r"controle DIR baixo")
dpad("DIR")
if not espera(lambda: conta(r"controle DIR baixo") > n0, 4):
    falha("o navegador devia receber o direcional do serviço de controles ('controle DIR baixo'): " + "|".join(re.findall(r"NAV controle[^\n]*", log())[-3:]))
aperta("A")
if not espera(lambda: estado().get("estado") == "servico" and estado().get("ativo") == "sonda", 12):
    falha(f"direita + A no início devia abrir o segundo serviço (a sonda): {estado()} / {titulo()}")
if not espera(lambda: titulo().startswith("SONDA"), 15):
    falha(f"a página da sonda devia carregar: {titulo()}")
espera(lambda: conta(r"site final") >= 1, 15)
time.sleep(1.0)

# ---------------------------------------------------------------------------------------------------------------------------- 4. no site: teclas
dpad("ESQ")
if not espera(lambda: (campo("k") or "").startswith("ArrowLeft"), 4):
    falha(f"o direcional para a esquerda devia chegar ao site como ArrowLeft: {campo('k')}")
dpad("CIMA")
if not espera(lambda: (campo("k") or "").startswith("ArrowUp"), 4):
    falha(f"o direcional para cima devia chegar ao site como ArrowUp: {campo('k')}")
aperta("X")
if not espera(lambda: (campo("k") or "").startswith("Space"), 4):
    falha(f"X devia mandar Espaço (play/pausa) ao site: {campo('k')}")
aperta("Y")
if not espera(lambda: (campo("k") or "").startswith("f/"), 4):
    falha(f"Y devia mandar a tecla f (tela cheia) ao site: {campo('k')}")
aperta("RB")
if not espera(lambda: (campo("k") or "").startswith("PageDown"), 4):
    falha(f"RB devia mandar PageDown ao site: {campo('k')}")

# ---------------------------------------------------------------------------------------------------------------------------- 5. no site: o ponteiro e o clique
j = janela()
orbit("mouse", "mover", str(j["x"] + 100), str(j["y"] + 100))  # o ponteiro de verdade parte daqui
time.sleep(0.4)
antes = campo("m")
evento(EV_ABS, ABS_X, 32767)   # analógico esquerdo todo para a direita...
time.sleep(0.7)
evento(EV_ABS, ABS_X, 0)       # ...e solta
time.sleep(0.4)
depois = campo("m")
try:
    x0, y0 = [int(v) for v in antes.split(",")]
    x1, y1 = [int(v) for v in depois.split(",")]
    if x1 - x0 < 150:
        falha(f"o analógico esquerdo devia mover o ponteiro para a direita (x {x0} → {x1})")
    if abs(y1 - y0) > 40:
        falha(f"movendo só para a direita o y quase não muda (y {y0} → {y1})")
except Exception:
    falha(f"o ponteiro devia mexer e a página devia receber o movimento (antes {antes}, depois {depois})")
c0, d0 = int(campo("c") or 0), int(campo("d") or 0)
aperta("A", 0.15)
if not espera(lambda: int(campo("d") or 0) > d0 and int(campo("c") or 0) > c0, 4):
    falha(f"A devia clicar no ponteiro (mousedown e click na página): d {d0}→{campo('d')} c {c0}→{campo('c')}")
    orbit("mouse", "botao", "baixo"); time.sleep(0.1); orbit("mouse", "botao", "cima"); time.sleep(0.4)
    print(f"  (diagnóstico: o clique direto pelo Orbit depois: {titulo()})", file=sys.stderr)
    print(f"  (log do navegador: {re.findall(r'NAV clique[^\n]*', log())})", file=sys.stderr)
# o ponteiro nunca chega à moldura de redimensionar: fica ao menos 12 px dentro da janela (empurra o analógico até o fim para a direita e para baixo)
evento(EV_ABS, ABS_X, 32767); evento(EV_ABS, ABS_Y, 32767)
time.sleep(1.8)
evento(EV_ABS, ABS_X, 0); evento(EV_ABS, ABS_Y, 0)
time.sleep(0.4)
x2, y2 = [int(v) for v in (campo("m") or "0,0").split(",")]
wj = janela()
if x2 > wj["w"] - 12 or y2 > wj["h"] - 12:
    falha(f"o ponteiro do controle tem de ficar 12 px dentro da janela (m={x2},{y2} numa janela {wj['w']}x{wj['h']})")
c1 = int(campo("c") or 0)
aperta("A", 0.15)
if not espera(lambda: int(campo("c") or 0) > c1, 4):
    falha(f"mesmo no canto da janela A ainda clica (c {c1} → {campo('c')})")

# o analógico esquerdo NÃO manda setas ao site (ele é o ponteiro): a última tecla continua a de antes
k_antes = campo("k")
evento(EV_ABS, ABS_Y, 32767)
time.sleep(0.5)
evento(EV_ABS, ABS_Y, 0)
time.sleep(0.4)
if campo("k") != k_antes:
    falha(f"no site o analógico esquerdo só move o ponteiro, não manda setas: {k_antes} → {campo('k')}")

# ---------------------------------------------------------------------------------------------------------------------------- 6. no site: a rolagem (analógico direito)
w0 = int(campo("w") or 0)
evento(EV_ABS, ABS_RY, 32767)
time.sleep(0.8)
evento(EV_ABS, ABS_RY, 0)
time.sleep(0.4)
w1 = int(campo("w") or 0)
if w1 - w0 < 200:
    falha(f"o analógico direito para baixo devia rolar a página (deltaY total {w0} → {w1})")

# ---------------------------------------------------------------------------------------------------------------------------- 7. o menu: Start, Super+M (--menu), B, Select
n = conta(r"estado menu")
aperta("START")
if not espera(lambda: conta(r"estado menu") > n and estado().get("estado") == "menu", 6):
    falha(f"Start devia abrir o menu de serviços: {estado()}")
time.sleep(1.2)
aperta("START")
if not espera(lambda: estado().get("estado") == "servico", 6):
    falha(f"Start de novo devia fechar o menu: {estado()}")
time.sleep(1.0)
subprocess.run([f"{stage}/lumna-navegador", "--menu"], cwd=run, env=envn, capture_output=True)   # o Super+M do Orbit
if not espera(lambda: estado().get("estado") == "menu", 6):
    falha(f"lumna-navegador --menu (Super+M) devia abrir o menu: {estado()}")
time.sleep(1.2)
dpad("ESQ")  # no menu, o direcional navega a interface (HTML), não manda tecla ao site
k_menu = campo("k")
aperta("B")
if not espera(lambda: estado().get("estado") == "servico", 6):
    falha(f"B no menu devia fechar o menu (voltar ao vídeo): {estado()}")
time.sleep(1.0)
aperta("SELECT")
if not espera(lambda: estado().get("estado") == "inicio", 6):
    falha(f"Select devia ir ao início: {estado()}")

# ---------------------------------------------------------------------------------------------------------------------------- 7b. o ANALÓGICO navega a interface igual ao direcional (início, abas, catálogo)
# (o relato do Eric: "o analógico funciona em algumas partes, em outras tenho de focar primeiro com as setas")
def foco():
    m_ = re.findall(r"NAV shell: foco zona=(\w+) idx=(\d+)/(\d+)", log())
    return m_[-1] if m_ else None


def analogico(dir_, seg=0.22):
    eixo, v = {"CIMA": (ABS_Y, -32768), "BAIXO": (ABS_Y, 32767), "ESQ": (ABS_X, -32768), "DIR": (ABS_X, 32767)}[dir_]
    evento(EV_ABS, eixo, v)
    time.sleep(seg)
    evento(EV_ABS, eixo, 0)
    time.sleep(0.35)


def percorre(entrada, passos):
    """volta ao início e faz os passos com a `entrada` (dpad ou analogico); devolve o foco depois de cada passo"""
    subprocess.run([f"{stage}/lumna-navegador", "--inicio"], cwd=run, env=envn, capture_output=True)
    time.sleep(1.0)
    espera(lambda: estado().get("estado") == "inicio", 6)
    time.sleep(1.0)
    # o início volta "para onde estava" (o catálogo): Esc leva ao início (e no início não faz nada); a comparação parte sempre da tela de início
    orbit("tecla_falsa", "0", "1")
    time.sleep(0.8)
    r = []
    for p_ in passos:
        antes = foco()
        if p_ == "A":
            aperta("A")
        else:
            entrada(p_)
        time.sleep(0.5)
        r.append(foco())
    return r


# salva a sonda no catálogo (para o catálogo ter um cartão) e volta ao início
subprocess.run([f"{stage}/lumna-navegador", "--inicio"], cwd=run, env=envn, capture_output=True)
time.sleep(1.2)
tecla_servico = None
aperta("A")   # abre o serviço em foco (Alfa) e salva a página
if espera(lambda: estado().get("estado") == "servico", 10):
    time.sleep(2.5)
    subprocess.run([f"{stage}/lumna-navegador", "--salvar"], cwd=run, env=envn, capture_output=True)
    time.sleep(1.5)
caminho = ["DIR", "ESQ", "CIMA", "CIMA", "DIR", "ESQ", "BAIXO", "BAIXO", "CIMA", "CIMA", "DIR", "A", "BAIXO", "A", "BAIXO", "CIMA", "CIMA", "ESQ"]
com_dpad = percorre(dpad, caminho)
com_stick = percorre(analogico, caminho)
for k_, (a_, b_) in enumerate(zip(com_dpad, com_stick)):
    if a_ != b_:
        falha(f"o analógico devia navegar igual ao direcional: passo {k_ + 1} ({caminho[k_]}) direcional={a_} analógico={b_}")
if None in com_dpad:
    falha(f"o direcional devia mudar o foco a cada passo: {com_dpad}")
if os.environ.get("CONTROLE_VERBOSO"):
    print("  passos            :", caminho, file=sys.stderr)
    print("  foco (direcional) :", [f"{z}{i}" for z, i, _ in [x for x in com_dpad if x]], file=sys.stderr)
    print("  foco (analógico)  :", [f"{z}{i}" for z, i, _ in [x for x in com_stick if x]], file=sys.stderr)

# um analógico de VERDADE nunca é reto: empurrado para a direita ele também marca um pouco para cima. Na interface vale UM eixo só (o dominante):
# a direita com um pouco de cima move só para a direita (antes virava direita E cima e a interface pulava de zona)
subprocess.run([f"{stage}/lumna-navegador", "--inicio"], cwd=run, env=envn, capture_output=True)
time.sleep(1.2)
orbit("tecla_falsa", "0", "1")
time.sleep(0.8)
antes = foco()
evento(EV_ABS, ABS_X, 29000)    # ~89% para a direita
evento(EV_ABS, ABS_Y, -11000)   # e ~34% para cima
time.sleep(0.3)
evento(EV_ABS, ABS_X, 0)
evento(EV_ABS, ABS_Y, 0)
time.sleep(0.6)
depois = foco()
if not antes or not depois or depois[0] != antes[0] or int(depois[1]) != int(antes[1]) + 1:
    falha(f"o analógico torto (direita com um pouco de cima) devia mover só para a direita (um eixo, o dominante): {antes} → {depois}")

# ---------------------------------------------------------------------------------------------------------------------------- 7c. o controle em TODOS os sites: modo "setas", User-Agent de TV, B, LB+RB
def abre(n, id_):
    """do início, anda o direcional até o n-ésimo serviço e abre com A"""
    subprocess.run([f"{stage}/lumna-navegador", "--inicio"], cwd=run, env=envn, capture_output=True)
    espera(lambda: estado().get("estado") == "inicio", 6)
    time.sleep(1.0)
    orbit("tecla_falsa", "0", "1")   # Esc: se o início voltou "para onde estava" (o catálogo), leva à tela de início
    time.sleep(0.8)
    for _ in range(12):
        f_ = foco()
        if f_ and f_[0] == "servicos" and int(f_[1]) == n:
            break
        dpad("DIR" if not f_ or f_[0] != "servicos" or int(f_[1]) < n else "ESQ")
        time.sleep(0.3)
    aperta("A")
    return espera(lambda: estado().get("estado") == "servico" and estado().get("ativo") == id_, 12)


def sinal(nome, esperado, seg=5, onde=None):
    return espera(lambda: (campo(nome) or "") == esperado, seg)


if not abre(2, "nav"):
    falha(f"devia abrir o serviço 'nav': {estado()}")
elif not espera(lambda: titulo().startswith("NAV|"), 15):
    falha(f"a página de setas devia carregar: {titulo()}")
else:
    time.sleep(1.5)
    if estado().get("controle") != "setas":
        falha(f"um serviço sem 'modo' abre em modo setas: {estado()}")
    if campo("nav") != "1":
        falha(f"o nav.js devia estar na página (window.__lumnaNav): {titulo()}")
    dpad("DIR")
    if not espera(lambda: campo("f") not in (None, "-"), 4):
        falha(f"a primeira seta devia pôr o foco no item mais perto do meio: {titulo()}")
    dpad("CIMA"); dpad("ESQ")
    if not sinal("f", "b1"):
        falha(f"cima + esquerda a partir do meio devia chegar em B1: {titulo()}")
    aperta("A")
    if not espera(lambda: campo("c") == "b1" and campo("n") == "1", 4):
        falha(f"A (Enter) devia clicar no item em foco: {titulo()}")
    # o analógico esquerdo também são setas (UM eixo, o dominante): direita → B2, que A clica
    evento(EV_ABS, ABS_X, 32767); time.sleep(0.2); evento(EV_ABS, ABS_X, 0); time.sleep(0.5)
    if not sinal("f", "b2"):
        falha(f"o analógico para a direita devia mover o foco para B2 (setas): {titulo()}")
    evento(EV_ABS, ABS_X, 29000); evento(EV_ABS, ABS_Y, 11000); time.sleep(0.2); evento(EV_ABS, ABS_X, 0); evento(EV_ABS, ABS_Y, 0); time.sleep(0.5)
    if campo("f") != "b2":   # direita torta (com um pouco de baixo) ao lado do último item: um eixo só, e não há nada à direita
        falha(f"direita com um pouco de baixo devia contar só a direita, e B2 é o último dessa linha: {titulo()}")
    evento(EV_ABS, ABS_Y, 32767); time.sleep(0.2); evento(EV_ABS, ABS_Y, 0); time.sleep(0.5)
    if not sinal("f", "b4"):
        falha(f"o analógico para baixo devia mover o foco para B4: {titulo()}")
    aperta("A")   # B4 é um link: só abre com o Enter "digitado" (o caractere)
    if not espera(lambda: titulo().startswith("SONDA"), 6):
        falha(f"A num link devia abri-lo: {titulo()}")
    aperta("B")   # voltar: o histórico
    if not espera(lambda: titulo().startswith("NAV|"), 8):
        falha(f"B devia voltar uma página (serviço sem 'voltar' = histórico): {titulo()}")
    # a rolagem (analógico direito) vai direto ao site, sem ponteiro
    time.sleep(1.0)
    w0 = int(campo("w") or 0)
    evento(EV_ABS, ABS_RY, 32767); time.sleep(0.8); evento(EV_ABS, ABS_RY, 0); time.sleep(0.5)
    w1 = int(campo("w") or 0)
    if w1 - w0 < 200:
        falha(f"no modo setas o analógico direito rola a página (deltaY {w0} → {w1}, positivo = para baixo)")
    # LB sozinho = PageUp (ao soltar); LB+RB juntos = troca setas <-> cursor SEM mandar PageUp/PageDown
    aperta("LB")
    if not sinal("k", "PageUp"):
        falha(f"LB sozinho devia mandar PageUp: {titulo()}")
    dpad("ESQ")
    evento(EV_KEY, BTN["LB"], 1); evento(EV_KEY, BTN["RB"], 1); time.sleep(0.15); evento(EV_KEY, BTN["LB"], 0); evento(EV_KEY, BTN["RB"], 0); time.sleep(0.5)
    if estado().get("controle") != "cursor":
        falha(f"LB+RB devia trocar para o modo cursor: {estado()}")
    if campo("k") != "ArrowLeft":
        falha(f"LB+RB não manda PageUp/PageDown (a última tecla seguia ArrowLeft): {titulo()}")
    f0 = campo("f")
    evento(EV_ABS, ABS_X, 32767); time.sleep(0.4); evento(EV_ABS, ABS_X, 0); time.sleep(0.4)
    if campo("k") != "ArrowLeft" or campo("f") != f0:
        falha(f"no modo cursor o analógico esquerdo não manda setas: {titulo()}")
    evento(EV_KEY, BTN["LB"], 1); evento(EV_KEY, BTN["RB"], 1); time.sleep(0.15); evento(EV_KEY, BTN["LB"], 0); evento(EV_KEY, BTN["RB"], 0); time.sleep(0.5)
    if estado().get("controle") != "setas":
        falha(f"LB+RB de novo devia voltar ao modo setas: {estado()}")

# o serviço de TV: o User-Agent de TV vai no pedido E na página (e some nos outros serviços); nenhum nav.js nele; B = Escape
if not abre(3, "tv"):
    falha(f"devia abrir o serviço 'tv': {estado()}")
elif not espera(lambda: titulo().startswith("UA|"), 15):
    falha(f"a página do eco devia carregar: {titulo()}")
else:
    time.sleep(1.0)
    if "SMART-TV" not in (campo("http") or ""):
        falha(f"o serviço de TV devia mandar o User-Agent de TV no pedido: {titulo()}")
    if "SMART-TV" not in (campo("js") or ""):
        falha(f"o serviço de TV devia ter o User-Agent de TV em navigator.userAgent: {titulo()}")
    if campo("nav") != "0":
        falha(f"a interface de TV tem a navegação dela: sem nav.js: {titulo()}")
    dpad("DIR")
    if not sinal("k", "ArrowRight"):
        falha(f"o direcional devia chegar à interface de TV como seta: {titulo()}")
    aperta("A")
    if not sinal("k", "Enter"):
        falha(f"A devia mandar Enter à interface de TV: {titulo()}")
    aperta("B")
    if not sinal("k", "Escape"):
        falha(f"B no serviço de TV (voltar = esc) devia mandar Escape: {titulo()}")
if not abre(4, "eco"):
    falha(f"devia abrir o serviço 'eco': {estado()}")
elif not espera(lambda: titulo().startswith("UA|"), 15):
    falha(f"a página do eco devia carregar: {titulo()}")
else:
    time.sleep(1.0)
    if "SMART-TV" in (campo("http") or "") or "SMART-TV" in (campo("js") or ""):
        falha(f"depois do serviço de TV, outro serviço volta ao User-Agent normal: {titulo()}")
    if campo("nav") != "1":
        falha(f"nos outros sites o nav.js está na página: {titulo()}")

# ---------------------------------------------------------------------------------------------------------------------------- 7d. o popup de login (window.open): fica DENTRO da janela, mantém window.opener, o controle fala com ele
if not abre(5, "login"):
    falha(f"devia abrir o serviço 'login': {estado()}")
elif not espera(lambda: titulo().startswith("LOGIN|"), 15):
    falha(f"a página de login devia carregar: {titulo()}")
else:
    time.sleep(1.5)
    dpad("DIR"); dpad("ESQ")
    if not sinal("k", "ArrowLeft"):
        falha(f"o direcional devia chegar à página de login: {titulo()}")
    n_ab = conta(r"NAV popup aberto"); n_fe = conta(r"NAV popup fechado")
    # o primeiro botão: o popup devolve o resultado por window.opener e fecha sozinho
    for _ in range(4):
        dpad("ESQ") if campo("f") != "b1" else None
    aperta("A")
    if not espera(lambda: conta(r"NAV popup aberto") > n_ab, 8):
        falha("A no botão devia abrir o popup DENTRO da janela (popup aberto): " + "|".join(re.findall(r"NAV popup[^\n]*|NAV site ERRO[^\n]*", log())[-4:]))
    if not espera(lambda: campo("msg") == "token-ok", 6):
        falha(f"o popup devia falar com a página pelo window.opener (postMessage): {titulo()}")
    if not espera(lambda: conta(r"NAV popup fechado") > n_fe, 6):
        falha("o popup devia fechar sozinho (window.close)")
    if not titulo().startswith("LOGIN|"):
        falha(f"depois do popup a página de login continua na tela (antes ficava preta): {titulo()}")
    if "popup: abriu opener=1 nav=0" not in log() and "popup: abriu opener=1" not in log():
        falha("o popup devia ter window.opener")
    # o segundo botão: o popup fica aberto e RECEBE o controle (setas, B fecha)
    n_ab = conta(r"NAV popup aberto"); n_fe = conta(r"NAV popup fechado")
    dpad("DIR"); time.sleep(0.4)
    aperta("A")
    if not espera(lambda: conta(r"NAV popup aberto") > n_ab, 8):
        falha(f"o segundo botão devia abrir o popup: {titulo()} {campo('f')}")
    else:
        time.sleep(1.2)
        k0 = campo("k")
        dpad("DIR")
        if not espera(lambda: "popup: tecla ArrowRight" in log(), 4):
            falha("com o popup aberto o direcional vai para o POPUP")
        if campo("k") != k0:
            falha(f"...e não para a página de trás: {k0} → {campo('k')}")
        if "popup: nav-depois=1" not in log():
            falha("o nav.js também entra na página do popup")
        aperta("B")
        if not espera(lambda: conta(r"NAV popup fechado") > n_fe, 5):
            falha("B no popup devia fechá-lo (sem história para voltar)")
        dpad("ESQ")
        if not sinal("k", "ArrowLeft"):
            falha(f"depois de fechar, o controle volta à página: {titulo()}")

# ---------------------------------------------------------------------------------------------------------------------------- 8. nada foi interpretado a mais
if "Cannot add multiple" in log():
    falha("o CEF recusou as duas visões na mesma janela")

sai()
shutil.rmtree(run, ignore_errors=True) if not falhas else print(f"(log em {run}: nav.log)", file=sys.stderr)
if falhas:
    sys.exit(1)
print("controle: ok")
