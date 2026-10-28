"""GitHub integration: encrypted PAT, repo listing, clone/pull (direct) and commit+push (via approval)."""
import asyncio
import logging
import os
import re
import shutil
import tempfile
from pathlib import Path
from typing import Optional

import httpx
from fastapi import APIRouter, BackgroundTasks, HTTPException
from pydantic import BaseModel, Field

import workspace_runtime as wr
from core import Action, Project, audit, db, dec, enc, get_project, now_iso, oid
from rag import index_project

router = APIRouter(prefix="/api")
log = logging.getLogger("github")
REPO_RE = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
GH = "https://api.github.com"
# The token reaches askpass through an inherited pipe (never argv/env/disk); its fd number comes in
# HARNESS_TOKEN_FD. Only the password prompt reads it: git asks for the username first.
ASKPASS = """#!/usr/bin/python3
import os, sys
if "username" in (sys.argv[1].lower() if len(sys.argv) > 1 else ""):
    print("x-access-token")
else:
    with os.fdopen(int(os.environ["HARNESS_TOKEN_FD"]), "r", closefd=False) as f:
        print(f.read().strip())
"""


async def _cfg() -> Optional[dict]:
    return await db.github.find_one({"key": "single-user"})


async def _token() -> tuple[str, dict]:
    c = await _cfg()
    if not c:
        raise HTTPException(400, "GitHub não conectado. Salve um token em Config → GitHub.")
    return dec(c["token"]), c


async def _gh(method: str, path: str, token: str, **kw):
    async with httpx.AsyncClient(timeout=20) as c:
        r = await c.request(method, GH + path, headers={"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"}, **kw)
    if r.status_code >= 400:
        raise HTTPException(400 if r.status_code != 401 else 401, f"GitHub respondeu {r.status_code}")
    return r.json()


async def run_git(args: list[str], cwd: Optional[str], token: str = "", timeout: int = 180) -> tuple[int, str]:
    home = tempfile.mkdtemp(prefix="git-home-")
    wr.own(Path(home))
    env = {"PATH": "/usr/bin:/bin", "HOME": home, "GIT_TERMINAL_PROMPT": "0", "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": "/dev/null", "LANG": "C.UTF-8"}
    fds: tuple = ()
    if token:
        ask = os.path.join(home, "askpass.py")
        with open(ask, "w") as f:
            f.write(ASKPASS)
        os.chmod(ask, 0o700)
        wr.own(Path(ask))
        r, w = os.pipe()
        os.write(w, token.encode())
        os.close(w)
        os.set_inheritable(r, True)
        fds = (r,)
        env["GIT_ASKPASS"] = ask
        env["HARNESS_TOKEN_FD"] = str(r)

    def pre():
        wr._drop_privileges()

    base = [shutil.which("git") or "/usr/bin/git", "-c", "core.hooksPath=/dev/null", "-c", "credential.helper=", "-c", "protocol.file.allow=never"]
    try:
        p = await asyncio.create_subprocess_exec(*base, *args, cwd=cwd, env=env, stdin=asyncio.subprocess.DEVNULL,
                                                 stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, pass_fds=() if wr.IS_WIN else fds, preexec_fn=wr.preexec(pre))
        try:
            out, _ = await asyncio.wait_for(p.communicate(), timeout=timeout)
        except asyncio.TimeoutError:
            p.kill()
            return 124, "Timeout na operação git"
        text = out.decode("utf-8", "replace")
        if token:
            text = text.replace(token, "***")
        if p.returncode != 0:
            log.warning("git %s falhou (código %s): %s", args[0], p.returncode, text[-1500:].strip())
        return p.returncode, text[-6000:]
    finally:
        for fd in fds:
            try:
                os.close(fd)
            except OSError:
                pass
        shutil.rmtree(home, ignore_errors=True)


class TokenIn(BaseModel):
    token: str = Field(min_length=20, max_length=300)


@router.get("/github/status")
async def status():
    c = await _cfg()
    return {"connected": bool(c), "login": (c or {}).get("login", ""), "name": (c or {}).get("name", "")}


@router.put("/github/token")
async def save_token(body: TokenIn):
    user = await _gh("GET", "/user", body.token.strip())
    await db.github.update_one({"key": "single-user"}, {"$set": {"token": enc(body.token.strip()), "login": user["login"], "name": user.get("name") or user["login"],
                                                               "email": f"{user['id']}+{user['login']}@users.noreply.github.com", "updated_at": now_iso()}}, upsert=True)
    await audit("github", f"GitHub conectado como {user['login']}")
    return {"connected": True, "login": user["login"]}


@router.delete("/github/token")
async def delete_token():
    await db.github.delete_many({"key": "single-user"})
    await audit("github", "GitHub desconectado")
    return {"connected": False}


@router.get("/github/repos")
async def repos():
    token, _ = await _token()
    data = await _gh("GET", "/user/repos", token, params={"per_page": 100, "sort": "updated", "affiliation": "owner,collaborator,organization_member"})
    return [{"full_name": r["full_name"], "private": r["private"], "default_branch": r.get("default_branch"), "description": r.get("description") or "", "updated_at": r.get("updated_at")} for r in data]


class CloneIn(BaseModel):
    full_name: str
    folder: Optional[str] = None


@router.post("/github/clone")
async def clone(body: CloneIn, bg: BackgroundTasks):
    if not REPO_RE.match(body.full_name):
        raise HTTPException(400, "Repositório inválido (use dono/nome)")
    token, _ = await _token()
    folder = body.folder or re.sub(r"[^a-zA-Z0-9._-]+", "-", body.full_name.split("/")[1].lower())[:60]
    path = wr.project_root(folder)
    if path.exists() or await db.projects.find_one({"folder": folder}):
        raise HTTPException(409, f"A pasta '{folder}' já existe")
    code, out = await run_git(["clone", "--no-recurse-submodules", "--", f"https://github.com/{body.full_name}.git", str(path)], str(wr.ROOT), token, timeout=600)
    if code != 0:
        shutil.rmtree(path, ignore_errors=True)
        raise HTTPException(400, f"Falha ao clonar: {out[-400:]}")
    p = Project(name=body.full_name.split("/")[1], folder=folder, description=f"Clonado de github.com/{body.full_name}", git_repo=body.full_name)
    res = await db.projects.insert_one(p.to_mongo())
    p.id = str(res.inserted_id)
    await audit("github", f"Clonado {body.full_name} → {folder}", p.id)
    bg.add_task(index_project, p)
    return p.out()


async def _repo_project(pid: str) -> Project:
    p = await get_project(pid)
    if not (wr.project_root(p.folder) / ".git").is_dir():
        raise HTTPException(400, "Este projeto não é um repositório git")
    return p


@router.get("/projects/{pid}/git/status")
async def git_status(pid: str):
    p = await get_project(pid)
    if not (wr.project_root(p.folder) / ".git").is_dir():
        return {"is_repo": False}
    cwd = str(wr.project_root(p.folder))
    _, branch = await run_git(["rev-parse", "--abbrev-ref", "HEAD"], cwd)
    _, st = await run_git(["status", "--porcelain"], cwd)
    return {"is_repo": True, "repo": p.git_repo, "branch": branch.strip(), "changes": [ln for ln in st.splitlines() if ln.strip()]}


@router.post("/projects/{pid}/git/pull")
async def git_pull(pid: str, bg: BackgroundTasks):
    p = await _repo_project(pid)
    token, _ = await _token()
    code, out = await run_git(["pull", "--ff-only", "--no-recurse-submodules"], str(wr.project_root(p.folder)), token)
    await audit("github", f"Pull {'ok' if code == 0 else 'falhou'}", p.id)
    if code != 0:
        raise HTTPException(400, f"Pull falhou: {out[-400:]}")
    wr.own_tree(wr.project_root(p.folder))
    bg.add_task(index_project, p)
    return {"ok": True, "output": out}


class CommitIn(BaseModel):
    message: str = Field(min_length=1, max_length=200)


@router.post("/projects/{pid}/git/commit-push")
async def propose_push(pid: str, body: CommitIn):
    p = await _repo_project(pid)
    await _token()
    _, st = await run_git(["status", "--porcelain"], str(wr.project_root(p.folder)))
    if not st.strip():
        raise HTTPException(400, "Nada para commitar")
    a = Action(project_id=p.id, type="git_push", command=f"git commit + push: {body.message}", new_content=st.strip(), old_content=body.message, origin="user")
    res = await db.actions.insert_one(a.to_mongo())
    a.id = str(res.inserted_id)
    await audit("github", f"Push proposto: {body.message}", p.id, action_id=a.id)
    return a.out()


async def execute_push(p: Project, message: str) -> tuple[bool, str]:
    token, cfg = await _token()
    cwd = str(wr.project_root(p.folder))
    ident = ["-c", f"user.name={cfg['name']}", "-c", f"user.email={cfg['email']}"]
    code, out = await run_git(["add", "--all"], cwd)
    if code == 0:
        code, out = await run_git([*ident, "commit", "-m", message], cwd)
    if code == 0:
        code, out2 = await run_git(["push", "origin", "HEAD"], cwd, token, timeout=300)
        out += "\n" + out2
    return code == 0, out
