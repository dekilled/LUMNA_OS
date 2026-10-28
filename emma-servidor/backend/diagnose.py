"""Settings → Sandbox → Diagnóstico: what this device's backend offers the model, and what happened
in the last answers. For finding out where a tool call gets lost (app, backend or model)."""
from pathlib import Path
from typing import Optional
from urllib.parse import urlparse

from fastapi import APIRouter
from pydantic import BaseModel

import quick
import tools as lumna_tools
from core import Provider, db, get_settings, oid
from routes_chat import TRACE

router = APIRouter(prefix="/api")
BUILD_FILE = Path(__file__).resolve().parent.parent / "BUILD"


@router.get("/tools/diagnose")
async def diagnose(client_tools: str = ""):
    s = await get_settings()
    phone = [t for t in client_tools.split(",") if t]
    offered = [t.name for t in lumna_tools.enabled_tools(s.tools, phone)] if s.sandbox_profile == "full" else []
    route = s.routing.get("chat")
    provider: Optional[Provider] = None
    if route and route.provider_id:
        provider = Provider.from_mongo(await db.providers.find_one({"_id": oid(route.provider_id)}))
    return {
        "build": BUILD_FILE.read_text().strip() if BUILD_FILE.exists() else "dev",
        "profile": s.sandbox_profile,
        "protocol": s.tools.protocol,
        "permissions": s.tools.permissions,
        "chat_route": {"provider": provider.name if provider else None, "host": urlparse(provider.base_url).hostname if provider else None,
                       "model": route.model if route else None},
        "phone_tools_from_app": phone,
        "offered": offered,
        "trace": list(TRACE),
    }


class CommandIn(BaseModel):
    text: str
    client_tools: list[str] = []


@router.post("/tools/diagnose/command")
async def diagnose_command(body: CommandIn):
    """Whether a message would run as a direct command (no model involved)."""
    names = {t.name for t in lumna_tools.REGISTRY.values()} if not body.client_tools else set(body.client_tools) | {"web_search", "web_fetch"}
    m = quick.match(body.text, names)
    return {"match": {"tool": m[0], "args": m[1]} if m else None}
