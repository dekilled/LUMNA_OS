"""Backup and restore of the user's data: a .zip with every collection (rows exactly as stored),
chat attachments and, optionally, the projects' files.

Not included: the login (users), login lockouts and the audit log. API keys stay encrypted with this
install's SECRETS_KEY; restored on another install they come back empty and must be entered again.
"""
import asyncio
import json
import os
import shutil
import tempfile
import time
import zipfile
from datetime import datetime
from pathlib import Path

from fastapi import APIRouter, File, HTTPException, UploadFile

import workspace_runtime as wr
from auth import download_token
from core import DB_PATH, Provider, Settings, audit, db
from media import ATTACH_DIR

router = APIRouter(prefix="/api")

FORMAT = 1
SKIP = {"users", "login_attempts", "audit_logs"}
SAFETY_DIR = Path(DB_PATH).parent / "backups"
MAX_RESTORE = 4 << 30


def _collections() -> list[str]:
    return [c for c in db.sync.collection_names() if c not in SKIP]


def build(out: Path, include_files: bool) -> dict:
    """Writes the backup zip to `out` (sync: run in a thread)."""
    counts = {}
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as z:
        for name in _collections():
            rows = db.sync.dump_rows(name)
            counts[name] = len(rows)
            z.writestr(f"db/{name}.jsonl", "".join(json.dumps({"id": i, "doc": d}, ensure_ascii=False) + "\n" for i, d in rows))
        if ATTACH_DIR.is_dir():
            for f in sorted(ATTACH_DIR.iterdir()):
                if f.is_file() and not f.is_symlink():
                    z.write(f, f"attachments/{f.name}")
        folders = []
        if include_files:
            for _, raw in db.sync.dump_rows("projects"):
                folder = json.loads(raw).get("folder", "")
                try:
                    root = wr.project_root(folder)
                except HTTPException:
                    continue
                if not root.is_dir():
                    continue
                folders.append(folder)
                for f in sorted(root.rglob("*")):
                    rel = f.relative_to(root)
                    if f.is_symlink() or not f.is_file() or any(p in wr.IGNORED_DIRS for p in rel.parts):
                        continue
                    z.write(f, f"workspace/{folder}/{rel.as_posix()}")
        manifest = {"app": "LUMNA", "format": FORMAT, "created_at": datetime.now().isoformat(timespec="seconds"),
                    "collections": counts, "project_files": folders if include_files else None}
        z.writestr("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
    return manifest


def restore(src: Path) -> dict:
    """Restores a backup zip (sync: run in a thread). The current state is saved first."""
    try:
        z = zipfile.ZipFile(src)
    except zipfile.BadZipFile:
        raise HTTPException(400, "Arquivo inválido: não é um .zip")
    with z:
        try:
            manifest = json.loads(z.read("manifest.json"))
        except (KeyError, ValueError):
            raise HTTPException(400, "Este .zip não é um backup do LUMNA")
        if manifest.get("app") != "LUMNA" or manifest.get("format", 0) > FORMAT:
            raise HTTPException(400, "Backup de uma versão desconhecida do LUMNA")
        rows = {}
        for info in z.infolist():
            if info.filename.startswith("db/") and info.filename.endswith(".jsonl"):
                name = info.filename[3:-6]
                if name in SKIP or not name.replace("_", "").isalnum():
                    continue
                rows[name] = [(r["id"], r["doc"]) for r in (json.loads(line) for line in z.read(info).decode().splitlines() if line.strip())]
        if not rows:
            raise HTTPException(400, "O backup não tem dados")

        # Safety copy of what is about to be replaced (keeps the 3 most recent)
        SAFETY_DIR.mkdir(parents=True, exist_ok=True)
        build(SAFETY_DIR / f"antes-de-restaurar-{time.strftime('%Y%m%d-%H%M%S')}.zip", include_files=False)
        for old in sorted(SAFETY_DIR.glob("antes-de-restaurar-*.zip"))[:-3]:
            old.unlink(missing_ok=True)

        db.sync.replace_rows(rows)

        ATTACH_DIR.mkdir(parents=True, exist_ok=True)
        files = 0
        for info in z.infolist():
            name = info.filename
            if info.is_dir():
                continue
            if name.startswith("attachments/"):
                leaf = name[len("attachments/"):]
                if "/" in leaf or not leaf or leaf.startswith("."):
                    continue
                with z.open(info) as fsrc, open(ATTACH_DIR / leaf, "wb") as fdst:
                    shutil.copyfileobj(fsrc, fdst)
            elif name.startswith("workspace/"):
                folder, _, rel = name[len("workspace/"):].partition("/")
                if not rel:
                    continue
                try:
                    with z.open(info) as fsrc:
                        wr.write_bytes(folder, rel, fsrc.read())  # validates the folder name, blocks traversal
                except HTTPException:
                    continue
                files += 1
    # API keys encrypted by another install cannot be read here: they come back empty
    lost = sum(1 for _, raw in db.sync.dump_rows("providers") if json.loads(raw).get("api_key") and not Provider.from_mongo(json.loads(raw)).api_key)
    for _, raw in db.sync.dump_rows("settings"):
        s = json.loads(raw)
        cfg = Settings.from_mongo(s)
        lost += sum(1 for k in ("embedding", "image_gen", "voice") if (s.get(k) or {}).get("api_key") and not getattr(cfg, k).api_key)
    for _, raw in db.sync.dump_rows("projects"):
        try:
            wr.project_root(json.loads(raw)["folder"]).mkdir(exist_ok=True)
        except (HTTPException, KeyError):
            pass
    return {"collections": {k: len(v) for k, v in rows.items()}, "files": files, "keys_lost": lost, "created_at": manifest.get("created_at")}


@router.get("/backup/download-token")
async def backup_token(files: bool = False):
    return {"url": f"/api/dl/{download_token({'backup': bool(files)})}"}


async def backup_file(include_files: bool) -> tuple[Path, str]:
    fd, tmp = tempfile.mkstemp(suffix=".zip")
    os.close(fd)
    await asyncio.to_thread(build, Path(tmp), include_files)
    await audit("backup", "Backup exportado" + (" (com arquivos dos projetos)" if include_files else ""))
    return Path(tmp), f"lumna-backup-{time.strftime('%Y%m%d-%H%M')}.zip"


@router.post("/backup/restore")
async def backup_restore(file: UploadFile = File(...)):
    fd, tmp = tempfile.mkstemp(suffix=".zip")
    size = 0
    with open(fd, "wb") as out:
        while chunk := await file.read(1 << 20):
            size += len(chunk)
            if size > MAX_RESTORE:
                Path(tmp).unlink(missing_ok=True)
                raise HTTPException(413, "Backup grande demais")
            out.write(chunk)
    try:
        result = await asyncio.to_thread(restore, Path(tmp))
    finally:
        Path(tmp).unlink(missing_ok=True)
    await audit("backup", f"Backup restaurado ({result['created_at']})", **{k: v for k, v in result.items() if k != "collections"})
    return {"ok": True, **result}
