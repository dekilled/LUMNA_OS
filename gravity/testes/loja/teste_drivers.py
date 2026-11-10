#!/usr/bin/env python3
"""Drivers e kernels da Loja num Orbit headless (NVIDIA RTX 3060 + Intel, kernel linux, GRUB): o hardware, as abas, "só compatíveis", o detalhe e o motivo, instalar um kernel
com os headers e a conferência do boot, remover (o em uso não sai), o incompatível bloqueado e o modo avançado."""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *


def est(k, padrao=""):
    return estado().get(k, padrao)


def num(k):
    try:
        return int(est(k, "-1"))
    except ValueError:
        return -1


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


def log():
    return le(f"{run}/pacman.log")


confere(lambda: est("carregando") == "0" and num("npkg") >= 10, "o catálogo carrega", 10)
toca("lj-nav/4")
confere(lambda: est("tela") == "4" and num("dr_n") == 10, f"Drivers e kernels: 10 drivers compatíveis ({est('dr_n')})")
confere(lambda: est("hw") == "[nvidia/GeForce RTX 3060]", f"o hardware detectado ({est('hw')})")

# 1. Ver hardware
toca("lj-d-hw")
confere(lambda: existe("lj-hw-fechar"), "Ver hardware abre o diálogo")
toca("lj-hw-fechar")
confere(lambda: not existe("lj-hw-fechar"), "e fecha")

# 2. só compatíveis
toca("lj-d-compat")
confere(lambda: est("dr_compat") == "0" and num("dr_n") == 17, f"sem o filtro aparecem os 17 drivers, com o motivo de cada um ({est('dr_n')})")
toca("lj-d-compat")
confere(lambda: est("dr_compat") == "1" and num("dr_n") == 10, "e o filtro volta a esconder o que não serve")

# 3. os kernels
toca("lj-d-aba/1")
confere(lambda: est("dr_aba") == "1" and num("dr_n") == 4, "a aba Kernels lista os 4 kernels")

# 4. detalhe de um kernel em uso: não dá para remover
toca("lj-d-ver/0")
confere(lambda: est("dr_det") == "[linux]", "o detalhe do linux")
confere(lambda: not existe("lj-dd-remover") and not existe("lj-dd-instalar"), "o kernel em uso não tem Remover nem Instalar")
toca("lj-dd-fechar")
confere(lambda: est("dr_det") == "[]", "fecha o detalhe")

# 5. instalar o linux-lts: pergunta, instala com os headers, confere o boot
toca("lj-d-ver/1")
confere(lambda: est("dr_det") == "[linux-lts]" and existe("lj-dd-instalar"), "o detalhe do linux-lts tem Instalar")
toca("lj-dd-instalar")
confere(lambda: est("dr_conf") == "1", "Instalar pede confirmação")
toca("lj-dc-cancelar")
confere(lambda: est("dr_conf") == "0" and "linux-lts" not in log().replace("-Si", ""), "Cancelar não instala")
toca("lj-dd-instalar")
toca("lj-dc-ok")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "1", "o kernel instala", 12)
confere(lambda: "pacman -S --noconfirm --needed linux-lts linux-lts-headers" in log(), "instalou o kernel e os headers numa transação só")
confere(lambda: num("checks") == 3 and num("checks_ok") == 2, f"a Loja conferiu o boot: kernel e initramfs ok, GRUB sem a entrada ({est('checks_ok')}/{est('checks')})")
time.sleep(1.0)   # o painel cresceu com a conferência do boot: o botão mudou de lugar
toca("lj-ag-fechar")
confere(lambda: est("ag_painel") == "0", "fecha o painel")

# 6. agora há dois kernels: o lts (não é o em uso) pode sair
toca("lj-d-ver/1")
confere(lambda: existe("lj-dd-remover"), "o linux-lts instalado pode ser removido (não é o em uso)")
toca("lj-dd-remover")
confere(lambda: est("dr_conf") == "1", "Remover pede confirmação")
toca("lj-dc-ok")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "1", "o kernel sai", 12)
confere(lambda: "pacman -Rns --noconfirm linux-lts linux-lts-headers" in log(), "removeu o kernel e os headers")
toca("lj-ag-fechar")

# 7. um driver "verifique" (nvidia-dkms) instala com os headers
toca("lj-d-aba/0")
confere(lambda: est("dr_aba") == "0", "volta aos drivers")
# a ordem na tela: mesa, nvidia-open, nvidia, nvidia-dkms, ...
toca("lj-d-ver/3")
confere(lambda: est("dr_det") == "[nvidia-dkms]", f"o detalhe do nvidia-dkms ({est('dr_det')})")
toca("lj-dd-instalar")
toca("lj-dc-ok")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "1", "o nvidia-dkms instala", 12)
confere(lambda: "pacman -S --noconfirm --needed nvidia-dkms linux-headers" in log(), "com os headers do kernel em uso")
toca("lj-ag-fechar")

# 8. o incompatível: bloqueado; o modo avançado libera
toca("lj-d-compat")   # mostra tudo
confere(lambda: num("dr_n") == 17, "mostrando tudo")
def rola_ate(nome):
    """arrasta a página com o dedo até o item caber na janela (a lista é mais alta que ela)"""
    ox, oy = origem()
    for _ in range(4):
        p = pos(nome)
        if p and p[1] + p[3] < 650:
            return
        msg("toque", "baixo", 1, ox + 900, oy + 600)
        for k in range(1, 9):
            msg("toque", "move", 1, ox + 900, oy + 600 - k * 45)
            time.sleep(0.03)
        msg("toque", "cima", 1)
        time.sleep(0.9)


rola_ate("lj-d-ver/7")
toca("lj-d-ver/7")   # nvidia-lts
confere(lambda: est("dr_det") == "[nvidia-lts]" and existe("lj-dd-instalar"), f"o detalhe do nvidia-lts ({est('dr_det')})")
toca("lj-dd-instalar")
time.sleep(0.5)
confere(lambda: est("dr_conf") == "0", "incompatível: o botão está bloqueado")
toca("lj-dd-fechar")
toca("lj-nav/5")
toca("lj-cfg-avancado")
confere(lambda: est("avancado") == "1", "o modo avançado liga em Configurações")
confere(lambda: "avancado=true" in le(f"{casa}/.config/lumna/loja/config.ini"), "e grava no config.ini")
toca("lj-nav/4")
confere(lambda: est("tela") == "4", "volta a Drivers")
rola_ate("lj-d-ver/7")
toca("lj-d-ver/7")
confere(lambda: est("dr_det") == "[nvidia-lts]", "abre o nvidia-lts de novo")
toca("lj-dd-instalar")
confere(lambda: est("dr_conf") == "1", "com o modo avançado, pede confirmação (em vermelho)")
toca("lj-dc-cancelar")
confere(lambda: "nvidia-lts" not in log().replace("-Si", ""), "e cancelar não instala")

print("loja (drivers e kernels, app): ok" if not falhas else f"loja drivers: {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
