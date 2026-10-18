#!/bin/sh
# Compila o lumna-shell pro celular (aarch64) NO PC, sem emular nada: o Rust do PC gera código
# ARM direto (compilação cruzada) e linka contra as bibliotecas ARM (WebKit, GTK...) de um
# sysroot baixado do Arch Linux ARM. O resultado é um pacote pacman normal pro celular.
#
# Rode no PC (Arch x86_64), como seu usuário, na raiz do LUMNA_OS:
#   sh mobile/scripts/build-pc.sh                       # gera out/lumna-shell-*-aarch64.pkg.tar.zst
#   sh mobile/scripts/build-pc.sh alarm@192.168.0.23    # e já instala no celular pelo ssh
#
# Precisa: rustup, clang, lld, pnpm, nodejs (o script diz o que falta). A primeira vez baixa o
# sysroot (~600 MB, em ~/.cache/lumna/sysroot-aarch64, pede sudo pro pacman); depois só compila.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PHONE=${1:-}
TARGET=aarch64-unknown-linux-gnu
SYS=${LUMNA_SYSROOT:-$HOME/.cache/lumna/sysroot-aarch64}
# Espelho do Arch Linux ARM (troque por um mais perto: LUMNA_ALARM_MIRROR=http://br.mirror.archlinuxarm.org)
MIRROR=${LUMNA_ALARM_MIRROR:-http://mirror.archlinuxarm.org}
OUT="$ROOT/out"

die() { echo "erro: $*" >&2; exit 1; }
[ "$(id -u)" != 0 ] || die "rode como seu usuário (o script pede sudo só pro sysroot)"
falta=
for c in rustup cargo clang ld.lld llvm-ar pnpm node pacman makepkg; do command -v "$c" >/dev/null || falta="$falta $c"; done
[ -z "$falta" ] || die "faltam no PC:$falta  (Arch: sudo pacman -S --needed rustup clang lld llvm pnpm nodejs base-devel; depois: rustup default stable)"

# ---- 1. sysroot ARM: só as bibliotecas e os cabeçalhos pra linkar ----
# O pacman do PC instala pacotes aarch64 numa pasta à parte (--root, --arch). Nada roda: sem
# scripts de instalação. As assinaturas não são conferidas (o chaveiro do ALARM não está no PC);
# o sysroot só serve pra linkar, nada dele vai pro celular.
DEPS="glibc gcc gcc-libs linux-api-headers webkit2gtk-4.1 gtk3 gtk-layer-shell libsoup3 systemd-libs openssl zlib"
if [ ! -f "$SYS/.lumna-ok" ] || [ -n "${LUMNA_SYSROOT_UPDATE:-}" ]; then
    echo "==> sysroot aarch64 em $SYS"
    # o pacman não cria essas sozinho com --root/--cachedir
    mkdir -p "$SYS/var/lib/pacman" "$SYS/var/cache/pacman/pkg" "$SYS/var/log" "$SYS/etc" "$SYS/tmp"
    conf="$SYS/etc/lumna-pacman.conf"
    cat > "$conf" <<EOF
[options]
Architecture = aarch64
SigLevel = Never
NoExtract = usr/share/doc/* usr/share/man/* usr/share/locale/* usr/share/help/* usr/share/gtk-doc/*

[core]
Server = $MIRROR/aarch64/\$repo
[extra]
Server = $MIRROR/aarch64/\$repo
[alarm]
Server = $MIRROR/aarch64/\$repo
EOF
    sudo pacman --root "$SYS" --dbpath "$SYS/var/lib/pacman" --config "$conf" --arch aarch64 \
        --cachedir "$SYS/var/cache/pacman/pkg" --noscriptlet --noconfirm --needed -Sy $DEPS
    # links absolutos (/usr/lib/...) dentro do sysroot apontariam pro PC: viram relativos
    find "$SYS/usr/lib" -maxdepth 1 -type l | while read -r l; do
        t=$(readlink "$l")
        case $t in /*) sudo ln -sfn "$SYS$t" "$l" ;; esac
    done
    sudo chown -R "$(id -u):$(id -g)" "$SYS"
    touch "$SYS/.lumna-ok"
fi

# ---- 2. compilação cruzada ----
rustup target add "$TARGET" >/dev/null
gccdir=$(ls -d "$SYS"/usr/lib/gcc/aarch64-unknown-linux-gnu/* 2>/dev/null | tail -1)
[ -n "$gccdir" ] || die "o sysroot ficou sem o gcc ARM (rode de novo com LUMNA_SYSROOT_UPDATE=1)"
cross="--target=$TARGET --sysroot=$SYS"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=clang
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_RUSTFLAGS="-C link-arg=--target=$TARGET -C link-arg=--sysroot=$SYS -C link-arg=-fuse-ld=lld -C link-arg=-B$gccdir -C link-arg=-L$gccdir"
export CC_aarch64_unknown_linux_gnu=clang
export CFLAGS_aarch64_unknown_linux_gnu="$cross -B$gccdir"
export CXX_aarch64_unknown_linux_gnu=clang++
export CXXFLAGS_aarch64_unknown_linux_gnu="$cross -B$gccdir"
export AR_aarch64_unknown_linux_gnu=llvm-ar
export PKG_CONFIG_ALLOW_CROSS=1
export PKG_CONFIG_SYSROOT_DIR="$SYS"
export PKG_CONFIG_LIBDIR="$SYS/usr/lib/pkgconfig:$SYS/usr/share/pkgconfig"
# no PC sobra memória: o release de verdade (LTO, 1 codegen unit)

echo "==> interface + lumna-shell (aarch64)"
cd "$ROOT/shell"
pnpm install --frozen-lockfile
pnpm tauri build --no-bundle --target "$TARGET"
cd "$ROOT"
echo "==> lumna-ctl (aarch64)"
cargo build --release --target "$TARGET" -p lumna-ctl

# ---- 3. pacote pacman pro celular ----
echo "==> pacote"
mkdir -p "$OUT"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp "$ROOT/packages/lumna-shell/PKGBUILD" "$work/"
sed "s|^CARCH=.*|CARCH=\"aarch64\"|; s|^CHOST=.*|CHOST=\"aarch64-unknown-linux-gnu\"|" /etc/makepkg.conf > "$work/makepkg.conf"
(
    cd "$work"
    # o PKGBUILD pula a compilação e só empacota os binários que já estão prontos
    LUMNA_PREBUILT="$ROOT/target/$TARGET/release" PKGDEST="$OUT" \
        makepkg --config "$work/makepkg.conf" -f --nodeps --noextract --nocheck --skipinteg
)
pkg=$(ls -t "$OUT"/lumna-shell-*-aarch64.pkg.tar.zst | head -1)
echo "ok: $pkg"

# ---- 4. (opcional) manda e instala no celular ----
if [ -n "$PHONE" ]; then
    echo "==> instalando em $PHONE"
    scp "$pkg" "$PHONE:/tmp/"
    ssh -t "$PHONE" "sudo pacman -U --noconfirm /tmp/$(basename "$pkg") && rm /tmp/$(basename "$pkg")"
    echo "ok: instalado. No celular: reinicie a sessão (ou rode lumna-mobile.sh com LUMNA_PACOTE pra reconfigurar)"
else
    echo "no celular: sudo pacman -U $(basename "$pkg")   (ou: LUMNA_PACOTE=<arquivo> sh mobile/session/lumna-mobile.sh)"
fi
