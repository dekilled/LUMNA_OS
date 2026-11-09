#!/usr/bin/env python3
"""O Atlas não cai nem fica pedindo quadros à toa: nomes que não são UTF-8 (pendrives antigos, zips), pasta que muda por fora enquanto está aberta e o app parado.
python3 teste_robustez.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])


def pid():
    r = subprocess.run(["pgrep", "-x", "atlas"], capture_output=True, text=True).stdout.split()
    return int(r[0]) if r else None


def ticks(p):
    return sum(int(x) for x in open(f"/proc/{p}/stat").read().rsplit(")", 1)[1].split()[11:13])


p0 = pid()
if not p0:
    falha("o Atlas não está rodando")
    sys.exit(1)

# 1. nomes que não são UTF-8: antes caía ao ordenar (g_utf8_normalize devolve NULL)
pasta_n = os.path.join(casa.encode(), b"Documentos", b"antigos")
os.makedirs(pasta_n, exist_ok=True)
for nome in [b"caf\xe9.txt", b"\xff\xfe_invalido.jpg", b"na\xefve.doc", b"normal.txt", b"p\xe9"]:
    try:
        if nome == b"p\xe9":
            os.makedirs(os.path.join(pasta_n, nome), exist_ok=True)
        else:
            open(os.path.join(pasta_n, nome), "wb").write(b"x")
    except OSError:
        pass
vai_para(os.fsdecode(pasta_n))
time.sleep(1.0)
if pid() != p0:
    falha("o Atlas caiu ao abrir uma pasta com nomes que não são UTF-8")
    sys.exit(1)
confere(lambda: estado().get("nvis") == "5", "os 5 itens da pasta de nomes antigos aparecem")

# 2. a pasta muda por fora (uma rajada de arquivos): o Atlas junta os avisos, relê e segue vivo
for i in range(60):
    open(os.path.join(pasta_n, f"chegou_{i:03d}.txt".encode()), "wb").write(b"x")
time.sleep(0.15)
for i in range(60, 120):
    open(os.path.join(pasta_n, f"chegou_{i:03d}.txt".encode()), "wb").write(b"x")
confere(lambda: estado().get("nvis") == "125", "depois da rajada, a lista tem os 125 itens")
if pid() != p0:
    falha("o Atlas caiu com a pasta mudando por fora")
    sys.exit(1)

# 3. parado, o Atlas não pede quadros: o CPU fica perto de zero (antes cada commit de janela do Orbit acordava todo app e dava 60 quadros por segundo)
time.sleep(1.5)
a = ticks(p0)
time.sleep(3.0)
usado = ticks(p0) - a  # em centésimos de segundo de CPU, em 3 s
if usado > 30:  # mais de 10 % de um núcleo
    falha(f"parado, o Atlas gastou {usado} centésimos de CPU em 3 s (devia ficar perto de zero: ele está redesenhando à toa)")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (robustez: nomes não UTF-8, pasta que muda e app parado): ok")
