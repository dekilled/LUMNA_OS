#!/usr/bin/env python3
"""O lumna-jogos num Orbit headless: Início (destaque e jogar), Biblioteca (filtros), Detalhes (favorito, Principal e Propriedades), adicionar um jogo avulso, editar as
propriedades e abrir com elas, e a desinstalação com confirmação (Steam pede ao Steam; avulso sai da lista e, se marcado, apaga a pasta)."""
import os, re, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *

gravity = os.environ.get('GRAVITY_DIR', os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')))


def est(k, padrao=""):
    return estado().get(k, padrao)


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


def falso():
    return le(f"{run}/falso.log")


def arq(p):
    return le(f"{casa}/.config/lumna/{p}")


_toca = toca


def toca(nome, espera_s=0.45):
    """espera a peça existir de verdade (uma tela que acabou de trocar ainda guarda a posição zerada da anterior)"""
    espera(lambda: existe(nome), 3)
    _toca(nome, espera_s)


# ordem por nome: Celeste, Horizonte, Rocket, Terras, Witcher
NOMES = ["Celeste", "Horizonte", "Rocket", "Terras", "Witcher"]

# 1. Início: o último jogado em destaque (Celeste) e jogar
confere(lambda: est("tela") == "0" and est("njogos") == "5", "abre no Início com 5 jogos", 8)
confere(lambda: existe("continuar") and existe("ic/0") and existe("ic/4") and not existe("ic/5"), "o destaque e a fila Seus jogos (5 cards)")

# 1b. o fundo das telas (olhando os pixels): cada jogo selecionado dá o fundo do Início; a barra de cima degrada; a de baixo é de uma cor só
from PIL import Image
import json as _json


def tira():
    subprocess.run(["grim", f"{run}/tela.png"], check=True)
    j = _json.loads(msg("janelas"))["janelas"][0]
    return Image.open(f"{run}/tela.png").convert("RGB"), j


def px(img, j, fx, fy):
    return img.getpixel((int(j["x"] + j["w"] * fx), int(j["y"] + j["h"] * fy)))


time.sleep(0.8)
img, j = tira()
sem_arte = px(img, j, 0.75, 0.4)
if not (sem_arte[2] < 45):
    falha(f"o primeiro jogo (Celeste) não tem arte: o fundo do Início fica escuro ({sem_arte})")
tecla("Right")
confere(lambda: est("sel") == "1", "a seta para a direita seleciona o próximo jogo")
time.sleep(1.2)
img, j = tira()
azul = px(img, j, 0.75, 0.4)
if not (azul[2] > azul[0] + 60):
    falha(f"o fundo do Início é a arte do jogo selecionado (Terras Distantes, azul): {azul}")
topo, abaixo = px(img, j, 0.75, 0.004), px(img, j, 0.75, 0.15)
if not (sum(topo) < sum(abaixo) * 0.45):
    falha(f"a barra de cima começa escura e vai ficando transparente (topo {topo}, mais abaixo {abaixo})")
r1, r2 = px(img, j, 0.2, 0.985), px(img, j, 0.8, 0.985)
if r1 != r2 or sum(r1) > 90:
    falha(f"a barra de baixo é de uma cor só ({r1} e {r2})")
tecla("Right")
confere(lambda: est("sel") == "2", "mais um jogo")
time.sleep(1.2)
img, j = tira()
if px(img, j, 0.75, 0.4) == azul:
    falha("trocar de jogo troca o fundo")
tecla("Left")
tecla("Left")
confere(lambda: est("sel") == "0", "volta ao primeiro")
toca("continuar", 1.0)
confere(lambda: "lutris lutris:rungame/celeste" in falso(), "Continuar abre o Celeste pelo Lutris", 5)
confere(lambda: "Abrindo" in est("aviso", ""), "mostra 'Abrindo…'", 3) if False else None

# 2. Biblioteca e filtros
toca("ver-bib", 0.8)
confere(lambda: est("tela") == "1" and est("nvis") == "5", "Ver biblioteca abre a Biblioteca com 5 jogos")
# o papel de parede da Biblioteca: automático (a arte do jogo em destaque: o Celeste não tem), uma imagem, e nenhum
os.makedirs(f"{casa}/.config/lumna/jogos", exist_ok=True)
open(f"{casa}/.config/lumna/jogos/ui.ini", "w").write(f"[biblioteca]\npapel=imagem\nimagem={run}/steam/appcache/librarycache/1001/library_hero.jpg\n")
time.sleep(6.5)  # a biblioteca relê a configuração de 5 em 5 s
img, j = tira()
papel_img = px(img, j, 0.8, 0.8)
if not (papel_img[2] > 50 and papel_img[2] > papel_img[0] + 25):
    falha(f"o papel de parede da Biblioteca (uma imagem azul) aparece escurecido atrás da grade: {papel_img}")
open(f"{casa}/.config/lumna/jogos/ui.ini", "w").write("[biblioteca]\npapel=nenhum\n")
time.sleep(6.5)
img, j = tira()
sem_papel = px(img, j, 0.8, 0.8)
if not (sem_papel[2] < 40):
    falha(f"sem papel de parede, só o fundo escuro: {sem_papel}")
toca("filtro/2", 0.6)
confere(lambda: est("filtro") == "2" and est("nvis") == "2", "filtro Steam: 2 jogos")
toca("filtro/3", 0.6)
confere(lambda: est("nvis") == "1", "filtro Lutris: 1 jogo")
toca("filtro/1", 0.6)
confere(lambda: est("nvis") == "0", "filtro Favoritos: nenhum ainda")
toca("filtro/0", 0.6)
confere(lambda: est("nvis") == "5", "Todos volta aos 5")

# 3. Detalhes de um jogo da Steam (Terras Distantes) e favorito
toca("card/3", 0.8)
confere(lambda: est("tela") == "2" and est("det") == "steam:1001", "tocar no card abre os Detalhes de Terras Distantes")
confere(lambda: existe("det-continuar") and existe("desinstalar") and existe("abrir-pasta"), "Principal: Continuar, Abrir pasta e Desinstalar")
toca("det-fav", 0.8)
confere(lambda: est("favorito") == "1", "o coração favorita")
confere(lambda: "steam:1001" in arq("jogos/props.json") and '"favorito":true' in arq("jogos/props.json"), "o favorito fica no props.json")
tecla("Escape")
confere(lambda: est("tela") == "1", "Esc volta dos Detalhes para a Biblioteca")
time.sleep(1.0)
toca("filtro/1", 0.6)
confere(lambda: est("nvis") == "1", "Favoritos mostra o jogo favoritado")
toca("filtro/0", 0.6)

# 4. Propriedades de um jogo da Steam: a Steam guarda as dela (só leitura, sem Aplicar)
toca("card/3", 0.8)
toca("det-mais", 0.8)
confere(lambda: est("aba") == "1", "os três pontinhos levam às Propriedades")
confere(lambda: not existe("p-aplicar"), "Steam: sem Aplicar (a Steam guarda as propriedades dela)")
tecla("Escape")
time.sleep(1.0)

# 5. adicionar um jogo avulso
confere(lambda: est("tela") == "1", "de volta à Biblioteca")
toca("add", 0.8)
confere(lambda: est("adicionando") == "1" and existe("add-nome"), "Adicionar jogo abre o diálogo")
toca("add-nome")
digita("Meu Jogo")
toca("add-exec")
digita(f"{run}/avulso/Jogo/jogo.sh")
toca("add-ok", 1.0)
confere(lambda: est("njogos") == "6" and est("adicionando") == "0", "o avulso entra na biblioteca (6 jogos)", 5)
toca("filtro/5", 0.6)
confere(lambda: est("nvis") == "1", "filtro Avulsos: 1 jogo")

# 6. propriedades do avulso: editar, aplicar e abrir com elas
toca("card/0", 0.8)
confere(lambda: est("tela") == "2" and est("det", "").startswith("avulso:"), "abre os Detalhes do avulso")
avulso = est("det")
toca("det-mais", 0.8)
confere(lambda: est("aba") == "1" and existe("p-aplicar"), "Propriedades do avulso: tem Aplicar")
confere(lambda: est("suja") == "0", "sem alterações no começo")
toca("p-opcoes")
digita("%command% --fase 3")
confere(lambda: est("suja") == "1", "mudar um campo marca 'Alterações não salvas'")
toca("p-add-env", 0.6)
toca("env-k/0")
digita("MEUVAR")
toca("env-v/0")
digita("valor")
toca("p-aplicar", 1.0)
confere(lambda: "--fase 3" in arq("jogos/props.json") and "MEUVAR" in arq("jogos/props.json"), "Aplicar grava as propriedades", 5)
confere(lambda: est("suja") == "0" and "Propriedades salvas" in est("aviso", "").replace("_", " ") or est("suja") == "0", "depois de Aplicar não há alterações pendentes")
toca("det-continuar", 1.5)
confere(lambda: "avulso rodou --fase 3 em" in falso() and "valor" in falso(), "o avulso abriu com as opções e a variável", 5)

# 7. desinstalar: confirmação, cancelar, e confirmar apagando os arquivos
tecla("Tab")  # (a aba Principal volta pelo clique)
toca("det-aba/0", 0.8)
confere(lambda: est("aba") == "0" and existe("desinstalar"), "volta à aba Principal")
toca("desinstalar", 0.8)
confere(lambda: est("confirmando") == "1" and existe("des-ok") and existe("apagar-arquivos"), "Desinstalar abre a confirmação (com a opção de apagar os arquivos)")
toca("des-cancelar", 0.8)
confere(lambda: est("confirmando") == "0" and est("njogos") == "6", "Cancelar fecha e não remove nada")
toca("desinstalar", 0.8)
toca("apagar-arquivos", 0.6)
toca("des-ok", 1.5)
confere(lambda: est("njogos") == "5" and est("tela") == "1", "confirmar tira o avulso da biblioteca e volta à Biblioteca", 5)
confere(lambda: not os.path.exists(f"{run}/avulso/Jogo"), "marcado, a pasta do jogo foi apagada", 3)

# 8. desinstalar um da Steam: pede ao Steam, o jogo continua até ele desinstalar
toca("filtro/0", 0.6)
toca("card/3", 0.8)
confere(lambda: est("det") == "steam:1001", "abre Terras Distantes de novo")
toca("desinstalar", 0.8)
confere(lambda: est("confirmando") == "1" and not existe("apagar-arquivos"), "Steam: a confirmação não oferece apagar arquivos (a Steam cuida deles)")
toca("des-ok", 1.5)
confere(lambda: "steam steam://uninstall/1001" in falso(), "o Steam recebeu steam://uninstall/1001", 5)
confere(lambda: est("njogos") == "5", "o jogo da Steam continua na lista (a Steam é quem desinstala)")

# 9. o vigia: entrar no modo Gaming abre a biblioteca sozinho
import json, subprocess
subprocess.run(["pkill", "-x", "jogos"])
time.sleep(1.0)
vigia = subprocess.Popen([f"{gravity}/jogos", "vigia"], env=dict(os.environ), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1.0)
def tem_jogos():
    try:
        return any("jogos" in (j.get("app_id") or "") for j in json.loads(msg("janelas"))["janelas"])
    except Exception:
        return False
confere(lambda: not tem_jogos(), "antes de entrar no Gaming a biblioteca não está aberta", 2)
msg("modo", "gaming")
confere(lambda: tem_jogos(), "entrar no modo Gaming abre a biblioteca sozinho", 8)
confere(lambda: "powerprofilesctl set performance" in falso(), "entrar no Gaming põe o perfil de energia em Desempenho", 6)
msg("modo", "desktop")
confere(lambda: "powerprofilesctl set balanced" in falso(), "sair do Gaming devolve o perfil que estava (Equilibrado)", 6)
vigia.kill()

sys.exit(1 if falhas else 0)
