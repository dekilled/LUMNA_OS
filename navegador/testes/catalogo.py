#!/usr/bin/env python3
"""O catálogo da Mídia, de ponta a ponta: "salvar na lista" a partir de páginas de verdade (com Open Graph e JSON-LD) e a tela do catálogo na interface.

  python3 navegador/testes/catalogo.py <pasta do orbit> <pasta stage do navegador>

Confere: o título, o tipo (filme/série), o gênero e a duração lidos da página; a miniatura baixada do og:image (os mesmos bytes da capa servida) ou, sem og:image,
o print da tela; o mesmo link salvo de novo atualiza o MESMO item (favorito e "assistir mais tarde" juntos); salvar fora de um serviço é recusado; e depois a
interface (abas, filtros, cartões, abrir, favoritar, remover) pelo teclado.
"""
import atexit, json, os, re, shutil, socket, subprocess, sys, tempfile, time

orbit_dir, stage = [os.path.abspath(a) for a in sys.argv[1:3]]
aqui = os.path.dirname(os.path.abspath(__file__))
falhas, procs = [], []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def espera(cond, seg=8.0):
    fim = time.time() + seg
    while time.time() < fim:
        try:
            if cond():
                return True
        except Exception:
            pass
        time.sleep(0.1)
    try:
        return bool(cond())
    except Exception:
        return False


run = tempfile.mkdtemp()
os.chmod(run, 0o700)
for d in ("cfg", "home", "dados"):
    os.makedirs(f"{run}/{d}")
env = dict(os.environ, HOME=f"{run}/home", XDG_CONFIG_HOME=f"{run}/cfg", XDG_RUNTIME_DIR=run, LUMNA_NAV_SEM_AVISO="1")
env.pop("WAYLAND_DISPLAY", None)
atexit.register(lambda: [p.kill() for p in procs if p.poll() is None])

s = socket.socket()
s.bind(("127.0.0.1", 0))
porta = s.getsockname()[1]
s.close()
procs.append(subprocess.Popen([sys.executable, f"{aqui}/servidor.py", str(porta), f"{aqui}/paginas"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
json.dump([
    {"id": "filmes", "nome": "Filmes+", "url": f"http://127.0.0.1:{porta}/filme.html", "dominios": ["127.0.0.1"]},
    {"id": "series", "nome": "Séries+", "url": f"http://127.0.0.1:{porta}/serie.html", "dominios": ["127.0.0.1"]},
], open(f"{run}/servicos.json", "w"))

envo = dict(env, WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1", LUMNA_TESTE_TECLADOS="1")
orb = subprocess.Popen(["./orbit"], cwd=orbit_dir, env=envo, stdout=open(f"{run}/orbit.log", "w"), stderr=subprocess.STDOUT)
procs.append(orb)
time.sleep(2)
m = re.search(r"DISPLAY=(:\d+)", open(f"{run}/orbit.log").read())
if not m:
    print("catalogo: dispensado (esta wlroots não tem XWayland)")
    sys.exit(0)
display = m.group(1)
orbit_sock = [f for f in os.listdir(run) if re.match(r"orbit\..*\.sock", f)][0]


def orbit(*a):
    return subprocess.run([f"{orbit_dir}/orbit-msg", "-s", f"{run}/{orbit_sock}", *a], capture_output=True, text=True).stdout


def tecla(*codes):
    for c in codes:
        orbit("tecla_falsa", "0", str(c))
        time.sleep(0.25)


KEY = {"enter": 28, "esc": 1, "dir": 106, "esq": 105, "baixo": 108, "cima": 103, "f": 33, "w": 17, "m": 50, "a": 30, "pgdn": 109, "pgup": 104, "del": 111}

envn = dict(env, DISPLAY=display, LUMNA_NAV_DEBUG="1", LUMNA_NAV_SEM_GPU="1", LUMNA_NAV_DADOS=f"{run}/dados", LUMNA_NAV_SERVICOS=f"{run}/servicos.json")
nav_log = f"{run}/nav.log"
nav = subprocess.Popen([f"{stage}/lumna-navegador"], cwd=run, env=envn, stdout=open(nav_log, "w"), stderr=subprocess.STDOUT)
procs.append(nav)


def log():
    return open(nav_log, errors="replace").read()


def conta(p):
    return len(re.findall(p, log()))


def cli(*a):
    return subprocess.run([f"{stage}/lumna-navegador", *a], cwd=run, env=envn, capture_output=True, text=True)


def estado():
    r = cli("--estado")
    return json.loads(r.stdout) if r.stdout.strip() else {}


def catalogo():
    try:
        return json.load(open(f"{run}/dados/catalogo.json"))["itens"]
    except Exception:
        return []


def foto(nome):
    if subprocess.run(["which", "grim"], capture_output=True).returncode == 0:
        w = [f for f in os.listdir(run) if re.match(r"wayland-\d+$", f)][0]
        subprocess.run(["grim", f"{run}/{nome}.png"], env=dict(os.environ, XDG_RUNTIME_DIR=run, WAYLAND_DISPLAY=w), capture_output=True)


if not espera(lambda: "shell: desenhou" in log(), 30):
    falha("a interface não abriu: " + log()[-300:])
espera(lambda: "foco\":true" in orbit("janelas"), 15)
time.sleep(1.0)

# ---------------------------------------------------------------------------------------------------------------------------- 1. salvar fora de um serviço é recusado
cli("--salvar")
time.sleep(0.5)
if catalogo():
    falha("salvar no início (sem serviço aberto) não pode criar item")
if not espera(lambda: "salvar: só vale com um serviço aberto" in log(), 3):
    falha("salvar no início devia avisar que só vale num serviço")

# ---------------------------------------------------------------------------------------------------------------------------- 2. um filme: título, tipo, gênero, duração, capa do og:image
tecla(KEY["enter"])  # o primeiro serviço (Filmes+)
if not espera(lambda: estado().get("estado") == "servico" and estado().get("ativo") == "filmes", 12):
    falha(f"devia abrir o Filmes+: {estado()}")
espera(lambda: "site carregou http://127.0.0.1" in log() and "filme.html" in log(), 15)
time.sleep(2.0)
n = conta("catalogo salvo")
r = cli("--salvar")
if r.returncode != 0:
    falha(f"--salvar devia dar ok: {r.stdout} {r.stderr}")
if not espera(lambda: conta("catalogo salvo") > n and conta("catalogo miniatura") >= 1, 15):
    falha("salvar devia gravar o item e a miniatura: " + "|".join(re.findall(r"NAV catalogo[^\n]*|NAV salvar[^\n]*", log())))
it = (catalogo() or [{}])[0]
if it.get("titulo") != "O Filme de Teste":
    falha(f"o título vem do JSON-LD/og:title da página: {it.get('titulo')!r}")
if it.get("tipo") != "filme":
    falha(f"og:type video.movie / Movie vira tipo filme: {it.get('tipo')!r}")
if it.get("genero") != "Aventura":
    falha(f"o gênero vem do JSON-LD (o primeiro): {it.get('genero')!r}")
if abs(it.get("duracao_min", 0) - 138) > 1:
    falha(f"a duração PT2H18M são 138 min: {it.get('duracao_min')}")
if not it.get("depois") or it.get("favorito"):
    falha(f"salvar sem marca é 'assistir mais tarde': {it}")
if it.get("servico") != "filmes" or "filme.html" not in it.get("url", ""):
    falha(f"o serviço e o link ficam no item: {it}")
mini = f"{run}/dados/{it.get('miniatura', 'nao-existe')}"
capa = open(f"{aqui}/paginas/capa.jpg", "rb").read()
if not os.path.exists(mini) or open(mini, "rb").read() != capa:
    falha(f"a miniatura devia ser a capa do og:image (bytes iguais): {it.get('miniatura')}")

# ---------------------------------------------------------------------------------------------------------------------------- 3. o mesmo link de novo: o MESMO item (favorito + depois)
cli("--salvar", "favorito")
if not espera(lambda: (catalogo() or [{}])[0].get("favorito") is True, 8):
    falha("salvar favorito devia marcar o favorito")
cat = catalogo()
if len(cat) != 1 or not (cat[0].get("depois") and cat[0].get("favorito")):
    falha(f"o mesmo link vira o mesmo item, com favorito e depois juntos: {cat}")

# ---------------------------------------------------------------------------------------------------------------------------- 4. uma série sem og:image: a miniatura é o print da tela
cli("--inicio")
espera(lambda: estado().get("estado") == "inicio", 8)
time.sleep(1.5)
tecla(KEY["dir"], KEY["enter"])  # o segundo serviço
if not espera(lambda: estado().get("estado") == "servico" and estado().get("ativo") == "series", 12):
    falha(f"devia abrir o Séries+: {estado()}")
espera(lambda: "serie.html" in log(), 10)
time.sleep(3.0)
n = conta("catalogo salvo")
cli("--salvar", "assistido")
if not espera(lambda: conta("catalogo salvo") > n and conta("catalogo miniatura") >= 2, 15):
    falha("a série devia ser salva com o print como miniatura: " + "|".join(re.findall(r"NAV catalogo[^\n]*", log())[-4:]))
cat = catalogo()
se = [i for i in cat if i.get("servico") == "series"]
if not se:
    falha(f"a série devia estar no catálogo: {cat}")
else:
    se = se[0]
    if se.get("titulo") != "Série de Teste" or se.get("tipo") != "serie" or se.get("genero") != "Drama" or not se.get("assistido") or se.get("depois"):
        falha(f"a série: título, tipo, gênero e 'assistido': {se}")
    mini = f"{run}/dados/{se.get('miniatura', 'nao-existe')}"
    if not os.path.exists(mini) or os.path.getsize(mini) < 2000 or not open(mini, "rb").read(3) == b"\xff\xd8\xff":
        falha(f"sem og:image a miniatura é o print (um JPEG): {se.get('miniatura')}")

# ---------------------------------------------------------------------------------------------------------------------------- 5. a tela do catálogo (pelo teclado, como o controle)
cli("--inicio")
espera(lambda: estado().get("estado") == "inicio", 8)
time.sleep(1.5)
foto("catalogo-0-inicio")


def ultimo_cat():
    m_ = re.findall(r"NAV shell: catalogo: (\d+)/(\d+) itens tipo=(\w+) lista=(\w+) genero=(\S+) busca=(\S+) ordem=(\w+) visao=(\w+)", log())
    return m_[-1] if m_ else None


def espera_cat(**kw):
    chaves = ["vis", "tot", "tipo", "lista", "genero", "busca", "ordem", "visao"]
    def ok():
        u = ultimo_cat()
        return u and all(str(dict(zip(chaves, u))[k]) == str(v) for k, v in kw.items())
    return espera(ok, 6)


# do início: para cima até as abas (os "Continuar assistindo" ficam no meio), uma aba para a direita (Filmes) e Enter
tecla(KEY["cima"], KEY["cima"], KEY["dir"], KEY["enter"])
if not espera_cat(vis=1, tot=2, tipo="filme"):
    falha(f"a aba Filmes mostra só os filmes (1 de 2): {ultimo_cat()}")
foto("catalogo-1-filmes")
tecla(KEY["dir"], KEY["enter"])
if not espera_cat(vis=1, tot=2, tipo="serie"):
    falha(f"a aba Séries mostra só as séries: {ultimo_cat()}")
tecla(KEY["pgup"])   # LB: a aba anterior (Filmes)
if not espera_cat(tipo="filme"):
    falha(f"Page Up (LB) volta para a aba anterior: {ultimo_cat()}")
tecla(KEY["baixo"])   # para os filtros (o primeiro é o "Todos")
tecla(KEY["enter"])
if not espera_cat(vis=2, tot=2, tipo="todos"):
    falha(f"o filtro 'Todos' mostra os dois itens: {ultimo_cat()}")
foto("catalogo-2-todos")
# a "Minhas listas" é o último dos filtros: Enter troca a lista (Assistir mais tarde, Favoritos, Já assisti, Todos)
tecla(*([KEY["esq"]] * 0))
for _ in range(9):
    tecla(KEY["dir"])
tecla(KEY["enter"])
if not espera_cat(lista="depois", vis=1):
    falha(f"'Minhas listas' > Assistir mais tarde mostra só o filme (o 'depois'): {ultimo_cat()}")
tecla(KEY["enter"])
if not espera_cat(lista="favoritos", vis=1):
    falha(f"... depois Favoritos (só o filme): {ultimo_cat()}")
tecla(KEY["enter"])
if not espera_cat(lista="assistidos", vis=1):
    falha(f"... depois Já assisti (só a série): {ultimo_cat()}")
tecla(KEY["enter"])
if not espera_cat(lista="todos", vis=2):
    falha(f"... e volta para Todos: {ultimo_cat()}")

# busca (digitando), gênero, ordem e a visão em lista: os filtros em ordem são Todos, Filmes, Séries, Vídeos, busca, gênero, ordem, grade, lista, Minhas listas
for _ in range(9):
    tecla(KEY["esq"])
tecla(KEY["dir"], KEY["dir"], KEY["dir"], KEY["dir"], KEY["enter"])   # a busca: o foco vai para o campo
tecla(33, 23, 38)   # digita "fil" (o teclado físico; o controle vai usar o teclado na tela, que ainda não existe)
if not espera_cat(busca="fil", vis=1):
    falha(f"digitar 'fil' na busca filtra (só o filme): {ultimo_cat()}")
tecla(KEY["enter"])   # sai do campo
tecla(KEY["enter"])   # entra de novo
tecla(14, 14, 14)     # Backspace x3
if not espera_cat(busca="-", vis=2):
    falha(f"apagar a busca volta aos dois itens: {ultimo_cat()}")
tecla(KEY["enter"])   # sai do campo
tecla(KEY["dir"])
tecla(KEY["enter"])   # o gênero: Aventura
if not espera_cat(genero="Aventura", vis=1):
    falha(f"o filtro de gênero mostra só 'Aventura': {ultimo_cat()}")
tecla(KEY["enter"], KEY["enter"])   # Drama, e depois (volta) todos os gêneros
if not espera_cat(genero="-", vis=2):
    falha(f"o gênero volta a todos depois de percorrer a lista: {ultimo_cat()}")
tecla(KEY["dir"], KEY["enter"])   # a ordem: alfabética
if not espera_cat(ordem="az"):
    falha(f"a ordem alfabética: {ultimo_cat()}")
tecla(KEY["enter"])               # (volta a recentes)
tecla(KEY["dir"], KEY["dir"], KEY["enter"])   # a visão em lista
if not espera_cat(visao="lista"):
    falha(f"a visão em lista: {ultimo_cat()}")
foto("catalogo-6-lista")
tecla(KEY["esq"], KEY["enter"])   # de volta à grade
if not espera_cat(visao="grade"):
    falha(f"a visão em grade: {ultimo_cat()}")

# a grade: o primeiro cartão (o mais recente: a série) e os atalhos do cartão
for _ in range(9):
    tecla(KEY["esq"])
tecla(KEY["baixo"])   # a grade (o primeiro cartão)
foto("catalogo-3-grade")
# os três pontinhos (···) do cartão, com o mouse de verdade: abrem o MENU DE AÇÕES e não o link (o bug: o clique caía no cartão inteiro e abria o serviço)
mr = re.findall(r"mais-rect (\w+) (\d+) (\d+)", log())
id0, mx, my = mr[-1]
jn = [j for j in json.loads(orbit("janelas")).get("janelas", []) if j.get("app_id") == "lumna-navegador"][0]
orbit("mouse", "mover", str(jn["x"] + int(mx)), str(jn["y"] + int(my)))
time.sleep(0.5)
orbit("mouse", "botao", "baixo"); time.sleep(0.12); orbit("mouse", "botao", "cima")
if not espera(lambda: f"acoes abertas {id0}" in log(), 6):
    falha("clicar nos três pontinhos devia abrir o menu de ações do cartão")
if estado().get("estado") != "inicio":
    falha(f"clicar nos três pontinhos NÃO pode abrir o link do serviço: {estado()}")
tecla(KEY["esc"])      # fecha o menu de ações
time.sleep(0.4)
tecla(KEY["dir"])     # o segundo cartão: o filme
tecla(KEY["f"])       # favoritar/desfavoritar o filme (era favorito)
if not espera(lambda: [i for i in catalogo() if i["servico"] == "filmes"][0]["favorito"] is False, 6):
    falha("F no cartão tira o favorito")
tecla(KEY["f"])
if not espera(lambda: [i for i in catalogo() if i["servico"] == "filmes"][0]["favorito"] is True, 6):
    falha("F de novo volta a favoritar")
tecla(KEY["m"])       # o menu de ações
foto("catalogo-4-acoes")
tecla(KEY["baixo"], KEY["baixo"], KEY["enter"])   # "Marcar como assistido"
if not espera(lambda: (lambda i: i["assistido"] is True and i["depois"] is False)([i for i in catalogo() if i["servico"] == "filmes"][0]), 6):
    falha("o menu de ações marca como assistido (e tira do 'assistir mais tarde')")
# Enter no cartão abre o serviço no link salvo
n = conta(r"estado servico filmes")
tecla(KEY["enter"])
if not espera(lambda: conta(r"estado servico filmes") > n and estado().get("ativo") == "filmes", 12):
    falha(f"Enter num cartão devia abrir o serviço no link salvo: {estado()}")
espera(lambda: "filme.html" in log()[-3000:], 8)
cli("--inicio")
espera(lambda: estado().get("estado") == "inicio", 8)
time.sleep(1.5)
# de volta ao início o catálogo continua onde estava (a grade), e remover pelo menu de ações
tecla(KEY["m"], KEY["baixo"], KEY["baixo"], KEY["baixo"], KEY["enter"])   # "Remover do catálogo"
if not espera(lambda: len(catalogo()) == 1 and catalogo()[0]["servico"] == "series", 6):
    falha(f"remover tira o item do catálogo (e a miniatura): {[i['titulo'] for i in catalogo()]}")
if [f for f in os.listdir(f"{run}/dados/thumbs") if f.startswith("cat-") and not f.startswith("cat-" + catalogo()[0]["id"])]:
    falha(f"a miniatura do item removido é apagada: {os.listdir(run + '/dados/thumbs')}")
foto("catalogo-5-depois-de-remover")

# 6. B (Esc) no catálogo volta ao início COM o catálogo escondido de verdade e a página sem rolagem (o bug: o catálogo ficava por baixo do início e a tela empilhava)
tecla(KEY["esc"])
if not espera(lambda: "vista inicio: catalogo=none home=flex rolagem=0" in log(), 6):
    falha("Esc no catálogo devia voltar ao início com o catálogo escondido (display none) e sem rolagem: " + "|".join(re.findall(r"vista inicio[^\n]*", log())[-2:]))
foto("catalogo-7-esc")

# (a interface do catálogo entra na próxima parte do teste)

for p in procs:
    p.kill()
if os.environ.get("CATALOGO_PRINTS"):   # guarda os prints (para olhar o visual): CATALOGO_PRINTS=/pasta
    os.makedirs(os.environ["CATALOGO_PRINTS"], exist_ok=True)
    for f in os.listdir(run):
        if f.endswith(".png"):
            shutil.copy(f"{run}/{f}", os.environ["CATALOGO_PRINTS"])
if falhas:
    print(f"(log em {run}: nav.log e os prints)", file=sys.stderr)
    sys.exit(1)
shutil.rmtree(run, ignore_errors=True)
print("catalogo: ok")
