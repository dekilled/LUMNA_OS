import hashlib
import os
import re
import secrets
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Optional
from urllib.parse import urlparse

import bcrypt
import jwt
from fastapi import APIRouter, HTTPException, Request, Response
from pydantic import BaseModel

from core import audit, db

router = APIRouter(prefix="/api/auth")
ALG = "HS256"
ACCESS_MIN = 60
REFRESH_DAYS = 7
REMEMBER_DAYS = 180  # "Manter conectado"
MIN_PASSWORD = 8
MAX_FAILS = 5
LOCK_MIN = 15
PUBLIC_PATHS = {"/api/", "/api/auth/login", "/api/auth/refresh", "/api/auth/logout"}
# Offline use: frontend (localhost:3000) and backend (localhost:8001) differ only by port.
LOOPBACK_ORIGIN_RE = r"https?://(localhost|127\.0\.0\.1|\[::1\])(:\d+)?"
LOOPBACK_HOSTS = {"localhost", "127.0.0.1", "::1"}


def _secret() -> str:
    return os.environ["JWT_SECRET"]


def hash_password(pw: str) -> str:
    return bcrypt.hashpw(pw.encode(), bcrypt.gensalt()).decode()


def verify_password(pw: str, hashed: str) -> bool:
    return bool(hashed) and bcrypt.checkpw(pw.encode(), hashed.encode())


def _token(user_id: str, kind: str, delta: timedelta, pwv: int = 0) -> str:
    # pwv = the user's password version: changing the password invalidates every older token.
    return jwt.encode({"sub": user_id, "type": kind, "pwv": pwv, "exp": datetime.now(timezone.utc) + delta}, _secret(), algorithm=ALG)


def _current(payload: dict, user: Optional[dict]) -> Optional[dict]:
    return user if user and payload.get("pwv", 0) == user.get("pw_version", 0) else None


def _is_https(request: Request) -> bool:
    return (request.headers.get("x-forwarded-proto") or request.url.scheme).split(",")[0].strip() == "https"


def _set_cookie(request: Request, resp: Response, key: str, value: str, max_age: int):
    # Secure cookies are dropped by browsers on plain http (offline localhost); fall back to Lax there.
    https = _is_https(request)
    resp.set_cookie(key=key, value=value, httponly=True, secure=https, samesite="none" if https else "lax", max_age=max_age, path="/")


def preview_token(project_id: str) -> str:
    return jwt.encode({"sub": project_id, "type": "preview", "exp": datetime.now(timezone.utc) + timedelta(hours=12)}, _secret(), algorithm=ALG)


def verify_preview(token: str) -> Optional[str]:
    try:
        p = jwt.decode(token, _secret(), algorithms=[ALG])
        return p["sub"] if p.get("type") == "preview" else None
    except jwt.InvalidTokenError:
        return None


def download_token(target: dict) -> str:
    """Short-lived link for one download (project file/folder or attachment): opened by the Android
    downloader, which has no access to the page's login token."""
    return jwt.encode({**target, "type": "download", "exp": datetime.now(timezone.utc) + timedelta(minutes=5)}, _secret(), algorithm=ALG)


def verify_download(token: str) -> Optional[dict]:
    try:
        p = jwt.decode(token, _secret(), algorithms=[ALG])
        return p if p.get("type") == "download" else None
    except jwt.InvalidTokenError:
        return None


def public_user(u: dict) -> dict:
    return {"user_id": u["user_id"], "email": u["email"], "name": u.get("name", ""), "role": u.get("role", "admin"), "picture": u.get("picture", "")}


API_TOKEN_PREFIX = "lumna_"
TOUCH_EVERY_S = 60  # "last used" is written at most once a minute per token


def hash_api_token(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


async def _api_token_user(token: str) -> Optional[dict]:
    """A long-lived API token (made in Configurações → Conta, for the LUMNA OS and other apps): only its hash is stored."""
    doc = await db.api_tokens.find_one({"hash": hash_api_token(token)})
    if not doc:
        return None
    user = await db.users.find_one({"user_id": doc["user_id"]}, {"_id": 0, "password_hash": 0})
    if not user:
        return None
    now = datetime.now(timezone.utc)
    last = doc.get("last_used_at")
    if not last or (now - (last.replace(tzinfo=timezone.utc) if last.tzinfo is None else last)).total_seconds() > TOUCH_EVERY_S:
        await db.api_tokens.update_one({"_id": doc["_id"]}, {"$set": {"last_used_at": now}})
    return {**user, "auth_kind": "token", "api_token_id": doc["token_id"]}


async def current_user(request: Request) -> Optional[dict]:
    token = request.cookies.get("access_token")
    auth = request.headers.get("Authorization", "")
    if not token and auth.startswith("Bearer "):
        token = auth[7:]
    if token and token.startswith(API_TOKEN_PREFIX):
        return await _api_token_user(token)
    if token:
        try:
            payload = jwt.decode(token, _secret(), algorithms=[ALG])
            if payload.get("type") == "access":
                return _current(payload, await db.users.find_one({"user_id": payload["sub"]}, {"_id": 0, "password_hash": 0}))
        except jwt.InvalidTokenError:
            pass
    return None


def origin_ok(request: Request) -> bool:
    origin = request.headers.get("origin")
    if not origin:
        return True
    if origin == "null":
        return False
    allowed = {o.strip() for o in os.environ.get("CORS_ORIGINS", "").split(",") if o.strip() and o.strip() != "*"}
    hosts = {request.headers.get("host", ""), request.headers.get("x-forwarded-host", "")} - {""}
    if origin in allowed or urlparse(origin).netloc in hosts:
        return True
    request_host = urlparse(f"//{request.headers.get('host', '')}").hostname
    return bool(re.fullmatch(LOOPBACK_ORIGIN_RE, origin)) and request_host in LOOPBACK_HOSTS


class LoginIn(BaseModel):
    email: str
    password: str
    remember: bool = True


def _ip(request: Request) -> str:
    return (request.headers.get("x-forwarded-for") or (request.client.host if request.client else "")).split(",")[0].strip()


@router.post("/login")
async def login(body: LoginIn, request: Request, response: Response):
    email = body.email.strip().lower()
    ident = f"{_ip(request)}:{email}"
    att = await db.login_attempts.find_one({"identifier": ident})
    now = datetime.now(timezone.utc)
    if att and att.get("locked_until"):
        lu = att["locked_until"]
        lu = lu.replace(tzinfo=timezone.utc) if lu.tzinfo is None else lu
        if lu > now:
            raise HTTPException(429, f"Muitas tentativas. Tente novamente em {int((lu - now).total_seconds() // 60) + 1} min.")
    user = await db.users.find_one({"email": email})
    if not user or not verify_password(body.password, user.get("password_hash", "")):
        fails = (att or {}).get("count", 0) + 1
        upd = {"count": fails, "updated_at": now}
        if fails >= MAX_FAILS:
            upd = {"count": 0, "locked_until": now + timedelta(minutes=LOCK_MIN), "updated_at": now}
        await db.login_attempts.update_one({"identifier": ident}, {"$set": upd}, upsert=True)
        await audit("auth", f"Login falhou: {email}", ip=_ip(request))
        raise HTTPException(401, "Email ou senha inválidos")
    await db.login_attempts.delete_many({"identifier": ident})
    await audit("auth", f"Login: {email}", ip=_ip(request))
    return {**public_user(user), **_issue(request, response, user, body.remember)}


def _issue(request: Request, response: Response, user: dict, remember: bool) -> dict:
    pwv = user.get("pw_version", 0)
    days = REMEMBER_DAYS if remember else REFRESH_DAYS
    access = _token(user["user_id"], "access", timedelta(minutes=ACCESS_MIN), pwv)
    refresh_tok = _token(user["user_id"], "refresh", timedelta(days=days), pwv)
    _set_cookie(request, response, "access_token", access, ACCESS_MIN * 60)
    _set_cookie(request, response, "refresh_token", refresh_tok, days * 86400)
    return {"access_token": access, "refresh_token": refresh_tok}


class RefreshIn(BaseModel):
    refresh_token: Optional[str] = None


@router.post("/refresh")
async def refresh(body: RefreshIn, request: Request, response: Response):
    token = body.refresh_token or request.cookies.get("refresh_token")
    if not token:
        raise HTTPException(401, "Sem refresh token")
    try:
        payload = jwt.decode(token, _secret(), algorithms=[ALG])
    except jwt.InvalidTokenError:
        raise HTTPException(401, "Refresh token inválido")
    user = await db.users.find_one({"user_id": payload["sub"]}) if payload.get("type") == "refresh" else None
    if not _current(payload, user):
        raise HTTPException(401, "Sessão encerrada: entre de novo")
    access = _token(payload["sub"], "access", timedelta(minutes=ACCESS_MIN), user.get("pw_version", 0))
    _set_cookie(request, response, "access_token", access, ACCESS_MIN * 60)
    return {"ok": True, "access_token": access}


@router.post("/logout")
async def logout(request: Request, response: Response):
    https = _is_https(request)
    for k in ("access_token", "refresh_token"):
        response.delete_cookie(k, path="/", secure=https, samesite="none" if https else "lax")
    return {"ok": True}


@router.get("/me")
async def me(request: Request):
    return public_user(request.state.user)


class PasswordIn(BaseModel):
    current: str
    new: str
    remember: bool = True


@router.post("/change-password")
async def change_password(body: PasswordIn, request: Request, response: Response):
    user = await db.users.find_one({"user_id": request.state.user["user_id"]})
    if not verify_password(body.current, user.get("password_hash", "")):
        await audit("auth", "Troca de senha recusada: senha atual errada", ip=_ip(request))
        raise HTTPException(400, "Senha atual incorreta")
    if len(body.new) < MIN_PASSWORD:
        raise HTTPException(400, f"A nova senha precisa de pelo menos {MIN_PASSWORD} caracteres")
    if body.new == body.current:
        raise HTTPException(400, "A nova senha é igual à atual")
    pwv = user.get("pw_version", 0) + 1
    # password_changed: the startup seed no longer resets it to ADMIN_PASSWORD.
    await db.users.update_one({"user_id": user["user_id"]}, {"$set": {"password_hash": hash_password(body.new), "pw_version": pwv, "password_changed": True}})
    await audit("auth", "Senha alterada (outras sessões encerradas)", ip=_ip(request))
    return {"ok": True, **_issue(request, response, {**user, "pw_version": pwv}, body.remember)}


class TokenIn(BaseModel):
    name: str


def _need_login(request: Request):
    """Making or revoking tokens takes a real login (a token can't mint more tokens)."""
    if request.state.user.get("auth_kind") == "token":
        raise HTTPException(403, "Tokens de API não criam nem revogam tokens: entre com a sua conta")


def _token_out(d: dict) -> dict:
    created, used = d.get("created_at"), d.get("last_used_at")
    return {"id": d["token_id"], "name": d["name"], "preview": d["preview"],
            "created_at": created.isoformat() if created else None, "last_used_at": used.isoformat() if used else None}


@router.get("/tokens")
async def list_tokens(request: Request):
    _need_login(request)
    docs = await db.api_tokens.find({"user_id": request.state.user["user_id"]}).sort("created_at", -1).to_list(200)
    return [_token_out(d) for d in docs]


@router.post("/tokens")
async def create_token(body: TokenIn, request: Request):
    _need_login(request)
    name = body.name.strip()[:60]
    if not name:
        raise HTTPException(400, "Dê um nome ao token (por exemplo: LUMNA OS)")
    count = await db.api_tokens.count_documents({"user_id": request.state.user["user_id"]})
    if count >= 30:
        raise HTTPException(400, "Muitos tokens: revogue algum que não usa mais")
    token = API_TOKEN_PREFIX + secrets.token_urlsafe(32)
    doc = {"token_id": f"tok_{uuid.uuid4().hex[:12]}", "user_id": request.state.user["user_id"], "name": name, "hash": hash_api_token(token),
           "preview": f"{token[:10]}…{token[-4:]}", "created_at": datetime.now(timezone.utc), "last_used_at": None}
    await db.api_tokens.insert_one(doc)
    await audit("auth", f"Token de API criado: {name}", ip=_ip(request))
    return {**_token_out(doc), "token": token}  # the only time the token itself is shown


@router.delete("/tokens/{token_id}")
async def revoke_token(token_id: str, request: Request):
    _need_login(request)
    doc = await db.api_tokens.find_one({"token_id": token_id, "user_id": request.state.user["user_id"]})
    if not doc:
        raise HTTPException(404, "Token não encontrado")
    await db.api_tokens.delete_one({"_id": doc["_id"]})
    await audit("auth", f"Token de API revogado: {doc['name']}", ip=_ip(request))
    return {"ok": True}


LOCAL_KEY_NAME = "LUMNA OS (automático)"


def local_key_path() -> Optional[Path]:
    """Where this server leaves the key for the LUMNA OS on the same computer (LUMNA_KEY_FILE=off turns it off)."""
    env = os.environ.get("LUMNA_KEY_FILE", "")
    if env.lower() == "off":
        return None
    return Path(env) if env else Path.home() / ".config" / "lumna" / "emma" / "chave"


async def ensure_local_key():
    """The LUMNA OS on this computer talks to the API without anyone typing a login: the server makes an API token for it
    and writes it to a file only this user can read. Kept across restarts while it still works; made again if it doesn't."""
    path = local_key_path()
    admin = await db.users.find_one({"email": os.environ["ADMIN_EMAIL"].strip().lower()})
    if not path or not admin:
        return
    try:
        have = path.read_text().strip() if path.exists() else ""
        if have.startswith(API_TOKEN_PREFIX) and await db.api_tokens.find_one({"hash": hash_api_token(have), "user_id": admin["user_id"]}):
            return
        await db.api_tokens.delete_many({"user_id": admin["user_id"], "name": LOCAL_KEY_NAME})
        token = API_TOKEN_PREFIX + secrets.token_urlsafe(32)
        await db.api_tokens.insert_one({"token_id": f"tok_{uuid.uuid4().hex[:12]}", "user_id": admin["user_id"], "name": LOCAL_KEY_NAME, "hash": hash_api_token(token),
                                        "preview": f"{token[:10]}…{token[-4:]}", "created_at": datetime.now(timezone.utc), "last_used_at": None})
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        tmp = path.with_name(path.name + ".tmp")
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(token + "\n")
        os.replace(tmp, path)
        await audit("auth", f"Chave local do LUMNA OS criada em {path}")
    except OSError as e:  # a read-only home etc.: the key is a convenience, never a reason not to start
        await audit("auth", f"Não consegui guardar a chave local do LUMNA OS: {e}")


async def seed_admin():
    email = os.environ["ADMIN_EMAIL"].strip().lower()
    pw = os.environ["ADMIN_PASSWORD"]
    existing = await db.users.find_one({"email": email})
    if not existing:
        await db.users.insert_one({"user_id": f"user_{uuid.uuid4().hex[:12]}", "email": email, "name": "Admin", "role": "admin",
                                   "password_hash": hash_password(pw), "created_at": datetime.now(timezone.utc)})
    elif os.environ.get("ADMIN_PASSWORD_RESET") == "1":
        # "Esqueci a senha" (Android app, confirmed with the phone's screen lock): back to the
        # newly generated password; every session is signed out and login lockouts are cleared.
        await db.users.update_one({"email": email}, {"$set": {"password_hash": hash_password(pw), "password_changed": False,
                                                              "pw_version": existing.get("pw_version", 0) + 1}})
        await db.login_attempts.delete_many({})
        await audit("auth", "Senha redefinida (Esqueci a senha)")
    elif not existing.get("password_changed") and not verify_password(pw, existing.get("password_hash", "")):
        await db.users.update_one({"email": email}, {"$set": {"password_hash": hash_password(pw)}})
    await db.users.create_index("email", unique=True)
    await db.users.create_index("user_id", unique=True)
    await db.login_attempts.create_index("identifier")
    await db.api_tokens.create_index("hash", unique=True)


def new_secret() -> str:
    return hashlib.sha256(secrets.token_bytes(32)).hexdigest()
