"""Images: chat attachments (for model vision, or generated), project uploads and raw image reads."""
import base64
import os
import re
import uuid
from pathlib import Path
from typing import Optional

import httpx
from fastapi import APIRouter, File, Form, HTTPException, UploadFile
from fastapi.responses import FileResponse, Response
from openai import AsyncOpenAI
from pydantic import BaseModel, Field

import workspace_runtime as wr
from core import DB_PATH, cut_session, ImageGenConfig, Message, Session, audit, db, get_project, get_settings, now_iso, oid
from netguard import assert_public_url
import nsfw_profile

router = APIRouter(prefix="/api")

MAX_BYTES = 10 << 20
ATTACH_DIR = Path(os.environ.get("ATTACHMENTS_DIR") or Path(DB_PATH).parent / "attachments")
EXT = {"image/png": "png", "image/jpeg": "jpg", "image/webp": "webp", "image/gif": "gif"}
ID_RE = re.compile(r"^[0-9a-f]{32}$")
# Served bytes never run as a page: images only, sandboxed, no MIME sniffing.
SAFE_HEADERS = {"Content-Security-Policy": "sandbox", "X-Content-Type-Options": "nosniff", "Cache-Control": "private, max-age=86400"}


def sniff_image(data: bytes) -> Optional[str]:
    """MIME type from the file's magic bytes (the client's content type is not trusted)."""
    if data.startswith(b"\x89PNG\r\n\x1a\n"):
        return "image/png"
    if data[:3] == b"\xff\xd8\xff":
        return "image/jpeg"
    if data[:6] in (b"GIF87a", b"GIF89a"):
        return "image/gif"
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return "image/webp"
    return None


async def read_upload(upload: UploadFile) -> bytes:
    data = await upload.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise HTTPException(413, f"Arquivo maior que {MAX_BYTES >> 20} MB")
    if not data:
        raise HTTPException(400, "Arquivo vazio")
    return data


MAX_TEXT_BYTES = 1 << 20
MAX_EXTRACT_CHARS = 400_000


def public(doc: dict) -> dict:
    return {"id": doc["_id"], "mime": doc["mime"], "name": doc.get("name", ""), "size": doc["size"], "kind": doc.get("kind", "upload"),
            "type": doc.get("type", "image")}


def as_text(data: bytes) -> Optional[str]:
    """The bytes as text when they are UTF-8 text (code, markdown, csv, logs…), else None."""
    if b"\x00" in data[:8192]:
        return None
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return None


def pdf_text(data: bytes) -> str:
    import io

    from pypdf import PdfReader
    try:
        reader = PdfReader(io.BytesIO(data))
        if reader.is_encrypted:
            reader.decrypt("")
        pages = []
        for i, page in enumerate(reader.pages):
            pages.append(f"--- página {i + 1} ---\n{page.extract_text() or ''}")
            if sum(len(p) for p in pages) > MAX_EXTRACT_CHARS:
                break
    except Exception as e:
        raise HTTPException(415, f"Não consegui ler o PDF: {str(e)[:120]}")
    text = "\n".join(pages).strip()
    if not text.replace("-", "").replace("página", "").strip(" \n0123456789"):
        raise HTTPException(415, "O PDF não tem texto extraível (provavelmente é escaneado)")
    return text[:MAX_EXTRACT_CHARS]


async def store_file(data: bytes, name: str) -> dict:
    """Any chat attachment: images (for vision), PDFs and text/code files (their text goes to the model)."""
    if sniff_image(data):
        return await store_image(data, name)
    if data.startswith(b"%PDF-"):
        typ, mime, text = "pdf", "application/pdf", pdf_text(data)
    else:
        text = as_text(data)
        if text is None:
            raise HTTPException(415, "Tipo não suportado: envie imagens, PDFs ou arquivos de texto/código")
        if len(data) > MAX_TEXT_BYTES:
            raise HTTPException(413, f"Arquivo de texto maior que {MAX_TEXT_BYTES >> 20} MB")
        typ, mime = "text", "text/plain; charset=utf-8"
    ATTACH_DIR.mkdir(parents=True, exist_ok=True)
    aid = uuid.uuid4().hex
    (ATTACH_DIR / aid).write_bytes(data)
    (ATTACH_DIR / f"{aid}.txt").write_text(text, encoding="utf-8")
    doc = {"_id": aid, "mime": mime, "type": typ, "name": (name or ("documento.pdf" if typ == "pdf" else "arquivo.txt"))[:120], "size": len(data),
           "kind": "upload", "chars": len(text), "created_at": now_iso()}
    await db.attachments.insert_one(doc)
    return public(doc)


async def load_attachment(aid: str) -> dict:
    if not ID_RE.match(aid or ""):
        raise HTTPException(404, "Anexo não encontrado")
    doc = await db.attachments.find_one({"_id": aid})
    if not doc or not (ATTACH_DIR / aid).is_file():
        raise HTTPException(404, "Anexo não encontrado")
    return doc


def attachment_text(aid: str) -> str:
    path = ATTACH_DIR / f"{aid}.txt"
    return path.read_text(encoding="utf-8") if path.is_file() else ""


async def store_image(data: bytes, name: str, kind: str = "upload") -> dict:
    mime = sniff_image(data)
    if not mime:
        raise HTTPException(415, "Só imagens PNG, JPEG, WebP ou GIF")
    ATTACH_DIR.mkdir(parents=True, exist_ok=True)
    aid = uuid.uuid4().hex
    (ATTACH_DIR / aid).write_bytes(data)
    doc = {"_id": aid, "mime": mime, "name": (name or f"imagem.{EXT[mime]}")[:120], "size": len(data), "kind": kind, "created_at": now_iso()}
    await db.attachments.insert_one(doc)
    return public(doc)


async def load_image(aid: str) -> tuple[bytes, dict]:
    if not ID_RE.match(aid or ""):
        raise HTTPException(404, "Anexo não encontrado")
    doc = await db.attachments.find_one({"_id": aid})
    path = ATTACH_DIR / aid
    if not doc or not path.is_file():
        raise HTTPException(404, "Anexo não encontrado")
    return path.read_bytes(), doc


def safe_name(name: str, fallback: str) -> str:
    base = re.sub(r"[^A-Za-z0-9._-]+", "-", Path(name or "").name).strip(".-")
    return base[:100] or fallback


def free_path(folder: str, rel: str) -> str:
    """rel, or rel with a -1/-2… suffix when a file already exists there."""
    stem, dot, ext = rel.rpartition(".") if "." in Path(rel).name else (rel, "", "")
    candidate, n = rel, 1
    while wr.safe_path(folder, candidate).exists():
        candidate = f"{stem}-{n}{dot}{ext}"
        n += 1
    return candidate


@router.post("/attachments")
async def upload_attachment(file: UploadFile = File(...)):
    return await store_file(await read_upload(file), safe_name(file.filename or "", "arquivo"))


@router.get("/attachments/{aid}")
async def get_attachment(aid: str):
    doc = await load_attachment(aid)
    if doc.get("type", "image") == "image":
        return FileResponse(ATTACH_DIR / aid, media_type=doc["mime"], headers=SAFE_HEADERS)
    # Documents are only ever downloaded, never rendered by the app's origin.
    return FileResponse(ATTACH_DIR / aid, media_type="application/octet-stream", filename=doc.get("name") or "arquivo", headers=SAFE_HEADERS)


@router.get("/attachments/{aid}/meta")
async def attachment_meta(aid: str):
    return public(await load_attachment(aid))


@router.get("/attachments/{aid}/text")
async def get_attachment_text(aid: str):
    doc = await load_attachment(aid)
    return {**public(doc), "text": attachment_text(aid)}


class SaveIn(BaseModel):
    project_id: str
    path: Optional[str] = None


@router.post("/attachments/{aid}/save-to-project")
async def save_to_project(aid: str, body: SaveIn):
    doc = await load_attachment(aid)
    data = (ATTACH_DIR / aid).read_bytes()
    p = await get_project(body.project_id)
    fallback = "imagem." + EXT[doc["mime"]] if doc["mime"] in EXT else "arquivo"
    rel = (body.path or f"{'assets' if doc.get('type', 'image') == 'image' else 'docs'}/{safe_name(doc.get('name', ''), fallback)}").strip().lstrip("/")
    rel = free_path(p.folder, rel)
    wr.write_bytes(p.folder, rel, data)
    await audit("file", f"Imagem salva no projeto: {rel}", p.id, origin="user")
    return {"ok": True, "path": rel}


@router.post("/projects/{pid}/upload")
async def upload_to_project(pid: str, file: UploadFile = File(...), dir: str = Form("")):
    p = await get_project(pid)
    data = await read_upload(file)
    rel = "/".join(x for x in (dir.strip().strip("/"), safe_name(file.filename or "", "arquivo")) if x)
    rel = free_path(p.folder, rel)
    wr.write_bytes(p.folder, rel, data)
    await audit("file", f"Upload para o projeto: {rel}", p.id, origin="user")
    return {"ok": True, "path": rel}


@router.get("/projects/{pid}/raw")
async def raw_image(pid: str, path: str):
    p = await get_project(pid)
    target = wr.safe_path(p.folder, path)
    if not target.is_file():
        raise HTTPException(404, "Arquivo não encontrado")
    if target.stat().st_size > MAX_BYTES * 2:
        raise HTTPException(413, "Imagem grande demais para exibir")
    data = target.read_bytes()
    mime = sniff_image(data)
    if not mime:
        raise HTTPException(415, "Não é uma imagem suportada")
    return Response(data, media_type=mime, headers=SAFE_HEADERS)


# ---- Image generation (OpenAI-compatible /images/generations) ----
async def fetch_image(url: str) -> bytes:
    """Downloads a generated image by URL, re-checking every redirect against the SSRF guard."""
    async with httpx.AsyncClient(timeout=60, follow_redirects=False) as c:
        target = url
        for _ in range(5):
            assert_public_url(target)
            async with c.stream("GET", target) as r:
                if r.is_redirect:
                    target = str(r.next_request.url) if r.next_request else r.headers["location"]
                    continue
                r.raise_for_status()
                data = b""
                async for chunk in r.aiter_bytes():
                    data += chunk
                    if len(data) > MAX_BYTES:
                        raise HTTPException(502, "Imagem gerada grande demais")
                return data
    raise HTTPException(502, "Redirecionamentos demais ao baixar a imagem")


async def generate_image(cfg: ImageGenConfig, prompt: str) -> bytes:
    if not cfg.api_key:
        raise HTTPException(400, "Configure a geração de imagens em Configurações → Imagens")
    assert_public_url(cfg.base_url)
    client = AsyncOpenAI(base_url=cfg.base_url, api_key=cfg.api_key, timeout=240, max_retries=1)
    kw = {"model": cfg.model, "prompt": prompt, "n": 1}
    if cfg.size.strip():
        kw["size"] = cfg.size.strip()
    try:
        resp = await client.images.generate(**kw)
    except Exception as e:
        raise HTTPException(502, f"Falha na API de imagens: {str(e)[:300]}")
    item = (resp.data or [None])[0]
    if item and item.b64_json:
        return base64.b64decode(item.b64_json)
    if item and item.url:
        try:
            return await fetch_image(item.url)
        except httpx.HTTPError as e:
            raise HTTPException(502, f"Falha ao baixar a imagem gerada: {e}")
    # Providers such as Gemini answer 200 with no image when their safety filter blocks the prompt.
    raise HTTPException(422, "A API respondeu sem nenhuma imagem: o prompt provavelmente foi bloqueado pelo filtro de conteúdo do provedor. Tente reformular.")


class ImageIn(BaseModel):
    prompt: str = Field(min_length=1, max_length=4000)
    mode: str = "chat"
    replace_from: Optional[str] = None


@router.post("/sessions/{sid}/image")
async def session_image(sid: str, body: ImageIn):
    doc = await db.sessions.find_one({"_id": oid(sid)})
    session = Session.from_mongo(doc) if await nsfw_profile.visible(doc) else None
    if not session:
        raise HTTPException(404, "Sessão não encontrada")
    cfg = (await get_settings()).image_gen
    if not cfg.enabled:
        raise HTTPException(400, "Geração de imagens desativada (Configurações → Imagens)")
    if body.mode not in ("chat", "plan", "build", "workspace"):
        body.mode = "chat"
    prompt = body.prompt.strip()
    # Generated first: a failed request leaves no half-written turn in the session.
    att = await store_image(await generate_image(cfg, prompt), safe_name(prompt[:40], "imagem"), kind="generated")
    if body.replace_from:
        await cut_session(sid, body.replace_from)
    user_msg = Message(session_id=sid, role="user", content=f"/imagem {prompt}", mode=body.mode)
    user_msg.id = str((await db.messages.insert_one(user_msg.to_mongo())).inserted_id)
    am = Message(session_id=sid, role="assistant", content="", mode=body.mode, model=cfg.model, provider="imagens", task="image", attachments=[att["id"]])
    am.id = str((await db.messages.insert_one(am.to_mongo())).inserted_id)
    upd = {"updated_at": now_iso()}
    if session.title == "Nova sessão":
        upd["title"] = re.sub(r"\s+", " ", prompt)[:60]
    await db.sessions.update_one({"_id": oid(sid)}, {"$set": upd})
    await audit("image", f"Imagem gerada ({cfg.model}): {prompt[:80]}", session.project_id, session_id=sid)
    return {"user_message": user_msg.out(), "message": am.out()}


@router.post("/settings/image/test")
async def test_image_gen():
    cfg = (await get_settings()).image_gen
    try:
        data = await generate_image(cfg, "a small red circle on a white background")
    except HTTPException as e:
        return {"ok": False, "error": e.detail}
    mime = sniff_image(data)
    return {"ok": bool(mime), "mime": mime, "size": len(data), **({} if mime else {"error": "A resposta não é uma imagem"})}
