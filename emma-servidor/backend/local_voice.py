"""Emma's voice without a paid service, on the PC: Whisper (faster-whisper) hears, Pocket TTS (Kyutai) speaks.

Both run on the computer's CPU, offline once their models are downloaded. They are big (Pocket TTS
brings PyTorch), so they are not in the app: Settings → Voz installs them on demand into the data
folder (pydeps/), and their models go to data/hf (Hugging Face cache).

Pocket TTS speaks Portuguese with its catalog voices; Emma's own voice is cloned from a short sample.
Cloning uses weights that Kyutai keeps behind a sign-up on Hugging Face (accept the terms on the model
page, create a token): the token is used for that one call and not stored; the clone is kept as
data/voices/emma.safetensors and loads without it.

Fish Audio stays as the other option; voice.py picks the engine from Settings (tts_engine / stt_engine).
"""
import asyncio
import importlib
import importlib.util
import io
import logging
import os
import sys
import tempfile
import wave
from pathlib import Path

from fastapi import APIRouter, File, Form, HTTPException, UploadFile
from pydantic import BaseModel

import pc_actions
from core import DB_PATH

router = APIRouter(prefix="/api")
log = logging.getLogger("local_voice")

DATA = Path(DB_PATH).parent
PYDEPS = Path(os.environ.get("LUMNA_PYDEPS") or DATA / "pydeps")
VOICES = DATA / "voices"
EMMA = VOICES / "emma.safetensors"
os.environ.setdefault("HF_HOME", str(DATA / "hf"))
if PYDEPS.is_dir() and str(PYDEPS) not in sys.path:
    sys.path.append(str(PYDEPS))  # after the app's own packages: never shadows them

PACKAGES = {"stt": ["faster-whisper"], "tts": ["pocket-tts"]}
MODULES = {"stt": "faster_whisper", "tts": "pocket_tts"}
LANGUAGE = "portuguese"
PT_VOICES = ["rafael"]  # Pocket TTS's Portuguese catalog voice (cloning adds Emma's)
WHISPER_SIZES = ["base", "small", "medium"]

_job: dict = {"what": None, "state": "idle", "log": [], "error": ""}
_whisper: dict = {"size": None, "model": None}
_tts: dict = {"model": None, "voice": None, "state": None}
_lock = asyncio.Lock()


def installed(what: str) -> bool:
    importlib.invalidate_caches()
    return importlib.util.find_spec(MODULES[what]) is not None


def ready(what: str) -> bool:
    return pc_actions.available() and installed(what)


def voices() -> list[str]:
    return (["emma"] if EMMA.exists() else []) + PT_VOICES


def status() -> dict:
    return {"available": pc_actions.available(), "stt": {"installed": installed("stt"), "sizes": WHISPER_SIZES},
            "tts": {"installed": installed("tts"), "voices": voices(), "emma": EMMA.exists()}, "job": _job}


# ---------- installing on demand ----------
async def _install(what: str) -> None:
    args = [sys.executable, "-m", "pip", "install", "--disable-pip-version-check", "--no-input", "--target", str(PYDEPS), *PACKAGES[what]]
    if what == "tts" and sys.platform.startswith("linux"):
        args += ["--extra-index-url", "https://download.pytorch.org/whl/cpu"]  # the CPU build: no gigabytes of CUDA
    PYDEPS.mkdir(parents=True, exist_ok=True)
    try:
        proc = await asyncio.create_subprocess_exec(*args, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
        async for line in proc.stdout:
            text = line.decode(errors="replace").strip()
            if text:
                _job["log"] = (_job["log"] + [text[:200]])[-8:]
        if await proc.wait() != 0:
            raise RuntimeError("a instalação falhou (veja as últimas linhas)")
        if str(PYDEPS) not in sys.path:
            sys.path.append(str(PYDEPS))
        if not installed(what):
            raise RuntimeError("instalado, mas não carregou")
        _job.update(state="done")
    except Exception as e:  # noqa: BLE001 — shown in Settings
        _job.update(state="error", error=str(e)[:300])


class InstallIn(BaseModel):
    what: str


@router.get("/local-voice")
async def get_status():
    return status()


@router.post("/local-voice/install")
async def install(body: InstallIn):
    if not pc_actions.available():
        raise HTTPException(400, "A voz local funciona com a LUMNA rodando direto no computador.")
    if body.what not in PACKAGES:
        raise HTTPException(400, "Opção desconhecida")
    if _job["state"] == "running":
        raise HTTPException(409, "Já tem uma instalação em andamento")
    _job.update(what=body.what, state="running", log=[], error="")
    asyncio.create_task(_install(body.what))
    return status()


# ---------- hearing: Whisper ----------
def _load_whisper(size: str):
    from faster_whisper import WhisperModel  # noqa: PLC0415 (installed on demand)
    if _whisper["size"] != size:
        _whisper.update(size=size, model=WhisperModel(size, device="cpu", compute_type="int8"))
    return _whisper["model"]


async def transcribe(data: bytes, filename: str, size: str = "base", language: str = "pt") -> str:
    size = size if size in WHISPER_SIZES else "base"
    suffix = Path(filename or "fala.webm").suffix or ".webm"

    def run() -> str:
        model = _load_whisper(size)
        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as f:
            f.write(data)
            path = f.name
        try:
            segments, _ = model.transcribe(path, language=(language or "pt")[:2], vad_filter=True, initial_prompt="Emma, LUMNA.")
            return " ".join(s.text.strip() for s in segments).strip()
        finally:
            os.unlink(path)

    async with _lock:
        return await asyncio.to_thread(run)


# ---------- speaking: Pocket TTS ----------
def _voice_state(voice: str):
    if _tts["model"] is None:
        from pocket_tts import TTSModel  # noqa: PLC0415 (installed on demand)
        _tts["model"] = TTSModel.load_model(language=LANGUAGE)
    if _tts["voice"] != voice:
        prompt = str(EMMA) if voice == "emma" and EMMA.exists() else (voice if voice in PT_VOICES else PT_VOICES[0])
        _tts.update(voice=voice, state=_tts["model"].get_state_for_audio_prompt(prompt))
    return _tts["model"], _tts["state"]


def _wav(samples, rate: int) -> bytes:
    import numpy as np  # noqa: PLC0415
    pcm = (np.clip(samples, -1, 1) * 32767).astype("<i2").tobytes()
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return out.getvalue()


async def synthesize(text: str, voice: str = "") -> bytes:
    def run() -> bytes:
        model, state = _voice_state(voice or ("emma" if EMMA.exists() else PT_VOICES[0]))
        audio = model.generate_audio(state, text)
        return _wav(audio.numpy(), model.sample_rate)

    async with _lock:
        return await asyncio.to_thread(run)


@router.post("/local-voice/clone")
async def clone(audio: UploadFile = File(...), hf_token: str = Form(...)):
    """Emma's voice from a sample (5–30 s of her speaking, clean): kept as emma.safetensors."""
    if not ready("tts"):
        raise HTTPException(400, "Instale a voz local (Pocket TTS) primeiro.")
    data = await audio.read(30 << 20)
    if not data:
        raise HTTPException(400, "Amostra vazia")
    suffix = Path(audio.filename or "amostra.wav").suffix or ".wav"

    def run():
        from pocket_tts import TTSModel, export_model_state  # noqa: PLC0415
        with tempfile.NamedTemporaryFile(suffix=suffix, delete=False) as f:
            f.write(data)
            path = f.name
        old = os.environ.get("HF_TOKEN")
        os.environ["HF_TOKEN"] = hf_token.strip()  # only for downloading the cloning weights, now
        try:
            model = TTSModel.load_model(language=LANGUAGE)
            state = model.get_state_for_audio_prompt(path)
            VOICES.mkdir(parents=True, exist_ok=True)
            export_model_state(state, str(EMMA))
            _tts.update(model=model, voice="emma", state=state)
        finally:
            os.unlink(path)
            if old is None:
                os.environ.pop("HF_TOKEN", None)
            else:
                os.environ["HF_TOKEN"] = old

    try:
        async with _lock:
            await asyncio.to_thread(run)
    except ValueError as e:
        if "voice cloning" in str(e):
            raise HTTPException(400, "O Hugging Face não liberou o modelo de clonagem: aceite os termos em "
                                     "huggingface.co/kyutai/pocket-tts com a sua conta e confira o token.")
        raise HTTPException(400, f"Não consegui clonar a voz: {e}"[:300])
    except Exception as e:  # noqa: BLE001
        log.warning("clonagem falhou: %s", e)
        raise HTTPException(400, f"Não consegui clonar a voz: {e}"[:300])
    return status()


@router.delete("/local-voice/emma")
async def forget_emma():
    EMMA.unlink(missing_ok=True)
    if _tts["voice"] == "emma":
        _tts.update(voice=None, state=None)
    return status()


def engine_ready(engine_setting: str, what: str) -> bool:
    """The local engine is chosen and usable."""
    return engine_setting == "local" and ready(what)

