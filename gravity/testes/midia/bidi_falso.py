import asyncio, json, base64, websockets
ctxs = {}      # id -> url
n = 0
async def handler(ws, path=None):
    global n
    async def ev(method, params): await ws.send(json.dumps({"type":"event","method":method,"params":params}))
    async for raw in ws:
        m = json.loads(raw); mid = m["id"]; p = m.get("params", {}); r = {}
        meth = m["method"]
        if meth == "session.new": r = {"sessionId":"s1","capabilities":{}}
        elif meth == "browsingContext.create":
            n += 1; cid = f"ctx-{n}"; ctxs[cid] = "about:blank"; r = {"context": cid}
        elif meth == "browsingContext.navigate":
            cid, url = p["context"], p["url"]; ctxs[cid] = url; r = {"navigation":"nav","url":url}
            async def depois(cid=cid, url=url):
                await ev("browsingContext.navigationStarted", {"context":cid,"url":url})
                await asyncio.sleep(0.4)
                await ev("browsingContext.load", {"context":cid,"url":url})
            asyncio.ensure_future(depois())
        elif meth == "browsingContext.getTree":
            r = {"contexts":[{"context":c,"url":u,"children":[],"parent":None} for c,u in ctxs.items()]}
        elif meth == "script.evaluate":
            r = {"type":"success","result":{"type":"string","value":"Título de "+ctxs.get(p["target"]["context"],"?")},"realm":"r"}
        elif meth == "browsingContext.captureScreenshot": r = {"data": base64.b64encode(b"\xff\xd8\xff\xe0FAKEJPEG\xff\xd9").decode()}
        elif meth == "browsingContext.close":
            cid = p["context"]; ctxs.pop(cid, None)
            asyncio.ensure_future(ev("browsingContext.contextDestroyed", {"context":cid}))
        await ws.send(json.dumps({"type":"success","id":mid,"result":r}))
async def main():
    async with websockets.serve(handler, "127.0.0.1", 47821):
        await asyncio.Future()
asyncio.run(main())
