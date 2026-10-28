"""Voice: text to speech (reading answers aloud) and speech to text (the mic) — Fish Audio, or the PC's local engines (local_voice.py)."""
import os
import re

import httpx
from fastapi import APIRouter, File, HTTPException, UploadFile
from fastapi.responses import Response
from pydantic import BaseModel, Field

import local_voice
from core import VoiceConfig, get_settings

router = APIRouter(prefix="/api")

# Overridable only for local tests (fake server); the key never goes anywhere else.
FISH_BASE = os.environ.get("FISH_AUDIO_BASE_URL", "https://api.fish.audio").rstrip("/")
MAX_TTS_CHARS = 4000
MAX_AUDIO_BYTES = 20 << 20


def clean_for_speech(text: str) -> str:
    """Markdown to plain speech: drops code blocks and HARNESS action blocks, links keep their label."""
    text = re.sub(r"<<<HARNESS:[\s\S]*?(<<<END>>>|$)", " ", text)
    text = re.sub(r"```[\s\S]*?(```|$)", " (bloco de código) ", text)
    text = re.sub(r"!\[[^\]]*\]\([^)]*\)", " ", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"^\s{0,3}(#{1,6}|>|[-*+]|\d+\.)\s+", "", text, flags=re.M)
    text = re.sub(r"(\*\*|__|\*|_|~~)", "", text)
    return re.sub(r"[ \t]+", " ", re.sub(r"\n{3,}", "\n\n", text)).strip()


async def _cfg() -> VoiceConfig:
    cfg = (await get_settings()).voice
    if not cfg.api_key:
        raise HTTPException(400, "Configure a chave do Fish Audio em Configurações → Voz")
    return cfg


FREE_TTS_MODEL = "s2.1-pro-free"


def _headers(cfg: VoiceConfig, model: str) -> dict:
    h = {"Authorization": f"Bearer {cfg.api_key}"}
    if model.strip():
        h["model"] = model.strip()
    return h


def _fail(r: httpx.Response, what: str):
    try:
        detail = r.json().get("message") or r.text
    except ValueError:
        detail = r.text
    raise HTTPException(502, f"Fish Audio ({what}) respondeu {r.status_code}: {str(detail)[:200]}")


async def synthesize(cfg: VoiceConfig, text: str) -> bytes:
    body = {"text": text, "format": "mp3"}
    if cfg.voice_id.strip():
        body["reference_id"] = cfg.voice_id.strip()
    try:
        async with httpx.AsyncClient(timeout=120) as c:
            # empty (saved blank by a form): the free test model, not Fish Audio's own (paid) default
            r = await c.post(f"{FISH_BASE}/v1/tts", json=body, headers=_headers(cfg, cfg.tts_model.strip() or FREE_TTS_MODEL))
    except httpx.HTTPError as e:
        raise HTTPException(502, f"Falha ao falar com o Fish Audio: {e}")
    if r.status_code != 200:
        _fail(r, "TTS")
    return r.content


class TtsIn(BaseModel):
    text: str = Field(min_length=1, max_length=50_000)


@router.post("/tts")
async def tts(body: TtsIn):
    text = clean_for_speech(body.text)[:MAX_TTS_CHARS]
    if not text:
        raise HTTPException(400, "Nada para ler")
    voice = (await get_settings()).voice
    if local_voice.engine_ready(voice.tts_engine, "tts"):
        return Response(await local_voice.synthesize(text, voice.local_voice), media_type="audio/wav", headers={"Cache-Control": "no-store"})
    cfg = await _cfg()
    return Response(await synthesize(cfg, text), media_type="audio/mpeg", headers={"Cache-Control": "no-store"})


@router.post("/stt")
async def stt(audio: UploadFile = File(...)):
    data = await audio.read(MAX_AUDIO_BYTES + 1)
    if len(data) > MAX_AUDIO_BYTES:
        raise HTTPException(413, "Áudio longo demais")
    if not data:
        raise HTTPException(400, "Áudio vazio")
    voice = (await get_settings()).voice
    if local_voice.engine_ready(voice.stt_engine, "stt"):
        return {"text": await local_voice.transcribe(data, audio.filename or "", voice.whisper_model, voice.language)}
    cfg = await _cfg()
    form = {"ignore_timestamps": "true"}
    if cfg.language.strip():
        form["language"] = cfg.language.strip()
    try:
        async with httpx.AsyncClient(timeout=120) as c:
            r = await c.post(f"{FISH_BASE}/v1/asr", data=form, files={"audio": (audio.filename or "audio.webm", data, audio.content_type or "application/octet-stream")},
                             headers=_headers(cfg, cfg.asr_model))
    except httpx.HTTPError as e:
        raise HTTPException(502, f"Falha ao falar com o Fish Audio: {e}")
    if r.status_code != 200:
        _fail(r, "transcrição")
    return {"text": (r.json().get("text") or "").strip()}

