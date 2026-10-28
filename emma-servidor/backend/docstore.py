"""SQLite-backed document store implementing the small MongoDB/Motor subset this app uses.

Each collection is a table (id TEXT PRIMARY KEY, doc TEXT JSON). Supported:
- filters: equality (None also matches a missing field), $in, $nin, $ne, $exists, $gt, $gte, $lt, $lte
- updates: $set, $unset (with upsert)
- find(...).sort(...).to_list(n), projections, count_documents, create_index (unique too)
Simple equality filters are pushed down to SQL through json_extract; everything is re-checked in Python.
"""
import asyncio
import json
import re
import sqlite3
import threading
from contextlib import contextmanager
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable, Optional

from bson import ObjectId

_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_MISSING = object()


class DuplicateKeyError(Exception):
    pass


# ---------- JSON encoding (ObjectId / datetime round-trip) ----------
def _default(o):
    if isinstance(o, ObjectId):
        return {"$oid": str(o)}
    if isinstance(o, datetime):
        return {"$date": o.isoformat()}
    raise TypeError(f"Tipo não serializável: {type(o).__name__}")


def _hook(d: dict):
    if len(d) == 1:
        if "$oid" in d:
            return ObjectId(d["$oid"])
        if "$date" in d:
            return datetime.fromisoformat(d["$date"])
    return d


def _dumps(doc: dict) -> str:
    return json.dumps(doc, default=_default, ensure_ascii=False)


def _loads(raw: str) -> dict:
    return json.loads(raw, object_hook=_hook)


def _id_key(value) -> str:
    return str(value)


def _id_value(key: str):
    return ObjectId(key) if ObjectId.is_valid(key) else key


# ---------- matching / projection / sorting ----------
def _get(doc: dict, path: str):
    cur: Any = doc
    for part in path.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return _MISSING
        cur = cur[part]
    return cur


def _eq(value, expected) -> bool:
    if expected is None:
        return value is _MISSING or value is None
    if isinstance(value, list) and not isinstance(expected, list):
        return expected in value
    return value == expected


def _cmp(value, op, expected) -> bool:
    if value is _MISSING or value is None:
        return False
    try:
        return {"$gt": value > expected, "$gte": value >= expected, "$lt": value < expected, "$lte": value <= expected}[op]
    except TypeError:
        return False


def _match_value(value, cond) -> bool:
    if isinstance(cond, dict) and cond and all(k.startswith("$") for k in cond):
        for op, arg in cond.items():
            if op == "$in":
                ok = any(_eq(value, a) for a in arg)
            elif op == "$nin":
                ok = not any(_eq(value, a) for a in arg)
            elif op == "$ne":
                ok = not _eq(value, arg)
            elif op == "$exists":
                ok = (value is not _MISSING) == bool(arg)
            elif op in ("$gt", "$gte", "$lt", "$lte"):
                ok = _cmp(value, op, arg)
            else:
                raise NotImplementedError(f"Operador não suportado: {op}")
            if not ok:
                return False
        return True
    return _eq(value, cond)


def _matches(doc: dict, flt: Optional[dict]) -> bool:
    return all(_match_value(_get(doc, k), v) for k, v in (flt or {}).items())


def _project(doc: dict, projection: Optional[dict]) -> dict:
    if not projection:
        return doc
    fields = {k: v for k, v in projection.items() if k != "_id"}
    keep_id = projection.get("_id", 1)
    if any(fields.values()):
        out = {k: doc[k] for k in fields if fields[k] and k in doc}
        if keep_id and "_id" in doc:
            out["_id"] = doc["_id"]
        return out
    out = {k: v for k, v in doc.items() if k not in fields}
    if not keep_id:
        out.pop("_id", None)
    return out


def _sort_key(value):
    if value is _MISSING or value is None:
        return (0, 0)
    if isinstance(value, bool):
        return (1, int(value))
    if isinstance(value, (int, float)):
        return (1, value)
    if isinstance(value, str):
        return (2, value)
    if isinstance(value, ObjectId):
        return (3, str(value))
    if isinstance(value, datetime):
        return (4, value.isoformat())
    return (5, str(value))


def _normalize_sort(key, direction=1) -> list[tuple[str, int]]:
    return [(key, direction)] if isinstance(key, str) else list(key)


def _set_path(doc: dict, path: str, value):
    parts = path.split(".")
    cur = doc
    for part in parts[:-1]:
        cur = cur.setdefault(part, {})
    cur[parts[-1]] = value


def _unset_path(doc: dict, path: str):
    parts = path.split(".")
    cur = doc
    for part in parts[:-1]:
        cur = cur.get(part)
        if not isinstance(cur, dict):
            return
    cur.pop(parts[-1], None)


def _apply_update(doc: dict, update: dict) -> dict:
    for op, fields in update.items():
        if op == "$set":
            for k, v in fields.items():
                _set_path(doc, k, v)
        elif op == "$unset":
            for k in fields:
                _unset_path(doc, k)
        else:
            raise NotImplementedError(f"Update não suportado: {op}")
    return doc


# ---------- results ----------
class InsertOneResult:
    def __init__(self, inserted_id):
        self.inserted_id = inserted_id


class InsertManyResult:
    def __init__(self, inserted_ids):
        self.inserted_ids = inserted_ids


class UpdateResult:
    def __init__(self, matched: int, modified: int, upserted_id=None):
        self.matched_count, self.modified_count, self.upserted_id = matched, modified, upserted_id


class DeleteResult:
    def __init__(self, deleted: int):
        self.deleted_count = deleted


# ---------- sync core ----------
class Collection:
    def __init__(self, store: "DocStore", name: str):
        if not _NAME_RE.match(name):
            raise ValueError(f"Nome de coleção inválido: {name}")
        self.store, self.name, self.table = store, name, f"c_{name}"
        self.version = 0  # bumped on every write: caches built from this collection know when to rebuild
        store._execute(f'CREATE TABLE IF NOT EXISTS "{self.table}" (id TEXT PRIMARY KEY, doc TEXT NOT NULL)')

    # SQL pre-filter for plain equality / _id lookups; _matches() re-checks every row anyway.
    # exact: every condition went to SQL, so SQL may also sort and limit (see Cursor.to_list).
    def _where(self, flt: Optional[dict]) -> tuple[str, list, bool]:
        clauses, params, exact = [], [], True
        for k, v in (flt or {}).items():
            if k == "_id":
                if isinstance(v, dict) and set(v) == {"$in"}:
                    ids = [_id_key(i) for i in v["$in"]]
                    if not ids:
                        return "0", [], True
                    clauses.append(f"id IN ({','.join('?' * len(ids))})")
                    params += ids
                elif not isinstance(v, dict):
                    clauses.append("id = ?")
                    params.append(_id_key(v))
                else:
                    exact = False
            elif v is None and _NAME_RE.match(k):
                clauses.append(f"json_extract(doc, '$.{k}') IS NULL")  # null or missing, like _eq
            elif isinstance(v, (str, int, float)) and _NAME_RE.match(k):
                clauses.append(f"json_extract(doc, '$.{k}') = ?")
                params.append(int(v) if isinstance(v, bool) else v)
            else:
                exact = False
        return (" AND ".join(clauses) or "1"), params, exact

    def _rows(self, flt: Optional[dict], tail: str = "", tail_params: Iterable = ()) -> Iterable[dict]:
        where, params, _ = self._where(flt)
        for key, raw in self.store._query(f'SELECT id, doc FROM "{self.table}" WHERE {where}{tail}', [*params, *tail_params]):
            doc = _loads(raw)
            doc["_id"] = _id_value(key)
            if _matches(doc, flt):
                yield doc

    def find(self, filter: Optional[dict] = None, projection: Optional[dict] = None) -> "Cursor":
        return Cursor(self, filter, projection)

    def find_one(self, filter: Optional[dict] = None, projection: Optional[dict] = None) -> Optional[dict]:
        for doc in self._rows(filter):
            return _project(doc, projection)
        return None

    def count_documents(self, filter: Optional[dict] = None) -> int:
        return sum(1 for _ in self._rows(filter))

    def _write(self, key: str, doc: dict, insert: bool):
        body = _dumps({k: v for k, v in doc.items() if k != "_id"})
        self.version += 1
        try:
            if insert:
                self.store._execute(f'INSERT INTO "{self.table}" (id, doc) VALUES (?, ?)', [key, body])
            else:
                self.store._execute(f'UPDATE "{self.table}" SET doc = ? WHERE id = ?', [body, key])
        except sqlite3.IntegrityError as e:
            raise DuplicateKeyError(f"{self.name}: {e}") from e

    def insert_one(self, doc: dict) -> InsertOneResult:
        doc.setdefault("_id", ObjectId())
        self._write(_id_key(doc["_id"]), doc, insert=True)
        return InsertOneResult(doc["_id"])

    def insert_many(self, docs: list[dict]) -> InsertManyResult:
        with self.store._transaction():
            return InsertManyResult([self.insert_one(d).inserted_id for d in docs])

    def update_one(self, filter: dict, update: dict, upsert: bool = False) -> UpdateResult:
        with self.store._lock:
            doc = next(iter(self._rows(filter)), None)
            if doc is not None:
                self._write(_id_key(doc["_id"]), _apply_update(doc, update), insert=False)
                return UpdateResult(1, 1)
            if not upsert:
                return UpdateResult(0, 0)
            new = {k: v for k, v in filter.items() if not (isinstance(v, dict) and any(x.startswith("$") for x in v))}
            new = _apply_update(new, update)
            return UpdateResult(0, 0, self.insert_one(new).inserted_id)

    def replace_one(self, filter: dict, doc: dict, upsert: bool = False) -> UpdateResult:
        with self.store._lock:
            old = next(iter(self._rows(filter)), None)
            if old is None:
                return UpdateResult(0, 0, self.insert_one(dict(doc)).inserted_id if upsert else None)
            self._write(_id_key(old["_id"]), doc, insert=False)
            return UpdateResult(1, 1)

    def _delete(self, filter: Optional[dict], many: bool) -> DeleteResult:
        with self.store._lock:
            ids = [_id_key(d["_id"]) for d in self._rows(filter)]
            if not many:
                ids = ids[:1]
            if ids:
                self.version += 1
            for i in range(0, len(ids), 500):
                chunk = ids[i:i + 500]
                self.store._execute(f'DELETE FROM "{self.table}" WHERE id IN ({",".join("?" * len(chunk))})', chunk)
            return DeleteResult(len(ids))

    def delete_one(self, filter: dict) -> DeleteResult:
        return self._delete(filter, many=False)

    def delete_many(self, filter: Optional[dict] = None) -> DeleteResult:
        return self._delete(filter, many=True)

    def prune(self, field: str, keep: int, before: Optional[str] = None) -> int:
        """Keeps only the newest `keep` documents by `field` (and none older than `before`). For logs
        that would otherwise grow forever."""
        if not _NAME_RE.match(field):
            raise ValueError(f"Campo inválido: {field}")
        col = f"json_extract(doc, '$.{field}')"
        with self.store._lock:
            gone = 0
            if before is not None:
                gone += self.store._conn.execute(f'DELETE FROM "{self.table}" WHERE {col} < ?', [before]).rowcount
            gone += self.store._conn.execute(
                f'DELETE FROM "{self.table}" WHERE id NOT IN (SELECT id FROM "{self.table}" ORDER BY {col} DESC LIMIT ?)', [keep]).rowcount
            if gone:
                self.version += 1
            return gone

    def create_index(self, keys, unique: bool = False, **_) -> str:
        fields = [k for k, _ in _normalize_sort(keys)]
        if not all(_NAME_RE.match(f) for f in fields):
            raise ValueError(f"Índice inválido: {fields}")
        name = f"ix_{self.name}_{'_'.join(fields)}"
        cols = ", ".join(f"json_extract(doc, '$.{f}')" for f in fields)
        self.store._execute(f'CREATE {"UNIQUE " if unique else ""}INDEX IF NOT EXISTS "{name}" ON "{self.table}" ({cols})')
        return name


class Cursor:
    def __init__(self, coll: Collection, flt: Optional[dict], projection: Optional[dict]):
        self.coll, self.flt, self.projection, self._sort = coll, flt, projection, []

    def sort(self, key, direction: int = 1) -> "Cursor":
        self._sort = _normalize_sort(key, direction)
        return self

    def __iter__(self):
        return iter(self.to_list())

    def to_list(self, length: Optional[int] = None) -> list[dict]:
        _, _, exact = self.coll._where(self.flt)
        if exact and all(_NAME_RE.match(f) for f, _ in self._sort):
            # SQLite sorts and stops at the limit: only the rows asked for are read and parsed. Same
            # order as below (missing/null first, then numbers, then text; ties in insertion order).
            order = "".join(f"json_extract(doc, '$.{f}') {'DESC' if d < 0 else 'ASC'}, " for f, d in self._sort) + "rowid"
            tail = f" ORDER BY {order}" + (" LIMIT ?" if length is not None else "")
            docs = list(self.coll._rows(self.flt, tail, [length] if length is not None else []))
            return [_project(d, self.projection) for d in docs]
        docs = list(self.coll._rows(self.flt))
        for field, direction in reversed(self._sort):
            docs.sort(key=lambda d: _sort_key(_get(d, field)), reverse=direction < 0)
        if length is not None:
            docs = docs[:length]
        return [_project(d, self.projection) for d in docs]


class DocStore:
    def __init__(self, path: str):
        if path != ":memory:":
            Path(path).parent.mkdir(parents=True, exist_ok=True)
        self._conn = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute("PRAGMA synchronous=NORMAL")
        self._lock = threading.RLock()
        self._collections: dict[str, Collection] = {}

    def _execute(self, sql: str, params: Iterable = ()):
        with self._lock:
            self._conn.execute(sql, list(params))

    @contextmanager
    def _transaction(self):
        with self._lock:
            self._conn.execute("BEGIN")
            try:
                yield
            except BaseException:
                self._conn.execute("ROLLBACK")
                raise
            self._conn.execute("COMMIT")

    def _query(self, sql: str, params: Iterable = ()) -> list[tuple]:
        with self._lock:
            return self._conn.execute(sql, list(params)).fetchall()

    def __getitem__(self, name: str) -> Collection:
        with self._lock:
            if name not in self._collections:
                self._collections[name] = Collection(self, name)
            return self._collections[name]

    def __getattr__(self, name: str) -> Collection:
        if name.startswith("_"):
            raise AttributeError(name)
        return self[name]

    # ---- raw table access, for backups (rows are kept exactly as stored) ----
    def collection_names(self) -> list[str]:
        return sorted(r[0][2:] for r in self._query("SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'c\\_%' ESCAPE '\\'"))

    def dump_rows(self, name: str) -> list[tuple[str, str]]:
        return self._query(f'SELECT id, doc FROM "{self[name].table}"')

    def replace_rows(self, rows_by_collection: dict[str, list[tuple[str, str]]]):
        """Replaces whole collections atomically: either all of them are restored or none."""
        tables = {name: self[name].table for name in rows_by_collection}
        with self._transaction():
            for name, rows in rows_by_collection.items():
                self._conn.execute(f'DELETE FROM "{tables[name]}"')
                self._conn.executemany(f'INSERT INTO "{tables[name]}" (id, doc) VALUES (?, ?)', rows)
                self[name].version += 1

    def close(self):
        self._conn.close()


# ---------- async facade (Motor-like) ----------
class AsyncCursor:
    def __init__(self, cursor: Cursor):
        self._cursor = cursor

    def sort(self, key, direction: int = 1) -> "AsyncCursor":
        self._cursor.sort(key, direction)
        return self

    async def to_list(self, length: Optional[int] = None) -> list[dict]:
        return await asyncio.to_thread(self._cursor.to_list, length)


class AsyncCollection:
    def __init__(self, coll: Collection):
        self._coll = coll

    def find(self, filter: Optional[dict] = None, projection: Optional[dict] = None) -> AsyncCursor:
        return AsyncCursor(self._coll.find(filter, projection))

    def __getattr__(self, name: str):
        method = getattr(self._coll, name)

        async def call(*args, **kwargs):
            return await asyncio.to_thread(method, *args, **kwargs)

        return call


class AsyncDocStore:
    def __init__(self, path: str):
        self.sync = DocStore(path)

    def __getitem__(self, name: str) -> AsyncCollection:
        return AsyncCollection(self.sync[name])

    def __getattr__(self, name: str) -> AsyncCollection:
        if name.startswith("_"):
            raise AttributeError(name)
        return self[name]

    def close(self):
        self.sync.close()
