import time
from typing import Optional

import local_voice
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, ValidationError

from core import SECRET_CONFIGS, EmbeddingConfig, ImageGenConfig, Provider, RouteTarget, Settings, ToolsConfig, VoiceConfig, audit, db, enc, get_settings, oid
from netguard import assert_public_url
from llm import PRESETS, client_for, embed
from workspace_runtime import PROFILES
import tools as lumna_tools
import nsfw_profile

router = APIRouter(prefix="/api")


class ProviderIn(BaseModel):
    name: str
    preset: str = "custom"
    base_url: str
    api_key: Optional[str] = None
    models: list[str] = []


class SettingsIn(BaseModel):
    routing: Optional[dict[str, RouteTarget]] = None
    embedding: Optional[dict] = None
    image_gen: Optional[dict] = None
    voice: Optional[dict] = None
    tools: Optional[dict] = None
    personality: Optional[str] = None
    personality_sfw: Optional[str] = None
    system_prompt_sfw: Optional[str] = None
    nsfw_on_start: Optional[bool] = None
    system_prompt: Optional[str] = None
    sandbox_profile: Optional[str] = None


def public_settings(s: Settings) -> dict:
    d = s.out()
    for name in SECRET_CONFIGS:
        k = d[name].pop("api_key")
        d[name]["has_key"] = bool(k)
        d[name]["api_key_masked"] = f"••••{k[-4:]}" if k else ""
    # Can the server speak / hear (Fish with its key, or the local engines on the PC)?
    v = d["voice"]
    v["server_tts"] = local_voice.engine_ready(v.get("tts_engine", ""), "tts") or (v.get("tts_engine") != "local" and v["has_key"])
    v["server_stt"] = local_voice.engine_ready(v.get("stt_engine", ""), "stt") or (v.get("stt_engine") != "local" and v["has_key"])
    return d


def merged(current, patch: dict, cls):
    """current config updated with the known fields of patch (an empty api_key keeps the stored one)."""
    e = current.model_dump()
    for k, v in patch.items():
        if k == "api_key" and not v:
            continue
        if k in e:
            e[k] = v
    try:
        return cls(**e)
    except ValidationError as err:
        raise HTTPException(400, f"Configuração inválida: {err.errors()[0]['msg']}")


@router.get("/providers/presets")
async def presets():
    return PRESETS


@router.get("/providers")
async def list_providers():
    docs = await db.providers.find().sort("created_at", 1).to_list(100)
    return [Provider.from_mongo(d).public() for d in docs]


@router.post("/providers")
async def create_provider(body: ProviderIn):
    assert_public_url(body.base_url)
    p = Provider(**body.model_dump(exclude={"api_key"}), api_key="".join((body.api_key or "").split()))  # LUMNA: sem espaço nem quebra de linha escondidos na chave
    res = await db.providers.insert_one(p.to_mongo())
    p.id = str(res.inserted_id)
    await audit("config", f"Provider criado: {p.name}")
    return p.public()


@router.patch("/providers/{pid}")
async def update_provider(pid: str, body: ProviderIn):
    assert_public_url(body.base_url)
    upd = body.model_dump(exclude={"api_key"})
    chave = "".join((body.api_key or "").split())  # LUMNA: sem espaço nem quebra de linha escondidos na chave
    if chave:
        upd["api_key"] = enc(chave)
    await db.providers.update_one({"_id": oid(pid)}, {"$set": upd})
    await audit("config", f"Provider atualizado: {body.name}")
    return Provider.from_mongo(await db.providers.find_one({"_id": oid(pid)})).public()


@router.delete("/providers/{pid}")
async def delete_provider(pid: str):
    await db.providers.delete_one({"_id": oid(pid)})
    return {"ok": True}


async def _provider(pid: str) -> Provider:
    p = Provider.from_mongo(await db.providers.find_one({"_id": oid(pid)}))
    if not p:
        raise HTTPException(404, "Provider não encontrado")
    return p


@router.get("/providers/{pid}/models")
async def provider_models(pid: str):
    p = await _provider(pid)
    try:
        res = await client_for(p).models.list()
        return {"ok": True, "models": sorted(m.id for m in res.data)}
    except Exception as e:
        return {"ok": False, "models": p.models, "error": str(e)[:300]}


class TestIn(BaseModel):
    model: str


@router.post("/providers/{pid}/test")
async def test_provider(pid: str, body: TestIn):
    p = await _provider(pid)
    c = client_for(p)
    report = {"model": body.model}
    t = time.monotonic()
    try:
        r = await c.chat.completions.create(model=body.model, messages=[{"role": "user", "content": "Responda apenas: ok"}])
        report.update(chat=True, latency_ms=int((time.monotonic() - t) * 1000), reply=(r.choices[0].message.content or "")[:80])
    except Exception as e:
        return {**report, "chat": False, "error": str(e)[:300]}
    try:
        stream = await c.chat.completions.create(model=body.model, messages=[{"role": "user", "content": "diga oi"}], stream=True)
        async for _ in stream:
            break
        report["streaming"] = True
    except Exception:
        report["streaming"] = False
    try:
        r = await c.chat.completions.create(model=body.model, messages=[{"role": "user", "content": 'Retorne o JSON {"ok": true}'}], response_format={"type": "json_object"})
        report["json_mode"] = "ok" in (r.choices[0].message.content or "")
    except Exception:
        report["json_mode"] = False
    return report


@router.get("/settings")
async def read_settings():
    # In the SFW profile the NSFW personality and guidelines never leave the server (someone may be
    # looking at the screen); the page edits only the current profile's (nsfw_profile.py).
    return nsfw_profile.mask(public_settings(await get_settings()), await nsfw_profile.nsfw())


@router.put("/settings")
async def write_settings(body: SettingsIn):
    s = await get_settings()
    if body.routing is not None:
        s.routing = body.routing
    if body.embedding is not None:
        s.embedding = merged(s.embedding, body.embedding, EmbeddingConfig)
        assert_public_url(s.embedding.base_url)
    if body.image_gen is not None:
        s.image_gen = merged(s.image_gen, body.image_gen, ImageGenConfig)
        assert_public_url(s.image_gen.base_url)
    if body.voice is not None:
        s.voice = merged(s.voice, body.voice, VoiceConfig)
    if body.tools is not None:
        s.tools = merged(s.tools, body.tools, ToolsConfig)
        if s.tools.protocol not in ("auto", "native", "text"):
            raise HTTPException(400, "Protocolo de ferramentas inválido")
        s.tools.permissions = {k: v for k, v in s.tools.permissions.items() if k in lumna_tools.REGISTRY}
        if any(v not in lumna_tools.PERMISSIONS for v in s.tools.permissions.values()):
            raise HTTPException(400, "Permissão inválida (auto, ask ou off)")
    if body.personality is not None:
        s.personality = body.personality
    if body.personality_sfw is not None:
        s.personality_sfw = body.personality_sfw
    if body.system_prompt_sfw is not None:
        s.system_prompt_sfw = body.system_prompt_sfw
    if body.nsfw_on_start is not None:
        s.nsfw_on_start = body.nsfw_on_start
    if body.system_prompt is not None:
        s.system_prompt = body.system_prompt
    if body.sandbox_profile is not None:
        prof = PROFILES.get(body.sandbox_profile)
        if not prof or not prof["enabled"]:
            raise HTTPException(400, "Perfil de sandbox indisponível no V1")
        s.sandbox_profile = body.sandbox_profile
    data = s.to_mongo()
    data.pop("_id", None)
    await db.settings.update_one({"key": "global"}, {"$set": data}, upsert=True)
    await audit("config", "Configurações atualizadas")
    return nsfw_profile.mask(public_settings(s), await nsfw_profile.nsfw())


@router.post("/settings/embedding/test")
async def test_embedding():
    cfg = (await get_settings()).embedding
    if not cfg.api_key:
        return {"ok": False, "error": "Sem API key de embeddings"}
    try:
        v = await embed(["teste de embedding"], EmbeddingConfig(**{**cfg.model_dump(), "enabled": True}))
        return {"ok": True, "dimensions": len(v[0])}
    except Exception as e:
        return {"ok": False, "error": str(e)[:300]}


@router.get("/tools")
async def tool_list():
    return lumna_tools.describe()


@router.get("/sandbox/profiles")
async def sandbox_profiles():
    import isolated_shell
    shell = await isolated_shell.status()  # Full's terminal: isolated (Docker/bubblewrap) or the Build list
    return [{"key": k, **{kk: vv for kk, vv in v.items()}, **({"shell": shell} if k == "full" else {})} for k, v in PROFILES.items()]


@router.get("/audit")
async def audit_logs(project_id: Optional[str] = None, limit: int = 100):
    q = {"project_id": project_id} if project_id else {}
    docs = await db.audit_logs.find(q).sort("created_at", -1).to_list(min(limit, 500))
    for d in docs:
        d["id"] = str(d.pop("_id"))
    return docs
