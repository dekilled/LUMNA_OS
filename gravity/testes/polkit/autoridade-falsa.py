#!/usr/bin/python3.12
"""O polkit de mentira para o lumna-polkit: dono de org.freedesktop.PolicyKit1 no D-Bus de sessão, guarda quem se registrou
como agente e chama BeginAuthentication/CancelAuthentication nele. Uso (dentro de um dbus-run-session):
  autoridade-falsa.py PASTA_GRAVITY PASTA_DE_TRABALHO
Os casos conferem o que o helper e a janela de mentira (testes/polkit/*.sh) viram."""
import os, subprocess, sys, threading, time
import dbus, dbus.service, dbus.mainloop.glib
from gi.repository import GLib

grav, trab = sys.argv[1], sys.argv[2]
aqui = os.path.dirname(os.path.abspath(__file__))
dbus.mainloop.glib.threads_init()  # o teste chama o agente de duas linhas ao mesmo tempo (o pedido e o cancelamento)
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
registro = {}
registrado = threading.Event()

class Autoridade(dbus.service.Object):
    @dbus.service.method("org.freedesktop.PolicyKit1.Authority", in_signature="(sa{sv})so", sender_keyword="remetente")
    def RegisterAuthenticationAgent(self, subject, locale, caminho, remetente=None):
        registro.update(remetente=remetente, caminho=str(caminho), sessao=str(subject[1].get("session-id")), tipo=str(subject[0]), locale=str(locale))
        registrado.set()

nome = dbus.service.BusName("org.freedesktop.PolicyKit1", bus)
Autoridade(bus, "/org/freedesktop/PolicyKit1/Authority")
threading.Thread(target=GLib.MainLoop().run, daemon=True).start()

etc = os.path.join(trab, "etc"); os.makedirs(etc, exist_ok=True)
uid = 1234  # a conta "maria" do teste (o teste roda como root, uid 0)
open(os.path.join(etc, "passwd"), "w").write(f"root:x:0:0::/root:/bin/sh\nmaria:x:{uid}:{uid}::/home/maria:/bin/sh\n")
env = dict(os.environ, LUMNA_POLKIT_UID=str(uid), LUMNA_POLKIT_BUS="sessao", XDG_SESSION_ID="teste", LUMNA_ETC=etc,
           LUMNA_POLKIT_JANELA=os.path.join(aqui, "janela-falsa.sh"), JANELA_DIR=trab)
falhas = 0
def confere(cond, msg):
    global falhas
    if not cond:
        print("FALHOU:", msg, file=sys.stderr); falhas += 1

def agente():
    return dbus.Interface(bus.get_object(registro["remetente"], registro["caminho"]), "org.freedesktop.PolicyKit1.AuthenticationAgent")

IDS = dbus.Array([dbus.Struct((dbus.String("unix-user"), dbus.Dictionary({"uid": dbus.UInt32(0)}, signature="sv")), signature=None),
                  dbus.Struct((dbus.String("unix-user"), dbus.Dictionary({"uid": dbus.UInt32(uid)}, signature="sv")), signature=None)],
                 signature="(sa{sv})")
def pedir(cookie, senhas, espera=60000):
    """Põe as senhas que a janela de mentira vai 'digitar' (uma por linha; sem mais, ela cancela) e chama o agente."""
    open(os.path.join(trab, "senhas"), "w").write("".join(s + "\n" for s in senhas))
    for f in ("janela.log", "helper.log"):
        open(os.path.join(trab, f), "w").close()
    try:
        agente().BeginAuthentication("os.lumna.teste", "Fazer algo importante", "", {}, cookie, IDS, timeout=espera / 1000)
        return "ok"
    except dbus.DBusException as e:
        return e.get_dbus_name().rsplit(".", 1)[-1] + ":" + str(e.get_dbus_message())

def log(nome):
    return open(os.path.join(trab, nome)).read()

def sobe(extra):
    e = dict(env, **extra)
    p = subprocess.Popen([os.path.join(grav, "polkit")], env=e, stderr=open(os.path.join(trab, "agente.log"), "a"))
    registrado.clear(); registro.clear()
    confere(registrado.wait(10), "o agente devia se registrar no polkit")
    return p

# ---- modo helper (polkit antigo): o programa setuid ----
p = sobe(dict(LUMNA_POLKIT_HELPER=os.path.join(aqui, "helper-falso.sh")))
confere(registro.get("sessao") == "teste" and registro.get("tipo") == "unix-session", f"registro: {registro}")
confere(registro.get("caminho") == "/os/lumna/polkit/agent", "caminho do agente")

r = pedir("c1", ["certa"])
confere(r == "ok", f"senha certa devia autenticar: {r}")
confere("maria c1" in log("helper.log"), f"o helper devia ver a conta maria e o cookie c1: {log('helper.log')!r}")
confere("--usuario maria" in log("janela.log") and "Fazer algo importante" in log("janela.log"), f"a janela devia receber conta e mensagem: {log('janela.log')!r}")
confere("--erro" not in log("janela.log"), "a primeira tentativa não tem erro")

r = pedir("c2", ["errada", "certa"])
confere(r == "ok", f"errada e depois certa devia autenticar: {r}")
confere(log("janela.log").count("--mensagem") == 2 and log("janela.log").count("--erro") == 1, f"a 2ª janela devia trazer o erro: {log('janela.log')!r}")

r = pedir("c3", ["x", "y", "z"])
confere(r.startswith("Failed") and "muitas tentativas" in r, f"3 erradas: {r}")
confere(log("janela.log").count("--mensagem") == 3, "só 3 tentativas")

r = pedir("c4", [])
confere(r.startswith("Cancelled"), f"janela cancelada: {r}")

# CancelAuthentication com a janela aberta: a janela é fechada e o pedido volta como cancelado
open(os.path.join(trab, "demora"), "w").write("1")
t0 = time.time()
resp = {}
def longo():
    resp["r"] = pedir("c5", ["certa"])
th = threading.Thread(target=longo); th.start()
time.sleep(1.5)
# o cancelamento sai por outro processo (o dbus-python não gosta de duas chamadas bloqueantes ao mesmo tempo na mesma conexão)
subprocess.run(["gdbus", "call", "--session", "--dest", registro["remetente"], "--object-path", registro["caminho"],
                "--method", "org.freedesktop.PolicyKit1.AuthenticationAgent.CancelAuthentication", "c5"], check=True, capture_output=True)
th.join(15)
os.unlink(os.path.join(trab, "demora"))
confere(resp.get("r", "").startswith("Cancelled"), f"CancelAuthentication: {resp}")
confere(time.time() - t0 < 8, "o cancelamento devia fechar a janela na hora")

# um pedido sem a conta dela: só o root, sem ser o dono: cai no root (o nome vem do passwd); sem nenhuma, falha
try:
    agente().BeginAuthentication("os.lumna.teste", "m", "", {}, "c6", dbus.Array([], signature="(sa{sv})"), timeout=10)
    confere(False, "sem contas devia falhar")
except dbus.DBusException as e:
    confere("Failed" in e.get_dbus_name(), f"sem contas: {e.get_dbus_name()}")
p.terminate(); p.wait()

# ---- modo socket (polkit 126+): o helper é um serviço em /run/polkit/agent-helper.socket ----
sock = os.path.join(trab, "helper.sock")
srv = subprocess.Popen(["/usr/bin/python3.12", os.path.join(aqui, "socket-falso.py"), sock, os.path.join(trab, "helper.log")])
for _ in range(50):
    if os.path.exists(sock):
        break
    time.sleep(0.1)
p = sobe(dict(LUMNA_POLKIT_SOCKET=sock))
r = pedir("s1", ["certa"])
confere(r == "ok", f"socket, senha certa: {r}")
confere("maria s1" in log("helper.log"), f"o socket devia receber a conta e depois o cookie: {log('helper.log')!r}")
r = pedir("s2", ["a", "b", "c"])
confere(r.startswith("Failed") and "muitas" in r, f"socket, 3 erradas: {r}")
p.terminate(); p.wait(); srv.terminate()

print("agente do polkit: ok" if not falhas else f"{falhas} falha(s)")
sys.exit(1 if falhas else 0)
