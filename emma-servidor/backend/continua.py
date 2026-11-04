"""A conversa única e contínua do assistente (a popup): ela sabe há quanto tempo vocês não falam e, na primeira fala depois de uma
pausa longa, cumprimenta e oferece retomar o que ficou pela metade (o último projeto em que vocês estavam)."""
import os
import re
from pathlib import Path
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


def ultima_historia(casa: Optional[Path] = None) -> Optional[dict]:
    """A história do Eric mexida mais recentemente (~/Emma/Histórias/NOME/: capitulos/ e biblia.md) e onde pararam ("## Onde paramos" da bíblia)."""
    base = (casa or Path(os.path.expanduser("~"))) / "Emma" / "Histórias"
    melhor = None
    try:
        pastas = [p for p in base.iterdir() if p.is_dir() and not p.name.startswith(".")]
    except OSError:
        return None
    for pasta in pastas:
        arquivos = [f for f in (pasta / "capitulos").glob("*.md")] if (pasta / "capitulos").is_dir() else []
        if not arquivos:
            continue  # uma história sem capítulos ainda não tem onde retomar
        quando = max(f.stat().st_mtime for f in arquivos)
        if not melhor or quando > melhor["quando"]:
            melhor = {"nome": pasta.name, "quando": quando, "pasta": pasta, "capitulos": len(arquivos)}
    if not melhor:
        return None
    onde = ""
    try:
        m = re.search(r"^## Onde paramos\s*\n+(.+?)(?:\n\n|\n## |\Z)", (melhor["pasta"] / "biblia.md").read_text(encoding="utf-8"), re.S | re.M)
        onde = re.sub(r"\s+", " ", m.group(1)).strip()[:200] if m else ""
    except OSError:
        pass
    melhor["onde"] = onde
    return melhor


def _historia_nota(h: dict, agora: Optional[datetime] = None) -> str:
    agora = agora or datetime.now(timezone.utc)
    ha = faz_quanto_tempo(agora - datetime.fromtimestamp(h["quando"], timezone.utc))
    return (f"Última história em que vocês escreveram: \"{h['nome']}\" ({h['capitulos']} capítulo(s), mexida há {ha}"
            + (f"; onde pararam: {h['onde']}" if h["onde"] else "") + "). Se fizer sentido, ofereça continuar (para isso use historia_abrir).")


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
        extra = ultima_historia()
        historia = (" " + _historia_nota(extra)) if extra else ""
        return (f"Último projeto em que vocês estavam: \"{proj.get('name', '')}\" (conversa \"{s.get('title', '')}\"" +
                (f", parada há {faz_quanto_tempo(datetime.now(timezone.utc) - quando)}" if quando else "") + ")." +
                (f" A última coisa que a pessoa disse lá: \"{dito}\"." if dito else "") + historia)
    h = ultima_historia()
    return _historia_nota(h) if h else ""


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
