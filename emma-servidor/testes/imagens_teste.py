"""Geração de imagens (backend/media.py): um servidor local compatível com OpenAI, sem chave; a ferramenta da Emma; o interruptor. Rode com o Python do servidor."""
import asyncio, base64, json, os, sys, tempfile, threading
from http.server import BaseHTTPRequestHandler, HTTPServer

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(AQUI, "..", "backend"))
os.environ.update(JWT_SECRET="x", SECRETS_KEY="teste", ALLOW_PRIVATE_URLS="true", SQLITE_PATH=os.path.join(tempfile.mkdtemp(), "t.db"), WORKSPACE_ROOT=tempfile.mkdtemp())
import media  # noqa: E402
from core import db, get_settings, module_on  # noqa: E402
from fastapi import HTTPException  # noqa: E402

PNG = base64.b64encode(base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")).decode()
PEDIDOS = []

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        PEDIDOS.append((self.path, json.loads(self.rfile.read(n) or b"{}"), self.headers.get("Authorization", "")))
        b = json.dumps({"data": [{"b64_json": PNG}]}).encode()
        self.send_response(200); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)

srv = HTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
falhas = 0
def confere(c, m):
    global falhas
    if not c:
        falhas += 1
        print("FALHOU:", m)

async def main():
    confere(media._local_url("http://127.0.0.1:8188/v1") and media._local_url("http://localhost:7860") and media._local_url("http://192.168.0.5:9000/v1"), "endereços locais")
    confere(not media._local_url("https://api.openai.com/v1") and not media._local_url(""), "a nuvem não é local")
    url = f"http://127.0.0.1:{srv.server_port}/v1"
    await db.settings.update_one({"key": "global"}, {"$set": {"key": "global", "image_gen": {"enabled": False, "base_url": url, "api_key": "", "model": "modelo-local", "size": "256x256"}}}, upsert=True)
    o = await media._tool_gerar_imagem({"prompt": "um gato astronauta"})
    confere(o.status == "error" and "desligada" in o.content, f"desligada devia recusar: {o}")
    await db.settings.update_one({"key": "global"}, {"$set": {"image_gen.enabled": True}})
    o = await media._tool_gerar_imagem({"prompt": "um gato astronauta"})
    confere(o.status == "ok" and len(o.attachments) == 1, f"a ferramenta devia gerar e devolver o anexo: {o}")
    confere(PEDIDOS and PEDIDOS[0][0].endswith("/images/generations") and PEDIDOS[0][1].get("prompt") == "um gato astronauta" and PEDIDOS[0][1].get("model") == "modelo-local", f"o pedido ao servidor local: {PEDIDOS}")
    confere((media.ATTACH_DIR / o.attachments[0]).is_file(), "a imagem devia estar guardada")
    o = await media._tool_gerar_imagem({"prompt": ""})
    confere(o.status == "error", "sem prompt")
    # nuvem sem chave: recusa com o caminho certo para configurar
    cfg = (await get_settings()).image_gen
    cfg.base_url, cfg.api_key = "https://api.openai.com/v1", ""
    try:
        await media.generate_image(cfg, "x")
        confere(False, "a nuvem sem chave devia recusar")
    except HTTPException as e:
        confere(e.status_code == 400 and "Painel" in str(e.detail), f"mensagem da nuvem sem chave: {e.detail}")
    s = await get_settings()
    confere(module_on(s, "visao") and module_on(s, "mcp"), "os módulos nascem ligados")
    s.modules = {"visao": False}
    confere(not module_on(s, "visao") and module_on(s, "mcp"), "desligar um módulo não mexe nos outros")

asyncio.run(main())
print("imagens ok" if not falhas else f"imagens: {falhas} falha(s)")
sys.exit(1 if falhas else 0)
