#!/bin/sh
# Pra testar no celular o que ainda não foi pro GitHub. (O normal agora é pela Loja: a cada push no
# lumna-mobile o GitHub compila os pacotes e o celular mostra em Loja → Atualizações.)
# Atualiza o celular inteiro com um comando só, rodado NO PC, na raiz do LUMNA_OS:
#   sh mobile/scripts/atualizar.sh              # celular na USB (alarm@172.16.42.1)
#   sh mobile/scripts/atualizar.sh alarm@IP     # outro endereço
#   sh mobile/scripts/atualizar.sh --ver        # só mostra o que mudou, sem fazer nada
#   sh mobile/scripts/atualizar.sh --tudo       # refaz tudo, mesmo o que não mudou
#
# O que ele faz, sozinho:
#  1. baixa as novidades (git pull) do LUMNA_OS e do NewHarnessV2 aqui no PC;
#  2. compara com o que está instalado no celular (anotado em ~/.config/lumna/implantado lá);
#  3. só o que mudou: compila o shell no PC (build-pc.sh), manda o servidor da Emma, as
#     configurações da sessão, e roda no celular o emma.sh e o lumna-mobile.sh;
#  4. reinicia o que precisa (a sessão, o serviço da Emma) e avisa se o celular inteiro precisa
#     reiniciar, perguntando antes.
# O celular não precisa de git nem de login no GitHub: tudo vai do PC pelo ssh.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PHONE=alarm@172.16.42.1
VER=
TUDO=
for a in "$@"; do
    case $a in
        --ver) VER=1 ;;
        --tudo) TUDO=1 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) PHONE=$a ;;
    esac
done

# uma conexão ssh só, reaproveitada: a senha do ssh (se não tiver chave) é pedida uma vez
mkdir -p "$HOME/.ssh"
CM="-o ControlMaster=auto -o ControlPath=$HOME/.ssh/lumna-%r@%h:%p -o ControlPersist=600"
s() { ssh $CM "$@"; }

passo() { printf '\n\033[1;35m==> %s\033[0m\n' "$*"; }
ok() { printf '\033[32m✓ %s\033[0m\n' "$*"; }
aviso() { printf '\033[33m! %s\033[0m\n' "$*"; }
die() { printf '\033[31merro: %s\033[0m\n' "$*" >&2; exit 1; }

# o NewHarnessV2 (o servidor da Emma) do lado do LUMNA_OS, ou em ~/NewHarnessV2
EMMA=${LUMNA_EMMA_SRC:-}
for d in "$ROOT/../NewHarnessV2" "$ROOT/../newharnessv2" "$HOME/NewHarnessV2"; do
    [ -n "$EMMA" ] || { [ -f "$d/backend/server.py" ] && EMMA=$(cd "$d" && pwd); } || true
done

# ---- 1. novidades ----
passo "baixando as novidades"
puxa() {
    if [ -n "$(git -C "$1" status --porcelain --untracked-files=no)" ]; then
        aviso "$2: tem mudanças locais aqui, não baixei nada (vai assim mesmo)"
    elif git -C "$1" pull -q --ff-only; then
        ok "$2: $(git -C "$1" log -1 --format='%h %s' | cut -c1-70)"
    else
        aviso "$2: não consegui baixar (sem internet ou sem login); sigo com o que tem aqui"
    fi
}
SELF="$ROOT/mobile/scripts/atualizar.sh"
ANTES=$(md5sum "$SELF" | cut -d' ' -f1)
puxa "$ROOT" "LUMNA_OS ($(git -C "$ROOT" branch --show-current))"
# o próprio atualizar.sh veio novo no pull: recomeça com ele (a versão que já estava rodando não
# sabe o que a nova sabe). Uma vez só.
if [ -z "${LUMNA_ATUALIZAR_DE_NOVO:-}" ] && [ "$(md5sum "$SELF" | cut -d' ' -f1)" != "$ANTES" ]; then
    ok "o atualizar.sh mudou: recomeçando com a versão nova"
    LUMNA_ATUALIZAR_DE_NOVO=1 exec sh "$SELF" "$@"
fi
if [ -n "$EMMA" ]; then
    puxa "$EMMA" "NewHarnessV2"
else
    aviso "não achei o NewHarnessV2 aqui no PC: a Emma fica como está."
    aviso "pra incluir: git clone https://github.com/dekilled/NewHarnessV2 \"$ROOT/../NewHarnessV2\""
fi

# ---- 2. o que mudou ----
# impressão digital de uma parte: o que está no git + o que foi mexido e ainda não foi pro git
digital() {
    repo=$1; shift
    { for p in "$@"; do git -C "$repo" rev-parse "HEAD:$p" 2>/dev/null || true; done
      git -C "$repo" diff HEAD -- "$@"
      git -C "$repo" ls-files --others --exclude-standard -- "$@"; } | sha1sum | cut -c1-12
}
NOVO_SHELL=$(digital "$ROOT" shell crates Cargo.toml Cargo.lock packages/lumna-shell)
NOVO_SESSAO=$(digital "$ROOT" mobile/session packages/lumna-mobile session/lumna-pkg session/os.lumna.pkg.policy)
# o que só vale depois de reiniciar o celular (boot, desligar, modem)
NOVO_BOOT=$(digital "$ROOT" mobile/session/boot-ok.sh mobile/session/lumna-boot-ok.service mobile/session/50-lumna-stop.conf mobile/session/rmtfs)
NOVO_EMMA=-
[ -z "$EMMA" ] || NOVO_EMMA=$(digital "$EMMA" backend)-$(digital "$ROOT" packages/lumna-emma)

passo "conferindo o celular ($PHONE)"
s -o ConnectTimeout=6 "$PHONE" true \
    || die "o celular não respondeu em $PHONE. Ele está ligado e no cabo USB? (ou passe o endereço: sh $0 alarm@IP)"
ANTES=$(s "$PHONE" 'cat ~/.config/lumna/implantado 2>/dev/null' || true)
antes() { printf '%s\n' "$ANTES" | sed -n "s/^$1=//p"; }
mudou() { [ -n "$TUDO" ] || [ "$(antes "$1")" != "$2" ]; }

FAZ_SHELL=; FAZ_SESSAO=; FAZ_EMMA=; FAZ_BOOT=
mudou shell "$NOVO_SHELL" && FAZ_SHELL=1
mudou sessao "$NOVO_SESSAO" && FAZ_SESSAO=1
mudou boot "$NOVO_BOOT" && FAZ_BOOT=1
[ "$NOVO_EMMA" = - ] || { mudou emma "$NOVO_EMMA" && FAZ_EMMA=1; }
# primeira vez (nada anotado no celular): o reboot não é preciso, o boot já está lá
[ -n "$(antes boot)" ] || [ -n "$TUDO" ] || FAZ_BOOT=

echo "  interface (lumna-shell): $([ -n "$FAZ_SHELL" ] && echo 'MUDOU → compilar e instalar' || echo 'igual')"
echo "  sessão do celular:       $([ -n "$FAZ_SESSAO" ] && echo 'MUDOU → reaplicar as configurações' || echo 'igual')"
echo "  servidor da Emma:        $([ "$NOVO_EMMA" = - ] && echo 'fica como está' || { [ -n "$FAZ_EMMA" ] && echo 'MUDOU → reinstalar' || echo 'igual'; })"
[ -z "$FAZ_BOOT" ] || echo "  boot/desligar:           MUDOU → o celular precisa reiniciar no fim"

if [ -z "$FAZ_SHELL$FAZ_SESSAO$FAZ_EMMA" ]; then
    ok "o celular já está com tudo atualizado"
    exit 0
fi
[ -z "$VER" ] || exit 0

# ---- 3. compilar no PC ----
PKG=
if [ -n "$FAZ_SHELL" ]; then
    passo "compilando a interface no PC (pode levar uns minutos)"
    sh "$ROOT/mobile/scripts/build-pc.sh"
    PKG=$(ls -t "$ROOT"/out/lumna-shell-*-aarch64.pkg.tar* | head -1)
    ok "pacote: $(basename "$PKG")"
fi

# ---- 4. mandar pro celular ----
passo "mandando pro celular"
DEST='.cache/lumna-implantar'
s "$PHONE" "rm -rf ~/$DEST && mkdir -p ~/$DEST"
# só o que os scripts do celular usam (a sessão, a Loja e as receitas dos pacotes)
tar -C "$ROOT" -czf - mobile/session packages session/lumna-pkg session/os.lumna.pkg.policy | s "$PHONE" "tar -C ~/$DEST -xzf -"
# o código do uMTP-Responder (arquivos pelo cabo) vai daqui: o celular nem sempre alcança o GitHub
UMTP_REF=$(sed -n 's/^_umtp=//p' "$ROOT/packages/lumna-mobile/PKGBUILD")
UMTP_SRC="$ROOT/mobile/cache/umtp-$UMTP_REF.tar.gz"
if [ -n "$UMTP_REF" ] && [ ! -s "$UMTP_SRC" ]; then
    mkdir -p "$ROOT/mobile/cache"
    umtp_tmp=$(mktemp -d)
    timeout 180 git clone -q https://github.com/viveris/uMTP-Responder "$umtp_tmp" &&
        git -C "$umtp_tmp" archive --format=tar.gz --prefix=umtp/ "$UMTP_REF" >"$UMTP_SRC.part" &&
        mv "$UMTP_SRC.part" "$UMTP_SRC" || { rm -f "$UMTP_SRC.part"; echo "aviso: não baixei o uMTP-Responder (o celular tenta sozinho)"; }
    rm -rf "$umtp_tmp"
fi
[ ! -s "$UMTP_SRC" ] || s "$PHONE" "mkdir -p ~/$DEST/mobile/cache && cat > ~/$DEST/mobile/cache/$(basename "$UMTP_SRC")" < "$UMTP_SRC"
[ -z "$PKG" ] || s "$PHONE" "cat > ~/$DEST/$(basename "$PKG")" < "$PKG"
if [ -n "$FAZ_EMMA" ]; then
    s "$PHONE" 'rm -rf ~/NewHarnessV2/backend && mkdir -p ~/NewHarnessV2'
    tar -C "$EMMA" -czf - --exclude='__pycache__' --exclude='backend/data' --exclude='backend/.venv' --exclude='backend/.env' backend \
        | s "$PHONE" 'tar -C ~/NewHarnessV2 -xzf -'
fi
ok "arquivos no celular"

# ---- 5. instalar lá (uma sessão ssh só: a senha do sudo é pedida uma vez) ----
passo "instalando no celular (a senha pedida é a do celular)"
REMOTO="set -e; cd ~/$DEST"
[ -z "$FAZ_EMMA" ] || REMOTO="$REMOTO; echo '--- Emma'; EMMA_SRC=\$HOME/NewHarnessV2 sh mobile/session/emma.sh"
if [ -n "$FAZ_SHELL$FAZ_SESSAO" ]; then
    PACOTE=manter
    [ -z "$PKG" ] || PACOTE="\$HOME/$DEST/$(basename "$PKG")"
    # no fim ele reinicia a sessão (a tela pisca e o LUMNA volta)
    REMOTO="$REMOTO; echo '--- interface e sessão'; LUMNA_PACOTE=$PACOTE LUMNA_VERSAO_MOBILE=$(sh "$ROOT/mobile/scripts/versao.sh" lumna-mobile) sh mobile/session/lumna-mobile.sh"
fi
REMOTO="$REMOTO; mkdir -p ~/.config/lumna; printf 'shell=%s\nsessao=%s\nboot=%s\nemma=%s\n' '$NOVO_SHELL' '$NOVO_SESSAO' '$NOVO_BOOT' '$(
    [ "$NOVO_EMMA" = - ] && antes emma || echo "$NOVO_EMMA")' > ~/.config/lumna/implantado"
s -t "$PHONE" "$REMOTO" || die "a instalação no celular parou (veja a mensagem acima). Pode rodar de novo: ele continua de onde precisa."
ok "celular atualizado"

# ---- 6. reiniciar o celular, se precisa ----
if [ -n "$FAZ_BOOT" ]; then
    printf '\nO que cuida do boot/desligar mudou: só vale depois de reiniciar. Reiniciar o celular agora? [s/N] '
    read -r r
    case $r in
        s|S|sim) s -t "$PHONE" 'sudo systemctl reboot' || true; ok "reiniciando" ;;
        *) aviso "lembre de reiniciar o celular depois" ;;
    esac
fi
