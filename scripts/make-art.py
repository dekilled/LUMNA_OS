#!/usr/bin/env python3
"""Gera a arte provisória do LUMNA (ícone e wallpaper) sem dependências além do Python.

Uso: make-art.py icon SAIDA.png | wallpaper SAIDA.png [LARGURA ALTURA]
"""
import math
import random
import struct
import sys
import zlib


def write_png(path, width, height, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def icon(path, size=256):
    """Cristal violeta: losango alongado com faceta clara."""
    rows = []
    cx, top, mid, bottom, half = size / 2, size * 0.06, size * 0.36, size * 0.94, size * 0.30
    for y in range(size):
        row = []
        for x in range(size):
            if top <= y <= mid:
                w = half * (y - top) / (mid - top)
            elif mid < y <= bottom:
                w = half * (bottom - y) / (bottom - mid)
            else:
                w = -1
            dx = x - cx
            if abs(dx) <= w:
                t = (y - top) / (bottom - top)
                light = 0.35 if abs(dx) < w * 0.35 else 0.0
                r = int(196 - 80 * t + 59 * light)
                g = int(181 - 123 * t + 74 * light)
                b = int(253 - 16 * t + 2 * light)
                row += [min(r, 255), min(g, 255), min(b, 255), 255]
            else:
                row += [0, 0, 0, 0]
        rows.append(row)
    write_png(path, size, size, rows)


def wallpaper(path, width=1920, height=1080):
    """Noite violeta: degradê, brilho de planeta no canto e estrelas."""
    rng = random.Random(42)
    stars = {(rng.randrange(width), rng.randrange(int(height * 0.7))): rng.random() for _ in range(width * height // 4000)}
    px, py, pr = width * 0.82, -height * 0.35, height * 0.75
    rows = []
    for y in range(height):
        t = y / height
        base = (int(13 + 10 * t), int(9 + 4 * t), int(28 + 20 * t))
        row = []
        for x in range(width):
            r, g, b = base
            d = math.hypot(x - px, y - py)
            if d < pr:
                edge = max(0.0, 1 - (pr - d) / 40)
                r, g, b = r + 20 + int(120 * edge), g + 14 + int(70 * edge), b + 40 + int(160 * edge)
            else:
                glow = max(0.0, 1 - (d - pr) / (height * 0.5))
                r, g, b = r + int(60 * glow**2), g + int(30 * glow**2), b + int(110 * glow**2)
            s = stars.get((x, y))
            if s and d >= pr:
                v = int(120 + 135 * s)
                r, g, b = max(r, v), max(g, v), max(b, v)
            row += [min(r, 255), min(g, 255), min(b, 255), 255]
        rows.append(row)
    write_png(path, width, height, rows)


if __name__ == "__main__":
    kind, out = sys.argv[1], sys.argv[2]
    if kind == "icon":
        icon(out)
    else:
        wallpaper(out, *(int(v) for v in sys.argv[3:5]))
