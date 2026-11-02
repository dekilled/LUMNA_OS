"""O leitor de chamadas de ferramenta escritas em texto (tools.find_call) com o que o Gemma realmente escreve:
texto longo entre aspas com quebras de linha de verdade, aspas e apóstrofos dentro. Uso: python testes/tools_parse_teste.py"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "backend"))
os.environ.setdefault("SECRETS_KEY", "teste")
import tools

NAMES = {"lumna_arquivos_escrever", "lumna_comando_executar", "lumna_arquivos_ler"}
falhas = 0


def confere(cond, msg):
    global falhas
    if not cond:
        falhas += 1
        print("FALHOU:", msg)


conto = 'O servidor estava operando no limite.\n\n"Eric, se você continuar me olhando assim, eu vou ter um crash", Emma provocou.\nEle sorriu: d\'água fria não resolvia.'

# 1) o caso real: aspas simples, quebras de linha de verdade, aspas duplas e apóstrofo dentro
raw = "```tool_code\nlumna_arquivos_escrever(caminho='~/Emma/Histórias/conto.txt', conteudo='" + conto.replace("'", "'") + "', motivo='guardar o conto')\n```"
r = tools.find_call(raw, NAMES)
confere(r is not None and r[0] == "lumna_arquivos_escrever", f"não achou a chamada: {r}")
if r:
    confere(r[1].get("caminho") == "~/Emma/Histórias/conto.txt", f"caminho: {r[1]}")
    confere(r[1].get("conteudo") == conto, f"o conteúdo veio diferente: {r[1].get('conteudo')!r}")
    confere(r[1].get("motivo") == "guardar o conto", f"motivo: {r[1]}")
    confere(r[3] == 0 and r[4] == len(raw), "a chamada inteira (o bloco) tem de sair do texto da resposta")

# 2) aspas duplas, e o mesmo com print(default_api....)
raw2 = '```tool_code\nprint(default_api.lumna_arquivos_escrever(caminho="~/Emma/a.md", conteudo="linha 1\nlinha 2 com \'apóstrofo\'", motivo="teste"))\n```'
r = tools.find_call(raw2, NAMES)
confere(r is not None and r[1].get("conteudo") == "linha 1\nlinha 2 com 'apóstrofo'", f"aspas duplas e print: {r}")

# 3) aspas triplas
raw3 = '```tool_code\nlumna_arquivos_escrever(caminho="~/Emma/b.md", conteudo="""primeira\nsegunda "citada"\n""", motivo="x")\n```'
r = tools.find_call(raw3, NAMES)
confere(r is not None and r[1].get("conteudo") == 'primeira\nsegunda "citada"\n', f"aspas triplas: {r}")

# 4) o que já funcionava continua: uma chamada comum, com \n escapado
raw4 = '```tool_code\nlumna_comando_executar(comando="echo oi", motivo="testar")\n```'
r = tools.find_call(raw4, NAMES)
confere(r is not None and r[1] == {"comando": "echo oi", "motivo": "testar"}, f"chamada simples: {r}")
raw5 = "```tool_code\nlumna_arquivos_escrever(caminho='~/Emma/c.md', conteudo='a\\nb', motivo='m')\n```"
r = tools.find_call(raw5, NAMES)
confere(r is not None and r[1].get("conteudo") == "a\nb", f"\\n escapado: {r}")

# 5) um número e um booleano
raw6 = '```tool_code\nlumna_comando_executar(comando="sleep 1", motivo="m", segundos=5)\n```'
r = tools.find_call(raw6, NAMES)
confere(r is not None and r[1].get("segundos") == 5, f"número: {r}")

# 6) texto comum e um bloco de código que não é chamada não viram chamada
confere(tools.find_call("Aqui vai um exemplo:\n```python\nprint('oi')\n```", NAMES) is None, "um exemplo de código virou chamada")
confere(tools.find_call("Claro! Vou escrever o conto agora.", NAMES) is None, "texto comum virou chamada")
# uma ferramenta que não foi oferecida não é chamada
confere(tools.find_call("```tool_code\nlumna_nao_existe(a='b')\n```", NAMES) is None, "ferramenta fora da lista virou chamada")

print("tools_parse ok" if not falhas else f"tools_parse: {falhas} falha(s)")
sys.exit(1 if falhas else 0)
