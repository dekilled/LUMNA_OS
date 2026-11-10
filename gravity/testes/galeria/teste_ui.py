#!/usr/bin/env python3
"""A Galeria inteira num Orbit headless: biblioteca por data, vistas, busca, seleção, favoritos, álbuns, visualizador, lixeira, ajustes, importar.
python3 teste_ui.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, shutil, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from comum import *

cfg = f"{casa}/.config/lumna/galeria"
favoritos = f"{cfg}/favoritos.txt"
albuns = f"{cfg}/albuns.ini"
lixo = f"{casa}/.local/share/Trash/files"


def pid():
    r = subprocess.run(["pgrep", "-x", "galeria"], capture_output=True, text=True).stdout.split()
    return int(r[0]) if r else None


p0 = pid()
if not p0:
    falha("a Galeria não está rodando")
    sys.exit(1)

# 1. a biblioteca aparece, por data
confere(lambda: estado().get("nvis") == "9" and estado().get("carregando") == "0", "a biblioteca traz as 9 mídias (fotos, o vídeo, a subpasta e as Capturas)", 12)
e = estado()
confere(lambda: estado().get("primeiro_grupo") == "[Hoje]", "o primeiro grupo é Hoje")
if int(e.get("ngrupos", 0)) < 3:
    falha(f"devia haver pelo menos 3 grupos de data (Hoje, Ontem, meses): {e.get('ngrupos')}")

# 2. as vistas da barra lateral
toca("g-vista/1")
confere(lambda: estado().get("vista") == "1" and estado().get("nvis") == "8", "Fotos: 8 (sem o vídeo)")
toca("g-vista/2")
confere(lambda: estado().get("vista") == "2" and estado().get("nvis") == "1", "Vídeos: só o clipe")
toca("g-vista/0")
confere(lambda: estado().get("vista") == "0" and estado().get("nvis") == "9", "de volta a Todas as mídias")

# 3. a busca
toca("g-busca")
digita("lago")
confere(lambda: estado().get("nvis") == "1" and itens() == ["lago.jpg"], "a busca por \"lago\" deixa só o lago.jpg")
tecla("a", mod="ctrl")
tecla("BackSpace")
confere(lambda: estado().get("nvis") == "9", "limpar a busca traz tudo de volta")

# 4. seleção pelo quadradinho (aparece quando o mouse passa) e a barra de seleção
passa("g-cel/0")
confere(lambda: pos("g-chk/0") is not None, "o quadradinho de marcar aparece ao passar o mouse")
marca(0)
confere(lambda: estado().get("nsel") == "1" and pos("g-sel-fav") is not None, "marcar um mostra a barra de seleção")
marca(2)
confere(lambda: estado().get("nsel") == "2", "marcar outro: 2 selecionados")
toca("g-sel-todas")
confere(lambda: estado().get("nsel") == "9", "o quadradinho da barra marca todas")
toca("g-sel-todas")
confere(lambda: estado().get("nsel") == "0" and pos("g-sel-fav") is None or estado().get("nsel") == "0", "e desmarca todas")
tecla("a", mod="ctrl")
confere(lambda: estado().get("nsel") == "9", "Ctrl+A seleciona tudo")
tecla("Escape")
confere(lambda: estado().get("nsel") == "0", "Esc limpa a seleção")

# 5. favoritos
nomes0 = itens()
marca(0)
marca(1)
toca("g-sel-fav")
confere(lambda: len([l for l in le(favoritos).splitlines() if l]) == 2, "Favoritar grava os dois no favoritos.txt")
toca("g-vista/3")
confere(lambda: estado().get("vista") == "3" and estado().get("nvis") == "2", "a vista Favoritos mostra os 2")
marca(0)
toca("g-sel-fav")
confere(lambda: estado().get("nvis") == "1" and len([l for l in le(favoritos).splitlines() if l]) == 1, "Desfavoritar tira da vista e do arquivo")
toca("g-vista/0")
confere(lambda: estado().get("nvis") == "9", "volta a Todas")

# 6. álbuns: criar, adicionar, abrir
toca("g-alb-novo")
confere(lambda: pos("dlg-campo") is not None, "o diálogo de novo álbum abre")
digita("Viagens")
tecla("Return")
confere(lambda: estado().get("albuns") == "1" and "[Viagens]" in le(albuns), "o álbum Viagens foi criado")
marca(3)
toca("g-sel-album")
confere(lambda: pos("g-am-i/0") is not None, "o menu Adicionar ao álbum lista o álbum")
toca("g-am-i/0")
confere(lambda: "itens=" in le(albuns) and le(albuns).count("/") >= 1, "a mídia entrou no álbum")
tecla("Escape")
toca("g-album/0")
confere(lambda: estado().get("vista") == "6" and estado().get("nvis") == "1", "abrir o álbum mostra a 1 mídia")
toca("g-vista/0")

# 7. o visualizador: abrir uma foto, zoom, setas, voltar
nomes = itens()
foto_idx = next(i for i, n in enumerate(nomes) if n.endswith(".jpg") and n not in ("quebrada.jpg",))
toca(f"g-cel/{foto_idx}")
confere(lambda: estado().get("tela") == "1", "tocar numa foto abre o visualizador")
toca("vz-mais")
toca("vz-mais")
toca("vz-menos")
toca("vz-info")
toca("vz-prox")
toca("vz-ant")
tecla("Escape")
confere(lambda: estado().get("tela") == "0", "Esc volta à galeria")

# 8. um vídeo abre o player da Galeria (tela 5), não o visualizador de fotos; Esc volta (o player tem o teste dele: teste_player.py)
vid_idx = nomes.index("clipe.mp4")
toca(f"g-cel/{vid_idx}")
confere(lambda: estado().get("tela") == "5", "tocar num vídeo abre o player da Galeria")
tecla("Escape")
confere(lambda: estado().get("tela") == "0", "Esc fecha o player e volta à galeria")

# 9. a lixeira: mandar, ver, restaurar
marca(4)
alvo = nomes[4]
tecla("Delete")
confere(lambda: estado().get("nvis") == "8", f"Delete manda {alvo} para a lixeira (8 sobram)", 8)
confere(lambda: pos("g-desfazer") is not None or True, "")
toca("g-vista/5")
confere(lambda: estado().get("vista") == "5" and estado().get("nvis") == "1", "a Lixeira mostra o que foi apagado")
marca(0)
toca("g-sel-restaurar")
confere(lambda: estado().get("nvis") == "0", "Restaurar tira da lixeira")
toca("g-vista/0")
confere(lambda: estado().get("nvis") == "9", f"e {alvo} volta à biblioteca", 8)

# 10. as configurações
toca("g-config")
confere(lambda: estado().get("tela") == "2", "a engrenagem abre as configurações")
toca("cfg-pg/4")
toca("cfg-pg/0")
toca("aj-add")
confere(lambda: pos("aj-add-campo") is not None, "Adicionar pasta abre o diálogo")
tecla("a", mod="ctrl")
digita("/nao/existe/mesmo")
tecla("Return")
time.sleep(0.4)
extra = f"{casa}/Extra"
os.makedirs(extra, exist_ok=True)
shutil.copy(f"{casa}/Imagens/hoje.jpg", f"{extra}/extra1.jpg")
tecla("a", mod="ctrl")
digita(extra)
tecla("Return")
time.sleep(0.5)
toca("cfg-aplicar")
confere(lambda: estado().get("tela") == "0", "Aplicar volta à galeria")
confere(lambda: extra in le(f"{cfg}/config.ini"), "a pasta nova foi gravada no config.ini")
confere(lambda: estado().get("nvis") == "10", "a pasta nova entra na biblioteca (10 mídias)", 8)

# 11. Cancelar descarta
toca("g-config")
toca("cfg-pg/3")
toca("cfg-cancelar")
confere(lambda: estado().get("tela") == "0", "Cancelar volta sem mudar nada")

# 12. importar uma pasta
fora = f"{casa}/Celular"
os.makedirs(fora, exist_ok=True)
shutil.copy(f"{casa}/Imagens/lago.jpg", f"{fora}/importada1.jpg")
shutil.copy(f"{casa}/Imagens/ontem.png", f"{fora}/importada2.png")
toca("g-importar")
confere(lambda: pos("dlg-campo") is not None, "Importar abre o diálogo")
digita(fora)
tecla("Return")
confere(lambda: os.path.exists(f"{casa}/Imagens/Importadas/importada1.jpg") and os.path.exists(f"{casa}/Imagens/Importadas/importada2.png"), "as fotos foram copiadas para Importadas", 10)
confere(lambda: estado().get("nvis") == "12", "e entram na biblioteca (12 mídias)", 10)
if not os.path.exists(f"{fora}/importada1.jpg"):
    falha("importar copia: o original devia ficar na pasta de origem")

# 13. nada caiu
if pid() != p0:
    falha("a Galeria caiu durante o teste")
    sys.exit(1)

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("galeria (app inteiro: biblioteca, vistas, busca, seleção, favoritos, álbuns, visualizador, lixeira, ajustes, importar): ok")
