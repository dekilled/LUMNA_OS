"""Workspace Runtime: long-running app processes (dev servers) with ports, logs and health."""
import asyncio
import os
import re
import signal
import socket
import time
import uuid
from collections import deque
from typing import Optional

import httpx

from core import now_iso
from workspace_runtime import build_limits, check_command, kill_tree, preexec, project_root, sandbox_env

PORTS = range(4100, 4200)
MAX_RUNNING = 3
MAX_LIFETIME = 3600
ERROR_RE = re.compile(r"(error|exception|traceback|failed|EADDRINUSE|cannot find module|module not found)", re.I)
# Dev servers announce where they listen ("Local:   http://localhost:5173/"); ANSI colors may split the text.
LISTEN_RE = re.compile(r"https?://(?:localhost|127\.0\.0\.1|0\.0\.0\.0|\[::1?\]):(\d{4,5})")
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
RESERVED_PORTS = {int(os.environ.get("HARNESS_PORT", "8001")), 3000}  # the Harness itself (backend, dev frontend)


class AppProcess:
    def __init__(self, project_id: str, folder: str, command: str, port: int, profile: str, kind: str = "app"):
        self.id = uuid.uuid4().hex[:12]
        self.kind = kind
        self.project_id, self.folder, self.command, self.port, self.profile = project_id, folder, command, port, profile
        self.status = "starting"
        self.exit_code: Optional[int] = None
        self.started_at = now_iso()
        self.started_mono = time.monotonic()
        self.logs: deque = deque(maxlen=3000)
        self.seq = 0
        self.errors = 0
        self.proc: Optional[asyncio.subprocess.Process] = None
        self.port_detected = False

    def log(self, line: str, stream: str = "out"):
        self.seq += 1
        err = bool(ERROR_RE.search(line))
        self.errors += err
        self.logs.append({"n": self.seq, "t": stream, "line": line[:2000], "error": err})

    def public(self) -> dict:
        return {"id": self.id, "project_id": self.project_id, "command": self.command, "port": self.port, "status": self.status,
                "exit_code": self.exit_code, "started_at": self.started_at, "errors": self.errors, "profile": self.profile, "kind": self.kind,
                "uptime_s": int(time.monotonic() - self.started_mono) if self.status == "running" else None}


REGISTRY: dict[str, AppProcess] = {}


def _port_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        try:
            s.bind(("127.0.0.1", port))
            return True
        except OSError:
            return False


def _alloc_port() -> Optional[int]:
    used = {p.port for p in REGISTRY.values() if p.status in ("starting", "running")}
    return next((p for p in PORTS if p not in used and _port_free(p)), None)


def running_for(project_id: str) -> Optional[AppProcess]:
    procs = [p for p in REGISTRY.values() if p.project_id == project_id and p.kind == "app" and p.status in ("starting", "running")]
    return procs[-1] if procs else None


def list_for(project_id: str) -> list[dict]:
    return [p.public() for p in reversed(list(REGISTRY.values())) if p.project_id == project_id][:10]


def _detect_port(ap: AppProcess, line: str):
    """Follows the port the app says it listens on when it ignored $PORT/{PORT} (e.g. Vite's 5173)."""
    m = LISTEN_RE.search(ANSI_RE.sub("", line))
    if not m or ap.port_detected:
        return
    port = int(m.group(1))
    if port != ap.port and port not in RESERVED_PORTS and 1024 <= port <= 65535:
        ap.log(f"[harness] o app está escutando na porta {port} (não na {ap.port}); preview ajustado", "sys")
        ap.port = port
    ap.port_detected = True


async def _pump(ap: AppProcess):
    assert ap.proc and ap.proc.stdout
    async for raw in ap.proc.stdout:
        line = raw.decode("utf-8", "replace").rstrip("\n")
        ap.log(line)
        if ap.kind == "app":
            _detect_port(ap, line)
    code = await ap.proc.wait()
    ap.exit_code = code
    if ap.status != "stopped":
        ap.status = "exited" if code == 0 else "crashed"
    ap.log(f"[harness] processo terminou com código {code}", "sys")


async def _watchdog(ap: AppProcess):
    await asyncio.sleep(MAX_LIFETIME)
    if ap.status == "running":
        ap.log("[harness] tempo máximo de execução atingido (1h) — encerrando", "sys")
        await stop(ap.id)


async def start(project_id: str, folder: str, command: str, profile: str, kind: str = "app") -> tuple[Optional[AppProcess], str]:
    if profile == "safe":
        return None, "Rodar apps exige o perfil Build (o perfil Safe é somente leitura)"
    if sum(p.status in ("starting", "running") for p in REGISTRY.values()) >= MAX_RUNNING:
        return None, f"Limite de {MAX_RUNNING} apps simultâneos atingido. Pare um processo antes."
    current = running_for(project_id) if kind == "app" else None
    if current:
        await stop(current.id)
    port = _alloc_port()
    if not port:
        return None, "Nenhuma porta livre no intervalo 4100-4199"
    cmd = command.replace("{PORT}", str(port))
    argv, reason = check_command(folder, cmd, profile)
    if reason:
        return None, reason
    ap = AppProcess(project_id, folder, cmd, port, profile, kind)
    env = {**sandbox_env(folder, profile), "PORT": str(port), "HOST": "127.0.0.1"}
    try:
        ap.proc = await asyncio.create_subprocess_exec(
            *argv, cwd=str(project_root(folder)), env=env, stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT, preexec_fn=preexec(build_limits),
        )
    except FileNotFoundError:
        return None, f"Executável não encontrado: {argv[0]}"
    ap.status = "running"
    ap.log(f"[harness] $ {cmd}  (porta {port}, perfil {profile})", "sys")
    REGISTRY[ap.id] = ap
    asyncio.create_task(_pump(ap))
    asyncio.create_task(_watchdog(ap))
    return ap, ""


async def stop(pid: str) -> Optional[AppProcess]:
    ap = REGISTRY.get(pid)
    if not ap or not ap.proc or ap.status not in ("starting", "running"):
        return ap
    ap.status = "stopped"
    for sig, wait in ((signal.SIGTERM, 5), (getattr(signal, "SIGKILL", 9), 2)):
        try:
            kill_tree(ap.proc.pid, sig)
        except ProcessLookupError:
            break
        try:
            await asyncio.wait_for(ap.proc.wait(), timeout=wait)
            break
        except asyncio.TimeoutError:
            continue
    ap.log("[harness] processo parado pelo usuário", "sys")
    return ap


async def health(ap: AppProcess) -> dict:
    if ap.status != "running":
        return {"ok": False, "status": ap.status, "detail": f"Processo {ap.status}", "port": ap.port}
    try:
        async with httpx.AsyncClient(timeout=3) as c:
            r = await c.get(f"http://127.0.0.1:{ap.port}/")
        ok = r.status_code < 500
        detail = "Respondendo" if ok else f"Servidor respondeu {r.status_code} — app provavelmente mal configurado"
        return {"ok": ok, "status": r.status_code, "detail": detail, "port": ap.port}
    except httpx.HTTPError:
        return {"ok": False, "status": None, "port": ap.port,
                "detail": f"Sem resposta na porta {ap.port} — o comando rodou mas o app não está escutando (verifique se usa $PORT / {{PORT}})"}


async def stop_all():
    for pid in list(REGISTRY):
        await stop(pid)
