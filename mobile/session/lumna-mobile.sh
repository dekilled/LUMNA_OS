#!/bin/sh
# Liga o LUMNA na tela do celular, pelos pacotes. Rode NO CELULAR, como alarm (sem sudo), dentro do
# clone do LUMNA_OS, com internet:
#   sh mobile/session/lumna-mobile.sh
# Ele liga o repositório [lumna-celular] no pacman (os pacotes que o GitHub compila a cada push,
# workflow "Celular") e instala o lumna-shell e o lumna-mobile de lá. Depois disso as novidades
# chegam pela Loja → Atualizações.
#
# Pra testar mudanças antes de irem pro GitHub (o mobile/scripts/atualizar.sh, no PC, faz isso):
#   LUMNA_PACOTE=lumna-shell-...-aarch64.pkg.tar.zst sh mobile/session/lumna-mobile.sh
#     instala esse lumna-shell (compilado no PC) e compila aqui o lumna-mobile (1 min)
#   LUMNA_PACOTE=manter     mantém o lumna-shell instalado e só refaz o lumna-mobile
#   LUMNA_COMPILAR=1        compila o lumna-shell aqui mesmo (30-60 min no Snapdragon 845)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ "$(id -u)" != 0 ] || { echo "rode como alarm, sem sudo (o makepkg não roda como root)"; exit 1; }

# ---- o repositório do LUMNA no pacman ----
# Só entra quando o GitHub já publicou (sem o banco, o pacman recusa qualquer instalação). O banco
# vem direto, sem o -Sy: sincronizar só um lado do sistema quebraria as atualizações do Arch.
REPO=https://github.com/dekilled/LUMNA_OS/releases/download/celular
if curl -fsSL --max-time 60 -o /tmp/lumna-celular.db "$REPO/lumna-celular.db"; then
    if ! grep -q '^\[lumna-celular\]' /etc/pacman.conf; then
        echo "--- ligando o repositório [lumna-celular] no pacman"
        printf '\n# Os pacotes do LUMNA para o celular (workflow "Celular" do LUMNA_OS)\n[lumna-celular]\nSigLevel = Optional TrustAll\nServer = %s\n' "$REPO" \
            | sudo tee -a /etc/pacman.conf >/dev/null
    fi
    sudo install -Dm644 /tmp/lumna-celular.db /var/lib/pacman/sync/lumna-celular.db
    rm -f /tmp/lumna-celular.db
    REPO_OK=1
else
    REPO_OK=
    echo "aviso: o repositório [lumna-celular] ainda não está no ar (o GitHub ainda não publicou ou sem internet): fica pra próxima"
    # se já estava no pacman.conf sem o banco, sai de lá (volta na próxima vez que estiver no ar)
    [ -f /var/lib/pacman/sync/lumna-celular.db ] || sudo sed -i '/^# Os pacotes do LUMNA para o celular/,/^Server = .*releases\/download\/celular$/d' /etc/pacman.conf
fi

# Antes dos pacotes, este script copiava os ícones dos apps à mão: saem do caminho do pacman
# (só os que nenhum pacote tem). E as cópias da pessoa (config, atalhos) dão lugar às do pacote.
for f in phone contacts remote emma gallery music; do
    f=/usr/share/lumna/icons/$f.svg
    [ ! -e "$f" ] || pacman -Qoq "$f" >/dev/null 2>&1 || sudo rm -f "$f"
done
for f in "$ROOT"/mobile/session/apps/*.desktop; do rm -f "$HOME/.local/share/applications/$(basename "$f")"; done
rm -f "$HOME/.config/lumna/hyprland-mobile.lua"

# O rootfs nasce com 6 GB: o ext4 cresce ligado, até ocupar a userdata inteira.
livre=$(df -Pk / | awk 'NR == 2 { print int($4 / 1048576) }')
if [ "$livre" -lt 15 ]; then
    echo "só ${livre} GB livres: aumentando o rootfs para a partição inteira"
    sudo resize2fs "$(findmnt -no SOURCE /)"
fi

# lumna-mobile compilado aqui (o código de mobile/session deste clone). LUMNA_UMTP_SRC: o código
# do uMTP-Responder que o atualizar.sh trouxe do PC (o celular nem sempre alcança o GitHub).
mobile_local() {
    sudo pacman -S --needed --noconfirm base-devel git
    cd "$ROOT/packages/lumna-mobile"
    [ -z "${LUMNA_VERSAO_MOBILE:-}" ] || sed -i "s/^pkgver=.*/pkgver=$LUMNA_VERSAO_MOBILE/" PKGBUILD
    umtp=$(ls "$ROOT"/mobile/cache/umtp-*.tar.gz 2>/dev/null | head -1)
    LUMNA_UMTP_SRC=$umtp makepkg -sfi --noconfirm
    cd "$ROOT"
}

if [ "${LUMNA_PACOTE:-}" = manter ]; then
    pacman -Q lumna-shell >/dev/null || { echo "o lumna-shell não está instalado: rode sem LUMNA_PACOTE=manter"; exit 1; }
    mobile_local
elif [ -n "${LUMNA_PACOTE:-}" ]; then
    # compilado no PC: só instala
    sudo pacman -U --noconfirm "$(realpath "$LUMNA_PACOTE")"
    mobile_local
elif [ -n "${LUMNA_COMPILAR:-}" ]; then
    sudo pacman -S --needed --noconfirm base-devel rust nodejs pnpm
    cd "$ROOT/packages/lumna-shell"
    makepkg -sfi --nocheck --noconfirm
    mobile_local
else
    [ -n "$REPO_OK" ] || { echo "sem o repositório não dá pra instalar: rode de novo depois que o workflow Celular terminar"; exit 1; }
    sudo pacman -S --noconfirm --needed lumna-shell lumna-mobile
fi

# o bootloader A/B precisa ouvir "liguei direitinho" a cada boot (senão desiste do slot)
[ -x /usr/local/bin/qbootctl ] || sh "$ROOT/mobile/session/boot-ok.sh"

# a sessão sobe com a config do Hyprland do pacote
sed -i 's|hyprland-teste.lua|hyprland-mobile.lua|; s|~/.config/lumna/hyprland-mobile.lua|/usr/share/lumna/hyprland-mobile.lua|' "$HOME/.bash_profile"
grep -q /usr/share/lumna/hyprland-mobile.lua "$HOME/.bash_profile" || { echo "rode antes: sudo sh mobile/session/primeira-tela.sh"; exit 1; }
# a hora de agora, guardada (o pacote liga o serviço que guarda de 15 em 15 min)
sudo /usr/lib/lumna/relogio.sh guarda || true

sudo rm -f /tmp/lumna-hypr-tentou
sudo systemctl restart getty@tty1
echo "ok: olhe a tela. Logs: tail -f ~/hyprland.log e journalctl -f -t lumna-shell"
echo "daqui pra frente as atualizações chegam pela Loja → Atualizações"
