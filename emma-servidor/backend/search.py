"""Search across conversations: message text and session titles, ignoring case and accents."""
import asyncio
import json
import logging
import re
import unicodedata
from typing import Optional

from fastapi import APIRouter, HTTPException, Query

from core import db
import nsfw_profile

router = APIRouter(prefix="/api")

LIMIT = 50
SNIPPET = 160  # characters of context shown around the first hit

# A full-text index of the messages (SQLite FTS5, by words, ignoring case and accents), kept in step
# with c_messages by triggers. Without it every search read every message. It only narrows the
# candidates (words that start with each term); each one is still checked exactly as before, and a
# search it finds nothing for (a piece from the middle of a word) still reads everything.
# Words, not trigrams: a trigram index would make the database about 2.5 times bigger; this one ~25%.
FTS = "fts_messages"
_fts_ok = False  # True once every message is in the index
_CONTENT = "coalesce(json_extract({}.doc, '$.content'), '')"
BATCH = 1000


def ensure_index() -> bool:
    """Creates the index and its triggers, then fills it with the messages already there, a batch
    at a time (the database stays usable meanwhile). False where SQLite can't (no FTS5 or too old):
    searches read everything, as before."""
    global _fts_ok
    if _fts_ok:
        return True
    store = db.sync
    store["messages"]  # the table exists
    try:
        with store._lock:
            c = store._conn
            c.execute(f"CREATE VIRTUAL TABLE IF NOT EXISTS {FTS} USING fts5(content, content='', contentless_delete=1, "
                      "tokenize='unicode61 remove_diacritics 2')")
            new, old = _CONTENT.format("new"), _CONTENT.format("old")
            c.execute(f'CREATE TRIGGER IF NOT EXISTS {FTS}_ai AFTER INSERT ON "c_messages" BEGIN '
                      f"INSERT INTO {FTS}(rowid, content) VALUES (new.rowid, {new}); END")
            c.execute(f'CREATE TRIGGER IF NOT EXISTS {FTS}_ad AFTER DELETE ON "c_messages" BEGIN '
                      f"DELETE FROM {FTS} WHERE rowid = old.rowid; END")
            c.execute(f'CREATE TRIGGER IF NOT EXISTS {FTS}_au AFTER UPDATE ON "c_messages" BEGIN '
                      f"DELETE FROM {FTS} WHERE rowid = old.rowid; INSERT INTO {FTS}(rowid, content) VALUES (new.rowid, {new}); END")
            top = c.execute('SELECT coalesce(max(rowid), 0) FROM "c_messages"').fetchone()[0]
        # Messages from before the index: in batches, skipping any a trigger already indexed meanwhile.
        content = _CONTENT.format('"c_messages"')
        for a in range(1, top + 1, BATCH):
            b = a + BATCH - 1
            with store._lock:
                store._conn.execute(
                    f'INSERT INTO {FTS}(rowid, content) SELECT rowid, {content} FROM "c_messages" WHERE rowid BETWEEN ? AND ? '
                    f"AND rowid NOT IN (SELECT rowid FROM {FTS} WHERE rowid BETWEEN ? AND ?)", [a, b, a, b])
        _fts_ok = True
    except Exception as e:
        logging.getLogger("search").warning("sem índice de busca (%s): a busca lê todas as mensagens", e)
        # Make sure no trigger is left pointing at a broken index, or saving a message would fail.
        with store._lock:
            for t in ("ai", "ad", "au"):
                try:
                    store._conn.execute(f"DROP TRIGGER IF EXISTS {FTS}_{t}")
                except Exception:
                    pass
    return _fts_ok


def _candidates(terms: list[str]) -> Optional[str]:
    """The FTS query for these terms, or None when the index can't answer (terms under 3 letters)."""
    if not _fts_ok:
        return None
    words = [w for t in terms for w in re.findall(r"\w+", t)]
    if not words:
        return None
    return " AND ".join('"' + w + '"*' for w in words)


def _fold_char(c: str) -> str:
    return "".join(x for x in unicodedata.normalize("NFKD", c) if not unicodedata.combining(x)).casefold()


_MARKS = re.compile(r"[\u0300-\u036f]")


def quick_fold(text: str) -> str:
    """Accent- and case-free text, for matching only."""
    return _MARKS.sub("", unicodedata.normalize("NFKD", text)).casefold()


def fold(text: str) -> tuple[str, list[int]]:
    """Accent- and case-free text, plus the original index of every folded character (for snippets)."""
    out, index = [], []
    for i, c in enumerate(text):
        f = _fold_char(c)
        out.append(f)
        index.extend([i] * len(f))
    return "".join(out), index


def _snippet(text: str, terms: list[str]) -> tuple[str, list[list[int]]]:
    """A window of the text around the first hit, with [start, end) of every hit inside it."""
    # Light markdown cleanup (bold markers, code ticks, heading hashes) so snippets read as text.
    plain = re.sub(r"(?m)^\s{0,3}#{1,6}\s+", "", re.sub(r"\*\*|`+", "", text))
    flat = re.sub(r"\s+", " ", plain).strip()
    folded, index = fold(flat)
    first = min((p for p in (folded.find(t) for t in terms) if p >= 0), default=0)
    center = index[first] if index else 0
    start = max(0, center - SNIPPET // 3)
    end = min(len(flat), start + SNIPPET)
    start = max(0, end - SNIPPET)
    if start > 0 and " " in flat[start:start + 20]:
        start = flat.index(" ", start) + 1
    window = flat[start:end]
    prefix = "…" if start > 0 else ""
    suffix = "…" if end < len(flat) else ""
    wf, wi = fold(window)
    marks = []
    for t in terms:
        for m in re.finditer(re.escape(t), wf):
            s, e = wi[m.start()], wi[m.end() - 1] + 1
            marks.append([s + len(prefix), e + len(prefix)])
    marks.sort()
    merged: list[list[int]] = []
    for s, e in marks:
        if merged and s <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([s, e])
    return prefix + window + suffix, merged


def _run(q: str, project_id: Optional[str], nsfw: bool = False) -> list[dict]:
    terms = quick_fold(q).split()
    if not terms:
        return []
    store = db.sync
    projects = {}
    for key, raw in store.dump_rows("projects"):
        projects[key] = json.loads(raw).get("name", "")
    sessions = {}
    for key, raw in store.dump_rows("sessions"):
        s = json.loads(raw)
        pid = s.get("project_id")
        if project_id is not None and (pid or "none") != project_id:
            continue
        if bool(s.get("nsfw")) != nsfw:
            continue  # the other profile's conversations (nsfw_profile.py)
        sessions[key] = {"title": s.get("title", ""), "project_id": pid, "project_name": projects.get(pid or "", ""), "updated_at": s.get("updated_at", "")}

    hits: list[dict] = []
    for sid, s in sessions.items():
        if all(t in quick_fold(s["title"]) for t in terms):
            text, marks = _snippet(s["title"], terms)
            hits.append({"kind": "session", "session_id": sid, "session_title": s["title"], "project_id": s["project_id"],
                         "project_name": s["project_name"], "message_id": None, "role": None, "snippet": text, "marks": marks,
                         "created_at": s["updated_at"]})

    match = _candidates(terms)
    found = _messages(store, f"WHERE rowid IN (SELECT rowid FROM {FTS} WHERE {FTS} MATCH ?)", [match], sessions, terms) if match else []
    if not found:
        found = _messages(store, "", [], sessions, terms)  # no index, or a piece from inside a word

    # The newest first; only the ones shown get a snippet.
    hits += [{"kind": "message", "session_id": sid, "message_id": mid, "role": role, "created_at": created or "", "_content": content, **sessions[sid]}
             for mid, sid, role, content, created in found]
    hits.sort(key=lambda h: h["created_at"], reverse=True)
    out = []
    for h in hits[:LIMIT]:
        if h["kind"] == "message":
            content = h.pop("_content")
            h["snippet"], h["marks"] = _snippet(content, terms)
            h["session_title"] = h.pop("title")
            h.pop("updated_at", None)
        out.append(h)
    return out


def _messages(store, where: str, params: list, sessions: dict, terms: list[str]) -> list[tuple]:
    """The newest LIMIT messages that really have every term (case and accents ignored), in the sessions
    being searched. Newest first, read in batches: it stops as soon as it has enough."""
    order = store._query(f'SELECT rowid FROM "c_messages" {where} ORDER BY json_extract(doc, \'$.created_at\') DESC, rowid', params)
    out = []
    for i in range(0, len(order), 200):
        ids = [r[0] for r in order[i:i + 200]]
        rows = store._query(
            "SELECT rowid, id, json_extract(doc, '$.session_id'), json_extract(doc, '$.role'), json_extract(doc, '$.content'),"
            f" json_extract(doc, '$.created_at') FROM \"c_messages\" WHERE rowid IN ({','.join('?' * len(ids))})", ids)
        by = {r[0]: r[1:] for r in rows}
        for rid in ids:
            mid, sid, role, content, created = by[rid]
            if sid in sessions and role in ("user", "assistant") and content and all(t in quick_fold(content) for t in terms):
                out.append((mid, sid, role, content, created))
                if len(out) == LIMIT:
                    return out
    return out


@router.get("/search")
async def search(q: str = Query("", max_length=200), project_id: Optional[str] = None):
    """Newest first. `project_id` narrows to one project ("none" = chats outside projects)."""
    if len(q.strip()) < 2:
        raise HTTPException(422, "Digite pelo menos 2 caracteres")
    return await asyncio.to_thread(_run, q, project_id, await nsfw_profile.nsfw())
