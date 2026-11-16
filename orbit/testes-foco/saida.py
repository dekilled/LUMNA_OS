#!/usr/bin/env python3
"""O encerramento do Orbit: o wlroots 0.19+ confere (assert) que ninguém mais ouve os sinais dos objetos que ele destrói em wl_display_destroy, e um ouvinte esquecido
derrubava o Orbit com SIGABRT toda vez que a sessão acabava (o Eric tinha 3 core dumps do Orbit em 2 h: cada login/logout). Aqui o Orbit sobe sem tela com um app X11 e uma
janela aberta, é encerrado pelo caminho normal (a ação `terminar`, a mesma do logout: wl_display_terminate, depois wl_display_destroy) e tem de sair com código 0, sem 'ainda há ouvintes' no log (a conferência que o próprio Orbit faz antes de destruir o display).

  python3 orbit/testes-foco/saida.py <pasta do orbit>      (precisa do xterm; sem XWayland na wlroots o teste é dispensado)
Observação honesta: este ambiente tem o wlroots 0.18, que não aborta nesses casos; a conferência de ouvintes é o que reproduz o assert do 0.20.
"""
import os, re, signal, subprocess, sys, tempfile, time

orbit_dir = os.path.abspath(sys.argv[1])
run = tempfile.mkdtemp()
os.chmod(run, 0o700)
os.makedirs(f"{run}/cfg")
env = dict(os.environ, XDG_RUNTIME_DIR=run, XDG_CONFIG_HOME=f"{run}/cfg", HOME=f"{run}/home", WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1")
env.pop("WAYLAND_DISPLAY", None)
env.pop("DISPLAY", None)
log = open(f"{run}/orbit.log", "w")
orb = subprocess.Popen(["./orbit"], cwd=orbit_dir, env=env, stdout=log, stderr=subprocess.STDOUT)
time.sleep(2)
m = re.search(r"DISPLAY=(:\d+)", open(f"{run}/orbit.log").read())
if not m:
    orb.kill()
    print("saida: dispensado (esta wlroots não tem XWayland)")
    sys.exit(0)
x = subprocess.Popen(["xterm", "-T", "uma-janela", "-e", "sleep", "60"], env=dict(env, DISPLAY=m.group(1)), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
sock = [f for f in os.listdir(run) if re.match(r"orbit\..*\.sock", f)][0]
subprocess.run([f"{orbit_dir}/orbit-msg", "-s", f"{run}/{sock}", "acao", "terminar"], capture_output=True)
try:
    rc = orb.wait(timeout=15)
except subprocess.TimeoutExpired:
    orb.kill()
    rc = None
x.kill()
txt = open(f"{run}/orbit.log", errors="replace").read()
falhas = []
if rc != 0:
    falhas.append(f"o Orbit devia sair com código 0 ao ser encerrado (saiu com {rc}; -6 = SIGABRT)")
esquecidos = re.findall(r"saída: ainda há ouvintes em ([^\n]+)", txt)
if esquecidos:
    falhas.append("ouvintes esquecidos no encerramento (o wlroots 0.20 abortaria): " + ", ".join(esquecidos))
if "Assertion" in txt or "assertion" in txt:
    falhas.append("uma assertion apareceu no log")
if falhas:
    for f in falhas:
        print("FALHOU:", f, file=sys.stderr)
    print(f"(log em {run}/orbit.log)", file=sys.stderr)
    sys.exit(1)
print("saida: ok")
