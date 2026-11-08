#!/usr/bin/env bash
# Confere os pacotes da sessão nova (lumna-orbit, lumna-gravity, lumna-session) numa pasta: os arquivos que a sessão chama
# existem, nenhum binário ficou com biblioteca faltando e as políticas do polkit são XML válido. Sai com erro se algo falha.
#   confere-pacotes.sh PASTA_COM_OS_PACOTES
set -euo pipefail
dir="${1:?uso: confere-pacotes.sh PASTA}"
check="$(mktemp -d)"
trap 'rm -rf "$check"' EXIT
for pkg in lumna-orbit lumna-gravity lumna-session; do
  arquivo="$(ls -1 "$dir"/"$pkg"-[0-9]*.pkg.tar.zst 2>/dev/null | sort | tail -1 || true)"
  [ -n "$arquivo" ] || { echo "FALTA o pacote $pkg em $dir"; exit 1; }
  bsdtar -xf "$arquivo" -C "$check"
done
faltou=0
for f in usr/bin/orbit usr/bin/orbit-msg usr/bin/lumna-painel usr/bin/lumna-barra usr/bin/lumna-dock usr/bin/lumna-lancador usr/bin/lumna-avisos \
         usr/bin/lumna-bloqueio usr/bin/lumna-entrada usr/bin/lumna-emma usr/bin/lumna-emma-app usr/bin/lumna-captura \
         usr/bin/lumna-polkit usr/bin/lumna-polkit-janela usr/bin/lumna-instalador usr/bin/lumna-console etc/skel/.config/lumna/dock \
         usr/bin/lumna-orbit-session usr/bin/lumna-entrada-greeter usr/share/wayland-sessions/lumna-orbit.desktop \
         usr/share/polkit-1/actions/os.lumna.entrada.policy usr/lib/lumna/lumna-entrada-ajustar usr/lib/lumna/lumna-pkg; do
  [ -e "$check/$f" ] || { echo "FALTA no pacote: $f"; faltou=1; }
done
for bin in "$check"/usr/bin/orbit "$check"/usr/bin/lumna-*; do
  head -c4 "$bin" | grep -q ELF || continue
  if ldd "$bin" 2>&1 | grep -q "not found"; then echo "biblioteca faltando em ${bin##*/}:"; ldd "$bin" | grep "not found"; faltou=1; fi
done
for policy in "$check"/usr/share/polkit-1/actions/*.policy; do xmllint --noout "$policy" || faltou=1; done
[ "$faltou" = 0 ] || { echo "a conferência dos pacotes falhou"; exit 1; }
echo "   pacotes conferidos"
