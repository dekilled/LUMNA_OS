#!/usr/bin/env python3
"""O controle de ponta a ponta no lumna-jogos: navegar só com o controle (direcional, A, B, LB, RB, Guia) e a aba Configurações (controles, apelido, ignorar,
vibração, ajustes, botão Guia e Gaming)."""
import json, os, re, socket, struct, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "galeria"))
from comum import *

xbox = f"{run}/falsos/xbox.ev"
EV_KEY, EV_ABS = 1, 3
BTN = {"A": 0x130, "B": 0x131, "LB": 0x136, "RB": 0x137, "GUIA": 0x13c}
ABS_HAT0X, ABS_HAT0Y = 0x10, 0x11


def est(k, padrao=""):
    return estado().get(k, padrao)


def existe(nome):
    p = pos(nome)
    return p is not None and p[2] > 0


_toca = toca


def toca(nome, espera_s=0.45):
    espera(lambda: existe(nome), 3)
    _toca(nome, espera_s)


def ev(tipo, cod, val):
    with open(xbox, "wb", buffering=0) as f:
        f.write(struct.pack("qqHHi", 0, 0, tipo, cod, val))


def aperta(b):
    ev(EV_KEY, BTN[b], 1)
    ev(EV_KEY, BTN[b], 0)
    time.sleep(0.9)  # o serviço manda a tecla pelo wtype e o app redesenha


def dpad(dx=0, dy=0):
    if dx:
        ev(EV_ABS, ABS_HAT0X, dx)
        ev(EV_ABS, ABS_HAT0X, 0)
    if dy:
        ev(EV_ABS, ABS_HAT0Y, dy)
        ev(EV_ABS, ABS_HAT0Y, 0)
    time.sleep(0.9)


def ini(nome):
    return le(f"{casa}/.config/lumna/{nome}")


def pad(p):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(3)
    s.connect(f"{run}/lumna-gamepad.sock")
    s.sendall((p + "\n").encode())
    r = s.makefile().readline()
    s.close()
    return json.loads(r)


confere(lambda: est("tela") == "0" and est("njogos") == "5", "o LUMNA Jogos abre no Início", 8)
confere(lambda: pad("estado")["ok"] and pad("estado")["controles"][0]["bateria"] == 73, "o serviço dos controles responde e vê o Xbox (bateria 73%)", 5)
# o jogo precisa estar em foco para o controle navegar nele: o Orbit dá o foco à janela que abriu
time.sleep(1.0)

# 1. navegar só com o controle: direcional, A (abre), B (volta)
confere(lambda: est("sel") == "0", "começa no primeiro card")
dpad(dx=1)
confere(lambda: est("sel") == "1", "o direcional para a direita anda para o card seguinte")
dpad(dx=-1)
confere(lambda: est("sel") == "0", "e para a esquerda volta")
aperta("A")
confere(lambda: est("tela") == "2", "A abre os Detalhes do jogo", 4)
aperta("B")
confere(lambda: est("tela") == "1", "B volta (Detalhes → Biblioteca)")
# LB e RB trocam de aba: Biblioteca → Início → Biblioteca → Configurações
aperta("LB")
confere(lambda: est("tela") == "0", "LB: Biblioteca → Início")
aperta("RB")
confere(lambda: est("tela") == "1", "RB: Início → Biblioteca")
aperta("RB")
confere(lambda: est("tela") == "4", "RB: Biblioteca → Loja", 4)
aperta("RB")
confere(lambda: est("lsub") == "1", "RB na Loja: Destaques → Categorias")
aperta("LB")
confere(lambda: est("lsub") == "0", "LB volta")
aperta("B")
confere(lambda: est("tela") == "0", "B na Loja volta ao Início")
toca("engrenagem", 1.0)
confere(lambda: est("tela") == "3" and est("servico") == "1", "a engrenagem abre as Configurações (com o serviço dos controles no ar)", 4)
confere(lambda: est("nctl") == "1", "Controles lista o Xbox")

# 2. dentro das Configurações: LB/RB trocam de página, B volta ao Início
aperta("RB")
confere(lambda: est("pg") == "1", "RB: Controles → Ajustes do controle")
aperta("RB")
confere(lambda: est("pg") == "2", "RB: → Botão Guia")
aperta("LB")
aperta("LB")
confere(lambda: est("pg") == "0", "LB volta às páginas")

# 3. Controles: testar vibração, renomear (apelido) e ignorar
toca("ctl-vib/0", 1.0)
confere(lambda: "AA:BB:CC:DD:EE:01 300" in le(f"{run}/falsos/vibrou.log"), "Testar vibração faz o controle vibrar")
toca("ctl-ren/0", 0.6)
confere(lambda: existe("ctl-apc/0"), "Renomear abre o campo do apelido")
toca("ctl-apc/0")
digita("Controle da sala")
toca("ctl-ap-ok/0", 1.2)
confere(lambda: "AA:BB:CC:DD:EE:01=Controle da sala" in ini("gamepad.ini"), "o apelido fica no gamepad.ini")
confere(lambda: pad("estado")["controles"][0]["apelido"] == "Controle da sala", "o serviço já mostra o apelido")
toca("ctl-ign/0", 1.2)
confere(lambda: pad("estado")["controles"][0]["ignorado"], "Ignorar: o serviço marca o controle como ignorado")
dpad(dx=1)
confere(lambda: est("pg") == "0", "um controle ignorado não navega")
toca("ctl-ign/0", 1.2)
confere(lambda: not pad("estado")["controles"][0]["ignorado"], "Voltar a usar")

# 4. Ajustes: zona morta, vibração, trocar A e B
toca("cfg-pg/1", 0.8)
confere(lambda: est("pg") == "1" and existe("aj-ab"), "a página Ajustes do controle")
toca("aj-ab", 1.2)
confere(lambda: "trocar_ab=sim" in ini("gamepad.ini") and pad("estado")["cfg"]["trocar_ab"], "Trocar A e B vale (no arquivo e no serviço)")
aperta("A")  # agora o A é o Esc: de Configurações volta ao Início
confere(lambda: est("tela") == "0", "com A e B trocados, o A volta (Esc)")
toca("engrenagem", 0.8)
confere(lambda: est("tela") == "3", "a engrenagem do topo abre as Configurações")
toca("cfg-pg/1", 0.8)
toca("aj-ab", 1.2)
confere(lambda: "trocar_ab=nao" in ini("gamepad.ini"), "desligar volta ao normal")
toca("aj-vib", 1.0)
confere(lambda: "vibracao=nao" in ini("gamepad.ini"), "desligar a vibração grava")
toca("aj-vib", 1.0)

# 5. Botão Guia: a escolha vale e o Guia age
toca("cfg-pg/2", 0.8)
toca("guia/1", 1.0)
confere(lambda: "guia=inicio" in ini("gamepad.ini"), "escolher 'Ir ao Início' grava")
aperta("GUIA")
confere(lambda: est("tela") == "0", "o Guia (guia=inicio) traz o Início, de dentro das Configurações", 5)
toca("engrenagem", 0.8)
toca("cfg-pg/2", 0.8)
toca("guia/3", 1.0)
confere(lambda: "guia=processos" in ini("gamepad.ini"), "escolher 'Abrir o gerenciador de processos' grava")
toca("guia/4", 1.0)
confere(lambda: "guia=nada" in ini("gamepad.ini"), "escolher 'Não fazer nada' grava")
aperta("GUIA")
confere(lambda: est("tela") == "3", "com guia=nada o Guia não muda nada")

# 6. Gaming: FPS e não perturbar
toca("cfg-pg/3", 0.8)
toca("gm-fps", 1.0)
confere(lambda: "fps=sim" in ini("gamepad.ini"), "FPS (MangoHud) liga")
antes = est("dnd")  # o padrão do Gaming é ligado
toca("gm-dnd", 1.0)
secao = lambda: ini("modos.ini").replace(" ", "").split("[gaming]")[-1].split("[")[0]
confere(lambda: est("dnd") != antes and ("nao_perturbe=sim" in secao()) == (est("dnd") == "1"), f"Não perturbar no Gaming inverte e grava no modos.ini (era {antes})")

# 7. um segundo controle conectado aparece nas Configurações
os.mkfifo(f"{run}/falsos/ps.ev")
open(f"{run}/falsos/ps.dev", "w").write("[controle]\nnome=DualSense\nuniq=11:22:33:44:55:66\nbus=usb\nrumble=nao\n")
toca("cfg-pg/0", 0.8)
confere(lambda: est("nctl") == "2" and existe("ctl-vib/1"), "um controle ligado na hora aparece na lista", 8)
os.unlink(f"{run}/falsos/ps.dev")
os.unlink(f"{run}/falsos/ps.ev")
confere(lambda: est("nctl") == "1", "e some quando é desligado", 8)

sys.exit(1 if falhas else 0)
