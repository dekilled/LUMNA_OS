#!/usr/bin/env python3
"""O player de vídeo da Galeria (libmpv dentro da janela) num Orbit headless: abrir pelo toque, tocar de verdade (o quadro tem cor, o tempo anda), pausar, buscar,
volume, velocidade, legendas e áudio, repetir, o vídeo seguinte e o anterior, MPRIS, Editar, favoritar, a lixeira, a falha de um arquivo quebrado e 'abrir no mpv'.
TESTE=teste_player.py sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY"""
import os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from comum import *

vids = f"{casa}/Vídeos"


def num(k):
    return float(estado().get(k, "nan"))


def tela():
    return estado().get("tela")


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


def dbus(*a):
    return subprocess.run(["gdbus", "call", "--session", "--dest", "org.mpris.MediaPlayer2.lumna_galeria", "--object-path", "/org/mpris/MediaPlayer2", *a],
                          capture_output=True, text=True).stdout


def mpris(prop):
    return dbus("--method", "org.freedesktop.DBus.Properties.Get", "org.mpris.MediaPlayer2.Player", prop)


# um segundo vídeo (sem barulho de fundo: tem áudio) e uma legenda para o primeiro, antes de a Galeria reler as pastas
subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=duration=60:size=320x240:rate=10", "-f", "lavfi", "-i", "sine=frequency=440:duration=60",
                "-pix_fmt", "yuv420p", "-shortest", f"{vids}/zz-longo.mp4"], check=True)
subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc=duration=3:size=320x240:rate=10", "-pix_fmt", "yuv420p", "-c:v", "libx264", f"{vids}/a-quebrado-tmp.mp4"], check=False)
open(f"{vids}/zz-longo.srt", "w").write("1\n00:00:00,500 --> 00:00:09,000\nOlá, legenda do teste\n")
with open(f"{vids}/quebrado.mp4", "wb") as f:   # um arquivo que não é vídeo
    f.write(b"isto nao e um video" * 50)
os.remove(f"{vids}/a-quebrado-tmp.mp4") if os.path.exists(f"{vids}/a-quebrado-tmp.mp4") else None
time.sleep(1.5)

toca("g-vista/2")   # Vídeos
confere(lambda: estado().get("vista") == "2", "a vista Vídeos")
confere(lambda: "zz-longo.mp4" in itens() and "clipe.mp4" in itens(), "os vídeos aparecem")
nomes = itens()

# 1. tocar abre o player, e o vídeo toca de verdade
toca(f"g-cel/{nomes.index('zz-longo.mp4')}")
confere(lambda: tela() == "5", "tocar num vídeo abre o player")
confere(lambda: estado().get("pl_car") == "0" and estado().get("pl_erro") == "0", "o vídeo abriu sem erro", 8)
confere(lambda: estado().get("pl_tocando") == "1", "começa tocando", 8)
confere(lambda: abs(num("pl_dur") - 60) < 1, f"a duração é 60 s ({estado().get('pl_dur')})")
confere(lambda: num("pl_w") == 320 and num("pl_h") == 240, "o tamanho do vídeo é 320x240")
p1 = num("pl_pos")
time.sleep(1.5)
if not num("pl_pos") > p1 + 0.8:
    falha(f"o tempo anda ({p1} → {estado().get('pl_pos')})")
confere(lambda: estado().get("pl_px") not in (None, "-1,-1,-1", "0,0,0"), "o quadro do meio da tela tem cor (o vídeo aparece)")
confere(lambda: estado().get("pl_mpris") == "1", "o MPRIS está de pé")
confere(lambda: estado().get("pl_naudio") == "1" and estado().get("pl_nleg") == "1", f"1 faixa de áudio e 1 legenda ({estado().get('pl_naudio')}/{estado().get('pl_nleg')})")

# 2. pausar e retomar
toca("pl-play")
confere(lambda: estado().get("pl_tocando") == "0", "o botão pausa")
tp = num("pl_pos")
time.sleep(1.0)
if abs(num("pl_pos") - tp) > 0.3:
    falha("pausado, o tempo não devia andar")
toca("pl-central") if existe("pl-central") else None
confere(lambda: estado().get("pl_tocando") == "1", "o botão do meio (ou o toque) retoma")
tecla("space")
confere(lambda: estado().get("pl_tocando") == "0", "Espaço pausa")
tecla("space")
confere(lambda: estado().get("pl_tocando") == "1", "Espaço retoma")

# 3. buscar com as setas e com a barra
t0 = num("pl_pos")
tecla("Right")
confere(lambda: num("pl_pos") > t0 + 3, f"→ avança uns 5 s ({t0} → {estado().get('pl_pos')})")
t0 = num("pl_pos")
tecla("Left")
confere(lambda: num("pl_pos") < t0 - 3, "← volta uns 5 s")
x, y = centro("pl-seek")
cx, cy, cw, ch = pos("pl-seek")
ox, oy = origem()
aponta(ox + cx + int(cw * 0.75), y)
msg("toque", "baixo", 1, ox + cx + int(cw * 0.75), y)
time.sleep(0.1)
msg("toque", "cima", 1)
confere(lambda: 40 < num("pl_pos") < 50, f"clicar a 75% da barra pula para ~45 s ({estado().get('pl_pos')})")

# 4. volume
v0 = num("pl_vol")
tecla("Down")
confere(lambda: num("pl_vol") == v0 - 5, f"↓ baixa o volume ({v0} → {estado().get('pl_vol')})")
tecla("Up")
confere(lambda: num("pl_vol") == v0, "↑ sobe o volume")
toca("pl-vol-icone")
confere(lambda: num("pl_vol") == 0, "tocar no alto-falante muda para mudo")
toca("pl-vol-icone")
confere(lambda: num("pl_vol") > 0, "e de novo desmuta")

# 5. velocidade
toca("pl-vel")
confere(lambda: existe("pl-vel-i/4"), "o menu de velocidade abre")
toca("pl-vel-i/4")
confere(lambda: abs(num("pl_vel") - 1.5) < 0.01, f"1,5× ({estado().get('pl_vel')})")
toca("pl-vel")
toca("pl-vel-i/2")
confere(lambda: abs(num("pl_vel") - 1.0) < 0.01, "volta a 1×")

# 6. legendas
toca("pl-faixas")
confere(lambda: existe("pl-leg-i/1"), "o menu de legendas e áudio abre")
toca("pl-leg-i/1")
confere(lambda: num("pl_leg") > 0, "escolher uma legenda liga")
toca("pl-faixas")
toca("pl-leg-i/0")
confere(lambda: num("pl_leg") == 0, "'Sem legenda' desliga")
confere(lambda: tela() == "5", "escolher uma legenda só fecha o menu (o player continua)")

# 7. repetir (liga e desliga)
toca("pl-repetir")
confere(lambda: estado().get("pl_rep") == "1", "repetir liga")
toca("pl-repetir")
confere(lambda: estado().get("pl_rep") == "0", "repetir desliga")

# 8. o MPRIS: o estado e os comandos de fora
st = mpris("PlaybackStatus")
if "Playing" not in st:
    falha(f"MPRIS PlaybackStatus devia ser Playing: {st!r}")
if "zz-longo" not in dbus("--method", "org.freedesktop.DBus.Properties.Get", "org.mpris.MediaPlayer2.Player", "Metadata"):
    falha("MPRIS Metadata devia ter o título do vídeo")
dbus("--method", "org.mpris.MediaPlayer2.Player.Pause")
confere(lambda: estado().get("pl_tocando") == "0", "MPRIS Pause pausa o player")
dbus("--method", "org.mpris.MediaPlayer2.Player.PlayPause")
confere(lambda: estado().get("pl_tocando") == "1", "MPRIS PlayPause retoma")
confere(lambda: "Playing" in mpris("PlaybackStatus"), "e o status volta a Playing")

# 8b. o fim do vídeo: para, mostra o botão do meio, e tocar de novo recomeça
cx, cy, cw, ch = pos("pl-seek")
ox, oy = origem()
xx = ox + cx + int(cw * 0.995)
aponta(xx, oy + cy + ch // 2)
msg("toque", "baixo", 1, xx, oy + cy + ch // 2)
time.sleep(0.1)
msg("toque", "cima", 1)
confere(lambda: estado().get("pl_fim") == "1" and estado().get("pl_tocando") == "0", "no fim o vídeo para", 8)
toca("pl-play")
confere(lambda: estado().get("pl_tocando") == "1" and num("pl_pos") < 5, f"tocar no fim recomeça do começo ({estado().get('pl_pos')})", 6)

# 9. o vídeo anterior e o seguinte (na ordem da vista)
k0 = int(estado().get("pl_k"))
tecla("n")
confere(lambda: int(estado().get("pl_k")) != k0 or k0 == len(nomes) - 1, "N vai ao vídeo seguinte")
tecla("p")
confere(lambda: tela() == "5", "P volta (e o player continua aberto)")

# 9b. tela cheia: F liga (o título e os controles somem sem mexer), Esc sai da tela cheia sem fechar o player
tecla("f")
confere(lambda: estado().get("pl_tc") == "1", "F põe em tela cheia")
tecla("Escape")
confere(lambda: estado().get("pl_tc") == "0" and tela() == "5", "Esc sai da tela cheia e o player continua")

# 10. favoritar
toca("pl-fav")
time.sleep(0.5)
nome_atual = nomes[int(estado().get("pl_k"))]
tecla("Escape")
confere(lambda: tela() == "0", "Esc volta à galeria")
confere(lambda: estado().get("pl") is None, "o player some do estado ao fechar")

# 11. Editar abre o editor de vídeo
toca(f"g-cel/{nomes.index('clipe.mp4')}")
confere(lambda: tela() == "5", "abre o clipe")
toca("pl-editar")
confere(lambda: tela() == "4", "Editar abre o editor de vídeo")
toca("vd-voltar")
confere(lambda: tela() == "0", "voltar do editor vai à galeria")

# 12. o arquivo quebrado dá erro, com a saída para o mpv
toca(f"g-cel/{nomes.index('quebrado.mp4')}")
confere(lambda: tela() == "5" and estado().get("pl_erro") == "1", "um arquivo que não é vídeo mostra o erro", 8)
confere(lambda: existe("pl-erro-mpv"), "e oferece tentar no mpv")
tecla("Escape")
confere(lambda: tela() == "0", "Esc sai do erro")

# 13. o menu "..." → mandar para a lixeira
toca(f"g-cel/{nomes.index('quebrado.mp4')}")
confere(lambda: tela() == "5", "abre de novo")
toca("pl-mais")
toca("pl-m-lixeira")
confere(lambda: tela() == "0" and "quebrado.mp4" not in itens() and not os.path.exists(f"{vids}/quebrado.mp4"), "o menu manda o vídeo para a lixeira e volta", 6)

# 14. o vídeo que some da lista com o player aberto: o player fecha
toca(f"g-cel/{nomes.index('zz-longo.mp4') - 1 if 'quebrado.mp4' in nomes and nomes.index('quebrado.mp4') < nomes.index('zz-longo.mp4') else nomes.index('zz-longo.mp4')}")
confere(lambda: tela() == "5", "abre o longo")
os.remove(f"{vids}/zz-longo.mp4")
confere(lambda: tela() == "0", "o arquivo apagado de fora fecha o player", 8)

print("player:", "ok" if not falhas else f"{len(falhas)} falhas")
sys.exit(1 if falhas else 0)
