#!/usr/bin/env python3
"""Um provedor de IA de mentira, no padrão OpenAI (/v1/models e /v1/chat/completions com streaming e ferramentas): para provar que a Emma
conversa direto com ele, sem servidor no meio. Exige a chave "sk-teste". Cada pedido vai para o ARQUIVO_DE_LOG como "METODO CAMINHO corpo".
Uso: provedor-falso.py PORTA ARQUIVO_DE_LOG"""
import json, sys, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORTA, LOG = int(sys.argv[1]), sys.argv[2]
CHAVE = "sk-teste"


def registrar(linha):
    with open(LOG, "a") as f:
        f.write(linha + "\n")


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def autorizado(self):
        if self.headers.get("Authorization") != f"Bearer {CHAVE}":
            b = json.dumps({"error": {"message": "Incorrect API key provided", "type": "invalid_request_error"}}).encode()
            self.send_response(401)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
            return False
        return True

    def do_GET(self):
        registrar(f"GET {self.path} auth={self.headers.get('Authorization')}")
        if not self.autorizado():
            return
        if self.path.rstrip("/") == "/v1/models":
            b = json.dumps({"data": [{"id": "modelo-z"}, {"id": "modelo-x"}, {"id": "modelo-y"}]}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
            return
        self.send_response(404)
        self.end_headers()

    def sse(self, obj):
        self.wfile.write(b"data: " + json.dumps(obj, ensure_ascii=False).encode() + b"\n\n")
        self.wfile.flush()

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        corpo = self.rfile.read(n)
        registrar(f"POST {self.path} auth={self.headers.get('Authorization')} {corpo.decode()[:30000]}")
        if not self.autorizado():
            return
        if self.path.rstrip("/") != "/v1/chat/completions":
            self.send_response(404)
            self.end_headers()
            return
        c = json.loads(corpo)
        msgs = c.get("messages", [])
        ultimo_user = next((m["content"] for m in reversed(msgs) if m["role"] == "user"), "").lower()
        resultado_de_ferramenta = msgs[-1] if msgs and msgs[-1]["role"] == "tool" else None
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "close")
        self.end_headers()

        def chamar(nome, args):
            self.sse({"choices": [{"index": 0, "delta": {"role": "assistant", "tool_calls": [{"index": 0, "id": "call_1", "type": "function", "function": {"name": nome, "arguments": ""}}]}}]})
            a = json.dumps(args)
            meio = len(a) // 2
            for parte in (a[:meio], a[meio:]):  # os argumentos chegam em pedaços, como nos provedores de verdade
                self.sse({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "function": {"arguments": parte}}]}}]})
            self.sse({"choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls"}]})

        def falar(partes):
            for p in partes:
                self.sse({"choices": [{"index": 0, "delta": {"content": p}}]})
                time.sleep(0.03)
            self.sse({"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]})

        if resultado_de_ferramenta is not None:
            if "netflix" in ultimo_user:
                falar(["Abri a Netflix. ", "Bom filme!"])
            elif "layout" in ultimo_user:
                falar(["Encontrei essa opção nas configurações. Qual layout você quer usar?\n[acao: Adicionar layout]\n[sugestao: Português (Brasil)]"])
            else:
                falar(["Pronto, feito."])
        elif "netflix" in ultimo_user:
            chamar("lumna_midia_abrir", {"servico": "netflix"})
        elif "layout" in ultimo_user:
            chamar("lumna_painel_abrir", {"secao": "teclado/teclado"})
        elif "volume" in ultimo_user:
            chamar("lumna_audio_volume", {"percentual": 35})
        elif "markdown" in ultimo_user:
            falar(["# Um exemplo\n\nAqui vai **negrito** e `codigo`.\n\n", "- item um\n- item dois\n\n```c\nint main(void) { return 0; }\n```\n"])
        else:
            falar(["Brasília é a capital do Brasil. ", "Fica no Planalto Central."])
        self.close_connection = True


ThreadingHTTPServer(("127.0.0.1", PORTA), H).serve_forever()
