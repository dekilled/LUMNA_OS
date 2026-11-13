#!/usr/bin/env bash
# Atualizações do LUMNA pelo GitHub: compila os pacotes da sessão nova (Orbit, Gravity, sessão, clássico e Emma), confere,
# junta com os que já estão publicados e publica o repositório [lumna] do canal (a release "repo-orbit"). O sistema instalado
# tem esse repositório no pacman.conf: um "Atualizar tudo" no Painel (ou pacman -Syu) baixa o que saiu.
#
# Roda como root dentro de um Arch (o container do CI), com o gh (github-cli) e o GH_TOKEN. Variáveis:
#   LUMNA_CANAL  "-orbit" (padrão) ou "" (o canal antigo: não usado por aqui)
#   PUBLICAR     1 (padrão) publica; 0 só compila e confere (para testar)
#   OUT          onde ficam os pacotes (padrão /tmp/lumna-repo)
set -euo pipefail

SRC="$(realpath "$(dirname "$0")/..")"
CANAL="${LUMNA_CANAL--orbit}"
RELEASE="repo$CANAL"
PUBLICAR="${PUBLICAR:-1}"
OUT="${OUT:-/tmp/lumna-repo}"
BUILDER="${BUILDER:-builder}"
WORK="${WORK:-/tmp/lumna-pub}"

# o nome do pacote (pkgname) de um arquivo .pkg.tar.zst
nome_do_pacote() { bsdtar -xOf "$1" .PKGINFO | sed -n 's/^pkgname = //p' | head -1; }

# deixa só uma versão de cada pacote: a dos arquivos NOVOS (os do segundo argumento em diante) vence os antigos de mesmo nome
limpar_antigos() {
  local dir="$1"; shift
  local novo velho n
  for novo in "$@"; do
    n="$(nome_do_pacote "$dir/$novo")"
    for velho in "$dir"/*.pkg.tar.zst; do
      if [ "$(basename "$velho")" != "$novo" ] && [ "$(nome_do_pacote "$velho")" = "$n" ]; then
        rm -f "$velho" "$velho.sig"
      fi
    done
  done
  return 0
}

principal() {
  mkdir -p "$OUT" "$WORK"
  if [ "$PUBLICAR" = 1 ]; then
    # o que já está publicado (lumna-shell, hyprbars...) fica no repositório mesmo sem ser recompilado aqui
    gh release download "$RELEASE" --pattern '*.pkg.tar.zst' --dir "$OUT" --clobber 2>/dev/null || echo "   (a release $RELEASE ainda não tem pacotes)"
  fi
  local antes novos tree="$WORK/src"
  antes="$(cd "$OUT" && ls -1 *.pkg.tar.zst 2>/dev/null | sort || true)"
  rm -rf "$tree"; mkdir -p "$tree"
  tar -C "$SRC" --exclude=./.git --exclude=./target --exclude=./out --exclude=node_modules --exclude=./shell/dist -cf - . | tar -C "$tree" -xf -
  chown -R "$BUILDER:$BUILDER" "$WORK" "$OUT"
  # cada publicação tem versão maior que a anterior (a data do build): o pacman sabe que é atualização
  local versao="${LUMNA_VERSION:-0.1.0.$(date -u +%Y%m%d%H%M)}"
  sed -i "s/^pkgver=.*/pkgver=$versao/" "$tree"/packages/*/PKGBUILD
  echo ">> versão $versao, canal ${CANAL:-estável}"
  build() {
    local nome="$1"; shift
    (cd "$tree/packages/$nome" && sudo -u "$BUILDER" --preserve-env=EMMA_SRC,PKGDEST env PKGDEST="$OUT" makepkg --noconfirm --cleanbuild "$@")
  }
  build lumna-orbit --syncdeps
  build lumna-gravity --syncdeps
  build lumna-session --nodeps
  build lumna-boot --nodeps
  build lumna-classico --nodeps
  pacman -S --needed --noconfirm python python-pip >/dev/null
  EMMA_SRC="$tree/emma-servidor" build lumna-emma --nodeps
  novos="$(cd "$OUT" && ls -1 *.pkg.tar.zst | sort | comm -13 <(echo "$antes") - )"
  echo ">> pacotes novos:"; echo "$novos" | sed 's/^/   /'
  # shellcheck disable=SC2086
  limpar_antigos "$OUT" $novos
  "$SRC/scripts/confere-pacotes.sh" "$OUT"
  # o banco do repositório, refeito com tudo
  (cd "$OUT" && rm -f lumna.db lumna.db.tar.gz lumna.files lumna.files.tar.gz && repo-add lumna.db.tar.gz ./*.pkg.tar.zst >/dev/null)
  for db in lumna.db lumna.files; do rm -f "$OUT/$db"; cp "$OUT/$db.tar.gz" "$OUT/$db"; done  # a release não guarda symlink: o banco vai com o nome que o pacman procura
  if [ "$PUBLICAR" != 1 ]; then
    echo ">> PUBLICAR=0: nada foi publicado"
    return 0
  fi
  gh release view "$RELEASE" >/dev/null 2>&1 || gh release create "$RELEASE" --prerelease \
    --title "Repositório [lumna]${CANAL:+ ($CANAL)}" \
    --notes "Pacotes do LUMNA para o pacman. Não baixe à mão: o sistema usa isto sozinho."
  (cd "$OUT" && gh release upload "$RELEASE" --clobber lumna.db lumna.db.tar.gz lumna.files lumna.files.tar.gz ./*.pkg.tar.zst)
  # tira da release o que não está mais no repositório (versões velhas)
  gh release view "$RELEASE" --json assets -q '.assets[].name' | while read -r asset; do
    [ -e "$OUT/$asset" ] || gh release delete-asset "$RELEASE" "$asset" -y
  done
  echo ">> publicado em $RELEASE"
}

# "source" só carrega as funções (para o teste); executado, publica
if [ "${BASH_SOURCE[0]}" = "$0" ]; then principal "$@"; fi
