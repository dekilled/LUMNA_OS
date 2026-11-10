#!/usr/bin/env python3
"""A Loja inteira num Orbit headless: Explorar (cartões e categorias), a busca, Pacotes com filtros, o detalhe, instalar (com confirmação e o log), o AUR
(aviso e instalação), Instalados e remover, Atualizações e "Atualizar tudo", falha do pacman, Configurações e os atalhos."""
import os, re, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *


def est(k, padrao=""):
    return estado().get(k, padrao)


def num(k):
    try:
        return int(est(k, "-1"))
    except ValueError:
        return -1


def lista():
    return [x for x in est("itens", "[]")[1:-1].split(",") if x]


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


def pacman_log():
    return le(f"{run}/pacman.log")


def limpa_busca():
    toca("lj-busca")
    tecla("a", mod="ctrl")
    tecla("BackSpace")


def busca(txt):
    limpa_busca()
    digita(txt)


# 1. o catálogo carrega e abre em Explorar, com os cartões dos apps conhecidos
confere(lambda: est("carregando") == "0" and num("npkg") == 10, "o catálogo carrega (10 pacotes)", 10)
confere(lambda: est("tela") == "0", "abre em Explorar")
confere(lambda: existe("lj-cartao/3") and not existe("lj-cartao/4"), "4 cartões em destaque (blender, firefox, htop, vlc)")
confere(lambda: num("natual") == 3, "3 atualizações marcadas (vlc, htop, meu-app-aur)")

# 2. as abas de categoria
toca("lj-aba/4")   # Multimídia
confere(lambda: existe("lj-cartao/0") and not existe("lj-cartao/1"), "Multimídia: só o VLC")
toca("lj-aba/0")
confere(lambda: existe("lj-cartao/3"), "Todos volta aos 4 cartões")

# 3. a busca leva a Pacotes; o AUR entra, o duplicado do repositório oficial não
busca("mesa")
confere(lambda: est("tela") == "1" and num("nres") == 4, f"buscar 'mesa' abre Pacotes com 4 resultados ({est('nres')})")
confere(lambda: lista()[0] == "mesa", "o nome exato vem primeiro")

# 4. os filtros
toca("lj-tipo/4")   # Driver
confere(lambda: num("nres") == 1 and lista() == ["libva-mesa-driver"], "filtro Driver: só o libva-mesa-driver")
toca("lj-tipo/0")
confere(lambda: num("nres") == 4, "Todos os tipos volta a 4")
toca("lj-repo-multilib")
confere(lambda: num("nres") == 3 and "lib32-mesa" not in lista(), "sem multilib some o lib32-mesa")
toca("lj-repo-multilib")
confere(lambda: num("nres") == 4, "ligar multilib de volta")

# 5. o detalhe
k = lista().index("mesa-utils")
toca(f"lj-linha/{k}")
confere(lambda: est("det") == "[mesa-utils]", "tocar na linha abre o detalhe")
toca("lj-det-fechar")
confere(lambda: est("det") == "[]", "fechar o detalhe")
toca(f"lj-linha/{k}")
tecla("Escape")
confere(lambda: est("det") == "[]", "Esc fecha o detalhe")

# 6. instalar: pergunta, instala, o painel mostra o log, e o pacote passa a instalado
ninst = num("ninst")
toca(f"lj-acao/{k}")
confere(lambda: est("conf") == "1", "Instalar pede confirmação")
toca("lj-conf-cancelar")
confere(lambda: est("conf") == "0" and "mesa-utils" not in pacman_log().replace("-Si", ""), "Cancelar não instala")
toca(f"lj-acao/{k}")
toca("lj-conf-ok")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "1", "a instalação termina bem", 10)
confere(lambda: "pacman -S --noconfirm --needed mesa-utils" in pacman_log(), "rodou pacman -S --noconfirm --needed mesa-utils")
confere(lambda: num("ninst") == ninst + 1, f"mesa-utils passa a instalado ({ninst} → {est('ninst')})")
confere(lambda: num("ag_linhas") >= 2, "o painel recebeu as linhas do pacman")
toca("lj-ag-fechar")
confere(lambda: est("ag_painel") == "0", "fechar o painel")

# 7. uma falha do pacman aparece com o motivo
busca("linux")
confere(lambda: "linux-lts" in lista(), "a busca 'linux' acha os kernels")
k2 = lista().index("linux-lts")
toca(f"lj-acao/{k2}")
toca("lj-conf-ok")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "0", "a falha do pacman é reportada", 10)
toca("lj-ag-fechar")

# 8. o AUR: a busca traz, instalar avisa e usa o paru
busca("code")
confere(lambda: "visual-studio-code-bin" in lista(), "o AUR traz o visual-studio-code-bin", 8)
confere(lambda: "rpc/v5/search/code" in pacman_log(), "consultou o AUR (curl) com o termo")
k3 = lista().index("visual-studio-code-bin")
toca(f"lj-acao/{k3}")
confere(lambda: est("conf") == "1", "instalar do AUR sempre pede confirmação")
toca("lj-conf-ok")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "1", "instala do AUR", 10)
confere(lambda: "paru -S --noconfirm --needed --sudo pkexec --skipreview visual-studio-code-bin" in pacman_log(), "usou o paru com o pkexec")
toca("lj-ag-fechar")

# 9. Instalados: o que foi pedido; as dependências aparecem ao ligar a chave; remover
limpa_busca()
toca("lj-nav/2")
confere(lambda: est("tela") == "2" and set(lista()) >= {"linux", "vlc", "meu-app-aur", "mesa-utils", "visual-studio-code-bin"} and "mesa" not in lista(), f"Instalados: só o pedido ({lista()})")
toca("lj-ver-deps")
confere(lambda: "mesa" in lista() and "htop" in lista(), "a chave mostra também as dependências")
toca("lj-ver-deps")
confere(lambda: "mesa" not in lista() and "vlc" in lista(), "desligar a chave volta só ao pedido")
busca("vlc")   # a lista é alta: a busca traz o vlc para o alto, onde o clique cabe na janela
confere(lambda: lista() == ["vlc"], "a busca em Instalados acha o vlc")
toca("lj-acao/0")
confere(lambda: est("conf") == "1", "Remover pede confirmação")
toca("lj-conf-ok")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "1", "remove", 10)
confere(lambda: "pacman -Rns --noconfirm vlc" in pacman_log(), "rodou pacman -Rns --noconfirm vlc")
confere(lambda: lista() == [], "vlc some de Instalados")
toca("lj-ag-fechar")
limpa_busca()

# 10. Atualizações e Atualizar tudo
toca("lj-nav/3")
confere(lambda: est("tela") == "3" and num("nres") >= 1, f"Atualizações lista o que tem versão nova ({lista()})")
toca("lj-atualizar-tudo")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "1", "Atualizar tudo termina", 10)
confere(lambda: "pacman -Syu --noconfirm" in pacman_log() and "paru -Sua --noconfirm --sudo pkexec" in pacman_log(), "atualizou o sistema e depois o AUR")
confere(lambda: num("natual") == 0 and num("nres") == 0, "não sobra atualização")
toca("lj-ag-fechar")

# 11. Drivers e kernels (a fase 2 ainda não existe: a página diz isso)
toca("lj-nav/4")
confere(lambda: est("tela") == "4", "a página Drivers e kernels abre (aviso da próxima fase)")

# 12. Configurações
toca("lj-nav/5")
confere(lambda: est("tela") == "5" and est("helper") == "[paru]", "Configurações: o paru está em uso")
toca("lj-cfg-aur")
confere(lambda: est("cfg_aur") == "0", "desligar o AUR")
confere(lambda: "aur=false" in le(f"{casa}/.config/lumna/loja/config.ini"), "e grava no config.ini")
toca("lj-cfg-aur")
confere(lambda: est("cfg_aur") == "1", "ligar o AUR de volta")
toca("lj-cfg-confirmar")
confere(lambda: est("cfg_conf") == "0", "desligar a confirmação")

# 13. sem confirmação, o repositório oficial instala direto; o AUR continua perguntando
toca("lj-nav/1")
busca("blender")
confere(lambda: lista() and lista()[0] == "blender", "busca blender")
toca("lj-acao/0")
confere(lambda: est("ag_fim") == "1" and est("ag_ok") == "1" and est("conf") == "0", "sem confirmação, o oficial instala direto", 10)
confere(lambda: "pacman -S --noconfirm --needed blender" in pacman_log(), "rodou pacman -S ... blender")
toca("lj-ag-fechar")
busca("outro-app")
confere(lambda: lista() == ["outro-app-aur"], "o AUR traz o outro-app-aur", 8)
toca("lj-acao/0")
confere(lambda: est("conf") == "1", "o AUR continua perguntando mesmo com a confirmação desligada")
toca("lj-conf-cancelar")
confere(lambda: est("conf") == "0" and "outro-app-aur" not in pacman_log().replace("search", ""), "Cancelar não instala o do AUR")

# 14. Ctrl+K foca a busca de qualquer tela
toca("lj-nav/0")
limpa_busca()
tecla("k", mod="ctrl")
digita("blender")
confere(lambda: est("tela") == "1" and lista() and lista()[0] == "blender", "Ctrl+K e digitar leva a Pacotes com o resultado")

print("loja (app inteiro): ok" if not falhas else f"loja: {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
