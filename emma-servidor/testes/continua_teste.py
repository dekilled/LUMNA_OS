"""A conversa contínua do assistente: o intervalo, a virada do dia e quando ela deve cumprimentar. Rode: python3 testes/continua_teste.py"""
import asyncio, os, sys
from datetime import datetime, timedelta, timezone
from types import SimpleNamespace

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
os.environ.setdefault("JWT_SECRET", "x")
from types import ModuleType  # o banco e o perfil não entram no que se testa aqui: só as contas de tempo
for nome, attrs in (("core", {"db": None, "oid": lambda x: x}), ("nsfw_profile", {"scope": None})):
    m = ModuleType(nome); m.__dict__.update(attrs); sys.modules[nome] = m
import continua  # noqa: E402

falhas = 0
def confere(c, m):
    global falhas
    if not c:
        falhas += 1
        print("FALHOU:", m)

confere(continua.faz_quanto_tempo(timedelta(minutes=3)) == "3 min", "min")
confere(continua.faz_quanto_tempo(timedelta(hours=5, minutes=20)) == "5 h", "horas")
confere(continua.faz_quanto_tempo(timedelta(days=1, hours=3)) == "1 dia", "1 dia")
confere(continua.faz_quanto_tempo(timedelta(days=4)) == "4 dias", "dias")

# 23:50 de um dia e 00:10 do seguinte (UTC-03): são só 20 min, mas virou o dia
tz = continua._offset("sexta-feira, 03/10/2026 00:10 (UTC-03:00)")
a = datetime(2026, 10, 3, 2, 50, tzinfo=timezone.utc)  # 23:50 do dia 2 em UTC-3
b = datetime(2026, 10, 3, 3, 10, tzinfo=timezone.utc)  # 00:10 do dia 3
confere(continua.virou_o_dia(a, b, tz), "virou o dia à meia-noite local")
confere(not continua.virou_o_dia(a, a + timedelta(minutes=5), tz), "mesmo dia")

async def main():
    agora = datetime(2026, 10, 3, 15, 0, tzinfo=timezone.utc)
    cn = "sexta-feira, 03/10/2026 12:00 (UTC-03:00)"
    chat = SimpleNamespace(origin="")
    ass = SimpleNamespace(origin="assistente")
    h = lambda dt: [{"role": "user", "created_at": dt.isoformat()}]
    confere(await continua.nota(chat, h(agora - timedelta(days=2)), cn, agora) == "", "fora do assistente não mexe")
    n = await continua.nota(ass, h(agora - timedelta(minutes=3)), cn, agora)
    confere("cumprimente" not in n and "há" not in n, "no meio da troca: sem intervalo nem bom dia: " + n)
    n = await continua.nota(ass, h(agora - timedelta(minutes=45)), cn, agora)
    confere("há 45 min" in n and "cumprimente" not in n, "pausa curta só diz o intervalo: " + n)
    n = await continua.nota(ass, [], cn, agora)
    confere("primeira fala da conversa" in n, "conversa vazia")
asyncio.run(main())
print("continua ok" if not falhas else f"continua: {falhas} falha(s)")
sys.exit(1 if falhas else 0)
