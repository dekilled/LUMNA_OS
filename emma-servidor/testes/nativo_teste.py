"""Imports que derrubam o processo (SIGILL num processador antigo): nativeok.crashes() prova num processo filho, e o RAG segue sem o numpy.
python3 testes/nativo_teste.py   (da pasta emma-servidor)"""
import os
import sys
import tempfile

aqui = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(aqui, "..", "backend"))

# o core.py do servidor pede estas variáveis ao importar
_t = tempfile.mkdtemp()
os.environ.setdefault("SECRETS_KEY", "a" * 64)
os.environ.setdefault("JWT_SECRET", "b" * 64)
os.environ.setdefault("SQLITE_PATH", f"{_t}/emma.db")
os.environ.setdefault("WORKSPACE_ROOT", f"{_t}/ws")
os.environ.setdefault("ADMIN_EMAIL", "x@lumna.local")
os.environ.setdefault("ADMIN_PASSWORD", "senha12345")
os.makedirs(f"{_t}/ws", exist_ok=True)

import nativeok  # noqa: E402

falhas = []


def confere(cond, msg):
    if not cond:
        falhas.append(msg)
        print("FALHOU:", msg, file=sys.stderr)


with tempfile.TemporaryDirectory() as d:
    # um "numpy" que morre com SIGILL ao importar, como o wheel novo num processador sem SSE4.2
    os.makedirs(f"{d}/falso/numpy")
    open(f"{d}/falso/numpy/__init__.py", "w").write("import os, signal\nos.kill(os.getpid(), signal.SIGILL)\n")
    open(f"{d}/bom.py", "w").write("X = 1\n")
    sys.path.insert(0, d)
    sys.path.insert(0, f"{d}/falso")
    confere(nativeok.crashes("numpy"), "um módulo que morre com SIGILL conta como queda")
    confere(nativeok.numpy_or_none() is None, "numpy_or_none devolve None quando o import derrubaria")
    confere(not nativeok.crashes("bom"), "um módulo normal não é queda")
    confere(not nativeok.crashes("modulo_que_nao_existe_xyz"), "um módulo que não existe é ImportError, não queda")
    sys.path.remove(f"{d}/falso")

# o RAG com e sem numpy dá as mesmas semelhanças
import importlib  # noqa: E402

real = None
try:
    import numpy as real  # noqa: E402,F811
except ImportError:
    pass

import rag  # noqa: E402

docs = [{"text": "um gato", "source": "a", "embedding": [1.0, 0.0, 0.5]}, {"text": "um cachorro", "source": "b", "embedding": [0.0, 1.0, 0.2]},
        {"text": "sem vetor", "source": "c"}]
consulta = [0.9, 0.1, 0.4]
sem_numpy = None
orig = rag.numpy_or_none
try:
    rag.numpy_or_none = lambda: None
    idx = rag._Index(docs)
    confere(isinstance(idx.matrix, list) and len(idx.matrix) == 2, "sem numpy a matriz é uma lista com os dois chunks que têm vetor")
    sem_numpy = rag._similarities(idx, consulta)
    confere(sem_numpy[0] > sem_numpy[1], "o gato fica mais perto da consulta")
finally:
    rag.numpy_or_none = orig
if real is not None:
    rag.numpy_or_none = lambda: real
    idx2 = rag._Index(docs)
    com_numpy = rag._similarities(idx2, consulta)
    confere(all(abs(a - b) < 1e-5 for a, b in zip(sem_numpy, com_numpy)), f"as semelhanças são as mesmas com e sem numpy ({sem_numpy} x {com_numpy})")
    rag.numpy_or_none = orig

if falhas:
    print(f"{len(falhas)} falha(s)")
    sys.exit(1)
print("nativo (import que derruba o processo, RAG sem numpy): ok")
