#!/usr/bin/env python3
"""O widget do Music: sem cartão com a janela aberta; ao fechar a janela (tocando) o Music vai para o segundo plano e o cartão aparece; os botões do cartão mandam no Music;
tocar no cartão traz a janela de volta e o cartão some."""
import json, os, re, subprocess, sys, time

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    return subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, capture_output=True, text=True).stdout


def le(arq):
    r = {}
    for l in open(f"{run}/{arq}", encoding="utf-8", errors="replace").read().splitlines():
        if l.startswith("estado "):
            r = dict(re.findall(r"([\w-]+)=(\[[^\]]*\]|\S+)", l[7:]))
    return r


def w():
    return le("w.out")


def m():
    return le("g.out")


def caixa(nome):
    v = w().get(nome)
    if not v:
        return None
    x, y, ww, h = (float(n) for n in v.split(","))
    return (x, y, ww, h) if ww > 0 else None


def espera(c, seg=6):
    fim = time.time() + seg
    while time.time() < fim:
        if c():
            return True
        time.sleep(0.15)
    return c()


def confere(c, o_que, seg=6):
    if not espera(c, seg):
        falha(o_que + f" (widget: visivel={w().get('w_visivel')} tem={w().get('w_tem')} tocando={w().get('w_tocando')}; music: tocando={m().get('tocando')} esc={m().get('esc')})")


def janelas_music():
    try:
        j = json.loads(msg("janelas"))["janelas"]
    except Exception:
        return []
    return [x for x in j if "music" in (x.get("app_id") or "").lower()]


def origem_cartao():
    """o canto de cima à direita da tela, com as margens do widget (24 à direita, 16 em cima) e a largura 372"""
    s = json.loads(msg("saidas"))
    saida = s["saidas"][0] if "saidas" in s else s[0]
    larg = saida.get("width") or saida.get("largura") or saida["rect"]["width"]
    return larg - 24 - 372, 16


def toque(x, y):
    msg("mouse", "mover", int(x), int(y))
    time.sleep(0.25)
    msg("toque", "baixo", 1, int(x), int(y))
    time.sleep(0.08)
    msg("toque", "cima", 1)
    time.sleep(0.7)


def centro(nome):
    c = caixa(nome)
    ox, oy = origem_cartao()
    return ox + c[0] + c[2] / 2, oy + c[1] + c[3] / 2


# 1. o Music toca com a janela aberta: o widget sabe, mas não mostra o cartão
confere(lambda: m().get("vivo") == "1" and float(m().get("dur") or 0) > 100, "o Music toca a faixa longa", 12)
confere(lambda: w().get("w_tem") == "1" and w().get("w_lumna") == "1", "o widget enxerga o Music pelo MPRIS", 8)
confere(lambda: len(janelas_music()) == 1, "e a janela do Music está aberta")
time.sleep(1.0)
confere(lambda: w().get("w_visivel") == "0", "com a janela aberta o cartão não aparece")

# 2. fechar a janela tocando: segundo plano, e o cartão aparece
msg("fechar", janelas_music()[0]["id"])
confere(lambda: len(janelas_music()) == 0 and m().get("esc") == "1", "a janela fecha e o Music segue em segundo plano", 6)
confere(lambda: w().get("w_visivel") == "1", "o cartão aparece na área de trabalho", 8)
confere(lambda: caixa("w-play") is not None and "Faixa" in (w().get("w_titulo") or ""), f"com o título e os botões ({w().get('w_titulo')})")

# 3. os botões mandam no Music
time.sleep(0.5)
toque(*centro("w-play"))
confere(lambda: m().get("tocando") == "0", "o botão do cartão pausa o Music")
confere(lambda: w().get("w_tocando") == "0", "e o cartão mostra pausado")
toque(*centro("w-play"))
confere(lambda: m().get("tocando") == "1", "tocar de novo")
confere(lambda: m().get("esc") == "1" and len(janelas_music()) == 0, "sem abrir a janela")

# 4. tocar no cartão (fora dos botões) abre o Music; o cartão some
c = caixa("w-cartao")
ox, oy = origem_cartao()
toque(ox + c[0] + 18, oy + c[1] + c[3] / 2)   # a capa, à esquerda
confere(lambda: len(janelas_music()) == 1 and m().get("esc") == "0", "tocar no cartão traz a janela do Music", 10)
confere(lambda: w().get("w_visivel") == "0", "e o cartão some", 8)
confere(lambda: len(subprocess.run(["pgrep", "-x", "musica"], capture_output=True, text=True).stdout.split()) == 1, "continua uma instância só")

print("musica (widget): ok" if not falhas else f"musica widget: {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
