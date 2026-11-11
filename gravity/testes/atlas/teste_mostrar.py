#!/usr/bin/env python3
"""Abrir o Atlas num arquivo ("Mostrar na pasta" de um download): abre a pasta dele com o arquivo selecionado.
python3 teste_mostrar.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
docs = f"{casa}/Documentos"
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


gravity = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
alvo = f"{docs}/notas.md"
saida = f"{run}/mostrar.out"
env = dict(os.environ, LUMNA_ATLAS_DEBUG="1")
proc = subprocess.Popen([os.environ.get("ATLAS_BIN", f"{gravity}/atlas"), alvo], env=env, stdout=open(saida, "w"), stderr=subprocess.DEVNULL)
fim = time.time() + 6
ok = False
while time.time() < fim and not ok:
    time.sleep(0.2)
    txt = open(saida, errors="replace").read()
    est = [l for l in txt.splitlines() if l.startswith("estado ")]
    ordem = [l for l in txt.splitlines() if l.startswith("ordem")]
    if est and ordem:
        e = dict(re.findall(r"(\w+)=(\[[^\]]*\]|\S+)", est[-1]))
        ok = e.get("pasta") == docs and e.get("sel") == "1" and "notas.md*" in ordem[-1].split(" ")
if not ok:
    falha(f"o Atlas aberto num arquivo devia abrir a pasta dele com o arquivo selecionado (estado {est[-1] if est else None}, ordem {ordem[-1][:200] if ordem else None})")
proc.kill()
sys.exit(1 if falhas else 0)
