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
         usr/bin/lumna-polkit usr/bin/lumna-polkit-janela usr/bin/lumna-instalador usr/bin/lumna-pulse usr/bin/lumna-teclado usr/bin/lumna-atlas etc/skel/.config/lumna/dock etc/xdg/fastfetch/config.jsonc usr/share/lumna/fastfetch/logo.txt usr/share/lumna/pulse/integracao.bash \
         usr/bin/lumna-orbit-session usr/bin/lumna-entrada-greeter usr/share/wayland-sessions/lumna-orbit.desktop \
         usr/share/polkit-1/actions/os.lumna.entrada.policy usr/lib/lumna/lumna-entrada-ajustar usr/lib/lumna/lumna-pkg; do
  [ -e "$check/$f" ] || { echo "FALTA no pacote: $f"; faltou=1; }
done
for bin in "$check"/usr/bin/orbit "$check"/usr/bin/lumna-*; do
  head -c4 "$bin" | grep -q ELF || continue
  if ldd "$bin" 2>&1 | grep -q "not found"; then echo "biblioteca faltando em ${bin##*/}:"; ldd "$bin" | grep "not found"; faltou=1; fi
done
for policy in "$check"/usr/share/polkit-1/actions/*.policy; do xmllint --noout "$policy" || faltou=1; done

# Conflito de arquivos: um arquivo que dois pacotes nossos levam, ou que já é de um pacote do Arch (o pacman recusa a atualização inteira: "exists in filesystem").
# Foi assim que um /etc/skel/.bashrc, que é do pacote bash, travou a atualização: a conferência de cima só vê o que falta, não o que sobra.
lista="$(mktemp)"; trap 'rm -rf "$check" "$lista"' EXIT
for arquivo in $(for p in "$dir"/*.pkg.tar.zst; do n="$(bsdtar -xOf "$p" .PKGINFO | sed -n 's/^pkgname = //p' | head -1)"; echo "$n $p"; done | sort -k1,1 -k2,2V | awk '{ultimo[$1]=$2} END{for (n in ultimo) print ultimo[n]}'); do
  nome="$(bsdtar -xOf "$arquivo" .PKGINFO | sed -n 's/^pkgname = //p' | head -1)"
  bsdtar -tf "$arquivo" | grep -v '/$' | grep -v '^\.' | sed "s|^|$nome |" >> "$lista"
done
repetidos="$(awk '{arq=$2; dono[arq]=dono[arq] " " $1; n[arq]++} END{for (a in n) if (n[a] > 1) print a ":" dono[a]}' "$lista" | sort)"
if [ -n "$repetidos" ]; then echo "arquivos que mais de um pacote nosso leva:"; echo "$repetidos" | sed 's/^/   /'; faltou=1; fi
if command -v pacman >/dev/null && { [ "$(id -u)" = 0 ] && pacman -Fy >/dev/null 2>&1 || sudo -n pacman -Fy >/dev/null 2>&1; }; then
  # um arquivo nosso que já é de um pacote do Arch (pacman -F ... --machinereadable: repo, pacote, versão e o caminho, separados por NUL)
  donos="$(awk '{print $2}' "$lista" | xargs -n 200 pacman -F --machinereadable -- 2>/dev/null | tr '\0' '\t' || true)"
  conflitos="$(awk -F'\t' 'NR==FNR {split($0, a, " "); meus[a[2]]=1; next} ($4 in meus) {print $4 "  (" $2 ")"}' "$lista" - <<<"$donos" | sort -u || true)"
  if [ -n "$conflitos" ]; then echo "arquivos nossos que já são de um pacote do Arch (o pacman recusaria a atualização):"; echo "$conflitos" | sed 's/^/   /'; faltou=1; fi
else
  echo "   (sem o banco de arquivos do pacman: o conflito com pacotes do Arch ficou sem conferir)"
fi
[ "$faltou" = 0 ] || { echo "a conferência dos pacotes falhou"; exit 1; }
echo "   pacotes conferidos"
