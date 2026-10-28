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
open(os.path.join(cfg, "lumna", "emma", "chave"), "w").write("lumna_chavedeteste0123456789\n")  # o token único que o servidor deixa

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


prov_porta = porta + 3000
os.makedirs(os.path.join(cfg, "lumna", "emma"), exist_ok=True)
open(os.path.join(cfg, "lumna", "emma", "provedores.json"), "w").write(json.dumps({
    "provedores": [{"lid": "p1", "nome": "Provedor falso", "preset": "custom", "url": f"http://127.0.0.1:{prov_porta}/v1", "modelos": "modelo-x, modelo-y", "chave": "sk-teste"}],
    "rotas": [{"chave": "chat", "lid": "p1", "modelo": "modelo-x"}], "proximo": 2}))

try:
    sobe(["python3", os.path.join(AQUI, "provedor-falso.py"), str(prov_porta), LOG])
    backend = sobe(["python3", os.path.join(AQUI, "backend-falso.py"), str(porta), LOG, STT])
    sobe(["python3", os.path.join(AQUI, "midia-falso.py"), os.path.join(run, "lumna-midia.sock"), LOG])
    time.sleep(0.8)
    d = sobe([EMMA], env=env, stderr=open(os.path.join(T, "emma.err"), "w"))
    confere(espera(lambda: os.path.exists(os.path.join(run, "lumna-emma.sock"))), "o socket não abriu")
    confere(espera(lambda: pede("estado").get("servidor") is True), "o serviço não entrou no servidor da Emma")

    # os comandos curtos: sem passar pela Emma
    pede("enviar", "pausa")
    confere(espera(lambda: "wtype -k XF86AudioPlay" in log()), "'pausa' não chegou ao player")
    pede("enviar", "volume trinta")
    confere(espera(lambda: "wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ 30%" in log()), "'volume trinta' não mexeu no volume")
    pede("enviar", "por favor, abre a netflix")
    confere(espera(lambda: "midia abrir netflix" in log()), "'abre a netflix' não chegou à Mídia")
    confere("/v1/chat/completions" not in log() and "/chat" not in log(), "um comando curto foi parar na Emma")
    pede("enviar", "avança um minuto")
    confere(espera(lambda: log().count("wtype -k Right") >= 12), "'avança um minuto' não andou 60 s")

    # FASE 1, com o servidor ligado: a conversa vai pelo servidor da Emma (o motor: skills, memória, RAG), com uma ação dela
    pede("enviar", "qual é a capital do Brasil")
    confere(espera(lambda: "/api/sessions/sess-1/chat" in log()), "a pergunta não chegou ao servidor")
    confere("/v1/chat/completions" not in log(), "com o servidor ligado, a conversa não podia ir direto ao provedor")
    r = pede("celular", "me ajuda a abrir a netflix agora")
    confere(r.get("ok") is True and "Netflix" in r.get("resposta", ""), f"a resposta ao celular veio errada: {r}")
    confere(log().count("midia abrir netflix") >= 2, "a Emma não pediu a Netflix pela ferramenta")
    confere('"ok": true' in log().split("tool-result")[-1], "o resultado da ferramenta não voltou ao servidor")
    confere("lumna_tools" in log() and "lumna_midia_abrir" in log(), "o catálogo não foi ao servidor")

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

    confere(pede("versao").get("versao", 0) >= 3, "o serviço não disse a versão")
    # o app completo, com o servidor: REST repassado e conversa numa sessão do servidor
    r = pede("rest", "GET", "/api/sessions/sess-9/messages")
    confere(r.get("ok") is True and r.get("status") == 200 and r.get("corpo") == [], f"o rest não voltou: {r}")
    confere("GET /api/sessions/sess-9/messages" in log(), "o rest não chegou ao servidor")
    confere(pede("rest", "GET", "/fora").get("ok") is False, "o rest aceitou um caminho fora de /api/")
    r = pede("conversar", json.dumps({"sid": "sess-9", "texto": "oi do app", "skill": "sk1"}))
    confere(r.get("ok") is True, f"a conversa do app não começou: {r}")
    confere(espera(lambda: "/api/sessions/sess-9/chat" in log()), "a conversa do app não chegou à sessão dela no servidor")
    confere('"skill_id": "sk1"' in log().split("/api/sessions/sess-9/chat")[-1], "a skill não foi ao servidor")
    c0 = pede("conta")
    confere(c0.get("ok") is True and c0.get("tem_token") is True and "0123456789" not in json.dumps(c0), f"o token do servidor não foi achado (ou vazou inteiro): {c0}")
    confere("/api/auth/login" not in log() and "/api/auth/refresh" not in log(), "o LUMNA tentou fazer login: não pode (trava a conta do harness)")
    confere("auth=Bearer lumna_chavedeteste0123456789" in log(), "o LUMNA não mandou o token do servidor")

    # o Painel: buscar os modelos e testar o provedor (o serviço fala direto com ele, com ou sem servidor)
    url_prov = f"http://127.0.0.1:{prov_porta}/v1"
    m = pede("llm", json.dumps({"acao": "modelos", "url": url_prov, "chave": "sk-teste"}))
    confere(m.get("ok") is True and m.get("models") == ["modelo-x", "modelo-y", "modelo-z"], f"os modelos não vieram: {m}")
    m = pede("llm", json.dumps({"acao": "modelos", "url": url_prov, "chave": "errada"}))
    confere(m.get("ok") is False and "chave" in m.get("error", ""), f"a chave errada devia ser recusada com uma frase clara: {m}")
    t = pede("llm", json.dumps({"acao": "testar", "url": url_prov, "chave": "sk-teste", "modelo": "modelo-x"}))
    confere(t.get("chat") is True and t.get("streaming") is True, f"o teste de capacidades falhou: {t}")

    # FASE 2, com o servidor DESLIGADO: a reserva. A Emma conversa direto com o provedor e guarda no PC
    backend.terminate()
    backend.wait()
    time.sleep(0.3)
    confere(espera(lambda: pede("estado").get("servidor") is False, 12), "o serviço não percebeu que o servidor caiu")
    pede("enviar", "qual é a capital do Brasil")
    confere(espera(lambda: "POST /v1/chat/completions auth=Bearer sk-teste" in log()), "sem o servidor, a pergunta não foi direto ao provedor com a chave")
    confere('"stream": true' in log() or '"stream":true' in log(), "a conversa não pediu resposta em pedaços")
    r = pede("celular", "me ajuda a abrir a netflix agora")
    confere(r.get("ok") is True and "Netflix" in r.get("resposta", ""), f"a resposta direta ao celular veio errada: {r}")
    corpo_final = log().split("POST /v1/chat/completions")[-1]
    confere('"role":"tool"' in corpo_final and "tool_call_id" in corpo_final, "o resultado da ferramenta não voltou ao provedor")
    confere('"type":"function"' in log() and any("lumna_midia_abrir" in seg for seg in log().split("POST /v1/chat/completions")[1:]), "o catálogo de ferramentas não foi ao provedor")
    h = pede("historico")
    confere(any("capital" in m.get("texto", "") for m in h.get("mensagens", [])) and any("Brasília" in m.get("texto", "") for m in h.get("mensagens", [])), f"o histórico da reserva não veio: {h}")

    r = pede("rest", "POST", "/api/sessions", json.dumps({"title": "Minha conversa"}))
    sid = r.get("corpo", {}).get("id", "")
    confere(r.get("ok") is True and sid, f"a conversa não foi criada: {r}")
    confere(pede("rest", "GET", "/api/sessions").get("corpo", [{}])[0].get("title") == "Minha conversa", "a conversa não apareceu na lista")
    confere(pede("rest", "GET", f"/api/sessions/{sid}/messages").get("corpo") == [], "uma conversa nova devia estar vazia")
    confere(pede("rest", "GET", "/fora").get("ok") is False, "o rest aceitou um caminho fora de /api/")
    r = pede("conversar", json.dumps({"sid": sid, "texto": "oi do app"}))
    confere(r.get("ok") is True, f"a conversa do app não começou: {r}")
    msgs = []
    def guardou():
        global msgs
        msgs = pede("rest", "GET", f"/api/sessions/{sid}/messages").get("corpo", [])
        return len(msgs) >= 2
    confere(espera(guardou, 10), "a conversa do app não foi guardada no PC")
    confere(msgs and msgs[0]["role"] == "user" and msgs[0]["content"] == "oi do app" and msgs[1]["role"] == "assistant" and "Brasília" in msgs[1]["content"], f"a conversa guardada veio errada: {msgs}")
    pede("rest", "PATCH", f"/api/sessions/{sid}", json.dumps({"title": "Renomeada"}))
    confere(pede("rest", "GET", "/api/sessions").get("corpo", [{}])[0].get("title") == "Renomeada", "o título não mudou")
    p = pede("rest", "POST", "/api/projects", json.dumps({"name": "Casa", "description": "tudo da casa"}))
    pid = p.get("corpo", {}).get("id", "")
    confere(p.get("ok") is True and pid, f"a rotina não foi criada: {p}")
    mm = pede("rest", "POST", f"/api/projects/{pid}/memories", json.dumps({"kind": "fact", "content": "A sala tem TV."}))
    confere(mm.get("ok") is True, f"a memória não foi guardada: {mm}")
    confere(pede("rest", "GET", f"/api/projects/{pid}/memories").get("corpo", [{}])[0].get("content") == "A sala tem TV.", "a memória não voltou")
    confere(pede("rest", "DELETE", f"/api/memories/{mm['corpo']['id']}").get("ok") is True and pede("rest", "GET", f"/api/projects/{pid}/memories").get("corpo") == [], "a memória não foi esquecida")
    confere(pede("rest", "DELETE", f"/api/projects/{pid}").get("ok") is True and pede("rest", "GET", "/api/projects").get("corpo") == [], "a rotina não foi apagada")
    confere(pede("rest", "DELETE", f"/api/sessions/{sid}").get("ok") is True and pede("rest", "GET", f"/api/sessions/{sid}/messages").get("ok") is False, "a conversa não foi apagada")
    pede("rest", "GET", "/api/sessions/../../x")
    confere(not os.path.exists(os.path.join(T, "x.json")), "o id de conversa saiu da pasta")

    # sem provedor, a Emma diz o que falta (e onde configurar)
    os.rename(os.path.join(cfg, "lumna", "emma", "provedores.json"), os.path.join(cfg, "lumna", "emma", "provedores.json.guardado"))
    r = pede("celular", "me conta uma piada")
    confere(r.get("ok") is False and "provedor" in r.get("erro", "").lower() and "Painel" in r.get("erro", ""), f"sem provedor devia explicar o que fazer: {r}")
    os.rename(os.path.join(cfg, "lumna", "emma", "provedores.json.guardado"), os.path.join(cfg, "lumna", "emma", "provedores.json"))
    # chave errada: a frase diz que a chave foi recusada
    arq = os.path.join(cfg, "lumna", "emma", "provedores.json")
    conf = json.load(open(arq))
    conf["provedores"][0]["chave"] = "sk-errada"
    json.dump(conf, open(arq, "w"))
    r = pede("celular", "me conta uma piada")
    confere(r.get("ok") is False and "chave" in r.get("erro", "").lower(), f"chave errada devia dizer que a chave foi recusada: {r}")
    conf["provedores"][0]["chave"] = "sk-teste"
    json.dump(conf, open(arq, "w"))

    # o catálogo
    a = pede("acoes")
    confere(any(x["nome"] == "midia.abrir" for x in a.get("acoes", [])), "o catálogo de ações veio sem a Mídia")
    confere(any(x["nome"] == "painel.abrir" for x in a.get("acoes", [])), "o catálogo de ações veio sem o painel.abrir")

    # quem assina e fica parado não é derrubado (a topbar e o painel ficam horas sem receber nada)
    assina = socket.socket(socket.AF_UNIX)
    assina.connect(os.path.join(run, "lumna-emma.sock"))
    assina.sendall(b"assinar\n")
    assina.settimeout(2)
    assina.recv(4096)  # o estado de entrada
    time.sleep(4.5)
    pede("enviar", "mudo")
    try:
        visto = b""
        while b"ouvido" not in visto:
            visto += assina.recv(4096)
        confere(True, "")
    except Exception as e:
        confere(False, f"o assinante parado foi derrubado: {e}")
    assina.close()

    # o processo: um só, reinicia e fecha
    segundo = subprocess.run([EMMA], env=env, capture_output=True, text=True, timeout=10)
    confere(segundo.returncode == 0 and "já está rodando" in segundo.stderr, "um segundo serviço não deveria subir")
    confere(pede("estado").get("ok") is True, "o primeiro serviço sumiu quando o segundo tentou subir")
    pede("reiniciar")
    time.sleep(0.6)
    confere(espera(lambda: pede("estado").get("ok") is True, 6), "o serviço não voltou depois de reiniciar")

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
    pede("encerrar")
    confere(espera(lambda: not os.path.exists(os.path.join(run, "lumna-emma.sock")), 5), "o serviço não fechou com 'encerrar'")
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
