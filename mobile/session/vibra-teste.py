#!/usr/bin/env python3
"""Testa o motor de vibração direto, sem o LUMNA no meio (faz o papel do fftest).

Rode no celular:  sudo python mobile/session/vibra-teste.py [/dev/input/eventN]
Toca cada tipo de efeito, de curto a longo, e pergunta se você sentiu. No fim mostra o resumo.
"""
import fcntl, glob, os, struct, sys, time

EV_FF, FF_RUMBLE, FF_PERIODIC, FF_GAIN = 0x15, 0x50, 0x51, 0x60
FF_SQUARE, FF_SINE = 0x58, 0x5A
EVIOCSFF = 0x40304580  # _IOW('E', 0x80, struct ff_effect), 48 bytes
EVIOCRMFF = 0x40044581  # _IOW('E', 0x81, int)


def achar():
    for ev in sorted(glob.glob("/sys/class/input/event*")):
        ff = open(f"{ev}/device/capabilities/ff").read().split()
        if any(int(w, 16) for w in ff):
            nome = open(f"{ev}/device/name").read().strip()
            return f"/dev/input/{os.path.basename(ev)}", nome
    sys.exit("nenhum motor com force feedback em /sys/class/input")


def efeito(tipo, ms, union):
    cab = struct.pack("=HhHHHHH", tipo, -1, 0, 0, 0, ms, 0) + b"\0\0"
    return bytearray(cab + union.ljust(32, b"\0"))


def tocar(fd, buf, ms):
    fcntl.ioctl(fd, EVIOCSFF, buf)
    eid = struct.unpack_from("=h", buf, 2)[0]
    os.write(fd, struct.pack("=qqHHi", 0, 0, EV_FF, eid, 1))
    time.sleep(ms / 1000 + 0.4)
    fcntl.ioctl(fd, EVIOCRMFF, eid)


dev, nome = (sys.argv[1], "?") if len(sys.argv) > 1 else achar()
print(f"motor: {dev} ({nome})")
fd = os.open(dev, os.O_RDWR)
os.write(fd, struct.pack("=qqHHi", 0, 0, EV_FF, FF_GAIN, 0xFFFF))

rumble = lambda: struct.pack("=HH", 0xFFFF, 0xFFFF)
so_fraco = lambda: struct.pack("=HH", 0, 0xFFFF)
periodico = lambda forma: struct.pack("=HHhhH", forma, 10, 0x7FFF, 0, 0)
testes = [
    ("rumble forte+fraco", FF_RUMBLE, rumble),
    ("rumble só fraco", FF_RUMBLE, so_fraco),
    ("periódico seno", FF_PERIODIC, lambda: periodico(FF_SINE)),
    ("periódico quadrado", FF_PERIODIC, lambda: periodico(FF_SQUARE)),
]
resumo = []
for nome_t, tipo, union in testes:
    for ms in (30, 100, 500):
        input(f"\n[{nome_t}, {ms} ms] Enter pra tocar...")
        try:
            tocar(fd, efeito(tipo, ms, union()), ms)
        except OSError as e:
            print(f"  o kernel recusou: {e}")
            resumo.append(f"{nome_t} {ms}ms: recusado ({e.strerror})")
            continue
        r = input("  vibrou? (s/n) ").strip().lower()
        resumo.append(f"{nome_t} {ms}ms: {'VIBROU' if r.startswith('s') else 'nada'}")

print("\n=== resumo (me manda isso) ===")
print("\n".join(resumo))
