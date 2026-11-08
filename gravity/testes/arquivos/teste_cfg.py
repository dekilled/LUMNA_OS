#!/usr/bin/env python3
"""As páginas de configurações do explorador (a engrenagem), com o app inteiro num Orbit headless: Exibição, Página inicial, Barra lateral e Avançado,
com Aplicar, Cancelar, Restaurar padrão e o que vale na hora.
python3 teste_cfg.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

cfg_ini = f"{casa}/.config/lumna/arquivos/config.ini"
barra_ini = f"{casa}/.config/lumna/arquivos/barra.ini"


def le(p):
    try:
        return open(p).read()
    except FileNotFoundError:
        return ""


def abre_cfg(pagina):
    toca("t-ajustes")
    confere(lambda: estado().get("pagina") == "0", "a engrenagem abre as configurações")
    if pagina:
        toca(f"cfg-pg/{pagina}")
        confere(lambda: estado().get("pagina") == str(pagina), f"página {pagina}")
    time.sleep(0.4)


vai_para(docs)
open(f"{docs}/.segredo", "w").write("x")
open(f"{docs}/Leiame.txt", "w").write("x")
time.sleep(0.6)

# 1. Exibição: Lista grande + mostrar ocultos + sem extensões; Aplicar grava e vale na hora
abre_cfg(0)
toca("ex-modo/2")
toca("li/ex-ocultos")
toca("li/ex-ext")
toca("cfg-aplicar")
time.sleep(0.6)
c = le(cfg_ini)
if "vista=2" not in c or "ocultos=true" not in c or "extensoes=false" not in c:
    falha(f"Aplicar não gravou a exibição: {c!r}")
toca("cfg-voltar")
confere(lambda: estado().get("pagina") == "-1", "Voltar aos arquivos")
confere(lambda: estado().get("lista") == "2" and estado().get("ocultos") == "1" and ".segredo" in ordem(), "a Lista grande e os ocultos valem na hora")

# 2. Cancelar descarta
abre_cfg(0)
toca("ex-modo/0")
toca("cfg-cancelar")
confere(lambda: estado().get("pagina") == "-1", "Cancelar volta aos arquivos")
if "vista=2" not in le(cfg_ini) or estado().get("lista") != "2":
    falha("Cancelar não devia mudar nada")

# 3. Lembrar a visualização de cada pasta
abre_cfg(0)
toca("ex-modo/0")  # a grade primeiro: com a Lista grande a prévia fica alta e empurra as opções para baixo da janela de teste
time.sleep(0.5)
toca("ex-r2")
toca("cfg-aplicar")
toca("cfg-voltar")
time.sleep(0.5)
toca("t-lista")
time.sleep(0.5)
if docs not in le(f"{casa}/.config/lumna/arquivos/visoes.ini"):
    falha("a visualização da pasta não foi lembrada")
vai_para(f"{docs}/Contratos")
confere(lambda: estado().get("lista") == "0", "outra pasta usa a visualização padrão (grade)")
vai_para(docs)
confere(lambda: estado().get("lista") in ("1", "2"), "a pasta lembra a sua visualização")

# 4. Barra lateral: esconder Recentes; Restaurar padrão desfaz
abre_cfg(2)
toca("bl-vis/1")
toca("cfg-aplicar")
time.sleep(0.5)
b = le(barra_ini)
m = re.search(r"\[item001\][^\[]*", b)
if not m or "visivel=false" not in m[0]:
    falha(f"esconder Recentes não gravou: {b[:300]!r}")
toca("cfg-restaurar")
toca("cfg-aplicar")
time.sleep(0.5)
m = re.search(r"\[item001\][^\[]*", le(barra_ini))
if not m or "visivel=true" not in m[0]:
    falha("Restaurar padrão não devolveu o Recentes")

# 5. Página inicial: tirar as unidades; o Início deixa de mostrá-las (só confere o ajuste)
toca("cfg-pg/1")
time.sleep(0.4)
toca("li/pi-s2")
toca("cfg-aplicar")
time.sleep(0.5)
if "inicio_unidades=false" not in le(cfg_ini):
    falha("tirar as unidades da página inicial não gravou")
toca("cfg-restaurar")
toca("cfg-aplicar")

# 6. Avançado: sem confirmar a exclusão permanente: Shift+Del apaga direto
toca("cfg-pg/4")
time.sleep(0.4)
toca("li/av-apagar")
toca("cfg-aplicar")
toca("cfg-voltar")
if "confirmar_apagar=false" not in le(cfg_ini):
    falha("o ajuste de confirmar a exclusão não gravou")
vai_para(docs)
open(f"{docs}/some.txt", "w").write("x")
confere(lambda: "some.txt" in ordem(), "o arquivo novo aparece")
seleciona("some.txt")
tecla("Delete", mod="shift")
confere(lambda: not os.path.exists(f"{docs}/some.txt"), "Shift+Del apaga direto quando não se quer confirmação")

# 7. Esc fecha a tela de configurações
toca("t-ajustes")
confere(lambda: estado().get("pagina") == "0", "reabre")
tecla("Escape")
confere(lambda: estado().get("pagina") == "-1", "Esc volta aos arquivos")

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (páginas de configurações): ok")
