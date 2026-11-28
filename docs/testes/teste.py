#!/usr/bin/env python3
"""
O registro de testes do LUMNA (registro.tsv), pela linha de comando. Todo chat usa isto, ninguém edita o TSV na mão.

  python3 docs/testes/teste.py resumo                     quantos testes por área e estado
  python3 docs/testes/teste.py lista AREA [ESTADO]        os testes de uma área (ESTADO: pendente, falhou...; padrão: pendente)
  python3 docs/testes/teste.py ver ID                     um teste inteiro
  python3 docs/testes/teste.py marcar ID ESTADO [NOTA]    passou | falhou | parcial | obsoleto | pulado | pendente (a data é a de hoje)
  python3 docs/testes/teste.py novo AREA "GRUPO" "O QUE FAZER" "DEVE ACONTECER"   um teste novo (id G-1, M-1, P-1, H-1, D-1...)
  python3 docs/testes/teste.py area ID AREA               muda a área de um teste
  python3 docs/testes/teste.py pagina                     gera a página de acompanhamento (acompanhamento.html)

Estados: pendente (ninguém testou ainda), passou, falhou, parcial (funciona com ressalva: dizer qual na nota),
obsoleto (o teste não vale mais: a função mudou ou saiu), pulado (não dá para testar agora: dizer por quê).
"""
import datetime, os, sys
from collections import Counter
AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
from registro import AREAS, ESTADOS, grava, le

ARQ = os.path.join(AQUI, "registro.tsv")
PREFIXO = {"Gaming": "G", "Mídia": "M", "Painel": "P", "Hardware": "H", "Desktop": "D", "Celular": "C", "A classificar": "X"}


def area_ok(a):
    for x in AREAS:
        if x.lower() == a.lower() or x.lower().replace("í", "i") == a.lower().replace("í", "i"):
            return x
    sys.exit(f"área desconhecida: {a} (use {', '.join(AREAS)})")


def resumo(testes):
    print(f"{'área':<15}" + "".join(f"{e:>10}" for e in ESTADOS) + f"{'total':>8}")
    for a in AREAS:
        c = Counter(t["estado"] for t in testes if t["area"] == a)
        if c:
            print(f"{a:<15}" + "".join(f"{c.get(e, 0):>10}" for e in ESTADOS) + f"{sum(c.values()):>8}")
    c = Counter(t["estado"] for t in testes)
    print(f"{'TOTAL':<15}" + "".join(f"{c.get(e, 0):>10}" for e in ESTADOS) + f"{len(testes):>8}")


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    testes = le(ARQ)
    por_id = {t["id"]: t for t in testes}
    cmd = a[0]
    if cmd == "resumo":
        resumo(testes)
    elif cmd == "lista":
        area = area_ok(a[1])
        estado = a[2] if len(a) > 2 else "pendente"
        for t in testes:
            if t["area"] == area and (estado == "todos" or t["estado"] == estado):
                print(f"{t['id']:<10} [{t['estado']}] {t['fazer'][:90]}")
    elif cmd == "ver":
        t = por_id.get(a[1]) or sys.exit("não existe: " + a[1])
        for k, v in t.items():
            print(f"{k:>9}: {v}")
    elif cmd == "marcar":
        t = por_id.get(a[1]) or sys.exit("não existe: " + a[1])
        if a[2] not in ESTADOS:
            sys.exit(f"estado inválido: {a[2]} (use {', '.join(ESTADOS)})")
        t["estado"] = a[2]
        t["data"] = datetime.date.today().isoformat() if a[2] != "pendente" else ""
        if len(a) > 3:
            t["nota"] = " ".join(a[3:])
        grava(ARQ, testes)
        print(f"{t['id']}: {t['estado']}")
    elif cmd == "novo":
        area = area_ok(a[1])
        p = PREFIXO[area]
        n = 1 + max([int(t["id"].split("-")[1]) for t in testes if t["id"].startswith(p + "-")] or [0])
        t = {"id": f"{p}-{n}", "area": area, "grupo": a[2], "fazer": a[3], "esperado": a[4], "estado": "pendente", "data": "", "nota": "",
             "origem": datetime.date.today().isoformat()}
        testes.append(t)
        grava(ARQ, testes)
        print(t["id"])
    elif cmd == "area":
        t = por_id.get(a[1]) or sys.exit("não existe: " + a[1])
        t["area"] = area_ok(a[2])
        grava(ARQ, testes)
    elif cmd == "pagina":
        import pagina
        pagina.gera(testes)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
