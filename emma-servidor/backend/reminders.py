"""Reminders (Modo Full, G5): "me lembra de ligar pro dentista amanhã às 10".

The Lumna creates, lists and cancels them with tools; they are kept in the `reminders` collection.
Delivery happens on the device: the Android app schedules a notification for each one (AlarmManager,
works with the app closed), the desktop/PWA page notifies while it is open (see frontend
lib/reminders.ts). A reminder whose time has passed counts as delivered ("done").

Times: the model writes local time ("2026-10-04T10:00"); the user's UTC offset comes from the page's
clock (ChatIn.client_now, "… (UTC-03:00)"), set per answer in CLIENT_OFFSET.
"""
import re
from contextvars import ContextVar
from datetime import datetime, timedelta, timezone
from typing import Optional

from bson import ObjectId
from fastapi import APIRouter, HTTPException

from core import db, now_iso, oid
from tools import REGISTRY, Outcome, Tool

router = APIRouter(prefix="/api")

CLIENT_OFFSET: ContextVar[Optional[int]] = ContextVar("client_offset", default=None)  # minutes east of UTC
MAX_AHEAD = timedelta(days=730)
MAX_TEXT = 300
WEEKDAYS = ["seg", "ter", "qua", "qui", "sex", "sáb", "dom"]


def offset_from(client_now: str) -> Optional[int]:
    """'sexta-feira, 03/10/2026 14:05 (UTC-03:00)' → -180."""
    m = re.search(r"UTC([+-])(\d{2}):(\d{2})", client_now or "")
    if not m:
        return None
    minutes = int(m.group(2)) * 60 + int(m.group(3))
    return minutes if m.group(1) == "+" else -minutes


def _tz(offset: Optional[int]) -> timezone:
    return timezone(timedelta(minutes=offset or 0))


def local_label(at_utc: str, offset: Optional[int]) -> str:
    """'sáb 04/10 às 10:00'."""
    t = datetime.fromisoformat(at_utc).astimezone(_tz(offset))
    return f"{WEEKDAYS[t.weekday()]} {t:%d/%m} às {t:%H:%M}"


def _status(doc: dict, now: datetime) -> str:
    if doc.get("status") == "cancelled":
        return "cancelled"
    return "done" if datetime.fromisoformat(doc["at"]) <= now else "pending"


def _out(doc: dict, now: datetime) -> dict:
    return {"id": str(doc["_id"]), "text": doc["text"], "at": doc["at"], "offset": doc.get("offset"),
            "label": local_label(doc["at"], doc.get("offset")), "status": _status(doc, now), "created_at": doc.get("created_at")}


async def _all() -> list[dict]:
    return await db.reminders.find({}).to_list(2000)


# ---------- tools ----------
async def create_reminder(args: dict) -> Outcome:
    text = str(args.get("text") or "").strip()[:MAX_TEXT]
    when = str(args.get("when") or "").strip()
    if not text:
        return Outcome("Erro: falta o texto do lembrete.", "Lembrete sem texto", status="error")
    offset = CLIENT_OFFSET.get()
    try:
        local = datetime.fromisoformat(when)
    except ValueError:
        return Outcome("Erro: 'when' deve ser data e hora local no formato AAAA-MM-DDTHH:MM.", "Horário inválido", status="error")
    if local.tzinfo is None:
        local = local.replace(tzinfo=_tz(offset))
    at = local.astimezone(timezone.utc)
    now = datetime.now(timezone.utc)
    if at < now - timedelta(minutes=1):
        return Outcome(f"Erro: esse horário já passou ({local_label(at.isoformat(), offset)}). Confira a data atual e tente de novo.",
                       "Horário já passou", status="error")
    if at > now + MAX_AHEAD:
        return Outcome("Erro: lembretes podem ser criados para até 2 anos.", "Longe demais", status="error")
    doc = {"text": text, "at": at.isoformat(), "offset": offset, "status": "pending", "created_at": now_iso()}
    res = await db.reminders.insert_one(doc)
    label = local_label(doc["at"], offset)
    return Outcome(f"Lembrete criado (id {res.inserted_id}): \"{text}\" para {label}. O celular avisa com uma notificação na hora.",
                   f"Lembrete: {text} · {label}")


async def list_reminders(args: dict) -> Outcome:
    now = datetime.now(timezone.utc)
    pending = sorted((d for d in await _all() if _status(d, now) == "pending"), key=lambda d: d["at"])
    if not pending:
        return Outcome("Nenhum lembrete pendente.", "Nenhum lembrete pendente")
    lines = [f"- id {d['_id']}: \"{d['text']}\" · {local_label(d['at'], d.get('offset'))}" for d in pending[:50]]
    return Outcome("Lembretes pendentes:\n" + "\n".join(lines), f"{len(pending)} lembrete(s) pendente(s)")


async def cancel_reminder(args: dict) -> Outcome:
    key = str(args.get("id") or "").strip()
    now = datetime.now(timezone.utc)
    pending = [d for d in await _all() if _status(d, now) == "pending"]
    found = [d for d in pending if str(d["_id"]) == key]
    if not found and key:
        # The model may pass part of the text instead of the id.
        found = [d for d in pending if key.casefold() in d["text"].casefold()]
    if len(found) != 1:
        return Outcome("Erro: " + ("nenhum lembrete pendente combina com isso." if not found else "mais de um lembrete combina; use o id (list_reminders)."),
                       "Lembrete não encontrado" if not found else "Mais de um lembrete combina", status="error")
    d = found[0]
    await db.reminders.update_one({"_id": d["_id"]}, {"$set": {"status": "cancelled"}})
    return Outcome(f"Lembrete cancelado: \"{d['text']}\" ({local_label(d['at'], d.get('offset'))}).", f"Cancelou: {d['text']}")


for _t in [
    Tool("create_reminder", "Lembretes",
         "Cria um lembrete: o celular mostra uma notificação no horário, mesmo com o app fechado. Use para "
         "\"me lembra de…\", \"me avisa às…\". Calcule a data/hora a partir da data e hora atuais informadas.",
         {"type": "object", "properties": {
             "text": {"type": "string", "description": "O que lembrar, curto (ex.: Ligar para o dentista)"},
             "when": {"type": "string", "description": "Data e hora LOCAL: AAAA-MM-DDTHH:MM"},
         }, "required": ["text", "when"]},
         create_reminder, risk="act"),
    Tool("list_reminders", "Ver lembretes", "Lista os lembretes pendentes (com id).",
         {"type": "object", "properties": {}, "required": []}, list_reminders),
    Tool("cancel_reminder", "Cancelar lembretes", "Cancela um lembrete pendente pelo id (veja list_reminders) ou por parte do texto.",
         {"type": "object", "properties": {"id": {"type": "string", "description": "id do lembrete, ou parte do texto"}}, "required": ["id"]},
         cancel_reminder, risk="act"),
]:
    REGISTRY[_t.name] = _t

REMINDER_TOOLS = {"create_reminder", "list_reminders", "cancel_reminder"}


# ---------- routes (settings screen, and the devices that deliver them) ----------
@router.get("/reminders")
async def get_reminders():
    """Pending ones (soonest first), then the last 30 days of done/cancelled ones."""
    now = datetime.now(timezone.utc)
    items = [_out(d, now) for d in await _all()]
    pending = sorted((i for i in items if i["status"] == "pending"), key=lambda i: i["at"])
    month_ago = (now - timedelta(days=30)).isoformat()
    past = sorted((i for i in items if i["status"] != "pending" and i["at"] >= month_ago), key=lambda i: i["at"], reverse=True)
    return {"items": pending + past[:50], "now": now.isoformat()}


@router.delete("/reminders/{rid}")
async def delete_reminder(rid: str):
    if not await db.reminders.find_one({"_id": oid(rid)}):
        raise HTTPException(404, "Lembrete não encontrado")
    await db.reminders.update_one({"_id": ObjectId(rid)}, {"$set": {"status": "cancelled"}})
    return {"ok": True}
