#!/usr/bin/env python3
"""O componente de modos (Desktop, Gaming, Mídia) como um só, em dois monitores: o que acontece com as janelas quando um monitor sai, os
comandos que não podem furar a "ilha" de uma tela em modo especial e a tela da Mídia que nunca fica vazia.
python3 teste_componente.py PASTA_DO_ORBIT RUN PASTA_GRAVITY"""
import json, os, subprocess, sys, time

orbit_dir, run, gravity = sys.argv[1], sys.argv[2], sys.argv[3]
sock = [f for f in os.listdir(run) if f.startswith("orbit.") and f.endswith(".sock")][0]
falhas, verificacoes = [], 0


def msg(*a):
    r = subprocess.run([f"{orbit_dir}/orbit-msg", "-s", f"{run}/{sock}", *map(str, a)], capture_output=True, text=True)
    try:
        return json.loads(r.stdout)
    except Exception:
        return {"ok": False, "erro": r.stdout + r.stderr}


def janela(app):
    """a janela cujo app_id começa por app (a Mídia é lumna-midia-inicio)"""
    for j in msg("janelas").get("janelas", []):
        if j["app_id"].startswith(app):
            return j
    return None


def saidas():
    """as duas telas, da esquerda para a direita"""
    return [s["nome"] for s in sorted(msg("saidas").get("saidas", []), key=lambda s: s["x"])]


def espera(cond, seg=6):
    fim = time.time() + seg
    while time.time() < fim:
        if cond():
            return True
        time.sleep(0.2)
    return cond()


def confere(cond, o_que, seg=6):
    global verificacoes
    verificacoes += 1
    if not espera(cond, seg):
        falhas.append(o_que)
        print(f"FALHOU: {o_que}  (janelas: {[(j['app_id'], j['saida'], j['espaco'], j['visivel'], j['na_dock'], j['foco']) for j in msg('janelas').get('janelas', [])]}; modos: {msg('modo').get('saidas')})", file=sys.stderr)


def abre(cmd):
    return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def abre_com(extra, cmd):
    return subprocess.Popen(cmd, env=dict(os.environ, **extra), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


CALC = "lumna-calculadora"
MIDIA = "lumna-midia"

ESQ, DIR = saidas()[:2]
print(f"(no começo: esquerda {ESQ}, direita {DIR})", file=sys.stderr)
# --- 1. monitor que sai: a janela comum não vira janela da Mídia ---
msg("mouse", "mover", 300, 300)  # o cursor na tela da esquerda
calc = abre([f"{gravity}/calculadora"])
confere(lambda: janela(CALC) and janela(CALC)["saida"] == ESQ and janela(CALC)["espaco"] == 1, "o app comum abre no Desktop da tela da esquerda")
msg("modo", "midia", DIR)
confere(lambda: any(s["saida"] == DIR and s["modo"] == "midia" for s in msg("modo").get("saidas", [])), "o monitor 2 está na Mídia")
msg("disposicao", "direita")  # só o monitor da direita: o 1 desliga
confere(lambda: janela(CALC) and janela(CALC)["saida"] == DIR, "com o monitor 1 desligado, a janela passa para o 2")
confere(lambda: janela(CALC) and janela(CALC)["espaco"] == 1, "a janela comum entra no Desktop da tela que fica (espaço 1), e não no espaço da Mídia (5)")
confere(lambda: janela(CALC) and not janela(CALC)["visivel"], "e fica escondida enquanto o monitor 2 está na Mídia (a Mídia não mostra app do Desktop)")
msg("modo", "desktop", DIR)
confere(lambda: janela(CALC) and janela(CALC)["visivel"], "voltando ao Desktop, a janela aparece")
msg("disposicao", "estender")
confere(lambda: len(msg("modo").get("saidas", [])) == 2, "os dois monitores de volta")
ESQ, DIR = saidas()[:2]  # a tela que volta pode voltar do outro lado
print(f"(depois de estender: esquerda {ESQ}, direita {DIR})", file=sys.stderr)

# --- 2. monitor que sai com a Mídia: a janela da Mídia continua no modo dela ---
msg("modo", "midia", ESQ)
midia = abre([f"{gravity}/midia", "tela", "inicio"])
confere(lambda: janela(MIDIA) and janela(MIDIA)["saida"] == ESQ and janela(MIDIA)["visivel"], "a janela da Mídia aparece no monitor 1, em Mídia")
msg("disposicao", "direita")
confere(lambda: janela(MIDIA) and janela(MIDIA)["saida"] == DIR and janela(MIDIA)["espaco"] == 5, "com o monitor 1 desligado, a janela da Mídia vai para o 2 e continua no espaço da Mídia")
confere(lambda: janela(MIDIA) and not janela(MIDIA)["visivel"] and not janela(MIDIA)["na_dock"], "escondida (o monitor 2 está no Desktop) e fora da dock, como toda janela da Mídia")
confere(lambda: janela(MIDIA) and not janela(MIDIA)["foco"], "o foco não fica na janela escondida")
confere(lambda: janela(CALC) and janela(CALC)["visivel"], "o Desktop do monitor 2 segue mostrando o app comum")
msg("modo", "midia", DIR)
confere(lambda: janela(MIDIA) and janela(MIDIA)["visivel"], "abrir a Mídia no monitor 2 mostra a janela dela de novo")

# --- 3. a ilha: o comando de espaço não tira a tela da Mídia ---
r = msg("espaco", 2, DIR)
verificacoes += 1
if r.get("ok") or "Mídia" not in r.get("erro", ""):
    falhas.append("espaco N numa tela em Mídia devia ser recusado")
    print(f"FALHOU: espaco numa tela em Mídia: {r}", file=sys.stderr)
confere(lambda: any(s["saida"] == DIR and s["modo"] == "midia" for s in msg("modo").get("saidas", [])), "e a tela continua na Mídia")

# --- 4. a tela da Mídia não fica vazia ---
j = janela(MIDIA)
if j:
    msg("fechar", j["id"])
confere(lambda: janela(MIDIA) is None or janela(MIDIA)["id"] != j["id"], "a janela da Mídia fecha", 4)
confere(lambda: janela(MIDIA) is not None and janela(MIDIA)["saida"] == DIR and janela(MIDIA)["visivel"], "a tela da Mídia que ficou vazia traz o Início de volta", 8)

# --- 5. o Gaming pula para a tela que ficou (se ela está no Desktop) ---
JOGO = "lumna-signals"
modo_de = lambda saida: next((x["modo"] for x in msg("modo").get("saidas", []) if x["saida"] == saida), None)
msg("disposicao", "estender")
confere(lambda: len(msg("modo").get("saidas", [])) == 2, "os dois monitores de volta (5)")
ESQ, DIR = saidas()[:2]
msg("modo", "desktop", DIR)
msg("modo", "gaming", ESQ)
jogo = abre_com({"LUMNA_JOGO": "1"}, [f"{gravity}/signals"])
confere(lambda: janela(JOGO) and janela(JOGO)["saida"] == ESQ and janela(JOGO)["espaco"] == 4 and janela(JOGO)["visivel"], "o jogo abre no Gaming da tela da esquerda")
msg("disposicao", "direita")  # a tela do Gaming sai
confere(lambda: modo_de(DIR) == "gaming", "a tela que ficou passa para o Gaming")
confere(lambda: janela(JOGO) and janela(JOGO)["saida"] == DIR and janela(JOGO)["visivel"] and janela(JOGO)["foco"], "o jogo aparece nela, com o foco")
msg("modo", "desktop", DIR)
confere(lambda: janela(CALC) and janela(CALC)["visivel"], "voltando ao Desktop, o app comum continua lá")
# --- 6. ... mas não interrompe a Mídia da outra tela ---
msg("disposicao", "estender")
confere(lambda: len(msg("modo").get("saidas", [])) == 2, "os dois monitores de volta (6)")
ESQ, DIR = saidas()[:2]
msg("modo", "gaming", ESQ)
confere(lambda: janela(JOGO) and janela(JOGO)["saida"] == ESQ and janela(JOGO)["visivel"], "o Gaming (com o jogo) na tela da esquerda")
msg("modo", "midia", DIR)
msg("disposicao", "direita")
confere(lambda: modo_de(DIR) == "midia", "com a outra tela na Mídia, ela continua na Mídia")
confere(lambda: janela(JOGO) and janela(JOGO)["espaco"] == 4 and not janela(JOGO)["visivel"], "e o jogo espera no Gaming, escondido")

for p in (calc, midia, jogo):
    p.terminate()
print(f"modos (componente): {verificacoes} verificações, {len(falhas)} falhas")
sys.exit(1 if falhas else 0)
