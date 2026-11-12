#!/usr/bin/env python3
"""O Signals inteiro num Orbit headless: digitar, desfazer, selecionar, abas, buscar/substituir, salvar (com o seletor de mentira), mudança no disco e os diálogos.
python3 teste_app.py PASTA_DO_ORBIT RUN PASTA_GRAVITY"""
import os, re, subprocess, sys, time

orbit_dir, run, gravity = sys.argv[1], sys.argv[2], sys.argv[3]
env = dict(os.environ, ORBIT_SOCK=f"{run}/orbit.wayland-0.sock")
falhas = []
out = f"{run}/signals.out"
verificacoes = 0


def falha(m):
    falhas.append(m)
    print("FALHOU:", m, file=sys.stderr)
    subprocess.run(["grim", f"{run}/falha-{len(falhas)}.png"])


def msg(*a):
    subprocess.run([f"{orbit_dir}/orbit-msg", *map(str, a)], env=env, stdout=subprocess.DEVNULL)


def log():
    return open(out, encoding="utf-8", errors="replace").read() if os.path.exists(out) else ""


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


def espera(cond, seg=4):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.1)
    return cond()


def confere(cond, o_que, seg=4):
    global verificacoes
    verificacoes += 1
    if not espera(cond, seg):
        falha(o_que + f"  (estado: {estado()})")


def origem():
    """onde a janela está na tela (as posições que o app imprime são da janela)"""
    r = subprocess.run([f"{orbit_dir}/orbit-msg", "janelas"], env=env, capture_output=True, text=True).stdout
    m = re.search(r'"app_id":"lumna-signals".*?"x":(-?\d+),"y":(-?\d+)', r)
    return (int(m[1]), int(m[2])) if m else (0, 0)


def toca(nome, espera_s=0.5):
    espera(lambda: pos(nome) is not None, 4)
    p = pos(nome)
    if not p:
        falha(f"não achei {nome} na tela")
        return
    ox, oy = origem()
    msg("mouse", "mover", ox + p[0] + p[2] // 2, oy + p[1] + p[3] // 2)
    time.sleep(0.15)
    msg("mouse", "botao", "baixo")
    time.sleep(0.08)
    msg("mouse", "botao", "cima")
    time.sleep(espera_s)


def tecla(*t, mod=None):
    cmd = ["wtype", "-s", "120"]
    for m in (mod.split("+") if mod else []):
        cmd += ["-M", m]
    for k in t:
        cmd += ["-k", k]
    for m in (mod.split("+") if mod else []):
        cmd += ["-m", m]
    subprocess.run(cmd, stdout=subprocess.DEVNULL)
    time.sleep(0.35)


def digita(txt):
    subprocess.run(["wtype", "-s", "60", txt], stdout=subprocess.DEVNULL)
    time.sleep(0.4)


def txt():
    return estado().get("txt", "")[1:-1]


def le(caminho):
    try:
        return open(caminho, encoding="utf-8").read()
    except OSError:
        return None


a_txt = f"{run}/a.txt"
# --- 1. digitar e mover ---
confere(lambda: estado().get("abas") == "1", "abriu com uma aba")
confere(lambda: estado().get("nomes") == "[a.txt]", "a aba tem o nome do arquivo")
confere(lambda: txt() == "ola\\nmundo\\n", "o texto do arquivo apareceu")
tecla("End")
digita("XY")
confere(lambda: txt().startswith("olaXY\\n"), "digitou no fim da linha 1")
confere(lambda: estado().get("mod") == "1", "ficou marcado como modificado")
confere(lambda: estado().get("col") == "6", "a coluna avançou")
tecla("Down")
confere(lambda: estado().get("linha") == "2", "seta para baixo desce uma linha")
tecla("Home")
confere(lambda: estado().get("col") == "1", "Home vai ao começo")
tecla("Return")
confere(lambda: txt() == "olaXY\\n_\\nmundo\\n".replace("_", "") or txt().count("\\n") == 3, "Enter abre uma linha")
# --- 2. desfazer / refazer ---
tecla("z", mod="ctrl")
confere(lambda: txt() == "olaXY\\nmundo\\n", "Ctrl+Z desfaz o Enter")
tecla("z", mod="ctrl")
confere(lambda: txt() == "ola\\nmundo\\n", "Ctrl+Z desfaz o 'XY' de uma vez (digitação agrupada)")
confere(lambda: estado().get("mod") == "0", "voltar ao salvo tira a marca de modificado")
tecla("y", mod="ctrl")
confere(lambda: txt() == "olaXY\\nmundo\\n", "Ctrl+Y refaz")
# --- 3. seleção, copiar, colar ---
tecla("a", mod="ctrl")
confere(lambda: estado().get("sel") == "0,0-2,0", "Ctrl+A seleciona tudo")
tecla("c", mod="ctrl")
tecla("Right")
tecla("Return")
tecla("v", mod="ctrl")
confere(lambda: txt() == "olaXY\\nmundo\\nolaXY\\nmundo\\n\\n".replace("\\n\\n", "\\n") or txt().count("mundo") == 2, "Ctrl+V colou o texto copiado")
tecla("z", mod="ctrl")
tecla("z", mod="ctrl")
# --- 4. salvar ---
tecla("s", mod="ctrl")
confere(lambda: estado().get("mod") == "0", "Ctrl+S salva")
confere(lambda: le(a_txt) == "olaXY\nmundo\n", "o arquivo no disco tem o texto novo")
# --- 5. abas ---
tecla("n", mod="ctrl")
confere(lambda: estado().get("abas") == "2" and estado().get("ativa") == "1", "Ctrl+N abre uma aba nova e vai para ela")
digita("segunda aba")
confere(lambda: txt() == "segunda_aba", "digitou na aba nova")
tecla("Tab", mod="ctrl")
confere(lambda: estado().get("ativa") == "0", "Ctrl+Tab volta à primeira aba")
confere(lambda: txt().startswith("olaXY"), "o texto da primeira aba continua lá")
toca("aba/1")
confere(lambda: estado().get("ativa") == "1", "clicar na aba a ativa")
# fechar aba com alterações: pergunta
toca("aba-x/1")
confere(lambda: estado().get("dlg") == "1", "fechar aba modificada pergunta")
toca("dlg/sg-d-cancelar")
confere(lambda: estado().get("dlg") == "0" and estado().get("abas") == "2", "Cancelar mantém a aba")
toca("aba-x/1")
toca("dlg/sg-d-descartar")
confere(lambda: estado().get("abas") == "1", "Descartar fecha a aba sem salvar")
# --- 6. salvar como com o seletor de mentira ---
tecla("n", mod="ctrl")
digita("nota nova")
tecla("s", mod="ctrl")
confere(lambda: le(f"{run}/escolhido.txt") == "nota nova", "Salvar de uma aba sem nome usa o seletor e grava o arquivo escolhido")
confere(lambda: estado().get("nomes") == "[a.txt|escolhido.txt]", "a aba ganhou o nome escolhido")
# --- 7. buscar e substituir ---
tecla("w", mod="ctrl")
confere(lambda: estado().get("abas") == "1", "Ctrl+W fecha a aba salva sem perguntar")
tecla("f", mod="ctrl")
digita("o")
confere(lambda: estado().get("busca") == "1" and estado().get("total") == "2", "buscar 'o' acha 2 (olaXY, mundo)")
digita("la")
confere(lambda: estado().get("total") == "1", "buscar 'ola' acha 1")
tecla("Escape")
confere(lambda: estado().get("busca") == "0", "Esc fecha a busca")
tecla("h", mod="ctrl")
tecla("a", mod="ctrl")
digita("mundo")
toca("subst")
digita("terra")
toca("subst/todos")
confere(lambda: txt() == "olaXY\\nterra\\n", "Substituir tudo troca 'mundo' por 'terra'")
tecla("z", mod="ctrl")
confere(lambda: txt() == "olaXY\\nmundo\\n", "o substituir tudo desfaz de uma vez")
tecla("Escape")
# --- 8. ir para a linha ---
tecla("g", mod="ctrl")
confere(lambda: estado().get("dlg") == "4", "Ctrl+G abre ir para a linha")
digita("2")
tecla("Return")
confere(lambda: estado().get("linha") == "2" and estado().get("dlg") == "0", "Ir para a linha 2")
# --- 9. mudança no disco ---
tecla("s", mod="ctrl")
confere(lambda: estado().get("mod") == "0", "salvo antes do teste de disco")
time.sleep(1.2)
open(a_txt, "w").write("mudou fora\n")
confere(lambda: txt() == "mudou_fora\\n", "arquivo sem alterações é recarregado quando muda no disco", 6)
digita("!")
time.sleep(1.2)
open(a_txt, "w").write("mudou de novo\n")
confere(lambda: estado().get("dlg") == "3", "arquivo modificado aqui e no disco pergunta", 6)
tecla("Escape")
confere(lambda: estado().get("dlg") == "0", "Esc mantém a versão daqui")
# --- 10. fechar a janela com alterações ---
msg("fechar")
confere(lambda: estado().get("dlg") == "2", "fechar a janela com alteração pergunta antes de sair")
tecla("Escape")
confere(lambda: estado().get("dlg") == "0", "Esc cancela a saída e a janela continua")
# --- 11. zoom e opções ---
tecla("0", mod="ctrl")
tecla("plus", mod="ctrl")
confere(lambda: int(estado().get("zoom", "0")) > 100, "Ctrl++ aumenta o zoom")
tecla("0", mod="ctrl")
confere(lambda: estado().get("zoom") == "100", "Ctrl+0 volta a 100%")

print(f"signals (app): {verificacoes} verificações, {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
