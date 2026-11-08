#!/usr/bin/env python3
"""Um lumna-emma de mentira para o módulo emma_terminal: ouve em LUMNA_EMMA_SOCK, entende só "pergunta {json}" e responde conforme o texto recebido.
Cada pedido recebido vai para o ARQUIVO_DE_LOG (uma linha). Uso: emma-falsa.py SOCKET ARQUIVO_DE_LOG"""
import json, os, socket, sys, threading, time

sock_path, log_path = sys.argv[1], sys.argv[2]


def tratar(c):
    f = c.makefile("rwb")
    linha = f.readline().decode().strip()
    with open(log_path, "a") as lg:
        lg.write(linha.replace("\n", "\\n") + "\n")
    cmd, _, resto = linha.partition(" ")
    if cmd != "pergunta":
        f.write(b'{"ok":false,"erro":"so pergunta"}\n')
        f.flush()
        return
    p = json.loads(resto)
    t = p.get("texto", "")
    if "LENTO" in t:
        time.sleep(6)
        r = {"ok": True, "resposta": "tarde demais"}
    elif "Explique este comando" in t and "pulse-lento" in t:
        time.sleep(5)
        r = {"ok": True, "resposta": "tarde demais\nRISCO: alto"}
    elif "SEM-EMMA" in t:
        r = {"ok": False, "erro": "sem resposta a tempo"}
    elif "<historico_encontrado>" in t:
        r = {"ok": True, "resposta": "docker ps\nSegundo o histórico, você usou o docker ontem."}
    elif "docker" in t.lower() and "HISTORICO-PEDE" in t:
        r = {"ok": True, "resposta": "[[HISTORICO: docker]]"}
    elif "<saida_do_comando>" in t and "<comando>pulse-erro-" in t:
        # os testes do cartão de correção: cada comando que falha ganha uma sugestão que deixa uma marca no EMMA_FALSA_DIR (ou nenhuma)
        n = t.split("<comando>pulse-erro-")[1][0]  # (o histórico também cita os comandos de antes: vale o da marca <comando>)
        d = os.environ.get("EMMA_FALSA_DIR", "/tmp")
        resp = {"1": f"touch {d}/m1\nA sugestão um.", "2": f"touch {d}/m2\nA sugestão dois.", "3": f"touch {d}/m3\nA sugestão três.",
                "4": f"rm -f {d}/m4\nApaga o arquivo m4.", "5": f"touch {d}/m5\nA sugestão cinco.", "6": "NENHUM\nSó uma explicação: o comando não existe."}.get(n, "NENHUM")
        r = {"ok": True, "resposta": resp}
    elif "<saida_do_comando>" in t:
        r = {"ok": True, "resposta": "ls -la\nFaltava o -la para ver os arquivos ocultos."}
    elif "linguagem natural): pulse-nl-" in t:
        # os testes do campo da Emma (Ctrl+E): o pedido vem depois de "linguagem natural): "
        pedido = t.rsplit("linguagem natural): ", 1)[1]
        n = pedido[len("pulse-nl-"):][0]
        d = os.environ.get("EMMA_FALSA_DIR", "/tmp")
        if n == "5":
            time.sleep(5)  # a Emma que demora (o teste muda o prazo para 1 s)
        resp = {"1": f"```bash\ntouch {d}/n1\n```\nCria o arquivo n1.", "2": f"rm -f {d}/n2\nApaga o n2.", "3": "NENHUM\nNão entendi: o que você quer criar?",
                "5": f"touch {d}/n5\ntarde demais"}.get(n, "NENHUM\nNão entendi.")
        r = {"ok": True, "resposta": resp}
    elif "linguagem natural" in t:
        r = {"ok": True, "resposta": "```bash\nls -S | head\n```\nLista os arquivos pelos maiores."}
    elif "Explique este comando" in t:
        r = {"ok": True, "resposta": "Apaga a pasta e tudo dentro dela, sem volta.\nRISCO: alto"}
    else:
        r = {"ok": True, "resposta": "NENHUM\nNão entendi."}
    f.write((json.dumps(r) + "\n").encode())
    f.flush()


if os.path.exists(sock_path):
    os.unlink(sock_path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(sock_path)
srv.listen(8)
while True:
    c, _ = srv.accept()
    threading.Thread(target=tratar, args=(c,), daemon=True).start()
