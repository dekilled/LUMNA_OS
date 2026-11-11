#!/usr/bin/env python3
"""O serviço do celular (lumna-connect.sock) e o da Emma (lumna-emma.sock) de mentira para o teste do pré-setup. Anota cada pedido em $RUN/pedidos.log.
Só passa a ter um celular pareado depois de um "parear"."""
import json, os, socket, sys, threading, time

run = sys.argv[1]
pareou = False
trava = threading.Lock()


def anota(s):
    with open(f"{run}/pedidos.log", "a") as f:
        f.write(s + "\n")


def connect(linha):
    global pareou
    with trava:
        if linha == "parear":
            pareou_agora = True
            threading.Timer(2.0, lambda: globals().update(pareou=True)).start()  # o celular digita o código e aparece depois de 2 s
            return json.dumps({"ok": True, "codigo": "123-456", "nome": "teste", "expira": 300, "enderecos": ["10.0.0.5"], "porta": 47800})
        if linha in ("connect", "estado"):
            ds = [{"id": "cel1", "nome": "Celular do teste", "conectado": True, "recursos": {}}] if pareou else []
            return json.dumps({"ativo": True, "dispositivos": ds})
        return json.dumps({"ok": True})


def emma(linha):
    if linha.startswith("llm "):
        p = json.loads(linha[4:])
        if "ruim" in p.get("url", ""):
            return json.dumps({"chat": False, "error": "401 chave inválida"})
        return json.dumps({"chat": True, "streaming": True})
    return json.dumps({"ok": True})


def serve(nome, fn):
    caminho = f"{run}/{nome}"
    if os.path.exists(caminho):
        os.unlink(caminho)
    s = socket.socket(socket.AF_UNIX)
    s.bind(caminho)
    s.listen(8)

    def trata(c):
        try:
            linha = c.makefile().readline().strip()
            anota(f"{nome.split('.')[0].replace('lumna-', '')} {linha}")
            c.sendall((fn(linha) + "\n").encode())
        finally:
            c.close()

    while True:
        c, _ = s.accept()
        threading.Thread(target=trata, args=(c,), daemon=True).start()


threading.Thread(target=serve, args=("lumna-connect.sock", connect), daemon=True).start()
threading.Thread(target=serve, args=("lumna-emma.sock", emma), daemon=True).start()
while True:
    time.sleep(1)
