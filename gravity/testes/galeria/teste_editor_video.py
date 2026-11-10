#!/usr/bin/env python3
"""O editor de vídeos da Galeria num Orbit headless: recorte na faixa, campos de tempo, tocar a prévia, áudio, girar, exportar a cópia e descartar.
TESTE=teste_editor_video.py sh teste_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY"""
import hashlib, json, os, subprocess, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from comum import *

vids = f"{casa}/Vídeos"


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


def tela():
    return estado().get("tela")


def num(k):
    return float(estado().get(k, "nan"))


def sha(p):
    return hashlib.sha1(open(p, "rb").read()).hexdigest()


def arrasta(x1, y1, x2, y2, passos=8):
    aponta(x1, y1)
    time.sleep(0.2)
    msg("mouse", "botao", "baixo")
    time.sleep(0.2)
    for i in range(1, passos + 1):
        msg("mouse", "mover", int(x1 + (x2 - x1) * i / passos), int(y1 + (y2 - y1) * i / passos))
        time.sleep(0.07)
    time.sleep(0.3)
    msg("mouse", "botao", "cima")
    time.sleep(0.6)


def faixa_x(t):
    """o x (na tela) do instante t na faixa: espelha t_para_x de editor_video.c"""
    cx, cy, cw, ch = pos("vd-faixa")
    ox, oy = origem()
    return ox + cx + 16 + t / num("ev_dur") * (cw - 32), oy + cy + 30 + 36


def sonda(p):
    r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=codec_type,width,height:format=duration", "-of", "json", p], capture_output=True, text=True).stdout
    j = json.loads(r)
    v = [s for s in j["streams"] if s["codec_type"] == "video"][0]
    return float(j["format"]["duration"]), (v["width"], v["height"]), any(s["codec_type"] == "audio" for s in j["streams"])


def abrir(nome):
    if int(estado().get("nsel", "0")) > 0:
        toca("g-sel-fechar")  # a seleção de antes ficou: marcar de novo desmarcaria
    toca("g-busca")
    tecla("a", mod="ctrl")
    digita(nome.rsplit(".", 1)[0])
    confere(lambda: nome in itens(), f"a busca acha {nome}")
    marca(itens().index(nome))  # seleciona e aperta E
    tecla("e")
    confere(lambda: tela() == "4", f"o editor de vídeo abre ({nome})")
    confere(lambda: estado().get("ev_carregando") == "0" and estado().get("ev_tiras") == "10" and estado().get("ev_quadro", "0x0") != "0x0" and estado().get("ev_busy") == "0", "carrega o quadro e as 10 miniaturas", 20)


subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc=duration=10:size=320x240:rate=15", "-f", "lavfi", "-i", "sine=frequency=440:duration=10",
                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", f"{vids}/longo.mp4"], check=True)
orig = sha(f"{vids}/longo.mp4")
confere(lambda: "longo.mp4" in itens() and estado().get("carregando") == "0", "a biblioteca vê o vídeo novo", 15)

# 1. abrir
abrir("longo.mp4")
confere(lambda: abs(num("ev_dur") - 10) < 0.3 and estado().get("ev_audio") == "1", f"duração e áudio ({estado().get('ev_dur')})")
confere(lambda: num("ev_ini") == 0 and abs(num("ev_fim") - num("ev_dur")) < 0.01 and estado().get("ev_alt") == "0", "começa com o vídeo todo, sem alterações")
toca("vd-exportar")
time.sleep(0.5)
confere(lambda: estado().get("ev_exp") == "0" and not os.path.exists(f"{vids}/longo-editado.mp4"), "Exportar sem alterações não faz nada")

# 2. as alças da faixa
x, y = faixa_x(0)
x3, _ = faixa_x(3)
arrasta(int(x + 1), int(y), int(x3), int(y))
confere(lambda: abs(num("ev_ini") - 3) < 0.5 and num("ev_t") == num("ev_ini"), f"arrastar a alça do começo para ~3 s ({estado().get('ev_ini')})")
x, y = faixa_x(num("ev_dur"))
x8, _ = faixa_x(8)
arrasta(int(x - 1), int(y), int(x8), int(y))
confere(lambda: abs(num("ev_fim") - 8) < 0.5, f"arrastar a alça do fim para ~8 s ({estado().get('ev_fim')})")
confere(lambda: estado().get("ev_alt") == "1", "o vídeo fica alterado")
# a alça do começo não passa da do fim
xi, y = faixa_x(num("ev_ini"))
xe, _ = faixa_x(9.9)
arrasta(int(xi), int(y), int(xe), int(y))
confere(lambda: num("ev_fim") - num("ev_ini") >= 0.45, f"o trecho nunca fica menor que 0,5 s ({estado().get('ev_ini')} .. {estado().get('ev_fim')})")
# volta ao trecho 3..8 pelos campos de tempo (1 s por clique)
for _ in range(12):
    if num("ev_ini") <= 3.4:
        break
    toca("vd-menos/0")
for _ in range(12):
    if num("ev_fim") <= 8.4:
        break
    toca("vd-menos/1")
confere(lambda: num("ev_ini") <= 3.5 and num("ev_fim") <= 8.5 and num("ev_fim") - num("ev_ini") > 3, f"os campos ajustam de 1 em 1 s ({estado().get('ev_ini')} .. {estado().get('ev_fim')})")

# 3. os campos de tempo
ini0, fim0 = num("ev_ini"), num("ev_fim")
toca("vd-mais/0")
confere(lambda: abs(num("ev_ini") - (ini0 + 1)) < 0.01, "▲ do início soma 1 s")
toca("vd-menos/1")
confere(lambda: abs(num("ev_fim") - (fim0 - 1)) < 0.01, "▼ do fim tira 1 s")
toca("vd-menos/0")
toca("vd-mais/1")

# 4. um clique na faixa leva o ponto
x5, y = faixa_x(5)
aponta(int(x5), int(y))
msg("mouse", "botao", "baixo")
time.sleep(0.15)
msg("mouse", "botao", "cima")
confere(lambda: abs(num("ev_t") - 5) < 0.6, f"clicar na faixa leva o ponto a ~5 s ({estado().get('ev_t')})")
confere(lambda: estado().get("ev_busy") == "0", "e o quadro chega", 10)

# 5. tocar a prévia
t0 = num("ev_t")
toca("vd-play")
confere(lambda: estado().get("ev_play") == "1", "Play toca")
def mpv_audio():
    r = subprocess.run(["pgrep", "-af", "mpv --no-video"], capture_output=True, text=True).stdout
    return r
confere(lambda: estado().get("ev_aud") == "1" and "--no-video" in mpv_audio(), "tocando, o som sai por um mpv só de áudio")
confere(lambda: "--volume=100" in mpv_audio() and "--start=" in mpv_audio() and "--end=" in mpv_audio(), f"com o volume, o começo e o fim do trecho ({mpv_audio().strip()[-120:]})")
time.sleep(1.5)
t1 = num("ev_t")
confere(lambda: t1 - t0 > 0.7, f"o ponto anda enquanto toca ({t0} -> {t1})")
toca("vd-play")
confere(lambda: estado().get("ev_play") == "0", "Pausa para")
confere(lambda: estado().get("ev_aud") == "0" and mpv_audio().strip() == "", "pausando, o som para")
t2 = num("ev_t")
time.sleep(0.8)
confere(lambda: abs(num("ev_t") - t2) < 0.05, "parado, o ponto não anda")
toca("vd-play")
confere(lambda: estado().get("ev_play") == "0", "tocando até o fim do trecho, para sozinho", 15)
confere(lambda: abs(num("ev_t") - num("ev_fim")) < 0.6, f"e o ponto fica no fim do trecho ({estado().get('ev_t')} / {estado().get('ev_fim')})")
tecla("space")
confere(lambda: estado().get("ev_play") == "1", "a barra de espaço toca")
tecla("space")
confere(lambda: estado().get("ev_play") == "0", "e para")

# 6. áudio
toca("vd-play")
confere(lambda: estado().get("ev_aud") == "1", "toca de novo com som")
toca("vd-mudo")
confere(lambda: estado().get("ev_mudo") == "1" and estado().get("ev_aud") == "0" and mpv_audio().strip() == "", "mudo, tocando: o som cala sem parar a imagem")
confere(lambda: estado().get("ev_play") == "1", "e a imagem segue")
toca("vd-mudo")
confere(lambda: estado().get("ev_aud") == "1", "desmutar traz o som de volta")
toca("vd-play")
confere(lambda: estado().get("ev_play") == "0", "para a prévia")
toca("vd-mudo")
confere(lambda: estado().get("ev_mudo") == "1", "Silenciar áudio liga")
toca("vd-mudo")
confere(lambda: estado().get("ev_mudo") == "0", "e desliga")
b = pos("vd-vol")
ox, oy = origem()
yv = oy + b[1] + b[3] // 2
arrasta(ox + b[0] + b[2] // 2, yv, ox + b[0] + int(b[2] * 0.2), yv)
confere(lambda: num("ev_vol") < 60, f"o volume desce ({estado().get('ev_vol')})")

# 7. girar
q0 = estado()["ev_quadro"]
toca("vd-girar")
confere(lambda: estado().get("ev_giro") == "1", "Girar vídeo")
confere(lambda: estado().get("ev_quadro") == "240x320", f"o quadro vira em pé ({q0} -> {estado().get('ev_quadro')})", 10)
confere(lambda: estado().get("ev_tiras") == "10" and estado().get("ev_busy") == "0", "e as miniaturas são refeitas", 20)

# 8. exportar (mudo, girado, ~3..8)
toca("vd-mudo")
copia = f"{vids}/longo-editado.mp4"
ini, fim = num("ev_ini"), num("ev_fim")
toca("vd-exportar")
confere(lambda: estado().get("ev_exp") != "0" or os.path.exists(copia) or tela() != "4", "a exportação começa", 5)
confere(lambda: os.path.exists(copia), "a cópia aparece", 60)
confere(lambda: tela() == "0", "e a Galeria volta à biblioteca", 15)
confere(lambda: "longo-editado.mp4" in itens(), "a cópia entra na biblioteca", 15)
confere(lambda: sha(f"{vids}/longo.mp4") == orig, "o original não mudou nem um byte")
if os.path.exists(copia):
    dur, dim, audio = sonda(copia)
    confere(lambda: abs(dur - (fim - ini)) < 0.5, f"a cópia tem o tamanho do trecho ({dur:.2f} s, esperado {fim - ini:.2f})")
    confere(lambda: dim == (240, 320), f"girada: 240x320 ({dim})")
    confere(lambda: not audio, "sem áudio (silenciado)")

# 9. descartar
abrir("longo.mp4")
tecla("Escape")
confere(lambda: tela() == "0", "Esc sem alterações sai na hora")
abrir("longo.mp4")
toca("vd-mudo")
toca("vd-cancelar")
confere(lambda: estado().get("ev_dlg") == "1", "Cancelar com alterações pergunta")
toca("vd-dlg-continuar")
confere(lambda: estado().get("ev_dlg") == "0" and tela() == "4" and estado().get("ev_mudo") == "1", "Continuar editando mantém tudo")
toca("vd-voltar")
confere(lambda: estado().get("ev_dlg") == "1", "Voltar também pergunta")
toca("vd-dlg-descartar")
confere(lambda: tela() == "0", "Descartar sai")
confere(lambda: not os.path.exists(f"{vids}/longo-editado-2.mp4"), "e nada foi exportado")

if not subprocess.run(["pgrep", "-x", "galeria"], capture_output=True).stdout:
    falha("a Galeria caiu durante o teste do editor de vídeo")
if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("galeria (editor de vídeos: faixa, recorte, tocar, áudio, girar, exportar e descartar): ok")
