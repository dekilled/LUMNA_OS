#!/usr/bin/env python3
"""O Connect de ponta a ponta: o lumna-connect de verdade, um celular falso, e wl-paste/wl-copy/notify-send de mentira."""
import asyncio, base64, json, os, subprocess, sys, tempfile, time

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
from celular_falso import entrar, pedir, URL as URL_CELULAR  # noqa: E402

CONTROLE = sys.argv[1]
T = tempfile.mkdtemp(prefix="connect-teste-")
run, cfg, home = (os.path.join(T, d) for d in ("run", "cfg", "home"))
for d in (run, cfg, home):
    os.makedirs(d)
os.chmod(run, 0o700)
env = dict(os.environ, XDG_RUNTIME_DIR=run, XDG_CONFIG_HOME=cfg, HOME=home, WAYLAND_DISPLAY="falso-0", CLIP_ARQ=os.path.join(T, "clip"), CLIP_COPIADO=os.path.join(T, "copiado"),
           AVISOS_LOG=os.path.join(T, "avisos.log"), PATH=os.path.join(AQUI, "bin") + ":" + os.environ["PATH"], DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent")
open(env["CLIP_ARQ"], "w").write("texto de antes")
falhas = []


def confere(c, m):
    if not c:
        falhas.append(m)
        print("FALHOU:", m)


def ctl(*args):
    r = subprocess.run([CONTROLE, *args], env=env, capture_output=True, text=True, timeout=15)
    try:
        return json.loads(r.stdout.strip().splitlines()[-1])
    except Exception:
        return {"ok": False, "bruto": r.stdout + r.stderr}


async def espera(cond, seg=8):
    t0 = time.time()
    while time.time() - t0 < seg:
        if cond():
            return True
        await asyncio.sleep(0.1)
    return False


async def principal():
    srv = subprocess.Popen([CONTROLE], env=env, stderr=open(os.path.join(T, "connect.err"), "w"))
    try:
        confere(await espera(lambda: os.path.exists(os.path.join(run, "lumna-connect.sock"))), "o serviço não subiu")
        await asyncio.sleep(0.5)
        # força bruta: com 5 códigos errados o código certo também deixa de valer (é preciso abrir o pareamento de novo)
        import websockets as _ws
        certo = ctl("parear")["codigo"]
        errado = "%06d" % ((int(certo) + 1) % 1000000)
        for _ in range(5):
            async with _ws.connect(URL_CELULAR) as w:
                await w.send(json.dumps({"type": "pair", "code": errado, "name": "Intruso"}))
                r = json.loads(await w.recv())
                confere(r.get("type") == "error", f"código errado devia ser recusado: {r}")
        async with _ws.connect(URL_CELULAR) as w:
            await w.send(json.dumps({"type": "pair", "code": certo, "name": "Intruso"}))
            r = json.loads(await w.recv())
            confere(r.get("type") == "error", f"depois de 5 erros, nem o código certo vale (força bruta): {r}")
        confere(ctl("connect")["pareando"] is False, "o pareamento fecha depois de 5 erros")
        codigo = ctl("parear")["codigo"]
        ws, token = await entrar(codigo=codigo)
        est = ctl("connect")
        d = est["dispositivos"][0]
        confere(len(est["dispositivos"]) == 1 and d["conectado"] and all(d["recursos"].values()), f"o celular devia estar pareado, conectado e com tudo ligado: {est}")
        did = d["id"]
        pasta = os.path.join(T, "recebidos")
        confere(ctl("connect_pasta", pasta)["ok"], "definir a pasta")

        # features
        r = await pedir(ws, {"type": "features"}, "features")
        confere(r.get("features", {}).get("clipboard") is True, f"features: {r}")

        # área de transferência: celular -> PC (e o eco não volta)
        r = await pedir(ws, {"type": "clipboard", "text": "olá do celular ✓"}, "ok")
        confere(r.get("type") == "ok", f"clipboard do celular: {r}")
        confere(await espera(lambda: os.path.exists(env["CLIP_COPIADO"]) and open(env["CLIP_COPIADO"], encoding="utf-8").read() == "olá do celular ✓"), "o texto do celular não foi para a área de transferência do PC")
        open(env["CLIP_ARQ"], "w", encoding="utf-8").write("olá do celular ✓")  # o PC "copia" o que acabou de receber: não pode voltar ao celular
        # PC -> celular
        await asyncio.sleep(0.8)
        open(env["CLIP_ARQ"], "w", encoding="utf-8").write("copiado no PC")
        r = json.loads(await asyncio.wait_for(ws.recv(), 6))
        confere(r.get("type") == "clipboard" and r.get("text") == "copiado no PC", f"o texto do PC devia chegar ao celular (e só ele, sem o eco): {r}")

        # avisos: celular -> PC
        r = await pedir(ws, {"type": "notify", "app": "WhatsApp", "summary": "Ana", "body": "oi!", "urgent": True}, "ok")
        confere(r.get("type") == "ok" and await espera(lambda: os.path.exists(env["AVISOS_LOG"]) and "x-lumna-connect" in open(env["AVISOS_LOG"]).read()), f"o aviso do celular não apareceu no PC (marcado para não voltar): {r}")
        log = open(env["AVISOS_LOG"]).read()
        confere("Celular Falso · WhatsApp" in log and "critical" in log and "oi!" in log, f"o aviso: {log}")
        # PC -> celular
        ctl("aviso", json.dumps({"app": "Firefox", "summary": "Download", "body": "pronto", "urgent": False}))
        r = json.loads(await asyncio.wait_for(ws.recv(), 6))
        confere(r.get("type") == "notification" and r.get("summary") == "Download", f"o aviso do PC devia chegar ao celular: {r}")

        # arquivos: celular -> PC
        conteudo = os.urandom(300_000)
        r = await pedir(ws, {"type": "file-begin", "id": "t1", "name": "../../fotos/a.bin", "size": len(conteudo)}, "ok")
        confere(r.get("type") == "ok", f"file-begin: {r}")
        for i in range(0, len(conteudo), 192 * 1024):
            r = await pedir(ws, {"type": "file-chunk", "id": "t1", "data": base64.b64encode(conteudo[i:i + 192 * 1024]).decode()}, "ok")
            confere(r.get("type") == "ok", f"file-chunk: {r}")
        r = await pedir(ws, {"type": "file-end", "id": "t1"}, "ok")
        recebido = os.path.join(pasta, "a.bin")
        confere(r.get("type") == "ok" and os.path.exists(recebido) and open(recebido, "rb").read() == conteudo, f"o arquivo devia chegar inteiro, com nome seguro (sem ../): {r} {os.listdir(pasta) if os.path.isdir(pasta) else 'sem pasta'}")
        confere(not os.path.exists(os.path.join(T, "fotos")), "o nome com ../ escapou da pasta")
        # um segundo com o mesmo nome não sobrescreve
        await pedir(ws, {"type": "file-begin", "id": "t2", "name": "a.bin", "size": 3}, "ok")
        await pedir(ws, {"type": "file-chunk", "id": "t2", "data": base64.b64encode(b"abc").decode()}, "ok")
        await pedir(ws, {"type": "file-end", "id": "t2"}, "ok")
        confere(os.path.exists(os.path.join(pasta, "a (2).bin")) and open(recebido, "rb").read() == conteudo, "um nome repetido devia virar 'a (2).bin' sem tocar no primeiro")
        # maior que o anunciado: recusa e apaga o parcial
        await pedir(ws, {"type": "file-begin", "id": "t3", "name": "grande.bin", "size": 2}, "ok")
        r = await pedir(ws, {"type": "file-chunk", "id": "t3", "data": base64.b64encode(b"abcdef").decode()}, "ok")
        confere(r.get("type") == "error" and not [f for f in os.listdir(pasta) if "grande" in f], f"o arquivo maior que o anunciado devia ser recusado e sumir: {r} {os.listdir(pasta)}")

        # arquivos: PC -> celular (o celular puxa)
        envio = os.path.join(T, "do_pc.txt")
        dados = os.urandom(250_000)
        open(envio, "wb").write(dados)
        confere(ctl("enviar", did, envio)["ok"], "enviar ao celular")
        oferta = json.loads(await asyncio.wait_for(ws.recv(), 6))
        confere(oferta.get("type") == "file-offer" and oferta.get("name") == "do_pc.txt" and oferta.get("size") == len(dados), f"a oferta: {oferta}")
        baixado, off = b"", 0
        while True:
            r = await pedir(ws, {"type": "file-pull", "id": oferta["id"], "offset": off}, "file-data")
            if r.get("type") != "file-data":
                confere(False, f"file-pull: {r}")
                break
            baixado += base64.b64decode(r["data"])
            off += r["len"]
            if r["eof"]:
                break
        confere(baixado == dados, "o celular devia baixar o arquivo inteiro e igual")
        est = ctl("connect")
        confere(any(t["estado"] == "done" and t["direcao"] == "out" for t in est["transferencias"]) and any(t["direcao"] == "in" and t["estado"] == "done" for t in est["transferencias"]), f"as transferências no estado: {est['transferencias']}")
        confere(not ctl("enviar", did, os.path.join(T, "nao_existe"))["ok"] and not ctl("enviar", did, T)["ok"], "enviar uma pasta ou um arquivo que não existe devia recusar")

        # recursos por celular
        confere(ctl("recurso", did, "clipboard", "nao")["ok"], "desligar a área de transferência")
        r = json.loads(await asyncio.wait_for(ws.recv(), 6))
        confere(r.get("type") == "features" and r["features"]["clipboard"] is False, f"o celular devia ser avisado do recurso desligado: {r}")
        r = await pedir(ws, {"type": "clipboard", "text": "x"}, "ok")
        confere(r.get("type") == "error", f"com a área de transferência desligada devia recusar: {r}")
        confere(not ctl("recurso", did, "inventado", "nao")["ok"], "recurso inventado")
        ctl("recurso", did, "notifications", "nao")
        await asyncio.wait_for(ws.recv(), 6)
        ctl("aviso", json.dumps({"app": "x", "summary": "não vai", "body": "", "urgent": False}))
        try:
            r = await asyncio.wait_for(ws.recv(), 1.5)
            confere(False, f"com os avisos do PC desligados nada devia chegar: {r}")
        except asyncio.TimeoutError:
            pass
        # o interruptor geral
        ctl("connect_ativo", "nao")
        r = await pedir(ws, {"type": "notify", "app": "a", "summary": "b", "body": "c"}, "ok")
        confere(r.get("type") == "error", f"com o Connect desligado nada entra: {r}")
        ctl("connect_ativo", "sim")
        est = ctl("connect")
        confere(est["ativo"] is True and est["dispositivos"][0]["recursos"]["clipboard"] is False, f"os recursos ficam guardados: {est}")
        # sobrevive a reiniciar o serviço
        await ws.close()
    finally:
        srv.terminate()
        srv.wait(timeout=5)


asyncio.run(principal())
print("connect ok" if not falhas else f"connect: {len(falhas)} falha(s)")
sys.exit(1 if falhas else 0)
