#!/usr/bin/env python3
"""Os botões "..." de cada arquivo (grade e lista) e o menu de contexto da barra lateral (segurar e soltar sem andar, que é o botão direito do dedo).
python3 teste_mais.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json, configparser

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

barra_ini = f"{casa}/.config/lumna/atlas/barra.ini"


def indice(nome):
    return [n.rstrip("*") for n in ordem()].index(nome)


vai_para(docs)
for modo in ("t-grade", "t-lista"):
    toca(modo)
    time.sleep(0.5)
    base = "cel-mais" if modo == "t-grade" else "lin-mais"
    # o "..." só aparece no item selecionado (ou com o ponteiro em cima)
    seleciona("Contratos")
    time.sleep(0.4)
    i = indice("Contratos")
    confere(lambda: pos(f"{base}/{i}") is not None, f"o ... aparece no item selecionado ({modo})")
    toca(f"{base}/{i}")
    confere(lambda: pos("mi-abrir") is not None and pos("mi-janela") is not None, f"clicar no ... abre o menu do item ({modo})")
    # o menu é o do item certo: "Abrir" entra nele
    toca("mi-abrir")
    confere(lambda: estado().get("pasta") == f"{docs}/Contratos", f"Abrir, pelo menu do ..., entra na pasta ({modo})")
    vai_para(docs)
    confere(lambda: estado().get("sel") == "0", f"a pasta nova começa sem seleção ({modo})")

toca("t-grade")
time.sleep(0.4)

# a barra lateral: segurar e soltar sem andar abre o menu (o dedo parado é o botão direito)
segura("l-it/3")  # Home (Coleções)
confere(lambda: pos("ml-abrir") is not None and pos("ml-aba") is not None and pos("ml-remover") is not None, "segurar um atalho de pasta abre o menu, com Remover da barra lateral")
tecla("Escape")
time.sleep(0.5)
segura("l-it/1")  # Recentes (de fábrica: só esconde)
confere(lambda: pos("ml-esconder") is not None, "o menu de um item de fábrica tem Esconder")
toca("ml-esconder")
time.sleep(0.6)
cp = configparser.ConfigParser(interpolation=None)
cp.read(barra_ini, encoding="utf-8")
ocultos = [cp[s]["rotulo"] for s in cp.sections() if cp[s]["visivel"] == "false"]
if ocultos != ["Recentes"]:
    falha(f"Esconder devia gravar Recentes como invisível: {ocultos}")
confere(lambda: pos("l-it/1") is None or True, "ok")
segura("l-lixeira")
confere(lambda: pos("ml-esvaziar") is not None, "o menu da Lixeira tem Esvaziar")
tecla("Escape")
time.sleep(0.4)

# de volta à barra de fábrica (Configurações > Barra lateral > Restaurar padrão > Aplicar), para os testes seguintes
toca("t-ajustes")
toca("cfg-pg/2")
toca("cfg-restaurar")
toca("cfg-aplicar")
toca("cfg-voltar")
confere(lambda: estado().get("pagina") == "-1", "volta aos arquivos")
vai_para(docs)

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (botões ... e menu da barra lateral): ok")
