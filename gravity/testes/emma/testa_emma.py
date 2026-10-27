#!/usr/bin/env python3
"""lumna-emma de ponta a ponta: backend, Mídia e comandos de mentira; o serviço de verdade."""
import json, os, socket, subprocess, sys, tempfile, time, math, struct

AQUI = os.path.dirname(os.path.abspath(__file__))
EMMA = sys.argv[1]
CONTROLE = sys.argv[2] if len(sys.argv) > 2 else None
T = tempfile.mkdtemp(prefix="emma-teste-")
LOG = os.path.join(T, "log.txt")
STT = os.path.join(T, "stt.txt")
open(LOG, "w").close()
open(STT, "w").write("pausa")
cfg = os.path.join(T, "cfg")
run = os.path.join(T, "run")
os.makedirs(os.path.join(cfg, "lumna", "emma"))
os.makedirs(run, mode=0o700)
open(os.path.join(cfg, "lumna", "emma", "backend.env"), "w").write("ADMIN_EMAIL=eu@lumna.local\nADMIN_PASSWORD=segredo\n")

# a fala: 0,6 s de ruído baixo, 1,6 s de "voz" alta, 1,6 s de silêncio (PCM 16 kHz)
pcm = bytearray()
for i in range(int(16000 * 3.8)):
    t = i / 16000
    v = 30 if t < 0.6 else (7000 * math.sin(2 * math.pi * 220 * t) if t < 2.2 else 30)
    pcm += struct.pack("<h", int(v))
open(os.path.join(T, "fala.raw"), "wb").write(pcm)

porta = 18000 + os.getpid() % 2000
procs = []


def sobe(cmd, **kw):
    p = subprocess.Popen(cmd, **kw)
    procs.append(p)
    return p


env = dict(os.environ, XDG_CONFIG_HOME=cfg, XDG_RUNTIME_DIR=run, HOME=T, EMMA_TESTE_LOG=LOG,
           LUMNA_EMMA_URL=f"http://127.0.0.1:{porta}", LUMNA_GRAVADOR=f"cat {T}/fala.raw", LUMNA_TOCADOR="true",
           PATH=os.path.join(AQUI, "bin") + ":" + os.environ["PATH"], DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent")
env.pop("WAYLAND_DISPLAY", None)
env.pop("ORBIT_SOCK", None)
falhas = []


def confere(cond, msg):
    if not cond:
        falhas.append(msg)
        print("FALHOU:", msg)


def log():
    return open(LOG).read()


def espera(cond, seg=8):
    t = time.time()
    while time.time() - t < seg:
        if cond():
            return True
        time.sleep(0.1)
    return False


def pede(*args):
    r = subprocess.run([EMMA, *args], env=env, capture_output=True, text=True, timeout=30)
    try:
        return json.loads(r.stdout.strip().splitlines()[-1])
    except Exception:
        return {"ok": False, "bruto": r.stdout + r.stderr}


try:
    sobe(["python3", os.path.join(AQUI, "backend-falso.py"), str(porta), LOG, STT])
    sobe(["python3", os.path.join(AQUI, "midia-falso.py"), os.path.join(run, "lumna-midia.sock"), LOG])
    time.sleep(0.8)
    d = sobe([EMMA], env=env, stderr=open(os.path.join(T, "emma.err"), "w"))
    confere(espera(lambda: os.path.exists(os.path.join(run, "lumna-emma.sock"))), "o socket não abriu")
    confere(espera(lambda: pede("estado").get("online") is True), "o serviço não achou o backend")

    # os comandos curtos: sem passar pela Emma
    pede("enviar", "pausa")
    confere(espera(lambda: "wtype -k XF86AudioPlay" in log()), "'pausa' não chegou ao player")
    pede("enviar", "volume trinta")
    confere(espera(lambda: "wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ 30%" in log()), "'volume trinta' não mexeu no volume")
    pede("enviar", "por favor, abre a netflix")
    confere(espera(lambda: "midia abrir netflix" in log()), "'abre a netflix' não chegou à Mídia")
    confere("/chat" not in log(), "um comando curto foi parar na Emma")
    pede("enviar", "avança um minuto")
    confere(espera(lambda: log().count("wtype -k Right") >= 12), "'avança um minuto' não andou 60 s")

    # uma conversa com a Emma (e uma ação dela)
    pede("enviar", "qual é a capital do Brasil")
    confere(espera(lambda: "/api/sessions/sess-1/chat" in log()), "a pergunta não chegou ao backend")
    r = pede("celular", "me ajuda a abrir a netflix agora")
    confere(r.get("ok") is True and "Netflix" in r.get("resposta", ""), f"a resposta ao celular veio errada: {r}")
    confere(log().count("midia abrir netflix") >= 2, "a Emma não pediu a Netflix pela ferramenta")
    confere('"ok": true' in log().split("tool-result")[-1], "o resultado da ferramenta não voltou ao backend")
    confere("lumna_tools" in log() and "lumna_midia_abrir" in log(), "o catálogo não foi ao backend")

    # o microfone: grava, transcreve ("pausa") e age
    antes = log().count("wtype -k XF86AudioPlay")
    pede("ouvir")
    confere(espera(lambda: "/api/stt" in log(), 12), "o áudio não chegou ao /api/stt")
    confere(espera(lambda: log().count("wtype -k XF86AudioPlay") > antes, 8), "a fala 'pausa' não virou ação")

    # uma ação desligada no Painel
    open(os.path.join(cfg, "lumna", "emma.json"), "w").write(json.dumps({"permissions": {"audio.volume": "off"}}))
    n = log().count("30%")
    r = pede("celular", "volume quarenta")
    confere(r.get("ok") is False and "desligada" in r.get("resposta", ""), f"a ação desligada rodou: {r}")
    confere(log().count("40%") == 0, "o volume mudou com a ação desligada")

    # o histórico do painel e o contexto
    h = pede("historico")
    confere(h.get("ok") is True, f"o histórico não veio: {h}")
    c = pede("contexto")
    confere(c.get("ok") is True and c.get("ativo") is True, f"o contexto não veio: {c}")
    confere(pede("contexto", "nao").get("ativo") is False, "o contexto não desligou")
    confere(pede("contexto", "sim").get("ativo") is True, "o contexto não religou")
    r = pede("celular", "como adiciono outro layout")
    confere(r.get("ok") is True and "layout" in r.get("resposta", "").lower(), f"a resposta com cartão não veio: {r}")

    # o catálogo
    a = pede("acoes")
    confere(any(x["nome"] == "midia.abrir" for x in a.get("acoes", [])), "o catálogo de ações veio sem a Mídia")
    confere(any(x["nome"] == "painel.abrir" for x in a.get("acoes", [])), "o catálogo de ações veio sem o painel.abrir")

    # o celular: a fala que o app Controle transcreveu chega à Emma pelo lumna-controle
    if CONTROLE:
        os.remove(os.path.join(cfg, "lumna", "emma.json"))  # as permissões voltam ao padrão
        import asyncio, websockets

        sobe([CONTROLE], env=env, stderr=subprocess.DEVNULL)
        confere(espera(lambda: os.path.exists(os.path.join(run, "lumna-controle.sock"))), "o lumna-controle não abriu o socket")
        cod = subprocess.run([CONTROLE, "parear"], env=env, capture_output=True, text=True).stdout
        codigo = json.loads(cod)["codigo"]

        async def celular():
            async with websockets.connect("ws://127.0.0.1:47800/") as ws:
                await ws.send(json.dumps({"type": "pair", "code": codigo, "name": "Celular de teste"}))
                par = json.loads(await ws.recv())
                assert par["type"] == "paired", par
                await ws.send(json.dumps({"type": "voice", "text": "volume cinquenta"}))
                visto = []
                for _ in range(4):
                    m = json.loads(await asyncio.wait_for(ws.recv(), 10))
                    visto.append(m)
                    if m.get("type") == "voice_reply":
                        return m
                return {"visto": visto}

        r = asyncio.run(celular())
        confere(r.get("type") == "voice_reply" and r.get("ok") and "Volume" in r.get("text", ""), f"a voz do celular não voltou: {r}")
        confere("50%" in log(), "o volume do celular não foi ajustado")
finally:
    for p in procs:
        p.terminate()
    for p in procs:
        try:
            p.wait(timeout=3)
        except Exception:
            p.kill()
if falhas:
    print(log())
    print(open(os.path.join(T, "emma.err")).read())
    sys.exit(1)
print("emma ok")
