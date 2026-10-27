# Monta, numa pasta nova, um banco do pacman de mentira (local/<nome>-<versão>/desc e files), uns arquivos de pacote, um AppImage
# integrado, um cache do yay e um pacman falso. Imprime as variáveis de ambiente para o teste.
import os, sys, stat
raiz = sys.argv[1]
db, fs, home, bin_ = raiz + "/db", raiz + "/fs", raiz + "/home", raiz + "/bin"
for d in (db, fs, home + "/Applications", home + "/.cache/yay/aurapp", home + "/.local/share/applications", bin_, fs + "/usr/share/applications"): os.makedirs(d, exist_ok=True)

def pacote(nome, ver, tam, deps=(), opt=(), prov=(), dependencia=False, arquivos=(), desktop=None, desc="", licenca=("GPL",)):
    d = f"{db}/{nome}-{ver}"; os.makedirs(d, exist_ok=True)
    def sec(k, v):
        v = [v] if isinstance(v, (str, int)) else list(v)
        return f"%{k}%\n" + "\n".join(str(x) for x in v) + "\n\n" if v else ""
    t = sec("NAME", nome) + sec("VERSION", ver) + sec("DESC", desc or f"O {nome}.") + sec("URL", f"https://{nome}.example") + sec("BUILDDATE", "1690000000") + \
        sec("INSTALLDATE", "1700000000") + sec("PACKAGER", "Alguém <a@b.c>") + sec("SIZE", tam) + (sec("REASON", "1") if dependencia else "") + sec("LICENSE", licenca) + \
        sec("DEPENDS", deps) + sec("OPTDEPENDS", opt) + sec("PROVIDES", prov)
    open(d + "/desc", "w").write(t)
    lista = list(arquivos)
    if desktop:
        nome_d, titulo, icone, extra = desktop
        os.makedirs(fs + "/usr/share/applications", exist_ok=True)
        open(f"{fs}/usr/share/applications/{nome_d}", "w").write(f"[Desktop Entry]\nType=Application\nName={titulo}\nExec={nome} %U\nIcon={icone}\n{extra}")
        lista.append(f"usr/share/applications/{nome_d}")
    open(d + "/files", "w").write("%FILES%\n" + "\n".join(lista) + "\n")
    for a in lista:
        if not a.endswith("/"):
            p = fs + "/" + a; os.makedirs(os.path.dirname(p), exist_ok=True)
            if not os.path.exists(p): open(p, "w").write("x")

pacote("foo", "1.0-1", 5000000, deps=("libbar>=1", "libonly"), opt=(), arquivos=("usr/", "usr/bin/", "usr/bin/foo", "usr/lib/foo/", "usr/lib/foo/core.so"),
       desktop=("foo.desktop", "Foo App", "foo-icon", ""), licenca=("GPL", "MIT"))
pacote("libbar", "1.2-1", 3000000, dependencia=True, prov=("libbar.so=1-64",), arquivos=("usr/lib/libbar.so",))
pacote("libonly", "0.1-1", 400000, dependencia=True, arquivos=("usr/lib/libonly.so",))
pacote("baz", "2.0-1", 1000000, deps=("libbar",), opt=("foo: integração",), arquivos=("usr/bin/baz",))
pacote("aurapp", "3.0-1", 2000000, arquivos=("usr/bin/aurapp",), desktop=("aurapp.desktop", "AUR App", "aur", ""))
pacote("optapp", "9-1", 7000000, arquivos=("opt/", "opt/optapp/", "opt/optapp/run"), desktop=("optapp.desktop", "Opt App", "opt", ""))
pacote("hiddenapp", "1-1", 100, arquivos=("usr/bin/h",), desktop=("hiddenapp.desktop", "Hidden", "h", "NoDisplay=true\n"))
# todos os arquivos dos pacotes são antigos, menos os dois que marcam a "última modificação" do foo
for base, _, nomes in os.walk(fs):
    for n_ in nomes: os.utime(os.path.join(base, n_), (1600000000, 1600000000))
os.utime(fs + "/usr/lib/foo/core.so", (1750000000, 1750000000)); os.utime(fs + "/usr/bin/foo", (1740000000, 1740000000))
# o AppImage com .desktop integrado
img = home + "/Applications/MyApp-1.0.AppImage"
open(img, "wb").write(b"\0" * 12345)
open(home + "/.local/share/applications/myapp.desktop", "w").write(f"[Desktop Entry]\nType=Application\nName=My App\nExec=\"{img}\" --no-sandbox\nIcon=myapp\n")
# o pacman falso: -Qmq lista o aurapp; -Rns anota; o -Rcns do libbar recusa
log = raiz + "/pacman.log"
open(bin_ + "/pacman", "w").write(f"""#!/bin/sh
echo "$@" >> {log}
case "$1" in
  -Qmq) echo aurapp;;
  -Rcns) [ "$3" = libbar ] && {{ echo "error: failed to prepare transaction (breaks dependency 'libbar' required by foo)" >&2; exit 1; }}; exit 0;;
  *) exit 0;;
esac
""")
os.chmod(bin_ + "/pacman", 0o755)
print(f"LUMNA_PACMAN_DB={db} LUMNA_PACMAN_RAIZ={fs} HOME={home} PATH={bin_}:/usr/bin:/bin LUMNA_SEM_PKEXEC=1 PACMAN_FALSO_LOG={log}")
