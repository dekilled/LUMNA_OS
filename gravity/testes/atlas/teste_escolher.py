#!/usr/bin/env python3
"""O Atlas como seletor de arquivos do sistema (atlas --escolher=...): abrir um arquivo, vários com filtro, escolher pasta, salvar (e substituir) e cancelar.
Cada janela é um processo à parte; o que a pessoa escolhe sai em stdout (um caminho por linha) e o código de saída é 0 (escolheu) ou 1 (cancelou).
python3 teste_escolher.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import json, os, re, subprocess, sys, time

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
docs = f"{casa}/Documentos"
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock", LUMNA_ATLAS_DEBUG="1")
gravity = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
atlas = os.environ.get("ATLAS_BIN", f"{gravity}/atlas")
falhas = []


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)


def msg(*a):
    subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, stdout=subprocess.DEVNULL)


class Seletor:
    def __init__(self, *args):
        self.saida = f"{run}/esc.out"
        self.proc = subprocess.Popen([atlas, *args], env=env, stdout=open(self.saida, "w"), stderr=subprocess.DEVNULL)
        self.ox = self.oy = None
        self.espera(lambda: self.estado().get("carregando") == "0" and self.estado().get("esc", "0") != "0", 8)
        time.sleep(0.6)

    def texto(self):
        return open(self.saida, errors="replace").read()

    def estado(self):
        r = {}
        for l in self.texto().splitlines():
            if l.startswith("estado "):
                r = dict(re.findall(r"(\w+)=(\[[^\]]*\]|\S+)", l))
        return r

    def ordem(self):
        r = []
        for l in self.texto().splitlines():
            if l.startswith("ordem"):
                r = l.split(" ")[1:]
        return r

    def pos(self, nome):
        r = None
        for l in self.texto().splitlines():
            m = re.match(r"pos (.+) (-?\d+) (-?\d+) (\d+) (\d+)$", l)
            if m and m[1] == nome:
                r = tuple(int(m[i]) for i in range(2, 6))
        return r

    def espera(self, cond, seg=4):
        fim = time.time() + seg
        while time.time() < fim:
            if cond():
                return True
            time.sleep(0.1)
        return cond()

    def origem(self):
        out = subprocess.run([f"{orbit_dir}/orbit-msg", "janelas"], env=env, capture_output=True, text=True).stdout
        for j in json.loads(out)["janelas"]:
            if j["app_id"] == "lumna-atlas-escolher":
                return j["x"], j["y"]
        raise RuntimeError("a janela do seletor não abriu")

    def clica(self, nome):
        p = self.pos(nome)
        if not p:
            raise RuntimeError(f"não achei {nome} na tela do seletor")
        ox, oy = self.origem()
        x, y = ox + p[0] + p[2] // 2, oy + p[1] + p[3] // 2
        msg("mouse", "mover", x, y)
        time.sleep(0.2)
        msg("mouse", "botao", "baixo")
        time.sleep(0.06)
        msg("mouse", "botao", "cima")
        time.sleep(0.5)

    def duplo(self, nome):
        p = self.pos(nome)
        ox, oy = self.origem()
        x, y = ox + p[0] + p[2] // 2, oy + p[1] + p[3] // 2
        msg("mouse", "mover", x, y)
        time.sleep(0.2)
        for _ in range(2):
            msg("mouse", "botao", "baixo")
            time.sleep(0.05)
            msg("mouse", "botao", "cima")
            time.sleep(0.1)
        time.sleep(0.5)

    def tecla(self, *teclas, mod=None):
        cmd = ["wtype", "-s", "250"]
        if mod:
            cmd += ["-M", mod]
        for t in teclas:
            cmd += ["-k", t]
        if mod:
            cmd += ["-m", mod]
        subprocess.run(cmd, stdout=subprocess.DEVNULL)
        time.sleep(0.5)

    def resultado(self, seg=4):
        try:
            rc = self.proc.wait(timeout=seg)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            return None, []
        linhas = [l for l in self.texto().splitlines() if l.startswith("/")]
        return rc, linhas

    def fecha(self):
        if self.proc.poll() is None:
            self.proc.kill()


def extras(criar):
    for n in ("a.md", "b.md", "foto.png"):
        if criar:
            open(f"{docs}/{n}", "w").write("x")
        else:
            try:
                os.remove(f"{docs}/{n}")
            except OSError:
                pass


# 1. abrir um arquivo: clicar nele e em Abrir
s = Seletor("--escolher=abrir", f"--pasta={docs}", "--titulo=Escolha um arquivo")
if "notas.md" not in " ".join(s.ordem()):
    falha(f"o seletor devia abrir na pasta pedida ({s.ordem()[:6]})")
ok = s.pos("esc-ok")
s.clica("item/notas.md")
s.clica("esc-ok")
rc, linhas = s.resultado()
if rc != 0 or linhas != [f"{docs}/notas.md"]:
    falha(f"abrir: devia sair 0 com o arquivo escolhido (rc {rc}, {linhas})")
s.fecha()

# 2. duplo clique abre direto
s = Seletor("--escolher=abrir", f"--pasta={docs}")
s.duplo("item/notas.md")
rc, linhas = s.resultado()
if rc != 0 or linhas != [f"{docs}/notas.md"]:
    falha(f"o duplo clique num arquivo devia escolhê-lo (rc {rc}, {linhas})")
s.fecha()

# 3. vários, com filtro: só os .md aparecem, e Ctrl+A leva só os arquivos (os arquivos a mais só existem aqui: mais itens empurram os outros para fora da janela)
extras(True)
s = Seletor("--escolher=abrir-varios", f"--pasta={docs}", "--filtro=Notas::g=*.md", "--filtro=Imagens::g=*.png,m=image/png")
nomes = [n.rstrip("*") for n in s.ordem()]
if "foto.png" in nomes or "planejamento.pdf" in nomes or "a.md" not in nomes:
    falha(f"o filtro 'Notas' devia mostrar só os .md (veio {nomes})")
s.tecla("a", mod="ctrl")
s.clica("esc-ok")
rc, linhas = s.resultado()
esperados = sorted(f"{docs}/{n}" for n in nomes if n.endswith(".md"))
if rc != 0 or sorted(linhas) != esperados or len(linhas) < 3:
    falha(f"abrir vários: devia sair com todos os .md marcados (rc {rc}, {linhas}, esperava {esperados})")
s.fecha()
extras(False)

# 4. escolher pasta: clicando numa pasta e em Escolher; sem marcar nada leva a pasta de agora
s = Seletor("--escolher=pasta", f"--pasta={docs}")
if any(n.rstrip("*").endswith((".md", ".pdf", ".png", ".ods", ".odp")) for n in s.ordem()):
    falha(f"escolher pasta não devia mostrar arquivos ({s.ordem()})")
s.clica("item/Projetos")
s.clica("esc-ok")
rc, linhas = s.resultado()
if rc != 0 or linhas != [f"{docs}/Projetos"]:
    falha(f"escolher pasta: devia sair com a pasta marcada (rc {rc}, {linhas})")
s.fecha()
s = Seletor("--escolher=pasta", f"--pasta={docs}")
s.clica("esc-ok")
rc, linhas = s.resultado()
if rc != 0 or linhas != [docs]:
    falha(f"escolher pasta sem marcar nada devia levar a pasta de agora (rc {rc}, {linhas})")
s.fecha()

# 5. salvar: o nome do campo na pasta; um nome que já existe pergunta antes de substituir
s = Seletor("--escolher=salvar", f"--pasta={docs}", "--nome=novo.txt")
s.clica("esc-ok")
rc, linhas = s.resultado()
if rc != 0 or linhas != [f"{docs}/novo.txt"]:
    falha(f"salvar: devia sair com pasta + nome (rc {rc}, {linhas})")
s.fecha()
s = Seletor("--escolher=salvar", f"--pasta={docs}", "--nome=notas.md")
s.clica("esc-ok")
if s.proc.poll() is not None:
    falha("salvar num nome que já existe devia perguntar antes de sair")
else:
    s.clica("esc-sub-sim")
    rc, linhas = s.resultado()
    if rc != 0 or linhas != [f"{docs}/notas.md"]:
        falha(f"substituir confirmado devia sair com o caminho (rc {rc}, {linhas})")
s.fecha()
s = Seletor("--escolher=salvar", f"--pasta={docs}", "--nome=notas.md")
s.clica("esc-ok")
s.clica("esc-sub-nao")
if s.proc.poll() is not None:
    falha("recusar substituir não devia sair")
s.fecha()

# 6. cancelar: Esc e o botão saem com 1 e sem nada em stdout
s = Seletor("--escolher=abrir", f"--pasta={docs}")
s.tecla("Escape")
rc, linhas = s.resultado()
if rc != 1 or linhas:
    falha(f"Esc devia cancelar (rc {rc}, {linhas})")
s.fecha()
s = Seletor("--escolher=abrir", f"--pasta={docs}")
s.clica("esc-cancelar")
rc, linhas = s.resultado()
if rc != 1 or linhas:
    falha(f"Cancelar devia sair com 1 (rc {rc}, {linhas})")
s.fecha()

extras(False)
try:
    os.remove(f"{docs}/novo.txt")
except OSError:
    pass
sys.exit(1 if falhas else 0)
