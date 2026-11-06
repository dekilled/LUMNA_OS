#!/usr/bin/env python3
"""O serviço lumna-emma de mentira, para o Painel: ouve em LUMNA_EMMA_SOCK as linhas "rest METODO CAMINHO CORPO" e responde
{ok, status, corpo} com um estado em memória (conversas, projetos com memórias, lembretes). Cada pedido vai para o arquivo de log."""
import json, os, socket, sys

sock_path, log_path = sys.argv[1], sys.argv[2]
estado = {
    "chat": [{"id": f"c{i}", "title": f"Conversa {i}"} for i in range(1, 4)],
    "popup": [{"id": f"a{i}", "title": f"Comando {i}"} for i in range(1, 3)],
    "memorias": {"p1": [{"id": "m1"}, {"id": "m2"}], "p2": [{"id": "m3"}]},
    "lembretes": [
        {"id": "r1", "text": "Ligar para o dentista", "label": "sex 06/11 às 09:00", "status": "pending"},
        {"id": "r2", "text": "Pagar a luz", "label": "qui 05/11 às 18:00", "status": "done"},
    ],
}

def responder(metodo, caminho):
    base = caminho.split("?")[0]
    if metodo == "GET" and base == "/api/sessions":
        return estado["popup"] if "origin=assistente" in caminho else estado["chat"]
    if metodo == "DELETE" and base.startswith("/api/sessions/"):
        sid = base.rsplit("/", 1)[1]
        for k in ("chat", "popup"):
            estado[k] = [s for s in estado[k] if s["id"] != sid]
        return {"ok": True}
    if metodo == "GET" and base == "/api/projects":
        return [{"id": p, "name": p} for p in estado["memorias"]]
    if metodo == "GET" and base.startswith("/api/projects/") and base.endswith("/memories"):
        return estado["memorias"].get(base.split("/")[3], [])
    if metodo == "DELETE" and base.startswith("/api/memories/"):
        mid = base.rsplit("/", 1)[1]
        for p in estado["memorias"]:
            estado["memorias"][p] = [m for m in estado["memorias"][p] if m["id"] != mid]
        return {"ok": True}
    if metodo == "GET" and base == "/api/reminders":
        return {"items": estado["lembretes"], "now": "2026-11-06T12:00:00+00:00"}
    if metodo == "DELETE" and base.startswith("/api/reminders/"):
        rid = base.rsplit("/", 1)[1]
        for l in estado["lembretes"]:
            if l["id"] == rid:
                l["status"] = "cancelled"
        return {"ok": True}
    return None

if os.path.exists(sock_path):
    os.unlink(sock_path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(8)
while True:
    c, _ = srv.accept()
    linha = c.makefile("r").readline().strip()
    partes = linha.split(" ", 3)
    with open(log_path, "a") as f:
        f.write(linha + "\n")
    if partes[0] == "rest" and len(partes) >= 3:
        corpo = responder(partes[1], partes[2])
        resp = {"ok": True, "status": 200, "corpo": corpo} if corpo is not None else {"ok": False, "status": 404, "corpo": {"detail": "não existe"}}
    else:
        resp = {"ok": False, "status": 0, "erro": "pedido desconhecido"}
    c.sendall((json.dumps(resp) + "\n").encode())
    c.close()
