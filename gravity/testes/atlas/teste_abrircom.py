#!/usr/bin/env python3
"""Abrir com...: o menu de contexto de um arquivo ganha "Abrir com…", que abre a janelinha de escolher o aplicativo; abrir por ela lança o app
escolhido com o arquivo, e "sempre usar" grava o padrão do tipo. Um aplicativo de mentira (um .desktop que anota o que recebeu) faz de app.
python3 teste_abrircom.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json, glob

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

# o app de mentira
apps = f"{casa}/.local/share/applications"
os.makedirs(apps, exist_ok=True)
open(f"{apps}/teste-abridor.desktop", "w").write(f"""[Desktop Entry]
Type=Application
Name=Teste Abridor
Exec=sh -c 'echo "$0" >> {run}/aberto.log' %f
MimeType=text/markdown;
""")
open(f"{apps}/mimeinfo.cache", "w").write("[MIME Cache]\ntext/markdown=teste-abridor.desktop;\n")  # o que o update-desktop-database escreveria (o GIO acha os apps do tipo por este arquivo)
aberto = f"{run}/aberto.log"

vai_para(docs)
tecla("Escape")
segura_sel("notas.md")  # seleciona e segura: o menu de contexto do arquivo
confere(lambda: pos("mi-abrir-com") is not None, "o menu de um arquivo tem 'Abrir com…'")
toca("mi-abrir-com")
confere(lambda: estado().get("abrircom") == "1" and pos("ac-app/0") is not None, "'Abrir com…' abre a janelinha com a lista de aplicativos")
toca("ac-app/0")
toca("ac-sempre")
toca("ac-ok", 1.0)
confere(lambda: os.path.exists(aberto) and f"{docs}/notas.md" in open(aberto).read(), "abrir pela janelinha lança o aplicativo escolhido com o arquivo", 6)
confere(lambda: estado().get("abrircom") == "0", "a janelinha fecha depois de abrir")
mime = ""
for f in (f"{casa}/.config/mimeapps.list", f"{casa}/.local/share/applications/mimeapps.list"):
    if os.path.exists(f):
        mime += open(f).read()
if "text/markdown=teste-abridor.desktop" not in mime:
    falha(f"'Sempre usar' devia gravar o padrão do tipo (mimeapps: {mime[:200]!r})")

# Esc fecha sem abrir
n = len(open(aberto).read().splitlines())
segura_sel("notas.md")
toca("mi-abrir-com")
confere(lambda: estado().get("abrircom") == "1", "a janelinha abre de novo")
tecla("Escape")
confere(lambda: estado().get("abrircom") == "0", "Esc fecha a janelinha")
time.sleep(0.5)
if len(open(aberto).read().splitlines()) != n:
    falha("Esc não devia abrir nada")
sys.exit(1 if falhas else 0)
