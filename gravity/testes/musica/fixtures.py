"""Músicas de mentira para os testes do LUMNA Music (servicos/musica.c). python3 fixtures.py PASTA  (cria PASTA/Músicas)"""
import os, subprocess, sys
from PIL import Image

raiz = os.path.join(sys.argv[1], "Músicas")
os.makedirs(raiz, exist_ok=True)


def capa(caminho, cor):
    Image.new("RGB", (300, 300), cor).save(caminho)


def audio(caminho, freq, seg=3, tags=None, cap=None, fmt=None):
    cmd = ["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", f"sine=frequency={freq}:duration={seg}"]
    if cap:
        cmd += ["-i", cap, "-map", "0:a", "-map", "1:v", "-c:v", "copy", "-disposition:v", "attached_pic"]
    if fmt == "mp3":
        cmd += ["-c:a", "libmp3lame", "-id3v2_version", "3"]
    elif fmt == "flac":
        cmd += ["-c:a", "flac"]
    elif fmt == "opus":
        cmd += ["-c:a", "libopus"]
    for k, v in (tags or {}).items():
        cmd += ["-metadata", f"{k}={v}"]
    cmd.append(caminho)
    subprocess.run(cmd, check=True)


a1 = os.path.join(raiz, "Horizonte violeta")
os.makedirs(a1, exist_ok=True)
capa("/tmp/_capa1.png", (120, 40, 200))
audio(f"{a1}/01 Entre órbitas.mp3", 440, tags={"title": "Entre órbitas", "artist": "LUMNA Sessions", "album": "Horizonte violeta", "track": "1/2", "date": "2024-03-01", "genre": "Ambiente"}, cap="/tmp/_capa1.png", fmt="mp3")
audio(f"{a1}/02 Maré noturna.mp3", 523, tags={"title": "Maré noturna", "artist": "LUMNA Sessions", "album": "Horizonte violeta", "track": "2/2", "date": "2024", "lyrics": "Primeira linha da tag\nSegunda linha da tag\n\nTerceira depois do espaço"}, cap="/tmp/_capa1.png", fmt="mp3")
open(f"{a1}/01 Entre órbitas.lrc", "w", encoding="utf-8").write("[ar:LUMNA Sessions]\n[ti:Entre órbitas]\n[00:01.00]O céu desacelera\n[00:02.50]Na curva do teu olhar\n[00:00.20][00:02.90]Entre órbitas eu te encontro\n[offset:0]\n")

a2 = os.path.join(raiz, "Luz distante")
os.makedirs(a2, exist_ok=True)
capa(f"{a2}/cover.jpg", (200, 120, 40))
audio(f"{a2}/02 Canção do fim.flac", 330, tags={"title": "Canção do fim", "artist": "Ana Maria", "album": "Luz distante", "track": "2", "disc": "1", "date": "2022"}, fmt="flac")
audio(f"{a2}/01 Aurora lenta.flac", 392, tags={"title": "Aurora lenta", "artist": "Ana Maria", "album": "Luz distante", "track": "1", "disc": "1", "date": "2022", "lyrics": "[00:00.50]linha um\n[00:01.50]linha dois"}, fmt="flac")
audio(f"{a2}/01 Bis.flac", 349, tags={"title": "Bis", "artist": "Ana Maria", "album": "Luz distante", "track": "1", "disc": "2", "date": "2022"}, fmt="flac")

a3 = os.path.join(raiz, "Solto", "Dentro")
os.makedirs(a3, exist_ok=True)
audio(f"{a3}/instantes.opus", 262, tags={"title": "Instantes", "artist": "Bruno", "album": "Instantes", "track": "1", "date": "2020"}, fmt="opus")
audio(f"{raiz}/sem tags.wav", 294, seg=2)
audio(f"{raiz}/Solto/coletanea1.mp3", 247, tags={"title": "Um", "artist": "Fulano", "album": "Coletânea"}, fmt="mp3")
audio(f"{raiz}/Solto/coletanea2.mp3", 247, tags={"title": "Dois", "artist": "Beltrano", "album": "Coletânea"}, fmt="mp3")
open(f"{raiz}/notas.txt", "w").write("não é música")
open(f"{raiz}/.oculta.mp3", "wb").write(b"x")
open(f"{raiz}/quebrada.mp3", "wb").write(b"ID3\x03\x00\x00\x00\x00\x00\x00 isto nao e audio" * 5)
