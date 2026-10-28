import asyncio
import base64
import json
import logging
import re
import unicodedata
import uuid
from collections import deque
from contextlib import aclosing
from typing import Optional

from fastapi import APIRouter, HTTPException
from fastapi.responses import StreamingResponse
from pydantic import BaseModel, Field

import quick
import pc_actions  # noqa: F401  registers the PC tools (when the backend runs on a computer)
import reminders  # registers the reminder tools
import yeelight  # registers the lamp tools
import isolated_shell
import modes
import nsfw_profile
import terminal_log  # registers read_terminal
import tools as lumna_tools
import workspace_runtime as wr
from core import Action, Memory, cut_session, Message, Project, Session, Skill, audit, db, get_settings, now_iso, oid
from llm import why_failed, MODE_TASK, complete, resolve, stream_chat, stream_turn
from media import ATTACH_DIR, attachment_text, load_attachment
from prompts import DEEP_RE, SCREEN_CONTEXT, VOICE_DEEP, VOICE_RULES, ACTION_RE, MEMORY_EXTRACT, build_system
from rag import retrieve

router = APIRouter(prefix="/api")
MODES = ("chat", "plan", "build", "workspace")


# ---- Sessions ----
class SessionIn(BaseModel):
    project_id: Optional[str] = None
    title: str = "Nova sessão"
    mode: str = "chat"
    assist_log: bool = False  # created by the assistant panel (see commands.py)


class SessionPatch(BaseModel):
    title: Optional[str] = None
    mode: Optional[str] = None
    system_prompt: Optional[str] = None


async def _session(sid: str) -> Session:
    d = await db.sessions.find_one({"_id": oid(sid)})
    s = Session.from_mongo(d) if await nsfw_profile.visible(d) else None  # the other profile's: as if it didn't exist
    if not s:
        raise HTTPException(404, "Sessão não encontrada")
    return s


@router.get("/sessions")
async def list_sessions(project_id: Optional[str] = None):
    q = {"project_id": project_id if project_id and project_id != "none" else None}
    docs = await db.sessions.find(await nsfw_profile.scope(q)).sort("updated_at", -1).to_list(400)
    # Commands from the assistant panel live in the command log (GET /api/commands), not here.
    return [Session.from_mongo(d).out() for d in docs if not d.get("assist_log")][:200]


@router.post("/sessions")
async def create_session(body: SessionIn):
    s = Session(**body.model_dump(), nsfw=await nsfw_profile.nsfw())
    res = await db.sessions.insert_one(s.to_mongo())
    s.id = str(res.inserted_id)
    return s.out()


@router.patch("/sessions/{sid}")
async def patch_session(sid: str, body: SessionPatch):
    await _session(sid)
    upd = {k: v for k, v in body.model_dump().items() if v is not None}
    await db.sessions.update_one({"_id": oid(sid)}, {"$set": upd})
    return (await _session(sid)).out()


@router.delete("/sessions/{sid}")
async def delete_session(sid: str):
    await _session(sid)
    await db.sessions.delete_one({"_id": oid(sid)})
    await db.messages.delete_many({"session_id": sid})
    return {"ok": True}


@router.get("/sessions/{sid}/messages")
async def list_messages(sid: str):
    await _session(sid)
    docs = await db.messages.find({"session_id": sid}).sort("created_at", 1).to_list(1000)
    return [Message.from_mongo(d).out() for d in docs]


# ---- Chat ----
class LumnaToolDef(BaseModel):
    """One action of the LUMNA OS shell, offered to the model as a tool for this answer."""
    name: str = Field(pattern=r"^lumna_[a-z0-9_]{1,60}$")
    label: str = Field(default="", max_length=80)
    description: str = Field(max_length=800)
    params: dict = Field(default_factory=dict)
    risk: str = Field(default="act", pattern=r"^(read|act)$")
    default: str = Field(default="ask", pattern=r"^(auto|ask|off)$")


class ChatIn(BaseModel):
    content: str = ""
    attachments: list[str] = Field(default=[], max_length=6)
    mode: str = "chat"
    mode_id: Optional[str] = Field(default=None, max_length=60)  # the action (modes.py) of the project's mode; without it, as before
    use_rag: bool = True
    skill_id: Optional[str] = None
    active_file: Optional[str] = None
    approved_plan: Optional[str] = Field(default=None, max_length=40)
    marker: Optional[str] = Field(default=None, pattern=r"^(plan|build|continue)$")  # sent by the app, not typed  # build turn: the Planner message whose plan was approved
    replace_from: Optional[str] = None  # last user message to replace (edit / retry / regenerate)
    client_tools: list[str] = Field(default=[], max_length=40)  # phone tools the Android app can run for this answer
    lumna_tools: list[LumnaToolDef] = Field(default=[], max_length=120)  # the LUMNA OS shell's actions (it runs them)
    client_now: str = Field(default="", max_length=80)  # the user's local date/time, for alarms and "amanhã"
    defer_launch: bool = False  # the page opens apps/links itself after the answer (see tools.LAUNCHERS)
    voice: bool = False  # spoken conversation (conversation screen, assistant panel): short, spoken answers
    # What the user is looking at (assistant panel, 👁 on): the screen's text and a screenshot (an uploaded attachment).
    screen: str = Field(default="", max_length=14000)
    screen_url: str = Field(default="", max_length=500)
    screen_image: Optional[str] = None


MAX_FILE_CONTEXT = 120_000


def file_blocks(files: list[dict]) -> str:
    """Attached documents as delimited blocks after the message, within a total character budget."""
    out, left = [], MAX_FILE_CONTEXT
    for d in files:
        name = re.sub(r'["<>\n]', "", d.get("name", "arquivo"))[:100]
        text = attachment_text(d["_id"])
        cut = text[:max(left, 0)]
        note = "" if len(cut) == len(text) else f"\n[... cortado: {len(text) - len(cut)} caracteres não enviados]"
        out.append(f'\n\n<<<ARQUIVO nome="{name}">>>\n{cut}{note}\n<<<FIM>>>')
        left -= len(cut)
    return "".join(out)


HISTORY_WINDOW = 16   # recent messages sent verbatim
SUMMARY_BATCH = 10    # older messages that trigger a summary update
SUMMARY_PROMPT = """Você mantém o resumo de uma conversa longa entre um usuário e a assistente Lumna.
Atualize o resumo existente incorporando as mensagens novas abaixo. Guarde o que importa para continuar a
conversa: fatos e dados citados, decisões, nomes, preferências do usuário, tarefas e perguntas em aberto,
trechos de código ou valores que foram combinados. Descarte saudações e repetições. Escreva em português,
em tópicos curtos, no máximo ~300 palavras. Responda só com o resumo."""
_summarizing: set = set()


async def update_summary(sid: str):
    """Folds the messages that left the history window into the session's running summary (after an
    answer, in the background; one at a time per session)."""
    if sid in _summarizing:
        return
    _summarizing.add(sid)
    try:
        session = await _session(sid)
        recent = await db.messages.find({"session_id": sid}).sort("created_at", -1).to_list(HISTORY_WINDOW)
        if len(recent) < HISTORY_WINDOW:
            return
        window_start = recent[-1]["created_at"]
        flt = {"session_id": sid, "created_at": {"$lt": window_start}}
        if session.summary_upto:
            flt["created_at"]["$gt"] = session.summary_upto
        older = await db.messages.find(flt).sort("created_at", 1).to_list(400)
        if len(older) < SUMMARY_BATCH:
            return
        who = {"user": "Usuário", "assistant": "Lumna"}
        convo = "\n\n".join(f"{who.get(m['role'], m['role'])}: {m['content'][:2500]}" for m in older if m.get("content"))
        provider, model = await resolve("chat")
        text = await complete(provider, model, [
            {"role": "system", "content": SUMMARY_PROMPT},
            {"role": "user", "content": f"Resumo atual:\n{session.summary or '(vazio)'}\n\nMensagens novas:\n{convo}"},
        ])
        if text.strip():
            await db.sessions.update_one({"_id": oid(sid)}, {"$set": {"summary": text.strip()[:6000], "summary_upto": older[-1]["created_at"]}})
    except Exception as e:  # a failed summary just waits for the next answer
        logging.getLogger("summary").warning("resumo da sessão %s falhou: %s", sid, e)
    finally:
        _summarizing.discard(sid)


_tasks: set = set()


def _background(coro):
    """Runs coro on its own (kept referenced until done)."""
    t = asyncio.get_running_loop().create_task(coro)
    _tasks.add(t)
    t.add_done_callback(_tasks.discard)


def sse(obj: dict) -> str:
    return f"data: {json.dumps(obj, ensure_ascii=False)}\n\n"


# ---- Tools (Modo Full): calls waiting for the user's OK, and endpoints that refused native tools ----
# The last answers' tool activity, for Settings → Sandbox → Diagnóstico (what was offered, what the
# model wrote, what ran). In memory only; message text is cut short.
TRACE: deque = deque(maxlen=15)
CONFIRM_TIMEOUT = 300
CLIENT_TIMEOUT = 45
LUMNA_TIMEOUT = 600  # the shell may install a program or wait for the user's password
_pending: dict[str, tuple[asyncio.Future, type]] = {}  # "sid:call id" -> (future, bool for the user's decision | dict for the phone's result)
_text_only: set = set()


class DecisionIn(BaseModel):
    call_id: str
    allow: bool


class ClientResultIn(BaseModel):
    call_id: str
    ok: bool
    summary: str = Field(default="", max_length=300)
    content: str = Field(default="", max_length=4000)


def _resolve(sid: str, call_id: str, value):
    fut, expects = _pending.get(f"{sid}:{call_id}", (None, None))
    if not fut or fut.done() or type(value) is not expects:
        raise HTTPException(404, "Nada aguardando esta resposta")
    fut.set_result(value)
    return {"ok": True}


@router.post("/sessions/{sid}/tool-decision")
async def tool_decision(sid: str, body: DecisionIn):
    return _resolve(sid, body.call_id, body.allow)


@router.post("/sessions/{sid}/tool-result")
async def tool_result(sid: str, body: ClientResultIn):
    """The Android app ran a phone tool (relayed by the page) and reports how it went."""
    return _resolve(sid, body.call_id, body.model_dump())


async def _wait(fut: asyncio.Future, timeout: int):
    """Waits for the user/phone, yielding SSE comments meanwhile so proxies keep the stream open."""
    waited = 0
    while not fut.done() and waited < timeout:
        try:
            await asyncio.wait_for(asyncio.shield(fut), min(15, timeout - waited))
        except asyncio.TimeoutError:
            waited += 15
            yield ": aguardando\n\n"


def _tool_note(m: dict) -> str:
    """Earlier tool use in the history, written as the calls themselves (```tool_code) plus their short
    result. A loose note ("[ferramentas usadas…]") taught the model that writing a note was acting."""
    out = []
    for t in m.get("tools") or []:
        args = ", ".join(f"{k}={v!r}" for k, v in (t.get("args") or {}).items())
        out.append(f"```tool_code\n{t['name']}({args[:200]})\n```\n→ {t.get('summary') or t.get('status', '')}")
    return "\n".join(out)


async def _auto_apply(project: Project, actions: list[dict], plan_msg: Optional[dict]) -> list[dict]:
    """What runs without asking: the files of an approved plan, and the commands the user allowed
    for good in this project ("Permitir sempre"). Everything else stays pending as before."""
    from routes_projects import execute_action
    paths = modes.plan_files((plan_msg or {}).get("plan_doc") or {})
    allowed = set(project.allowed_commands)
    out = []
    for d in actions:
        in_plan = d["type"] in ("edit", "delete") and modes.plan_match(paths, d["path"], project.folder) is not None
        go = d["status"] == "pending" and (in_plan or (d["type"] == "command" and d["command"] in allowed))
        if go:
            a = Action.from_mongo(await db.actions.find_one({"_id": oid(d["id"])}))
            a.auto = "always" if a.type == "command" else "plan"
            await db.actions.update_one({"_id": oid(a.id)}, {"$set": {"auto": a.auto}})
            await execute_action(a, project)
            d = a.out()
        out.append(d)
    return out


async def _record_build(plan_msg: dict, actions: list[dict], folder: str = "") -> dict:
    """The build card: what this turn wrote from the plan, what it proposed outside it, what is missing."""
    files = modes.plan_files(plan_msg["plan_doc"])
    applied = [a["path"] for a in actions if a.get("auto") == "plan" and a["status"] == "executed"]
    # The plan's names (a block may write "caido" for the plan's "caído"): what is done and what is missing.
    done = list(dict.fromkeys((plan_msg.get("plan_done") or []) + [modes.plan_match(files, p, folder) or p for p in applied]))
    await db.messages.update_one({"_id": plan_msg["_id"]}, {"$set": {"plan_done": done}})
    outside = [a["path"] for a in actions if a["type"] in ("edit", "delete") and modes.plan_match(files, a["path"], folder) is None]
    return {"plan_id": str(plan_msg["_id"]), "title": plan_msg["plan_doc"].get("title", ""), "applied": applied,
            "outside": outside, "missing": [p for p in files if p not in done]}


async def _create_actions(project: Project, session_id: str, message_id: str, content: str, kinds: Optional[set] = None) -> list[dict]:
    out = []
    for kind, path, body in ACTION_RE.findall(content):
        kind = kind.lower()
        if kinds is not None and kind not in kinds:
            continue  # the mode doesn't touch this (e.g. no terminal in "História")
        a = Action(project_id=project.id, session_id=session_id, message_id=message_id, type=kind)
        try:
            if kind in ("edit", "delete"):
                a.path = modes.clean_path(path)
                prefix = project.folder + "/"
                if a.path.startswith(prefix) and not wr.safe_path(project.folder, a.path).exists():
                    a.path = a.path[len(prefix):]
                cur = wr.read_file(project.folder, a.path) if wr.safe_path(project.folder, a.path).is_file() else None
                a.old_content = cur["content"] if cur else ""
                a.base_hash = cur["hash"] if cur else ""
                if kind == "edit":
                    a.new_content = body.rstrip("\n") + "\n"
            else:
                a.command = body.strip()
                profile = (await get_settings()).sandbox_profile
                if kind == "command" and profile == "full" and await isolated_shell.detect():
                    reason = ""  # Full with an isolated shell: any command (it still waits for approval)
                else:
                    _, reason = wr.check_command(project.folder, a.command.replace("{PORT}", "4100"), profile)
                if kind == "run" and profile == "safe":
                    reason = "RUN exige o perfil Build"
                if reason:
                    a.status, a.result = "blocked", f"Bloqueado pela política: {reason}"
        except HTTPException as e:
            a.status, a.result = "blocked", f"Bloqueado pela política: {e.detail}"
        res = await db.actions.insert_one(a.to_mongo())
        a.id = str(res.inserted_id)
        out.append(a.out())
    return out


@router.post("/sessions/{sid}/chat")
async def chat(sid: str, body: ChatIn):
    session = await _session(sid)
    if not body.content.strip() and not body.attachments:
        raise HTTPException(400, "Mensagem vazia")
    mode = body.mode if body.mode in MODES else "chat"
    # The action asked for, in the project's mode (modes.py): its base, instructions and files; the
    # mode's focus and tools. From the Workspace too: the open file goes along with a build action.
    picked = await modes.turn(body.mode_id, session.project_id, "build" if mode == "workspace" else mode) if body.mode_id else None
    recipe, mode_def = picked if picked else (None, None)
    in_workspace = mode == "workspace"
    if recipe:
        mode = recipe.engine
    use_rag = body.use_rag and (recipe is None or recipe.context)
    project = Project.from_mongo(await db.projects.find_one({"_id": oid(session.project_id)})) if session.project_id else None
    settings = await get_settings()
    task = "rag" if (mode == "chat" and use_rag and project) else MODE_TASK[mode]
    # The screen goes along only when the message needs it: a plain command ("abre o Spotify") doesn't.
    screen, screen_image = body.screen.strip(), body.screen_image
    if (screen or screen_image) and quick.match(body.content, set(lumna_tools.REGISTRY), []):
        screen, screen_image = "", None
    attach_ids = body.attachments + ([screen_image] if screen_image else [])
    docs = [await load_attachment(a) for a in attach_ids]
    images = [((ATTACH_DIR / d["_id"]).read_bytes(), d) for d in docs if d.get("type", "image") == "image"]
    files = [d for d in docs if d.get("type", "image") != "image"]
    # Turns with images go to the "vision" route when one is configured (the chat model may not see).
    vision = settings.routing.get("vision")
    if images and vision and vision.provider_id:
        task = "vision"
    provider, model = await resolve(task)

    if body.replace_from:
        await cut_session(sid, body.replace_from)
    history = await db.messages.find({"session_id": sid}).sort("created_at", -1).to_list(HISTORY_WINDOW)
    history.reverse()
    user_msg = Message(session_id=sid, role="user", content=body.content, marker=body.marker, mode=mode, mode_id=recipe.key if recipe else None, skill_id=body.skill_id, attachments=attach_ids)
    res = await db.messages.insert_one(user_msg.to_mongo())
    user_msg.id = str(res.inserted_id)

    memories, citations, active = [], [], None
    if project:
        if recipe is None or recipe.context:
            memories = [Memory.from_mongo(d) for d in await db.memories.find(await nsfw_profile.scope({"project_id": project.id})).sort("created_at", -1).to_list(40)]
        if use_rag and body.content.strip():
            citations = await retrieve(project.id, body.content, 6)
        if body.active_file and (mode in ("workspace", "build") or in_workspace):
            try:
                active = wr.read_file(project.folder, body.active_file)
            except HTTPException:
                active = None
    isolated = await isolated_shell.detect() if settings.sandbox_profile == "full" else None
    # The Emma of the current profile (SFW / NSFW: nsfw_profile.py); the settings object is per request.
    nsfw_profile.apply(settings, await nsfw_profile.nsfw())
    system = build_system(settings, project, session, mode, memories, citations, active,
                          {"key": settings.sandbox_profile, **wr.PROFILES[settings.sandbox_profile], "isolated": isolated})
    # Building an approved plan (modes.accept_plan): its files are written without asking, the rest waits.
    plan_msg = None
    if body.approved_plan and project and mode == "build":
        d = await db.messages.find_one({"_id": oid(body.approved_plan)})
        if d and d.get("plan_doc") and d.get("plan_doc_status") == "approved" and d.get("session_id") == sid:
            plan_msg = d
    can_build = bool(recipe and mode == "plan" and project and await modes.build_action(mode_def))
    # The Chat of a mode with a Planner passes the turn to it by itself (modes.HANDOFF_RULES).
    # (Unless the mode's "Trocar de ação sozinha" is off.)
    plan_key = await modes.action_with(mode_def, "plan") if recipe and mode == "chat" and project and mode_def.auto_switch else None
    if recipe and (extra := modes.prompt_extra(recipe, mode_def, mode, bool(project), can_build, bool(plan_key))):
        system += "\n\n" + extra
    if plan_msg:
        system += "\n\n" + modes.build_note(plan_msg["plan_doc"], plan_msg.get("plan_done") or [])
    # Spoken conversation: short answers unless this turn asks for detail; a ceiling so she never rambles.
    max_tokens = None
    if body.voice:
        deep = bool(re.search(DEEP_RE, unicodedata.normalize("NFKD", body.content.lower()).encode("ascii", "ignore").decode()))
        system += "\n\n" + VOICE_RULES + ("\n" + VOICE_DEEP if deep else "")
        max_tokens = 900 if deep else 320
    if screen:
        system += "\n\n" + SCREEN_CONTEXT.format(url=f"página/app: {body.screen_url[:300]}" if body.screen_url else "app aberto", text=screen.replace("</tela>", ""))
    # Only this turn's images go to the model; earlier ones are mentioned, not resent.
    past = [{"role": m["role"], "content": ((_tool_note(m) + "\n\n" if m.get("tools") else "") + m["content"]).strip()} for m in history]
    ids = [a for m in history for a in m.get("attachments") or []]
    if ids:
        kinds = {d["_id"]: d for d in await db.attachments.find({"_id": {"$in": ids}}).to_list(500)}
        for item, m in zip(past, history):
            notes = []
            for a in m.get("attachments") or []:
                d = kinds.get(a, {})
                if d.get("type", "image") != "image":
                    notes.append(f"[arquivo anexado: {d.get('name', 'arquivo')}]")
                else:
                    notes.append("[imagem gerada]" if m["role"] == "assistant" else "[imagem anexada]")
            item["content"] = (item["content"] + "\n" + " ".join(notes)).strip()
    current: str | list = (body.content or ("Veja o(s) arquivo(s) anexado(s)." if files else "")) + file_blocks(files)
    if images:
        current = [{"type": "text", "text": current or "Descreva a imagem."}] + [
            {"type": "image_url", "image_url": {"url": f"data:{doc['mime']};base64,{base64.b64encode(data).decode()}"}} for data, doc in images]
    shell_tools = lumna_tools.lumna_tools(body.lumna_tools)
    if settings.sandbox_profile == "full":
        tool_list = lumna_tools.enabled_tools(settings.tools, body.client_tools, shell_tools)
    else:
        # The LUMNA OS shell's actions run there, under its own permissions: they don't depend on this server's sandbox.
        tool_list = [t for t in shell_tools if t.default != "off"]
    if not project:
        tool_list = [t for t in tool_list if t.name != "read_terminal"]  # a project's terminal: none here
    if recipe:
        tool_list = [t for t in tool_list if modes.allowed_tool(mode_def, recipe, t.name, t.where)]
    # Lamp tools only when there is a lamp: every tool offered is read by the model on every message.
    if any(t.name in yeelight.LIGHT_TOOLS for t in tool_list) and not await db.devices.find_one({"kind": "yeelight"}):
        tool_list = [t for t in tool_list if t.name not in yeelight.LIGHT_TOOLS]
    names = {x.name for x in tool_list}
    # A plain command ("abre o Spotify") runs its tool right away; the model only confirms it.
    last_tools = next((m.get("tools") or [] for m in reversed(history) if m["role"] == "assistant"), [])
    direct = quick.match(body.content, names, last_tools) if names and not attach_ids else None
    proto = settings.tools.protocol
    route_key = (provider.id, model)
    state = {"native": bool(tool_list) and proto != "text" and (proto == "native" or route_key not in _text_only)}
    if tool_list:
        system += "\n\n" + lumna_tools.system_prompt(tool_list, state["native"], body.client_now)
    base_system = system.split("\n\nMODO FULL")[0]
    trace = {"at": now_iso(), "text": body.content[:80], "profile": settings.sandbox_profile, "client_tools": body.client_tools,
             "offered": sorted(names), "protocol": proto, "native": state["native"], "direct": direct[0] if direct else None,
             "provider": provider.name, "model": model, "rounds": [], "error": None}
    TRACE.appendleft(trace)
    logging.getLogger("tools").info("chat: perfil=%s ferramentas=%d (celular: %d) direto=%s modelo=%s", settings.sandbox_profile,
                                    len(names), len(body.client_tools), trace["direct"], model)
    msgs = [{"role": "system", "content": system}]
    if session.summary:
        msgs.append({"role": "system", "content": "Resumo do começo desta conversa (mensagens antigas que não estão mais no histórico abaixo):\n" + session.summary})
    msgs += past + [{"role": "user", "content": current}]

    upd = {"mode": mode, "mode_id": recipe.key if recipe else None, "updated_at": now_iso()}
    if session.assist_log and any(m["role"] == "user" for m in history):
        upd["assist_log"] = False  # a reply in the assistant panel: a real conversation, to the sidebar
    if session.title == "Nova sessão":
        upd["title"] = re.sub(r"\s+", " ", body.content)[:60] or (docs[0].get("name") if docs else "") or "Anexo"
    await db.sessions.update_one({"_id": oid(sid)}, {"$set": upd})

    async def gen():
        yield sse({"type": "meta", "user_message": user_msg.out(), "citations": citations, "model": model, "provider": provider.name, "task": task, "mode": mode, "mode_id": recipe.key if recipe else None, "memories_used": len(memories), "summary": bool(session.summary), "tools": [x.name for x in tool_list]})
        acc = {"text": "", "round": ""}
        trail: list[dict] = []
        tainted = bool(screen)  # what is on the screen came from outside: actions after it ask first

        def show(piece: str) -> Optional[str]:
            """A piece of visible answer; a new round after tools starts on its own paragraph."""
            if not piece:
                return None
            if not acc["round"] and acc["text"] and not acc["text"].endswith("\n"):
                piece = "\n\n" + piece
            acc["round"] += piece
            acc["text"] += piece
            return sse({"type": "delta", "content": piece})

        try:
            for rnd in range(lumna_tools.MAX_ROUNDS + 1):
                offer = bool(tool_list) and rnd < lumna_tools.MAX_ROUNDS
                if tool_list and rnd == lumna_tools.MAX_ROUNDS:
                    msgs.append({"role": "system", "content": "Limite de ferramentas desta resposta atingido. Responda agora com o que já tem."})
                acc["round"] = ""
                calls: list[lumna_tools.Call] = []
                native_round = written = False
                st = {"raw": "", "sent": 0}
                forced = rnd == 0 and direct is not None
                if forced:
                    calls, written = [lumna_tools.Call("direct", direct[0], direct[1])], True

                def scan(final: bool = False):
                    """Shows what can be shown of this round's text; a tool call written in it (any
                    format, see tools.find_call) is cut out and returned. Returns (events, call)."""
                    events, raw, sent = [], st["raw"], st["sent"]
                    fc = lumna_tools.find_call(raw, names) if offer else None
                    if fc:
                        name, args, bad, b, e = fc
                        if b < sent:  # part of it was already on screen (a bare line): take it back
                            cut = sent - b
                            acc["text"], acc["round"] = acc["text"][:-cut], acc["round"][:-cut]
                            events.append(sse({"type": "rewrite", "content": acc["text"]}))
                        elif (out := show(raw[sent:b])):
                            events.append(out)
                        st["raw"], st["sent"] = raw[:e], b
                        return events, lumna_tools.Call(f"call_{rnd}", name, args, bad)
                    stop = len(raw) if final or not offer else lumna_tools.held_back(raw, sent)
                    if (out := show(raw[sent:stop])):
                        events.append(out)
                    st["sent"] = max(sent, stop)
                    return events, None

                if offer and state["native"] and not forced:
                    for attempt in range(2):
                        try:
                            async with aclosing(stream_turn(provider, model, msgs, [x.spec() for x in tool_list], max_tokens)) as it:
                                async for kind, val in it:
                                    if kind == "text":
                                        st["raw"] += val
                                        events, wc = scan()
                                        for out in events:
                                            yield out
                                        if wc:
                                            calls, written = [wc], True
                                            break
                                    else:
                                        for n, c in enumerate(val):
                                            args, bad = lumna_tools.parse_args(c["arguments"])
                                            calls.append(lumna_tools.Call(c["id"] or f"call_{rnd}_{n}", c["name"], args, bad))
                            native_round = True
                            break
                        except (asyncio.CancelledError, GeneratorExit):
                            raise
                        except Exception as e:
                            why = why_failed(e)
                            trace.setdefault("retries", []).append(f"rodada {rnd}: {why} — {str(e)[:120]}")
                            if st["raw"]:
                                raise  # part of the answer is already on screen
                            if why == "transient" and attempt == 0:
                                # A network hiccup or a busy server: once more, the same way (it was being
                                # taken as "no native tools" and switched the whole route to text).
                                logging.getLogger("tools").info("%s/%s: falha passageira (%s); tentando de novo", provider.name, model, str(e)[:160])
                                await asyncio.sleep(1.5)
                                continue
                            if why not in ("unsupported", "bad_call") or proto == "native":
                                raise
                            if why == "unsupported":
                                # The endpoint/model refused native tools: use the text protocol from now on.
                                logging.getLogger("tools").info("%s/%s sem tools nativas (%s); usando protocolo de texto", provider.name, model, str(e)[:200])
                                _text_only.add(route_key)
                            else:
                                # The server couldn't read the call the model wrote: written calls, this answer only.
                                logging.getLogger("tools").info("%s/%s: chamada ilegível no servidor; protocolo de texto nesta resposta", provider.name, model)
                            state["native"] = False
                            msgs[0]["content"] = base_system + "\n\n" + lumna_tools.system_prompt(tool_list, False, body.client_now)
                            break
                if not native_round and not forced:
                    async with aclosing(stream_chat(provider, model, msgs, max_tokens)) as it:
                        async for piece in it:
                            st["raw"] += piece
                            events, wc = scan()
                            for out in events:
                                yield out
                            if wc:
                                calls, written = [wc], True
                                break
                if not written and not forced:
                    events, wc = scan(final=True)  # what was held back (or a call on the last line)
                    for out in events:
                        yield out
                    if wc and not calls:
                        calls, written = [wc], True
                raw = st["raw"]
                trace["rounds"].append({"native": native_round, "written": written, "forced": forced, "raw": raw[:400],
                                        "calls": [{"name": c.name, "args": c.args} for c in calls], "results": []})
                if not calls:
                    break

                # The model's turn, then one result per call.
                if native_round and not written:
                    msgs.append({"role": "assistant", "content": raw, "tool_calls": [
                        {"id": c.id, "type": "function", "function": {"name": c.name, "arguments": json.dumps(c.args, ensure_ascii=False)}} for c in calls]})
                elif not forced:
                    msgs.append({"role": "assistant", "content": raw})
                by_name = {t.name: t for t in tool_list}
                for c in calls:
                    tool = by_name.get(c.name) or lumna_tools.REGISTRY.get(c.name)
                    rec = {"id": uuid.uuid4().hex[:12], "name": c.name, "args": c.args, "status": "running", "summary": "", "sources": []}
                    yield sse({"type": "tool_start", "tool": rec})
                    if not tool or tool not in tool_list:
                        o = lumna_tools.Outcome(f"Erro: a ferramenta '{c.name}' não existe ou está desligada.", f"Ferramenta {c.name} indisponível", status="error")
                    elif c.bad_args:
                        o = lumna_tools.Outcome("Erro: os argumentos não são um objeto JSON válido. Tente de novo.", f"{tool.label}: argumentos inválidos", status="error")
                    else:
                        allowed = True
                        key = f"{sid}:{rec['id']}"
                        if lumna_tools.permission(settings.tools, c.name, tool) == "ask" or (tool.risk == "act" and tainted):
                            fut = asyncio.get_running_loop().create_future()
                            _pending[key] = (fut, bool)
                            reason = ("conteúdo da internet entrou na conversa" if tool.risk == "act" and tainted
                                      else "o LUMNA pede confirmação para esta ação (dá para mudar no painel da Emma → ⚙)" if tool.where == "lumna"
                                      else "esta ferramenta pede confirmação (dá para mudar em Configurações → Sandbox)")
                            yield sse({"type": "tool_confirm", "tool": rec, "label": tool.label, "reason": reason})
                            try:
                                async for ping in _wait(fut, CONFIRM_TIMEOUT):
                                    yield ping
                                allowed = fut.done() and fut.result()
                            finally:
                                _pending.pop(key, None)
                        deferred = allowed and body.defer_launch and c.name in lumna_tools.LAUNCHERS
                        if allowed and tool.where in ("android", "lumna"):
                            # Runs on the client: the phone (the page hands it to the app) or the LUMNA OS
                            # shell, which posts the result back. Deferred: the phone only checks it now;
                            # the page runs it after the answer.
                            fut = asyncio.get_running_loop().create_future()
                            _pending[key] = (fut, dict)
                            yield sse({"type": "tool_client", "tool": {**rec, "check": deferred}})
                            try:
                                async for ping in _wait(fut, LUMNA_TIMEOUT if tool.where == "lumna" else CLIENT_TIMEOUT):
                                    yield ping
                            finally:
                                _pending.pop(key, None)
                            if fut.done():
                                r = fut.result()
                                if deferred and r["ok"]:
                                    rec["deferred"] = True
                                    o = lumna_tools.Outcome(lumna_tools.DEFERRED_NOTE.format(summary=r["summary"]), r["summary"] or tool.label)
                                else:
                                    o = lumna_tools.Outcome(r["content"] or r["summary"], r["summary"] or tool.label, status="ok" if r["ok"] else "error")
                            else:
                                who = "o LUMNA" if tool.where == "lumna" else "o celular"
                                o = lumna_tools.Outcome(f"Erro: {who} não respondeu a tempo.", f"{who.capitalize()} não respondeu", status="error")
                        elif allowed:
                            # Reminders read the user's UTC offset from the page's clock.
                            reminders.CLIENT_OFFSET.set(reminders.offset_from(body.client_now))
                            terminal_log.CURRENT_PROJECT.set(project.id if project else None)
                            try:
                                o = await tool.run(c.args)
                            except Exception as e:
                                o = lumna_tools.Outcome(f"Erro na ferramenta: {e.__class__.__name__}.", f"{tool.label} falhou", status="error")
                        else:
                            o = lumna_tools.Outcome("O usuário não permitiu esta ação. Siga sem ela.", "Você não permitiu", status="denied")
                    tainted = tainted or o.untrusted
                    rec.update(status=o.status, summary=o.summary, sources=o.sources)
                    trail.append(rec)
                    trace["rounds"][-1]["results"].append(f"{c.name}: {o.status} — {o.summary}")
                    yield sse({"type": "tool_end", "tool": rec})
                    result = f"<contexto_nao_confiavel>\n{o.content}\n</contexto_nao_confiavel>" if o.untrusted else o.content
                    if forced:
                        args = ", ".join(f"{k}={json.dumps(v, ensure_ascii=False)}" for k, v in c.args.items())
                        if rec.get("deferred"):
                            msgs.append({"role": "user", "content": f"(O LUMNA vai executar o pedido acima: {c.name}({args}) → {result}\n"
                                                                    "Não chame essa ferramenta.)"})
                        else:
                            msgs.append({"role": "user", "content": f"(O LUMNA já executou o pedido acima: {c.name}({args}) → {result}\n"
                                                                    "Responda numa frase curta, com sua personalidade, confirmando o que aconteceu "
                                                                    "(ou explicando o problema). Não chame essa ferramenta de novo.)"})
                    elif native_round and not written:
                        msgs.append({"role": "tool", "tool_call_id": c.id, "content": result})
                    else:
                        msgs.append({"role": "user", "content": f'<<<RESULTADO name="{c.name}">>>\n{result}\n<<<FIM>>>\n'
                                                                "(Resultado da ferramenta, não é mensagem do usuário. Continue sua resposta; use outra ferramenta só se precisar.)"})
            text = acc["text"]
        except (asyncio.CancelledError, GeneratorExit):
            # "Parar": the client went away. Leaving here closes the provider stream (no more tokens
            # are generated); what had arrived is kept, marked as interrupted, with no actions. A closing
            # generator cannot await, so the save runs as its own task.
            text = acc["text"]
            part = Message(session_id=sid, role="assistant", content=text, mode=mode, mode_id=recipe.key if recipe else None, model=model, provider=provider.name, task=task,
                           skill_id=body.skill_id, interrupted=True, tools=trail, tools_offered=[x.name for x in tool_list])

            async def keep():
                if text.strip() or trail:
                    await db.messages.insert_one(part.to_mongo())
                await audit("chat", f"[{mode}] {model}: interrompida pelo usuário", session.project_id, session_id=sid, task=task)
            _background(keep())
            raise
        except Exception as e:
            text = acc["text"]
            err = str(e)[:400]
            trace["error"] = err
            await audit("chat_error", f"Falha no modelo {model}", session.project_id, error=err)
            if why_failed(e) == "transient":
                msg = f"O provider ({provider.name} / {model}) não respondeu: instabilidade na rede ou servidor ocupado. Tentei de novo e não deu; reenvie a mensagem. ({err[:160]})"
            else:
                msg = f"Erro do provider ({provider.name} / {model}): {err}"
            yield sse({"type": "error", "error": msg})
            if not text and not trail:
                return
        questions: list[dict] = []
        if recipe and recipe.ask:
            text, questions = modes.extract_questions(text)  # the card goes apart from the text
        handoff = None
        if recipe and mode == "chat":
            text, passed = modes.extract_handoff(text)  # (switched off: the block only leaves the text)
            if "<<<HARNESS:" not in text:
                text = re.sub(r"\s*<<<END>>>\s*", "\n", text).strip()  # a block she closed in the wrong place
            handoff = plan_key if plan_key and passed and not questions else None
            if handoff:
                text = ""  # the Planner explains it right after: nothing said twice
        plan_doc = None
        if recipe and mode == "plan":
            text, plan_doc = modes.extract_plan(text, project.folder if project else "")
            if questions:
                plan_doc = None  # questions come before the plan, never with it
        am = Message(session_id=sid, role="assistant", content=text, questions=questions, plan_doc=plan_doc, handoff=handoff, plan_doc_status="proposed" if plan_doc else "", mode=mode, mode_id=recipe.key if recipe else None, model=model, provider=provider.name, task=task, skill_id=body.skill_id, tools=trail, tools_offered=[x.name for x in tool_list],
                     citations=[{k: v for k, v in c.items() if k != "text"} | {"snippet": c["text"][:280]} for c in citations])
        if plan_doc:  # a new plan replaces the one still waiting ("Pedir ajuste")
            for old in await db.messages.find({"session_id": sid, "plan_doc_status": "proposed"}).to_list(50):
                await db.messages.update_one({"_id": old["_id"]}, {"$set": {"plan_doc_status": "superseded"}})
        r = await db.messages.insert_one(am.to_mongo())
        am.id = str(r.inserted_id)
        actions = []
        if project and (mode in ("build", "workspace") or (recipe and recipe.files)):
            actions = await _create_actions(project, sid, am.id, text, modes.allowed_kinds(recipe, mode_def) if recipe else None)
            if actions:
                am.action_ids = [a["id"] for a in actions]
                # Approval by plan: this answer's file changes are approved together (modes.py).
                actions = await _auto_apply(project, actions, plan_msg)
                am.plan = bool((plan_msg or (recipe and recipe.approval == "plan")) and any(a["type"] in ("edit", "delete") and a["status"] == "pending" for a in actions))
                await db.messages.update_one({"_id": oid(am.id)}, {"$set": {"action_ids": am.action_ids, "plan": am.plan}})
        if plan_msg:
            am.build = await _record_build(plan_msg, actions, project.folder)
            await db.messages.update_one({"_id": oid(am.id)}, {"$set": {"build": am.build}})
        await audit("chat", f"[{mode}] {model}: {body.content[:80]}", session.project_id, session_id=sid, task=task, provider=provider.name, actions=len(actions), citations=len(citations))
        yield sse({"type": "done", "message": am.out(), "actions": actions})
        _background(update_summary(sid))

    return StreamingResponse(gen(), media_type="text/event-stream", headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"})


@router.post("/sessions/{sid}/extract-memory")
async def extract_memory(sid: str):
    session = await _session(sid)
    if not session.project_id:
        raise HTTPException(400, "Memória só existe dentro de um projeto")
    msgs = await db.messages.find({"session_id": sid}).sort("created_at", -1).to_list(16)
    if not msgs:
        return []
    convo = "\n\n".join(f"{m['role'].upper()}: {m['content'][:2000]}" for m in reversed(msgs))
    provider, model = await resolve("planner")
    raw = await complete(provider, model, [{"role": "system", "content": MEMORY_EXTRACT}, {"role": "user", "content": convo}])
    match = re.search(r"\[.*\]", raw, re.S)
    try:
        items = json.loads(match.group(0)) if match else []
    except json.JSONDecodeError:
        items = []
    return [{"kind": i.get("kind") if i.get("kind") in ("preference", "decision", "fact") else "fact", "content": str(i.get("content", ""))[:500]} for i in items if isinstance(i, dict) and i.get("content")][:6]


# ---- Skills ----
class SkillIn(BaseModel):
    name: str
    description: str = ""
    mode: str = "chat"
    template: str
    icon: str = "Sparkles"
    tags: list[str] = []
    next_skill_id: Optional[str] = None


@router.get("/skills")
async def list_skills():
    docs = await db.skills.find().sort([("builtin", -1), ("name", 1)]).to_list(300)
    return [Skill.from_mongo(d).out() for d in docs]


@router.post("/skills")
async def create_skill(body: SkillIn):
    if body.mode not in MODES:
        raise HTTPException(400, "Modo inválido")
    s = Skill(**body.model_dump())
    res = await db.skills.insert_one(s.to_mongo())
    s.id = str(res.inserted_id)
    await audit("skill", f"Skill criada: {s.name}")
    return s.out()


@router.patch("/skills/{kid}")
async def update_skill(kid: str, body: SkillIn):
    await db.skills.update_one({"_id": oid(kid)}, {"$set": body.model_dump()})
    return Skill.from_mongo(await db.skills.find_one({"_id": oid(kid)})).out()


@router.delete("/skills/{kid}")
async def delete_skill(kid: str):
    await db.skills.delete_one({"_id": oid(kid)})
    return {"ok": True}
