"""The command log: what was asked in the assistant panel (AIO) and answered in a single exchange.

Each opening of the panel is its own session, created with assist_log=True. While it is one message
and one answer ("abre o Spotify", "me lembra às 19h", a quick question) it stays out of the sidebar
and shows here: what the user said, Emma's answer, what she ran, and when. When the user replies in
the panel it becomes a real conversation (routes_chat clears assist_log) and moves to the sidebar;
"Abrir como conversa" does the same by hand.
"""
import logging
import re
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from core import db, oid
import nsfw_profile

router = APIRouter(prefix="/api")
log = logging.getLogger("commands")


@router.get("/commands")
async def list_commands(before: Optional[str] = None, limit: int = 60):
    """Newest first; `before` (a created_at) pages back."""
    limit = max(1, min(limit, 200))
    sessions = [s for s in await db.sessions.find(await nsfw_profile.scope({"assist_log": True})).to_list(5000) if not before or s["created_at"] < before]
    sessions.sort(key=lambda s: s["created_at"], reverse=True)
    page = sessions[:limit]
    ids = [str(s["_id"]) for s in page]
    msgs = await db.messages.find({"session_id": {"$in": ids}}).sort("created_at", 1).to_list(len(ids) * 4 + 10) if ids else []
    by_session: dict[str, list[dict]] = {}
    for m in msgs:
        by_session.setdefault(m["session_id"], []).append(m)
    items = []
    for s in page:
        ms = by_session.get(str(s["_id"]), [])
        user = next((m for m in ms if m["role"] == "user"), None)
        if not user:
            continue  # opened and closed without saying anything
        answer = next((m for m in ms if m["role"] == "assistant"), None)
        items.append({
            "session_id": str(s["_id"]),
            "at": user["created_at"],
            "user": user["content"],
            "answer": answer["content"] if answer else "",
            "tools": (answer or {}).get("tools") or [],
            "model": (answer or {}).get("model"),
            "interrupted": bool((answer or {}).get("interrupted")),
        })
    return {"items": items, "more": len(sessions) > limit, "next": page[-1]["created_at"] if page else None}


@router.post("/commands/{sid}/promote")
async def promote(sid: str):
    """"Abrir como conversa": to the sidebar."""
    if not await nsfw_profile.visible(await db.sessions.find_one({"_id": oid(sid)})):
        raise HTTPException(404, "Sessão não encontrada")
    await db.sessions.update_one({"_id": oid(sid)}, {"$set": {"assist_log": False}})
    return {"ok": True}


class DeferredResultIn(BaseModel):
    tool_id: str
    ok: bool
    summary: str = Field(default="", max_length=300)


@router.post("/sessions/{sid}/deferred-result")
async def deferred_result(sid: str, body: DeferredResultIn):
    """An action deferred to after the answer (see tools.LAUNCHERS) ran on the phone: the answer's
    tool trail says what really happened ("Abriu Spotify"), not "Vai abrir…"."""
    for m in await db.messages.find({"session_id": sid}).sort("created_at", -1).to_list(20):
        tools = m.get("tools") or []
        if not any(t.get("id") == body.tool_id for t in tools):
            continue
        for t in tools:
            if t.get("id") == body.tool_id:
                t.update(status="ok" if body.ok else "error", summary=body.summary or t.get("summary", ""), deferred=False, ran=True)
        await db.messages.update_one({"_id": m["_id"]}, {"$set": {"tools": tools}})
        return {"ok": True}
    raise HTTPException(404, "Ação não encontrada")


async def migrate():
    """Sessions from the panel before the log existed (title "Assistente"): a single exchange goes to
    the log, titled after what was said; longer ones stay in the sidebar. Runs once per session."""
    moved = 0
    for s in await db.sessions.find({"title": "Assistente"}).to_list(5000):
        if "assist_log" in s:
            continue
        sid = str(s["_id"])
        users = [m for m in await db.messages.find({"session_id": sid}).sort("created_at", 1).to_list(500) if m["role"] == "user"]
        single = len(users) <= 1
        upd = {"assist_log": single}
        if users:
            upd["title"] = re.sub(r"\s+", " ", users[0]["content"])[:60] or "Assistente"
        await db.sessions.update_one({"_id": s["_id"]}, {"$set": upd})
        moved += single
    if moved:
        log.info("registro de comandos: %d conversa(s) antiga(s) do assistente movida(s) para o registro", moved)
