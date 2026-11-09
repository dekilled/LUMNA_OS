#!/usr/bin/env python3
"""Discos e pendrives na página inicial do Atlas: um pendrive ligado e ainda não montado aparece como cartão com "Montar"; tocar monta (o udisksctl de mentira) e abre a pasta.
python3 teste_discos.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

fake = os.environ["FAKE_DISCOS_DIR"]
registro = f"{fake}/udisksctl.log"

# 1. o Início mostra o pendrive que está ligado e não montado (a lista se refaz sozinha em até uns 3 s)
toca("l-it/0")
confere(lambda: estado().get("vista") == "3", "o Início abre")
for _ in range(60):
    if pos("in-disco/0"):
        break
    time.sleep(0.2)
if not pos("in-disco/0"):
    falha("o pendrive ligado e não montado devia aparecer como cartão no Início")
    subprocess.run(["grim", f"{run}/falha.png"])
else:
    if os.path.exists(registro) and "mount" in open(registro).read():
        falha("só aparecer não monta")
    # 2. tocar monta (udisksctl mount -b /dev/sdb1) e abre a pasta onde ficou
    toca("in-disco/0")
    confere(lambda: os.path.exists(registro) and "mount -b /dev/sdb1" in open(registro).read(), "tocar no cartão monta o pendrive com o udisks")
    confere(lambda: estado().get("pasta") == f"{fake}/PENDRIVE", "depois de montar, o Atlas abre a pasta do pendrive")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (discos e pendrives): ok")
