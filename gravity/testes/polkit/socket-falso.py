#!/usr/bin/python3.12
"""O helper do polkit 126+ de mentira: um socket Unix. A conta chega primeiro, depois o cookie; a senha certa é "certa"."""
import os, socket, sys
caminho, log = sys.argv[1], sys.argv[2]
if os.path.exists(caminho):
    os.unlink(caminho)
s = socket.socket(socket.AF_UNIX); s.bind(caminho); s.listen(4)
while True:
    c, _ = s.accept()
    f = c.makefile("rw", buffering=1)
    usuario = f.readline().strip(); cookie = f.readline().strip()
    open(log, "a").write(f"{usuario} {cookie}\n")
    f.write("PAM_PROMPT_ECHO_OFF Password: \n"); f.flush()
    senha = f.readline().strip()
    f.write("SUCCESS\n" if senha == "certa" else "FAILURE\n"); f.flush()
    c.close()
