"""Imports that can kill the whole process.

A binary wheel built for a newer CPU (numpy 2.x wants x86-64-v2: SSE4.2 and POPCNT; PyTorch and ctranslate2 want more) does not raise an
ImportError on an older one: the interpreter dies with SIGILL at the `import`, and no `try` can catch that. On the first LUMNA notebook
(2011) that took the whole server down on startup, over and over. So anything optional that ships native code is tried in a throwaway child
process first; only when the child exits normally (or with a plain ImportError) does the server import it itself.
"""
import logging
import os
import subprocess
import sys

log = logging.getLogger("nativeok")
_crashes: dict[str, bool] = {}


def crashes(module: str) -> bool:
    """True if importing `module` kills the interpreter (a signal, usually SIGILL). A missing module is not a crash."""
    if module not in _crashes:
        env = dict(os.environ, PYTHONPATH=os.pathsep.join(p for p in sys.path if p), PYTHONFAULTHANDLER="0")
        try:
            r = subprocess.run([sys.executable, "-c", f"import {module}"], env=env, capture_output=True, timeout=90)
            _crashes[module] = r.returncode < 0
        except (subprocess.TimeoutExpired, OSError):
            _crashes[module] = False
        if _crashes[module]:
            log.warning("importing %s kills Python here (this CPU is too old for its binary wheel); it stays off", module)
    return _crashes[module]


def numpy_or_none():
    """numpy, or None if it is not installed or would crash this CPU."""
    if crashes("numpy"):
        return None
    try:
        import numpy  # noqa: PLC0415
    except ImportError:
        return None
    return numpy


CPU_MESSAGE = ("Este processador é antigo demais para a voz local: o numpy (que o Whisper e o Pocket TTS precisam) usa instruções que ele não tem. "
               "Use o Fish Audio, ou um computador mais novo.")
