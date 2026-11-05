#!/usr/bin/env python3
"""O controle do celular aperta teclas pelo wtype: com ele o direcional chega nele, sem ele o celular recebe um erro claro (nunca um "ok" falso)."""
import asyncio, json, os, subprocess, sys, tempfile, time

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
from celular_falso import entrar, pedir  # noqa: E402

CONTROLE = sys.argv[1]
falhas = []


def confere(c, m):
    if not c:
        falhas.append(m)
        print("FALHOU:", m)


def sobe(com_wtype):
    T = tempfile.mkdtemp(prefix="wtype-teste-")
    run, cfg, home = (os.path.join(T, d) for d in ("run", "cfg", "home"))
    for d in (run, cfg, home):
        os.makedirs(d)
    os.chmod(run, 0o700)
    caminho = os.path.join(AQUI, "bin")
    if com_wtype:
        caminho = os.path.join(AQUI, "bin_wtype") + ":" + caminho
    env = dict(os.environ, XDG_RUNTIME_DIR=run, XDG_CONFIG_HOME=cfg, HOME=home, WAYLAND_DISPLAY="falso-0", CLIP_ARQ=os.path.join(T, "clip"),
               CLIP_COPIADO=os.path.join(T, "copiado"), AVISOS_LOG=os.path.join(T, "avisos.log"), WTYPE_LOG=os.path.join(T, "wtype.log"),
               PATH=caminho, DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent")  # sem o PATH do sistema: o wtype de verdade não aparece
    open(env["CLIP_ARQ"], "w").write("")
    srv = subprocess.Popen([CONTROLE], env=env, stderr=open(os.path.join(T, "connect.err"), "w"))
    for _ in range(80):
        if os.path.exists(os.path.join(run, "lumna-connect.sock")):
            break
        time.sleep(0.1)
    time.sleep(0.4)
    r = subprocess.run([CONTROLE, "parear"], env=env, capture_output=True, text=True, timeout=15)
    return srv, env, json.loads(r.stdout.strip().splitlines()[-1])["codigo"], T


async def caso(com_wtype):
    srv, env, codigo, T = sobe(com_wtype)
    try:
        ws, _ = await entrar(codigo=codigo)
        direcional = await pedir(ws, {"type": "pad", "action": "up"})
        menu = await pedir(ws, {"type": "pad", "action": "menu"})
        texto = await pedir(ws, {"type": "text", "value": "oi"})
        if com_wtype:
            confere(direcional.get("type") != "error", f"com wtype o direcional devia dar ok: {direcional}")
            time.sleep(0.5)
            log = open(env["WTYPE_LOG"]).read() if os.path.exists(env["WTYPE_LOG"]) else ""
            confere("-k Up" in log, f"o wtype devia receber -k Up: {log!r}")
            confere("logo" in log and "m" in log, f"o menu da Mídia devia apertar Super+M: {log!r}")
            confere("oi" in log, f"o texto devia chegar ao wtype: {log!r}")
        else:
            for nome, r in (("direcional", direcional), ("menu", menu), ("texto", texto)):
                confere(r.get("type") == "error" and "wtype" in r.get("message", ""), f"sem wtype o {nome} devia dar erro falando do wtype: {r}")
        await ws.close()
    finally:
        srv.terminate()
        srv.wait()


async def principal():
    await caso(True)
    await caso(False)


asyncio.run(principal())
if falhas:
    sys.exit(1)
print("wtype ok")
