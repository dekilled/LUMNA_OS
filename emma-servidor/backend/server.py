import asyncio
import logging
import os
from pathlib import Path

from dotenv import load_dotenv

load_dotenv(Path(__file__).parent / ".env")

from fastapi import FastAPI, Request  # noqa: E402
from fastapi.responses import FileResponse, JSONResponse  # noqa: E402
from starlette.middleware.cors import CORSMiddleware  # noqa: E402

from auth import LOOPBACK_ORIGIN_RE, PUBLIC_PATHS, current_user, ensure_local_key, origin_ok, seed_admin  # noqa: E402

TOKEN_PATHS = ("/api/pv/", "/api/px/", "/api/dl/")
from auth import router as auth_router  # noqa: E402
from backup import router as backup_router  # noqa: E402
from core import Provider, Settings, db  # noqa: E402
from github import router as github_router  # noqa: E402
from media import router as media_router  # noqa: E402
from routes_chat import router as chat_router  # noqa: E402
from routes_config import router as config_router  # noqa: E402
from routes_projects import router as projects_router  # noqa: E402
from search import router as search_router  # noqa: E402
from diagnose import router as diagnose_router  # noqa: E402
from reminders import router as reminders_router  # noqa: E402
from commands import migrate as migrate_commands, router as commands_router  # noqa: E402
import yeelight  # noqa: E402
import app_update  # noqa: E402
import wake  # noqa: E402
import modes  # noqa: E402
import nsfw_profile  # noqa: E402
import local_voice  # noqa: E402
from seed import seed  # noqa: E402
from voice import router as voice_router  # noqa: E402
from workspace_runtime import ROOT, own_tree  # noqa: E402

logging.basicConfig(level=logging.INFO, format="%(asctime)s - %(name)s - %(levelname)s - %(message)s")

app = FastAPI(title="AI Harness", docs_url=None, redoc_url=None, openapi_url=None)


@app.get("/api/")
async def root():
    # boot: set per start by the Android app, which checks it is talking to the process it launched.
    return {"message": "LUMNA online", "boot": os.environ.get("HARNESS_BOOT_ID", "")}


app.include_router(auth_router)
app.include_router(config_router)
app.include_router(projects_router)
app.include_router(chat_router)
app.include_router(github_router)
app.include_router(media_router)
app.include_router(voice_router)
app.include_router(backup_router)
app.include_router(search_router)
app.include_router(diagnose_router)
app.include_router(reminders_router)
app.include_router(commands_router)
app.include_router(yeelight.router)
app.include_router(app_update.router)
app.include_router(wake.router)
app.include_router(modes.router)
app.include_router(nsfw_profile.router)
app.include_router(local_voice.router)

# Serves the built frontend (yarn build) from the backend itself, e.g. inside the Android app where
# there is no dev server. Registered after the API routers, so /api/* keeps precedence.
FRONTEND_DIST = Path(os.environ["FRONTEND_DIST"]).resolve() if os.environ.get("FRONTEND_DIST") else None
if FRONTEND_DIST and (FRONTEND_DIST / "index.html").is_file():

    @app.get("/{path:path}", include_in_schema=False)
    async def frontend(path: str):
        if path == "api" or path.startswith("api/"):
            return JSONResponse({"detail": "Not Found"}, status_code=404)
        target = (FRONTEND_DIST / path).resolve()
        if path and target.is_file() and FRONTEND_DIST in target.parents:
            # The service worker and manifest must be revalidated so app updates reach installed copies.
            fresh = path in ("sw.js", "manifest.json")
            return FileResponse(target, headers={"Cache-Control": "no-cache"} if fresh else None)
        return FileResponse(FRONTEND_DIST / "index.html", headers={"Cache-Control": "no-cache"})


@app.middleware("http")
async def auth_gate(request: Request, call_next):
    path = request.url.path
    if path.startswith("/api") and request.method != "OPTIONS" and not path.startswith(TOKEN_PATHS):
        if request.method not in ("GET", "HEAD") and not origin_ok(request):
            return JSONResponse({"detail": "Origem não permitida"}, status_code=403)
        if path not in PUBLIC_PATHS:
            user = await current_user(request)
            if not user:
                return JSONResponse({"detail": "Não autenticado"}, status_code=401)
            request.state.user = user
    return await call_next(request)


app.add_middleware(
    CORSMiddleware,
    allow_credentials=True,
    # "*" is ignored: with credentials, Starlette answers "*" to requests without cookies, which browsers reject.
    allow_origins=[o.strip() for o in os.environ["CORS_ORIGINS"].split(",") if o.strip() and o.strip() != "*"],
    allow_origin_regex=LOOPBACK_ORIGIN_RE,
    allow_methods=["*"],
    allow_headers=["*"],
)


async def encrypt_secrets_at_rest():
    for doc in await db.providers.find().to_list(200):
        await db.providers.replace_one({"_id": doc["_id"]}, Provider.from_mongo(doc).to_mongo())
    doc = await db.settings.find_one({"key": "global"})
    if doc:
        await db.settings.replace_one({"_id": doc["_id"]}, Settings.from_mongo(doc).to_mongo())


@app.on_event("startup")
async def startup():
    env_file = Path(__file__).parent / ".env"
    if env_file.exists():
        os.chmod(env_file, 0o600)
    await seed_admin()
    await ensure_local_key()
    await seed()
    await migrate_commands()
    app_update.on_start()
    await wake.on_start()
    app.state.light_scheduler = asyncio.create_task(yeelight.run_scheduler())
    await encrypt_secrets_at_rest()
    own_tree(ROOT)
    await nsfw_profile.migrate()  # everything before the SFW/NSFW profiles is NSFW (once)
    await modes.migrate()  # the old per-project lists of "modes" become modes of actions (once)
    import search
    asyncio.create_task(asyncio.to_thread(search.ensure_index))  # the search index (filled once, then by triggers)


@app.on_event("shutdown")
async def shutdown_db_client():
    from processes import stop_all
    wake.stop()
    await stop_all()
    db.close()
