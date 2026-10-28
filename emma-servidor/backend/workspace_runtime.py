"""Workspace Runtime: executes file ops and commands inside the sandboxed root. Never decides."""
import asyncio
import hashlib
import logging
import os
import shutil
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

from fastapi import HTTPException

IS_WIN = os.name == "nt"  # the PC app on Windows: no POSIX limits/sessions; commands still run in the project folder
if not IS_WIN:
    import resource

ROOT = Path(os.environ["WORKSPACE_ROOT"]).resolve()
ROOT.mkdir(parents=True, exist_ok=True)

IGNORED_DIRS = {"node_modules", ".git", "__pycache__", ".venv", "venv", "dist", "build", ".next", ".cache"}
MAX_READ = 1_000_000
MAX_OUTPUT = 64_000
FOLDER_RE = re.compile(r"^[a-zA-Z0-9][a-zA-Z0-9._-]{0,63}$")

SANDBOX_UID = 65534
log = logging.getLogger("workspace_runtime")
INLINE_EVAL = {"-c", "-e", "--eval", "-p", "--print", "-i", "--interactive"}


def own(path: Path):
    if not DROP_PRIVILEGES:
        return
    try:
        os.chown(path, SANDBOX_UID, SANDBOX_UID)
    except OSError:
        pass


def own_tree(base: Path):
    if not DROP_PRIVILEGES:
        return
    own(base)
    for dirpath, dirnames, filenames in os.walk(base):
        for n in dirnames + filenames:
            try:
                os.lchown(os.path.join(dirpath, n), SANDBOX_UID, SANDBOX_UID)
            except OSError:
                pass


SAFE_COMMANDS = ["ls", "cat", "head", "tail", "grep", "find", "wc", "pwd", "echo", "tree", "du", "file", "stat", "git", "diff", "sort", "uniq"]
HOME_DIR = ROOT / ".harness" / "home"
HOME_DIR.mkdir(parents=True, exist_ok=True)
LOCAL_BIN = (".venv/bin/", "node_modules/.bin/")

PROFILES = {
    "safe": {
        "enabled": True,
        "label": "Safe",
        "description": "Somente leitura. Sem rede, sem escrita, sem instalação.",
        "network": False,
        "timeout": 15,
        "commands": SAFE_COMMANDS,
    },
    "build": {
        "enabled": True,
        "label": "Build",
        "description": "Leitura + build/run (node, npm, yarn, python, pip em .venv). Rede liberada para baixar pacotes; scripts de instalação desativados; pip só wheels.",
        "network": True,
        "timeout": 300,
        "commands": SAFE_COMMANDS + ["node", "npm", "yarn", "pnpm", "python3", "python", "pip", "pip3", "make", "tsc", "vite"],
    },
    "full": {
        "enabled": True,
        "label": "Full",
        "description": "Tudo do Build + a Lumna usa ferramentas por conta própria (web, celular, lembretes, lâmpadas, PC, terminal). "
                       "Ações com efeito pedem sua confirmação. No computador com Docker ou bubblewrap, o terminal aceita qualquer comando, "
                       "isolado só com a pasta do projeto.",
        "network": True,
        "timeout": 300,
        "commands": SAFE_COMMANDS + ["node", "npm", "yarn", "pnpm", "python3", "python", "pip", "pip3", "make", "tsc", "vite"],
    },
}

DENYLIST = {"rm", "sudo", "su", "curl", "wget", "chmod", "chown", "mv", "dd", "ssh", "scp", "nc", "ncat", "bash", "sh", "zsh", "python", "python3", "node", "kill", "pkill", "mkfs", "mount", "eval", "exec"}
RUNTIMES = {"python", "python3", "node"}
GIT_READONLY = {"status", "log", "diff", "show", "branch", "ls-files", "blame"}
FIND_FORBIDDEN = {"-exec", "-execdir", "-delete", "-ok", "-okdir", "-fprint", "-fprintf", "-fls"}
META = re.compile(r"[;&|<>`$\\\n]")


def sha(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8", "replace")).hexdigest()


def project_root(folder: str) -> Path:
    if not FOLDER_RE.match(folder or ""):
        raise HTTPException(400, "Nome de pasta inválido")
    p = (ROOT / folder).resolve()
    if p.parent != ROOT:
        raise HTTPException(403, "Fora da pasta raiz")
    return p


def safe_path(folder: str, rel: str) -> Path:
    base = project_root(folder)
    rel = (rel or "").strip().lstrip("/")
    target = (base / rel).resolve()
    if target != base and base not in target.parents:
        raise HTTPException(403, "Acesso negado: caminho fora da pasta do projeto")
    return target


def is_binary(data: bytes) -> bool:
    return b"\x00" in data[:8192]


def tree(folder: str, max_entries: int = 3000) -> list[dict]:
    base = project_root(folder)
    count = 0

    def walk(d: Path, depth: int) -> list[dict]:
        nonlocal count
        items = []
        try:
            entries = sorted(d.iterdir(), key=lambda e: (not e.is_dir(), e.name.lower()))
        except OSError:
            return items
        for e in entries:
            if count >= max_entries or e.is_symlink():
                continue
            count += 1
            rel = str(e.relative_to(base))
            if e.is_dir():
                node = {"name": e.name, "path": rel, "type": "dir", "ignored": e.name in IGNORED_DIRS}
                node["children"] = [] if (e.name in IGNORED_DIRS or depth > 8) else walk(e, depth + 1)
                items.append(node)
            else:
                items.append({"name": e.name, "path": rel, "type": "file", "size": e.stat().st_size})
        return items

    return walk(base, 0)


def read_file(folder: str, rel: str) -> dict:
    p = safe_path(folder, rel)
    if not p.is_file():
        raise HTTPException(404, "Arquivo não encontrado")
    size = p.stat().st_size
    if size > MAX_READ:
        return {"path": rel, "binary": False, "too_large": True, "size": size, "content": "", "hash": ""}
    data = p.read_bytes()
    if is_binary(data):
        return {"path": rel, "binary": True, "too_large": False, "size": size, "content": "", "hash": ""}
    text = data.decode("utf-8", "replace")
    return {"path": rel, "binary": False, "too_large": False, "size": size, "content": text, "hash": sha(text)}


def current_hash(folder: str, rel: str) -> str:
    p = safe_path(folder, rel)
    if not p.is_file():
        return ""
    return sha(p.read_bytes().decode("utf-8", "replace"))


def write_file(folder: str, rel: str, content: str):
    write_bytes(folder, rel, content.encode("utf-8"))


def write_bytes(folder: str, rel: str, data: bytes):
    p = safe_path(folder, rel)
    if p == project_root(folder) or p.is_dir():
        raise HTTPException(400, "Caminho inválido")
    base = project_root(folder)
    missing = [d for d in [p.parent, *p.parent.parents] if base in d.parents and not d.exists()]
    p.parent.mkdir(parents=True, exist_ok=True)
    for d in missing:
        own(d)
    p.write_bytes(data)
    own(p)


def delete_file(folder: str, rel: str):
    p = safe_path(folder, rel)
    if not p.is_file():
        raise HTTPException(404, "Arquivo não encontrado")
    p.unlink()


def check_command(folder: str, command: str, profile: str) -> tuple[list[str], str]:
    prof = PROFILES.get(profile)
    if not prof or not prof["enabled"]:
        return [], f"Perfil '{profile}' indisponível no V1"
    if not command.strip():
        return [], "Comando vazio"
    if META.search(command):
        return [], "Operadores de shell (; | & > < $ `) não são permitidos neste perfil (no Full, com Docker ou bubblewrap no computador, são)"
    try:
        argv = shlex.split(command)
    except ValueError as e:
        return [], f"Comando inválido: {e}"
    prog = argv[0]
    local = profile != "safe" and prog.startswith(LOCAL_BIN) and ".." not in prog
    denied = DENYLIST - RUNTIMES if profile != "safe" else DENYLIST
    if ("/" in prog and not local) or prog in denied:
        return [], f"'{prog}' está na denylist"
    if not local and prog not in prof["commands"]:
        return [], f"'{prog}' não está na allowlist do perfil {prof['label']}"
    if (prog in RUNTIMES or local) and any(a in INLINE_EVAL or a.startswith("--eval=") or a.startswith("--print=") for a in argv[1:]):
        return [], "Execução inline de código (-c/-e/--eval) não é permitida. Crie um arquivo via proposta aprovada e execute-o."
    if prog in ("npm", "yarn", "pnpm") and len(argv) > 1 and argv[1] in ("exec", "dlx", "x", "create"):
        return [], f"'{prog} {argv[1]}' baixa e executa pacotes arbitrários — não permitido"
    if prog == "git" and (len(argv) < 2 or argv[1] not in GIT_READONLY):
        return [], "Apenas subcomandos git de leitura: " + ", ".join(sorted(GIT_READONLY))
    if prog == "find" and FIND_FORBIDDEN.intersection(argv):
        return [], "find com ações de execução/escrita não é permitido"
    base = project_root(folder)
    for a in argv[1:]:
        if a.startswith("-"):
            continue
        if a.startswith("/") or ".." in a or a.startswith("~"):
            t = (base / a.lstrip("/")).resolve() if not a.startswith("/") else Path(a).resolve()
            if t != base and base not in t.parents:
                return [], f"Argumento fora da pasta do projeto: {a}"
    return argv, ""


def _switch_user():
    os.setgroups([])
    os.setgid(SANDBOX_UID)
    os.setuid(SANDBOX_UID)


def _probe_privilege_drop() -> bool:
    """Whether child processes can run as SANDBOX_UID. Needs real root; a normal user, Termux or
    PRoot without -0 cannot switch users, so commands run as the backend's own user instead.
    SANDBOX_DROP_PRIVILEGES=true|false overrides the probe."""
    mode = os.environ.get("SANDBOX_DROP_PRIVILEGES", "auto").strip().lower()
    if mode in ("1", "true", "yes", "on"):
        return True
    if IS_WIN or mode in ("0", "false", "no", "off") or os.geteuid() != 0:
        return False
    try:
        subprocess.run([sys.executable, "-c", ""], preexec_fn=_switch_user, check=True, timeout=15,
                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return True
    except (OSError, subprocess.SubprocessError):
        return False


DROP_PRIVILEGES = _probe_privilege_drop()
if not DROP_PRIVILEGES:
    log.warning("Sandbox: sem troca para o uid %s (backend não roda como root real); comandos rodam como o "
                "usuário atual, mantendo os limites de CPU/memória/escrita.", SANDBOX_UID)


def _drop_privileges():
    os.setsid()
    if DROP_PRIVILEGES:
        _switch_user()


def _safe_limits():
    resource.setrlimit(resource.RLIMIT_CPU, (10, 10))
    resource.setrlimit(resource.RLIMIT_AS, (512 * 1024 * 1024, 512 * 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_FSIZE, (0, 0))
    _drop_privileges()


def build_limits():
    resource.setrlimit(resource.RLIMIT_FSIZE, (500 * 1024 * 1024, 500 * 1024 * 1024))
    _drop_privileges()


def preexec(fn):
    """The POSIX limits to apply in the child (none on Windows, where preexec_fn doesn't exist)."""
    return None if IS_WIN else fn


def kill_tree(pid: int, sig: int = 9) -> None:
    """Stops a command and what it started (its process group; on Windows, its process tree)."""
    if IS_WIN:
        subprocess.run(["taskkill", "/pid", str(pid), "/t", "/f"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        os.killpg(pid, sig)


def sandbox_env(folder: str, profile: str) -> dict:
    cwd = project_root(folder)
    env = {"LANG": "C.UTF-8", "GIT_PAGER": "cat", "PAGER": "cat", "CI": "1", "BROWSER": "none"}
    if IS_WIN:  # keep Windows' own PATH and system variables, or nothing would start
        return {**os.environ, **env, "HOME": str(cwd if profile == "safe" else HOME_DIR)}
    if profile == "safe":
        return {**env, "PATH": "/usr/bin:/bin", "HOME": str(cwd)}
    return {
        **env,
        "PATH": f"{cwd}/.venv/bin:{cwd}/node_modules/.bin:/usr/local/bin:/usr/bin:/bin",
        "HOME": str(HOME_DIR),
        "npm_config_ignore_scripts": "true",
        "YARN_IGNORE_SCRIPTS": "true",
        "npm_config_fund": "false",
        "npm_config_audit": "false",
        "PIP_ONLY_BINARY": ":all:",
        "PIP_REQUIRE_VIRTUALENV": "true",
        "PIP_DISABLE_PIP_VERSION_CHECK": "1",
    }


async def run_command(folder: str, command: str, profile: str) -> dict:
    if profile == "full":
        import isolated_shell  # noqa: PLC0415 (it imports build_limits from here)
        if await isolated_shell.detect():
            # Full on a computer with a sandbox: any command, isolated with only the project's folder.
            return await isolated_shell.run(project_root(folder), command, PROFILES["full"]["timeout"])
    argv, reason = check_command(folder, command, profile)
    if reason:
        return {"ok": False, "denied": True, "reason": reason, "exit_code": None, "stdout": "", "stderr": "", "duration_ms": 0}
    cwd = project_root(folder)
    start = time.monotonic()
    proc = await asyncio.create_subprocess_exec(
        *argv, cwd=str(cwd), env=sandbox_env(folder, profile), stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE,
        stdin=asyncio.subprocess.DEVNULL, preexec_fn=preexec(_safe_limits if profile == "safe" else build_limits),
    )
    timed_out = False
    try:
        out, err = await asyncio.wait_for(proc.communicate(), timeout=PROFILES[profile]["timeout"])
    except asyncio.TimeoutError:
        timed_out = True
        try:
            kill_tree(proc.pid, 9)
        except ProcessLookupError:
            pass
        out, err = await proc.communicate()
    dur = int((time.monotonic() - start) * 1000)
    return {
        "ok": proc.returncode == 0 and not timed_out,
        "denied": False,
        "reason": "Timeout: processo encerrado" if timed_out else "",
        "exit_code": proc.returncode,
        "stdout": out.decode("utf-8", "replace")[:MAX_OUTPUT],
        "stderr": err.decode("utf-8", "replace")[:MAX_OUTPUT],
        "duration_ms": dur,
    }


def _entry(folder: str, rel: str) -> Path:
    """An existing file/folder inside the project, never the project root itself."""
    p = safe_path(folder, rel)
    if p == project_root(folder):
        raise HTTPException(400, "Operação não permitida na raiz do projeto")
    if not p.exists() and not p.is_symlink():
        raise HTTPException(404, "Arquivo ou pasta não encontrado")
    return p


def create_entry(folder: str, rel: str, is_dir: bool):
    p = safe_path(folder, rel)
    if p == project_root(folder) or not Path(rel.strip().strip("/")).name:
        raise HTTPException(400, "Nome inválido")
    if p.exists():
        raise HTTPException(409, "Já existe um arquivo ou pasta com esse nome")
    if is_dir:
        base = project_root(folder)
        missing = [d for d in [p, *p.parents] if base in d.parents and not d.exists()]
        p.mkdir(parents=True)
        for d in missing:
            own(d)
    else:
        write_bytes(folder, rel, b"")


def move_entry(folder: str, src: str, dst: str):
    a = _entry(folder, src)
    b = safe_path(folder, dst)
    if b == project_root(folder):
        raise HTTPException(400, "Destino inválido")
    if a.is_dir() and (b == a or a in b.parents):
        raise HTTPException(400, "Não dá para mover uma pasta para dentro dela mesma")
    if b.exists():
        raise HTTPException(409, "Já existe um arquivo ou pasta no destino")
    if not b.parent.is_dir():
        raise HTTPException(400, "A pasta de destino não existe")
    a.rename(b)


def remove_entry(folder: str, rel: str):
    p = _entry(folder, rel)
    if p.is_dir() and not p.is_symlink():
        shutil.rmtree(p)
    else:
        p.unlink()


def zip_dir(folder: str, rel: str, out: Path):
    """Zips a project folder (symlinks and ignored dirs like node_modules left out)."""
    import zipfile

    d = _entry(folder, rel)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(d.rglob("*")):
            parts = f.relative_to(d).parts
            if f.is_symlink() or any(x in IGNORED_DIRS for x in parts) or not f.is_file():
                continue
            z.write(f, str(Path(d.name, *parts)))
