"""Yeelight lamps (Modo Full, G8): find them on the Wi-Fi, turn them on/off, on a schedule.

Yeelight's LAN protocol ("Controle LAN" must be on in the Yeelight app, per lamp):
- discovery: an SSDP-like M-SEARCH to 239.255.255.250:1982; each lamp answers (unicast) with its
  address (yeelight://ip:55443), id, model, name and state;
- control: one JSON command per line over TCP to ip:55443, e.g. {"id":1,"method":"set_power",
  "params":["on","smooth",400]}; the answer is {"id":1,"result":["ok"]} or {"id":1,"error":{...}}.

Lamps found are kept in the `devices` collection (with a friendly name, "luz do quarto"); a lamp
whose address changed (DHCP) is found again on the next failure. Schedules ("acende às 18h") are
kept in `light_jobs` and run by the backend (run_scheduler): the lamp itself can only switch off
after N minutes, not on at a time. So the LUMNA must be running at that time (on the phone:
"Manter a LUMNA ativa").

Only private (LAN) addresses are ever contacted. YEELIGHT_DISCOVERY_ADDR=host:port and
YEELIGHT_ALLOW_LOOPBACK=1 exist for the tests (a fake lamp on 127.0.0.1).
"""
import asyncio
import ipaddress
import json
import logging
import os
import re
import socket
import unicodedata
from datetime import datetime, timedelta, timezone
from typing import Optional

from bson import ObjectId
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from core import db, now_iso, oid
from reminders import CLIENT_OFFSET, local_label
from tools import REGISTRY, Outcome, Tool

router = APIRouter(prefix="/api")
log = logging.getLogger("yeelight")

PORT = 55443
DISCOVERY = os.environ.get("YEELIGHT_DISCOVERY_ADDR", "239.255.255.250:1982")
TIMEOUT = 3.0
MSEARCH = b'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1982\r\nMAN: "ssdp:discover"\r\nST: wifi_bulb\r\n\r\n'


class LampError(Exception):
    pass


def _lan_ip(ip: str) -> bool:
    try:
        a = ipaddress.ip_address(ip)
    except ValueError:
        return False
    if a.is_loopback:
        return os.environ.get("YEELIGHT_ALLOW_LOOPBACK") == "1"
    return a.is_private and not a.is_link_local and not a.is_multicast


def _fold(s: str) -> str:
    return re.sub(r"\s+", " ", unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().lower()).strip()


# ---------- protocol ----------
def _parse_answer(data: bytes) -> Optional[dict]:
    headers = {}
    for line in data.decode(errors="replace").split("\r\n")[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    m = re.match(r"yeelight://([\d.]+):(\d+)", headers.get("location", ""))
    if not m or not headers.get("id") or not _lan_ip(m.group(1)):
        return None
    return {"bulb_id": headers["id"], "ip": m.group(1), "port": int(m.group(2)), "model": headers.get("model", ""),
            "lamp_name": headers.get("name", ""), "power": headers.get("power", ""), "bright": headers.get("bright", ""),
            "support": headers.get("support", "").split()}


async def discover(seconds: float = 2.5) -> list[dict]:
    """The lamps answering on this network (Controle LAN on)."""
    host, port = DISCOVERY.rsplit(":", 1)
    loop = asyncio.get_running_loop()
    found: dict[str, dict] = {}

    class Listener(asyncio.DatagramProtocol):
        def datagram_received(self, data, addr):
            lamp = _parse_answer(data)
            if lamp:
                found[lamp["bulb_id"]] = lamp

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    sock.bind(("", 0))
    transport, _ = await loop.create_datagram_endpoint(Listener, sock=sock)
    try:
        for _ in range(3):  # UDP gets lost: ask a few times
            transport.sendto(MSEARCH, (host, int(port)))
            await asyncio.sleep(seconds / 3)
    finally:
        transport.close()
    return list(found.values())


async def command(ip: str, port: int, method: str, params: list) -> list:
    """One command to a lamp; its result list, or LampError."""
    if not _lan_ip(ip):
        raise LampError("endereço fora da rede local")
    try:
        reader, writer = await asyncio.wait_for(asyncio.open_connection(ip, port), TIMEOUT)
    except (OSError, asyncio.TimeoutError):
        raise LampError("a lâmpada não respondeu (está ligada na tomada? \"Controle LAN\" ativo no app Yeelight?)")
    try:
        writer.write((json.dumps({"id": 1, "method": method, "params": params}) + "\r\n").encode())
        await writer.drain()
        deadline = asyncio.get_running_loop().time() + TIMEOUT
        while True:  # the lamp also sends {"method":"props",…} notifications: skip them
            left = deadline - asyncio.get_running_loop().time()
            if left <= 0:
                raise LampError("a lâmpada não respondeu a tempo")
            line = await asyncio.wait_for(reader.readline(), left)
            if not line:
                raise LampError("a lâmpada fechou a conexão")
            msg = json.loads(line)
            if msg.get("id") == 1:
                if "error" in msg:
                    raise LampError(str(msg["error"].get("message") or msg["error"]))
                return msg.get("result") or []
    except (OSError, asyncio.TimeoutError, json.JSONDecodeError) as e:
        raise LampError(f"falha na conversa com a lâmpada ({e.__class__.__name__})")
    finally:
        writer.close()


# ---------- the lamps we know ----------
def _label(d: dict) -> str:
    return d.get("name") or d.get("lamp_name") or f"Yeelight {d.get('model') or ''} ({d['ip']})".replace("  ", " ")


def _out(d: dict) -> dict:
    return {"id": str(d["_id"]), "name": _label(d), "custom_name": d.get("name", ""), "ip": d["ip"], "port": d.get("port", PORT),
            "model": d.get("model", ""), "bulb_id": d.get("bulb_id", ""), "support": d.get("support", [])}


async def lamps() -> list[dict]:
    return await db.devices.find({"kind": "yeelight"}).to_list(200)


async def remember(found: list[dict]) -> int:
    """Found lamps are added (new) or get their current address (known). Returns how many are new."""
    new = 0
    for lamp in found:
        known = await db.devices.find_one({"kind": "yeelight", "bulb_id": lamp["bulb_id"]})
        fields = {k: lamp[k] for k in ("ip", "port", "model", "lamp_name", "support")}
        if known:
            await db.devices.update_one({"_id": known["_id"]}, {"$set": fields})
        else:
            await db.devices.insert_one({"kind": "yeelight", "bulb_id": lamp["bulb_id"], "name": "", "created_at": now_iso(), **fields})
            new += 1
    return new


async def send(d: dict, method: str, params: list) -> list:
    """A command to a known lamp; if it moved (new IP), it is found again and the command retried."""
    try:
        return await command(d["ip"], d.get("port", PORT), method, params)
    except LampError:
        if not d.get("bulb_id"):
            raise
        found = [x for x in await discover(2) if x["bulb_id"] == d["bulb_id"]]
        if not found or found[0]["ip"] == d["ip"]:
            raise
        await remember(found)
        d.update(ip=found[0]["ip"], port=found[0]["port"])
        return await command(d["ip"], d["port"], method, params)


async def find_lamps(name: str = "") -> tuple[list[dict], str]:
    """The lamps a request means: by name ("quarto" → "luz do quarto"), "todas", or the only one."""
    all_ = await lamps()
    if not all_:
        return [], "Nenhuma lâmpada cadastrada. Em Configurações → Dispositivos, toque em \"Procurar lâmpadas\" (com \"Controle LAN\" ligado no app Yeelight)."
    want = _fold(name)
    if not want or want in ("todas", "todas as luzes", "tudo", "as luzes", "luzes"):
        if not want and len(all_) > 1:
            return [], "Há mais de uma lâmpada: " + ", ".join(_label(d) for d in all_) + ". Diga qual (ou \"todas\")."
        return all_, ""
    hits = [d for d in all_ if want in _fold(_label(d)) or _fold(_label(d)) in want]
    if not hits:
        return [], f"Nenhuma lâmpada chamada \"{name}\". As que existem: " + ", ".join(_label(d) for d in all_) + "."
    return hits, ""


async def state(d: dict) -> dict:
    power, bright, mode, rgb, ct = (list(await send(d, "get_prop", ["power", "bright", "color_mode", "rgb", "ct"])) + [""] * 5)[:5]
    num = lambda v: int(v) if str(v).isdigit() else None  # noqa: E731
    return {"power": power, "bright": num(bright), "color_mode": {"1": "rgb", "2": "ct"}.get(str(mode), ""), "rgb": num(rgb), "ct": num(ct),
            "colors": _supports(d, "set_rgb"), "temps": _supports(d, "set_ct_abx")}


COLORS = {  # pt-BR color names → RGB
    "vermelho": 0xFF0000, "vermelha": 0xFF0000, "verde": 0x00FF00, "azul": 0x0000FF, "amarelo": 0xFFD000, "amarela": 0xFFD000,
    "laranja": 0xFF6A00, "roxo": 0x8000FF, "roxa": 0x8000FF, "lilas": 0xB57EDC, "violeta": 0x7F00FF, "rosa": 0xFF4FA3,
    "pink": 0xFF1493, "magenta": 0xFF00FF, "ciano": 0x00FFFF, "turquesa": 0x30D5C8, "azul claro": 0x4FC3FF, "azul escuro": 0x0010A0,
    "verde claro": 0x7CFF6B, "verde escuro": 0x007A1E, "dourado": 0xFFB300, "salmao": 0xFA8072, "coral": 0xFF6F50,
}
TEMPERATURES = {"quente": 2700, "amarelada": 2700, "aconchegante": 2200, "neutra": 4000, "natural": 4000, "branca": 4000, "branco": 4000,
                "fria": 6000, "clara": 6000, "azulada": 6500}


def parse_color(value: str) -> tuple[Optional[int], Optional[int], str]:
    """'azul' / '#FF8800' → (rgb, None); 'branco quente' / 'fria' → (None, kelvin); else an error."""
    v = _fold(value).replace("luz ", "").strip()
    if m := re.fullmatch(r"#?([0-9a-f]{6})", v):
        return int(m.group(1), 16), None, ""
    if v in COLORS:
        return COLORS[v], None, ""
    words = v.replace("brancos ", "").replace("brancas ", "").replace("branco ", "").replace("branca ", "")
    plural = {"azuis": "azul", "lilases": "lilas"}.get(v) or (v[:-1] if v.endswith("s") else v)  # "as luzes roxas"
    if plural in COLORS:
        return COLORS[plural], None, ""
    words = plural if plural in TEMPERATURES else words
    if words in TEMPERATURES:
        return None, TEMPERATURES[words], ""
    return None, None, f"não conheço a cor \"{value}\" (use um nome como azul, vermelho, roxo, quente, fria, ou #RRGGBB)"


def _supports(d: dict, method: str) -> bool:
    sup = d.get("support") or []
    return not sup or method in sup  # added by IP: unknown, try it


async def adjust(d: dict, bright: Optional[int] = None, rgb: Optional[int] = None, ct: Optional[int] = None) -> list[str]:
    """Brightness / color / white temperature; turns the lamp on first if needed. What was done."""
    if rgb is not None and not _supports(d, "set_rgb"):
        raise LampError("essa lâmpada não faz cores (só branco)")
    if ct is not None and not _supports(d, "set_ct_abx"):
        raise LampError("essa lâmpada não muda a temperatura do branco")
    if (await state(d))["power"] != "on":
        await set_power(d, True)
    done = []
    if rgb is not None:
        await send(d, "set_rgb", [rgb, "smooth", 400])
        done.append(next((k for k, v in COLORS.items() if v == rgb), f"#{rgb:06X}"))
    if ct is not None:
        await send(d, "set_ct_abx", [max(1700, min(6500, ct)), "smooth", 400])
        done.append(next((k for k, v in TEMPERATURES.items() if v == ct), f"{ct} K"))
    if bright is not None:
        await send(d, "set_bright", [max(1, min(100, bright)), "smooth", 400])
        done.append(f"{max(1, min(100, bright))}%")
    return done


async def set_power(d: dict, on: bool) -> None:
    await send(d, "set_power", ["on" if on else "off", "smooth", 400])


# ---------- schedules ----------
def _when(args: dict) -> tuple[Optional[datetime], str]:
    """The moment from "minutes" (from now) or "when" (local AAAA-MM-DDTHH:MM, user's offset)."""
    now = datetime.now(timezone.utc)
    if args.get("minutes") not in (None, ""):
        try:
            m = int(args["minutes"])
        except (TypeError, ValueError):
            return None, "minutes deve ser um número"
        if not 1 <= m <= 7 * 24 * 60:
            return None, "minutes deve ser de 1 a 10080"
        return now + timedelta(minutes=m), ""
    when = str(args.get("when") or "").strip()
    try:
        t = datetime.fromisoformat(when)
    except ValueError:
        return None, "informe minutes ou when (AAAA-MM-DDTHH:MM, hora local)"
    if t.tzinfo is None:
        t = t.replace(tzinfo=timezone(timedelta(minutes=CLIENT_OFFSET.get() or 0)))
    t = t.astimezone(timezone.utc)
    if t < now - timedelta(minutes=1):
        return None, "esse horário já passou"
    if t > now + timedelta(days=30):
        return None, "agendamentos vão até 30 dias"
    return t, ""


async def run_due() -> None:
    now = datetime.now(timezone.utc).isoformat()
    for job in await db.light_jobs.find({"status": "pending"}).to_list(500):
        if job["at"] > now:
            continue
        ok, errors = 0, []
        for did in job["devices"]:
            d = await db.devices.find_one({"_id": ObjectId(did)})
            if not d:
                continue
            try:
                await set_power(d, job["on"])
                ok += 1
            except LampError as e:
                errors.append(f"{_label(d)}: {e}")
        await db.light_jobs.update_one({"_id": job["_id"]}, {"$set": {"status": "done" if not errors else "failed", "ran_at": now_iso(), "error": "; ".join(errors)}})
        log.info("agendamento %s: %s %d lâmpada(s)%s", job["_id"], "acendeu" if job["on"] else "apagou", ok, f" — falhas: {errors}" if errors else "")


async def run_scheduler() -> None:
    """Background loop (started with the server): runs the schedules whose time came."""
    while True:
        try:
            await run_due()
        except Exception:  # noqa: BLE001 — the loop must survive anything
            log.exception("agendamentos das lâmpadas")
        await asyncio.sleep(15)


def _job_out(j: dict) -> dict:
    return {"id": str(j["_id"]), "on": j["on"], "at": j["at"], "label": local_label(j["at"], j.get("offset")), "names": j.get("names", []),
            "status": j["status"], "error": j.get("error", "")}


# ---------- tools ----------
async def light_list(args: dict) -> Outcome:
    all_ = await lamps()
    if not all_:
        _, msg = await find_lamps()
        return Outcome(msg, "Nenhuma lâmpada cadastrada")
    lines = []
    for d in all_:
        try:
            s = await state(d)
            lines.append(f"- {_label(d)}: {'acesa' if s['power'] == 'on' else 'apagada'}" + (f", brilho {s['bright']}%" if s["bright"] is not None and s["power"] == "on" else "")
                         + ("" if _supports(d, "set_rgb") else " (só branco)"))
        except LampError as e:
            lines.append(f"- {_label(d)}: sem resposta ({e})")
    jobs = sorted((j for j in await db.light_jobs.find({"status": "pending"}).to_list(200)), key=lambda j: j["at"])
    if jobs:
        lines.append("Agendado:")
        lines += [f"- id {j['_id']}: {'acender' if j['on'] else 'apagar'} {', '.join(j.get('names', []))} · {local_label(j['at'], j.get('offset'))}" for j in jobs]
    return Outcome("\n".join(lines), f"{len(all_)} lâmpada(s)")


async def light_power(args: dict) -> Outcome:
    on = bool(args.get("on", True))
    targets, msg = await find_lamps(str(args.get("device") or ""))
    if not targets:
        return Outcome(msg, "Qual lâmpada?" if "Diga qual" in msg else "Lâmpada não encontrada", status="error")
    done, errors = [], []
    for d in targets:
        try:
            await set_power(d, on)
            done.append(_label(d))
        except LampError as e:
            errors.append(f"{_label(d)}: {e}")
    verb = "Acendeu" if on else "Apagou"
    if not done:
        return Outcome("Erro: " + "; ".join(errors), f"Não consegui {'acender' if on else 'apagar'}", status="error")
    return Outcome(f"{verb}: {', '.join(done)}." + (f" Falhou: {'; '.join(errors)}" if errors else ""), f"{verb} {', '.join(done)}")


async def light_set(args: dict) -> Outcome:
    bright = args.get("brightness")
    try:
        bright = int(bright) if bright not in (None, "") else None
    except (TypeError, ValueError):
        return Outcome("Erro: brightness deve ser de 1 a 100.", "Brilho inválido", status="error")
    rgb = ct = None
    if (color := str(args.get("color") or "").strip()):
        rgb, ct, err = parse_color(color)
        if err:
            return Outcome(f"Erro: {err}.", "Cor desconhecida", status="error")
    if bright is None and rgb is None and ct is None:
        return Outcome("Erro: informe brightness (1-100) e/ou color.", "Faltou brilho ou cor", status="error")
    targets, msg = await find_lamps(str(args.get("device") or ""))
    if not targets:
        return Outcome(msg, "Qual lâmpada?" if "Diga qual" in msg else "Lâmpada não encontrada", status="error")
    done, errors = [], []
    for d in targets:
        try:
            what = await adjust(d, bright, rgb, ct)
            done.append(f"{_label(d)} ({', '.join(what)})")
        except LampError as e:
            errors.append(f"{_label(d)}: {e}")
    if not done:
        return Outcome("Erro: " + "; ".join(errors), errors[0][:120] if len(errors) == 1 else "Não consegui ajustar a luz", status="error")
    return Outcome(f"Ajustei: {'; '.join(done)}." + (f" Falhou: {'; '.join(errors)}" if errors else ""), f"Luz: {'; '.join(done)}")


async def light_schedule(args: dict) -> Outcome:
    on = bool(args.get("on", True))
    at, err = _when(args)
    if not at:
        return Outcome(f"Erro: {err}.", "Horário inválido", status="error")
    targets, msg = await find_lamps(str(args.get("device") or ""))
    if not targets:
        return Outcome(msg, "Qual lâmpada?" if "Diga qual" in msg else "Lâmpada não encontrada", status="error")
    names = [_label(d) for d in targets]
    offset = CLIENT_OFFSET.get()
    res = await db.light_jobs.insert_one({"on": on, "at": at.isoformat(), "offset": offset, "devices": [str(d["_id"]) for d in targets],
                                          "names": names, "status": "pending", "created_at": now_iso()})
    label = local_label(at.isoformat(), offset)
    verb = "acender" if on else "apagar"
    return Outcome(f"Agendado (id {res.inserted_id}): {verb} {', '.join(names)} {label}. O LUMNA precisa estar rodando nessa hora.",
                   f"Vai {verb} {', '.join(names)} · {label}")


async def light_cancel(args: dict) -> Outcome:
    key = str(args.get("id") or "").strip()
    jobs = await db.light_jobs.find({"status": "pending"}).to_list(200)
    hit = [j for j in jobs if str(j["_id"]) == key] or ([j for j in jobs] if key in ("todos", "tudo") else [])
    if not hit:
        return Outcome("Erro: nenhum agendamento pendente com esse id (veja light_list).", "Agendamento não encontrado", status="error")
    for j in hit:
        await db.light_jobs.update_one({"_id": j["_id"]}, {"$set": {"status": "cancelled"}})
    return Outcome(f"{len(hit)} agendamento(s) cancelado(s).", f"Cancelou {len(hit)} agendamento(s) da luz")


_DEVICE = {"type": "string", "description": "Nome da lâmpada (ex.: quarto, sala) ou \"todas\"; vazio se só houver uma"}
for _t in [
    Tool("light_list", "Lâmpadas: ver", "Lista as lâmpadas Yeelight: acesa/apagada, brilho e agendamentos pendentes (com id).",
         {"type": "object", "properties": {}, "required": []}, light_list),
    Tool("light_power", "Lâmpadas: acender/apagar", "Acende ou apaga lâmpadas Yeelight da casa (mesma rede Wi-Fi).",
         {"type": "object", "properties": {"on": {"type": "boolean", "description": "true acende, false apaga"}, "device": _DEVICE}, "required": ["on"]},
         light_power, risk="act"),
    Tool("light_set", "Lâmpadas: brilho e cor",
         "Muda o brilho e/ou a cor de lâmpadas Yeelight (acende se estiver apagada). color: nome da cor (azul, vermelho, roxo, rosa, "
         "laranja…), #RRGGBB, ou o tom do branco: quente, neutra, fria.",
         {"type": "object", "properties": {
             "brightness": {"type": "integer", "description": "1 a 100"},
             "color": {"type": "string", "description": "azul, vermelho, #FF8800, quente, fria…"},
             "device": _DEVICE,
         }, "required": []},
         light_set, risk="act"),
    Tool("light_schedule", "Lâmpadas: agendar", "Agenda acender ou apagar lâmpadas Yeelight num horário (when, hora local) ou daqui a N minutos.",
         {"type": "object", "properties": {
             "on": {"type": "boolean", "description": "true acende, false apaga"},
             "when": {"type": "string", "description": "Data e hora LOCAL: AAAA-MM-DDTHH:MM"},
             "minutes": {"type": "integer", "description": "Ou: daqui a quantos minutos"},
             "device": _DEVICE,
         }, "required": ["on"]},
         light_schedule, risk="act"),
    Tool("light_cancel", "Lâmpadas: cancelar agendamento", "Cancela um agendamento das lâmpadas pelo id (veja light_list) ou \"todos\".",
         {"type": "object", "properties": {"id": {"type": "string"}}, "required": ["id"]}, light_cancel, risk="act"),
]:
    REGISTRY[_t.name] = _t
LIGHT_TOOLS = {"light_list", "light_power", "light_set", "light_schedule", "light_cancel"}


# ---------- routes: Configurações → Dispositivos ----------
@router.get("/devices")
async def get_devices(live: bool = False):
    out = []
    for d in await lamps():
        item = _out(d)
        if live:
            try:
                item.update(await state(d), online=True)
            except LampError as e:
                item.update(online=False, error=str(e))
        out.append(item)
    jobs = sorted((j for j in await db.light_jobs.find({"status": "pending"}).to_list(200)), key=lambda j: j["at"])
    return {"devices": out, "jobs": [_job_out(j) for j in jobs]}


@router.post("/devices/discover")
async def post_discover():
    found = await discover()
    new = await remember(found)
    return {"found": len(found), "new": new, **(await get_devices())}


class ManualIn(BaseModel):
    ip: str = Field(max_length=64)


@router.post("/devices")
async def add_by_ip(body: ManualIn):
    """A lamp by its IP (when the search doesn't find it): it must answer."""
    ip = body.ip.strip()
    if not _lan_ip(ip):
        raise HTTPException(400, "Use o IP da lâmpada na rede local (ex.: 192.168.0.23)")
    try:
        await command(ip, PORT, "get_prop", ["power"])
    except LampError as e:
        raise HTTPException(400, f"Não achei uma Yeelight em {ip}: {e}")
    if await db.devices.find_one({"kind": "yeelight", "ip": ip}):
        raise HTTPException(400, "Essa lâmpada já está cadastrada")
    await db.devices.insert_one({"kind": "yeelight", "bulb_id": "", "ip": ip, "port": PORT, "model": "", "lamp_name": "", "support": [], "name": "", "created_at": now_iso()})
    return await get_devices()


class DevicePatch(BaseModel):
    name: str = Field(max_length=60)


@router.patch("/devices/{did}")
async def rename_device(did: str, body: DevicePatch):
    if not await db.devices.find_one({"_id": oid(did)}):
        raise HTTPException(404, "Dispositivo não encontrado")
    await db.devices.update_one({"_id": oid(did)}, {"$set": {"name": body.name.strip()}})
    return await get_devices()


@router.delete("/devices/{did}")
async def delete_device(did: str):
    await db.devices.delete_one({"_id": oid(did)})
    return {"ok": True}


class PowerIn(BaseModel):
    on: bool


@router.post("/devices/{did}/power")
async def device_power(did: str, body: PowerIn):
    d = await db.devices.find_one({"_id": oid(did)})
    if not d:
        raise HTTPException(404, "Dispositivo não encontrado")
    try:
        await set_power(d, body.on)
        return {"ok": True, **(await state(d))}
    except LampError as e:
        raise HTTPException(502, str(e))


class AdjustIn(BaseModel):
    brightness: Optional[int] = Field(default=None, ge=1, le=100)
    color: Optional[str] = Field(default=None, max_length=40)


@router.post("/devices/{did}/adjust")
async def device_adjust(did: str, body: AdjustIn):
    d = await db.devices.find_one({"_id": oid(did)})
    if not d:
        raise HTTPException(404, "Dispositivo não encontrado")
    rgb = ct = None
    if body.color:
        rgb, ct, err = parse_color(body.color)
        if err:
            raise HTTPException(400, err)
    try:
        await adjust(d, body.brightness, rgb, ct)
        return {"ok": True, **(await state(d))}
    except LampError as e:
        raise HTTPException(502, str(e))


@router.delete("/devices/jobs/{jid}")
async def cancel_job(jid: str):
    await db.light_jobs.update_one({"_id": oid(jid)}, {"$set": {"status": "cancelled"}})
    return {"ok": True}
