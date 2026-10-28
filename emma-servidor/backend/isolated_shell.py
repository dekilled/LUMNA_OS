"""The Full profile's shell (G9b, computer only): any command, with internet, locked in a sandbox.

With Full on and a sandbox available, terminal commands (typed by the user or approved in the queue)
skip the command allowlist and run isolated, seeing only the project's folder (at /workspace):
- Docker: a throwaway container per command (LUMNA_SHELL_IMAGE, default node:22-bookworm, which has
  node, npm, python3, git, make), CPU/memory/process limits, read-only root, no capabilities, the
  project's owner as user. The LUMNA's data (database, keys, other projects) is never mounted.
- bubblewrap (Linux without Docker): new namespaces sharing only the network, the system's /usr, /bin,
  /lib and /etc read-only, the project at /workspace and an empty /tmp as home.
- neither: the Full profile keeps the Build allowlist (status() says why).
Never on the phone (the Android app's backend). LUMNA_ISOLATED_SHELL=off|docker|bwrap forces a choice.
"""
import asyncio
import logging
import os
import shutil
import time
import uuid
from pathlib import Path
from typing import Optional

log = logging.getLogger("isolated_shell")
IMAGE = os.environ.get("LUMNA_SHELL_IMAGE", "node:22-bookworm")
MAX_COMMAND = 4000
_cache: dict = {"at": 0.0, "value": None}
_pulling: dict = {"task": None, "error": ""}


async def _ok(*cmd: str, timeout: float = 8) -> bool:
    try:
        proc = await asyncio.create_subprocess_exec(*cmd, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        return await asyncio.wait_for(proc.wait(), timeout) == 0
    except (OSError, asyncio.TimeoutError):
        return False


async def detect() -> Optional[str]:
    """"docker", "bwrap" or None (cached for a minute)."""
    if time.monotonic() - _cache["at"] < 60:
        return _cache["value"]
    choice = os.environ.get("LUMNA_ISOLATED_SHELL", "").strip().lower()
    value = None
    if choice != "off" and not os.environ.get("HARNESS_BOOT_ID"):  # never on the phone
        if choice in ("", "docker") and shutil.which("docker") and await _ok("docker", "info", "--format", "{{.ServerVersion}}"):
            value = "docker"
        elif choice in ("", "bwrap") and shutil.which("bwrap") and await _ok(
                "bwrap", "--unshare-all", "--ro-bind", "/usr", "/usr", "--ro-bind-try", "/bin", "/bin", "--ro-bind-try", "/lib", "/lib",
                "--ro-bind-try", "/lib64", "/lib64", "--proc", "/proc", "--dev", "/dev", "true"):
            value = "bwrap"
    _cache.update(at=time.monotonic(), value=value)
    return value


async def status() -> dict:
    """For the settings screen."""
    backend = await detect()
    if backend == "docker":
        text = f"Qualquer comando, com internet, num container Docker descartável ({IMAGE}) que só enxerga a pasta do projeto."
        if _pulling["task"] and not _pulling["task"].done():
            text += " Baixando a imagem…"
    elif backend == "bwrap":
        text = "Qualquer comando, com internet, isolado pelo bubblewrap: só a pasta do projeto e os programas do sistema (somente leitura)."
    elif os.environ.get("HARNESS_BOOT_ID"):
        text = "No celular o Full usa a mesma lista de comandos do Build."
    else:
        text = "Sem Docker nem bubblewrap neste computador: o Full usa a lista de comandos do Build. Instale um deles para liberar o shell isolado."
    return {"backend": backend, "image": IMAGE if backend == "docker" else None, "text": text}


def _result(ok: bool, code: Optional[int], out: bytes, err: bytes, ms: int, reason: str = "", limit: int = 64_000) -> dict:
    return {"ok": ok, "denied": False, "reason": reason, "exit_code": code, "duration_ms": ms, "isolated": True,
            "stdout": out.decode("utf-8", "replace")[:limit], "stderr": err.decode("utf-8", "replace")[:limit]}


async def _image_ready() -> Optional[str]:
    """None when the image is here; otherwise starts pulling it and says so."""
    if await _ok("docker", "image", "inspect", IMAGE, timeout=15):
        return None
    if not _pulling["task"] or _pulling["task"].done():
        async def pull():
            proc = await asyncio.create_subprocess_exec("docker", "pull", IMAGE, stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.PIPE)
            _, err = await proc.communicate()
            _pulling["error"] = err.decode(errors="replace")[-300:] if proc.returncode else ""
            log.info("imagem do shell isolado %s: %s", IMAGE, "pronta" if not proc.returncode else f"falhou: {_pulling['error']}")
        _pulling["task"] = asyncio.create_task(pull())
    extra = f" (a última tentativa falhou: {_pulling['error']})" if _pulling["error"] else ""
    return f"Preparando o shell isolado: baixando a imagem {IMAGE} pela primeira vez. Tente de novo em alguns minutos.{extra}"


async def run(project: Path, command: str, timeout: int) -> dict:
    """Runs one command in the sandbox; the same result shape as workspace_runtime.run_command."""
    command = command.strip()
    if not command:
        return {"ok": False, "denied": True, "reason": "Comando vazio", "exit_code": None, "stdout": "", "stderr": "", "duration_ms": 0}
    if len(command) > MAX_COMMAND:
        return {"ok": False, "denied": True, "reason": "Comando longo demais", "exit_code": None, "stdout": "", "stderr": "", "duration_ms": 0}
    backend = await detect()
    env_vars = {"HOME": "/tmp", "LANG": "C.UTF-8", "CI": "1", "GIT_PAGER": "cat", "PAGER": "cat", "BROWSER": "none",
                "PATH": "/workspace/.venv/bin:/workspace/node_modules/.bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",
                "npm_config_update_notifier": "false", "npm_config_fund": "false"}
    name = f"lumna-sh-{uuid.uuid4().hex[:10]}"
    if backend == "docker":
        if (wait := await _image_ready()):
            return {"ok": False, "denied": True, "reason": wait, "exit_code": None, "stdout": "", "stderr": "", "duration_ms": 0}
        argv = ["docker", "run", "--rm", "-i", "--name", name, "--network", "bridge",
                "--cpus", "2", "--memory", "2g", "--pids-limit", "512", "--read-only", "--tmpfs", "/tmp:exec,size=1g",
                "--security-opt", "no-new-privileges", "--cap-drop", "ALL",
                "-v", f"{project}:/workspace", "-w", "/workspace"]
        if hasattr(os, "getuid"):
            st = project.stat()
            argv += ["--user", f"{st.st_uid}:{st.st_gid}"]
        for k, v in env_vars.items():
            argv += ["-e", f"{k}={v}"]
        argv += [IMAGE, "sh", "-c", command]
        env = None
        preexec = None
    elif backend == "bwrap":
        argv = [shutil.which("bwrap") or "bwrap", "--die-with-parent", "--new-session", "--unshare-all", "--share-net",
                "--ro-bind", "/usr", "/usr", "--ro-bind-try", "/bin", "/bin", "--ro-bind-try", "/sbin", "/sbin",
                "--ro-bind-try", "/lib", "/lib", "--ro-bind-try", "/lib32", "/lib32", "--ro-bind-try", "/lib64", "/lib64",
                "--ro-bind", "/etc", "/etc", "--proc", "/proc", "--dev", "/dev", "--tmpfs", "/tmp",
                "--bind", str(project), "/workspace", "--chdir", "/workspace", "sh", "-c", command]
        env = env_vars
        from workspace_runtime import build_limits
        preexec = build_limits
    else:
        return {"ok": False, "denied": True, "reason": "Shell isolado indisponível", "exit_code": None, "stdout": "", "stderr": "", "duration_ms": 0}

    start = time.monotonic()
    proc = await asyncio.create_subprocess_exec(*argv, stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.PIPE,
                                                stderr=asyncio.subprocess.PIPE, env=env, preexec_fn=preexec)
    try:
        out, err = await asyncio.wait_for(proc.communicate(), timeout)
        return _result(proc.returncode == 0, proc.returncode, out, err, int((time.monotonic() - start) * 1000))
    except asyncio.TimeoutError:
        if backend == "docker":
            await _ok("docker", "kill", name, timeout=15)
        try:
            proc.kill()
        except ProcessLookupError:
            pass
        out, err = await proc.communicate()
        return _result(False, proc.returncode, out, err, int((time.monotonic() - start) * 1000), "Timeout: processo encerrado")
