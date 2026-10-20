#!/bin/sh
# Os pacotes do celular no GitHub Actions (workflow "Celular"): monta um Arch Linux ARM (o mesmo
# sistema do celular) numa máquina ARM do GitHub, compila lá dentro, sem emulação, e deixa o
# repositório [lumna-celular] em out/celular (o workflow publica na release "celular").
#
# Roda como root (sudo), na raiz do LUMNA_OS, com o histórico inteiro (as versões vêm do git):
#   sudo -E sh mobile/scripts/ci-celular.sh
# EMMA_SRC: o NewHarnessV2 (sem ele, a Emma fica com o pacote que já está publicado).
# LUMNA_CACHE: pasta guardada entre builds (cargo, pnpm e o target do Rust).
# Pacote cuja versão já está publicada não é compilado de novo: vem da release.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CACHE=${LUMNA_CACHE:-$ROOT/.cache-ci}
OUT="$ROOT/out/celular"
REPO_URL=${LUMNA_REPO_URL:-https://github.com/dekilled/LUMNA_OS/releases/download/celular}
ALARM_URL=${LUMNA_ALARM_URL:-http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz}
CH=/var/lib/lumna-alarm

die() { echo "erro: $*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "rode com sudo"
[ "$(uname -m)" = aarch64 ] || die "precisa de uma máquina ARM (runs-on: ubuntu-24.04-arm)"

# ---- versões (pelo git, aqui fora: lá dentro não tem o histórico) ----
# (os clones são do usuário do runner; o git, como root, só lê com isto)
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=safe.directory GIT_CONFIG_VALUE_0='*'
V_SHELL=$(sh "$ROOT/mobile/scripts/versao.sh" lumna-shell)
V_MOBILE=$(sh "$ROOT/mobile/scripts/versao.sh" lumna-mobile)
V_EMMA=
[ -z "${EMMA_SRC:-}" ] || V_EMMA=$(sh "$ROOT/mobile/scripts/versao.sh" lumna-emma "$EMMA_SRC")
echo "lumna-shell $V_SHELL · lumna-mobile $V_MOBILE · lumna-emma ${V_EMMA:-(a publicada)}"

mkdir -p "$OUT"
rm -f "$OUT"/*

# o que já está publicado (pra não compilar de novo o que não mudou)
PUBLICADO=$(curl -fsSL "$REPO_URL/lumna-celular.db" 2>/dev/null | tar -tzf - 2>/dev/null | sed -n 's|/desc$||p' || true)
echo "publicado:" $PUBLICADO
baixa() { # baixa NOME VERSÃO → 0 se veio da release
    [ -n "$2" ] && printf '%s\n' "$PUBLICADO" | grep -qx "$1-$2-1" || return 1
    curl -fsSL -o "$OUT/$1-$2-1-aarch64.pkg.tar.zst" "$REPO_URL/$1-$2-1-aarch64.pkg.tar.zst" && echo "$1 $2: já publicado, reaproveitado"
}
FAZ=
baixa lumna-shell "$V_SHELL" || FAZ="$FAZ lumna-shell"
baixa lumna-mobile "$V_MOBILE" || FAZ="$FAZ lumna-mobile"
if [ -n "$V_EMMA" ]; then
    baixa lumna-emma "$V_EMMA" || FAZ="$FAZ lumna-emma"
else
    # sem o código da Emma: a última publicada continua no repositório
    v=$(printf '%s\n' "$PUBLICADO" | sed -n 's/^lumna-emma-\(.*\)-1$/\1/p' | head -1)
    [ -z "$v" ] || baixa lumna-emma "$v" || true
fi
echo "compilar:${FAZ:- nada}"

if [ -n "$FAZ" ]; then
    # ---- o Arch Linux ARM ----
    if [ ! -x "$CH/usr/bin/pacman" ]; then
        echo "==> Arch Linux ARM em $CH"
        mkdir -p "$CH"
        curl -fsSL --retry 3 "$ALARM_URL" | tar -xzp --numeric-owner -C "$CH" -f -
    fi
    sobe() { mountpoint -q "$CH$1" || { mkdir -p "$CH$1"; mount --bind "$2" "$CH$1"; }; }
    # shellcheck disable=SC2154
    trap 'for m in /home/alarm/cache /dev/pts /dev /sys /proc ""; do umount -l "$CH$m" 2>/dev/null || true; done' EXIT
    mountpoint -q "$CH" || mount --bind "$CH" "$CH" # o pacman confere o espaço livre pelo ponto de montagem
    sobe /proc /proc
    sobe /sys /sys
    sobe /dev /dev
    sobe /dev/pts /dev/pts
    mkdir -p "$CACHE/alarm"
    cp -L /etc/resolv.conf "$CH/etc/resolv.conf"
    dentro() { chroot "$CH" /usr/bin/env -i HOME=/root PATH=/usr/bin LANG=C.UTF-8 "$@"; }

    # o sandbox do pacman (landlock, usuário de download) não anda num chroot
    grep -q '^DisableSandbox' "$CH/etc/pacman.conf" || sed -i 's/^\[options\]/[options]\nDisableSandbox/' "$CH/etc/pacman.conf"
    sed -i 's/^DownloadUser/#DownloadUser/' "$CH/etc/pacman.conf"
    sed -i 's/^#\?ParallelDownloads.*/ParallelDownloads = 8/' "$CH/etc/pacman.conf"
    dentro pacman-key --init
    dentro pacman-key --populate archlinuxarm
    # o kernel e o firmware da imagem não servem pra nada aqui
    dentro pacman -Rdd --noconfirm linux-aarch64 linux-firmware 2>/dev/null || true
    dentro pacman -Syu --noconfirm --needed base-devel git sudo
    echo 'alarm ALL=(ALL) NOPASSWD: ALL' >"$CH/etc/sudoers.d/lumna-ci"

    # o código e o cache do alarm (cargo, pnpm, target), dele
    rm -rf "$CH/home/alarm/LUMNA_OS" "$CH/home/alarm/NewHarnessV2"
    mkdir -p "$CH/home/alarm/LUMNA_OS" "$CH/home/alarm/cache"
    tar -C "$ROOT" --exclude=./.git --exclude=./target --exclude=./out --exclude=./.cache-ci \
        --exclude=./shell/node_modules -cf - . | tar -C "$CH/home/alarm/LUMNA_OS" -xf -
    [ -z "${EMMA_SRC:-}" ] || { mkdir -p "$CH/home/alarm/NewHarnessV2"; tar -C "$EMMA_SRC" --exclude=./.git -cf - . | tar -C "$CH/home/alarm/NewHarnessV2" -xf -; }
    sobe /home/alarm/cache "$CACHE/alarm"
    mkdir -p "$CH/home/alarm/out"
    dentro chown -R alarm:alarm /home/alarm

    # makepkg como o alarm: com -s ele instala as dependências pelo sudo
    pacote() { # pacote NOME VERSÃO [ARGS DO MAKEPKG]
        nome=$1 versao=$2; shift 2
        echo "==> $nome $versao"
        sed -i "s/^pkgver=.*/pkgver=$versao/; s/^pkgrel=.*/pkgrel=1/" "$CH/home/alarm/LUMNA_OS/packages/$nome/PKGBUILD"
        dentro su alarm -s /bin/sh -c "cd ~/LUMNA_OS/packages/$nome && \
            CARGO_HOME=~/cache/cargo CARGO_TARGET_DIR=~/cache/target npm_config_store_dir=~/cache/pnpm \
            LUMNA_CI=1 EMMA_SRC=~/NewHarnessV2 PKGDEST=~/out PKGEXT=.pkg.tar.zst \
            makepkg -f --noconfirm --nocheck $*"
    }
    for p in $FAZ; do
        case $p in
            lumna-shell) pacote lumna-shell "$V_SHELL" -s ;;
            # depende do lumna-shell, que não está nos repositórios do Arch: só as de compilar
            lumna-mobile) dentro pacman -S --noconfirm --needed git && pacote lumna-mobile "$V_MOBILE" -d ;;
            lumna-emma) dentro pacman -S --noconfirm --needed python python-pip curl && pacote lumna-emma "$V_EMMA" -d ;;
        esac
    done
    cp "$CH"/home/alarm/out/*.pkg.tar.zst "$OUT/"
fi

# ---- o repositório ----
echo "==> repositório [lumna-celular]"
ls -la "$OUT"
if [ -n "$FAZ" ]; then
    mkdir -p "$CH/tmp/repo" && rm -f "$CH"/tmp/repo/*
    cp "$OUT"/*.pkg.tar.zst "$CH/tmp/repo/"
    dentro sh -c 'repo-add -q /tmp/repo/lumna-celular.db.tar.gz /tmp/repo/*.pkg.tar.zst'
    cp "$CH"/tmp/repo/lumna-celular.db.tar.gz "$CH"/tmp/repo/lumna-celular.files.tar.gz "$OUT/"
else
    # nada novo: o mesmo banco de antes
    curl -fsSL -o "$OUT/lumna-celular.db.tar.gz" "$REPO_URL/lumna-celular.db.tar.gz"
    curl -fsSL -o "$OUT/lumna-celular.files.tar.gz" "$REPO_URL/lumna-celular.files.tar.gz"
fi
# a release não guarda symlink: o banco vai também com o nome que o pacman procura
cp "$OUT/lumna-celular.db.tar.gz" "$OUT/lumna-celular.db"
cp "$OUT/lumna-celular.files.tar.gz" "$OUT/lumna-celular.files"
chown -R "${SUDO_UID:-0}:${SUDO_GID:-0}" "$OUT" "$CACHE" 2>/dev/null || true
echo "ok: $OUT"
