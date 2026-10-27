# A extensão do Firefox, de mentira: o mesmo que extensao-firefox/fundo.js faz com as abas, mas numa
# lista em memória. Conecta no serviço (ws://127.0.0.1:47822/extensao) com o Origin que o perfil fixou
# (extensions.webextensions.uuids, no user.js do perfil dado em --profile).
import asyncio, base64, json, os, re, sys, websockets

ATRASO = float(os.environ.get("EXT_ATRASO", "0.4"))   # quanto uma página leva para carregar
perfil = sys.argv[sys.argv.index("--profile") + 1]
uuid = re.search(r'extensions\.webextensions\.uuids.*?:\\"([0-9a-f-]{36})', open(os.path.join(perfil, "user.js")).read()).group(1)
extensao = os.path.join(perfil, "extensions")
print("extensões do perfil:", sorted(os.listdir(extensao)) if os.path.isdir(extensao) else None, flush=True)

abas = {}    # id -> {servico, url, titulo, status}
proximo = 0

async def main():
    global proximo
    while True:
        try:
            async with websockets.connect("ws://127.0.0.1:47822/extensao", origin="moz-extension://" + uuid) as ws:
                async def enviar(o): await ws.send(json.dumps(o))
                async def aba(i):
                    a = abas[i]
                    await enviar({"evento": "aba", "servico": a["servico"], "tab": i, "url": a["url"], "titulo": a["titulo"], "carregando": a["status"] == "loading"})
                async def carregar(i, url):
                    a = abas[i]; a["url"] = url; a["status"] = "loading"; a["titulo"] = ""
                    await aba(i)
                    await asyncio.sleep(ATRASO)
                    if i in abas:
                        a["status"] = "complete"; a["titulo"] = "Título de " + url
                        await aba(i)
                await enviar({"evento": "ola", "versao": "teste"})
                async for bruto in ws:
                    c = json.loads(bruto); r = {"ok": True}
                    achada = next((i for i, a in abas.items() if a["servico"] == c.get("servico")), None)
                    if c["cmd"] == "abrir":
                        if achada is None:
                            proximo += 1
                            abas[proximo] = {"servico": c["servico"], "url": "", "titulo": "", "status": "complete"}
                            asyncio.ensure_future(carregar(proximo, c["url"]))
                    elif c["cmd"] == "ir" and achada is not None:
                        asyncio.ensure_future(carregar(achada, c["url"]))
                    elif c["cmd"] == "fechar" and achada is not None:
                        del abas[achada]
                        await enviar({"evento": "fechada", "servico": c["servico"], "tab": achada})
                    elif c["cmd"] == "capturar":
                        r = {"ok": True, "dados": "data:image/jpeg;base64," + base64.b64encode(b"\xff\xd8\xff\xe0FAKEJPEG\xff\xd9").decode()} if achada else {"ok": False}
                    await enviar(dict({"id": c["id"]}, **r))
        except (OSError, websockets.exceptions.WebSocketException):
            await asyncio.sleep(0.5)

asyncio.run(main())
