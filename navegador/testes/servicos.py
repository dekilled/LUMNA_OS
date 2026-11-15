#!/usr/bin/env python3
"""Os streamings do usuário na Mídia: o botão "+ Adicionar" (a biblioteca de streamings conhecidos e "Outro site" digitando), o favorito (vai para o começo e ganha o
foco inicial), o modo de controle de cada um, remover, o comando `lumna-navegador --adicionar`, e a lista guardada em servicos.json que SOBREVIVE a reiniciar o app.

  python3 navegador/testes/servicos.py <pasta do orbit> <pasta stage do navegador>
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
s = socket.socket(); s.bind(("127.0.0.1", 0)); porta = s.getsockname()[1]; s.close()
procs.append(subprocess.Popen([sys.executable, f"{aqui}/servidor.py", str(porta), f"{aqui}/paginas"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
json.dump([
    {"id": "alfa", "nome": "Serviço Alfa", "url": f"http://127.0.0.1:{porta}/a.html", "dominios": ["127.0.0.1"], "cor": "#ff8844"},
    {"id": "beta", "nome": "Serviço Beta", "url": f"http://127.0.0.1:{porta}/b.html", "dominios": ["127.0.0.1"]},
], open(f"{run}/servicos.json", "w"))
envo = dict(env, LUMNA_TESTE_TECLADOS="1", WLR_BACKENDS="headless", WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="pixman", WLR_LIBINPUT_NO_DEVICES="1")
orb = subprocess.Popen(["./orbit"], cwd=orbit_dir, env=envo, stdout=open(f"{run}/orbit.log", "w"), stderr=subprocess.STDOUT)
procs.append(orb)
time.sleep(2)
m = re.search(r"DISPLAY=(:\d+)", open(f"{run}/orbit.log").read())
if not m:
    print("servicos: dispensado (esta wlroots não tem XWayland)")
    sys.exit(0)
display = m.group(1)
orbit_sock = [f for f in os.listdir(run) if re.match(r"orbit\..*\.sock", f)][0]


def orbit(*a):
    return subprocess.run([f"{orbit_dir}/orbit-msg", "-s", f"{run}/{orbit_sock}", *a], capture_output=True, text=True).stdout


def tecla(*codes):
    for c in codes:
        orbit("tecla_falsa", "0", str(c))
        time.sleep(0.22)


K = {"enter": 28, "esc": 1, "dir": 106, "esq": 105, "baixo": 108, "cima": 103, "m": 50, "bksp": 14, "t": 20, "v": 47, ".": 52, "c": 46, "o": 24, "m_": 50, "b": 48, "r": 19, "l": 38, "e": 18, "s": 31}
envn = dict(env, DISPLAY=display, LUMNA_NAV_DEBUG="1", LUMNA_NAV_SEM_GPU="1", LUMNA_NAV_DADOS=f"{run}/dados", LUMNA_NAV_SERVICOS=f"{run}/servicos.json")
nav = None


def abre():
    global nav
    n = len(re.findall("shell: desenhou", log())) if os.path.exists(f"{run}/nav.log") else 0
    nav = subprocess.Popen([f"{stage}/lumna-navegador"], cwd=run, env=envn, stdout=open(f"{run}/nav.log", "a"), stderr=subprocess.STDOUT)
    procs.append(nav)
    if not espera(lambda: len(re.findall("shell: desenhou", log())) > n, 30):
        falha("a interface não abriu")
    espera(lambda: "foco\":true" in orbit("janelas"), 15)
    time.sleep(1.0)


def log():
    return open(f"{run}/nav.log", errors="replace").read()


def cli(*a):
    return subprocess.run([f"{stage}/lumna-navegador", *a], cwd=run, env=envn, capture_output=True, text=True)


def lista():
    try:
        return json.load(open(f"{run}/dados/servicos.json"))["servicos"]
    except Exception:
        return None


def foco():
    mm = re.findall(r"NAV shell: foco zona=(\w+) idx=(\d+)/(\d+)", log())
    return mm[-1] if mm else None


abre()

# ---------------------------------------------------------------------------------------------------------------------------- 1. a fila tem o "+ Adicionar" no fim; nada é gravado até a primeira mudança
if lista() is not None:
    falha("sem mudança nenhuma o servicos.json não existe (a lista vem do padrão)")
tecla(K["dir"], K["dir"])   # alfa, beta, + : o terceiro é o "+ Adicionar"
if not espera(lambda: foco() == ("servicos", "2", "3"), 4):
    falha(f"a fila tem 2 serviços e o '+ Adicionar' (3 itens): {foco()}")
tecla(K["enter"])
if not espera(lambda: "biblioteca aberta: 17 itens, 17 disponiveis" in log(), 6):
    falha("o '+' abre a biblioteca com os 17 streamings conhecidos: " + "|".join(re.findall(r"biblioteca aberta[^\n]*", log())))
# ---------------------------------------------------------------------------------------------------------------------------- 2. adicionar o primeiro da biblioteca (o YouTube, que é de TV)
tecla(K["enter"])
if not espera(lambda: lista() and [x["id"] for x in lista()] == ["alfa", "beta", "youtube"], 6):
    falha(f"Enter no YouTube da biblioteca o adiciona ao fim da lista: {lista()}")
yt = [x for x in (lista() or []) if x["id"] == "youtube"]
if yt and not (yt[0]["ua_tv"] is True and yt[0]["voltar"] == "esc" and yt[0]["url"].endswith("/youtube.com/tv".replace("/youtube", "youtube")) or yt[0]["url"] == "https://www.youtube.com/tv"):
    falha(f"o YouTube é a interface de TV (ua_tv, voltar=esc): {yt}")
if not espera(lambda: foco() == ("servicos", "2", "4"), 4):
    falha(f"o foco vai para o serviço recém-adicionado: {foco()}")
# o mesmo de novo não duplica: a biblioteca marca os que já estão
tecla(K["dir"], K["enter"])   # o "+" agora é o quarto
if not espera(lambda: "biblioteca aberta: 17 itens, 16 disponiveis" in log(), 6):
    falha("a biblioteca marca o YouTube como já adicionado (16 disponíveis)")
# ---------------------------------------------------------------------------------------------------------------------------- 3. "Outro site" digitando: nome "tv", endereço "tv.com.br"
for _ in range(10):
    tecla(K["baixo"])
tecla(K["dir"], K["dir"], K["dir"])   # o último da biblioteca (a grade tem linhas e colunas): "Outro site…"
tecla(K["enter"])
tecla(K["t"], K["v"], K["enter"])                                              # nome: tv
tecla(K["t"], K["v"], K["."], K["c"], K["o"], K["m"], K["."], K["b"], K["r"], K["enter"])   # endereço: tv.com.br
tecla(K["enter"])   # o botão "Adicionar"
if not espera(lambda: lista() and lista()[-1]["id"] == "tv", 6):
    falha(f"'Outro site' adiciona o site digitado: {lista()}")
tv = [x for x in (lista() or []) if x["id"] == "tv"]
if tv and not (tv[0]["nome"] == "tv" and tv[0]["url"] == "https://tv.com.br" and tv[0]["dominios"] == ["tv.com.br"] and tv[0]["modo"] == "setas"):
    falha(f"o site digitado: nome, https:// colocado, domínio tv.com.br e modo setas: {tv}")
# um endereço inválido é recusado
n = len(lista() or [])
cli("--adicionar", "Sem ponto", "semponto")
time.sleep(0.8)
if len(lista() or []) != n:
    falha("um endereço sem domínio (sem ponto) é recusado")
# ---------------------------------------------------------------------------------------------------------------------------- 4. o comando --adicionar (o celular, o Connect, um atalho)
r = cli("--adicionar", "Minha", "TV", "Local", "www.minhatvlocal.com.br")
if r.returncode != 0:
    falha(f"--adicionar devia dar ok: {r.stdout} {r.stderr}")
if not espera(lambda: lista() and lista()[-1]["id"] == "minha-tv-local" and lista()[-1]["dominios"] == ["minhatvlocal.com.br"], 6):
    falha(f"--adicionar 'Minha TV Local' www.minhatvlocal.com.br: {lista()[-1:] if lista() else None}")
# ---------------------------------------------------------------------------------------------------------------------------- 5. favorito: vai para o começo; o modo de controle; remover (pelo menu de ações, a tecla m)
cli("--inicio"); time.sleep(1.5)
tecla(K["esq"] * 0 or K["esq"], K["esq"], K["esq"], K["esq"], K["esq"], K["esq"])   # volta ao primeiro serviço
tecla(K["dir"])   # o segundo: beta
tecla(K["m"])     # as ações do Beta
if not espera(lambda: "acoes de servico beta" in log(), 4):
    falha("m num serviço abre o menu de ações dele")
tecla(K["enter"])   # "Tornar favorito"
if not espera(lambda: lista() and lista()[0]["id"] == "beta" and lista()[0]["favorito"] is True and sum(1 for x in lista() if x["favorito"]) == 1, 6):
    falha(f"favorito: o Beta vai para o começo e é o único favorito: {[(x['id'], x['favorito']) for x in (lista() or [])]}")
tecla(K["m"], K["baixo"], K["enter"])   # o foco continua no Beta (agora o primeiro): "Controle: usar o cursor"
if not espera(lambda: [x for x in (lista() or []) if x["id"] == "beta"][0]["modo"] == "cursor", 6):
    falha("o menu de ações troca o modo de controle do serviço para 'cursor'")
tecla(K["m"], K["baixo"], K["baixo"], K["enter"])   # "Remover da lista"
if not espera(lambda: lista() and "beta" not in [x["id"] for x in lista()], 6):
    falha(f"remover tira o serviço da lista: {[x['id'] for x in (lista() or [])]}")
# ---------------------------------------------------------------------------------------------------------------------------- 6. a lista SOBREVIVE a reiniciar o app; o favorito é o primeiro e tem o foco inicial
cli("--menu")   # (nada)
tecla(K["m"])   # (o Alfa em foco: favorito)
tecla(K["enter"])
if not espera(lambda: lista() and lista()[0]["id"] == "alfa" and lista()[0]["favorito"] is True, 6):
    falha("o Alfa vira o favorito")
antes = [x["id"] for x in lista()]
nav.kill(); nav.wait()
time.sleep(1.0)
abre()
if [x["id"] for x in lista()] != antes:
    falha(f"a lista é a mesma depois de reiniciar: {antes} → {[x['id'] for x in lista()]}")
if foco() != ("servicos", "0", str(len(antes) + 1)):
    falha(f"o foco inicial é o favorito (o primeiro da fila): {foco()}")
tecla(K["enter"])
if not espera(lambda: cli("--estado").stdout.find('"ativo":"alfa"') >= 0, 12):
    falha(f"Enter no favorito o abre: {cli('--estado').stdout}")

for p in procs:
    p.kill()
if os.environ.get("SERVICOS_PRINTS"):
    pass
if falhas:
    print(f"(log em {run}: nav.log)", file=sys.stderr)
    sys.exit(1)
shutil.rmtree(run, ignore_errors=True)
print("servicos: ok")
