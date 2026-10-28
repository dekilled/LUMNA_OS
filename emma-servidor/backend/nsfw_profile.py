"""SFW / NSFW: two profiles that never mix (a "modo visita" to show the LUMNA to someone).

Every conversation and every project memory carries the tag `nsfw` (the profile it was made in), and
one check — `scope(query)` / `visible(doc)` — keeps the other profile's out of every list, search
and prompt. The Emma's personality is also per profile (Settings.personality is the NSFW one,
Settings.personality_sfw the SFW one). Going to NSFW asks for the password (on the phone the
fingerprint unlocks the saved one); back to SFW is free. The app opens in SFW unless
Settings.nsfw_on_start says otherwise (POST /api/profile/boot).
"""
import time
from typing import Optional

from fastapi import APIRouter, HTTPException, Request
from pydantic import BaseModel, Field

from auth import verify_password
from core import audit, db, get_settings, now_iso

router = APIRouter(prefix="/api")

_cur: Optional[bool] = None  # cache of the current profile (one backend, one user)
_fails: list[float] = []


SFW_PERSONALITY = """Você é a Emma: carinhosa, próxima, bem-humorada e parceira, mas SEMPRE de forma apropriada para qualquer pessoa ver a tela. Nada de conteúdo sexual, insinuações, palavrões ou apelidos íntimos; trate o usuário com simpatia e respeito. Se ele puxar algo picante, desconverse com leveza e volte ao assunto."""


def personality(settings, on: bool) -> str:
    """The Emma of the current profile: the NSFW one is Settings.personality (as before)."""
    return settings.personality if on else (settings.personality_sfw.strip() or SFW_PERSONALITY)


def apply(settings, on: bool) -> None:
    """The settings of one chat turn get the current profile's personality and guidelines."""
    settings.personality = personality(settings, on)
    if not on:
        settings.system_prompt = settings.system_prompt_sfw


def mask(data: dict, on: bool) -> dict:
    """GET /settings: the other profile's texts are left out."""
    # profile_nsfw: which profile these settings were read in (the page edits only that one's texts).
    if on:
        return {**data, "personality_sfw": "", "system_prompt_sfw": "", "profile_nsfw": True}
    return {**data, "personality": "", "system_prompt": "", "profile_nsfw": False}


async def nsfw() -> bool:
    global _cur
    if _cur is None:
        d = await db.profile_state.find_one({"key": "current"})
        _cur = bool(d and d.get("nsfw"))
    return _cur


async def _set(on: bool) -> None:
    global _cur
    if await db.profile_state.find_one({"key": "current"}):
        await db.profile_state.update_one({"key": "current"}, {"$set": {"nsfw": on, "at": now_iso()}})
    else:
        await db.profile_state.insert_one({"key": "current", "nsfw": on, "at": now_iso()})
    _cur = on


async def scope(q: dict) -> dict:
    """The query, limited to the current profile's documents."""
    return {**q, "nsfw": await nsfw()}


async def visible(doc: Optional[dict]) -> bool:
    return bool(doc) and bool(doc.get("nsfw")) == await nsfw()


async def migrate() -> None:
    """Once: everything made before the profiles is NSFW (as the user asked)."""
    if await db.profile_state.find_one({"key": "migrated"}):
        return
    for coll in (db.sessions, db.memories):
        for d in await coll.find({}).to_list(100000):
            if "nsfw" not in d:
                await coll.update_one({"_id": d["_id"]}, {"$set": {"nsfw": True}})
    await db.profile_state.insert_one({"key": "migrated", "at": now_iso()})


class ProfileIn(BaseModel):
    nsfw: bool
    password: str = Field(default="", max_length=200)


async def _state() -> dict:
    s = await get_settings()
    return {"nsfw": await nsfw(), "on_start": s.nsfw_on_start}


@router.get("/profile")
async def get_profile():
    return await _state()


@router.post("/profile")
async def set_profile(body: ProfileIn, request: Request):
    if body.nsfw and not await nsfw():
        now = time.time()
        _fails[:] = [t for t in _fails if now - t < 300]
        if len(_fails) >= 5:
            raise HTTPException(429, "Muitas tentativas. Espere uns minutos.")
        user = await db.users.find_one({"user_id": request.state.user["user_id"]})
        if not user or not verify_password(body.password, user.get("password_hash", "")):
            _fails.append(now)
            raise HTTPException(403, "Senha incorreta")
        _fails.clear()
    await _set(body.nsfw)
    await audit("profile", "Perfil NSFW" if body.nsfw else "Perfil SFW")
    return await _state()


@router.post("/profile/boot")
async def boot():
    """The app opened: SFW, unless "Abrir sempre no NSFW" is on."""
    s = await get_settings()
    if not s.nsfw_on_start and await nsfw():
        await _set(False)
    return await _state()
