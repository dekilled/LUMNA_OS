#!/usr/bin/env python3
"""Rede e nuvem do Atlas com o app inteiro num Orbit headless: o formulário, o diálogo de senha (um "servidor" de teste no lugar do gvfs), o menu "..." (desconectar e
esquecer), uma conta Nextcloud (com um rclone de mentira) e as listas Compartilhadas e Nuvem da barra lateral.
python3 teste_rede.py PASTA_DO_ORBIT PASTA_RUN CASA"""
import os, re, subprocess, sys, time, json, stat

orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "teste_ops.py")).read().split("# o app já está aberto")[0].split('orbit_dir, run, casa = sys.argv[1], sys.argv[2], sys.argv[3]')[1])

cfgdir = f"{casa}/.config/lumna/atlas"
servidor = f"{casa}/Servidor"


def n(chave):
    return int(estado().get(chave, "-9"))


def le(p):
    try:
        return open(p).read()
    except FileNotFoundError:
        return ""


# 1. a página
toca("t-ajustes")
toca("cfg-pg/3")
confere(lambda: pos("rd-url") is not None and pos("rd-add-conta") is not None, "a página Rede e nuvem abre")

# 2. um servidor sem senha: o formulário normaliza o endereço e conecta
toca("rd-url")
digita("livre/dados")
toca("rd-conectar")
confere(lambda: n("nshares") == 1 and n("nmont") == 1, "o formulário adiciona e conecta um compartilhamento sem senha")
if "uri=smb://livre/dados" not in le(f"{cfgdir}/rede.ini"):
    falha(f"rede.ini sem o endereço normalizado: {le(cfgdir + '/rede.ini')!r}")
confere(lambda: pos("rd-share/0") is not None, "ele aparece na lista")

# 3. um servidor com senha: o diálogo aparece, a senha errada falha, a certa conecta
toca("rd-url")
digita("seguro/pasta")
toca("rd-conectar")
confere(lambda: pos("cx-senha") is not None and n("cx") == 1, "o diálogo de senha aparece")
digita("eric")
tecla("Tab")
digita("errada")
toca("cx-entrar")
confere(lambda: n("nshares") == 2 and n("nmont") == 1 and n("cx") == -1, "senha errada: não conecta e o diálogo fecha")
# de novo, certa
toca("rd-conectar")  # o campo foi limpo ao adicionar; digita outra vez
toca("rd-url")
digita("seguro/pasta")
toca("rd-conectar")
confere(lambda: pos("cx-senha") is not None, "pede a senha de novo")
digita("eric")
tecla("Tab")
digita("certa")
toca("cx-entrar")
confere(lambda: n("nmont") == 2 and n("cx") == -1, "senha certa conecta")
# cancelar o diálogo
toca("rd-url")
digita("outro/x")
toca("rd-conectar")
confere(lambda: pos("cx-cancelar") is not None and n("cx") == 1, "um terceiro pede a senha")
toca("cx-cancelar")
confere(lambda: n("cx") == -1 and n("nmont") == 2, "cancelar o diálogo não conecta")

# 4. o menu "...": desconectar o primeiro
toca("rd-mais/0")
confere(lambda: pos("mr-desconectar") is not None, "o menu ... mostra Desconectar")
toca("mr-desconectar")
confere(lambda: n("nmont") == 1, "desconectar tira da lista de montados")

# 5. esquecer o terceiro (o "outro/x", que ficou na lista desconectado)
toca("rd-mais/2")
confere(lambda: pos("mr-esquecer") is not None, "o menu ... de um desconectado mostra Conectar e Esquecer")
toca("mr-esquecer")
confere(lambda: n("nshares") == 2, "esquecer tira da lista")

# 6. uma conta Nextcloud
toca("rd-add-conta")
confere(lambda: pos("ct-ok") is not None, "o diálogo Adicionar conta abre")
time.sleep(0.4)
tecla("a", mod="ctrl")
digita("Teste")
toca("ct-url")
digita("https://nuvem.exemplo.com")
toca("ct-usuario")
digita("eric")
toca("ct-senha")
digita("segredo")
toca("ct-ok")
confere(lambda: n("ncontas") == 1, "a conta é adicionada")
conf = f"{cfgdir}/rclone.conf"
txt = le(conf)
if "type=webdav" not in txt or "vendor=nextcloud" not in txt or "remote.php/dav/files/eric/" not in txt or "pass=OBSC-segredo" not in txt:
    falha(f"rclone.conf inesperado: {txt!r}")
if os.path.exists(conf) and (os.stat(conf).st_mode & 0o777) != 0o600:
    falha("o rclone.conf devia ser 0600")
confere(lambda: "--resync" in le(f"{run}/rclone.log"), "a primeira sincronização começa sozinha, com --resync", 8)
confere(lambda: re.search(r"ultima_sync=[1-9]", le(f"{cfgdir}/rede.ini")) is not None, "a conta fica com a hora da última sincronização", 8)
if "segredo" in le(f"{run}/rclone.log"):
    falha("a senha apareceu na linha de comando do rclone")
confere(lambda: pos("rd-sync-agora") is not None, "a conta mostra Sincronizar agora")
toca("rd-sync-agora")
confere(lambda: le(f"{run}/rclone.log").count("bisync") >= 2 and le(f"{run}/rclone.log").splitlines()[-1].find("--resync") < 0, "Sincronizar agora usa bisync sem --resync")

# 7. as listas da barra lateral
toca("cfg-voltar")
confere(lambda: estado().get("pagina") == "-1", "volta aos arquivos")
toca("l-rede")
confere(lambda: estado().get("vista") == "4", "a lista Compartilhadas abre")
time.sleep(0.6)
confere(lambda: pos("rd-cartao/1") is not None and pos("rd-cartao/0") is None, "só o conectado aparece (os desconectados ficam escondidos)")
toca("rd-cartao/1")
confere(lambda: estado().get("vista") == "0" and estado().get("pasta") == servidor, "clicar num compartilhamento conectado entra nele")
toca("l-nuvem")
confere(lambda: estado().get("vista") == "5", "a lista Nuvem abre")
time.sleep(0.6)
confere(lambda: pos("rd-cartao/0") is not None, "a conta aparece")
segura("rd-cartao/0")
confere(lambda: pos("mc-sync") is not None and pos("mc-remover") is not None, "o botão direito na conta abre o menu")
toca("mc-abrir")
confere(lambda: estado().get("vista") == "0" and "Nuvem/Teste" in estado().get("pasta", ""), "Abrir a pasta local da conta (criada pela sincronização)")

# 8. remover a conta tira do rclone.conf
toca("t-ajustes")
toca("cfg-pg/3")
confere(lambda: pos("rd-conta-mais/0") is not None, "a conta na página")
toca("rd-conta-mais/0")
time.sleep(0.8)
toca("mc-remover")
confere(lambda: n("ncontas") == 0, "remover a conta")
if "Nuvem" in le(conf) and "atlas-" in le(conf):
    falha(f"remover devia tirar a conta do rclone.conf: {le(conf)!r}")
toca("cfg-voltar")

# 9. as preferências valem ao Aplicar
toca("t-ajustes")
toca("cfg-pg/3")
toca("li/rd-indisp")
toca("cfg-aplicar")
time.sleep(0.5)
if "rede_indisponiveis=true" not in le(f"{cfgdir}/config.ini"):
    falha("Mostrar locais indisponíveis não foi gravado")
toca("cfg-voltar")
toca("l-rede")
confere(lambda: pos("rd-cartao/0") is not None, "com 'Mostrar locais indisponíveis' o desconectado aparece")
vai_para(docs)

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("explorador (rede e nuvem): ok")
