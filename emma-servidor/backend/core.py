import base64
import hashlib
import os
from datetime import datetime, timedelta, timezone
from typing import Annotated, Any, Optional

from bson import ObjectId
from cryptography.fernet import Fernet, InvalidToken
from fastapi import HTTPException
from pydantic import BaseModel, BeforeValidator, ConfigDict, Field

from docstore import AsyncDocStore

DB_PATH = os.environ.get("SQLITE_PATH") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "harness.db")
db = AsyncDocStore(DB_PATH)

_fernet = Fernet(base64.urlsafe_b64encode(hashlib.sha256(os.environ["SECRETS_KEY"].encode()).digest()))


def enc(value: str) -> str:
    if not value or value.startswith("enc:"):
        return value
    return "enc:" + _fernet.encrypt(value.encode()).decode()


def dec(value: str) -> str:
    if not value or not value.startswith("enc:"):
        return value
    try:
        return _fernet.decrypt(value[4:].encode()).decode()
    except InvalidToken:
        return ""


PyObjectId = Annotated[str, BeforeValidator(lambda v: str(v) if isinstance(v, ObjectId) else v)]


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def oid(value: str) -> ObjectId:
    if not value or not ObjectId.is_valid(value):
        raise HTTPException(404, "ID inválido")
    return ObjectId(value)


class BaseDocument(BaseModel):
    model_config = ConfigDict(populate_by_name=True, extra="ignore")
    id: Optional[PyObjectId] = Field(default=None, alias="_id")

    @classmethod
    def from_mongo(cls, doc: Optional[dict]):
        return cls.model_validate(doc) if doc else None

    def to_mongo(self) -> dict:
        data = self.model_dump(exclude={"id"})
        if self.id:
            data["_id"] = ObjectId(self.id)
        return data

    def out(self) -> dict:
        return self.model_dump()


class Provider(BaseDocument):
    name: str
    preset: str = "custom"
    base_url: str
    api_key: str = ""
    models: list[str] = []
    created_at: str = Field(default_factory=now_iso)

    @classmethod
    def from_mongo(cls, doc):
        p = super().from_mongo(doc)
        if p:
            p.api_key = dec(p.api_key)
        return p

    def to_mongo(self) -> dict:
        d = super().to_mongo()
        d["api_key"] = enc(d["api_key"])
        return d

    def public(self) -> dict:
        d = self.out()
        k = d.pop("api_key")
        d["api_key_masked"] = f"••••{k[-4:]}" if k else ""
        d["has_key"] = bool(k)
        return d


class RouteTarget(BaseModel):
    provider_id: str = ""
    model: str = ""


class EmbeddingConfig(BaseModel):
    enabled: bool = False
    base_url: str = "https://generativelanguage.googleapis.com/v1beta/openai/"
    api_key: str = ""
    model: str = "gemini-embedding-001"


class ImageGenConfig(BaseModel):
    """OpenAI-compatible /images/generations endpoint."""
    enabled: bool = False
    base_url: str = "https://api.openai.com/v1"
    api_key: str = ""
    model: str = "gpt-image-1"
    size: str = "1024x1024"


class VoiceConfig(BaseModel):
    """Fish Audio: text to speech and speech to text — or, on the PC, the local engines (local_voice.py)."""
    api_key: str = ""
    tts_engine: str = "fish"  # fish | local (Pocket TTS)
    stt_engine: str = "fish"  # fish | local (Whisper)
    whisper_model: str = "base"
    local_voice: str = ""  # Pocket TTS voice: "emma" (cloned) or a catalog one; "" = Emma if cloned
    voice_id: str = ""
    tts_model: str = "s2.1-pro-free"  # o gratuito de testes do Fish Audio; o pago é "s2.1-pro"
    asr_model: str = ""
    language: str = "pt"
    auto_read: bool = False


class ToolsConfig(BaseModel):
    """Lumna's tools in the Full sandbox profile (see tools.py)."""
    protocol: str = "auto"  # auto (native tool calls, text protocol if the endpoint refuses) | native | text
    permissions: dict[str, str] = {}  # tool name -> auto | ask | off (missing = the tool's default)


SECRET_CONFIGS = ("embedding", "image_gen", "voice")


class Settings(BaseDocument):
    key: str = "global"
    routing: dict[str, RouteTarget] = {}
    embedding: EmbeddingConfig = EmbeddingConfig()
    image_gen: ImageGenConfig = ImageGenConfig()
    voice: VoiceConfig = VoiceConfig()
    tools: ToolsConfig = ToolsConfig()
    personality: str = ""  # the Emma in the NSFW profile (nsfw_profile.py)
    personality_sfw: str = ""  # the Emma in the SFW profile
    system_prompt_sfw: str = ""  # the global guidelines in the SFW profile (system_prompt: the NSFW one)
    nsfw_on_start: bool = False  # "Abrir sempre no NSFW": the app opens in NSFW instead of SFW
    system_prompt: str = ""
    sandbox_profile: str = "safe"

    @classmethod
    def from_mongo(cls, doc):
        s = super().from_mongo(doc)
        if s:
            for name in SECRET_CONFIGS:
                cfg = getattr(s, name)
                cfg.api_key = dec(cfg.api_key)
        return s

    def to_mongo(self) -> dict:
        d = super().to_mongo()
        for name in SECRET_CONFIGS:
            d[name]["api_key"] = enc(d[name]["api_key"])
        return d


class Project(BaseDocument):
    name: str
    folder: str
    description: str = ""
    system_prompt: str = ""
    personality: str = ""
    index_status: str = "idle"
    index_error: str = ""
    indexed_at: Optional[str] = None
    chunk_count: int = 0
    embedded: bool = False
    git_repo: str = ""
    mode: Optional[str] = None  # the project's mode (modes.py; none: Projeto)
    allowed_commands: list[str] = []  # "Permitir sempre": these run without asking (exact command)
    created_at: str = Field(default_factory=now_iso)


class Session(BaseDocument):
    project_id: Optional[str] = None
    nsfw: bool = False  # the profile it was made in (nsfw_profile.py)
    title: str = "Nova sessão"
    mode: str = "chat"
    mode_id: Optional[str] = None  # the recipe (modes.py) the last turn used, when the page named one
    system_prompt: str = ""
    # Long conversations: running summary of the messages that left the history window
    summary: str = ""
    summary_upto: str = ""  # created_at of the last message folded into the summary
    # From the assistant panel (AIO) and still a single exchange: shown in the command log, not in the
    # sidebar. A second message from the user (a real conversation) clears it (see commands.py).
    assist_log: bool = False
    created_at: str = Field(default_factory=now_iso)
    updated_at: str = Field(default_factory=now_iso)


class Message(BaseDocument):
    session_id: str
    role: str
    content: str
    mode: str = "chat"
    model: str = ""
    provider: str = ""
    task: str = ""
    citations: list[dict] = []
    action_ids: list[str] = []
    attachments: list[str] = []
    skill_id: Optional[str] = None
    interrupted: bool = False  # the user stopped the generation; content is what had arrived
    tools: list[dict] = []  # tools used while answering: {id, name, args, status, summary, sources}
    tools_offered: list[str] = []  # tools the model had for this answer (Modo Full)
    mode_id: Optional[str] = None  # the recipe (modes.py) of this turn
    plan: bool = False  # its file changes are approved together (modes.py: /plans/{id}/approve)
    plan_status: str = ""  # executed | rejected | undone
    questions: list[dict] = []  # the card of questions she ended with (modes.extract_questions)
    answers: Optional[list[dict]] = None  # what the user picked: [{question, choices, other}]
    plan_doc: Optional[dict] = None  # the Planner's plan card: {title, summary, steps, files: [{path, op, why}]}
    plan_doc_status: str = ""  # proposed | approved | superseded
    plan_done: list[str] = []  # files of this plan the builds already wrote
    build: Optional[dict] = None  # a build of an approved plan: {plan_id, applied, outside, missing}
    marker: Optional[str] = None  # a message the app sent for the user (plan / build / continue): drawn as a step, not a bubble
    handoff: Optional[str] = None  # the Chat passed the turn to this action (the Planner): the page switches and asks for the plan
    created_at: str = Field(default_factory=now_iso)


class Memory(BaseDocument):
    project_id: str
    nsfw: bool = False  # the profile it was made in (nsfw_profile.py)
    kind: str = "fact"
    content: str
    source: str = "manual"
    created_at: str = Field(default_factory=now_iso)


class Skill(BaseDocument):
    name: str
    description: str = ""
    mode: str = "chat"
    template: str
    icon: str = "Sparkles"
    tags: list[str] = []
    next_skill_id: Optional[str] = None
    builtin: bool = False
    created_at: str = Field(default_factory=now_iso)


class Action(BaseDocument):
    project_id: str
    session_id: Optional[str] = None
    message_id: Optional[str] = None
    type: str
    path: str = ""
    command: str = ""
    old_content: Optional[str] = None
    new_content: Optional[str] = None
    base_hash: str = ""
    status: str = "pending"
    result: str = ""
    origin: str = "ai"
    auto: str = ""  # ran without asking: "plan" (a file of the approved plan) or "always" (an allowed command)
    created_at: str = Field(default_factory=now_iso)
    resolved_at: Optional[str] = None


class Chunk(BaseDocument):
    project_id: str
    source: str
    source_type: str = "file"
    idx: int = 0
    text: str
    embedding: Optional[list[float]] = None
    suspicious: bool = False


class AuditLog(BaseDocument):
    project_id: Optional[str] = None
    kind: str
    summary: str
    detail: dict[str, Any] = {}
    created_at: str = Field(default_factory=now_iso)


AUDIT_KEEP = 3000      # the audit log keeps the newest entries…
AUDIT_DAYS = 60        # …from the last two months
_audit_writes = 0


async def audit(kind: str, summary: str, project_id: Optional[str] = None, **detail):
    global _audit_writes
    await db.audit_logs.insert_one(AuditLog(project_id=project_id, kind=kind, summary=summary, detail=detail).to_mongo())
    _audit_writes += 1
    if _audit_writes % 200 == 1:  # the first write after starting, then every 200: it grows one entry per message
        await prune_audit()


async def prune_audit() -> int:
    before = (datetime.now(timezone.utc) - timedelta(days=AUDIT_DAYS)).isoformat()
    return await db.audit_logs.prune("created_at", AUDIT_KEEP, before)


async def get_settings() -> Settings:
    return Settings.from_mongo(await db.settings.find_one({"key": "global"})) or Settings()


async def get_project(pid: str) -> Project:
    p = Project.from_mongo(await db.projects.find_one({"_id": oid(pid)}))
    if not p:
        raise HTTPException(404, "Projeto não encontrado")
    return p


async def cut_session(sid: str, mid: str):
    """Removes the session's last user message (mid) and everything after it, so the turn can be
    sent again (edit / retry / regenerate). Pending actions proposed by removed answers are dropped."""
    last = await db.messages.find({"session_id": sid, "role": "user"}).sort("created_at", -1).to_list(1)
    if not last or str(last[0]["_id"]) != mid:
        raise HTTPException(400, "Só a última mensagem sua pode ser reenviada")
    gone = await db.messages.find({"session_id": sid, "created_at": {"$gte": last[0]["created_at"]}}).to_list(1000)
    ids = [str(m["_id"]) for m in gone]
    for a in await db.actions.find({"message_id": {"$in": ids}, "status": "pending"}).to_list(500):
        await db.actions.update_one({"_id": a["_id"]}, {"$set": {"status": "rejected", "result": "Mensagem substituída", "resolved_at": now_iso()}})
    await db.messages.delete_many({"session_id": sid, "created_at": {"$gte": last[0]["created_at"]}})
