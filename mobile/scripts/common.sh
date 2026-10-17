# Funções comuns. Uso: . "$(dirname "$0")/common.sh" <aparelho>
set -eu

OS_DIR=$(cd "$(dirname "$0")/.." && pwd)
DEVICE=${1:-}
[ -n "$DEVICE" ] || { echo "uso: $0 <aparelho>  (ex.: oneplus-fajita)"; ls "$OS_DIR/devices"; exit 1; }
[ -f "$OS_DIR/devices/$DEVICE/device.conf" ] || { echo "aparelho desconhecido: $DEVICE"; exit 1; }
. "$OS_DIR/devices/$DEVICE/device.conf"

CACHE="$OS_DIR/cache"
OUT="$OS_DIR/out/$DEVICE"
mkdir -p "$CACHE" "$OUT"

die() { echo "erro: $*" >&2; exit 1; }
# Confere tudo de uma vez e diz o pacote de cada comando que falta
need() {
    falta=
    for c in "$@"; do command -v "$c" >/dev/null || falta="$falta $c"; done
    [ -z "$falta" ] && return 0
    echo "erro: faltam comandos no PC:" >&2
    for c in $falta; do
        case $c in
            ld.lld) p="lld" ;;
            llvm-*) p="llvm" ;;
            depmod) p="kmod" ;;
            mkfs.ext4) p="e2fsprogs" ;;
            bsdtar) p="libarchive (Arch) / libarchive-tools (Debian/Ubuntu) / bsdtar (Fedora)" ;;
            mountpoint|mount) p="util-linux" ;;
            md5sum|chroot) p="coreutils" ;;
            ar) p="binutils" ;;
            python3) p="python (Arch) / python3 (Debian/Ubuntu/Fedora)" ;;
            fastboot) p="android-tools (Arch/Fedora) / fastboot (Debian/Ubuntu)" ;;
            qemu-system-aarch64) p="qemu-system-aarch64 (Arch/Fedora) / qemu-system-arm (Debian/Ubuntu)" ;;
            *) p="$c" ;;
        esac
        echo "  $c  ->  pacote: $p" >&2
    done
    echo "lista completa em mobile/README.md, seção \"Preparar o PC\"" >&2
    exit 1
}

# Baixa uma vez para o cache e devolve o caminho
fetch() {
    f="$CACHE/$2"
    [ -s "$f" ] || { curl -fL --retry 3 -o "$f.part" "$1" && mv "$f.part" "$f"; } >&2 ||
        die "não consegui baixar $1"
    echo "$f"
}

# Igual ao fetch, mas se o .tar.gz for recusado (o GitLab às vezes dá 403)
# monta o mesmo tarball com git: fetch_git <url> <arquivo> <repo git> <commit/tag>
fetch_git() {
    f="$CACHE/$2"
    if [ ! -s "$f" ]; then
        { curl -fL --retry 3 -o "$f.part" "$1" && mv "$f.part" "$f"; } >&2 || {
            echo "download recusado, tentando com git: $3" >&2
            g="$CACHE/git-${2%.tar.gz}"
            rm -rf "$g" && git init -q "$g" &&
                git -C "$g" fetch -q --depth 1 "$3" "$4" &&
                git -C "$g" archive --format=tar.gz --prefix="${2%.tar.gz}/" -o "$f.part" FETCH_HEAD &&
                mv "$f.part" "$f" && rm -rf "$g" || die "não consegui baixar $3 ($4)"
        }
    fi
    echo "$f"
}

pmaports_file() {
    fetch "https://gitlab.postmarketos.org/postmarketOS/pmaports/-/raw/$PMAPORTS_REF/$1" "pmaports-${PMAPORTS_REF%"${PMAPORTS_REF#???????}"}-$(basename "$1")"
}
