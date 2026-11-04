"""O cliente MCP (backend/mcp_client.py) contra um servidor MCP de mentira por stdio. Precisa do pacote mcp 1.x. Rode: python3 testes/mcp_teste.py"""
import asyncio, os, sys, tempfile

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(AQUI, "..", "backend"))
os.environ.setdefault("JWT_SECRET", "x")
os.environ.setdefault("SECRETS_KEY", "teste")
os.environ["SQLITE_PATH"] = os.path.join(tempfile.mkdtemp(), "t.db")
import mcp_client as M  # noqa: E402
import tools as T  # noqa: E402

falhas = 0
def confere(c, m):
    global falhas
    if not c:
        falhas += 1
        print("FALHOU:", m)

async def espera(c, seg=40):
    for _ in range(seg * 10):
        if c.status in ("ok", "error"):
            return
        await asyncio.sleep(0.1)

async def main():
    c = M.Conn({"_id": "abc123", "name": "Teste Falso", "transport": "stdio", "command": f"{sys.executable} {os.path.join(AQUI, 'servidor_mcp_falso.py')}", "env": {}})
    c.start()
    await espera(c)
    confere(c.status == "ok", f"devia conectar: {c.status} {c.error}")
    soma, nota = T.REGISTRY.get("mcp_teste_falso__somar"), T.REGISTRY.get("mcp_teste_falso__ler_nota")
    confere(soma and nota, f"as ferramentas não entraram no registro: {list(T.REGISTRY)[-6:]}")
    confere(soma.default == "ask" and soma.risk == "act" and soma.where == "server", "uma ferramenta MCP nasce 'ask' e 'act'")
    confere(nota.risk == "read" and nota.default == "ask", "só-leitura declarada vira risk=read, mas continua perguntando")
    confere(len(T.REGISTRY["mcp_teste_falso__longa"].description) < 700, "a descrição de terceiros devia ser cortada")
    confere(all(len(n) <= 64 for n in T.REGISTRY), "nome de ferramenta com mais de 64 letras")
    o = await soma.run({"a": 2, "b": 3})
    confere(o.status == "ok" and o.content.strip() == "5" and o.untrusted, f"somar: {o}")
    o = await nota.run({"nome": "x"})
    confere(o.untrusted and "nota x" in o.content, "o resultado vai como conteúdo de fora")
    o = await T.REGISTRY["mcp_teste_falso__quebrar"].run({})
    confere(o.status == "error", f"uma ferramenta que quebra devia voltar com erro: {o}")
    await c.stop()
    confere(not any(n.startswith("mcp_teste_falso") for n in T.REGISTRY), "ao parar, as ferramentas saem do registro")

    ruim = M.Conn({"_id": "zzz999", "name": "Ruim", "transport": "stdio", "command": "programa-que-nao-existe --x", "env": {}})
    ruim.start()
    await espera(ruim)
    confere(ruim.status == "error" and "não achei o programa" in ruim.error, f"comando inexistente: {ruim.status} {ruim.error}")
    await ruim.stop()
    confere(M.tool_name("GitHub Server!", "Create Issue") == "mcp_github_server__create_issue", "o nome da ferramenta")

asyncio.run(main())
print("mcp ok" if not falhas else f"mcp: {falhas} falha(s)")
sys.exit(1 if falhas else 0)
