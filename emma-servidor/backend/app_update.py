"""The app's "Atualizar" button (Android, and the PC app in desktop/): newer builds of the LUMNA from GitHub Actions.

On the PC (LUMNA_DESKTOP set by desktop/main.js) the build is the AppImage (Linux) or the installer
(Windows) of desktop.yml, and the PC app installs it (desktop/main.js, "lumna:install-update").

The build records where it comes from (/opt/harness/SOURCE: "owner/repo@branch") and its number
(/opt/harness/BUILD: "<run number>-<sha>"; the APK's versionCode is the run number). With the GitHub
token saved in Config → GitHub, this finds the newest build of that branch — on the phone a GitHub
Release "apk-<run>" (android.yml; its body says "branch: …" and "sha: …"), else, as before, a
successful run whose artifact still exists — lists the commits since the installed one, downloads
the .apk (the release asset, or the artifact's zip) into the app's own storage — checking free space first — and leaves the .apk ready for the
Android side (AppUpdater.kt), which installs it with the system's PackageInstaller and tells the real
reason when Android refuses. The file is removed after the update (and on the next start).
"""
import asyncio
import logging
import os
import re
import shutil
import zipfile
from pathlib import Path
from typing import Optional

import httpx
from fastapi import APIRouter, HTTPException

from github import _token

router = APIRouter(prefix="/api")
log = logging.getLogger("app_update")

HARNESS_DIR = Path(os.environ.get("LUMNA_HARNESS_DIR") or Path(__file__).resolve().parent.parent)  # /opt/harness on the phone
UPDATE_DIR = Path(os.environ.get("LUMNA_UPDATE_DIR") or (Path.home() / ".lumna-update"))  # the app's files/runtime/home/.lumna-update
GH = os.environ.get("LUMNA_UPDATE_GH", "https://api.github.com")  # overridable for the tests only
DESKTOP = os.environ.get("LUMNA_DESKTOP", "")  # "linux" / "win32" in the PC app (desktop/main.js)
# Which build to look for: the phone's APK, or the PC app of this system.
if DESKTOP:
    WORKFLOW, ARTIFACT, EXT = "desktop.yml", ("lumna-desktop-win-" if DESKTOP == "win32" else "lumna-desktop-linux-"), (".exe" if DESKTOP == "win32" else ".AppImage")
else:
    WORKFLOW, ARTIFACT, EXT = "android.yml", "harness-apk-", ".apk"
_job: dict = {"state": "idle", "run": None, "progress": 0, "error": "", "file": None}


def current() -> dict:
    build = (HARNESS_DIR / "BUILD").read_text().strip() if (HARNESS_DIR / "BUILD").exists() else ""
    source = (HARNESS_DIR / "SOURCE").read_text().strip() if (HARNESS_DIR / "SOURCE").exists() else ""
    m = re.match(r"(\d+)-([0-9a-f]+)", build)
    repo, _, branch = source.partition("@")
    return {"run": int(m.group(1)) if m else None, "sha": m.group(2) if m else None, "build": build or "dev",
            "repo": repo if re.fullmatch(r"[\w.-]+/[\w.-]+", repo) else None, "branch": branch or None,
            "android": bool(os.environ.get("HARNESS_BOOT_ID")), "desktop": DESKTOP or None}


async def _gh(client: httpx.AsyncClient, path: str, token: str, **params):
    r = await client.get(GH + path, params=params, headers={"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json",
                                                              "X-GitHub-Api-Version": "2022-11-28"})
    if r.status_code in (401, 403, 404):
        raise HTTPException(400, "O token do GitHub não tem acesso a este repositório (precisa de \"Contents: read\" e \"Actions: read\", ou do escopo \"repo\").")
    if r.status_code >= 400:
        raise HTTPException(502, f"GitHub respondeu {r.status_code}")
    return r.json()


def free_mb() -> int:
    UPDATE_DIR.mkdir(parents=True, exist_ok=True)
    return shutil.disk_usage(UPDATE_DIR).free // (1024 * 1024)


def cleanup(keep: Optional[str] = None) -> None:
    """Old downloads go (the installed version no longer needs them)."""
    if UPDATE_DIR.is_dir():
        for f in UPDATE_DIR.iterdir():
            if f.name != keep:
                f.unlink(missing_ok=True)


@router.get("/app-update")
async def check():
    me = current()
    out = {"current": me, "available": False, "latest": None, "changes": [], "free_mb": None, "job": _job}
    if not me["android"] and not me["desktop"]:
        return {**out, "reason": "A atualização pelo app existe no LUMNA do celular e no app de PC."}
    if not me["repo"] or not me["branch"] or me["run"] is None:
        return {**out, "reason": "Esta versão não sabe de onde veio (instale uma versão nova uma vez pelo GitHub)."}
    token, _ = await _token()
    async with httpx.AsyncClient(timeout=20) as c:
        latest = None if DESKTOP else await _latest_release(c, me["repo"], me["branch"], token)
        runs = [] if latest else (await _gh(c, f"/repos/{me['repo']}/actions/workflows/{WORKFLOW}/runs", token, branch=me["branch"], status="success", per_page=10))["workflow_runs"]
        for run in runs:
            arts = [a for a in (await _gh(c, f"/repos/{me['repo']}/actions/runs/{run['id']}/artifacts", token))["artifacts"]
                    if a["name"].startswith(ARTIFACT) and not a["expired"]]
            if arts:
                latest = {"run": run["run_number"], "sha": run["head_sha"][:7], "artifact_id": arts[0]["id"], "size_mb": arts[0]["size_in_bytes"] // (1024 * 1024),
                          "created_at": run["created_at"]}
                break
        changes = []
        if latest and latest["run"] > me["run"] and me["sha"]:
            try:
                cmp = await _gh(c, f"/repos/{me['repo']}/compare/{me['sha']}...{latest['sha']}", token)
                changes = [re.sub(r"\s*\[skip ci\]", "", x["commit"]["message"].split("\n")[0]) for x in cmp.get("commits", [])][::-1][:40]
            except HTTPException:
                changes = []
    return {**out, "latest": latest, "available": bool(latest and latest["run"] > me["run"]), "changes": changes, "free_mb": free_mb()}


async def _latest_release(c: httpx.AsyncClient, repo: str, branch: str, token: str) -> Optional[dict]:
    """The newest APK release ("apk-<run>") built from this branch, with its .apk asset."""
    best = None
    for rel in await _gh(c, f"/repos/{repo}/releases", token, per_page=30):
        m = re.fullmatch(r"apk-(\d+)", rel.get("tag_name") or "")
        body = rel.get("body") or ""
        b = re.search(r"^branch:\s*(\S+)", body, re.M)
        sha = re.search(r"^sha:\s*([0-9a-f]{7,40})", body, re.M)
        asset = next((a for a in rel.get("assets") or [] if a["name"].endswith(EXT)), None)
        if not (m and b and b.group(1) == branch and asset):
            continue
        run = int(m.group(1))
        if not best or run > best["run"]:
            best = {"run": run, "sha": (sha.group(1) if sha else "")[:7], "asset_id": asset["id"],
                    "size_mb": asset["size"] // (1024 * 1024), "created_at": rel.get("created_at")}
    return best


async def _download(repo: str, artifact_id: Optional[int], run: int, size_mb: int, asset_id: Optional[int] = None) -> None:
    token, _ = await _token()
    zpath, apk = UPDATE_DIR / f"lumna-{run}.zip", UPDATE_DIR / f"lumna-{run}{EXT}"
    try:
        cleanup()
        # Authorization only on GitHub: the redirect goes to blob storage, which must not get the token.
        # A release asset is the .apk itself; an artifact is a zip with it.
        url = f"{GH}/repos/{repo}/releases/assets/{asset_id}" if asset_id else f"{GH}/repos/{repo}/actions/artifacts/{artifact_id}/zip"
        accept = "application/octet-stream" if asset_id else "application/vnd.github+json"
        async with httpx.AsyncClient(timeout=httpx.Timeout(60, read=120), follow_redirects=False) as c:
            r = await c.get(url, headers={"Authorization": f"Bearer {token}", "Accept": accept})
            if r.status_code != 302 or "location" not in r.headers:
                raise RuntimeError(f"GitHub não entregou o arquivo (código {r.status_code})")
            async with c.stream("GET", r.headers["location"]) as s:
                if s.status_code != 200:
                    raise RuntimeError(f"download falhou (código {s.status_code})")
                total = int(s.headers.get("content-length") or size_mb * 1024 * 1024) or 1
                got = 0
                with open(apk if asset_id else zpath, "wb") as f:
                    async for chunk in s.aiter_bytes(1 << 20):
                        f.write(chunk)
                        got += len(chunk)
                        _job["progress"] = min(99, got * 100 // total)
        if asset_id:
            _job.update(state="ready", progress=100, file=apk.name if DESKTOP else f".lumna-update/{apk.name}")
            log.info("atualização %s pronta (%d MB)", run, apk.stat().st_size // (1024 * 1024))
            return
        _job.update(state="extracting")

        def extract():
            with zipfile.ZipFile(zpath) as z:
                names = [n for n in z.namelist() if n.endswith(EXT) and "/" not in n.strip("/")]
                if not names:
                    raise RuntimeError(f"o arquivo baixado não tem um {EXT}")
                with z.open(names[0]) as src, open(apk, "wb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
        await asyncio.to_thread(extract)
        zpath.unlink(missing_ok=True)
        _job.update(state="ready", progress=100, file=apk.name if DESKTOP else f".lumna-update/{apk.name}")
        log.info("atualização %s pronta (%d MB)", run, apk.stat().st_size // (1024 * 1024))
    except Exception as e:  # noqa: BLE001 — shown to the user
        zpath.unlink(missing_ok=True)
        apk.unlink(missing_ok=True)
        _job.update(state="error", error=str(e)[:300] or e.__class__.__name__)
        log.warning("atualização %s falhou: %s", run, e)


@router.post("/app-update/download")
async def download():
    if _job["state"] in ("downloading", "extracting"):
        return _job
    info = await check()
    latest = info["latest"]
    if not info["available"] or not latest:
        raise HTTPException(400, info.get("reason") or "Você já está na versão mais nova.")
    need = latest["size_mb"] * (1 if latest.get("asset_id") else 2) + 250  # the zip, the .apk extracted from it, and room for Android to install it
    if info["free_mb"] is not None and info["free_mb"] < need:
        raise HTTPException(400, f"Pouco espaço livre no celular: {info['free_mb']} MB livres, a atualização precisa de uns {need} MB. "
                                 "Apague arquivos (APKs antigos em Downloads, por exemplo) e tente de novo.")
    _job.update(state="downloading", run=latest["run"], progress=0, error="", file=None)
    asyncio.create_task(_download(info["current"]["repo"], latest.get("artifact_id"), latest["run"], latest["size_mb"], latest.get("asset_id")))
    return _job


@router.get("/app-update/status")
async def status():
    return _job


@router.post("/app-update/clear")
async def clear():
    """After a failed install the user gives up: the file goes."""
    if _job["state"] not in ("downloading", "extracting"):
        cleanup()
        _job.update(state="idle", run=None, progress=0, error="", file=None)
    return _job


def on_start() -> None:
    """The backend started: a downloaded update that is already installed (or older) is removed."""
    me = current()
    if UPDATE_DIR.is_dir():
        for f in UPDATE_DIR.iterdir():
            m = re.match(r"lumna-(\d+)\.", f.name)
            if not m or me["run"] is None or int(m.group(1)) <= me["run"]:
                f.unlink(missing_ok=True)
