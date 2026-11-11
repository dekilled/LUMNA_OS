"""Como o servidor entende as falhas do provedor: um 400 sem explicação (Ollama com um modelo de GGUF) cai no protocolo de texto, e http://localhost:11434 vira /v1.
python3 testes/falha_llm_teste.py   (da pasta emma-servidor; precisa do pacote openai)"""
import os
import sys
import tempfile

aqui = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(aqui, "..", "backend"))
_t = tempfile.mkdtemp()
os.environ.setdefault("SECRETS_KEY", "a" * 64)
os.environ.setdefault("JWT_SECRET", "b" * 64)
os.environ.setdefault("SQLITE_PATH", f"{_t}/emma.db")
os.environ.setdefault("WORKSPACE_ROOT", f"{_t}/ws")
os.environ.setdefault("ADMIN_EMAIL", "x@lumna.local")
os.environ.setdefault("ADMIN_PASSWORD", "senha12345")
os.makedirs(f"{_t}/ws", exist_ok=True)

import httpx  # noqa: E402
import openai  # noqa: E402

import llm  # noqa: E402

falhas = []


def confere(cond, msg):
    if not cond:
        falhas.append(msg)
        print("FALHOU:", msg, file=sys.stderr)


def erro(status, msg):
    req = httpx.Request("POST", "http://localhost:11434/v1/chat/completions")
    return openai.APIStatusError(msg, response=httpx.Response(status, request=req), body=None)


confere(llm.why_failed(erro(400, "registry.ollama.ai/library/x does not support tools")) == "unsupported", "400 dizendo que não tem tools: unsupported")
confere(llm.why_failed(erro(400, "Bad request")) == "bad_call", "400 sem explicação: protocolo de texto nesta resposta")
confere(llm.why_failed(erro(422, "unprocessable")) == "bad_call", "422 sem explicação: o mesmo")
confere(llm.why_failed(erro(404, "404 page not found")) == "other", "404 é endereço errado: aparece como erro, sem trocar de protocolo")
confere(llm.why_failed(erro(401, "invalid api key")) == "other", "401 é chave errada: erro")
confere(llm.why_failed(erro(503, "overloaded")) == "transient", "503: tenta de novo")
confere(llm.why_failed(erro(400, "error parsing tool call: invalid character")) == "bad_call", "chamada ilegível: texto nesta resposta")

confere(llm.normalize_base_url("http://localhost:11434") == "http://localhost:11434/v1", "Ollama sem /v1 ganha /v1")
confere(llm.normalize_base_url("http://localhost:11434/") == "http://localhost:11434/v1", "com barra também")
confere(llm.normalize_base_url("http://localhost:11434/v1") == "http://localhost:11434/v1", "já com /v1 fica")
confere(llm.normalize_base_url("http://127.0.0.1:11434") == "http://127.0.0.1:11434/v1", "127.0.0.1 também")
confere(llm.normalize_base_url("https://api.openai.com/v1") == "https://api.openai.com/v1", "outros provedores não mudam")
confere(llm.normalize_base_url("http://meupc:8080") == "http://meupc:8080", "outra porta não muda")

if falhas:
    print(f"{len(falhas)} falha(s)", file=sys.stderr)
    sys.exit(1)
print("llm (falhas do provedor): ok")
