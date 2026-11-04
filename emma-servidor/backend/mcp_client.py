"""Servidores MCP (Model Context Protocol): ferramentas de fora que a Emma pode chamar.

Cada servidor configurado (um comando local, falando por stdin/stdout, ou um endereço http) é conectado numa tarefa própria; as ferramentas que ele
anuncia entram no REGISTRY como `mcp_<servidor>__<ferramenta>`, do mesmo jeito que as ferramentas do servidor, e passam pelas mesmas regras:
  - a permissão de cada uma nasce "ask" (a pessoa libera, uma a uma, no Painel); "off" a tira do prompt do modelo;
  - o que elas devolvem é conteúdo de fora (`untrusted`): vai ao modelo marcado como dado, e as ações seguintes pedem aprovação;
  - o texto que o servidor MCP escreve sobre as ferramentas (descrição) é de terceiros: vai cortado.
Adicionar um servidor stdio é mandar rodar um comando com os seus poderes: só a pessoa faz isso, no Painel (o servidor da Emma só escuta o LUMNA local).
"""
import asyncio
import logging
import os
import re
import shlex
import shutil
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

import tools as T
from core import audit, db, get_settings, now_iso, oid

router = APIRouter(prefix="/api")
log = logging.getLogger("mcp")

PREFIX = "mcp_"
MAX_TOOLS = 60  # por servidor: cada ferramenta ocupa o prompt de todas as mensagens
DESC_MAX = 500
RESULT_MAX = 30000
CONNECT_TIMEOUT = 120  # a primeira vez, o npx/uvx baixa o servidor
CALL_TIMEOUT = 120

_conns: dict[str, "Conn"] = {}


def slug(text: str, limit: int) -> str:
    s = re.sub(r"[^A-Za-z0-9]+", "_", text or "").strip("_").lower()
    return (s or "x")[:limit]


def tool_name(server: str, tool: str) -> str:
    """mcp_<servidor>__<ferramenta>: até 64 letras (o limite dos provedores) e só [a-z0-9_]."""
    return f"{PREFIX}{slug(server, 14)}__{slug(tool, 40)}"


def _params(doc: dict):
    from mcp import StdioServerParameters

    parts = shlex.split(doc.get("command") or "")
    if not parts:
        raise ValueError("falta o comando")
    if not shutil.which(parts[0]):
        raise ValueError(f"não achei o programa “{parts[0]}” (instale-o: ex.: Node.js para npx, ou uv para uvx)")
    env = {str(k): str(v) for k, v in (doc.get("env") or {}).items() if k}
    return StdioServerParameters(command=parts[0], args=parts[1:], env=env or None)


def _text(result) -> str:
    out = []
    for c in getattr(result, "content", None) or []:
        t = getattr(c, "type", "")
        if t == "text":
            out.append(c.text)
        elif t == "image":
            out.append("[imagem omitida]")
        elif t == "resource_link":
            out.append(f"[recurso: {getattr(c, 'uri', '')}]")
        elif t == "resource":
            r = getattr(c, "resource", None)
            out.append(getattr(r, "text", None) or f"[recurso: {getattr(r, 'uri', '')}]")
        else:
            out.append(f"[{t or 'conteúdo'} omitido]")
    return "\n".join(out).strip()


class Conn:
    def __init__(self, doc: dict):
        self.doc = doc
        self.id = str(doc["_id"])
        self.status = "stopped"  # connecting | ok | error | stopped
        self.error = ""
        self.names: list[str] = []  # as ferramentas que ele registrou
        self.client = None
        self.task: Optional[asyncio.Task] = None
        self._stop = asyncio.Event()

    def start(self) -> None:
        self._stop = asyncio.Event()
        self.status, self.error = "connecting", ""
        self.task = asyncio.create_task(self._run(), name=f"mcp-{self.doc.get('name')}")

    async def stop(self) -> None:
        self._stop.set()
        if self.task:
            try:
                await asyncio.wait_for(self.task, 10)
            except (asyncio.TimeoutError, asyncio.CancelledError, Exception):  # noqa: BLE001
                self.task.cancel()
        self.task = None

    async def _run(self) -> None:
        try:
            from mcp import ClientSession
            from mcp.client.stdio import stdio_client
            from mcp.client.streamable_http import streamablehttp_client
            if self.doc.get("transport", "stdio") == "stdio":
                transport = stdio_client(_params(self.doc))
            else:
                if not self.doc.get("url"):
                    raise ValueError("falta o endereço")
                transport = streamablehttp_client(self.doc["url"])
            async with transport as streams:
                async with ClientSession(streams[0], streams[1]) as session:
                    await asyncio.wait_for(session.initialize(), CONNECT_TIMEOUT)
                    lt = await asyncio.wait_for(session.list_tools(), CONNECT_TIMEOUT)
                    self.client = session
                    self._register(lt.tools)
                    self.status = "ok"
                    log.info("MCP %s: %d ferramenta(s)", self.doc.get("name"), len(self.names))
                    await self._stop.wait()
        except asyncio.CancelledError:
            raise
        except ImportError:
            self.status, self.error = "error", "O pacote “mcp” não está instalado no Python do servidor (pip install \"mcp>=1.12,<2\" \"pydantic<2.12\")."
        except BaseException as e:  # noqa: BLE001 — shown in the Painel (ExceptionGroup from anyio too)
            while getattr(e, "exceptions", None):  # ExceptionGroup (anyio) dentro de ExceptionGroup: o erro de verdade está no fundo
                e = e.exceptions[0]
            msg = str(e) or e.__class__.__name__
            self.status, self.error = "error", msg[:300]
            log.warning("MCP %s caiu: %s", self.doc.get("name"), msg[:200])
        finally:
            self._unregister()
            self.client = None
            if self.status == "ok":
                self.status = "stopped"

    def _register(self, tools) -> None:
        name = self.doc.get("name") or "mcp"
        for t in list(tools)[:MAX_TOOLS]:
            full = tool_name(name, t.name)
            if full in T.REGISTRY and full not in self.names:
                full = full[:60] + "_" + slug(self.id, 3)  # dois servidores com o mesmo nome
            desc = re.sub(r"\s+", " ", t.description or t.title or t.name)[:DESC_MAX]
            ann = getattr(t, "annotations", None)
            ro = bool(getattr(ann, "readOnlyHint", False))
            schema = getattr(t, "inputSchema", None)
            schema = schema if isinstance(schema, dict) else {"type": "object", "properties": {}}
            T.REGISTRY[full] = T.Tool(full, f"{name}: {t.title or t.name}", f"[servidor MCP “{name}”] {desc}", schema,
                                      self._runner(t.name, name), risk="read" if ro else "act", default="ask", where="server")
            self.names.append(full)

    def _unregister(self) -> None:
        for n in self.names:
            T.REGISTRY.pop(n, None)
        self.names = []

    def _runner(self, remote: str, server: str):
        async def run(args: dict) -> T.Outcome:
            if not self.client:
                return T.Outcome(f"O servidor MCP “{server}” não está conectado.", f"{server}: desconectado", status="error")
            try:
                r = await asyncio.wait_for(self.client.call_tool(remote, args or {}), CALL_TIMEOUT)
            except asyncio.TimeoutError:
                return T.Outcome(f"O servidor MCP “{server}” não respondeu em {CALL_TIMEOUT} s.", f"{remote}: sem resposta", status="error")
            except Exception as e:  # noqa: BLE001
                return T.Outcome(f"Erro do servidor MCP “{server}”: {e}"[:500], f"{remote}: erro", status="error")
            text = _text(r) or "(sem texto)"
            return T.Outcome(text[:RESULT_MAX], f"{server}: {remote}", untrusted=True, status="error" if getattr(r, "isError", False) else "ok")

        return run


def _public(c: Conn) -> dict:
    d = c.doc
    return {
        "id": c.id, "name": d.get("name", ""), "transport": d.get("transport", "stdio"), "command": d.get("command", ""), "url": d.get("url", ""),
        "env_keys": sorted((d.get("env") or {}).keys()), "enabled": bool(d.get("enabled", True)), "status": c.status, "error": c.error,
        "tools": [{"name": n, "label": T.REGISTRY[n].label if n in T.REGISTRY else n, "description": T.REGISTRY[n].description if n in T.REGISTRY else "",
                   "read_only": bool(n in T.REGISTRY and T.REGISTRY[n].risk == "read")} for n in c.names],
    }


async def on_start() -> None:
    for d in await db.mcp_servers.find({}).to_list(200):
        if d.get("enabled", True):
            c = _conns[str(d["_id"])] = Conn(d)
            c.start()


async def stop() -> None:
    for c in list(_conns.values()):
        await c.stop()
    _conns.clear()


class ServerIn(BaseModel):
    name: str = Field(min_length=1, max_length=40)
    transport: str = "stdio"  # stdio | http
    command: str = Field(default="", max_length=2000)
    url: str = Field(default="", max_length=500)
    env: dict[str, str] = {}
    enabled: bool = True


def _check(body: ServerIn) -> None:
    if body.transport not in ("stdio", "http"):
        raise HTTPException(400, "Transporte inválido (stdio ou http)")
    if body.transport == "stdio" and not body.command.strip():
        raise HTTPException(400, "Falta o comando do servidor")
    if body.transport == "http" and not body.url.strip().startswith(("http://", "https://")):
        raise HTTPException(400, "O endereço tem de começar com http:// ou https://")
    if len(body.env) > 40:
        raise HTTPException(400, "Variáveis demais")


@router.get("/mcp/servers")
async def list_servers():
    settings = await get_settings()
    out = []
    for d in await db.mcp_servers.find({}).sort("created_at", 1).to_list(200):
        c = _conns.get(str(d["_id"])) or Conn(d)
        c.doc = d
        p = _public(c)
        for t in p["tools"]:
            t["permission"] = T.permission(settings.tools, t["name"])
        out.append(p)
    return out


@router.post("/mcp/servers")
async def add_server(body: ServerIn):
    _check(body)
    doc = {"name": body.name.strip(), "transport": body.transport, "command": body.command.strip(), "url": body.url.strip(), "env": body.env,
           "enabled": body.enabled, "created_at": now_iso()}
    res = await db.mcp_servers.insert_one(doc)
    doc["_id"] = res.inserted_id
    await audit("mcp", f"Servidor MCP adicionado: {doc['name']}")
    if doc["enabled"]:
        c = _conns[str(doc["_id"])] = Conn(doc)
        c.start()
    return {"id": str(doc["_id"])}


@router.put("/mcp/servers/{sid}")
async def update_server(sid: str, body: ServerIn):
    _check(body)
    old = await db.mcp_servers.find_one({"_id": oid(sid)})
    if not old:
        raise HTTPException(404, "Servidor não encontrado")
    env = {k: (old.get("env", {}).get(k, "") if v in ("", "••••") else v) for k, v in body.env.items()}  # vazio/mascarado: mantém o segredo de antes
    upd = {"name": body.name.strip(), "transport": body.transport, "command": body.command.strip(), "url": body.url.strip(), "env": env, "enabled": body.enabled}
    await db.mcp_servers.update_one({"_id": oid(sid)}, {"$set": upd})
    if sid in _conns:
        await _conns.pop(sid).stop()
    doc = {**old, **upd}
    if doc["enabled"]:
        c = _conns[sid] = Conn(doc)
        c.start()
    return {"ok": True}


@router.delete("/mcp/servers/{sid}")
async def delete_server(sid: str):
    if sid in _conns:
        await _conns.pop(sid).stop()
    await db.mcp_servers.delete_one({"_id": oid(sid)})
    await audit("mcp", "Servidor MCP removido")
    return {"ok": True}


@router.post("/mcp/servers/{sid}/restart")
async def restart_server(sid: str):
    d = await db.mcp_servers.find_one({"_id": oid(sid)})
    if not d:
        raise HTTPException(404, "Servidor não encontrado")
    if sid in _conns:
        await _conns.pop(sid).stop()
    c = _conns[sid] = Conn(d)
    c.start()
    return {"ok": True}


class PermIn(BaseModel):
    permission: str


@router.put("/mcp/tools/{name}/permission")
async def set_permission(name: str, body: PermIn):
    if body.permission not in T.PERMISSIONS:
        raise HTTPException(400, "Permissão inválida (auto, ask ou off)")
    if not name.startswith(PREFIX):
        raise HTTPException(400, "Só as ferramentas MCP se mudam aqui")
    s = await get_settings()
    s.tools.permissions = {**(s.tools.permissions or {}), name: body.permission}
    data = s.to_mongo()
    data.pop("_id", None)
    await db.settings.update_one({"key": "global"}, {"$set": data}, upsert=True)
    return {"ok": True}
