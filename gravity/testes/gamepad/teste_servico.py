#!/usr/bin/env python3
"""O serviço lumna-gamepad de ponta a ponta, com controles de mentira (FIFOs) e um wtype de mentira que anota as teclas: navegação só com o Gaming em foco,
repetição do direcional, o botão Guia, controles ignorados, apelido, vibração, hotplug e quem assina os eventos."""
import json, os, socket, struct, subprocess, sys, time

run, gravity = sys.argv[1], sys.argv[2]
falhas = []
sock = f"{run}/lumna-gamepad.sock"
log_teclas = f"{run}/teclas.log"
falsos = f"{run}/falsos"
EV_KEY, EV_ABS = 1, 3
BTN_SOUTH, BTN_EAST, BTN_TL, BTN_TR, BTN_MODE = 0x130, 0x131, 0x136, 0x137, 0x13c
ABS_HAT0X, ABS_HAT0Y = 0x10, 0x11


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def espera(cond, seg=4):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.05)
    return cond()


def confere(cond, o_que, seg=4):
    if not espera(cond, seg):
        falha(o_que)


def pedido(p):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(3)
    s.connect(sock)
    s.sendall((p + "\n").encode())
    r = s.makefile().readline()
    s.close()
    return json.loads(r)


def evento(fifo, tipo, codigo, valor):
    with open(fifo, "wb", buffering=0) as f:
        f.write(struct.pack("qqHHi", 0, 0, tipo, codigo, valor))


def teclas():
    return open(log_teclas).read().split() if os.path.exists(log_teclas) else []


def limpa():
    open(log_teclas, "w").close()
    time.sleep(0.1)


def foco(app):
    open(f"{run}/foco", "w").write(app)
    time.sleep(0.7)  # o serviço lê o foco de 400 em 400 ms


def aperta(fifo, codigo):
    evento(fifo, EV_KEY, codigo, 1)
    evento(fifo, EV_KEY, codigo, 0)


xbox = f"{falsos}/xbox.ev"
os.mkfifo(xbox)
open(f"{falsos}/xbox.dev", "w").write("[controle]\nnome=Xbox\nuniq=AA:BB:CC:DD:EE:01\nbus=bluetooth\nrumble=sim\nbateria=88\n")
foco("lumna-jogos")
import atexit
os.makedirs(f"{os.environ['HOME']}/.config/lumna", exist_ok=True)
srv = subprocess.Popen([f"{gravity}/gamepad"], env=dict(os.environ), stderr=subprocess.DEVNULL)
atexit.register(srv.kill)
confere(lambda: os.path.exists(sock), "o serviço abre o socket", 5)
time.sleep(0.5)

e = pedido("estado")
confere(lambda: pedido("estado")["controles"][0]["id"] == "AA:BB:CC:DD:EE:01", "estado lista o controle")
c0 = pedido("estado")["controles"][0]
if not (c0["bluetooth"] and c0["bateria"] == 88 and c0["vibra"] and pedido("estado")["cfg"]["zona_morta"] == 15):
    falha(f"estado com os dados do controle e a configuração: {c0}")

# 1. com o Gaming em foco, os botões viram teclas
assina = socket.socket(socket.AF_UNIX)
assina.connect(sock)
assina.sendall(b"assinar\n")
assina.settimeout(3)
leitor = assina.makefile()
time.sleep(0.4)  # o serviço registra a assinatura
limpa()
aperta(xbox, BTN_SOUTH)
aperta(xbox, BTN_EAST)
aperta(xbox, BTN_TL)
aperta(xbox, BTN_TR)
confere(lambda: sorted(teclas()) == sorted(["-k", "Return", "-k", "Escape", "-k", "Prior", "-k", "Next"]), f"A, B, LB, RB viram Enter, Esc, Page Up, Page Down ({teclas()})")
linha = leitor.readline()
if json.loads(linha).get("botao") != "A":
    falha(f"quem assina recebe os eventos ({linha})")

# 2. o direcional: uma vez, e repetindo enquanto segura
limpa()
evento(xbox, EV_ABS, ABS_HAT0Y, 1)
time.sleep(1.0)
evento(xbox, EV_ABS, ABS_HAT0Y, 0)
time.sleep(0.3)
downs = teclas().count("Down")
if not (3 <= downs <= 12):
    falha(f"segurar o direcional repete (Down {downs} vezes em 1 s)")
limpa()
evento(xbox, EV_ABS, ABS_HAT0X, -1)
evento(xbox, EV_ABS, ABS_HAT0X, 0)
confere(lambda: teclas() == ["-k", "Left"], f"um toque curto no direcional manda uma tecla só ({teclas()})")

# 3. com um jogo em foco, nenhuma tecla (mas quem assina ainda sabe)
foco("steam_app_1001")
limpa()
aperta(xbox, BTN_SOUTH)
time.sleep(0.8)
if teclas():
    falha(f"com um jogo em foco o serviço não pode mandar tecla ({teclas()})")
foco("lumna-jogos")

# 4. o Guia: biblioteca (abre o LUMNA Jogos), emma e nada
open(f"{run}/chamadas.log", "w").close()
aperta(xbox, BTN_MODE)
confere(lambda: "lumna-jogos" in open(f"{run}/chamadas.log").read(), "o Guia abre o LUMNA Jogos (guia=biblioteca)")
ini = f"{os.environ['HOME']}/.config/lumna/gamepad.ini"
open(ini, "w").write("[geral]\nguia=emma\n")
pedido("recarregar")
open(f"{run}/chamadas.log", "w").close()
aperta(xbox, BTN_MODE)
confere(lambda: "lumna-emma-chat" in open(f"{run}/chamadas.log").read(), "o Guia chama a Emma (guia=emma)")
open(ini, "w").write("[geral]\nguia=nada\n")
pedido("recarregar")
open(f"{run}/chamadas.log", "w").close()
aperta(xbox, BTN_MODE)
time.sleep(1.0)
if open(f"{run}/chamadas.log").read().strip():
    falha("guia=nada não faz nada")
open(ini, "w").write("[geral]\nguia=inicio\n")
pedido("recarregar")
limpa()
aperta(xbox, BTN_MODE)
confere(lambda: "Home" in teclas(), f"guia=inicio manda Home para a biblioteca ({teclas()})", 5)

# 5. trocar A e B pela configuração
open(ini, "w").write("[geral]\ntrocar_ab=sim\nguia=nada\n")
pedido("recarregar")
limpa()
aperta(xbox, BTN_SOUTH)
confere(lambda: teclas() == ["-k", "Escape"], f"trocar_ab: o A vira Esc ({teclas()})")
open(ini, "w").write("[geral]\nguia=nada\n")
pedido("recarregar")

# 6. apelido, vibrar, ignorar
if not pedido("apelido AA:BB:CC:DD:EE:01 Controle da sala")["ok"]:
    falha("apelido")
confere(lambda: pedido("estado")["controles"][0]["apelido"] == "Controle da sala", "o apelido aparece no estado")
if not pedido("vibrar AA:BB:CC:DD:EE:01")["ok"]:
    falha("vibrar")
confere(lambda: "AA:BB:CC:DD:EE:01 300" in open(f"{falsos}/vibrou.log").read(), "o tranco chegou ao controle")
open(ini, "w").write("[geral]\nvibracao=nao\nguia=nada\n")
pedido("recarregar")
if pedido("vibrar AA:BB:CC:DD:EE:01")["ok"]:
    falha("com a vibração desligada, vibrar é recusado")
open(ini, "w").write("[geral]\nguia=nada\n")
pedido("recarregar")
pedido("ignorar AA:BB:CC:DD:EE:01 sim")
confere(lambda: pedido("estado")["controles"][0]["ignorado"], "o controle ignorado aparece como ignorado")
limpa()
aperta(xbox, BTN_SOUTH)
time.sleep(0.8)
if teclas():
    falha(f"controle ignorado não manda tecla ({teclas()})")
pedido("ignorar AA:BB:CC:DD:EE:01 nao")
if pedido("apelido a=b x")["ok"]:
    falha("id com lixo é recusado")
if pedido("faz-coisa")["ok"]:
    falha("pedido desconhecido é recusado")

# 7. hotplug: um controle novo aparece e quem assina é avisado
ps = f"{falsos}/ps.ev"
os.mkfifo(ps)
open(f"{falsos}/ps.dev", "w").write("[controle]\nnome=DualSense\nuniq=11:22:33:44:55:66\nbus=usb\nrumble=nao\n")
confere(lambda: len(pedido("estado")["controles"]) == 2, "o controle novo entra na lista (até 2 s)", 6)
limpa()
aperta(ps, BTN_SOUTH)
confere(lambda: teclas() == ["-k", "Return"], f"o controle novo já navega ({teclas()})")
os.unlink(f"{falsos}/ps.dev")
os.unlink(ps)
confere(lambda: len(pedido("estado")["controles"]) == 1, "o controle desconectado sai da lista", 6)

# 8. um segundo serviço não toma o socket
r = subprocess.run([f"{gravity}/gamepad"], env=dict(os.environ), capture_output=True, text=True, timeout=10)
if r.returncode != 0 or "já está rodando" not in r.stderr:
    falha("um segundo lumna-gamepad devia sair avisando que já roda")
srv.terminate()
sys.exit(1 if falhas else 0)
