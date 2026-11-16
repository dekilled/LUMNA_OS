#!/usr/bin/env python3
"""Para onde vai o foco quando uma janela fecha (o relato do Eric: "o foco vai para um app lá no fundo", "o VS Code perde o foco sem motivo").

  python3 orbit/testes-foco/foco.py <pasta do orbit>

O Orbit sem tela com XWayland e três xterm (A, B, C, abertos nessa ordem; cada um é uma janela de verdade). Confere:
  1. fechar a janela COM foco passa o foco para a USADA MAIS RECENTEMENTE (a que está logo atrás), e não para a mais antiga;
  2. fechar uma janela SEM foco não mexe no foco de quem está em uso (antes o foco pulava para a mais antiga);
  3. e, em sequência, o foco vai voltando pela ordem de uso (como o Alt+Tab).
"""
import atexit, json, os, re, shutil, subprocess, sys, tempfile, time

orbit_dir = os.path.abspath(sys.argv[1])
run = tempfile.mkdtemp()
os.chmod(run, 0o700)
os.makedirs(f"{run}/cfg")
env = dict(os.environ, XDG_RUNTIME_DIR=run, XDG_CONFIG_HOME=f"{run}/cfg", HOME=f"{run}/home")
env.pop("WAYLAND_DISPLAY", None)
env.pop("DISPLAY", None)
procs, falhas = [], []


def sai():
    for p in procs:
        try:
            p.kill()
        except Exception:
            pass


atexit.register(sai)


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


envo = dict(env, WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1")
orb = subprocess.Popen(["./orbit"], cwd=orbit_dir, env=envo, stdout=open(f"{run}/orbit.log", "w"), stderr=subprocess.STDOUT)
procs.append(orb)
time.sleep(2)
m = re.search(r"DISPLAY=(:\d+)", open(f"{run}/orbit.log").read())
if not m:
    print("foco: dispensado (esta wlroots não tem XWayland)")
    sys.exit(0)
display = m.group(1)
sock = [f for f in os.listdir(run) if re.match(r"orbit\..*\.sock", f)][0]


def msg(*a):
    return subprocess.run([f"{orbit_dir}/orbit-msg", "-s", f"{run}/{sock}", *a], capture_output=True, text=True).stdout


def janelas():
    try:
        return json.loads(msg("janelas") or "{}").get("janelas", [])
    except Exception:
        return []


def com_foco():
    for j in janelas():
        if j.get("foco"):
            return j["titulo"]
    return None


def id_de(titulo):
    for j in janelas():
        if j["titulo"] == titulo:
            return str(j["id"])
    return None


def espera(cond, seg=8.0):
    fim = time.time() + seg
    while time.time() < fim:
        try:
            if cond():
                return True
        except Exception:
            pass
        time.sleep(0.1)
    return False


def abre(nome):
    p = subprocess.Popen(["xterm", "-T", nome, "-e", "sleep", "600"], env=dict(env, DISPLAY=display), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    procs.append(p)
    if not espera(lambda: com_foco() == nome, 10):
        falha(f"a janela {nome} devia abrir já com o foco (foco: {com_foco()})")
    return p


def foca(nome):
    msg("foco", id_de(nome))
    if not espera(lambda: com_foco() == nome, 4):
        falha(f"o foco devia ir para {nome}: {com_foco()}")
    time.sleep(0.3)


def fecha(nome):
    msg("fechar", id_de(nome))
    if not espera(lambda: id_de(nome) is None, 6):
        falha(f"{nome} devia fechar")
    time.sleep(0.8)


A, B, C = "janela-A", "janela-B", "janela-C"
abre(A); time.sleep(0.5)
abre(B); time.sleep(0.5)
abre(C); time.sleep(0.5)
# a ordem de uso agora: C (mais recente), B, A (a mais antiga, "lá no fundo")

# 1. fechar a janela COM foco: o foco vai para a usada mais recentemente (a de trás), não para a mais antiga
foca(B)                       # uso: B, C, A
fecha(B)
if com_foco() != C:
    falha(f"fechei a B (com foco): o foco devia ir para a C (a usada por último), não para a mais antiga: {com_foco()}")

# 2. fechar uma janela SEM foco não mexe no foco de quem está em uso
abre("janela-D"); time.sleep(0.5)    # uso: D, C, A
foca(C)                              # uso: C, D, A
fecha("janela-D")                    # sem foco
if com_foco() != C:
    falha(f"fechei a D (SEM foco): o foco devia continuar na C: {com_foco()}")
abre("janela-E"); time.sleep(0.5)    # uso: E, C, A
foca(E := "janela-E")
fecha(A)                             # a mais antiga, sem foco
if com_foco() != E:
    falha(f"fechei a A (SEM foco): o foco devia continuar na E: {com_foco()}")

# 3. em sequência o foco volta pela ordem de uso: E → C
fecha(E)
if com_foco() != C:
    falha(f"fechei a E (com foco): o foco devia ir para a C (a próxima na ordem de uso): {com_foco()}")

# 4. o cenário do Eric: um navegador MAXIMIZADO (o mais antigo), o Atlas no meio e o Signals por cima. Fechar OU minimizar o Signals leva o foco (e a frente) ao Atlas,
# que estava logo atrás, e NÃO ao navegador (antes o Firefox ia para a frente e o Atlas ficava escondido atrás dele).
fecha(C)   # a única que sobrava: limpa para o cenário ficar claro
NAV, ATLAS, SIGNALS = "navegador-max", "atlas", "signals"
abre(NAV); time.sleep(0.5)
msg("maximizar", id_de(NAV)); time.sleep(0.5)
abre(ATLAS); time.sleep(0.5)
abre(SIGNALS); time.sleep(0.5)
if com_foco() != SIGNALS:
    falha(f"o Signals (o último a abrir) devia ter o foco: {com_foco()}")
def minimiza(nome):
    r = subprocess.run(["xdotool", "search", "--name", f"^{nome}$"], env=dict(env, DISPLAY=display), capture_output=True, text=True).stdout.split()
    if not r:
        falha(f"não achei a janela {nome} para minimizar")
        return
    subprocess.run(["xdotool", "windowminimize", r[0]], env=dict(env, DISPLAY=display), capture_output=True)
    time.sleep(1.0)
minimiza(SIGNALS)
if com_foco() != ATLAS:
    falha(f"minimizei o Signals: o foco devia ir para o Atlas (o que estava logo atrás), não para o navegador: {com_foco()}")
# o Atlas tem de estar VISÍVEL (não escondido atrás do navegador maximizado): é a janela com foco e não está minimizada
j = {x["titulo"]: x for x in janelas()}
if j.get(ATLAS, {}).get("minimizada"):
    falha("o Atlas não pode ter sido minimizado junto")
msg("foco", id_de(SIGNALS)); time.sleep(0.5)       # traz o Signals de volta (minimizada → volta)
if com_foco() != SIGNALS:
    falha(f"dar o foco a uma janela minimizada a traz de volta: {com_foco()}")
fecha(SIGNALS)
if com_foco() != ATLAS:
    falha(f"fechei o Signals: o foco devia ir para o Atlas, não para o navegador: {com_foco()}")

sai()
shutil.rmtree(run, ignore_errors=True) if not falhas else print(f"(log em {run})", file=sys.stderr)
if falhas:
    sys.exit(1)
print("foco: ok")
