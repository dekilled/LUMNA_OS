#!/usr/bin/env python3
"""Um celular de mentira: fala o protocolo do Connect (WebSocket JSON, porta 47800) com o lumna-connect de verdade."""
import asyncio, base64, json, os, sys, websockets

URL = "ws://127.0.0.1:%d" % int(os.environ.get("PORTA_CONTROLE", "47800"))


async def entrar(codigo=None, token=None):
    ws = await websockets.connect(URL, max_size=2 ** 22)
    if codigo:
        await ws.send(json.dumps({"type": "pair", "code": codigo, "name": "Celular Falso"}))
        r = json.loads(await ws.recv())
        assert r.get("type") == "paired", r
        token = r["token"]
    await ws.send(json.dumps({"type": "hello", "token": token}))
    while True:  # o "mode" que o PC manda ao entrar vem antes do "ready"
        r = json.loads(await ws.recv())
        if r.get("type") == "ready":
            break
    return ws, token


async def pedir(ws, msg, tipo_resposta=None):
    await ws.send(json.dumps(msg))
    while True:
        r = json.loads(await asyncio.wait_for(ws.recv(), 10))
        if tipo_resposta is None or r.get("type") == tipo_resposta or r.get("type") == "error":
            return r
