#!/usr/bin/env python3
"""O controle de longe do Connect: informações do PC, energia, agendamentos e a Emma por texto.
O lumna-connect é o de verdade; o celular, o systemctl, o lumna-bloqueio, o /proc e a Emma são de mentira."""
import asyncio, json, os, socket, subprocess, sys, tempfile, threading, time

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
from celular_falso import entrar, pedir  # noqa: E402

CONTROLE = sys.argv[1]
T = tempfile.mkdtemp(prefix="controle-teste-")
run, cfg, home, raiz = (os.path.join(T, d) for d in ("run", "cfg", "home", "raiz"))
for d in (run, cfg, home, raiz + "/proc", raiz + "/sys/class/power_supply/BAT0"):
    os.makedirs(d)
os.chmod(run, 0o700)
open(raiz + "/proc/uptime", "w").write("5400.55 20000.1\n")
open(raiz + "/proc/loadavg", "w").write("0.42 0.30 0.20 1/300 999\n")
open(raiz + "/proc/meminfo", "w").write("MemTotal:       16000000 kB\nMemFree: 1 kB\nMemAvailable:    9000000 kB\n")
open(raiz + "/sys/class/power_supply/BAT0/capacity", "w").write("77\n")
open(raiz + "/sys/class/power_supply/BAT0/status", "w").write("Charging\n")
AGORA = 1_800_000_000
EMMA_SOCK = os.path.join(run, "emma-falsa.sock")
env = dict(os.environ, XDG_RUNTIME_DIR=run, XDG_CONFIG_HOME=cfg, HOME=home, WAYLAND_DISPLAY="falso-0", CLIP_ARQ=os.path.join(T, "clip"), CLIP_COPIADO=os.path.join(T, "copiado"),
           AVISOS_LOG=os.path.join(T, "avisos.log"), ENERGIA_LOG=os.path.join(T, "energia.log"), LUMNA_RAIZ_SISTEMA=raiz, LUMNA_AGORA=str(AGORA),
           LUMNA_EMMA_SOCK=EMMA_SOCK, PATH=os.path.join(AQUI, "bin_controle") + ":" + os.path.join(AQUI, "bin") + ":" + os.environ["PATH"],
           DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent")
open(env["CLIP_ARQ"], "w").write("")
falhas = []
emma_pedidos = []


def confere(c, m):
    if not c:
        falhas.append(m)
        print("FALHOU:", m)


def emma_falsa():
    """Uma linha por pedido, uma linha de JSON de volta (o protocolo do lumna-emma)."""
    s = socket.socket(socket.AF_UNIX)
    s.bind(EMMA_SOCK)
    s.listen(5)
    while True:
        c, _ = s.accept()
        linha = c.makefile().readline().strip()
        emma_pedidos.append(linha)
        if linha.startswith("celular "):
            texto = linha[8:]
            resp = {"ok": False, "erro": "sem internet"} if "quebra" in texto else {"ok": True, "local": False, "resposta": "Feito: " + texto}
        else:
            resp = {"ok": True}
        c.sendall((json.dumps(resp) + "\n").encode())
        c.close()


def le(arq):
    return open(arq).read() if os.path.exists(arq) else ""


async def espera(cond, seg=8):
    t0 = time.time()
    while time.time() - t0 < seg:
        if cond():
            return True
        await asyncio.sleep(0.1)
    return False


async def drena(ws):
    """O PC também avisa sozinho (a lista mudou, um recurso mudou): tira isso da frente antes de pedir de novo."""
    while True:
        try:
            await asyncio.wait_for(ws.recv(), 0.5)
        except asyncio.TimeoutError:
            return


async def principal():
    threading.Thread(target=emma_falsa, daemon=True).start()
    srv = subprocess.Popen([CONTROLE], env=env, stderr=open(os.path.join(T, "connect.err"), "w"))
    try:
        confere(await espera(lambda: os.path.exists(os.path.join(run, "lumna-connect.sock"))), "o serviço não subiu")
        await asyncio.sleep(0.5)
        r = subprocess.run([CONTROLE, "parear"], env=env, capture_output=True, text=True, timeout=15)
        ws, token = await entrar(codigo=json.loads(r.stdout.strip().splitlines()[-1])["codigo"])

        # informações do PC
        i = await pedir(ws, {"type": "info"})
        confere(i.get("type") == "info" and i["uptime"] == 5400 and i["load"] == "0.42", f"info: {i}")
        confere(i["memory_total_kb"] == 16000000 and i["memory_available_kb"] == 9000000, f"memória: {i}")
        confere(i["battery"] == 77 and i["charging"] is True, f"bateria: {i}")
        confere(bool(i["host"]) and bool(i["user"]), f"nome do PC: {i}")

        # energia
        for acao, esperado in (("lock", "lumna-bloqueio"), ("suspend", "systemctl suspend"), ("reboot", "systemctl reboot"), ("poweroff", "systemctl poweroff")):
            r = await pedir(ws, {"type": "power", "action": acao})
            confere(r.get("type") == "ok", f"power {acao}: {r}")
            confere(await espera(lambda: esperado in le(env["ENERGIA_LOG"])), f"power {acao} devia rodar {esperado}: {le(env['ENERGIA_LOG'])!r}")
        r = await pedir(ws, {"type": "power", "action": "apagar-tudo"})
        confere(r.get("type") == "error", f"ação inventada devia dar erro: {r}")

        # a Emma por texto
        r = await pedir(ws, {"type": "emma", "text": "abre a netflix"}, "emma-reply")
        confere(r.get("type") == "emma-reply" and r.get("content") == "Feito: abre a netflix", f"emma: {r}")
        r = await pedir(ws, {"type": "emma", "text": "quebra tudo"}, "emma-reply")
        confere(r.get("type") == "error" and "sem internet" in r.get("message", ""), f"erro da Emma devia chegar ao celular: {r}")
        r = await pedir(ws, {"type": "emma-decide", "call_id": "abc-123", "allow": True})
        confere(r.get("type") == "ok" and await espera(lambda: "confirmar abc-123 sim" in emma_pedidos), f"emma-decide: {r} {emma_pedidos}")
        r = await pedir(ws, {"type": "emma-decide", "call_id": "abc\nenviar apaga", "allow": True})
        confere(r.get("type") == "error", f"id com quebra de linha devia ser recusado: {r}")
        confere(not any("apaga" in p for p in emma_pedidos), f"nada pode escapar para o socket da Emma: {emma_pedidos}")

        # agendamentos
        r = await pedir(ws, {"type": "schedule-list"}, "schedule")
        confere(r.get("tasks") == [], f"lista começa vazia: {r}")
        r = await pedir(ws, {"type": "schedule-add", "task": {"action": "remind", "at": AGORA + 3600, "text": ""}})
        confere(r.get("type") == "error", f"lembrete sem texto devia dar erro: {r}")
        r = await pedir(ws, {"type": "schedule-add", "task": {"action": "poweroff", "at": AGORA - 600}})
        confere(r.get("type") == "error" and "passou" in r["message"], f"horário passado devia dar erro: {r}")
        r = await pedir(ws, {"type": "schedule-add", "task": {"action": "formatar", "at": AGORA + 600}})
        confere(r.get("type") == "error", f"ação inventada: {r}")
        r = await pedir(ws, {"type": "schedule-add", "task": {"action": "remind", "at": AGORA + 7200, "text": "tirar o pão", "daily": True}}, "schedule")
        confere(r.get("type") == "schedule" and len(r["tasks"]) == 1 and r["tasks"][0]["by"] == "Celular Falso", f"add: {r}")
        r = await pedir(ws, {"type": "schedule-add", "task": {"action": "remind", "at": AGORA - 30, "text": "agora", "daily": False}}, "schedule")
        r = await pedir(ws, {"type": "schedule-add", "task": {"action": "suspend", "at": AGORA - 10}}, "schedule")
        confere(len(r["tasks"]) == 3 and r["tasks"][0]["at"] <= r["tasks"][1]["at"], f"3 tarefas em ordem: {r}")
        guarda = json.load(open(os.path.join(cfg, "lumna", "schedule.json")))
        confere(len(guarda) == 3 and guarda[0]["by"] == "Celular Falso", f"schedule.json no formato antigo: {guarda}")

        # o relógio do serviço anda de 15 em 15 s: as duas vencidas rodam sozinhas
        log0 = le(env["ENERGIA_LOG"])
        confere(await espera(lambda: "Lembrete" in le(env["AVISOS_LOG"]) and "agora" in le(env["AVISOS_LOG"]), 25), f"o lembrete devia aparecer no PC: {le(env['AVISOS_LOG'])!r}")
        confere(await espera(lambda: le(env["ENERGIA_LOG"]).count("systemctl suspend") > log0.count("systemctl suspend"), 10), "a tarefa de suspender devia rodar sozinha")
        await drena(ws)
        r = await pedir(ws, {"type": "schedule-list"}, "schedule")
        confere(len(r["tasks"]) == 1 and r["tasks"][0]["text"] == "tirar o pão", f"só a diária fica: {r}")
        id_ = r["tasks"][0]["id"]
        r = await pedir(ws, {"type": "schedule-remove", "id": id_}, "schedule")
        confere(r["tasks"] == [], f"remover: {r}")

        # sem o recurso "controle" ligado no PC, nada disso funciona (mas as informações continuam)
        def ctl(*args):
            r = subprocess.run([CONTROLE, *args], env=env, capture_output=True, text=True, timeout=15)
            return json.loads(r.stdout.strip().splitlines()[-1])
        did = ctl("estado")["dispositivos"][0]["id"]
        confere(ctl("recurso", did, "control", "nao")["ok"], "desligar o controle de longe")
        await drena(ws)
        for msg in ({"type": "power", "action": "poweroff"}, {"type": "emma", "text": "oi"}, {"type": "schedule-list"}, {"type": "schedule-add", "task": {"action": "lock", "at": AGORA + 99}}):
            r = await pedir(ws, msg)
            confere(r.get("type") == "error" and "controle de longe" in r["message"], f"com o controle desligado {msg['type']} devia ser recusado: {r}")
        r = await pedir(ws, {"type": "info"})
        confere(r.get("type") == "info", f"info não depende do controle: {r}")
        await ws.close()
    finally:
        srv.terminate()
        srv.wait()


asyncio.run(principal())
if falhas:
    sys.exit(1)
print("controle ok")
