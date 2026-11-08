#!/usr/bin/env python3
"""As abas do explorador, com o app inteiro num Orbit headless: Ctrl+T, trocar clicando e por Ctrl+Tab/Ctrl+número, o histórico de cada aba, "Abrir em nova aba" no
menu de uma pasta, fechar pelo X e Ctrl+W (a última fecha a janela).
python3 teste_abas.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

contratos = f"{docs}/Contratos"
projetos = f"{docs}/Projetos"


def abas():
    e = estado()
    return int(e.get("abas", "0")), int(e.get("aba", "-1"))


vai_para(docs)
confere(lambda: abas() == (1, 0), "começa com uma aba só")
if pos("aba/0"):
    falha("com uma aba só a faixa não devia aparecer")

# 1. Ctrl+T: aba nova no Início, a faixa aparece
tecla("t", mod="ctrl")
confere(lambda: abas() == (2, 1) and estado().get("pasta") == "-", "Ctrl+T abre uma aba nova no Início")
confere(lambda: pos("aba/0") is not None and pos("aba/1") is not None, "a faixa de abas aparece com duas")

# 2. cada aba tem o seu lugar: a segunda vai para Contratos, clicar na primeira volta a Documentos
vai_para(contratos)
toca("aba/0")
confere(lambda: abas() == (2, 0) and estado().get("pasta") == docs, "clicar na primeira aba volta a Documentos")
toca("aba/1")
confere(lambda: abas() == (2, 1) and estado().get("pasta") == contratos, "clicar na segunda aba volta a Contratos")

# 3. o histórico é de cada aba: voltar na segunda leva ao Início, e a primeira continua em Documentos
toca("t-voltar")
confere(lambda: estado().get("pasta") == "-", "Voltar na segunda aba volta ao Início dela")
toca("aba/0")
confere(lambda: estado().get("pasta") == docs, "a primeira aba continua em Documentos")
toca("aba/1")
confere(lambda: estado().get("pasta") == "-", "a segunda continua no Início")
toca("t-avancar")
confere(lambda: estado().get("pasta") == contratos, "Avançar vai de novo para Contratos")

# 4. Ctrl+Tab, Ctrl+Shift+Tab e Ctrl+número
tecla("Tab", mod="ctrl")
confere(lambda: abas() == (2, 0), "Ctrl+Tab vai para a próxima aba (dá a volta)")
tecla("Tab", mod="ctrl+shift")
confere(lambda: abas() == (2, 1), "Ctrl+Shift+Tab volta")
tecla("1", mod="ctrl")
confere(lambda: abas() == (2, 0), "Ctrl+1 vai para a primeira")
tecla("9", mod="ctrl")
confere(lambda: abas() == (2, 1), "Ctrl+9 vai para a última")

# 5. "Abrir em nova aba" no menu de uma pasta: a aba nasce logo depois da de agora
toca("aba/0")
confere(lambda: abas() == (2, 0) and estado().get("pasta") == docs, "de volta a Documentos")
segura_sel("Projetos")
toca("mi-aba")
confere(lambda: abas() == (3, 1) and estado().get("pasta") == projetos, "Abrir em nova aba abre a pasta numa aba depois da de agora")
# a de Documentos continua lá, e a de Contratos virou a terceira
toca("aba/0")
confere(lambda: abas() == (3, 0) and estado().get("pasta") == docs, "a primeira não mudou")
toca("aba/2")
confere(lambda: abas() == (3, 2) and estado().get("pasta") == contratos, "a de Contratos virou a terceira")

# 6. fechar pelo X: a aba do meio (Projetos), sem ser a de agora, não tira a de agora
toca("aba-x/1")
confere(lambda: abas() == (2, 1) and estado().get("pasta") == contratos, "fechar uma aba que não é a de agora mantém a de agora")
# fechar a de agora: vai para a vizinha
toca("aba-x/1")
confere(lambda: abas() == (1, 0) and estado().get("pasta") == docs, "fechar a aba de agora mostra a vizinha")

# 7. Ctrl+W numa aba só fecha a janela
tecla("w", mod="ctrl")
fim = time.time() + 5
while time.time() < fim and subprocess.run(["pgrep", "-x", "atlas"], stdout=subprocess.DEVNULL).returncode == 0:
    time.sleep(0.2)
if subprocess.run(["pgrep", "-x", "atlas"], stdout=subprocess.DEVNULL).returncode == 0:
    falha("Ctrl+W com uma aba só devia fechar a janela")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (abas): ok")
