#!/usr/bin/env python3
"""O pré-setup inteiro: abre no primeiro login, escolhe perfil e hora, pula a rede, guarda o e-mail, liga e desliga a Emma, pareia um celular (com os recursos
escolhidos) e termina; depois o login não abre mais. O celular e a Emma são de mentira (celular_emma_falsos.py); timedatectl, rclone e pkill só anotam."""
import os, re, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *

gravity = sys.argv[4]


def est(k, padrao=""):
    return estado().get(k, padrao)


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


def arq(p):
    return le(f"{casa}/.config/lumna/{p}")


def pedidos():
    return le(f"{run}/pedidos.log")


def falso():
    return le(f"{run}/falso.log")


# 1. abre no primeiro login, na etapa 1, com o perfil Uso diário
confere(lambda: est("passo") == "1" and est("perfil") == "0", "abre na etapa 1 com Uso diário", 8)
confere(lambda: arq("setup.ini").count("aberturas=1") == 1, "a primeira abertura fica contada")
toca("perfil/1")
confere(lambda: est("perfil") == "1", "escolher Gaming")
toca("h12")
confere(lambda: est("h24") == "0", "escolher 12 horas")
toca("continuar", 1.0)
confere(lambda: est("passo") == "2", "Continuar vai para Rede e som")
confere(lambda: "formato = 12" in arq("hora.ini"), "a hora em 12 horas foi gravada ao sair da etapa", 6)
confere(lambda: "perfil=gaming" in arq("setup.ini"), "o perfil foi gravado")
confere(lambda: "ao_ligar=gaming" in arq("modos.ini") or "ao_ligar = gaming" in arq("modos.ini"), "Gaming começa no modo Gaming")
confere(lambda: "gamemode" in arq("sugestoes.ini"), "as sugestões de apps do perfil foram gravadas")
confere(lambda: "timedatectl set-timezone America/Sao_Paulo" in falso(), "o fuso foi mandado ao timedatectl", 6)

# 2. rede e som sem NetworkManager nem PipeWire: não trava, dá para pular
confere(lambda: existe("rede-depois"), "a etapa 2 tem 'Configurar rede depois'")
toca("rede-depois", 0.8)
confere(lambda: est("passo") == "3", "'Configurar rede depois' vai para Contas")

# 3. e-mail: o botão só vale com um endereço certo
toca("em-end")
digita("isso-nao-e-email")
toca("em-conectar", 0.5)
confere(lambda: est("email_ok") == "0", "e-mail inválido não é guardado")
toca("em-end")
tecla("a", mod="ctrl")
digita("eric@exemplo.com")
toca("em-conectar", 0.8)
confere(lambda: est("email_ok") == "1", "e-mail válido é guardado")
confere(lambda: "endereco=eric@exemplo.com" in arq("setup.ini"), "o e-mail está no setup.ini")
toca("em-manual", 0.6)
confere(lambda: existe("em-imap"), "'Configuração manual' mostra o servidor IMAP")
toca("continuar", 0.8)
confere(lambda: est("passo") == "4", "Contas continua para a Emma")

# 4. Emma: desligar no sistema e ligar de novo
toca("emma-on", 0.6)
confere(lambda: est("emma_on") == "0", "desligar a Emma")
toca("continuar", 1.0)
confere(lambda: est("passo") == "5", "Emma continua para o Connect")
confere(lambda: "ativa=nao" in arq("emma-sistema.ini"), "a Emma desligada no ini", 6)
confere(lambda: "pkill -x lumna-emma-chat" in falso() and "pkill -x lumna-emma" in falso(), "parou o serviço e o chat da Emma", 6)
toca("voltar", 0.6)
confere(lambda: est("passo") == "4", "Voltar vai para a Emma")
toca("emma-on", 0.6)
confere(lambda: est("emma_on") == "1", "ligar a Emma de novo")
toca("continuar", 1.0)
confere(lambda: "ativa=sim" in arq("emma-sistema.ini") and "lumna-emma subiu" in falso(), "ligada: ini e o serviço subiu", 6)

# 5. Connect: gerar o código; o celular aparece e já nasce com os recursos escolhidos
confere(lambda: est("passo") == "5", "chegou ao Connect")
toca("cc-gerar", 1.0)
confere(lambda: est("codigo") == "123-456", "o código de pareamento aparece", 5)
confere(lambda: est("pareados") == "1", "o celular aparece pareado", 8)
confere(lambda: "connect recurso cel1 files sim" in pedidos() and "connect recurso cel1 clipboard nao" in pedidos(), "os recursos escolhidos foram aplicados ao celular", 6)
confere(lambda: "recurso cel1 control sim" in pedidos() and "recurso cel1 phone_notifications nao" in pedidos(), "controle de mídia ligado e avisos do celular desligados", 4)
toca("rec/2", 1.0)
confere(lambda: "recurso cel1 clipboard sim" in pedidos(), "mudar um recurso já pareado vale na hora", 5)

# 6. terminar: concluído, a janela fecha e o login não abre mais
toca("continuar", 1.5)
confere(lambda: "concluido=sim" in arq("setup.ini"), "terminar grava concluído", 6)
confere(lambda: subprocess.run(["pgrep", "-x", "setup"], capture_output=True).returncode != 0, "a janela fecha ao terminar", 5)
r = subprocess.run([f"{gravity}/setup", "--login"], capture_output=True, timeout=10)
falha_login = r.returncode != 0
if falha_login:
    falha(f"o login depois de concluir devia sair em silêncio (código {r.returncode})")
time.sleep(0.5)
confere(lambda: subprocess.run(["pgrep", "-x", "setup"], capture_output=True).returncode != 0, "concluído: o login não abre a janela", 3)

# 7. 'não mostrar mais' e 'já abriu e não terminou': o login não abre janela (o segundo só lembra por aviso)
os.makedirs(f"{casa}/.config/lumna", exist_ok=True)
for conteudo in ("[setup]\nconcluido=nao\nnao_mostrar=sim\naberturas=1\n", "[setup]\nconcluido=nao\nnao_mostrar=nao\naberturas=1\n"):
    open(f"{casa}/.config/lumna/setup.ini", "w").write(conteudo)
    r = subprocess.run([f"{gravity}/setup", "--login"], capture_output=True, timeout=15)
    time.sleep(0.5)
    if r.returncode != 0 or subprocess.run(["pgrep", "-x", "setup"], capture_output=True).returncode == 0:
        falha(f"o login com {conteudo.split()[2]} {conteudo.split()[3]} não devia abrir a janela")

import shutil
shutil.copy(f"{run}/g.out", f"{run}/g1.out")

# 8. quem fecha pela metade sem pedir 'não mostrar' continua sendo lembrado; com o interruptor, nunca mais
open(f"{casa}/.config/lumna/setup.ini", "w").write("[setup]\nconcluido=nao\nnao_mostrar=nao\naberturas=0\n")
subprocess.Popen([f"{gravity}/setup", "--passo", "2"], stdout=open(f"{run}/g.out", "w"), stderr=subprocess.DEVNULL, env=dict(os.environ, LUMNA_SETUP_DEBUG="1"))
confere(lambda: est("passo") == "2", "--passo 2 abre direto na etapa 2", 8)
toca("nm-i", 0.6)
confere(lambda: est("nm") == "1", "o interruptor 'Não mostrar de novo' liga")
toca("sair", 1.0)
confere(lambda: "nao_mostrar=sim" in arq("setup.ini"), "fechar com o interruptor grava 'não mostrar mais'", 6)
confere(lambda: "concluido=nao" in arq("setup.ini"), "fechar sem terminar não marca concluído", 3)

sys.exit(1 if falhas else 0)
