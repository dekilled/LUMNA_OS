import asyncio
import html
import logging
import math
import os
import re
from collections import Counter

import httpx
import numpy as np

from core import Chunk, Project, db, get_settings, now_iso, oid
from llm import embed
from netguard import assert_public_url
from workspace_runtime import IGNORED_DIRS, is_binary, project_root

log = logging.getLogger("rag")

TEXT_EXT = {".md", ".txt", ".py", ".js", ".jsx", ".ts", ".tsx", ".json", ".html", ".css", ".scss", ".yml", ".yaml", ".toml", ".ini", ".sh", ".sql", ".go", ".rs", ".java", ".kt", ".rb", ".php", ".c", ".h", ".cpp", ".vue", ".svelte", ".env.example", ".cfg", ".xml", ".csv", ""}
MAX_FILE = 300_000
CHUNK = 1200
OVERLAP = 200
SUSPICIOUS = re.compile(
    r"(ignore (all |the )?(previous|prior|above) instructions|ignore as instru[cç][õo]es|disregard (the )?system|you are now|"
    r"system prompt|reveal (your|the) (prompt|key)|api[_ ]?key|rm -rf|exfiltrat|esque[çc]a (as|todas))",
    re.I,
)
TOKEN = re.compile(r"\w+", re.U)


def chunk_text(text: str) -> list[str]:
    text = text.strip()
    if not text:
        return []
    out, i = [], 0
    while i < len(text):
        out.append(text[i:i + CHUNK])
        i += CHUNK - OVERLAP
    return out


def _file_chunks(project: Project) -> list[Chunk]:
    base = project_root(project.folder)
    chunks: list[Chunk] = []
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = [d for d in dirnames if d not in IGNORED_DIRS and not d.startswith(".")]
        for fn in filenames:
            p = os.path.join(dirpath, fn)
            if os.path.islink(p):
                continue
            ext = os.path.splitext(fn)[1].lower()
            if ext not in TEXT_EXT or os.path.getsize(p) > MAX_FILE:
                continue
            with open(p, "rb") as f:
                data = f.read()
            if is_binary(data):
                continue
            rel = os.path.relpath(p, base)
            for i, t in enumerate(chunk_text(data.decode("utf-8", "replace"))):
                chunks.append(Chunk(project_id=project.id, source=rel, source_type="file", idx=i, text=t, suspicious=bool(SUSPICIOUS.search(t))))
    return chunks


async def _store(chunks: list[Chunk]) -> bool:
    embedded = False
    if chunks:
        try:
            vecs = await embed([c.text for c in chunks])
            if vecs:
                for c, v in zip(chunks, vecs):
                    c.embedding = v
                embedded = True
        except Exception as e:
            log.warning("embedding failed, lexical fallback: %s", e)
        await db.rag_chunks.insert_many([c.to_mongo() for c in chunks])
    return embedded


async def index_project(project: Project):
    pid = project.id
    await db.projects.update_one({"_id": oid(pid)}, {"$set": {"index_status": "indexing", "index_error": ""}})
    try:
        chunks = _file_chunks(project)
        await db.rag_chunks.delete_many({"project_id": pid, "source_type": "file"})
        forget(pid)
        embedded = await _store(chunks)
        forget(pid)
        total = await db.rag_chunks.count_documents({"project_id": pid})
        await db.projects.update_one({"_id": oid(pid)}, {"$set": {"index_status": "ready", "indexed_at": now_iso(), "chunk_count": total, "embedded": embedded}})
    except Exception as e:
        log.exception("index failed")
        await db.projects.update_one({"_id": oid(pid)}, {"$set": {"index_status": "error", "index_error": str(e)[:300]}})


def _html_to_text(raw: str) -> str:
    raw = re.sub(r"(?is)<(script|style|noscript|svg).*?</\1>", " ", raw)
    raw = re.sub(r"(?s)<[^>]+>", " ", raw)
    return re.sub(r"\s+", " ", html.unescape(raw)).strip()


async def ingest_url(project: Project, url: str) -> int:
    async with httpx.AsyncClient(timeout=30, follow_redirects=False, headers={"User-Agent": "AI-Harness/1.0"}) as c:
        target = url
        for _ in range(5):
            assert_public_url(target)
            r = await c.get(target)
            if not r.is_redirect:
                break
            target = str(r.next_request.url) if r.next_request else r.headers["location"]
        else:
            raise ValueError("Redirecionamentos demais")
        r.raise_for_status()
        if len(r.content) > 5_000_000:
            raise ValueError("Conteúdo grande demais (>5MB)")
    ctype = r.headers.get("content-type", "")
    if not any(t in ctype for t in ("text", "json", "xml")):
        raise ValueError(f"Tipo de conteúdo não suportado: {ctype}")
    text = _html_to_text(r.text) if "html" in ctype else r.text
    text = text[:MAX_FILE]
    await db.rag_chunks.delete_many({"project_id": project.id, "source": url})
    forget(project.id)
    chunks = [Chunk(project_id=project.id, source=url, source_type="url", idx=i, text=t, suspicious=bool(SUSPICIOUS.search(t))) for i, t in enumerate(chunk_text(text))]
    await _store(chunks)
    forget(project.id)
    total = await db.rag_chunks.count_documents({"project_id": project.id})
    await db.projects.update_one({"_id": oid(project.id)}, {"$set": {"chunk_count": total}})
    return len(chunks)


def _bm25(query: str, docs: list[dict], k1=1.5, b=0.75) -> list[float]:
    q = [t.lower() for t in TOKEN.findall(query)]
    toks = [[t.lower() for t in TOKEN.findall(d["text"])] for d in docs]
    n = len(docs)
    avg = sum(len(t) for t in toks) / max(n, 1)
    df = Counter()
    for t in toks:
        df.update(set(t))
    scores = []
    for t in toks:
        tf = Counter(t)
        s = 0.0
        for term in q:
            if term not in tf:
                continue
            idf = math.log(1 + (n - df[term] + 0.5) / (df[term] + 0.5))
            s += idf * tf[term] * (k1 + 1) / (tf[term] + k1 * (1 - b + b * len(t) / max(avg, 1)))
        scores.append(s)
    return scores


class _Index:
    """A project's chunks ready to search: the texts (without their vectors), an inverted index for
    BM25 and the normalized embedding matrix. Built once and kept until the chunks change, instead of
    reading and parsing every chunk (vectors included) on every message."""

    def __init__(self, docs: list[dict], k1: float = 1.5, b: float = 0.75):
        self.k1, self.b = k1, b
        self.docs = [{k: v for k, v in d.items() if k != "embedding"} for d in docs]
        self.n = len(docs)
        toks = [[t.lower() for t in TOKEN.findall(d["text"])] for d in docs]
        self.lens = [len(t) for t in toks]
        self.avg = sum(self.lens) / max(self.n, 1)
        self.postings: dict[str, list[tuple[int, int]]] = {}
        for i, t in enumerate(toks):
            for term, tf in Counter(t).items():
                self.postings.setdefault(term, []).append((i, tf))
        vec = [(i, d["embedding"]) for i, d in enumerate(docs) if d.get("embedding")]
        self.vec_docs = [self.docs[i] for i, _ in vec]
        self.matrix = None
        if vec:
            m = np.array([v for _, v in vec], dtype=np.float32)
            self.matrix = m / (np.linalg.norm(m, axis=1, keepdims=True) + 1e-9)

    def bm25(self, query: str) -> list[float]:
        """The same scores as _bm25(query, docs), touching only the chunks that have a query term."""
        scores = [0.0] * self.n
        for term in (t.lower() for t in TOKEN.findall(query)):
            post = self.postings.get(term)
            if not post:
                continue
            df = len(post)
            idf = math.log(1 + (self.n - df + 0.5) / (df + 0.5))
            for i, tf in post:
                scores[i] += idf * tf * (self.k1 + 1) / (tf + self.k1 * (1 - self.b + self.b * self.lens[i] / max(self.avg, 1)))
        return scores


_INDEX: dict[str, tuple[tuple, _Index]] = {}
_MAX_INDEXES = 4


def _fingerprint() -> int:
    """Any write to the chunks (any project; this process is their only writer) bumps it."""
    return db.sync["rag_chunks"].version


def forget(project_id: str):
    """The chunks were rewritten (reindex, URL): the next search rebuilds the index."""
    _INDEX.pop(project_id, None)


async def _index(project_id: str) -> _Index:
    fp = _fingerprint()
    hit = _INDEX.get(project_id)
    if hit and hit[0] == fp:
        return hit[1]
    docs = await db.rag_chunks.find({"project_id": project_id}, {"text": 1, "source": 1, "source_type": 1, "idx": 1, "embedding": 1, "suspicious": 1}).to_list(20000)
    idx = await asyncio.to_thread(_Index, docs)
    _INDEX.pop(project_id, None)
    _INDEX[project_id] = (fp, idx)
    while len(_INDEX) > _MAX_INDEXES:
        _INDEX.pop(next(iter(_INDEX)))  # the least recently built
    return idx


async def retrieve(project_id: str, query: str, k: int = 6) -> list[dict]:
    index = await _index(project_id)
    if not index.n:
        return []
    method = "bm25"
    scores = None
    docs = index.docs
    settings = await get_settings()
    if settings.embedding.enabled and index.matrix is not None:
        try:
            qv = (await embed([query]))[0]
            qa = np.array(qv, dtype=np.float32)
            sims = index.matrix @ (qa / (np.linalg.norm(qa) + 1e-9))
            docs, scores, method = index.vec_docs, sims.tolist(), "embedding"
        except Exception as e:
            log.warning("query embedding failed: %s", e)
    if scores is None:
        scores = index.bm25(query)
    ranked = sorted(zip(scores, docs), key=lambda x: x[0], reverse=True)[:k]
    return [
        {"source": d["source"], "source_type": d.get("source_type", "file"), "idx": d.get("idx", 0), "text": d["text"], "score": round(float(s), 4), "suspicious": d.get("suspicious", False), "method": method}
        for s, d in ranked if s > 0
    ]
