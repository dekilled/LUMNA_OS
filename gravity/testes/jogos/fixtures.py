#!/usr/bin/env python3
"""Uma Steam, um Lutris e um Heroic de mentira. fixtures.py PASTA (cria PASTA/steam, PASTA/lutris.db, PASTA/heroic e os programas falsos em PASTA/bin)."""
import os, sqlite3, struct, sys, zlib

raiz = sys.argv[1]


def png(caminho, cor):
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    linhas = b"".join(b"\x00" + bytes(cor) * 8 for _ in range(8))
    open(caminho, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(linhas)) + chunk(b"IEND", b""))


st = f"{raiz}/steam"
os.makedirs(f"{st}/steamapps/common", exist_ok=True)
os.makedirs(f"{st}/outra/steamapps/common", exist_ok=True)
os.makedirs(f"{st}/userdata/123/config", exist_ok=True)
open(f"{st}/steamapps/libraryfolders.vdf", "w").write('"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"%s/outra"\n\t}\n}\n' % (st, st))


def acf(pasta, appid, nome, instalado, pasta_jogo, tamanho):
    open(f"{pasta}/steamapps/appmanifest_{appid}.acf", "w").write(
        '"AppState"\n{\n\t"appid"\t\t"%s"\n\t"name"\t\t"%s"\n\t"StateFlags"\t\t"%d"\n\t"installdir"\t\t"%s"\n\t"SizeOnDisk"\t\t"%d"\n\t"buildid"\t\t"777"\n}\n'
        % (appid, nome, 4 if instalado else 1026, pasta_jogo, tamanho))


acf(st, 1001, "Terras Distantes", True, "TerrasDistantes", 68400000000)
acf(f"{st}/outra", 1002, "Horizonte \\\"Rival\\\"", True, "HorizonteRival", 1200000000)
acf(st, 1003, "Ainda Baixando", False, "Baixando", 5)
acf(st, 1004, "Proton 9.0", True, "Proton 9.0", 100)
open(f"{st}/userdata/123/config/localconfig.vdf", "w").write(
    '"UserLocalConfigStore"\n{\n\t"Software"\n\t{\n\t\t"Valve"\n\t\t{\n\t\t\t"Steam"\n\t\t\t{\n\t\t\t\t"apps"\n\t\t\t\t{\n'
    '\t\t\t\t\t"1001"\n\t\t\t\t\t{\n\t\t\t\t\t\t"LastPlayed"\t\t"1700000000"\n\t\t\t\t\t\t"Playtime"\t\t"1440"\n\t\t\t\t\t}\n'
    '\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n')
os.makedirs(f"{st}/appcache/librarycache/1001", exist_ok=True)
png(f"{st}/appcache/librarycache/1001/library_600x900.jpg", (200, 100, 50))
png(f"{st}/appcache/librarycache/1001/library_hero.jpg", (50, 100, 200))
os.makedirs(f"{st}/appcache/librarycache", exist_ok=True)
png(f"{st}/appcache/librarycache/1002_header.jpg", (10, 10, 10))

db = sqlite3.connect(f"{raiz}/lutris.db")
db.execute("CREATE TABLE games (id INTEGER PRIMARY KEY, slug TEXT, name TEXT, runner TEXT, directory TEXT, installed INTEGER, lastplayed INTEGER, playtime REAL)")
db.execute("INSERT INTO games (slug, name, runner, directory, installed, lastplayed, playtime) VALUES ('celeste', 'Celeste', 'wine', '/games/celeste', 1, 1700001000, 3.5)")
db.execute("INSERT INTO games (slug, name, runner, directory, installed, lastplayed, playtime) VALUES ('naoinstalado', 'Nao Instalado', 'wine', '', 0, 0, 0)")
db.commit()

he = f"{raiz}/heroic"
os.makedirs(f"{he}/legendaryConfig/legendary", exist_ok=True)
os.makedirs(f"{he}/gog_store", exist_ok=True)
open(f"{he}/legendaryConfig/legendary/installed.json", "w").write('{"Sugar": {"title": "Rocket Arena", "install_path": "/epic/Rocket", "version": "1.2", "install_size": 5000000000}}')
open(f"{he}/gog_store/installed.json", "w").write('{"installed": [{"appName": "1207658930", "title": "Witcher Antigo", "install_path": "/gog/w", "version": "2", "install_size": 100}]}')

os.makedirs(f"{raiz}/bin", exist_ok=True)
for p in ("steam", "lutris", "heroic", "wine"):
    open(f"{raiz}/bin/{p}", "w").write(f'#!/bin/sh\necho "{p} $*" >> "$FALSO_LOG"\n')
    os.chmod(f"{raiz}/bin/{p}", 0o755)
open(f"{raiz}/bin/powerprofilesctl", "w").write('#!/bin/sh\n[ "$1" = get ] && echo balanced\necho "powerprofilesctl $*" >> "$FALSO_LOG"\n')
os.chmod(f"{raiz}/bin/powerprofilesctl", 0o755)
os.makedirs(f"{raiz}/avulso/Jogo", exist_ok=True)
open(f"{raiz}/avulso/Jogo/jogo.sh", "w").write('#!/bin/sh\necho "avulso rodou $* em $PWD $MEUVAR" >> "$FALSO_LOG"\n')
os.chmod(f"{raiz}/avulso/Jogo/jogo.sh", 0o755)
