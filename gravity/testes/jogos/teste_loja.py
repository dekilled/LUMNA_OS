#!/usr/bin/env python3
"""A Loja do Gaming: Destaques (do catálogo real gravado), gêneros, subabas, Buscar com filtros, a página do jogo, Comprar (abre a loja da Steam), Lista de desejos
(local), instalar (a Steam sobe sem janela, local de instalação, progresso do download, instalado, desinstalar) e a chave da API (Configurações)."""
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *


def est(k, padrao=""):
    return estado().get(k, padrao)


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


_toca = toca


def toca(nome, espera_s=0.45):
    espera(lambda: existe(nome), 4)
    _toca(nome, espera_s)


def falso():
    return le(f"{run}/falso.log")


def pedidos():
    return le(f"{run}/pedidos.log")


def cfg(p):
    return le(f"{casa}/.config/lumna/{p}")


def manifesto(appid, flags, a_baixar=0, baixado=0):
    pasta = f"{run}/steam/steamapps"
    open(f"{pasta}/appmanifest_{appid}.acf", "w").write(
        '"AppState"\n{\n\t"appid"\t\t"%d"\n\t"name"\t\t"Portal 2"\n\t"StateFlags"\t\t"%d"\n\t"installdir"\t\t"Portal 2"\n\t"SizeOnDisk"\t\t"1000"\n\t"buildid"\t\t"1"\n\t"BytesToDownload"\t\t"%d"\n\t"BytesDownloaded"\t\t"%d"\n}\n'
        % (appid, flags, a_baixar, baixado))


# 1. Destaques: o catálogo real carrega; o primeiro jogo manda no centro
confere(lambda: est("tela") == "4" and int(est("lndest", "0")) >= 4, "a Loja abre nos Destaques com os mais vendidos do catálogo", 10)
confere(lambda: existe("l-ver"), "o hero tem 'Ver jogo'")
tecla("Right")
confere(lambda: est("lsel") == "1", "a seta para a direita seleciona o próximo jogo da fila")
tecla("Left")
confere(lambda: est("lsel") == "0", "e para a esquerda volta")

# 2. gêneros: o chip Ação refaz a lista pelo gênero (tag 19)
toca("l-chip/1", 1.5)
confere(lambda: "tags=19" in pedidos() and "filter=topsellers" in pedidos(), "o chip Ação pede o gênero Ação, os mais vendidos", 6)
confere(lambda: int(est("lndest", "0")) >= 4, "e a fila continua com jogos")
toca("l-chip/0", 1.2)

# 3. subabas: Categorias, Ofertas, Lista de desejos
toca("l-subabas/2", 1.5)
confere(lambda: est("lsub") == "2", "a subaba Ofertas")
toca("l-subabas/1", 0.8)
confere(lambda: est("lsub") == "1", "a subaba Categorias")
toca("l-subabas/3", 0.8)
confere(lambda: est("lsub") == "3" and est("ldes") == "0", "a Lista de desejos começa vazia")
toca("l-subabas/0", 0.8)

# 4. Buscar: por texto e com filtros
toca("l-busca")
digita("portal")
tecla("Return")
confere(lambda: est("tela") == "5", "Enter na busca abre a tela Buscar", 5)
confere(lambda: "term=portal" in pedidos() and int(est("lnres", "0")) >= 5 and int(est("ltotal", "0")) > 100, "a busca por 'portal' devolve resultados e o total", 8)
toca("b-linux", 1.5)
confere(lambda: "os=linux" in pedidos(), "o filtro Linux nativo vira os=linux", 6)
toca("b-gratis", 1.5)
confere(lambda: "maxprice=free" in pedidos(), "o filtro Só grátis vira maxprice=free", 6)
toca("b-gen/3", 1.5)
confere(lambda: "tags=122" in pedidos(), "o gênero RPG vira tags=122", 6)

# 5. a página de um jogo (Portal 2, dados reais gravados)
tecla("Escape")
confere(lambda: est("tela") == "4", "Esc volta da busca para a Loja")
os.system(f"pkill -x jogos")
time.sleep(1.0)
import subprocess
env = dict(os.environ, LUMNA_JOGOS_DEBUG="1")
subprocess.Popen([f"{sys.argv[4] if len(sys.argv) > 4 else os.path.dirname(os.path.abspath(__file__)) + '/../../'}jogos", "--loja-jogo", "620"], stdout=open(f"{run}/g.out", "w"), stderr=subprocess.DEVNULL, env=env)
confere(lambda: est("tela") == "6" and est("ldet") == "1", "a página do jogo carrega os detalhes do Portal 2", 10)
confere(lambda: existe("lj-comprar") and existe("lj-desejo") and not existe("lj-instalar"), "um jogo que a conta não tem: Comprar e Lista de desejos, sem Instalar")
toca("lj-desejo", 1.0)
confere(lambda: '"id":620' in cfg("jogos/desejos.json"), "a Lista de desejos guarda o jogo (localmente)")
toca("lj-comprar", 1.0)
confere(lambda: "steam steam://store/620" in falso(), "Comprar abre a loja da Steam (steam://store)", 5)
toca("lj-desejo", 1.0)
confere(lambda: '"id":620' not in cfg("jogos/desejos.json"), "tocar de novo tira da lista")

# 6. a chave da API: Configurações > Steam
toca("engrenagem", 1.0)
toca("cfg-pg/4", 0.8)
toca("st-chave")
digita("curta")
toca("st-salvar", 0.8)
confere(lambda: not os.path.exists(f"{casa}/.config/lumna/jogos/steam.ini"), "uma chave curta é recusada")
toca("st-chave")
tecla("a", mod="ctrl")
digita("ABCDEF0123456789ABCDEF0123456789")
toca("st-id")
digita("76561198000000000")
toca("st-salvar", 1.0)
confere(lambda: os.path.exists(f"{casa}/.config/lumna/jogos/steam.ini") and (os.stat(f"{casa}/.config/lumna/jogos/steam.ini").st_mode & 0o777) == 0o600, "a chave certa é guardada, com permissão 600", 5)
confere(lambda: "ABCDEF0123456789" not in open(f"{run}/g.out", errors="replace").read(), "a chave nunca aparece na saída do app")

# 7. com a chave, o jogo da conta vira "Na sua biblioteca" com Instalar (reabre a página)
os.system("pkill -x jogos")
time.sleep(1.0)
subprocess.Popen([f"{sys.argv[4] if len(sys.argv) > 4 else os.path.dirname(os.path.abspath(__file__)) + '/../../'}jogos", "--loja-jogo", "620"], stdout=open(f"{run}/g.out", "w"), stderr=subprocess.DEVNULL, env=env)
confere(lambda: est("ldet") == "1" and int(est("lpossui", "0")) == 2, "a conta tem 2 jogos (a chave funciona)", 10)
confere(lambda: existe("lj-instalar") and not existe("lj-comprar"), "o jogo da conta mostra Instalar (e não Comprar)", 6)
toca("lj-instalar", 1.0)
confere(lambda: est("ldlg") == "1" and existe("li-ok") and existe("li-local"), "Instalar abre a janela de instalação com o local")
toca("li-nao", 0.8)
confere(lambda: est("ldlg") == "0", "Agora não fecha sem instalar")
toca("lj-instalar", 1.0)
toca("li-ok", 2.8)
confere(lambda: "steam -silent -nochatui -nofriendsui" in falso() and "steam steam://install/620" in falso(), "Instalar sobe a Steam sem janela e pede steam://install/620", 8)
# o download corre: a Steam escreve o manifesto e a barra anda
manifesto(620, 1024, 10000000000, 2500000000)
confere(lambda: est("lprog", "").startswith("1,25"), "o progresso do download aparece (25%)", 8)
confere(lambda: int(est("lbaix", "0")) >= 1, "o download entra na conta do ícone de downloads", 5)
manifesto(620, 1024, 10000000000, 7500000000)
confere(lambda: est("lprog", "").startswith("1,75"), "e anda (75%)", 8)
manifesto(620, 4, 0, 0)
confere(lambda: est("lprog", "").startswith("0,100") and existe("lj-desinstalar"), "pronto: Instalado, com Desinstalar", 8)
toca("lj-desinstalar", 1.5)
confere(lambda: "steam steam://uninstall/620" in falso() or "steam steam://uninstall/" in falso(), "Desinstalar pede à Steam (steam://uninstall)", 8)

# 8. a GOG, no mesmo catálogo: os destaques misturam as duas lojas; a página de um jogo da GOG; o login; instalar pelo gogdl
os.system("pkill -x jogos")
time.sleep(1.0)
subprocess.Popen([f"{sys.argv[4]}jogos", "--tela", "loja"], stdout=open(f"{run}/g.out", "w"), stderr=subprocess.DEVNULL, env=env)
confere(lambda: est("tela") == "4" and int(est("lndest", "0")) >= 4 and int(est("lgog", "0")) >= 1, "os destaques misturam a Steam e a GOG (catálogo unificado)", 10)
os.system("pkill -x jogos")
time.sleep(1.0)
subprocess.Popen([f"{sys.argv[4]}jogos", "--loja-jogo", "-1207658930"], stdout=open(f"{run}/g.out", "w"), stderr=subprocess.DEVNULL, env=env)
confere(lambda: est("tela") == "6" and est("ldet") == "1", "a página de um jogo da GOG (The Witcher 2) carrega", 10)
confere(lambda: existe("lj-comprar") and not existe("lj-instalar"), "sem login: Comprar (e não Instalar)")
toca("lj-comprar", 1.0)
confere(lambda: "xdg-open " in falso() and "gog.com" in falso(), "Comprar abre a página do jogo na GOG (no navegador)", 5)
toca("engrenagem", 1.0)
toca("cfg-pg/5", 0.8)
confere(lambda: est("pg") == "5" and existe("gg-abrir") and est("lgogin") == "0", "Configurações > GOG: sem login")
toca("gg-abrir", 1.0)
confere(lambda: "xdg-open https://auth.gog.com/auth?client_id=" in falso(), "a página de login da GOG abre no navegador", 5)
toca("gg-codigo")
digita("lixo")
toca("gg-entrar", 1.0)
confere(lambda: est("lgogin") == "0" and not os.path.exists(f"{casa}/.config/lumna/jogos/gog/auth.json"), "um código que não é da GOG é recusado")
toca("gg-codigo")
tecla("a", mod="ctrl")
digita("https://embed.gog.com/on_login_success?origin=client&code=ABCDEFGHIJ1234567")
toca("gg-entrar", 2.0)
confere(lambda: est("lgogin") == "1" and os.path.exists(f"{casa}/.config/lumna/jogos/gog/auth.json"), "a URL colada vira o login (o gogdl guardou)", 6)
confere(lambda: "gogdl --auth-config-path" in falso() and "auth --code ABCDEFGHIJ1234567" in falso(), "o gogdl recebeu só o código")
confere(lambda: (os.stat(f"{casa}/.config/lumna/jogos/gog/auth.json").st_mode & 0o777) == 0o600, "o login fica num arquivo 600")
# com o login, o jogo da conta mostra Instalar
os.system("pkill -x jogos")
time.sleep(1.0)
subprocess.Popen([f"{sys.argv[4]}jogos", "--loja-jogo", "-1207658930"], stdout=open(f"{run}/g.out", "w"), stderr=subprocess.DEVNULL, env=env)
confere(lambda: est("ldet") == "1" and int(est("lgpossui", "0")) == 2, "a conta da GOG tem 2 jogos", 10)
confere(lambda: existe("lj-instalar") and not existe("lj-comprar"), "o jogo da conta mostra Instalar")
toca("lj-instalar", 1.0)
confere(lambda: est("ldlg") == "1" and existe("li-pasta") and existe("li-ok"), "a janela de instalação da GOG pede a pasta")
toca("li-pasta")
tecla("a", mod="ctrl")
digita(f"{run}/JogosGOG")
toca("li-ok", 2.0)
confere(lambda: "download 1207658930 --platform linux --path " in falso() and "JogosGOG" in falso(), "Instalar chama o gogdl download (Linux nativo) na pasta escolhida", 8)
confere(lambda: est("lgprog", "").startswith("1,") , "o download em curso aparece na página do jogo", 10)
confere(lambda: int(est("lbaix", "0")) >= 1, "e no contador de downloads da barra", 5)
confere(lambda: est("lgprog", "").startswith("0,") and existe("lj-desinstalar"), "pronto: Instalado, com Desinstalar", 15)
confere(lambda: os.path.exists(f"{casa}/.config/lumna/jogos/gog_instalados.json"), "o jogo foi registrado")
confere(lambda: any(os.path.exists(f"{run}/JogosGOG/{d}/start.sh") for d in os.listdir(f"{run}/JogosGOG")) if os.path.isdir(f"{run}/JogosGOG") else False, "o jogo foi para a pasta escolhida")
toca("lj-jogar", 1.5)
confere(lambda: est("tela") == "2" and est("det", "").startswith("gog:"), "'Ver na biblioteca' abre o jogo da GOG nos Detalhes", 6)
toca("det-continuar", 1.5)
confere(lambda: "gog jogo rodou" in falso(), "e Continuar abre o jogo (o start.sh)", 6)
# desinstalar pela Loja: dois toques, o segundo apaga a pasta
tecla("Escape")
confere(lambda: est("tela") == "1", "volta à Biblioteca")
toca("filtro/6", 0.8)
confere(lambda: est("filtro") == "6" and est("nvis") == "1", "o filtro GOG da Biblioteca mostra o jogo instalado")

# 9. a Epic: a subaba (promoções reais gravadas), resgatar, o login pelo legendary, a biblioteca, instalar, abrir e desinstalar
def reabre(*args):
    os.system("pkill -x jogos")
    time.sleep(1.0)
    subprocess.Popen([f"{sys.argv[4]}jogos", *args], stdout=open(f"{run}/g.out", "w"), stderr=subprocess.DEVNULL, env=env)


reabre("--tela", "loja")
confere(lambda: est("tela") == "4", "a Loja abre", 10)
toca("l-subabas/4", 2.0)
confere(lambda: est("lsub") == "4" and int(est("lepic", "0,0").split(",")[1]) >= 8 and est("lepic").split(",")[0] == "0", "a subaba Epic mostra as promoções da Epic (sem login, a biblioteca vazia)", 10)
reabre("--loja-epic", "a9748abde1c94b66aae5250bb9fc5503")
confere(lambda: est("tela") == "6" and est("ldet") == "1", "a página de um jogo grátis da Epic (Idle Champions) carrega", 10)
confere(lambda: existe("lj-comprar") and not existe("lj-instalar"), "um jogo que ainda não é seu: Resgatar grátis (não Instalar)")
toca("lj-comprar", 1.0)
confere(lambda: "xdg-open https://store.epicgames.com/pt-BR/p/idle-champions" in falso(), "Resgatar abre a página do jogo na loja da Epic", 5)
toca("engrenagem", 1.0)
toca("cfg-pg/6", 0.8)
confere(lambda: est("pg") == "6" and existe("ep-abrir") and est("lepicin") == "0", "Configurações > Epic: sem login")
toca("ep-abrir", 1.0)
confere(lambda: "xdg-open https://legendary.gl/epiclogin" in falso(), "a página de login abre no navegador", 5)
toca("ep-codigo")
digita("lixo")
toca("ep-entrar", 1.0)
confere(lambda: est("lepicin") == "0" and not os.path.exists(f"{casa}/.config/lumna/jogos/epic/user.json"), "um texto que não é da Epic é recusado")
toca("ep-codigo")
tecla("a", mod="ctrl")
digita('{"authorizationCode":"abcdef0123456789abcdef0123456789"}')
toca("ep-entrar", 2.0)
confere(lambda: est("lepicin") == "1" and os.path.exists(f"{casa}/.config/lumna/jogos/epic/user.json"), "o texto colado vira o login (o legendary guardou)", 6)
confere(lambda: "legendary auth --code abcdef0123456789abcdef0123456789" in falso(), "o legendary recebeu só o código")
reabre("--loja-epic", "Sugar:lib")
confere(lambda: est("tela") == "6" and est("ldet") == "1", "a página de um jogo da biblioteca (Rocket League) carrega", 10)
confere(lambda: existe("lj-instalar") and not existe("lj-comprar"), "um jogo seu: Instalar")
toca("lj-instalar", 1.0)
confere(lambda: est("ldlg") == "1" and existe("li-pasta"), "a janela de instalação pede a pasta")
toca("li-pasta")
tecla("a", mod="ctrl")
digita(f"{run}/JogosEpic")
toca("li-ok", 2.0)
confere(lambda: "legendary install Sugar --base-path " in falso() and "JogosEpic" in falso(), "Instalar chama o legendary install com a pasta escolhida", 8)
confere(lambda: est("leprog", "").startswith("1,"), "o download em curso aparece na página do jogo", 10)
confere(lambda: est("leprog", "").startswith("0,") and existe("lj-desinstalar"), "pronto: Instalado, com Desinstalar", 15)
toca("lj-jogar", 1.5)
confere(lambda: est("tela") == "2" and est("det", "") == "epic:Sugar", "'Ver na biblioteca' abre o jogo da Epic nos Detalhes", 6)
toca("det-continuar", 2.0)
confere(lambda: "legendary launch Sugar" in falso() and "epic jogo rodou" in falso(), "Continuar abre pelo legendary launch", 8)

sys.exit(1 if falhas else 0)
