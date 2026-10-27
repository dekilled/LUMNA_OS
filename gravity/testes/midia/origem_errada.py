# Uma página da web (ou qualquer um) tentando falar com o serviço da extensão: tem de ser barrada.
# Sai com 0 se o servidor fechou a conexão sem obedecer, 1 se deixou passar.
import asyncio, json, sys, websockets

async def tenta(origem):
    try:
        async with websockets.connect("ws://127.0.0.1:47822/extensao", origin=origem) as ws:
            await ws.send(json.dumps({"evento": "ola", "versao": "intruso"}))
            await ws.send(json.dumps({"evento": "aba", "servico": "intruso", "tab": 99, "url": "https://x", "titulo": "x", "carregando": False}))
            await asyncio.wait_for(ws.recv(), 1.5)    # se o servidor deixasse, mandaria algo de volta; fechando, dá erro
            return False
    except (websockets.exceptions.ConnectionClosed, asyncio.TimeoutError, websockets.exceptions.InvalidStatusCode, OSError):
        return True

barrou = all(asyncio.run(tenta(o)) for o in ("https://www.youtube.com", "moz-extension://00000000-0000-0000-0000-000000000000", None))
print("barrado" if barrou else "DEIXOU PASSAR")
sys.exit(0 if barrou else 1)
