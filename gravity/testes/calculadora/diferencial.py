#!/usr/bin/env python3
"""Contas sorteadas: o motor C contra frações exatas do Python. python3 diferencial.py BINARIO_DO_TESTE [N] [SEMENTE]"""
import random, subprocess, sys
from fractions import Fraction

binario = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 4000
semente = int(sys.argv[3]) if len(sys.argv) > 3 else 12345
rnd = random.Random(semente)

# precedência: soma 1, produto 2, unário 3, potência 4, número 5
def numero():
    k = rnd.random()
    if k < 0.5:
        v = rnd.randint(0, 999)
        return Fraction(v), str(v)
    if k < 0.8:
        casas = rnd.randint(1, 3)
        v = rnd.randint(0, 10 ** (casas + 2))
        f = Fraction(v, 10 ** casas)
        s = f"{v // 10 ** casas},{str(v % 10 ** casas).zfill(casas)}"
        if rnd.random() < 0.3:
            s = s.replace(",", ".") if (v // 10 ** casas) < 1000 and casas != 3 else s
            # "1.5" é decimal; com 3 casas "1.500" seria milhar: evitado acima
        return f, s
    v = rnd.randint(1000, 9_999_999)
    s = f"{v:,}".replace(",", ".")
    return Fraction(v), s


def gera(prof):
    """devolve (valor, texto, precedência) ou valor None se der divisão por zero"""
    if prof <= 0 or rnd.random() < 0.25:
        v, s = numero()
        return v, s, 5
    k = rnd.random()
    if k < 0.10:
        v, s, p = gera(prof - 1)
        if v is None:
            return None, "", 0
        return -v, "-" + (f"({s})" if p < 4 else s), 3
    if k < 0.18:
        v, s, p = gera(prof - 1)
        if v is None:
            return None, "", 0
        e = rnd.choice([0, 1, 2, 3, -1, -2])
        if v == 0 and e < 0:
            return None, "", 0
        base = f"({s})" if p < 5 else s
        return v ** e, f"{base}^{'-' if e < 0 else ''}{abs(e)}" if e >= 0 else f"{base}^({e})", 4
    op = rnd.choice(["+", "-", "*", "/", "*", "+"])
    a, sa, pa = gera(prof - 1)
    b, sb, pb = gera(prof - 1)
    if a is None or b is None:
        return None, "", 0
    prec = 1 if op in "+-" else 2
    if op == "/" and b == 0:
        return None, "", 0
    # esquerda: parênteses se a precedência for menor; direita: também se igual (a esquerda associa primeiro)
    sa2 = f"({sa})" if pa < prec else sa
    sb2 = f"({sb})" if pb <= prec else sb
    simb = {"+": " + ", "-": " - ", "*": rnd.choice([" × ", "*", " x "]), "/": rnd.choice([" ÷ ", "/"])}[op]
    val = {"+": a + b, "-": a - b, "*": a * b, "/": (a / b if b != 0 else None)}[op]
    return val, sa2 + simb + sb2, prec


casos = []
while len(casos) < N:
    v, s, _ = gera(4)
    if v is None or abs(v) > Fraction(10) ** 12 or (v != 0 and abs(v) < Fraction(1, 10 ** 8)):
        continue
    casos.append((v, s))

entrada = "\n".join(s for _, s in casos) + "\n"
saida = subprocess.run([binario, "--stdin"], input=entrada, capture_output=True, text=True, timeout=120).stdout.splitlines()
if len(saida) != len(casos):
    print(f"FALHOU: {len(casos)} contas, {len(saida)} respostas")
    sys.exit(1)
falhas = 0
for (v, s), r in zip(casos, saida):
    if r.startswith("ERRO"):
        print(f"FALHOU: erro numa conta boa: {s!r} → {r}")
        falhas += 1
        continue
    got = float(r)
    esperado = float(v)
    tol = max(abs(esperado) * 1e-9, 5e-10)  # o resultado mostrado tem 10 casas decimais
    if abs(got - esperado) > tol:
        print(f"FALHOU: {s!r}: esperava {esperado!r}, veio {got!r}")
        falhas += 1
    if falhas > 15:
        break
print(f"calculadora (diferencial): {len(casos)} contas, {falhas} falhas")
sys.exit(1 if falhas else 0)
