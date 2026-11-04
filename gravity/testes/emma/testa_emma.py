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


os.environ["EMMA_TESTE_CASA"] = T  # o HOME do teste
os.environ["EMMA_TESTE_FORA"] = os.path.join(tempfile.gettempdir(), f"emma-fora-{os.getpid()}.txt")  # um arquivo FORA da pasta pessoal (o HOME do teste é T)
env = dict(os.environ, LUMNA_EMMA_ESPERA_TESTE="3", LUMNA_EMMA_SERVIDOR="off", XDG_CONFIG_HOME=cfg, XDG_RUNTIME_DIR=run, HOME=T, EMMA_TESTE_LOG=LOG,
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

    # "Oi Emma": o servidor avisa que ouviu o nome; o serviço abre a popup e passa a gravar (a fala gravada vira "pausa")
    antes_stt = log().count("/api/stt")
    open(os.path.join(T, "oi-emma"), "w").close()
    confere(espera(lambda: "popup aberta" in log(), 10), "o 'Oi Emma' não abriu a popup")
    confere(espera(lambda: log().count("/api/stt") > antes_stt, 12), "o 'Oi Emma' não ligou o microfone")

    # o perfil SFW/NSFW: o NSFW só abre com o PIN do bloqueio (conferido aqui, no serviço); voltar ao SFW é livre
    import hashlib
    confere(pede("perfil").get("nsfw") is False, "o serviço devia abrir no SFW")
    r = pede("perfil", "nsfw", "1234")  # sem PIN cadastrado vale a senha do Linux: "1234" não é
    confere(r.get("ok") is False and "incorret" in r.get("erro", ""), f"uma senha errada devia recusar: {r}")
    sal = "00112233445566778899aabbccddeeff"
    h = hashlib.sha256(f"{sal}:2468".encode()).digest()
    for _ in range(120000):
        h = hashlib.sha256(h).digest()
    os.makedirs(os.path.join(cfg, "lumna"), exist_ok=True)
    open(os.path.join(cfg, "lumna", "mobile-pin"), "w").write(f"4:{sal}:{h.hex()}")
    r = pede("perfil", "nsfw", "1111")
    confere(r.get("ok") is False and "incorreto" in r.get("erro", ""), f"PIN errado devia recusar: {r}")
    confere(pede("perfil").get("nsfw") is False, "PIN errado não podia mudar o perfil")
    confere('"nsfw": true' not in log(), "PIN errado chegou ao servidor")
    r = pede("perfil", "nsfw", "2468")
    confere(r.get("ok") is True and r.get("nsfw") is True, f"PIN certo devia entrar no NSFW: {r}")
    confere('"trusted": true' in log(), "o servidor não soube que o PIN foi conferido")
    r = pede("perfil")
    confere(r.get("nsfw") is True, f"o perfil devia ficar NSFW: {r}")
    r = pede("perfil", "sfw")
    confere(r.get("ok") is True and r.get("nsfw") is False, f"voltar ao SFW devia ser livre: {r}")

    # gerar imagem: o pedido demorado passa pelo serviço (sessão longa) até o servidor
    r = pede("rest", "POST", "/api/sessions/sess-1/image", json.dumps({"prompt": "um gato"}))
    confere(r.get("ok") is True, f"o pedido de imagem devia chegar ao servidor: {r}")
    confere(espera(lambda: "/api/sessions/sess-1/image" in log()), "o servidor não recebeu o pedido de imagem")

    # imagens: sobe um arquivo, baixa para o cache, e a mensagem leva o id do anexo ao servidor
    import base64
    png = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")
    foto = os.path.join(T, "foto.png")
    open(foto, "wb").write(png)
    r = pede("anexar", foto)
    confere(r.get("ok") is True and r.get("id") == "att-1", f"anexar devia devolver o id: {r}")
    confere(pede("anexar", os.path.join(T, "fala.raw")).get("ok") is False, "um arquivo que não é imagem foi aceito")
    r = pede("baixar", "att-1")
    confere(r.get("ok") is True and os.path.exists(r.get("caminho", "")) and open(r["caminho"], "rb").read() == png, f"baixar devia trazer a imagem: {r}")
    confere(pede("baixar", "../etc").get("ok") is False, "um id com barras foi aceito")
    r = pede("conversar", json.dumps({"sid": "sess-1", "texto": "", "anexos": ["att-1"]}))
    confere(r.get("ok") is True, f"uma mensagem só com imagem devia ser aceita: {r}")
    confere(espera(lambda: '"attachments": ["att-1"]' in log()), "o chat não levou os anexos ao servidor")

    # arquivos e comandos: a política de cada chamada (direto, com aprovação, recusado)
    import threading
    home = T

    def conversa_aprovando(texto, decisao):
        ass = socket.socket(socket.AF_UNIX)
        ass.connect(os.path.join(run, "lumna-emma.sock"))
        ass.sendall(b"assinar\n")
        ass.settimeout(1)
        res = {}
        th = threading.Thread(target=lambda: res.update(pede("celular", texto)))
        th.start()
        visto, confirmar, t0 = b"", None, time.time()
        while time.time() - t0 < 8 and not confirmar and th.is_alive():
            try:
                visto += ass.recv(65536)
            except socket.timeout:
                pass
            for linha in visto.decode(errors="ignore").splitlines():
                if linha.startswith("{") and '"evento":"confirmar"' in linha:
                    try:
                        confirmar = json.loads(linha)
                    except ValueError:
                        pass
        if confirmar and decisao:
            pede("confirmar", confirmar["id"], decisao)
        th.join(20)
        ass.close()
        return res, confirmar

    # uma ferramenta DO SERVIDOR (um servidor MCP) pede licença: o cartão aparece aqui e a decisão volta ao servidor, que é quem roda
    r, conf = conversa_aprovando("usa mcp agora", "sim")
    confere(conf is not None and "Falso: ler" in conf.get("nome", "") and '"q"' in conf.get("detalhe", ""), f"o cartão da ferramenta do servidor não apareceu com os argumentos: {conf}")
    confere("Usei o MCP" in r.get("resposta", ""), f"aprovar devia deixar o servidor usar a ferramenta: {r}")
    r, conf = conversa_aprovando("usa mcp de novo", "nao")
    confere(conf is not None and "não usei" in r.get("resposta", "").lower(), f"negar devia chegar ao servidor: {r}")

    # escrever na pasta dela: direto, sem pedir
    r, conf = conversa_aprovando("escreve historia do dragao", None)
    historia = os.path.join(home, "Emma", "Histórias", "conto.md")
    confere(conf is None, f"escrever na pasta da Emma não devia pedir aprovação: {conf}")
    confere(os.path.exists(historia) and open(historia).read() == "Era uma vez a Emma.\n", "a história não foi guardada em ~/Emma/Histórias")
    # ler um segredo: recusa direta, sem nem perguntar
    r, conf = conversa_aprovando("le segredo", None)
    confere(conf is None, "ler a chave do LUMNA não devia nem pedir aprovação (é recusado)")
    confere('não mexe' in log().split("tool-result")[-1] or "A Emma não mexe" in log(), "a recusa não voltou ao servidor")
    # escrever fora da pasta pessoal: pede, mostra tudo, e só escreve se deixarem
    fora = os.environ["EMMA_TESTE_FORA"]
    r, conf = conversa_aprovando("escreve fora", "nao")
    confere(conf is not None, "escrever fora da pasta pessoal devia pedir aprovação")
    if conf:
        d = conf.get("detalhe", "")
        confere(fora in d and "conteudo-de-fora" in d and "teste de aprovação" in d and "fora da sua pasta pessoal" in d,
                f"o cartão não mostra o quê, onde, por quê e o conteúdo: {d!r}")
    confere(not os.path.exists(fora), "escreveu fora mesmo sem a aprovação")
    r, conf = conversa_aprovando("escreve fora", "sim")
    confere(os.path.exists(fora) and open(fora).read() == "conteudo-de-fora\n", "não escreveu fora depois da aprovação")
    # comando: sempre pede, mostra o comando, e devolve a saída
    r, conf = conversa_aprovando("roda comando", "sim")
    confere(conf is not None and "$ echo ola-do-comando" in conf.get("detalhe", ""), f"o cartão do comando não mostra o comando: {conf}")
    confere("ola-do-comando" in log().split("tool-result")[-1], "a saída do comando não voltou ao servidor")
    antes_cmd = log().count("ola-do-comando")
    r, conf = conversa_aprovando("roda comando", "nao")
    confere(conf is not None and "deixou" in log().split("tool-result")[-1], "negar o comando devia voltar 'você não deixou' ao servidor")
    confere(log().split("tool-result")[-1].count("ola-do-comando") == 0 and log().count("ola-do-comando") == antes_cmd,
            "o comando rodou mesmo sem a aprovação")
    if os.path.exists(fora):
        os.unlink(fora)

    # criar pasta: direto, sem cartão. Mover: UM cartão com a lista inteira, e o que já existe no destino é pulado.
    os.makedirs(os.path.join(home, "Downloads"), exist_ok=True)
    for nome in ("a.png", "b.png", "c.png"):
        open(os.path.join(home, "Downloads", nome), "w").write(nome)
    r, conf = conversa_aprovando("cria pasta dos prints", None)
    confere(conf is None, f"criar uma pasta na pasta pessoal não devia pedir aprovação: {conf}")
    confere(os.path.isdir(os.path.join(home, "Downloads", "Prints")), "a pasta não foi criada")
    open(os.path.join(home, "Downloads", "Prints", "c.png"), "w").write("ja estava la")  # um destino que já existe
    r, conf = conversa_aprovando("organiza os prints", "sim")
    confere(conf is not None, "mover arquivos devia pedir aprovação")
    if conf:
        d = conf.get("detalhe", "")
        confere("Mover 3 itens" in d and "a.png" in d and "b.png" in d and "c.png" in d and "Prints" in d and "organizar os prints" in d,
                f"o cartão não lista todos os movimentos: {d!r}")
    confere(os.path.exists(os.path.join(home, "Downloads", "Prints", "a.png")) and not os.path.exists(os.path.join(home, "Downloads", "a.png")), "a.png não foi movido")
    confere(os.path.exists(os.path.join(home, "Downloads", "Prints", "b.png")) and not os.path.exists(os.path.join(home, "Downloads", "b.png")), "b.png não foi movido")
    confere(open(os.path.join(home, "Downloads", "Prints", "c.png")).read() == "ja estava la" and os.path.exists(os.path.join(home, "Downloads", "c.png")),
            "c.png foi sobrescrito (ou sumiu): um destino que existe tem de ser pulado")

    # uma aprovação que ninguém responde expira: o cartão some, a ação é negada e a conversa SEGUE (não fica "pensando" para sempre)
    r, conf = conversa_aprovando("escreve fora", None)
    confere(conf is not None, "o pedido de aprovação não chegou")
    confere(r.get("ok") is not None and "Escrevi" in str(r.get("resposta", "")) or r.get("ok") is True, f"a conversa não seguiu depois que a aprovação expirou: {r}")
    confere(not os.path.exists(fora), "escreveu fora sem ninguém aprovar")
    confere(pede("estado").get("estado") == "ocioso", "o serviço continuou 'pensando' depois que a aprovação expirou")

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
    # apagar uma mensagem (o menu da bolha): some da conversa, a outra fica; um id que não existe dá erro
    apagada = pede("rest", "DELETE", f"/api/sessions/{sid}/messages/{msgs[1]['id']}")
    confere(apagada.get("ok") is True, f"a mensagem não foi apagada: {apagada}")
    restantes = pede("rest", "GET", f"/api/sessions/{sid}/messages").get("corpo", [])
    confere(len(restantes) == len(msgs) - 1 and all(m["id"] != msgs[1]["id"] for m in restantes), f"a conversa não perdeu só aquela mensagem: {restantes}")
    confere(pede("rest", "DELETE", f"/api/sessions/{sid}/messages/naoexiste").get("ok") is False, "apagar uma mensagem que não existe devia dar erro")
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
