"""The terminal the Lumna can read (Modo Full, G9a): "Emma, o que deu errado no build?".

The workspace terminal's output used to go to the screen only (the audit log keeps the command and its
exit code, not the text). Now each project keeps its last runs in the `terminal_log` collection: the
commands the user typed, the ones approved in the queue, and (live) the log of the app it is running.
The tool read_terminal hands them to the model as untrusted content (a package can print anything),
so a later action in the same answer needs the user's OK. She reads; running something stays a
proposal the user approves.
"""
import re
from contextvars import ContextVar
from typing import Optional

from core import db, now_iso
from tools import REGISTRY, Outcome, Tool

KEEP = 30           # runs kept per project
OUTPUT_TAIL = 8000  # characters kept per run (the end: where the errors are)
MAX_FOR_MODEL = 14000
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07]*\x07")
CURRENT_PROJECT: ContextVar[Optional[str]] = ContextVar("current_project", default=None)  # the chat's project


def _clean(text: str) -> str:
    return ANSI_RE.sub("", text or "").replace("\r\n", "\n").replace("\r", "\n")


async def record(project_id: str, command: str, origin: str, result: dict) -> None:
    """One finished command (origin: "user" typed it, "approved" from the queue)."""
    out = _clean(result.get("stdout", ""))
    err = _clean(result.get("stderr", ""))
    text = result.get("reason") if result.get("denied") else (out + ("\n" + err if err.strip() else "")).strip()
    await db.terminal_log.insert_one({
        "project_id": project_id, "command": command, "origin": origin, "at": now_iso(),
        "exit_code": result.get("exit_code"), "denied": bool(result.get("denied")), "timed_out": "Timeout" in (result.get("reason") or ""),
        "output": (text or "")[-OUTPUT_TAIL:], "cut": len(text or "") > OUTPUT_TAIL,
    })
    old = await db.terminal_log.find({"project_id": project_id}).sort("at", -1).to_list(500)
    for d in old[KEEP:]:
        await db.terminal_log.delete_one({"_id": d["_id"]})


def _app_log(project_id: str, lines: int) -> str:
    """The project's app (dev server) started from the LUMNA: its latest log lines."""
    import processes
    procs = [p for p in processes.REGISTRY.values() if p.project_id == project_id and p.kind == "app"]
    if not procs:
        return ""
    p = procs[-1]
    state = {"running": "rodando", "starting": "iniciando"}.get(p.status, f"parado (código {p.exit_code})")
    tail = [_clean(x["line"]) for x in list(p.logs)[-lines:]]
    return f"### App: `{p.command}` — {state}, porta {p.port}, {p.errors} linha(s) com erro\n" + "\n".join(tail)


async def read_terminal(args: dict) -> Outcome:
    project_id = CURRENT_PROJECT.get()
    if not project_id:
        return Outcome("Esta conversa não é de um projeto, então não há terminal para ler.", "Sem projeto: nada a ler", status="error")
    try:
        runs = max(1, min(int(args.get("runs") or 3), 10))
    except (TypeError, ValueError):
        runs = 3
    docs = await db.terminal_log.find({"project_id": project_id}).sort("at", -1).to_list(runs)
    parts = []
    for d in reversed(docs):
        status = ("bloqueado pela política: " if d["denied"] else "tempo esgotado, " if d.get("timed_out") else "") + (
            f"código de saída {d['exit_code']}" if d.get("exit_code") is not None else "")
        cut = "(… início cortado)\n" if d.get("cut") else ""
        parts.append(f"### $ {d['command']}  [{d['at'][:16].replace('T', ' ')} UTC · {status or 'sem código'} · "
                     f"{'digitado por você' if d['origin'] == 'user' else 'aprovado na fila'}]\n{cut}{d['output'] or '(sem saída)'}")
    app = _app_log(project_id, 80)
    if app:
        parts.append(app)
    if not parts:
        return Outcome("O terminal deste projeto ainda não tem nada: nenhum comando rodou por aqui.", "Terminal vazio")
    text = "\n\n".join(parts)
    if len(text) > MAX_FOR_MODEL:
        text = "(… mais antigo cortado)\n" + text[-MAX_FOR_MODEL:]
    failed = sum(1 for d in docs if d["denied"] or (d.get("exit_code") not in (0, None)))
    summary = f"Leu o terminal: {len(docs)} comando(s)" + (f", {failed} com erro" if failed else "") + (" + log do app" if app else "")
    return Outcome(text, summary, untrusted=True)


REGISTRY["read_terminal"] = Tool(
    "read_terminal", "Ler o terminal",
    "Lê as últimas saídas do terminal deste projeto (os comandos rodados e o log do app em execução) para explicar erros e "
    "sugerir o próximo passo. Não executa nada: para rodar algo, proponha o comando para o usuário aprovar.",
    {"type": "object", "properties": {"runs": {"type": "integer", "description": "Quantos dos últimos comandos ler (1-10, padrão 3)"}}, "required": []},
    read_terminal,
)
