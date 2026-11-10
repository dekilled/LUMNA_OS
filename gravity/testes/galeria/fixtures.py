#!/usr/bin/env python3
"""Uma biblioteca de mentira para os testes da Galeria: fotos (uma com EXIF), PNG com transparência, um vídeo, subpasta, arquivos que não são mídia, um JPEG
estragado e uma imagem enorme (para a regra do limite de memória). python3 fixtures.py CASA"""
import os, subprocess, sys, time
from PIL import Image

casa = sys.argv[1]
img = os.path.join(casa, "Imagens")
vid = os.path.join(casa, "Vídeos")
cap = os.path.join(casa, "Capturas")
for d in (img, vid, cap, os.path.join(img, "Viagem")):
    os.makedirs(d, exist_ok=True)


def foto(caminho, cor, tam=(640, 480), exif=None, modo="RGB"):
    im = Image.new(modo, tam, cor)
    if exif:
        e = Image.Exif()
        e[0x0132] = exif
        ifd = e.get_ifd(0x8769)
        ifd[0x9003] = exif
        e[0x8769] = ifd
        im.save(caminho, exif=e)
    else:
        im.save(caminho)


agora = time.time()
foto(os.path.join(img, "lago.jpg"), (30, 90, 160), exif="2026:10:05 12:30:00")
foto(os.path.join(img, "hoje.jpg"), (200, 80, 40))
foto(os.path.join(img, "ontem.png"), (40, 160, 90), tam=(300, 600))
os.utime(os.path.join(img, "ontem.png"), (agora - 86400, agora - 86400))
foto(os.path.join(img, "vidro.png"), (255, 255, 255, 80), modo="RGBA")
foto(os.path.join(img, "Viagem", "praia.jpg"), (240, 200, 120), exif="2026:09:20 08:00:00")
foto(os.path.join(cap, "captura.png"), (20, 20, 30))
open(os.path.join(img, "notas.txt"), "w").write("não é mídia")
open(os.path.join(img, ".oculta.jpg"), "wb").write(b"x")
open(os.path.join(img, "quebrada.jpg"), "wb").write(b"\xff\xd8\xff nao e uma imagem de verdade")
Image.new("RGB", (9000, 9000), (10, 10, 10)).save(os.path.join(img, "enorme.png"))
subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc=duration=3:size=320x240:rate=10", "-pix_fmt", "yuv420p", os.path.join(vid, "clipe.mp4")], check=True)
print("ok")
