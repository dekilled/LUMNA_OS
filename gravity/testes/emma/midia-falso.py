#!/usr/bin/env python3
"""O serviço da Mídia de mentira: responde ok a tudo e registra o pedido. python3 midia-falso.py SOCKET LOG"""
import os, socketserver, sys

SOCK, LOG = sys.argv[1], sys.argv[2]
if os.path.exists(SOCK):
    os.unlink(SOCK)


class H(socketserver.StreamRequestHandler):
    def handle(self):
        for linha in self.rfile:
            linha = linha.decode().strip()
            with open(LOG, "a") as f:
                f.write(f"midia {linha}\n")
            self.wfile.write(b'{"ok":true}\n')


socketserver.ThreadingUnixStreamServer(SOCK, H).serve_forever()
