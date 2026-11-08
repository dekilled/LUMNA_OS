#!/usr/bin/env python3
"""O explorador (lumna-arquivos) inteiro num Orbit headless, com toques de verdade (orbit-msg toque) e teclas (wtype): abrir pastas, voltar, avançar, subir,
breadcrumb, buscar, selecionar (Ctrl e Shift), teclas, lista e ordem, favoritar, nova pasta, ocultos, filtros.
python3 teste_ui.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, stdout=subprocess.DEVNULL)


def log():
    try:
        return open(f"{run}/ar.out", encoding="utf-8", errors="replace").read()
    except FileNotFoundError:
        return ""


def pos(nome):
    r = None
    for l in log().splitlines():
        m = re.match(r"pos (\S+) (-?\d+) (-?\d+) (\d+) (\d+)$", l) if " " not in nome else None
        if m is None:
            m = re.match(r"pos (.+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
        if m and m[1] == nome:
            r = tuple(int(m[i]) for i in range(2, 6))
    return r


def estado():
    r = {}
    for l in log().splitlines():
        if l.startswith("estado "):
            r = dict(re.findall(r"(\w+)=(\[[^\]]*\]|\S+)", l))
    return r


def ordem():
    r = []
    for l in log().splitlines():
        if l.startswith("ordem"):
            r = l.split(" ")[1:]
    return r


def espera(cond, seg=4):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


# a janela e onde ela está na tela (as posições do app são da janela)
def origem():
    import json
    out = subprocess.run([f"{orbit_dir}/orbit-msg", "janelas"], env=env, capture_output=True, text=True).stdout
    j = json.loads(out)["janelas"][0]
    return j["x"], j["y"]


def centro(nome):
    p = pos(nome)
    if not p:
        raise RuntimeError(f"não achei {nome} na tela")
    ox, oy = origem()
    return ox + p[0] + p[2] // 2, oy + p[1] + p[3] // 2


def toca(nome, espera_s=0.35, dedo=1):
    x, y = centro(nome)
    msg("toque", "baixo", dedo, x, y)
    time.sleep(0.08)
    msg("toque", "cima", dedo)
    time.sleep(espera_s)


def duplo(nome):
    x, y = centro(nome)
    for _ in range(2):
        msg("toque", "baixo", 1, x, y)
        time.sleep(0.06)
        msg("toque", "cima", 1)
        time.sleep(0.12)
    time.sleep(0.4)


def tecla(*teclas, mod=None):
    cmd = ["wtype", "-s", "250"]  # o -s dá tempo ao app de ver o teclado virtual novo (cada wtype cria um)
    if mod:
        cmd += ["-M", mod]
    for t in teclas:
        cmd += ["-k", t]
    if mod:
        cmd += ["-m", mod]
    subprocess.run(cmd, env=dict(os.environ), stdout=subprocess.DEVNULL)
    time.sleep(0.35)


def digita(txt):
    subprocess.run(["wtype", "-s", "250", txt], stdout=subprocess.DEVNULL)
    time.sleep(0.4)


def confere(cond, o_que):
    if not espera(cond, 3):
        falha(o_que + f" (estado {estado()}, ordem {ordem()[:8]})")


docs = f"{casa}/Documentos"
if not espera(lambda: pos("item/Projetos") is not None and estado().get("pasta") == docs, 8):
    falha("o explorador não abriu em Documentos: " + log()[:300])
    print(log()[-800:])
    sys.exit(1)
time.sleep(0.5)

# 1. a pasta: pastas primeiro, depois os arquivos
o = ordem()
if o[:6] != ["Arquivados", "Contratos", "Estudos", "Modelos", "Pessoal", "Projetos"] or "notas.md" not in o:
    falha(f"a ordem inicial não bate: {o}")

# 2. clicar seleciona; clicar de novo noutro troca; Ctrl soma
toca("item/Contratos")
confere(lambda: estado().get("sel") == "1" and "Contratos*" in ordem(), "um clique seleciona")
toca("item/Estudos")
confere(lambda: "Estudos*" in ordem() and "Contratos*" not in ordem(), "clicar noutro troca a seleção")
tecla("a", mod="ctrl")
confere(lambda: estado().get("sel") == "10", "Ctrl+A seleciona tudo")
tecla("Escape")
confere(lambda: estado().get("sel") == "0", "Esc limpa a seleção")

# 3. duplo clique abre a pasta; voltar; avançar; subir; breadcrumb
duplo("item/Projetos")
confere(lambda: estado().get("pasta") == f"{docs}/Projetos" and ordem()[:3] == ["a", "b", "c"], "duplo clique abre Projetos")
toca("t-voltar")
confere(lambda: estado().get("pasta") == docs, "voltar")
toca("t-avancar")
confere(lambda: estado().get("pasta") == f"{docs}/Projetos", "avançar")
toca("t-subir")
confere(lambda: estado().get("pasta") == docs, "subir")
duplo("item/Projetos")
confere(lambda: estado().get("pasta") == f"{docs}/Projetos", "abrir de novo")
toca("parte/0")
confere(lambda: estado().get("pasta") == casa, "clicar em Home no caminho")

# 4. a barra lateral
toca("l-recentes")
confere(lambda: estado().get("vista") == "1", "Recentes")
toca("l-favoritos")
confere(lambda: estado().get("vista") == "2" and estado().get("nvis") == "0", "Favoritos vazio")
toca("l-inicio")
confere(lambda: estado().get("vista") == "0" and estado().get("pasta") == casa, "Início")

# 5. ir para Documentos de novo (pela pasta)
duplo("item/Documentos")
confere(lambda: estado().get("pasta") == docs, "entrar em Documentos")

# 6. teclado: setas, Enter, Backspace
tecla("Right")
tecla("Right")
confere(lambda: estado().get("sel") == "1", "as setas selecionam")
sel = [x for x in ordem() if x.endswith("*")]
if sel != ["Contratos*"]:
    falha(f"depois de Right Right devia estar em Contratos (ou o primeiro selecionado): {sel}")
tecla("Down")
confere(lambda: any(x.endswith("*") for x in ordem()) and "Contratos*" not in ordem(), "a seta para baixo anda uma linha")
tecla("Home")
confere(lambda: ordem()[0] == "Arquivados*", "Home vai ao primeiro")
tecla("End")
confere(lambda: ordem()[-1].endswith("*"), "End vai ao último")
tecla("Home")
for _ in range(5):
    tecla("Right")
confere(lambda: "Projetos*" in ordem(), "cinco setas até Projetos")
tecla("Return")
confere(lambda: estado().get("pasta") == f"{docs}/Projetos", "Enter abre a pasta selecionada")
tecla("BackSpace")
confere(lambda: estado().get("pasta") == docs, "Backspace sobe")
# (o Alt é a tecla do Orbit: Alt+setas ele toma para si; o app aceita, mas aqui o teste não passa por ele)

# 7. busca (sem acento e sem caixa)
toca("busca")
digita("orcamento")
confere(lambda: estado().get("nvis") == "1" and "orçamento.ods" in ordem(), "a busca acha 'orçamento' por 'orcamento'")
tecla("Escape")
confere(lambda: estado().get("busca") == "[]" and estado().get("nvis") == "10", "Esc limpa a busca")

# 8. filtros
toca("filtro/1")
confere(lambda: estado().get("nvis") == "6" and estado().get("filtro") == "1", "filtro Pastas")
toca("filtro/0")
confere(lambda: estado().get("nvis") == "10", "filtro Tudo")

# 9. lista e ordem por tamanho
toca("t-lista")
confere(lambda: estado().get("lista") == "1" and pos("item/Projetos") is not None, "modo lista")
toca("t-ordem")
time.sleep(0.5)
toca("mo-o/2")
confere(lambda: estado().get("ordem") == "2+", "ordenar por tamanho")
toca("t-grade")
confere(lambda: estado().get("lista") == "0", "voltar para a grade")

# 10. favoritos
toca("item/Projetos")
time.sleep(0.4)
toca("det-fav")
time.sleep(0.4)
if not os.path.exists(f"{casa}/.config/lumna/arquivos/favoritos") or "Projetos" not in open(f"{casa}/.config/lumna/arquivos/favoritos").read():
    falha("favoritar não gravou o arquivo de favoritos")
toca("l-favoritos")
confere(lambda: estado().get("vista") == "2" and estado().get("nvis") == "1" and "Projetos" in ordem(), "Favoritos mostra Projetos")
toca("l-inicio")
time.sleep(0.5)
duplo("item/Documentos")
confere(lambda: estado().get("pasta") == docs, "voltar a Documentos")

# 11. nova pasta
toca("t-novo")
time.sleep(0.5)
toca("mn-pasta")
time.sleep(0.5)
tecla("a", mod="ctrl")
digita("Fotos_Eric")
tecla("Return")
confere(lambda: os.path.isdir(f"{docs}/Fotos_Eric") and "Fotos_Eric*" in ordem(), "nova pasta criada e selecionada")

# 12. a pasta muda por fora: aparece sozinha (inotify)
open(f"{docs}/chegou.txt", "w").write("oi")
confere(lambda: "chegou.txt" in ordem(), "um arquivo novo por fora aparece sozinho")
os.remove(f"{docs}/chegou.txt")
confere(lambda: "chegou.txt" not in ordem(), "e some quando é apagado por fora")

# 13. ocultos
open(f"{docs}/.segredo", "w").write("x")
time.sleep(0.5)
if ".segredo" in ordem():
    falha("arquivo oculto não devia aparecer")
tecla("h", mod="ctrl")
confere(lambda: ".segredo" in ordem() and estado().get("ocultos") == "1", "Ctrl+H mostra os ocultos")
tecla("h", mod="ctrl")
confere(lambda: ".segredo" not in ordem(), "e esconde de novo")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador de arquivos (app inteiro): ok")
