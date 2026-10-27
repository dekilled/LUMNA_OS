#!/usr/bin/env python3
"""Um backend da Emma de mentira: o bastante para o serviço lumna-emma conversar, ouvir e falar.

  python3 backend-falso.py PORTA ARQUIVO_DE_LOG [ARQUIVO_DO_STT]

Cada pedido que chega vai para o ARQUIVO_DE_LOG, uma linha "METODO CAMINHO CORPO". O /api/stt devolve o
texto que estiver em ARQUIVO_DO_STT. No chat: "netflix" faz a Emma chamar lumna_midia_abrir e esperar o
resultado; "volume" chama lumna_audio_volume; qualquer outra coisa é uma resposta em duas frases.
"""
import json, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORTA, LOG = int(sys.argv[1]), sys.argv[2]
STT = sys.argv[3] if len(sys.argv) > 3 else None
resultados = {}
travas = threading.Lock()


def registrar(linha):
    with travas:
        with open(LOG, "a") as f:
            f.write(linha + "\n")


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

    def do_GET(self):
        registrar(f"GET {self.path}")
        if self.path.startswith("/api/sessions/"):
            return self.json([])
        self.json({"ok": True})

    def do_PATCH(self):
        self.corpo()
        registrar(f"PATCH {self.path}")
        self.json({})

    def do_POST(self):
        dados = self.corpo()
        if self.path.endswith("/stt"):
            registrar(f"POST {self.path} audio={len(dados)}")
            texto = open(STT).read().strip() if STT else ""
            return self.json({"text": texto})
        if self.path.endswith("/tts"):
            registrar(f"POST {self.path} {dados.decode()}")
            b = b"RIFF0000WAVEfake"
            self.send_response(200)
            self.send_header("Content-Type", "audio/wav")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            return self.wfile.write(b)
        try:
            c = json.loads(dados or b"{}")
        except Exception:
            c = {}
        registrar(f"POST {self.path} {json.dumps(c, ensure_ascii=False)[:6000]}")
        if self.path == "/api/auth/login":
            return self.json({"access_token": "tok1", "refresh_token": "ref1"})
        if self.path == "/api/auth/refresh":
            return self.json({"access_token": "tok2"})
        if self.path == "/api/sessions":
            return self.json({"id": "sess-1"})
        if self.path.endswith("/tool-result"):
            resultados[c.get("call_id")] = c
            return self.json({})
        if self.path.endswith("/tool-decision"):
            return self.json({})
        if self.path.endswith("/chat"):
            return self.chat(c)
        self.json({"detail": "?"}, 404)

    def sse(self, ev):
        self.wfile.write(b"data: " + json.dumps(ev, ensure_ascii=False).encode() + b"\n\n")
        self.wfile.flush()

    def chat(self, c):
        texto = c.get("content", "").lower()
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "close")
        self.end_headers()
        ferramenta = None
        if "netflix" in texto:
            ferramenta = ("lumna_midia_abrir", {"servico": "netflix"}, "Abri a Netflix. Bom filme!")
        elif "volume" in texto:
            ferramenta = ("lumna_audio_volume", {"percentual": 35}, "Volume em 35 por cento.")
        if ferramenta:
            nome, args, final = ferramenta
            self.sse({"type": "tool_client", "tool": {"id": "call-1", "name": nome, "args": args}})
            for _ in range(100):
                if "call-1" in resultados:
                    break
                time.sleep(0.05)
            resultados.pop("call-1", None)
            partes = [final]
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
