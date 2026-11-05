#!/usr/bin/env python3
"""Um backend da Emma de mentira: o bastante para o serviço lumna-emma conversar, ouvir e falar.

  python3 backend-falso.py PORTA ARQUIVO_DE_LOG [ARQUIVO_DO_STT]

Cada pedido que chega vai para o ARQUIVO_DE_LOG, uma linha "METODO CAMINHO CORPO". O /api/stt devolve o
texto que estiver em ARQUIVO_DO_STT. No chat: "netflix" faz a Emma chamar lumna_midia_abrir e esperar o
resultado; "volume" chama lumna_audio_volume; qualquer outra coisa é uma resposta em duas frases.
"""
import json, time, os, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORTA, LOG = int(sys.argv[1]), sys.argv[2]
STT = sys.argv[3] if len(sys.argv) > 3 else None
resultados = {}
travas = threading.Lock()


def registrar(linha):
    with travas:
        with open(LOG, "a") as f:
            f.write(linha + "\n")


TOKEN = "lumna_chavedeteste0123456789"
PROVS = []
ROUTING = {}
SENHA = {"ok": "segredo"}


def prov_publico(p):
    k = p.get("api_key", "")
    return {**{x: p[x] for x in ("id", "name", "preset", "base_url", "models")}, "has_key": bool(k), "api_key_masked": f"••••{k[-4:]}" if k else ""}


PERFIL = {"nsfw": False}
ANEXOS = {}
DECISOES = {}
WAKE = {"count": 0}


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def corpo(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def json(self, obj, status=200):
        b = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def sem_token(self):
        """Como o servidor de verdade: sem o token, 401 (o LUMNA não faz login)."""
        if self.path in ("/api/", "/api/auth/login", "/api/auth/refresh"):
            return False
        if self.headers.get("Authorization", "") != f"Bearer {TOKEN}":
            self.json({"detail": "Não autenticado"}, 401)
            return True
        return False

    def do_GET(self):
        registrar(f"GET {self.path} auth={self.headers.get('Authorization', '')}")
        if self.sem_token():
            return
        if self.path == "/api/providers":
            return self.json([prov_publico(p) for p in PROVS])
        if self.path == "/api/profile":
            return self.json({"nsfw": PERFIL["nsfw"], "on_start": False})
        if self.path == "/api/wake":
            return self.json({"enabled": True, "count": WAKE["count"], "running": True})
        if self.path.startswith("/api/wake/wait"):
            flag = os.environ.get("EMMA_TESTE_CASA", "/tmp") + "/oi-emma"
            for _ in range(40):  # espera até 2 s, como o long poll do servidor de verdade
                if os.path.exists(flag):
                    os.unlink(flag)
                    WAKE["count"] += 1
                    return self.json({"count": WAKE["count"], "heard": True})
                time.sleep(0.05)
            return self.json({"count": WAKE["count"], "heard": False})
        if self.path.startswith("/api/attachments/") and self.path[17:] in ANEXOS:
            b = ANEXOS[self.path[17:]]
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            return self.wfile.write(b)
        if self.path == "/api/settings":
            # "Ler as respostas em voz alta" (Painel > Emma > Voz): o teste liga criando o arquivo
            auto = bool(os.environ.get("EMMA_AUTO_READ_FILE")) and os.path.exists(os.environ["EMMA_AUTO_READ_FILE"])
            return self.json({"routing": ROUTING, "voice": {"auto_read": auto}})
        if self.path.startswith("/api/providers/") and self.path.endswith("/models"):
            return self.json({"ok": True, "models": ["modelo-x", "modelo-y", "modelo-z"]})
        if self.path == "/api/sessions/sess-a/messages":
            return self.json([
                {"id": "m1", "role": "user", "content": "como adiciono outro layout de teclado", "created_at": "2026-10-27T14:02:00+00:00", "tools": []},
                {"id": "m2", "role": "assistant", "content": "Dá pra fazer no Painel, em Teclado e mouse.\n[acao: Abrir configuração]\n[sugestao: E o atalho para trocar?]", "created_at": "2026-10-27T14:02:05+00:00",
                 "tools": [{"name": "lumna_painel_abrir", "args": {"secao": "teclado/teclado"}, "status": "ok", "summary": "Abri o Painel em Layouts de teclado."}]},
                {"id": "m3", "role": "user", "content": "construir", "marker": "build", "created_at": "2026-10-27T14:03:00+00:00", "tools": []},
            ])
        if self.path.startswith("/api/sessions/"):
            return self.json([])
        if self.path.startswith("/api/sessions"):
            if "project_id=p1" in self.path:
                return self.json([{"id": "sess-p", "title": "Backup semanal"}])
            return self.json([{"id": "sess-a", "title": "Layout de teclado"}, {"id": "sess-b", "title": "Cinema na TV"}])
        if self.path == "/api/projects":
            return self.json([{"id": "p1", "name": "LUMNA OS", "description": "O sistema operacional que estamos construindo"}, {"id": "p2", "name": "Casa", "description": ""}])
        if self.path == "/api/projects/p1/memories":
            return self.json([{"id": "k1", "kind": "preference", "content": "Gosta de respostas curtas e com humor."}, {"id": "k2", "kind": "decision", "content": "Build e Workspace foram para o LUMNA IDE."}])
        if self.path == "/api/skills":
            return self.json([{"id": "sk1", "name": "Resumir", "description": "Resume um texto em tópicos.", "icon": "Sparkles"}, {"id": "sk2", "name": "Revisar texto", "description": "Corrige e melhora a escrita."}])
        self.json({"ok": True})

    def do_PATCH(self):
        dados = self.corpo()
        registrar(f"PATCH {self.path} {dados.decode()[:300]}")
        if self.sem_token():
            return
        if self.path.startswith("/api/providers/"):
            c = json.loads(dados or b"{}")
            for p in PROVS:
                if p["id"] == self.path.rsplit("/", 1)[1]:
                    chave = c.pop("api_key", None)
                    p.update(c)
                    if chave:
                        p["api_key"] = chave
                    return self.json(prov_publico(p))
        self.json({})

    def do_PUT(self):
        dados = self.corpo()
        registrar(f"PUT {self.path} {dados.decode()[:600]}")
        if self.sem_token():
            return
        if self.path == "/api/settings":
            ROUTING.clear()
            ROUTING.update(json.loads(dados or b"{}").get("routing", {}))
            return self.json({"routing": ROUTING})
        self.json({})

    def do_DELETE(self):
        registrar(f"DELETE {self.path}")
        if self.sem_token():
            return
        if self.path.startswith("/api/providers/"):
            pid = self.path.rsplit("/", 1)[1]
            PROVS[:] = [p for p in PROVS if p["id"] != pid]
        self.json({"ok": True})

    def do_POST(self):
        dados = self.corpo()
        if self.path == "/api/attachments":  # o upload de uma imagem (multipart): guarda os bytes depois do cabeçalho da parte
            registrar(f"POST {self.path} bytes={len(dados)}")
            if self.headers.get("Authorization", "") != f"Bearer {TOKEN}":
                return self.json({"detail": "Não autenticado"}, 401)
            corpo = dados.split(b"\r\n\r\n", 1)[-1].rsplit(b"\r\n--", 1)[0]
            ANEXOS["att-1"] = corpo
            return self.json({"id": "att-1", "mime": "image/png", "name": "foto.png"})
        if self.path.endswith("/stt"):
            registrar(f"POST {self.path} audio={len(dados)}")
            texto = open(STT).read().strip() if STT else ""
            return self.json({"text": texto})
        if self.path.endswith("/tts"):
            registrar(f"POST {self.path} {dados.decode()}")
            if b"falha-tts" in dados:
                self.send_response(502)
                self.send_header("Content-Type", "application/json")
                corpo = json.dumps({"detail": "Configure a chave do Fish Audio em Configurações → Voz"}).encode()
                self.send_header("Content-Length", str(len(corpo)))
                self.end_headers()
                return self.wfile.write(corpo)
            b = b"RIFF0000WAVE" + json.loads(dados or b"{}").get("text", "fake").encode()  # o áudio de mentira é o próprio texto: dá para conferir o que tocou
            self.send_response(200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            return self.wfile.write(b)
        try:
            c = json.loads(dados or b"{}")
        except Exception:
            c = {}
        registrar(f"POST {self.path} {json.dumps(c, ensure_ascii=False)[:40000]}")
        if self.sem_token():
            return
        if self.path == "/api/profile/boot":
            return self.json({"nsfw": PERFIL["nsfw"], "on_start": False})
        if self.path == "/api/profile":  # o servidor de verdade só deixa entrar no NSFW com senha ou trusted (o PIN conferido pelo LUMNA)
            if c.get("nsfw") and not c.get("trusted"):
                return self.json({"detail": "Senha incorreta"}, 403)
            PERFIL["nsfw"] = bool(c.get("nsfw"))
            return self.json({"nsfw": PERFIL["nsfw"], "on_start": False})
        if self.path == "/api/auth/login":
            return self.json({"access_token": "tok1", "refresh_token": "ref1"})
        if self.path == "/api/auth/refresh":
            return self.json({"access_token": "tok2"})
        if self.path == "/api/providers":
            p = {"id": f"prov{len(PROVS) + 1}", "name": c.get("name", ""), "preset": c.get("preset", "custom"), "base_url": c.get("base_url", ""),
                 "models": c.get("models", []), "api_key": c.get("api_key", "")}
            PROVS.append(p)
            return self.json(prov_publico(p))
        if self.path.startswith("/api/providers/") and self.path.endswith("/test"):
            return self.json({"model": c.get("model"), "chat": True, "latency_ms": 321, "reply": "ok", "streaming": True, "json_mode": False})
        if self.path == "/api/sessions":
            return self.json({"id": "sess-1"})
        if self.path.endswith("/image"):  # POST /api/sessions/ID/image: a imagem gerada vem como mensagem da Emma
            return self.json({"user_message": {"id": "m-img-1", "role": "user", "content": "/imagem " + c.get("prompt", "")},
                              "message": {"id": "m-img-2", "role": "assistant", "content": "", "attachments": ["att-1"]}})
        if self.path.endswith("/tool-result"):
            resultados[c.get("call_id")] = c
            return self.json({})
        if self.path.endswith("/tool-decision"):
            DECISOES[c.get("call_id")] = bool(c.get("allow"))
            return self.json({})
        if self.path.endswith("/chat"):
            return self.chat(c)
        self.json({"detail": "?"}, 404)

    def sse(self, ev):
        self.wfile.write(b"data: " + json.dumps(ev, ensure_ascii=False).encode() + b"\n\n")
        self.wfile.flush()

    def chat(self, c):
        # o servidor de verdade valida o que o shell manda (routes_chat.LumnaToolDef): descrição com mais de 800 letras dá 422 em TODA conversa
        for ferr in c.get("lumna_tools", []):
            if len(ferr.get("description", "")) > 800:
                registrar(f"422 descricao longa demais: {ferr.get('name')}")
                corpo = json.dumps({"detail": [{"loc": ["body", "lumna_tools"], "msg": "String should have at most 800 characters"}]}).encode()
                self.send_response(422)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(corpo)))
                self.end_headers()
                return self.wfile.write(corpo)
        texto = c.get("content", "").lower()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "close")
        self.end_headers()
        ferramenta = None
        fora = os.environ.get("EMMA_TESTE_FORA", "/tmp/emma-fora.txt")
        casa = os.environ.get("EMMA_TESTE_CASA", "/tmp")
        if "cria pasta" in texto:
            ferramenta = ("lumna_arquivos_criar_pasta", {"caminho": f"{casa}/Downloads/Prints"}, "Criei a pasta.")
        elif "organiza" in texto:
            ferramenta = ("lumna_arquivos_mover", {"movimentos": [{"de": f"{casa}/Downloads/a.png", "para": f"{casa}/Downloads/Prints"},
                                                                   {"de": f"{casa}/Downloads/b.png", "para": f"{casa}/Downloads/Prints"},
                                                                   {"de": f"{casa}/Downloads/c.png", "para": f"{casa}/Downloads/Prints"}], "motivo": "organizar os prints"}, "Organizei.")
        elif "escreve historia" in texto:
            ferramenta = ("lumna_arquivos_escrever", {"caminho": "Histórias/conto.md", "conteudo": "Era uma vez a Emma.\n", "motivo": "guardar a história"}, "Guardei a história.")
        elif "escreve fora" in texto:
            ferramenta = ("lumna_arquivos_escrever", {"caminho": fora, "conteudo": "conteudo-de-fora\n", "motivo": "teste de aprovação"}, "Escrevi lá fora.")
        elif "roda comando" in texto:
            ferramenta = ("lumna_comando_executar", {"comando": "echo ola-do-comando", "motivo": "testar o comando"}, "Rodei o comando.")
        elif "le segredo" in texto:
            ferramenta = ("lumna_arquivos_ler", {"caminho": "~/.config/lumna/emma/chave"}, "Tentei ler.")
        elif "netflix" in texto:
            ferramenta = ("lumna_midia_abrir", {"servico": "netflix"}, "Abri a Netflix. Bom filme!")
        elif "config da emma" in texto:  # o modelo escreve a seção do jeito dele: maiúsculas, acento, espaços
            ferramenta = ("lumna_painel_abrir", {"secao": "Emma / Configurações"}, "Abri as configurações dela.")
        elif "layout" in texto:
            ferramenta = ("lumna_painel_abrir", {"secao": "teclado/teclado"},
                          "Encontrei essa opção nas configurações. Qual layout você quer usar?\n[acao: Adicionar layout]\n[sugestao: Português (Brasil)]\n[sugestao: Inglês (EUA)]")
        elif "volume" in texto:
            ferramenta = ("lumna_audio_volume", {"percentual": 35}, "Volume em 35 por cento.")
        if "usa mcp" in texto:  # uma ferramenta DO SERVIDOR (como as de um servidor MCP) pede licença: o servidor espera a decisão
            self.sse({"type": "tool_confirm", "tool": {"id": "call-mcp", "name": "mcp_falso__ler", "args": {"q": "abc"}}, "label": "Falso: ler",
                      "reason": "esta ferramenta pede confirmação"})
            for _ in range(160):
                if "call-mcp" in DECISOES:
                    break
                time.sleep(0.05)
            ok = DECISOES.pop("call-mcp", False)
            partes = ["Usei o MCP." if ok else "Não usei o MCP (você não deixou)."]
            ferramenta = None
        elif ferramenta:
            nome, args, final = ferramenta
            self.sse({"type": "tool_client", "tool": {"id": "call-1", "name": nome, "args": args}})
            for _ in range(100):
                if "call-1" in resultados:
                    break
                time.sleep(0.05)
            resultados.pop("call-1", None)
            partes = [final]
        elif "semprovedor" in texto:
            self.sse({"type": "error", "message": "Nenhum provedor configurado. Adicione um provedor e uma chave de API no Painel."})
            self.close_connection = True
            return
        elif "markdown" in texto or "troca esta tela" in texto:
            partes = ["# Um exemplo\n\nAqui vai **negrito**, *itálico*, `codigo inline` e um [link](http://x.y).\n\n",
                      "## Passos\n\n- primeiro item\n- segundo item com **ênfase**\n  - um sub-item\n\n1. abrir o Painel\n2. escolher o teclado\n\n",
                      "> Uma citação importante\n> em duas linhas\n\n```c\nint main(void) {\n    printf(\"oi\\n\");\n    return 0;\n}\n```\n\n",
                      "| Modo | Tela |\n|---|---|\n| Mídia | TV |\n| Desktop | Monitor |\n"]
        else:
            partes = ["Brasília é a capital do Brasil. ", "Fica no Planalto Central."]
        total = ""
        for p in partes:
            self.sse({"type": "delta", "content": p})
            total += p
            time.sleep(0.05)
        self.sse({"type": "done", "message": {"content": total}})
        self.close_connection = True


ThreadingHTTPServer(("127.0.0.1", PORTA), H).serve_forever()
