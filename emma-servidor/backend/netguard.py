"""Server-side request forgery guard for user-supplied URLs."""
import ipaddress
import os
import socket
from urllib.parse import urlparse

from fastapi import HTTPException


def _private_allowed() -> bool:
    return os.environ.get("ALLOW_PRIVATE_URLS", "false").lower() == "true"


def assert_public_url(url: str) -> str:
    p = urlparse(url)
    if p.scheme not in ("http", "https") or not p.hostname:
        raise HTTPException(400, "URL inválida: use http(s)://host")
    if _private_allowed():
        return url
    try:
        infos = socket.getaddrinfo(p.hostname, p.port or (443 if p.scheme == "https" else 80), proto=socket.IPPROTO_TCP)
    except socket.gaierror:
        raise HTTPException(400, f"Host não resolvido: {p.hostname}")
    for info in infos:
        ip = ipaddress.ip_address(info[4][0].split("%")[0])
        if not ip.is_global or ip.is_multicast:
            raise HTTPException(400, f"Endereço bloqueado (rede privada/interna): {p.hostname}")
    return url
