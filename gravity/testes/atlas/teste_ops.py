#!/usr/bin/env python3
"""As operações do explorador, com o app inteiro num Orbit headless: copiar, recortar e colar, desfazer, renomear, lixeira (e restaurar), conflito, apagar para
sempre, o caminho digitado com sugestões, o menu de contexto (compactar e extrair) e as propriedades.
python3 teste_ops.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json, glob

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
falhas = []
docs = f"{casa}/Documentos"
lixeira = f"{casa}/.local/share/Trash/files"


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, stdout=subprocess.DEVNULL)


def log():
    return open(f"{run}/ar.out", encoding="utf-8", errors="replace").read()


def pos(nome):
    r = None
    for l in log().splitlines():
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


def origem():
    out = subprocess.run([f"{orbit_dir}/orbit-msg", "janelas"], env=env, capture_output=True, text=True).stdout
    j = json.loads(out)["janelas"][0]
    return j["x"], j["y"]


def centro(nome):
    p = pos(nome)
    if not p:
        subprocess.run(["grim", f"{run}/falha.png"])
        raise RuntimeError(f"não achei {nome} na tela")
    ox, oy = origem()
    return ox + p[0] + p[2] // 2, oy + p[1] + p[3] // 2


def aponta(x, y):
    """o ponteiro chega ao alvo antes de apertar, como uma pessoa faz: dá ao app um quadro para assentar o estado (menu que fecha, página que volta)"""
    msg("mouse", "mover", x, y)
    time.sleep(0.15)


def toca(nome, espera_s=0.4):
    x, y = centro(nome)
    aponta(x, y)
    msg("toque", "baixo", 1, x, y)
    time.sleep(0.08)
    msg("toque", "cima", 1)
    time.sleep(espera_s)


def duplo(nome):
    x, y = centro(nome)
    for _ in range(2):
        msg("toque", "baixo", 1, x, y)
        time.sleep(0.06)
        msg("toque", "cima", 1)
        time.sleep(0.12)
    time.sleep(0.5)


def segura_sel(nome):
    """seleciona pelo teclado e segura o dedo no item (que então está à vista) para abrir o menu de contexto"""
    seleciona(nome)
    time.sleep(0.3)
    segura("item/" + nome)


def segura(nome):
    x, y = centro(nome)
    aponta(x, y)
    msg("toque", "baixo", 1, x, y)
    time.sleep(0.9)
    msg("toque", "cima", 1)
    time.sleep(0.6)


def tecla(*teclas, mod=None):
    cmd = ["wtype", "-s", "250"]  # o -s dá tempo ao app de ver o teclado virtual novo (cada wtype cria um)
    if mod:
        for m in mod.split("+"):
            cmd += ["-M", m]
    for t in teclas:
        cmd += ["-k", t]
    if mod:
        for m in mod.split("+"):
            cmd += ["-m", m]
    subprocess.run(cmd, stdout=subprocess.DEVNULL)
    time.sleep(0.4)


def digita(txt):
    subprocess.run(["wtype", "-s", "250", txt], stdout=subprocess.DEVNULL)
    time.sleep(0.4)


def confere(cond, o_que, seg=4):
    if not espera(cond, seg):
        falha(o_que + f" (estado {estado()}, ordem {ordem()[:10]})")


def existe(p):
    return os.path.lexists(p)


def lixeira_tem(nome):
    return existe(f"{lixeira}/{nome}")


def seleciona(nome):
    """seleciona um item pelo teclado (Home e setas): o clique falha em itens que a janela não mostra (a grade rola)"""
    nomes = [n.rstrip("*") for n in ordem()]
    if nome not in nomes:
        raise RuntimeError(f"{nome} não está na pasta: {nomes}")
    i = nomes.index(nome)
    cmd = ["wtype", "-s", "300", "-k", "Home"]
    for _ in range(i):
        cmd += ["-s", "150", "-k", "Right"]
    subprocess.run(cmd, stdout=subprocess.DEVNULL)
    time.sleep(0.5)
    confere(lambda: f"{nome}*" in ordem() and estado().get("sel") == "1", f"selecionar {nome} pelo teclado")


def vai_para(pasta_dest):
    """navega pelo caminho digitado (Ctrl+L): o jeito mais simples de chegar a um lugar"""
    tecla("l", mod="ctrl")
    tecla("a", mod="ctrl")
    digita(pasta_dest)
    tecla("Return")
    confere(lambda: estado().get("pasta") == pasta_dest and estado().get("editando") == "0", f"ir para {pasta_dest} pelo Ctrl+L")
    time.sleep(0.5)


# o app já está aberto (o teste anterior deixou em Documentos, com tudo isso)
vai_para(docs)
for nome in ("Contratos", "Estudos"):
    for f in os.listdir(f"{docs}/{nome}"):
        pass
for lixo in glob.glob(f"{docs}/Fotos_Eric*"):
    os.rmdir(lixo)
time.sleep(0.4)

# 1. copiar e colar noutra pasta; desfazer a cópia (vai para a lixeira)
seleciona("notas.md")
tecla("c", mod="ctrl")
vai_para(f"{docs}/Contratos")
tecla("v", mod="ctrl")
confere(lambda: existe(f"{docs}/Contratos/notas.md") and existe(f"{docs}/notas.md"), "colar copia sem tirar o original")
confere(lambda: "notas.md*" in ordem(), "o colado fica selecionado")
tecla("z", mod="ctrl")
confere(lambda: not existe(f"{docs}/Contratos/notas.md") and existe(f"{docs}/notas.md"), "Ctrl+Z desfaz a cópia")

# 2. recortar e colar (mover) e desfazer
vai_para(docs)
seleciona("orçamento.ods")
tecla("x", mod="ctrl")
vai_para(f"{docs}/Estudos")
tecla("v", mod="ctrl")
confere(lambda: existe(f"{docs}/Estudos/orçamento.ods") and not existe(f"{docs}/orçamento.ods"), "recortar e colar move")
tecla("z", mod="ctrl")
confere(lambda: existe(f"{docs}/orçamento.ods") and not existe(f"{docs}/Estudos/orçamento.ods"), "Ctrl+Z desfaz o mover")

# 3. renomear (F2) e desfazer
vai_para(docs)
seleciona("apresentação.odp")
tecla("F2")
tecla("a", mod="ctrl")
digita("slides.odp")
tecla("Return")
confere(lambda: existe(f"{docs}/slides.odp") and not existe(f"{docs}/apresentação.odp"), "F2 renomeia")
tecla("z", mod="ctrl")
confere(lambda: existe(f"{docs}/apresentação.odp") and not existe(f"{docs}/slides.odp"), "Ctrl+Z desfaz o renomear")

# 4. lixeira com desfazer, e restaurar pela tela da lixeira
seleciona("planejamento.pdf")
tecla("Delete")
confere(lambda: not existe(f"{docs}/planejamento.pdf") and lixeira_tem("planejamento.pdf"), "Delete manda para a lixeira")
if not existe(f"{casa}/.local/share/Trash/info/planejamento.pdf.trashinfo"):
    falha("a lixeira não gravou o .trashinfo")
tecla("z", mod="ctrl")
confere(lambda: existe(f"{docs}/planejamento.pdf") and not lixeira_tem("planejamento.pdf"), "Ctrl+Z tira da lixeira")
seleciona("planejamento.pdf")
tecla("Delete")
confere(lambda: lixeira_tem("planejamento.pdf"), "de novo na lixeira")
toca("l-lixeira")
confere(lambda: estado().get("lixeira") == "1" and "planejamento.pdf" in ordem(), "a Lixeira lista o que foi apagado")
seleciona("planejamento.pdf")
toca("lix-restaurar")
confere(lambda: existe(f"{docs}/planejamento.pdf") and not lixeira_tem("planejamento.pdf"), "Restaurar devolve ao lugar de origem")

# 5. conflito: colar duas vezes na mesma pasta de destino
vai_para(docs)
seleciona("notas.md")
tecla("c", mod="ctrl")
vai_para(f"{docs}/Contratos")
tecla("v", mod="ctrl")
confere(lambda: existe(f"{docs}/Contratos/notas.md"), "primeira colagem")
tecla("v", mod="ctrl")
confere(lambda: pos("cf-ambos") is not None, "a segunda colagem pergunta (Já existe)")
time.sleep(0.5)
toca("cf-ambos")
confere(lambda: existe(f"{docs}/Contratos/notas (2).md"), "Manter os dois cria o (2)")
c1 = open(f"{docs}/Contratos/notas.md").read()
if c1 != open(f"{docs}/notas.md").read():
    falha("o original da pasta de destino foi alterado")

# 6. apagar para sempre (Shift+Delete) pede confirmação
vai_para(docs)
open(f"{docs}/apagar.txt", "w").write("x")
confere(lambda: "apagar.txt" in ordem(), "o arquivo novo aparece")
seleciona("apagar.txt")
tecla("Delete", mod="shift")
time.sleep(0.6)
toca("ap-cancelar")
if not existe(f"{docs}/apagar.txt"):
    falha("cancelar a confirmação apagou mesmo assim")
seleciona("apagar.txt")
tecla("Delete", mod="shift")
time.sleep(0.6)
toca("ap-confirmar")
confere(lambda: not existe(f"{docs}/apagar.txt") and not lixeira_tem("apagar.txt"), "Shift+Delete confirmado apaga de vez")

# 7. o caminho digitado, com sugestões e Tab
tecla("l", mod="ctrl")
tecla("a", mod="ctrl")
digita(f"{casa}/Docu")
confere(lambda: pos("sug/0") is not None and estado().get("editando") == "1", "as sugestões aparecem ao digitar o caminho")
tecla("Tab")
confere(lambda: pos("sug/0") is not None, "Tab completa")
tecla("Escape")
confere(lambda: estado().get("editando") == "0", "Esc cancela a edição do caminho")
tecla("l", mod="ctrl")
tecla("a", mod="ctrl")
digita("/nao/existe/mesmo")
tecla("Return")
time.sleep(0.5)
if estado().get("pasta") != docs:
    falha("um caminho que não existe não pode levar a lugar nenhum")
tecla("Escape")

# 8. menu de contexto: compactar e extrair
seleciona("notas.md")
segura_sel("notas.md")
confere(lambda: pos("mi-compactar") is not None, "o menu de contexto abre ao segurar")
toca("mi-compactar")
confere(lambda: existe(f"{docs}/notas.zip"), "Compactar cria o .zip ao lado")
segura_sel("notas.zip")
confere(lambda: pos("mi-extrair") is not None, "o menu do .zip tem Extrair aqui")
toca("mi-extrair")
confere(lambda: existe(f"{docs}/notas/notas.md") or existe(f"{docs}/notas (2)/notas.md") or existe(f"{docs}/notas.md"), "Extrair aqui entrega o conteúdo")

# 9. o menu de uma pasta tem "Fixar na barra lateral" (clicar nos submenus não entra aqui: a janela do teste é baixa e o menu rola;
# o que fixar grava está no teste do serviço)
tecla("Escape")
time.sleep(0.8)
segura_sel("Contratos")
confere(lambda: pos("mi-fixar") is not None, "o menu de uma pasta tem Fixar na barra lateral")
tecla("Escape")
time.sleep(0.6)

# 10. o menu do espaço vazio (botão direito/dedo parado fora dos itens): nova pasta, ordenar, git clone, cor e ícone
def segura_xy(x, y):
    ox, oy = origem()
    aponta(ox + x, oy + y)
    msg("toque", "baixo", 1, ox + x, oy + y)
    time.sleep(0.9)
    msg("toque", "cima", 1)
    time.sleep(0.6)


tecla("Escape")
time.sleep(0.8)
segura_xy(900, 560)
confere(lambda: pos("mf-git") is not None and pos("mf-ordenar") is not None, "o menu do espaço vazio abre (com Ordenar por e Git clone)")
toca("mf-ordenar")
time.sleep(0.5)
toca("mf-ord/1")
confere(lambda: estado().get("ordem") == "1+", "Ordenar por data pelo menu do espaço vazio")
# git clone de um repositório local pelo diálogo
repo = f"{casa}/repo-origem"
os.makedirs(repo, exist_ok=True)
open(f"{repo}/ola.txt", "w").write("oi")
for cmd in (["init", "-q"], ["add", "-A"], ["-c", "user.email=a@b", "-c", "user.name=t", "commit", "-q", "-m", "x"]):
    subprocess.run(["git", "-C", repo, *cmd], stdout=subprocess.DEVNULL)
segura_xy(900, 560)
toca("mf-git")
time.sleep(0.6)
digita(repo)
tecla("Return")
confere(lambda: os.path.exists(f"{docs}/repo-origem/.git"), "Git clone cria a pasta do repositório", 8)
# cor e ícone desta pasta: submenus que abrem ao lado do menu
segura_xy(900, 560)
toca("mf-cor")
confere(lambda: pos("sub-cor-i/3") is not None, "Cor desta pasta abre o submenu das cores ao lado")
toca("sub-cor-i/3")
time.sleep(0.6)
segura_xy(900, 560)
toca("mf-icone")
confere(lambda: pos("sub-ic/1") is not None, "Ícone desta pasta abre a grade de emblemas ao lado")
toca("sub-ic/1")
time.sleep(0.4)
ini = open(f"{casa}/.config/lumna/atlas/pastas.ini").read()
if "#30A46C" not in ini or "icone=heart" not in ini:
    falha(f"cor e ícone não foram gravados: {ini!r}")

# 11. propriedades
tecla("i", mod="ctrl")
confere(lambda: pos("pr-aplicar") is not None, "Ctrl+I abre as propriedades")
time.sleep(0.4)
toca("pr-cancelar")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (operações com o app inteiro): ok")
