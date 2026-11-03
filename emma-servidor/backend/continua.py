"""A conversa única e contínua do assistente (a popup): ela sabe há quanto tempo vocês não falam e, na primeira fala depois de uma
pausa longa, cumprimenta e oferece retomar o que ficou pela metade (o último projeto em que vocês estavam)."""
import re
from datetime import datetime, timedelta, timezone
from typing import Optional

from core import db, oid
import nsfw_profile

PAUSA_LONGA = timedelta(hours=6)  # depois disso (ou virando o dia) ela cumprimenta
PAUSA_CURTA = timedelta(minutes=10)  # abaixo disso o intervalo nem é mencionado


def _dt(iso: str) -> Optional[datetime]:
    try:
        d = datetime.fromisoformat(iso)
        return d if d.tzinfo else d.replace(tzinfo=timezone.utc)
    except (TypeError, ValueError):
        return None


def _offset(client_now: str) -> timezone:
    m = re.search(r"UTC([+-])(\d{2}):(\d{2})", client_now or "")
    if not m:
        return timezone.utc
    minutos = int(m.group(2)) * 60 + int(m.group(3))
    return timezone(timedelta(minutes=minutos if m.group(1) == "+" else -minutos))


def faz_quanto_tempo(d: timedelta) -> str:
    s = int(d.total_seconds())
    if s < 3600:
        return f"{max(1, s // 60)} min"
    if s < 86400:
        return f"{s // 3600} h"
    dias = s // 86400
    return "1 dia" if dias == 1 else f"{dias} dias"


def virou_o_dia(ultima: datetime, agora: datetime, tz: timezone) -> bool:
    return ultima.astimezone(tz).date() != agora.astimezone(tz).date()


async def retomada() -> str:
    """Onde vocês pararam: a conversa de projeto mais recente (a pessoa pode ter deixado algo no meio)."""
    q = await nsfw_profile.scope({"project_id": {"$ne": None}})
    docs = await db.sessions.find(q).sort("updated_at", -1).to_list(5)
    for s in docs:
        if s.get("assist_log") or not s.get("project_id"):
            continue
        proj = await db.projects.find_one({"_id": oid(s["project_id"])})
        if not proj:
            continue
        ult = await db.messages.find({"session_id": str(s["_id"]), "role": "user"}).sort("created_at", -1).to_list(1)
        dito = re.sub(r"\s+", " ", ult[0]["content"])[:160] if ult else ""
        quando = _dt(s.get("updated_at", ""))
        return (f"Último projeto em que vocês estavam: \"{proj.get('name', '')}\" (conversa \"{s.get('title', '')}\"" +
                (f", parada há {faz_quanto_tempo(datetime.now(timezone.utc) - quando)}" if quando else "") + ")." +
                (f" A última coisa que a pessoa disse lá: \"{dito}\"." if dito else ""))
    return ""


async def nota(session, history: list[dict], client_now: str, agora: Optional[datetime] = None) -> str:
    """O trecho do prompt desta conversa; vazio fora da conversa do assistente ou no meio de uma troca."""
    if getattr(session, "origin", "") != "assistente":
        return ""
    agora = agora or datetime.now(timezone.utc)
    ultima = next((_dt(m.get("created_at", "")) for m in reversed(history) if _dt(m.get("created_at", ""))), None)
    hora = f"Agora: {client_now}." if client_now else ""
    if not ultima:
        return f"{hora} Esta é a primeira fala da conversa contínua com a pessoa: ela é uma só, para sempre; ninguém precisa recomeçar nem repetir o que já disse."
    pausa = agora - ultima
    partes = [hora] if hora else []
    if pausa >= PAUSA_CURTA:
        partes.append(f"A última fala desta conversa foi há {faz_quanto_tempo(pausa)}.")
    if pausa >= PAUSA_LONGA or virou_o_dia(ultima, agora, _offset(client_now)):
        partes.append("É a primeira fala depois de uma pausa longa: cumprimente de acordo com o horário, de forma natural e curta (sem repetir o 'oi' todo dia da mesma forma)."
                      " Se houver algo ficado pela metade abaixo, lembre em uma frase e pergunte se querem continuar; senão, só cumprimente.")
        r = await retomada()
        if r:
            partes.append(r)
    return " ".join(partes)
