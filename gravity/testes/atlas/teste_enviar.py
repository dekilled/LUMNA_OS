#!/usr/bin/env python3
"""Enviar para: o menu de contexto de um arquivo ganha "Enviar para" (Área de trabalho, Documentos, E-mail, contas na nuvem e compartilhamentos de rede
conectados) e "Enviar para dispositivo…" (o antigo Compartilhar, os celulares do Connect). Um xdg-email de mentira anota o que recebeu.
python3 teste_enviar.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json, glob

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

gravity = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cfgdir = f"{casa}/.config/lumna/atlas"
servidor = f"{casa}/Servidor"
desktop = f"{casa}/Desktop"
# o cliente de e-mail de mentira
email_log = f"{run}/email.log"
open(f"{run}/bin/xdg-email", "w").write(f'#!/bin/sh\necho "$@" >> {email_log}\n')
os.chmod(f"{run}/bin/xdg-email", 0o755)


def n(chave):
    return int(estado().get(chave, "-9"))


def abre_enviar(arquivo="notas.md"):
    tecla("Escape")
    segura_sel(arquivo)
    confere(lambda: pos("mi-enviar") is not None, "o menu de um arquivo tem 'Enviar para'")
    toca("mi-enviar", 0.6)


vai_para(docs)
tecla("Escape")
segura_sel("notas.md")
confere(lambda: pos("mi-compart") is not None, "o botão do Connect continua lá (agora 'Enviar para dispositivo…')")
tecla("Escape")

# 1. sem nuvem nem rede conectadas: só os locais da máquina, e o aviso
abre_enviar()
confere(lambda: pos("ev-desktop") is not None and pos("ev-docs") is not None and pos("ev-email") is not None, "o submenu tem Área de trabalho, Documentos e E-mail")
confere(lambda: pos("ev-nada") is not None, "sem nuvem nem rede conectada o submenu avisa")

# 2. Área de trabalho: copia (não move) para ~/Desktop
toca("ev-desktop", 1.2)
confere(lambda: os.path.exists(f"{desktop}/notas.md"), "'Área de trabalho' copia o arquivo para ~/Desktop", 6)
confere(lambda: os.path.exists(f"{docs}/notas.md"), "e o original continua onde estava")

# 3. E-mail: o cliente de e-mail do sistema recebe o arquivo como anexo
abre_enviar()
toca("ev-email", 1.0)
confere(lambda: os.path.exists(email_log) and f"--attach {docs}/notas.md" in open(email_log).read(), "'E-mail' abre o cliente com o arquivo anexado", 6)

# 4. um compartilhamento de rede conectado aparece e recebe a cópia
tecla("Escape")
time.sleep(0.6)
toca("t-ajustes", 1.0)
confere(lambda: pos("cfg-pg/3") is not None, "a página de ajustes abre")
toca("cfg-pg/3")
confere(lambda: pos("rd-url") is not None, "a página Rede e nuvem abre")
toca("rd-url")
digita("livre/dados")
toca("rd-conectar")
confere(lambda: n("nshares") == 1 and n("nmont") == 1, "um compartilhamento conectado")
toca("cfg-voltar", 0.8)
vai_para(docs)
abre_enviar()
confere(lambda: pos("ev-rede/0") is not None, "o compartilhamento conectado aparece no 'Enviar para'")
toca("ev-rede/0", 1.2)
confere(lambda: os.path.exists(f"{servidor}/notas.md"), "enviar para o compartilhamento copia o arquivo para lá", 6)

# 5. uma conta na nuvem (pasta local da conta): reabre o app com a conta no rede.ini
subprocess.run(["pkill", "-x", "atlas"])
time.sleep(0.8)
nuvem = f"{casa}/Nuvem/Conta"
os.makedirs(nuvem, exist_ok=True)
ini = open(f"{cfgdir}/rede.ini").read()
open(f"{cfgdir}/rede.ini", "w").write(ini + f"\n[conta000]\nnome=Minha nuvem\ntipo=webdav\nremote=minha-nuvem\npasta={nuvem}\n")
atlas = subprocess.Popen([os.environ.get("ATLAS_BIN", f"{gravity}/atlas"), docs], env=dict(os.environ, LUMNA_ATLAS_DEBUG="1"),
                         stdout=open(f"{run}/ar.out", "w"), stderr=subprocess.DEVNULL)
time.sleep(2.5)
abre_enviar()
confere(lambda: pos("ev-nuvem/0") is not None, "a conta na nuvem aparece no 'Enviar para'")
toca("ev-nuvem/0", 1.2)
confere(lambda: os.path.exists(f"{nuvem}/notas.md"), "enviar para a nuvem copia o arquivo para a pasta da conta", 6)
# devolve tudo como estava (os testes seguintes contam com o app aberto, sem a conta nem a cópia)
atlas.kill()
time.sleep(0.8)
open(f"{cfgdir}/rede.ini", "w").write(ini)
for f in (f"{desktop}/notas.md", f"{servidor}/notas.md", f"{nuvem}/notas.md"):
    if os.path.exists(f):
        os.remove(f)
subprocess.Popen([os.environ.get("ATLAS_BIN", f"{gravity}/atlas"), docs], env=dict(os.environ, LUMNA_ATLAS_DEBUG="1"),
                 stdout=open(f"{run}/ar.out", "w"), stderr=subprocess.DEVNULL)
time.sleep(2.5)
sys.exit(1 if falhas else 0)
