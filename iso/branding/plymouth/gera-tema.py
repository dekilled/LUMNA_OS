#!/usr/bin/env python3
"""Gera o tema de boot do Plymouth (packages/lumna-boot/plymouth/) a partir do vídeo iso/branding/BOOT_ANIMATION.mp4.

O vídeo vira poucos arquivos pequenos em vez de 240 quadros: uma placa estática (fundo, logo, textos, rodapé), um brilho da gema que pulsa, o ponto
que orbita (posição por matemática: elipse e período medidos no vídeo) e o anel de carregamento (57 quadros pequenos: o arco cresce e encolhe
enquanto gira). Precisa de ffmpeg, numpy e Pillow, só para gerar (o resultado vai versionado: a ISO não roda isto).

Uso: python3 gera-tema.py [video.mp4] [pasta_de_saida]
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image

AQUI = os.path.dirname(os.path.abspath(__file__))
VIDEO = sys.argv[1] if len(sys.argv) > 1 else os.path.join(AQUI, "..", "BOOT_ANIMATION.mp4")
SAIDA = sys.argv[2] if len(sys.argv) > 2 else os.path.join(AQUI, "..", "..", "..", "packages", "lumna-boot", "plymouth")
W, H, FPS = 1280, 720, 24
GEMA = (470, 80, 830, 360)       # a caixa do brilho da gema (x0, y0, x1, y1): o pulso vive aqui
ANEL = (600, 485, 680, 565)      # a caixa do anel de carregamento
PERIODO_ANEL = 57                # quadros de um ciclo do anel (medido: o arco volta igual a cada 57)

os.makedirs(SAIDA, exist_ok=True)
raw = subprocess.run(["ffmpeg", "-v", "error", "-i", VIDEO, "-map", "0:v:0", "-vf", f"scale={W}:{H}", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                     capture_output=True, check=True).stdout
A = np.frombuffer(raw, dtype=np.uint8).reshape(-1, H, W, 3)
N = len(A)
print("quadros:", N)

# ---- a placa: a mediana no tempo tira o que se mexe (o ponto, o arco); o anel e a gema ficam no mínimo (o trilho e o brilho de repouso)
plate = np.median(A[::2], axis=0)
x0, y0, x1, y1 = ANEL
plate[y0:y1, x0:x1] = np.percentile(A[:, y0:y1, x0:x1], 3, axis=0)
gx0, gy0, gx1, gy1 = GEMA
baixo = np.percentile(A[:, gy0:gy1, gx0:gx1], 5, axis=0)
alto = np.percentile(A[:, gy0:gy1, gx0:gx1], 95, axis=0)
plate[gy0:gy1, gx0:gx1] = baixo
Image.fromarray(plate.astype(np.uint8)).save(os.path.join(SAIDA, "placa.png"), optimize=True)

# ---- o brilho da gema: a versão clara, com a borda esmaecida (a opacidade dela pulsa por cima da placa)
h, w = gy1 - gy0, gx1 - gx0
yy, xx = np.mgrid[0:h, 0:w]
r = np.hypot((xx - w / 2) / (w / 2), (yy - h / 2) / (h / 2))
mask = np.clip(1.0 - r ** 2, 0, 1) ** 0.8
glow = np.dstack([alto, mask * 255]).astype(np.uint8)
Image.fromarray(glow, "RGBA").save(os.path.join(SAIDA, "brilho.png"), optimize=True)

# ---- o ponto da órbita: a posição brilhante mais forte fora da gema, ao longo do tempo
lum = A.astype(np.float32).sum(axis=3)
pos = []
for i in range(N):
    sub = lum[i, 60:340, 440:860].copy()
    sub[90:260, 100:300] = 0
    yb, xb = np.unravel_index(np.argmax(sub), sub.shape)
    pos.append((xb + 440, yb + 60, sub.max()))
pos = np.array(pos)
ok = pos[:, 2] > 600
x, y = pos[ok, 0], pos[ok, 1]
M = np.stack([x * x, x * y, y * y, x, y], 1)
a, b, c, d, e = np.linalg.lstsq(M, np.ones_like(x), rcond=None)[0]
cx = (b * e - 2 * c * d) / (4 * a * c - b * b)
cy = (b * d - 2 * a * e) / (4 * a * c - b * b)
Q = np.array([[a, b / 2], [b / 2, c]])
wv, vv = np.linalg.eigh(Q)
rhs = 1 + np.array([cx, cy]) @ Q @ np.array([cx, cy])
semi = np.sqrt(rhs / wv)
i = int(np.argmax(semi))
sa, sb = semi[i], semi[1 - i]
ang = float(np.arctan2(vv[1, i], vv[0, i]))
R = vv[:, [i, 1 - i]]
n = np.arange(N)
P = np.stack([pos[:, 0] - cx, pos[:, 1] - cy], 1) @ R
t = np.arctan2(P[:, 1] / sb, P[:, 0] / sa)
rr = np.hypot(P[:, 0] / sa, P[:, 1] / sb)
bom = (pos[:, 2] > 700) & (abs(rr - 1) < 0.12)
for _ in range(6):
    tu = np.unwrap(t[bom])
    pf = np.polyfit(n[bom], tu, 1)
    res = tu - np.polyval(pf, n[bom])
    keep = abs(res) < max(0.15, 2 * res.std())
    idx = np.where(bom)[0]
    bom[idx[~keep]] = False
omega_s = pf[0] * FPS   # rad/s
fase = pf[1]
if R[0, 0] * R[1, 1] - R[0, 1] * R[1, 0] < 0:  # o eixo menor do ajuste saiu virado ao contrário do (-sen, cos) que o script usa: o sentido troca
    omega_s, fase = -omega_s, -fase
print("órbita: centro %.1f,%.1f semieixos %.1f,%.1f ângulo %.1f°, período %.2f s" % (cx, cy, sa, sb, np.degrees(ang), 2 * np.pi / abs(omega_s)))
# o sprite do ponto: sintetizado (um núcleo branco com um halo suave); recortá-lo do vídeo trazia o ruído do fundo num quadradinho
S = 12
yy2, xx2 = np.mgrid[-S:S + 1, -S:S + 1]
r2 = np.hypot(xx2, yy2)
dl = np.clip(np.exp(-(r2 / 2.4) ** 2) + 0.38 * np.exp(-(r2 / 6.5) ** 2), 0, 1)
dot = np.dstack([np.full(dl.shape + (3,), 255), dl * 255]).astype(np.uint8)
dot[..., :3] = (255, 248, 255)
Image.fromarray(dot, "RGBA").save(os.path.join(SAIDA, "ponto.png"), optimize=True)

# ---- o pulso da gema: seno ajustado ao brilho médio do miolo
g = np.array([float(A[k, 165:300, 570:710].astype(np.float32).mean()) for k in range(N)])
X = np.arange(N)
melhor = None
for Pn in np.arange(110, 150, 0.25):
    Mm = np.stack([np.ones(N), np.sin(2 * np.pi * X / Pn), np.cos(2 * np.pi * X / Pn)], 1)
    cc = np.linalg.lstsq(Mm, g, rcond=None)[0]
    er = ((Mm @ cc - g) ** 2).mean()
    if melhor is None or er < melhor[0]:
        melhor = (er, Pn, cc)
_, Pn, cc = melhor
gema_periodo_s = Pn / FPS
gema_fase = float(np.arctan2(cc[2], cc[1]))
print("pulso da gema: período %.2f s" % gema_periodo_s)

# ---- o anel: um ciclo de quadros pequenos (alfa pelo quanto passa do mínimo do trilho)
trilho = A[:, ANEL[1]:ANEL[3], ANEL[0]:ANEL[2]].astype(np.float32).sum(axis=3).min(axis=0)
for k in range(PERIODO_ANEL):
    q = A[k, ANEL[1]:ANEL[3], ANEL[0]:ANEL[2]].astype(np.float32)
    l = q.sum(axis=2)
    al = np.clip((l - trilho) / np.maximum(l, 1) * 3.0, 0, 1)
    Image.fromarray(np.dstack([q, al * 255]).astype(np.uint8), "RGBA").save(os.path.join(SAIDA, "anel-%02d.png" % k), optimize=True)

# ---- a cor do fundo (para o que sobra em telas de outra proporção) e os números para o script
borda = plate[:8, :].reshape(-1, 3).mean(axis=0) / 255.0
param = f"""# Gerado por iso/branding/plymouth/gera-tema.py a partir do vídeo do boot: não edite à mão.
PLACA_L = {W};
PLACA_A = {H};
FUNDO_R = {borda[0]:.4f};
FUNDO_G = {borda[1]:.4f};
FUNDO_B = {borda[2]:.4f};
BRILHO_X = {GEMA[0]};
BRILHO_Y = {GEMA[1]};
BRILHO_PERIODO = {gema_periodo_s:.4f};
BRILHO_FASE = {gema_fase:.4f};
PONTO_CX = {cx:.2f};
PONTO_CY = {cy:.2f};
PONTO_A = {sa:.2f};
PONTO_B = {sb:.2f};
PONTO_ANG = {ang:.5f};
PONTO_OMEGA = {omega_s:.5f};
PONTO_FASE = {fase:.5f};
PONTO_MEIO = {S};
ANEL_X = {ANEL[0]};
ANEL_Y = {ANEL[1]};
ANEL_QUADROS = {PERIODO_ANEL};
ANEL_FPS = {FPS};
"""
modelo = open(os.path.join(AQUI, "lumna.script.in")).read()
open(os.path.join(SAIDA, "lumna.script"), "w").write(param + "\n" + modelo)
print("pronto em", SAIDA, "—", sum(os.path.getsize(os.path.join(SAIDA, f)) for f in os.listdir(SAIDA)) // 1024, "KB")
