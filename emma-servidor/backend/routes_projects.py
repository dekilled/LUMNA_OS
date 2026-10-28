import json
import os
import re
from typing import Optional

import httpx
from fastapi import APIRouter, BackgroundTasks, HTTPException, Request, Response
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field

import processes as pm
import terminal_log
import workspace_runtime as wr
from auth import download_token, preview_token, verify_download, verify_preview
from core import Action, Memory, Project, audit, db, get_project, get_settings, now_iso, oid
from rag import index_project, ingest_url, retrieve
import nsfw_profile

router = APIRouter(prefix="/api")


class ProjectIn(BaseModel):
    name: str
    folder: Optional[str] = None
    description: str = ""
    mode: Optional[str] = None  # the project's mode (see modes.py); none: Projeto


class ProjectPatch(BaseModel):
    name: Optional[str] = None
    description: Optional[str] = None
    system_prompt: Optional[str] = None
    personality: Optional[str] = None
    mode: Optional[str] = None


def slug(s: str) -> str:
    return re.sub(r"[^a-zA-Z0-9._-]+", "-", s.strip().lower()).strip("-")[:60] or "projeto"


@router.get("/projects")
async def list_projects():
    docs = await db.projects.find().sort("created_at", 1).to_list(200)
    return [Project.from_mongo(d).out() for d in docs]


@router.get("/projects/folders")
async def available_folders():
    used = {d["folder"] for d in await db.projects.find({}, {"folder": 1}).to_list(500)}
    return {"root": str(wr.ROOT), "folders": sorted(p.name for p in wr.ROOT.iterdir() if p.is_dir() and not p.name.startswith(".") and p.name not in used)}


@router.post("/projects")
async def create_project(body: ProjectIn, bg: BackgroundTasks):
    folder = body.folder or slug(body.name)
    path = wr.project_root(folder)
    if await db.projects.find_one({"folder": folder}):
        raise HTTPException(409, "Pasta já conectada a outro projeto")
    path.mkdir(exist_ok=True)
    wr.own(path)
    p = Project(name=body.name, folder=folder, description=body.description)
    res = await db.projects.insert_one(p.to_mongo())
    p.id = str(res.inserted_id)
    import modes
    p.mode = await modes.set_project_mode(p.id, body.mode or modes.DEFAULT_MODE)
    await audit("project", f"Projeto conectado: {p.name} ({folder})", p.id)
    bg.add_task(index_project, p)
    return p.out()


@router.get("/projects/{pid}")
async def read_project(pid: str):
    return (await get_project(pid)).out()


@router.patch("/projects/{pid}")
async def patch_project(pid: str, body: ProjectPatch):
    await get_project(pid)
    upd = {k: v for k, v in body.model_dump().items() if v is not None and k != "mode"}
    if body.mode is not None:
        import modes
        await modes.set_project_mode(pid, body.mode)
    if upd:
        await db.projects.update_one({"_id": oid(pid)}, {"$set": upd})
    return (await get_project(pid)).out()


@router.delete("/projects/{pid}")
async def delete_project(pid: str):
    p = await get_project(pid)
    await db.projects.delete_one({"_id": oid(pid)})
    for col in ("rag_chunks", "memories", "actions"):
        await db[col].delete_many({"project_id": pid})
    await audit("project", f"Projeto desconectado (arquivos preservados): {p.name}", pid)
    return {"ok": True}


# ---- RAG ----
@router.post("/projects/{pid}/index")
async def reindex(pid: str, bg: BackgroundTasks):
    p = await get_project(pid)
    bg.add_task(index_project, p)
    await audit("rag", "Reindexação solicitada", pid)
    return {"ok": True, "status": "indexing"}


class UrlIn(BaseModel):
    url: str


@router.post("/projects/{pid}/ingest-url")
async def ingest(pid: str, body: UrlIn):
    p = await get_project(pid)
    if not body.url.startswith(("http://", "https://")):
        raise HTTPException(400, "URL inválida")
    try:
        n = await ingest_url(p, body.url)
    except Exception as e:
        raise HTTPException(400, f"Falha ao ingerir URL: {str(e)[:200]}")
    await audit("rag", f"URL ingerida: {body.url} ({n} chunks)", pid)
    return {"ok": True, "chunks": n}


@router.get("/projects/{pid}/sources")
async def sources(pid: str):
    groups: dict[tuple, dict] = {}
    for d in await db.rag_chunks.find({"project_id": pid}, {"source": 1, "source_type": 1, "suspicious": 1}).to_list(None):
        key = (d.get("source"), d.get("source_type"))
        g = groups.setdefault(key, {"source": key[0], "source_type": key[1], "chunks": 0, "suspicious": False})
        g["chunks"] += 1
        g["suspicious"] = g["suspicious"] or bool(d.get("suspicious"))
    return sorted(groups.values(), key=lambda g: g["source"] or "")[:2000]


class QueryIn(BaseModel):
    query: str
    k: int = 6


@router.post("/projects/{pid}/rag/search")
async def rag_search(pid: str, body: QueryIn):
    await get_project(pid)
    return await retrieve(pid, body.query, body.k)


# ---- Memory ----
class MemoryIn(BaseModel):
    kind: str = "fact"
    content: str
    source: str = "manual"


@router.get("/projects/{pid}/memories")
async def list_memories(pid: str):
    docs = await db.memories.find(await nsfw_profile.scope({"project_id": pid})).sort("created_at", -1).to_list(500)
    return [Memory.from_mongo(d).out() for d in docs]


@router.post("/projects/{pid}/memories")
async def add_memory(pid: str, body: MemoryIn):
    await get_project(pid)
    if body.kind not in ("preference", "decision", "fact"):
        raise HTTPException(400, "Tipo inválido")
    m = Memory(project_id=pid, **body.model_dump(), nsfw=await nsfw_profile.nsfw())
    res = await db.memories.insert_one(m.to_mongo())
    m.id = str(res.inserted_id)
    await audit("memory", f"Memória adicionada [{m.kind}]", pid, content=m.content)
    return m.out()


@router.patch("/memories/{mid}")
async def patch_memory(mid: str, body: MemoryIn):
    if not await nsfw_profile.visible(await db.memories.find_one({"_id": oid(mid)})):
        raise HTTPException(404, "Memória não encontrada")
    await db.memories.update_one({"_id": oid(mid)}, {"$set": {"kind": body.kind, "content": body.content}})
    return Memory.from_mongo(await db.memories.find_one({"_id": oid(mid)})).out()


@router.delete("/memories/{mid}")
async def delete_memory(mid: str):
    if not await nsfw_profile.visible(await db.memories.find_one({"_id": oid(mid)})):
        raise HTTPException(404, "Memória não encontrada")
    await db.memories.delete_one({"_id": oid(mid)})
    return {"ok": True}


# ---- Workspace files ----
@router.get("/projects/{pid}/tree")
async def file_tree(pid: str):
    p = await get_project(pid)
    return wr.tree(p.folder)


@router.get("/projects/{pid}/file")
async def file_read(pid: str, path: str):
    p = await get_project(pid)
    return wr.read_file(p.folder, path)


class SaveIn(BaseModel):
    path: str
    content: str
    base_hash: str = ""


@router.put("/projects/{pid}/file")
async def file_save(pid: str, body: SaveIn):
    p = await get_project(pid)
    cur = wr.current_hash(p.folder, body.path)
    if cur and body.base_hash and cur != body.base_hash:
        raise HTTPException(409, "Conflito: o arquivo mudou desde que foi aberto. Recarregue antes de salvar.")
    wr.write_file(p.folder, body.path, body.content)
    await audit("file", f"Edição manual aprovada: {body.path}", pid, origin="user")
    return {"ok": True, "hash": wr.sha(body.content)}


class PathIn(BaseModel):
    path: str


class MoveIn(BaseModel):
    src: str
    dst: str


@router.post("/projects/{pid}/fs/file")
async def fs_new_file(pid: str, body: PathIn):
    p = await get_project(pid)
    wr.create_entry(p.folder, body.path, is_dir=False)
    await audit("file", f"Arquivo criado: {body.path}", pid, origin="user")
    return {"ok": True}


@router.post("/projects/{pid}/fs/dir")
async def fs_new_dir(pid: str, body: PathIn):
    p = await get_project(pid)
    wr.create_entry(p.folder, body.path, is_dir=True)
    await audit("file", f"Pasta criada: {body.path}", pid, origin="user")
    return {"ok": True}


@router.post("/projects/{pid}/fs/move")
async def fs_move(pid: str, body: MoveIn):
    p = await get_project(pid)
    wr.move_entry(p.folder, body.src, body.dst)
    await audit("file", f"Movido: {body.src} → {body.dst}", pid, origin="user")
    return {"ok": True}


@router.delete("/projects/{pid}/fs")
async def fs_delete(pid: str, path: str):
    p = await get_project(pid)
    wr.remove_entry(p.folder, path)
    await audit("file", f"Excluído: {path}", pid, origin="user")
    return {"ok": True}


@router.get("/projects/{pid}/download-token")
async def get_download_token(pid: str, path: str):
    p = await get_project(pid)
    wr._entry(p.folder, path)
    return {"url": f"/api/dl/{download_token({'pid': pid, 'path': path})}"}


@router.get("/attachments/{aid}/download-token")
async def get_attachment_download_token(aid: str):
    from media import load_attachment
    await load_attachment(aid)
    return {"url": f"/api/dl/{download_token({'att': aid})}"}


@router.get("/dl/{token}")
async def download(token: str, bg: BackgroundTasks):
    t = verify_download(token)
    if not t:
        raise HTTPException(401, "Link de download inválido ou expirado")
    if "backup" in t:
        from backup import backup_file
        path, name = await backup_file(bool(t["backup"]))
        bg.add_task(path.unlink, missing_ok=True)
        return FileResponse(path, media_type="application/zip", filename=name, headers=SANDBOX_HEADERS)
    if "att" in t:
        from media import ATTACH_DIR, load_attachment
        doc = await load_attachment(t["att"])
        name = doc.get("name") or "arquivo"
        if doc.get("type", "image") == "image" and "." not in name:
            name += "." + doc["mime"].split("/")[-1]
        return FileResponse(ATTACH_DIR / t["att"], media_type="application/octet-stream", filename=name, headers=SANDBOX_HEADERS)
    p = await get_project(t["pid"])
    target = wr._entry(p.folder, t["path"])
    if target.is_dir():
        import tempfile
        from pathlib import Path
        fd, tmp = tempfile.mkstemp(suffix=".zip")
        os.close(fd)
        wr.zip_dir(p.folder, t["path"], Path(tmp))
        bg.add_task(os.unlink, tmp)
        return FileResponse(tmp, media_type="application/zip", filename=f"{target.name}.zip", headers=SANDBOX_HEADERS)
    return FileResponse(target, media_type="application/octet-stream", filename=target.name, headers=SANDBOX_HEADERS)


@router.get("/projects/{pid}/preview-token")
async def get_preview_token(pid: str):
    await get_project(pid)
    return {"token": preview_token(pid)}


def _pid_from(token: str) -> str:
    pid = verify_preview(token)
    if not pid:
        raise HTTPException(401, "Token de preview inválido ou expirado")
    return pid


@router.get("/pv/{token}/{path:path}")
async def preview_tokenized(token: str, path: str = ""):
    return await preview(_pid_from(token), path)


@router.get("/projects/{pid}/preview/{path:path}")
async def preview(pid: str, path: str = ""):
    p = await get_project(pid)
    target = wr.safe_path(p.folder, path or "index.html")
    if target.is_dir():
        target = target / "index.html"
    if not target.is_file():
        raise HTTPException(404, "Arquivo de preview não encontrado (esperado index.html)")
    return FileResponse(target, headers=SANDBOX_HEADERS)


# ---- Terminal (user-initiated, still policy-checked) ----
class CmdIn(BaseModel):
    command: str


@router.post("/projects/{pid}/terminal")
async def terminal(pid: str, body: CmdIn):
    p = await get_project(pid)
    s = await get_settings()
    res = await wr.run_command(p.folder, body.command, s.sandbox_profile)
    await audit("command", f"$ {body.command}", pid, origin="user", profile=s.sandbox_profile, exit_code=res["exit_code"], denied=res["denied"], reason=res["reason"])
    await terminal_log.record(pid, body.command, "user", res)  # what the Lumna can read (read_terminal)
    return {"command": body.command, "profile": s.sandbox_profile, **res}


# ---- Approval queue ----
@router.get("/projects/{pid}/actions")
async def list_actions(pid: str, status: Optional[str] = None):
    q = {"project_id": pid}
    if status:
        q["status"] = status
    docs = await db.actions.find(q).sort("created_at", -1).to_list(300)
    return [Action.from_mongo(d).out() for d in docs]


@router.get("/actions")
async def actions_by_ids(ids: str):
    docs = await db.actions.find({"_id": {"$in": [oid(i) for i in ids.split(",") if i]}}).to_list(100)
    return [Action.from_mongo(d).out() for d in docs]


class ApproveIn(BaseModel):
    remember: bool = False
    note: str = ""
    always: bool = False  # a command: "Permitir sempre" in this project (exact command)


async def _pending(aid: str) -> Action:
    a = Action.from_mongo(await db.actions.find_one({"_id": oid(aid)}))
    if not a:
        raise HTTPException(404, "Ação não encontrada")
    if a.status != "pending":
        raise HTTPException(409, f"Ação já está '{a.status}'")
    return a


async def _finish(a: Action, status: str, result: str):
    await db.actions.update_one({"_id": oid(a.id)}, {"$set": {"status": status, "result": result, "resolved_at": now_iso()}})
    a.status, a.result = status, result


async def execute_action(a: Action, p: Project):
    """Runs one approved action (a card's "Aprovar", or each file of an approved plan)."""
    try:
        if a.type == "edit":
            cur = wr.current_hash(p.folder, a.path)
            if cur != a.base_hash:
                await _finish(a, "conflict", "O arquivo mudou após a proposta. Peça uma nova proposta.")
                await audit("action", f"Conflito em {a.path}", p.id, action_id=a.id)
                return
            wr.write_file(p.folder, a.path, a.new_content or "")
            await _finish(a, "executed", "Arquivo gravado")
        elif a.type == "delete":
            wr.delete_file(p.folder, a.path)
            await _finish(a, "executed", "Arquivo removido")
        elif a.type == "command":
            s = await get_settings()
            res = await wr.run_command(p.folder, a.command, s.sandbox_profile)
            await terminal_log.record(p.id, a.command, "approved", res)
            out = res["reason"] or (res["stdout"] + ("\n" + res["stderr"] if res["stderr"] else ""))
            await _finish(a, "executed" if res["ok"] else "failed", out[:20000])
        elif a.type == "git_push":
            from github import execute_push
            ok, out = await execute_push(p, a.old_content or "update")
            await _finish(a, "executed" if ok else "failed", out[-6000:])
        elif a.type == "run":
            s = await get_settings()
            ap, reason = await pm.start(p.id, p.folder, a.command, s.sandbox_profile)
            await _finish(a, "failed" if reason else "executed", reason or f"App iniciado na porta {ap.port} — veja o Preview")
    except HTTPException as e:
        await _finish(a, "failed", str(e.detail))
    await audit("action", f"Aprovado: {a.type} {a.path or a.command}", p.id, action_id=a.id, status=a.status)


@router.post("/actions/{aid}/approve")
async def approve(aid: str, body: ApproveIn):
    a = await _pending(aid)
    p = await get_project(a.project_id)
    if body.always and a.type == "command" and a.command and a.command not in p.allowed_commands:
        p.allowed_commands = [*p.allowed_commands, a.command][-200:]
        await db.projects.update_one({"_id": oid(p.id)}, {"$set": {"allowed_commands": p.allowed_commands}})
        await audit("action", f"Permitido sempre: {a.command}", p.id, action_id=a.id)
    await execute_action(a, p)
    if body.remember and a.status == "executed":
        text = body.note or f"Mudança aprovada ({a.type}): {a.path or a.command}"
        await db.memories.insert_one(Memory(project_id=p.id, kind="decision", content=text, source="approval", nsfw=await nsfw_profile.nsfw()).to_mongo())
    return a.out()


@router.get("/projects/{pid}/allowed-commands")
async def allowed_commands(pid: str):
    return {"commands": (await get_project(pid)).allowed_commands}


class CommandIn(BaseModel):
    command: str = Field(max_length=2000)


@router.post("/projects/{pid}/allowed-commands/remove")
async def forget_command(pid: str, body: CommandIn):
    p = await get_project(pid)
    left = [c for c in p.allowed_commands if c != body.command]
    await db.projects.update_one({"_id": oid(p.id)}, {"$set": {"allowed_commands": left}})
    await audit("action", f"Deixou de permitir sempre: {body.command}", p.id)
    return {"commands": left}


@router.post("/actions/{aid}/reject")
async def reject(aid: str):
    a = await _pending(aid)
    await _finish(a, "rejected", "Descartado pelo usuário")
    await audit("action", f"Rejeitado: {a.type} {a.path or a.command}", a.project_id, action_id=a.id)
    return a.out()


# ---- App processes & live preview ----
class RunIn(BaseModel):
    command: str


@router.get("/projects/{pid}/processes")
async def list_processes(pid: str):
    await get_project(pid)
    return pm.list_for(pid)


def _port_args(runner: str, script: str) -> str:
    """Vite and Next ignore $PORT: pass the allocated port (and loopback host) on the command line."""
    sep = " " if runner == "yarn" else " -- "
    if re.search(r"\bvite\b", script):
        return f"{sep}--port {{PORT}} --host 127.0.0.1 --strictPort"
    if re.search(r"\bnext\b", script):
        return f"{sep}-p {{PORT}} -H 127.0.0.1"
    return ""


@router.get("/projects/{pid}/run-suggestions")
async def run_suggestions(pid: str):
    p = await get_project(pid)
    base = wr.project_root(p.folder)
    out = []
    pkg = base / "package.json"
    if pkg.is_file():
        try:
            scripts = json.loads(pkg.read_text()).get("scripts", {})
        except ValueError:
            scripts = {}
        runner = "yarn" if (base / "yarn.lock").exists() else "npm run"
        out += [f"{runner} {s}{_port_args(runner, scripts[s])}" for s in ("dev", "start", "serve", "preview") if s in scripts]
        if not (base / "node_modules").exists():
            out.insert(0, "yarn install" if runner == "yarn" else "npm install")
    if (base / "requirements.txt").is_file() and not (base / ".venv").exists():
        out.append("python3 -m venv .venv")
    if (base / "index.html").is_file():
        out.append("python3 -m http.server {PORT} --bind 127.0.0.1")
    return out


@router.get("/projects/{pid}/deps-suggestions")
async def deps_suggestions(pid: str):
    p = await get_project(pid)
    base = wr.project_root(p.folder)
    out = []
    if (base / "package.json").is_file():
        out.append({"label": "yarn install" if (base / "yarn.lock").exists() else "npm install", "command": "yarn install" if (base / "yarn.lock").exists() else "npm install"})
    if (base / "requirements.txt").is_file() or (base / "pyproject.toml").is_file():
        if not (base / ".venv").exists():
            out.append({"label": "Criar .venv", "command": "python3 -m venv .venv"})
        if (base / "requirements.txt").is_file():
            out.append({"label": "pip install -r requirements.txt", "command": ".venv/bin/pip install -r requirements.txt"})
    return out


@router.post("/projects/{pid}/tasks")
async def start_task(pid: str, body: RunIn):
    p = await get_project(pid)
    s = await get_settings()
    ap, reason = await pm.start(pid, p.folder, body.command, s.sandbox_profile, kind="task")
    await audit("process", f"Tarefa: {body.command}", pid, profile=s.sandbox_profile, denied=bool(reason), reason=reason)
    if reason:
        raise HTTPException(400, reason)
    return ap.public()


@router.post("/projects/{pid}/processes")
async def start_process(pid: str, body: RunIn):
    p = await get_project(pid)
    s = await get_settings()
    ap, reason = await pm.start(pid, p.folder, body.command, s.sandbox_profile)
    await audit("process", f"Iniciar app: {body.command}", pid, profile=s.sandbox_profile, denied=bool(reason), reason=reason)
    if reason:
        raise HTTPException(400, reason)
    return ap.public()


def _proc(xid: str) -> pm.AppProcess:
    ap = pm.REGISTRY.get(xid)
    if not ap:
        raise HTTPException(404, "Processo não encontrado")
    return ap


@router.post("/processes/{xid}/stop")
async def stop_process(xid: str):
    ap = await pm.stop(_proc(xid).id)
    await audit("process", f"Parar app: {ap.command}", ap.project_id)
    return ap.public()


@router.post("/processes/{xid}/restart")
async def restart_process(xid: str):
    old = _proc(xid)
    p = await get_project(old.project_id)
    s = await get_settings()
    raw = old.command.replace(str(old.port), "{PORT}")
    await pm.stop(old.id)
    ap, reason = await pm.start(p.id, p.folder, raw, s.sandbox_profile)
    if reason:
        raise HTTPException(400, reason)
    await audit("process", f"Reiniciar app: {raw}", p.id)
    return ap.public()


@router.get("/processes/{xid}/logs")
async def process_logs(xid: str, since: int = 0):
    ap = _proc(xid)
    return {"process": ap.public(), "lines": [x for x in ap.logs if x["n"] > since]}


@router.get("/processes/{xid}/health")
async def process_health(xid: str):
    return await pm.health(_proc(xid))


SANDBOX_HEADERS = {"Content-Security-Policy": "sandbox allow-scripts allow-forms allow-popups", "X-Content-Type-Options": "nosniff"}
HOP = {"content-encoding", "content-length", "transfer-encoding", "connection", "keep-alive", "x-frame-options", "content-security-policy"}
ABS_URL = re.compile(r'((?:src|href|action)=["\'])/(?!/)')


PROXY_METHODS = ["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD"]


@router.api_route("/px/{token}/{path:path}", methods=PROXY_METHODS)
async def proxy_tokenized(token: str, path: str, request: Request):
    return await _proxy(_pid_from(token), path, request, f"/api/px/{token}/")


@router.api_route("/projects/{pid}/proxy/{path:path}", methods=PROXY_METHODS)
async def proxy(pid: str, path: str, request: Request):
    return await _proxy(pid, path, request, f"/api/projects/{pid}/proxy/")


async def _proxy(pid: str, path: str, request: Request, prefix: str):
    ap = pm.running_for(pid)
    if not ap:
        return Response("<h3 style='font-family:sans-serif'>Nenhum app rodando neste projeto.</h3>", status_code=503, media_type="text/html")
    url = f"http://127.0.0.1:{ap.port}/{path}"
    headers = {k: v for k, v in request.headers.items() if k.lower() not in ("host", "accept-encoding", "content-length", "cookie", "authorization")}
    try:
        async with httpx.AsyncClient(timeout=30) as c:
            r = await c.request(request.method, url, params=request.query_params, content=await request.body(), headers=headers)
    except httpx.HTTPError as e:
        return Response(f"<h3 style='font-family:sans-serif'>App sem resposta na porta {ap.port}</h3><pre>{e}</pre>", status_code=502, media_type="text/html")
    out_headers = {k: v for k, v in r.headers.items() if k.lower() not in HOP}
    body = r.content
    if "text/html" in r.headers.get("content-type", ""):
        body = ABS_URL.sub(lambda m: m.group(1) + prefix, r.text).encode()
    return Response(body, status_code=r.status_code, headers={**out_headers, **SANDBOX_HEADERS})
