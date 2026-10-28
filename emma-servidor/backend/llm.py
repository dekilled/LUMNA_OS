from typing import AsyncIterator, Optional

from fastapi import HTTPException
from openai import APIConnectionError, APIStatusError, APITimeoutError, AsyncOpenAI

from core import Provider, db, get_settings, oid
from netguard import assert_public_url

MODE_TASK = {"chat": "chat", "plan": "planner", "build": "coder", "workspace": "coder"}

PRESETS = [
    {"preset": "ollama", "name": "Ollama Cloud", "base_url": "https://ollama.com/v1", "models": ["gemma4:31b-cloud", "gpt-oss:120b", "kimi-k2.6", "deepseek-v4.1-flash"]},
    {"preset": "openai", "name": "OpenAI", "base_url": "https://api.openai.com/v1", "models": ["gpt-5.4-mini", "gpt-5.5"]},
    {"preset": "gemini", "name": "Gemini", "base_url": "https://generativelanguage.googleapis.com/v1beta/openai/", "models": ["gemini-3-flash", "gemini-3.1-pro"]},
    {"preset": "claude", "name": "Claude", "base_url": "https://api.anthropic.com/v1/", "models": ["claude-sonnet-4-6", "claude-haiku-4-5"]},
    {"preset": "custom", "name": "Endpoint customizado", "base_url": "http://localhost:11434/v1", "models": []},
]


def client_for(p: Provider) -> AsyncOpenAI:
    assert_public_url(p.base_url)
    return AsyncOpenAI(base_url=p.base_url, api_key=p.api_key or "none", timeout=180, max_retries=2)


def why_failed(e: Exception) -> str:
    """What a failed request with native tools means:
    - "unsupported": the endpoint/model doesn't take tools (use the text protocol from now on);
    - "bad_call": the server couldn't read the call the model wrote (Ollama "error parsing tool call"):
      the text protocol for this answer only;
    - "transient": network, timeout, busy or failing server: try again, change nothing;
    - "other": anything else (shown as an error)."""
    text = str(e).lower()
    status = getattr(e, "status_code", None)
    if "parsing tool call" in text or "parse tool call" in text:
        return "bad_call"
    if isinstance(e, (APIConnectionError, APITimeoutError)) or status in (408, 409, 425, 429, 500, 502, 503, 504):
        return "transient"
    if isinstance(e, APIStatusError) and status in (400, 404, 422, 501) and any(k in text for k in ("tool", "function")):
        return "unsupported"
    return "other"


async def resolve(task: str) -> tuple[Provider, str]:
    s = await get_settings()
    target = s.routing.get(task) or s.routing.get("chat")
    if not target or not target.provider_id:
        raise HTTPException(400, f"Nenhum provider configurado para a tarefa '{task}'")
    p = Provider.from_mongo(await db.providers.find_one({"_id": oid(target.provider_id)}))
    if not p:
        raise HTTPException(400, f"Provider da tarefa '{task}' não existe mais")
    return p, target.model or (p.models[0] if p.models else "")


async def stream_chat(p: Provider, model: str, messages: list[dict], max_tokens: Optional[int] = None) -> AsyncIterator[str]:
    c = client_for(p)
    got = False
    try:
        stream = await c.chat.completions.create(model=model, messages=messages, stream=True, **({"max_tokens": max_tokens} if max_tokens else {}))
        async for ev in stream:
            if not ev.choices:
                continue
            delta = ev.choices[0].delta
            if delta and delta.content:
                got = True
                yield delta.content
        if got:
            return
    except Exception:
        if got:
            raise
    # Fallback for partially compatible endpoints without streaming
    resp = await c.chat.completions.create(model=model, messages=messages, **({"max_tokens": max_tokens} if max_tokens else {}))
    yield resp.choices[0].message.content or ""


def _calls(slots: dict) -> list[dict]:
    return [{"id": s["id"], "name": s["name"], "arguments": s["args"]} for _, s in sorted(slots.items()) if s["name"]]


async def stream_turn(p: Provider, model: str, messages: list[dict], tools: list[dict], max_tokens: Optional[int] = None) -> AsyncIterator[tuple[str, object]]:
    """One model turn with native tools: yields ("text", piece) while it writes and, at the end,
    ("calls", [{id, name, arguments}]) if it asked for tools. Errors before any output propagate,
    so the caller can fall back to the text protocol (endpoints/models without tool support)."""
    c = client_for(p)
    stream = await c.chat.completions.create(model=model, messages=messages, tools=tools, stream=True, **({"max_tokens": max_tokens} if max_tokens else {}))
    slots: dict = {}
    async for ev in stream:
        if not ev.choices:
            continue
        delta = ev.choices[0].delta
        if not delta:
            continue
        if delta.content:
            yield "text", delta.content
        for tc in delta.tool_calls or []:
            # Some servers (Ollama) send whole calls without an index; others stream the arguments.
            key = tc.index if tc.index is not None else len(slots)
            s = slots.setdefault(key, {"id": "", "name": "", "args": ""})
            if tc.id:
                s["id"] = tc.id
            if tc.function:
                if tc.function.name and not s["name"]:
                    s["name"] = tc.function.name
                if tc.function.arguments:
                    s["args"] += tc.function.arguments
    if slots:
        yield "calls", _calls(slots)


async def complete(p: Provider, model: str, messages: list[dict]) -> str:
    resp = await client_for(p).chat.completions.create(model=model, messages=messages)
    return resp.choices[0].message.content or ""


async def embed(texts: list[str], cfg=None) -> Optional[list[list[float]]]:
    cfg = cfg or (await get_settings()).embedding
    if not cfg.enabled or not cfg.api_key:
        return None
    assert_public_url(cfg.base_url)
    c = AsyncOpenAI(base_url=cfg.base_url, api_key=cfg.api_key, timeout=120, max_retries=2)
    out: list[list[float]] = []
    for i in range(0, len(texts), 32):
        resp = await c.embeddings.create(model=cfg.model, input=texts[i:i + 32])
        out.extend(d.embedding for d in resp.data)
    return out
