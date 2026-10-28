"""Wake word on the PC ("Oi Emma" / "Ei Emma"): the computer's microphone, always listening, offline.

Vosk (offline speech recognition, Portuguese model ~50 MB) listens with a closed grammar: the two
wake phrases plus a list of look-alike words ("tema", "poema", "sistema"…) that absorb ordinary
speech, so "hoje o tema" is heard as "o tema", not as "oi ema". The model doesn't know "Emma" — it
spells her "ema". Nothing is recorded or sent anywhere: only the decision leaves this module.

When it hears her name, the open LUMNA page (browser or installed PWA) is told through a long poll
(GET /api/wake/wait) and opens the Live Speech already listening.

Only where the backend runs directly on a computer (pc_actions.available()), with the optional
packages `vosk` and `sounddevice` installed (pip install vosk sounddevice). The model is downloaded
from Settings. LUMNA_WAKE_INPUT=<file of 16 kHz mono 16-bit PCM> replaces the microphone (tests).
"""
import asyncio
import json
import logging
import os
import shutil
import threading
import time
import zipfile
from pathlib import Path
from typing import Optional

import httpx
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

import pc_actions
from core import DB_PATH, db

router = APIRouter(prefix="/api")
log = logging.getLogger("wake")

MODEL_NAME = "vosk-model-small-pt-0.3"
MODEL_URL = f"https://alphacephei.com/vosk/models/{MODEL_NAME}.zip"
MODELS_DIR = Path(os.environ.get("LUMNA_WAKE_MODELS") or Path(DB_PATH).parent / "models")
RATE = 16000
BLOCK = RATE // 10  # 100 ms
PHRASES = ["oi ema", "ei ema"]
# Words that sound like the wake phrase: in the grammar they win over it when that's what was said.
DECOYS = ["tema", "poema", "sistema", "problema", "esquema", "cinema", "gema", "lema", "uma", "ela", "o", "hoje",
          "me", "meu", "eu", "espera", "oi", "ei", "[unk]"]
MIN_CONF = 0.5
COOLDOWN = 3.0  # seconds: one "Oi Emma" opens one conversation

_state: dict = {"running": False, "error": "", "count": 0, "last_heard": None, "download": None}
_thread: Optional[threading.Thread] = None
_stop = threading.Event()
_waiters: set[asyncio.Future] = set()
_loop: Optional[asyncio.AbstractEventLoop] = None


def _has(module: str) -> bool:
    try:
        __import__(module)
        return True
    except Exception:  # noqa: BLE001 — a broken install counts as missing
        return False


def model_dir() -> Path:
    return MODELS_DIR / MODEL_NAME


def model_ready() -> bool:
    return (model_dir() / "final.mdl").exists() or (model_dir() / "am" / "final.mdl").exists()


def heard_wake(result: dict) -> bool:
    """A Vosk result (with words) holds "oi/ei" + "ema", both confidently."""
    words = result.get("result") or []
    for a, b in zip(words, words[1:]):
        if a["word"] in ("oi", "ei") and b["word"] == "ema" and a.get("conf", 0) >= MIN_CONF and b.get("conf", 0) >= MIN_CONF:
            return True
    return False


class Detector:
    """Feeds 16 kHz PCM to Vosk; True when the wake phrase was just heard."""

    def __init__(self, path: Path):
        import vosk  # optional dependency

        vosk.SetLogLevel(-1)
        self.model = vosk.Model(str(path))
        self.vosk = vosk
        self.grammar = json.dumps(PHRASES + DECOYS)
        self.last = 0.0
        self._new()

    def _new(self):
        self.rec = self.vosk.KaldiRecognizer(self.model, RATE, self.grammar)
        self.rec.SetWords(True)

    def feed(self, pcm: bytes) -> bool:
        if not self.rec.AcceptWaveform(pcm):
            return False
        hit = heard_wake(json.loads(self.rec.Result()))
        if hit and time.monotonic() - self.last >= COOLDOWN:
            self.last = time.monotonic()
            self._new()
            return True
        return False

    def flush(self) -> bool:
        return heard_wake(json.loads(self.rec.FinalResult()))


def _heard():
    _state["count"] += 1
    _state["last_heard"] = time.time()
    log.info("palavra de ativação ouvida")
    for f in list(_waiters):
        if not f.done():
            f.set_result(True)


def _notify():
    if _loop:
        _loop.call_soon_threadsafe(_heard)


def _listen():
    try:
        det = Detector(model_dir())
        source = os.environ.get("LUMNA_WAKE_INPUT")
        if source:  # tests: a file instead of the microphone, then keep "listening" to silence
            data = Path(source).read_bytes()
            for i in range(0, len(data), BLOCK * 2):
                if _stop.is_set():
                    return
                if det.feed(data[i:i + BLOCK * 2]):
                    _notify()
                time.sleep(BLOCK / RATE)  # at the microphone's pace
            if det.flush():
                _notify()
            _stop.wait()
            return
        import sounddevice as sd  # optional dependency

        with sd.RawInputStream(samplerate=RATE, blocksize=BLOCK, channels=1, dtype="int16") as mic:
            while not _stop.is_set():
                pcm, _overflow = mic.read(BLOCK)
                if det.feed(bytes(pcm)):
                    _notify()
    except Exception as e:  # noqa: BLE001 — shown in Settings
        _state["error"] = f"{e.__class__.__name__}: {e}"[:300]
        log.warning("palavra de ativação parou: %s", e)
    finally:
        _state["running"] = False


def start() -> None:
    global _thread, _loop
    if _thread and _thread.is_alive():
        return
    _loop = asyncio.get_running_loop()
    _stop.clear()
    _state.update(running=True, error="")
    _thread = threading.Thread(target=_listen, name="wake-word", daemon=True)
    _thread.start()


def stop() -> None:
    global _thread
    _stop.set()
    if _thread:
        _thread.join(timeout=2)
    _thread = None
    _state["running"] = False


async def _enabled() -> bool:
    doc = await db.wake.find_one({"key": "config"})
    return bool(doc and doc.get("enabled"))


def status_info() -> dict:
    deps = {"vosk": _has("vosk"), "sounddevice": _has("sounddevice") or bool(os.environ.get("LUMNA_WAKE_INPUT"))}
    return {"available": pc_actions.available(), "deps": deps, "model": model_ready(), "model_name": MODEL_NAME,
            "phrases": ["Oi Emma", "Ei Emma"], **{k: _state[k] for k in ("running", "error", "count", "last_heard", "download")}}


async def on_start() -> None:
    """The backend started: listening resumes if it was on and everything is in place."""
    info = status_info()
    if await _enabled() and info["available"] and all(info["deps"].values()) and info["model"]:
        start()


@router.get("/wake")
async def get_wake():
    return {**status_info(), "enabled": await _enabled()}


class WakeIn(BaseModel):
    enabled: bool


@router.put("/wake")
async def put_wake(body: WakeIn):
    info = status_info()
    if body.enabled:
        if not info["available"]:
            raise HTTPException(400, "A palavra de ativação funciona só com o LUMNA rodando direto no computador.")
        missing = [k for k, ok in info["deps"].items() if not ok]
        if missing:
            raise HTTPException(400, f"Falta instalar no Python do LUMNA: pip install {' '.join(missing)}")
        if not info["model"]:
            raise HTTPException(400, "Baixe o modelo de voz primeiro.")
    await db.wake.update_one({"key": "config"}, {"$set": {"key": "config", "enabled": body.enabled}}, upsert=True)
    if body.enabled:
        start()
        await asyncio.sleep(0.3)  # an immediate failure (no microphone) shows up right away
    else:
        stop()
    return await get_wake()


async def _download():
    tmp = MODELS_DIR / f"{MODEL_NAME}.zip.part"
    try:
        MODELS_DIR.mkdir(parents=True, exist_ok=True)
        async with httpx.AsyncClient(timeout=httpx.Timeout(30, read=120), follow_redirects=True) as c:
            async with c.stream("GET", MODEL_URL) as r:
                if r.status_code != 200:
                    raise RuntimeError(f"download falhou (código {r.status_code})")
                total = int(r.headers.get("content-length") or 0) or 1
                got = 0
                with open(tmp, "wb") as f:
                    async for chunk in r.aiter_bytes(1 << 20):
                        f.write(chunk)
                        got += len(chunk)
                        _state["download"] = {"state": "downloading", "progress": min(99, got * 100 // total)}

        def extract():
            with zipfile.ZipFile(tmp) as z:
                for n in z.namelist():  # only inside the model's own folder
                    if not n.startswith(MODEL_NAME + "/") or ".." in Path(n).parts:
                        raise RuntimeError("arquivo do modelo inesperado")
                shutil.rmtree(model_dir(), ignore_errors=True)
                z.extractall(MODELS_DIR)
        await asyncio.to_thread(extract)
        _state["download"] = {"state": "done", "progress": 100}
    except Exception as e:  # noqa: BLE001 — shown in Settings
        _state["download"] = {"state": "error", "progress": 0, "error": str(e)[:300] or e.__class__.__name__}
    finally:
        tmp.unlink(missing_ok=True)


@router.post("/wake/model")
async def download_model():
    if not pc_actions.available():
        raise HTTPException(400, "A palavra de ativação funciona só com o LUMNA rodando direto no computador.")
    if (_state["download"] or {}).get("state") != "downloading":
        _state["download"] = {"state": "downloading", "progress": 0}
        asyncio.create_task(_download())
    return status_info()


@router.get("/wake/wait")
async def wait(seen: int = 0, timeout: float = 25):
    """Long poll: returns as soon as the wake phrase was heard more times than `seen` (or after `timeout`)."""
    if _state["count"] > seen:
        return {"count": _state["count"], "heard": True}
    f = asyncio.get_running_loop().create_future()
    _waiters.add(f)
    try:
        await asyncio.wait_for(f, timeout=min(max(timeout, 1), 55))
    except asyncio.TimeoutError:
        pass
    finally:
        _waiters.discard(f)
    return {"count": _state["count"], "heard": _state["count"] > seen}
