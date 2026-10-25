#!/usr/bin/env python3
"""Um greetd de mentira para testar a tela de login sem mexer no login de verdade.

Fala o protocolo do greetd (JSON com o tamanho na frente) num socket: aceita qualquer usuário com
a senha "lumna" e, no fim, só mostra a sessão que o greetd de verdade abriria.
   greetd-falso.py /caminho/do/socket
"""
import json
import os
import socket
import struct
import sys

caminho = sys.argv[1]
if os.path.exists(caminho):
    os.unlink(caminho)
servidor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
servidor.bind(caminho)
servidor.listen(4)


def ler(c):
    cab = c.recv(4, socket.MSG_WAITALL)
    if len(cab) < 4:
        return None
    (n,) = struct.unpack("=I", cab)
    return json.loads(c.recv(n, socket.MSG_WAITALL))


def mandar(c, msg):
    corpo = json.dumps(msg).encode()
    c.sendall(struct.pack("=I", len(corpo)) + corpo)


while True:
    c, _ = servidor.accept()
    usuario = None
    while True:
        msg = ler(c)
        if msg is None:
            break
        tipo = msg.get("type")
        if tipo == "create_session":
            usuario = msg.get("username")
            mandar(c, {"type": "auth_message", "auth_message_type": "secret", "auth_message": "Senha:"})
        elif tipo == "post_auth_message_response":
            if msg.get("response") == "lumna":
                mandar(c, {"type": "success"})
            else:
                mandar(c, {"type": "error", "error_type": "auth_error", "description": "pam_authenticate: AUTH_ERR"})
        elif tipo == "cancel_session":
            mandar(c, {"type": "success"})
        elif tipo == "start_session":
            print(f"greetd-falso: {usuario} entrou; abriria a sessão {msg.get('cmd')}", flush=True)
            mandar(c, {"type": "success"})
    c.close()
