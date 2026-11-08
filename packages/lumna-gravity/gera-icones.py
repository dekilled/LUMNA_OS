#!/usr/bin/env python3
"""Gera os ícones dos apps (SVG, tema hicolor) a partir dos desenhos embutidos no Gravity (src/gv_icones.c):
um quadrado de cantos redondos escuro com o desenho em lilás (o acento do tema). Roda uma vez; o resultado vai commitado em icones/."""
import os, re, sys

aqui = os.path.dirname(os.path.abspath(__file__))
fonte = open(os.path.join(aqui, "..", "..", "gravity", "src", "gv_icones.c"), encoding="utf-8").read()
caminhos = dict(re.findall(r'\{ "([a-z0-9-]+)", "([^"]+)" \}', fonte))

# nome do ícone do app -> desenho do Gravity
apps = {
    "lumna-painel": "settings", "lumna-emma": "emma", "lumna-galeria": "image", "lumna-captura": "camera",
    "lumna-rede": "wifi", "lumna-mixer": "volume", "lumna-midia": "media-play", "lumna-instalador": "download",
    "lumna-connect": "phone", "lumna-arquivos": "folder", "lumna-loja": "bag", "lumna-pulse": "terminal",
}
saida = os.path.join(aqui, "icones")
os.makedirs(saida, exist_ok=True)
for nome, desenho in apps.items():
    if desenho not in caminhos:
        sys.exit(f"o Gravity não tem o desenho '{desenho}'")
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64">'
           f'<rect width="64" height="64" rx="15" fill="#1b1d24"/>'
           f'<g transform="translate(14 14) scale(1.5)" fill="none" stroke="#a78bfa" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">'
           f'<path d="{caminhos[desenho]}"/></g></svg>\n')
    open(os.path.join(saida, nome + ".svg"), "w", encoding="utf-8").write(svg)
print(len(apps), "ícones em", saida)
