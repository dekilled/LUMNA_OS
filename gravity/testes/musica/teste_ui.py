#!/usr/bin/env python3
"""O LUMNA Music inteiro num Orbit headless, com o mpv de verdade (saída nula) e um D-Bus de sessão: biblioteca, álbuns, tocar, fila, favoritas, playlists,
busca, letras, configurações, MPRIS e recomeçar de onde parou.  python3 teste_ui.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, shutil, subprocess, sys, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *

cfgdir = f"{casa}/.config/lumna/musica"
musicas = f"{casa}/Músicas"


def num(k):
    return float(estado().get(k, "nan"))


def e(k):
    return estado().get(k)


def pid():
    r = subprocess.run(["pgrep", "-x", "musica"], capture_output=True, text=True).stdout.split()
    return int(r[0]) if r else None


def gd(*args):
    return subprocess.run(["gdbus", "call", "--session", "--dest", "org.mpris.MediaPlayer2.lumna_music", "--object-path", "/org/mpris/MediaPlayer2", "--method", *args],
                          capture_output=True, text=True).stdout


def mpris_prop(prop):
    return gd("org.freedesktop.DBus.Properties.Get", "org.mpris.MediaPlayer2.Player", prop)


def mpris_cmd(m, *a):
    return gd(f"org.mpris.MediaPlayer2.Player.{m}", *a)


def clica_em(nome, fx=0.5, fy=0.5):
    b = pos(nome)
    ox, oy = origem()
    x, y = ox + b[0] + int(b[2] * fx), oy + b[1] + int(b[3] * fy)
    aponta(x, y)
    msg("toque", "baixo", 1, x, y)
    time.sleep(0.08)
    msg("toque", "cima", 1)
    time.sleep(0.45)


def arrasta(x1, y1, x2, y2, passos=10):
    aponta(x1, y1)
    time.sleep(0.2)
    msg("mouse", "botao", "baixo")
    time.sleep(0.25)
    for i in range(1, passos + 1):
        msg("mouse", "mover", int(x1 + (x2 - x1) * i / passos), int(y1 + (y2 - y1) * i / passos))
        time.sleep(0.08)
    time.sleep(0.3)
    msg("mouse", "botao", "cima")
    time.sleep(0.7)


def centro_de(nome):
    b = pos(nome)
    ox, oy = origem()
    return ox + b[0] + b[2] // 2, oy + b[1] + b[3] // 2


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


def rola(dy, x=None):
    """arrasta o dedo na área de conteúdo para rolar (dy < 0 rola para baixo)"""
    ox, oy = origem()
    x = ox + 700 if x is None else x
    y = oy + 400
    msg("toque", "baixo", 1, x, y)
    for i in range(1, 9):
        time.sleep(0.04)
        msg("toque", "move", 1, x, int(y + dy * i / 8))
    time.sleep(0.15)
    msg("toque", "cima", 1)
    time.sleep(0.6)


def le_ini():
    return le(f"{cfgdir}/config.ini")


def toca_faixa_pelo_nome(termo, titulo):
    """busca pelo termo e toca o primeiro resultado"""
    toca("mu-busca")
    tecla("a", mod="ctrl")
    digita(termo)
    confere(lambda: e("vista") == "6" and int(e("nbusca")) >= 1, f"a busca por '{termo}' acha algo")
    toca("mu-lin/0")
    confere(lambda: e("tocando") == "1" and e("titulo") == titulo.replace(" ", "_"), f"tocou {titulo} ({e('titulo')})", 8)
    tecla("a", mod="ctrl")
    tecla("BackSpace")


if not pid():
    falha("o LUMNA Music não está rodando")
    sys.exit(1)

# ===== 1. a biblioteca aparece =====
confere(lambda: e("carregando") == "0" and e("nfaixas") == "9", "a biblioteca traz as 9 músicas", 40)
confere(lambda: e("nalbuns") == "5" and e("nartistas") == "6" and e("npastas") == "5", f"5 álbuns, 6 artistas, 5 pastas ({e('nalbuns')}, {e('nartistas')}, {e('npastas')})")
confere(lambda: e("nplaylists") == "1" and e("mpris") == "1" and e("vista") == "0", "só a Favoritas, MPRIS no ar, abre em Ouvir agora")
confere(lambda: e("nfila") == "0" and e("tocando") == "0", "nada tocando")

# ===== 2. a barra lateral =====
for i, v in enumerate(["0", "1", "2", "3", "4"]):
    toca(f"mu-nav/{i}")
    confere(lambda v=v: e("vista") == v, f"o item {i} da barra lateral abre a vista {v}")

# ===== 3. um álbum: abrir, reproduzir =====
toca("mu-nav/2")
toca("mu-album/2")  # Horizonte violeta (ordem por título: Álbum desconhecido, Coletânea, Horizonte violeta, ...)
confere(lambda: e("vista") == "5" and e("det_tipo") == "0" and e("det_n") == "2", f"o álbum abre com 2 faixas ({e('det_n')})")
toca("mu-reproduzir")
confere(lambda: e("tocando") == "1" and e("titulo") == "Entre_órbitas" and e("nfila") == "2" and e("atual") == "0", f"Reproduzir toca o álbum desde a primeira ({e('titulo')})", 10)
confere(lambda: abs(num("dur") - 3) < 0.5, f"duração de uns 3 s ({e('dur')})")
confere(lambda: "Playing" in mpris_prop("PlaybackStatus"), "o MPRIS diz Playing")
confere(lambda: "Entre órbitas" in mpris_prop("Metadata") and "LUMNA Sessions" in mpris_prop("Metadata"), "o MPRIS tem o título e o artista")
p1 = num("pos")
time.sleep(0.6)
confere(lambda: num("pos") > p1 + 0.3 or e("tocando") == "0", "o tempo anda")

# ===== 4. os controles =====
toca("mu-play")
confere(lambda: e("tocando") == "0", "Pausar")
confere(lambda: "Paused" in mpris_prop("PlaybackStatus"), "o MPRIS diz Paused")
toca("mu-play")
confere(lambda: e("tocando") == "1", "Tocar de novo")
toca("mu-prox")
confere(lambda: e("atual") == "1" and e("titulo") == "Maré_noturna", f"Próxima ({e('titulo')})", 6)
toca("mu-ant")
confere(lambda: e("atual") == "0" and e("titulo") == "Entre_órbitas", "Anterior (com menos de 3 s, volta uma faixa)", 6)
clica_em("mu-seek", 0.7)
confere(lambda: num("pos") > 1.4, f"clicar na barra de tempo busca (pos={e('pos')})", 6)

# ===== 5. aleatório e repetir =====
toca("mu-aleatorio")
confere(lambda: e("aleatorio") == "1", "Aleatório liga")
toca("mu-aleatorio")
confere(lambda: e("aleatorio") == "0", "e desliga")
toca("mu-repetir")
confere(lambda: e("repetir") == "1", "Repetir: a fila")
toca("mu-repetir")
confere(lambda: e("repetir") == "2", "Repetir: a faixa")
toca("mu-repetir")
confere(lambda: e("repetir") == "0", "Repetir: nada")
confere(lambda: "aleatorio=true" in le_ini().lower() or "aleatorio=false" in le_ini().lower(), "as escolhas vão para o config.ini")

# ===== 6. volume =====
clica_em("mu-volume", 0.3)
confere(lambda: 15 <= num("volume") <= 45, f"o volume (clicar em 30%) = {e('volume')}")
toca("mu-vol-icone")
confere(lambda: e("volume") == "0", "o ícone do volume silencia")
toca("mu-vol-icone")
confere(lambda: int(e("volume")) > 0, "e volta ao que era")

# ===== 7. repetir a faixa e emendar sozinho (gapless) =====
# (o álbum de 2 faixas já acabou sozinho: tocar recomeça a faixa da fila)
if e("tocando") == "0":
    toca("mu-play")
confere(lambda: e("tocando") == "1", "tocar de novo depois do fim da fila", 8)
toca("mu-repetir")
toca("mu-repetir")  # a faixa
clica_em("mu-seek", 0.02)  # volta ao começo
time.sleep(0.5)
estado_antes = e("atual")
clica_em("mu-seek", 0.9)
time.sleep(1.6)
confere(lambda: e("atual") == estado_antes and e("tocando") == "1", f"com 'repetir a faixa' ela recomeça sozinha no fim (atual={e('atual')} antes={estado_antes} tocando={e('tocando')} repetir={e('repetir')} pos={e('pos')} vivo={e('vivo')} carregada={e('carregada')} fila={e('fila')})", 6)
toca("mu-repetir")  # nada
confere(lambda: e("repetir") == "0", "repetir desligado")
# o álbum todo, sem repetir: acaba e para
toca("mu-nav/2")
clica_em("mu-album/4", 0.5, 0.08)  # Luz distante: 3 faixas (a linha de baixo da grade fica meio escondida: clica na capa de cima)
toca("mu-reproduzir")
confere(lambda: e("nfila") == "3" and e("tocando") == "1" and e("atual") == "0", "tocando o álbum de 3 faixas")
confere(lambda: e("atual") == "1", "a segunda entra sozinha", 9)
confere(lambda: e("atual") == "2", "e a terceira", 9)
confere(lambda: e("tocando") == "0", "no fim da fila (sem repetir) para", 9)
confere(lambda: e("atual") == "2", "e fica na última")
toca("mu-play")
confere(lambda: e("tocando") == "1", "Tocar no fim recomeça a faixa", 6)
# repetir a fila: do fim volta ao começo
toca("mu-repetir")
confere(lambda: e("repetir") == "1", "repetir a fila")
clica_em("mu-seek", 0.93)
confere(lambda: e("atual") == "0" and e("tocando") == "1", "no fim da última, volta à primeira", 9)
toca("mu-repetir")
toca("mu-repetir")
confere(lambda: e("repetir") == "0", "repetir desligado de novo")

# ===== 8. o painel da fila =====
confere(lambda: e("fila_aberta") == "0", "o painel da fila começa fechado")
toca("mu-fila-bt")
confere(lambda: e("fila_aberta") == "1" and pos("mu-fl/0") is not None, "o painel da fila abre")
confere(lambda: pos("mu-fl/1") is not None and pos("mu-fl/2") is not None, "mostra a faixa de agora e as seguintes")
toca("mu-fl/2")
confere(lambda: e("atual") == "2" and e("tocando") == "1", "tocar uma linha da fila pula para ela", 6)
toca("mu-ant")
toca("mu-ant")
confere(lambda: e("atual") == "0", "voltando duas faixas", 6)
ordem_antes = e("fila").strip("[]").split(",")
x, y = centro_de("mu-fgrip/1")
x2, y2 = centro_de("mu-fl/2")
toca("mu-play")  # pausa: as faixas de teste têm 3 s e a faixa não pode mudar sozinha durante o arrasto
confere(lambda: e("tocando") == "0", "pausada para arrastar")
x, y = centro_de("mu-fgrip/1")
x2, y2 = centro_de("mu-fl/2")
arrasta(x, y, x, y2 + 8)
ordem_depois = e("fila").strip("[]").split(",")
confere(lambda: ordem_depois == [ordem_antes[0], ordem_antes[2], ordem_antes[1]], f"arrastar a alça da 2ª para depois da 3ª reordena a fila ({ordem_antes} -> {ordem_depois})")
confere(lambda: e("atual") == "0", "e a faixa de agora continua a mesma")
toca("mu-fila-mais")
confere(lambda: pos("mu-fila-limpar") is not None, "o menu do painel abre")
toca("mu-fila-limpar")
confere(lambda: e("nfila") == "0" and e("tocando") == "0" and e("atual") == "-1", "Limpar a fila")
toca("mu-fila-bt")
confere(lambda: e("fila_aberta") == "0", "o painel fecha")

# ===== 9. o menu de uma faixa =====
toca("mu-nav/1")
confere(lambda: pos("mu-lin-mais/0") is not None, "a lista da biblioteca está na tela")
toca("mu-lin-mais/0")
confere(lambda: pos("mf-fila") is not None, "o '...' abre o menu da faixa")
toca("mf-fila")
confere(lambda: e("nfila") == "1" and e("tocando") == "1", "Adicionar à fila numa fila vazia cria a fila e toca", 6)
toca("mu-play")  # pausa: as faixas de teste têm 3 s e a fila não pode andar sozinha durante o teste
confere(lambda: e("tocando") == "0", "pausada")
toca("mu-lin-mais/2")
toca("mf-fila")
confere(lambda: e("nfila") == "2", f"Adicionar à fila soma ({e('nfila')})")
toca("mu-lin-mais/1")
toca("mf-seguir")
confere(lambda: e("nfila") == "3", "Tocar a seguir soma também")
f = e("fila").strip("[]").split(",")
confere(lambda: len(f) == 3 and f[1] == "Canção_do_fim", f"... e vai para logo depois da atual ({f})")
toca("mu-lin-mais/3")
toca("mf-tocar")
confere(lambda: e("nfila") == "1" and e("tocando") == "1", "Tocar agora troca a fila por essa faixa", 6)

# ===== 10. favoritas =====
confere(lambda: e("fav") == "0", "a faixa não é favorita")
toca("mu-coracao")
confere(lambda: e("fav") == "1", "o coração favorita")
confere(lambda: "sem tags" in le(f"{cfgdir}/playlists/Favoritas.m3u8"), "a Favoritas guarda o caminho")
toca("mu-pl/0")
confere(lambda: e("vista") == "5" and e("det_tipo") == "3" and e("det_n") == "1", "a playlist Favoritas tem 1 faixa")
toca("mu-coracao")
confere(lambda: e("fav") == "0" and e("det_n") == "0", "desfavoritar a tira da lista")

# ===== 11. playlists =====
toca("mu-nova-playlist")
confere(lambda: pos("mu-dlg-campo") is not None, "o '+' abre o diálogo de nova playlist")
digita("Foco")
tecla("Return")
confere(lambda: e("nplaylists") == "2" and os.path.exists(f"{cfgdir}/playlists/Foco.m3u8"), "a playlist Foco é criada")
toca("mu-nova-playlist")
digita("Foco")
tecla("Return")
confere(lambda: pos("mu-dlg-campo") is not None and e("nplaylists") == "2", "um nome repetido é recusado e o diálogo fica")
tecla("Escape")
confere(lambda: e("dlg") == "0", "Esc fecha o diálogo")
toca("mu-nav/1")
for k in (0, 1, 2):
    toca(f"mu-lin-mais/{k}")
    confere(lambda: pos("mf-pl/0") is not None, "o menu lista 'Adicionar a Foco'")
    toca("mf-pl/0")
time.sleep(0.4)
confere(lambda: len([l for l in le(f"{cfgdir}/playlists/Foco.m3u8").splitlines() if l and not l.startswith("#")]) == 3, "3 faixas na playlist Foco")
toca("mu-pl/1")
confere(lambda: e("vista") == "5" and e("det_tipo") == "3" and e("det_n") == "3", "abrir a playlist mostra as 3")
ordem0 = [os.path.basename(l) for l in le(f"{cfgdir}/playlists/Foco.m3u8").splitlines() if l and not l.startswith("#")]
x, y = centro_de("mu-grip/0")
x2, y2 = centro_de("mu-lin/2")
arrasta(x, y, x, y2 + 10)
ordem1 = [os.path.basename(l) for l in le(f"{cfgdir}/playlists/Foco.m3u8").splitlines() if l and not l.startswith("#")]
confere(lambda: ordem1 == ordem0[1:] + ordem0[:1], f"arrastar a alça da 1ª para o fim reordena a playlist ({ordem0} -> {ordem1})")
toca("mu-mais-det")
confere(lambda: pos("md-renomear") is not None, "o '...' do detalhe tem Renomear")
toca("md-renomear")
tecla("a", mod="ctrl")
digita("Estudo")
tecla("Return")
confere(lambda: os.path.exists(f"{cfgdir}/playlists/Estudo.m3u8") and not os.path.exists(f"{cfgdir}/playlists/Foco.m3u8"), "renomear a playlist")
toca("mu-mais-det")
toca("md-apagar")
confere(lambda: pos("mu-dlg-ok") is not None, "apagar pergunta antes")
toca("mu-dlg-cancelar")
confere(lambda: os.path.exists(f"{cfgdir}/playlists/Estudo.m3u8"), "Cancelar não apaga")
toca("mu-mais-det")
toca("md-apagar")
toca("mu-dlg-ok")
confere(lambda: not os.path.exists(f"{cfgdir}/playlists/Estudo.m3u8") and e("nplaylists") == "1", "Apagar remove a playlist")
confere(lambda: e("vista") != "5", "e sai da tela dela")
confere(lambda: os.path.exists(f"{musicas}/Luz distante/01 Aurora lenta.flac"), "as músicas continuam onde estavam")

# ===== 12. a busca =====
toca("mu-busca")
digita("cancao")
confere(lambda: e("vista") == "6" and e("nbusca") == "1", "buscar 'cancao' acha a 'Canção do fim' (sem acento)")
tecla("a", mod="ctrl")
tecla("BackSpace")
confere(lambda: e("vista") != "6" and e("busca") == "[]", "apagar a busca volta à tela de antes")
digita("ana luz")
confere(lambda: e("nbusca") == "3", f"duas palavras: 3 faixas ({e('nbusca')})")
toca("mu-lin/0")
confere(lambda: e("tocando") == "1" and e("titulo") == "Aurora_lenta" and e("nfila") == "3", "tocar um resultado toca a lista de resultados", 8)
tecla("a", mod="ctrl")
tecla("BackSpace")

# ===== 13. a letra =====
toca_faixa_pelo_nome("entre orbitas", "Entre órbitas")
toca("mu-letra-bt")
confere(lambda: e("vista") == "0" and e("letra_n") == "4" and e("letra_sinc") == "1", f"a letra sincronizada (.lrc) carrega: {e('letra_n')} linhas", 8)
clica_em("mu-seek", 0.01)
if e("tocando") == "0":
    toca("mu-play")
confere(lambda: e("tocando") == "1", "tocando de novo para ver a letra andar", 6)
vistas = set()
fim = time.time() + 3.0
while time.time() < fim:
    vistas.add(e("letra_linha"))
    time.sleep(0.1)
confere(lambda: {"1", "2"} <= vistas or {"2", "3"} <= vistas, f"a linha da vez anda com a música ({sorted(vistas)})")
toca("mu-seguir")
confere(lambda: e("seguir") == "0", "'Seguir reprodução' desliga")
toca("mu-seguir")
toca("mu-letra-bt")
confere(lambda: e("vista") != "0", "o botão da letra volta à tela de antes")
toca_faixa_pelo_nome("mare noturna", "Maré noturna")
toca("mu-letra-bt")
confere(lambda: e("letra_n") == "4" and e("letra_sinc") == "0", "a letra da tag (sem tempo) aparece inteira, não sincronizada", 6)
toca("mu-letra-bt")
toca_faixa_pelo_nome("instantes", "Instantes")
confere(lambda: e("letra_n") == "0", "sem letra: nada", 6)
toca("mu-letra-bt")
confere(lambda: not existe("mu-seguir"), "sem letra sincronizada não há 'Seguir reprodução'")
toca("mu-letra-bt")

# ===== 14. configurações =====
toca("mu-config")
confere(lambda: e("vista") == "7", "a engrenagem abre as configurações")
toca("mu-cfg/0")
confere(lambda: e("normalizar") == "0" and "normalizar=false" in le_ini(), "Normalizar volume desliga e é gravado")
toca("mu-cfg/0")
toca("mu-cfg/1")
confere(lambda: e("gapless") == "0", "Reprodução sem intervalos desliga")
toca("mu-cfg/1")
clica_em("mu-cfg/2", 0.5)
confere(lambda: 3 <= int(e("transicao")) <= 9 and "transicao=" in le_ini(), f"a transição entre faixas ({e('transicao')} s)")
clica_em("mu-cfg/2", 0.0)
confere(lambda: e("transicao") == "0", "e volta a 0")
rola(-400)
toca("mu-cfg/3")
confere(lambda: e("restaurar") == "0", "Restaurar fila ao abrir desliga")
toca("mu-cfg/3")
rola(400)
confere(lambda: existe("mu-emma"), "com a Emma ligada o botão dela aparece no alto")
rola(-400)
toca("mu-cfg/4")
rola(400)
confere(lambda: e("emma") == "0" and not existe("mu-emma"), "desligar 'Emma neste app' tira o botão")
rola(-400)
toca("mu-cfg/4")
rola(400)
toca("mu-aba/1")
confere(lambda: e("aba") == "1" and pos("mu-adicionar-pasta") is not None, "a aba Biblioteca")
# subpastas
toca("mu-cfg/5")
confere(lambda: e("subpastas") == "0" and e("nfaixas") == "1", f"sem subpastas só sobra o wav da raiz ({e('nfaixas')})", 20)
toca("mu-cfg/5")
confere(lambda: e("nfaixas") == "9", "com subpastas voltam as 9", 20)
# adicionar uma pasta de verdade
os.makedirs(f"{casa}/Extra", exist_ok=True)
shutil.copy(f"{musicas}/Solto/coletanea1.mp3", f"{casa}/Extra/novo.mp3")
toca("mu-adicionar-pasta")
confere(lambda: pos("mu-dlg-campo") is not None, "Adicionar pasta abre o diálogo")
tecla("a", mod="ctrl")
digita("/nao/existe/mesmo")
tecla("Return")
time.sleep(0.4)
confere(lambda: pos("mu-dlg-campo") is not None and e("npastas_cfg") == "1", "uma pasta que não existe é recusada")
tecla("a", mod="ctrl")
digita(f"{casa}/Extra")
tecla("Return")
confere(lambda: e("npastas_cfg") == "2" and e("nfaixas") == "10", f"a pasta nova entra: 10 músicas ({e('nfaixas')})", 25)
confere(lambda: f"{casa}/Extra" in le_ini(), "e vai para o config.ini")
toca("mu-pasta-rm/1")
confere(lambda: e("npastas_cfg") == "1" and e("nfaixas") == "9", "remover a pasta tira as músicas dela", 25)
toca("mu-aba/2")
confere(lambda: e("aba") == "2", "a aba Letras")
toca("mu-cfg/7")
confere(lambda: "letras_mostrar=false" in le_ini(), "Mostrar letras desliga e grava")
toca("mu-cfg/7")
toca("mu-aba/3")
confere(lambda: e("aba") == "3", "a aba Aparência")
toca("mu-cfg/9")
confere(lambda: "capas=false" in le_ini(), "Mostrar as capas desliga e grava")
toca("mu-cfg/9")
toca("mu-aba/4")
confere(lambda: e("aba") == "4" and pos("mu-abrir-emma") is not None, "a aba Emma")
toca("mu-aba/0")

# ===== 15. a saída de áudio =====
toca("mu-saida")
confere(lambda: pos("mu-saida-i/0") is not None, "o seletor de saída abre com 'Neste computador'")
n_saidas = sum(1 for i in range(12) if pos(f"mu-saida-i/{i}") is not None)
if n_saidas > 1:
    toca("mu-saida-i/1")
    confere(lambda: e("saida") != "padrao", f"escolher outra saída ({e('saida')})")
    toca("mu-saida")
    toca("mu-saida-i/0")
    confere(lambda: e("saida") == "padrao", "e voltar ao padrão")
else:
    tecla("Escape")
    print(f"  (o mpv desta máquina só lista {n_saidas} saída(s): troca de saída só provada no teste do motor)")

# ===== 16. o MPRIS controla o app =====
toca("mu-nav/1")
toca("mu-lin/0")
confere(lambda: e("tocando") == "1" and e("nfila") == "9", "tocando a biblioteca toda", 8)
mpris_cmd("Pause")
confere(lambda: e("tocando") == "0", "MPRIS Pause")
mpris_cmd("Play")
confere(lambda: e("tocando") == "1", "MPRIS Play")
mpris_cmd("PlayPause")
confere(lambda: e("tocando") == "0", "MPRIS PlayPause")
mpris_cmd("PlayPause")
a0 = int(e("atual"))
mpris_cmd("Next")
confere(lambda: int(e("atual")) == a0 + 1, "MPRIS Next", 6)
mpris_cmd("Previous")
confere(lambda: int(e("atual")) == a0, "MPRIS Previous", 6)
gd("org.freedesktop.DBus.Properties.Set", "org.mpris.MediaPlayer2.Player", "Volume", "<0.25>")
confere(lambda: 20 <= num("volume") <= 30, f"MPRIS Volume = {e('volume')}")
gd("org.freedesktop.DBus.Properties.Set", "org.mpris.MediaPlayer2.Player", "Shuffle", "<true>")
confere(lambda: e("aleatorio") == "1", "MPRIS Shuffle")
gd("org.freedesktop.DBus.Properties.Set", "org.mpris.MediaPlayer2.Player", "Shuffle", "<false>")
gd("org.freedesktop.DBus.Properties.Set", "org.mpris.MediaPlayer2.Player", "LoopStatus", "<'Track'>")
confere(lambda: e("repetir") == "2", "MPRIS LoopStatus Track")
gd("org.freedesktop.DBus.Properties.Set", "org.mpris.MediaPlayer2.Player", "LoopStatus", "<'None'>")
mpris_cmd("Stop")
confere(lambda: e("tocando") == "0" and e("carregada") == "0", "MPRIS Stop")

# ===== 17. o mpv morre: o app avisa e se recupera =====
toca("mu-play")
confere(lambda: e("tocando") == "1", "tocando de novo", 6)
subprocess.run(["pkill", "-9", "-f", "lumna-musica-"])
confere(lambda: e("vivo") == "0", "o mpv morreu: o app percebe", 6)
toca("mu-play")
confere(lambda: e("tocando") == "1" and e("vivo") == "1", "Tocar levanta outro mpv", 10)

# ===== 18. recomeçar de onde parou =====
toca_faixa_pelo_nome("aurora", "Aurora lenta")
clica_em("mu-seek", 0.4)
time.sleep(0.5)
toca("mu-play")  # pausa
time.sleep(0.4)
titulo_antes, fila_antes, pos_antes, atual_antes = e("titulo"), e("fila"), num("pos"), e("atual")
confere(lambda: e("tocando") == "0", "pausada antes de fechar")
subprocess.run(["pkill", "-x", "-TERM", "musica"])
time.sleep(1.5)
if pid():
    subprocess.run(["pkill", "-x", "-9", "musica"])
    time.sleep(0.5)
open(f"{run}/g.out", "w").close()
novo = subprocess.Popen([f"{os.environ.get('MUSICA_BIN', '')}" or "musica"], env=dict(os.environ, LUMNA_MUSICA_DEBUG="1"), stdout=open(f"{run}/g.out", "a"), stderr=open(f"{run}/g2.err", "a"))
confere(lambda: e("nfaixas") == "9" and e("carregando") == "0", "reabriu e leu a biblioteca (pelo cache)", 30)
confere(lambda: e("titulo") == titulo_antes and e("atual") == atual_antes, f"a fila volta com a mesma faixa ({e('titulo')} == {titulo_antes})")
confere(lambda: e("nfila") != "0" and e("tocando") == "0", "e fica parada, esperando")
toca("mu-play")
confere(lambda: e("tocando") == "1" and num("pos") >= max(0, pos_antes - 0.8), f"Tocar continua perto de onde parou ({e('pos')} / {pos_antes:.1f})", 10)
novo.terminate()

if not subprocess.run(["pgrep", "-x", "orbit"], capture_output=True).stdout:
    falha("o Orbit caiu")
if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("musica (app inteiro: biblioteca, álbuns, tocar, fila, favoritas, playlists, busca, letras, configurações, MPRIS e recomeçar): ok")
