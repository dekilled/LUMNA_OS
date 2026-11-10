"""Fotos de mentira para o teste do editor (servicos/edicao.c). python3 fixtures_edicao.py PASTA"""
import os, random, sys
from PIL import Image

d = sys.argv[1]
os.makedirs(d, exist_ok=True)

# 640x480 com um bloco colorido em cada canto e o EXIF dizendo "gire 90 graus para a direita" (Orientation = 6): na tela fica 480x640
im = Image.new("RGB", (640, 480), (128, 128, 128))
for (x, y, cor) in ((0, 0, (255, 0, 0)), (600, 0, (0, 255, 0)), (0, 440, (0, 0, 255)), (600, 440, (255, 255, 0))):
    im.paste(cor, (x, y, x + 40, y + 40))
e = Image.Exif()
e[0x0112] = 6
im.save(os.path.join(d, "girada.jpg"), exif=e, quality=95)

# ruído suave: o tamanho do JPEG depende da qualidade
random.seed(7)
r = Image.new("RGB", (800, 600))
px = r.load()
for y in range(600):
    for x in range(800):
        px[x, y] = ((x * 255) // 800 + random.randint(-30, 30) & 255, (y * 255) // 600 + random.randint(-30, 30) & 255, random.randint(0, 255))
r.save(os.path.join(d, "ruido.jpg"), quality=95)

# PNG com transparência (metade transparente)
a = Image.new("RGBA", (200, 100), (200, 40, 40, 255))
a.paste((0, 0, 0, 0), (100, 0, 200, 100))
a.save(os.path.join(d, "alfa.png"))

# um arquivo quebrado
with open(os.path.join(d, "quebrada.jpg"), "wb") as f:
    f.write(b"\xff\xd8\xff\xe0 isto nao e uma foto " * 4)

# um WebP (se o PIL souber): o editor salva como JPEG ou PNG
try:
    Image.new("RGB", (120, 80), (10, 120, 200)).save(os.path.join(d, "foto.webp"))
except Exception:
    pass
