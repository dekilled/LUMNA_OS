#!/usr/bin/env python3
"""Um disco lento não derruba o Atlas: com cada leitura de pasta demorando segundos, o app abre a pasta ("Abrindo…"), segue lendo os eventos do mouse (o Orbit
derruba o cliente que para de ler: "Data too big for buffer") e, ao chegar, mostra a pasta; selecionar pastas (contagem e tamanho no painel de detalhes) também não trava.
python3 teste_lento.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json, socket

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])


def pid():
    r = subprocess.run(["pgrep", "-x", "atlas"], capture_output=True, text=True).stdout.split()
    return int(r[0]) if r else None


def mexe_mouse(segundos):
    """movimentos de mouse sem parar sobre a janela (o que enche o buffer de um cliente que não lê)"""
    s = socket.socket(socket.AF_UNIX)
    sock = f"{run}/orbit.wayland-0.sock"
    fim = time.time() + segundos
    n = 0
    while time.time() < fim:
        c = socket.socket(socket.AF_UNIX)
        c.connect(sock)
        c.sendall(f"mouse mover {300 + n % 400} {300 + n % 250}\n".encode())
        c.recv(256)
        c.close()
        n += 1
        time.sleep(0.001)  # ~600 movimentos por segundo (um mouse de 500 a 1000 Hz)
    return n


p0 = pid()
if not p0:
    falha("o Atlas não está rodando")
    sys.exit(1)

lento = os.path.join(casa, ".atlas_lento_ms")
grande = os.path.join(casa, "lenta")
for i in range(6):
    d = os.path.join(grande, f"sub{i}")
    os.makedirs(d, exist_ok=True)
    for j in range(30):
        open(os.path.join(d, f"a{j}.txt"), "wb").write(b"x")
for j in range(50):
    open(os.path.join(grande, f"arq{j}.txt"), "wb").write(b"x")

vai_para(casa)
open(lento, "w").write("16000")  # cada leitura de pasta: 16 s (mais que os ~10 s que o buffer do Orbit aguenta com o mouse mexendo a ~600/s)
try:
    # 1. abrir uma pasta lenta: o app mostra "Abrindo…" e continua vivo e lendo eventos
    tecla("l", mod="ctrl")
    tecla("a", mod="ctrl")
    digita(grande)
    tecla("Return")
    time.sleep(1.0)
    if pid() != p0:
        falha("o Atlas caiu ao abrir uma pasta lenta")
        sys.exit(1)
    confere(lambda: estado().get("carregando") == "1", "enquanto a pasta lenta não chega, o app diz que está abrindo", 3)
    mexe_mouse(18)  # mais que o tempo da leitura
    if pid() != p0:
        falha("o Atlas foi derrubado enquanto a pasta lenta carregava e o mouse se mexia (parou de ler os eventos)")
        subprocess.run(["tail", "-5", f"{run}/orbit.log"])
        sys.exit(1)
    confere(lambda: estado().get("carregando") == "0" and estado().get("nvis") == "56", "a pasta lenta aparece quando chega (56 itens)", 20)

    # 2. selecionar pastas com leituras lentas (contagem e tamanho no painel de detalhes) não trava o app
    open(lento, "w").write("6000")
    for i in range(4):
        toca(f"item/sub{i}", 0.2)
    mexe_mouse(10)
    if pid() != p0:
        falha("o Atlas foi derrubado ao selecionar pastas cujo tamanho e contagem demoram")
        subprocess.run(["tail", "-5", f"{run}/orbit.log"])
        sys.exit(1)
    confere(lambda: estado().get("sel") == "1", "a seleção segue respondendo (a última pasta clicada fica selecionada)")
finally:
    if os.path.exists(lento):
        os.remove(lento)

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (disco lento: abrir e selecionar pastas sem travar nem cair): ok")
