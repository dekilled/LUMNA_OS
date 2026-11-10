#!/usr/bin/env python3
"""A topbar e o LUMNA Music: o ícone de música aparece quando há um player, acende tocando, o painel traz os controles (que mandam no Music, mesmo sem janela) e
"Fechar o LUMNA Music" encerra o app."""
import json, os, re, subprocess, sys, time

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    return subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, capture_output=True, text=True).stdout


def barra():
    r = {}
    for l in open(f"{run}/b.out", encoding="utf-8", errors="replace").read().splitlines():
        if l.startswith("estado "):
            r = dict(re.findall(r"([\w-]+)=(\[[^\]]*\]|\S+)", l[7:]))
    return r


def musica():
    r = {}
    for l in open(f"{run}/g.out", encoding="utf-8", errors="replace").read().splitlines():
        if l.startswith("estado "):
            r = dict(re.findall(r"([\w-]+)=(\[[^\]]*\]|\S+)", l[7:]))
    return r


def caixa(nome):
    v = barra().get(nome)
    if not v:
        return None
    x, y, w, h = (float(n) for n in v.split(","))
    return (x, y, w, h) if w > 0 else None


def espera(c, seg=6):
    fim = time.time() + seg
    while time.time() < fim:
        if c():
            return True
        time.sleep(0.15)
    return c()


def confere(c, o_que, seg=6):
    if not espera(c, seg):
        falha(o_que + f" (barra: {({k: barra().get(k) for k in ('mus_tem', 'mus_tocando', 'mus_pop')})}, music: tocando={musica().get('tocando')} esc={musica().get('esc')})")


def pgrep():
    return subprocess.run(["pgrep", "-x", "musica"], capture_output=True, text=True).stdout.split()


def toque(x, y):
    msg("mouse", "mover", int(x), int(y))
    time.sleep(0.2)
    msg("toque", "baixo", 1, int(x), int(y))
    time.sleep(0.08)
    msg("toque", "cima", 1)
    time.sleep(0.6)


# 1. o Music toca: a barra ganha o ícone, com o título
confere(lambda: barra().get("mus_tem") == "1" and barra().get("mus_lumna") == "1" and barra().get("mus_tocando") == "1", "o ícone de música aparece e a barra sabe que toca", 10)
confere(lambda: "Faixa_longa" in (barra().get("mus_titulo") or "") or "Faixa" in (barra().get("mus_titulo") or ""), f"e sabe o título ({barra().get('mus_titulo')})")
confere(lambda: caixa("musica") is not None, "o ícone está na tela")

# 2. o painel abre ao tocar no ícone
c = caixa("musica")
toque(c[0] + c[2] / 2, c[1] + c[3] / 2)
confere(lambda: barra().get("mus_pop") == "1", "tocar no ícone abre o painel")
confere(lambda: caixa("mu-play") is not None, "com os botões de controle")

def centro(nome):
    c = caixa(nome)
    return c[0] + c[2] / 2, c[1] + c[3] / 2


def estado_musica(k):
    return musica().get(k)


# 3. os botões do painel mandam no Music, que está com a janela escondida em segundo plano
j = json.loads(msg("janelas"))["janelas"]
music_win = [x for x in j if "music" in (x.get("app_id") or "").lower()]
if music_win:
    msg("fechar", music_win[0]["id"])
confere(lambda: estado_musica("esc") == "1", "o Music vai para o segundo plano (a janela fecha tocando)")
confere(lambda: barra().get("mus_tocando") == "1", "e a barra continua mostrando que toca")
# o painel de antes pode ter fechado: abre de novo se for preciso
if barra().get("mus_pop") != "1":
    toque(*centro("musica"))
    confere(lambda: barra().get("mus_pop") == "1" and caixa("mu-play") is not None, "o painel abre")
toque(*centro("mu-play"))
confere(lambda: estado_musica("tocando") == "0", "o botão do painel pausa o Music")
confere(lambda: barra().get("mus_tocando") == "0", "e o ícone apaga")
toque(*centro("mu-play"))
confere(lambda: estado_musica("tocando") == "1", "tocar de novo pelo painel")
confere(lambda: estado_musica("esc") == "1", "sem trazer a janela de volta")

# 4. Abrir o LUMNA Music: a janela volta
toque(*centro("mu-abrir"))
confere(lambda: estado_musica("esc") == "0", "\"Abrir o LUMNA Music\" traz a janela de volta", 8)

# 5. Fechar o LUMNA Music: encerra o app, e o ícone some
if barra().get("mus_pop") != "1" or caixa("mu-sair") is None:
    toque(*centro("musica"))
    confere(lambda: caixa("mu-sair") is not None, "o painel abre de novo")
toque(*centro("mu-sair"))
confere(lambda: pgrep() == [], "\"Fechar o LUMNA Music\" encerra o app", 8)
confere(lambda: barra().get("mus_tem") == "0", "e o ícone some da barra", 8)
print("musica (topbar): ok" if not falhas else f"musica topbar: {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
