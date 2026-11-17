#!/usr/bin/env python3
"""
Importa (uma vez) o roteiro antigo docs/TESTES-ISO.md para o registro de testes (registro.tsv).
Cada linha de tabela "| 2.435 | O que fazer | Deve acontecer | ☐ |" vira um teste, com a área adivinhada pela seção e por palavras-chave
(o chat da área Testes confere e corrige a área de cada um). Não sobrescreve um registro que já existe, a não ser com --forcar.
  python3 docs/testes/importar.py [--forcar]
"""
import os, re, sys
AQUI = os.path.dirname(os.path.abspath(__file__))
ORIGEM = os.path.join(AQUI, "..", "TESTES-ISO.md")
DESTINO = os.path.join(AQUI, "registro.tsv")
sys.path.insert(0, AQUI)
from registro import COLUNAS, grava

# a área pela seção do roteiro; None = adivinhar pelo texto
POR_SECAO = {
    "0": "Hardware", "1": "Hardware", "2": None, "2b": "Painel", "2c": "Desktop", "2d": "Desktop", "2e": "Desktop", "2f": "Desktop",
    "2g": "Hardware", "2h": "Desktop", "2i": "Desktop", "2j": "Painel", "2k": "Gaming", "2l": "Gaming", "2m": "Gaming", "2n": "Gaming",
    "2o": "Gaming", "2p": "Gaming", "2q": "Desktop", "2r": "Desktop", "2s": "Desktop", "2t": None, "3": "Hardware", "3b": "Hardware",
    "4": "Hardware", "4b": "Painel", "5": "Hardware",
}
PALAVRAS = [
    ("Gaming", r"gaming|jogo|steam|gamepad|controle \(|proton|heroic|lutris"),
    ("Mídia", r"m[ií]dia|youtube|netflix|streaming|super\+m"),
    ("Painel", r"painel|atualiza|idioma|senha do administrador|lumna-polkit|pr[ée]-setup|animaç"),
    ("Hardware", r"boot|grub|pendrive|disco|wi-?fi|networkmanager|bluetooth|kernel|driver|monitor|resolu|hdmi|bateria|suspend|som|volume|fone"),
    ("Desktop", r"firefox|atlas|arquivos|janela|dock|barra|orbit|m[úu]sica|galeria|captura|signals|calculadora|lan[çc]ador|teclado"),
]


def area_de(secao, texto):
    a = POR_SECAO.get(secao)
    if a:
        return a
    t = texto.lower()
    for nome, rx in PALAVRAS:
        if re.search(rx, t):
            return nome
    return "A classificar"


def main():
    if os.path.exists(DESTINO) and "--forcar" not in sys.argv:
        sys.exit("registro.tsv já existe (use --forcar para importar de novo e PERDER os resultados)")
    secao, titulo, testes = "", "", []
    for linha in open(ORIGEM, encoding="utf-8"):
        m = re.match(r"^#{2,3} +(\d+[a-z]?)\.? (.*)", linha)
        if m:
            secao, titulo = m.group(1), m.group(2).strip()
            continue
        if not re.match(r"^\| *\d+[a-z]?\.\d+[a-z]? *\|", linha):
            continue
        # as células; um "|" dentro de um comando (grep -E "a|b") fica no "deve acontecer"
        cel = linha.strip().strip("|").split("|")
        num, fazer = cel[0].strip(), cel[1].strip()
        esperado = "|".join(cel[2:-1]).strip() if len(cel) > 3 else (cel[2].strip() if len(cel) > 2 else "")
        testes.append({"id": "T" + num, "area": area_de(secao, fazer + " " + esperado + " " + titulo), "grupo": f"{secao}. {titulo}",
                       "fazer": fazer, "esperado": esperado, "estado": "pendente", "data": "", "nota": "", "origem": "TESTES-ISO"})
    grava(DESTINO, testes)
    print(len(testes), "testes importados")


main()
