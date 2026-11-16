#!/bin/sh
# lumna-pkg-analisar (a lista "Escolher pacotes", sem root) e lumna-pkg upgrade-pacotes (a instalação só dos escolhidos), com checkupdates e pacman de mentira.
# sh pacotes.sh   (a partir da pasta gravity)
AQUI=$(cd "$(dirname "$0")" && pwd)
PKG=$AQUI/../../../session/lumna-pkg
ANA=$AQUI/../../../session/lumna-pkg-analisar
t=$(mktemp -d)
export PATH=$AQUI/bin-lumna:$AQUI/bin:$PATH FALSO_LOG=$t/log FAKE=$t/fake LUMNA_SNAPDIR=$t/snaps LUMNA_ETC=$t/etc LUMNA_VARLIB=$t/varlib LUMNA_PRIVDB=$t/privdb
export LUMNA_BASE_LISTA=$AQUI/../../../session/pacotes-base.txt CHECKUPDATES_DB=$t/checkdb
chmod +x "$AQUI"/bin/* "$AQUI"/bin-lumna/*
mkdir -p "$FAKE/db/sync" "$t/snaps"
printf '#!/bin/sh\ncat "%s/cu"\nexit "$(cat %s/cu-rc 2>/dev/null || echo 0)"\n' "$FAKE" "$FAKE" > "$AQUI/bin-lumna/checkupdates"; chmod +x "$AQUI/bin-lumna/checkupdates"
trap 'rm -f "$AQUI/bin-lumna/checkupdates"' EXIT
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
zera() { : > "$t/log"; rm -f "$FAKE"/sim-* "$FAKE/sync-falha"; rm -rf "$t/snaps"; mkdir -p "$t/snaps"; }
printf 'vlc\nglibc\nfirefox\nlibfoo\nmesa\nlumna-orbit\nbar\nlinux\n' > "$FAKE/instalados"

# ---------- o analisador ----------
cat > "$FAKE/cu" <<'CU'
vlc 3.0.20-1 -> 3.0.21-1
glibc 2.40-1 -> 2.41-1
firefox 130.0-1 -> 131.0-1
libfoo 1.0-1 -> 2.0-1
linux 6.10.1-1 -> 6.10.2-1
mesa 24.1-1 -> 24.2-1
nvidia-utils 550-1 -> 555-1
htop 3.3-1 -> 3.4-1
CU
zera
# vlc: sozinho, com uma dependência NOVA (não instalada)
printf 'LUMNA-SIM extra vlc 3.0.21-1\nLUMNA-SIM extra libnovo 1.0-1\n' > "$FAKE/sim-vlc"
# firefox: puxaria a atualização do glibc, que já está instalado
printf 'LUMNA-SIM extra firefox 131.0-1\nLUMNA-SIM core glibc 2.41-1\n' > "$FAKE/sim-firefox"
# libfoo: o pacman recusa (quebraria a dependência do bar)
: > "$FAKE/sim-falha-libfoo"
# htop: sozinho, sem nada a mais
printf 'LUMNA-SIM extra htop 3.4-1\n' > "$FAKE/sim-htop"
bash "$ANA" > "$t/ana" 2> "$t/ana.erro" || fail "o analisador devia terminar bem: $(cat "$t/ana.erro")"
col() { awk -F'\t' -v n="$1" '$1 == n {print $5}' "$t/ana"; }
[ "$(col vlc)" = ok ] || fail "vlc (sozinho, com dependência nova) devia ser ok: $(grep '^vlc' "$t/ana")"
grep -P '^vlc\t' "$t/ana" | grep -q "libnovo" || fail "vlc devia dizer que instala libnovo (nova)"
[ "$(col htop)" = ok ] || fail "htop devia ser ok: $(grep '^htop' "$t/ana")"
[ "$(col firefox)" = arrasta ] || fail "firefox (puxaria o glibc) devia ser 'arrasta': $(grep '^firefox' "$t/ana")"
grep -P '^firefox\t' "$t/ana" | grep -q "glibc" || fail "firefox devia dizer QUE pacote puxaria"
[ "$(col libfoo)" = quebra ] || fail "libfoo (o pacman recusa) devia ser 'quebra': $(grep '^libfoo' "$t/ana")"
grep -P '^libfoo\t' "$t/ana" | grep -q "breaks dependency" || fail "libfoo devia trazer o motivo do pacman"
for b in glibc linux mesa nvidia-utils; do [ "$(col $b)" = base ] || fail "$b é do sistema base (bloqueado): $(grep "^$b" "$t/ana")"; done
grep -q "simulate\|--print.* -- glibc\|-- linux\|-- mesa" "$t/log" && fail "o base nem é simulado: $(grep -- '-- glibc\|-- linux' "$t/log")"
grep -q -- "--dbpath $t/checkdb -S --needed --print" "$t/log" || fail "a simulação usa o banco do checkupdates (sem root): $(head -3 "$t/log")"
[ "$(wc -l < "$t/ana")" = 8 ] || fail "uma linha por pacote (8): $(wc -l < "$t/ana")"
# nada a atualizar: lista vazia e código 0; checkupdates falhando: código 1
: > "$FAKE/cu"; echo 2 > "$FAKE/cu-rc"; bash "$ANA" > "$t/ana" 2>/dev/null; rc=$?
{ [ "$rc" = 0 ] && [ ! -s "$t/ana" ]; } || fail "sem atualização: vazio e 0 (rc=$rc)"
echo 1 > "$FAKE/cu-rc"; bash "$ANA" >/dev/null 2>&1; [ "$?" = 1 ] || fail "checkupdates falhando: código 1"
echo 0 > "$FAKE/cu-rc"

# ---------- a instalação só dos escolhidos ----------
printf 'vlc 3.0.20-1 -> 3.0.21-1\nhtop 3.3-1 -> 3.4-1\nfirefox 130.0-1 -> 131.0-1\nglibc 2.40-1 -> 2.41-1\n' > "$FAKE/qu"
# 1. dois pacotes que vão sozinhos: instala SÓ eles, com o banco privado, com snapshot, sem tocar no banco do sistema
zera
printf 'LUMNA-SIM extra vlc 3.0.21-1\nLUMNA-SIM extra htop 3.4-1\nLUMNA-SIM extra libnovo 1.0-1\n' > "$FAKE/sim"
bash "$PKG" upgrade-pacotes vlc htop > "$t/saida" 2>&1 || fail "upgrade-pacotes devia terminar bem: $(cat "$t/saida")"
grep -q "^pacman --dbpath $t/privdb -S --needed --noconfirm --noprogressbar -- vlc htop$" "$t/log" || fail "devia instalar SÓ vlc e htop, pelo banco privado: $(cat "$t/log")"
grep -q "^pacman -Sy\|^pacman -Syu\|^pacman -S " "$t/log" && fail "nada pode usar o banco do sistema para sincronizar/instalar: $(cat "$t/log")"
grep -q "^pacman --dbpath $t/privdb -Sy" "$t/log" || fail "sincroniza o banco PRIVADO"
[ -L "$t/privdb/local" ] || fail "o banco de instalados do banco privado é o do sistema (um link)"
grep -q "^btrfs subvolume snapshot -r / $t/snaps/antes-" "$t/log" || fail "devia tirar o snapshot"
grep -q "libnovo" "$t/saida" || fail "avisa das dependências novas: $(cat "$t/saida")"
# 2. a escolha puxaria outro pacote INSTALADO (firefox → glibc): recusa, nada instalado, sem snapshot
zera
printf 'LUMNA-SIM extra firefox 131.0-1\nLUMNA-SIM core glibc 2.41-1\n' > "$FAKE/sim"
bash "$PKG" upgrade-pacotes firefox > "$t/saida" 2>&1; rc=$?
[ "$rc" = 3 ] || fail "puxar outro pacote: código 3 (foi $rc)"
grep -q -- " -S --needed --noconfirm" "$t/log" && fail "não instala quando puxa outros"
grep -q "glibc" "$t/saida" || fail "devia dizer QUAL pacote seria puxado: $(cat "$t/saida")"
grep -q "subvolume snapshot" "$t/log" && fail "sem instalar, sem snapshot"
# 3. o pacman recusa a conta
zera; : > "$FAKE/sim-falha"
bash "$PKG" upgrade-pacotes vlc > "$t/saida" 2>&1; rc=$?
[ "$rc" = 3 ] && grep -q "Nada foi instalado" "$t/saida" || fail "pacman recusando: código 3 e 'Nada foi instalado' (rc=$rc): $(cat "$t/saida")"
# 4. o sistema base é recusado ANTES de qualquer coisa (mesmo que a pessoa peça direto)
zera
for b in glibc linux mesa linux-firmware-amdgpu nvidia-utils lib32-mesa; do
  : > "$t/log"; bash "$PKG" upgrade-pacotes vlc "$b" > "$t/saida" 2>&1; rc=$?
  [ "$rc" = 3 ] || fail "$b é do sistema base: código 3 (foi $rc)"
  [ ! -s "$t/log" ] || fail "o base recusa antes de falar com o pacman: $(cat "$t/log")"
done
# 5. nomes inválidos (roda como root): código 2
for ruim in 'vlc;reboot' '$(id)' 'a b' '../x' '-Syu' ''; do
  bash "$PKG" upgrade-pacotes "$ruim" >/dev/null 2>&1; rc=$?
  [ "$rc" = 2 ] || fail "nome inválido '$ruim' devia dar código 2 (foi $rc)"
done
bash "$PKG" upgrade-pacotes >/dev/null 2>&1; [ "$?" = 2 ] || fail "sem pacotes: código 2"
# 6. quem não tem atualização fica de fora; se não sobra ninguém, não instala
zera; printf 'vlc 3.0.20-1 -> 3.0.21-1\n' > "$FAKE/qu"
printf 'LUMNA-SIM extra vlc 3.0.21-1\n' > "$FAKE/sim"
bash "$PKG" upgrade-pacotes vlc htop > "$t/saida" 2>&1 || fail "um sem atualização não impede o outro"
grep -q "htop já está em dia" "$t/saida" || fail "avisa quem ficou de fora"
grep -q -- "-- vlc$" "$t/log" || fail "instala só o vlc: $(grep -- '-S --needed --noconfirm' "$t/log")"
zera; : > "$FAKE/qu"
bash "$PKG" upgrade-pacotes htop > "$t/saida" 2>&1 || fail "nada a atualizar não é erro"
grep -q -- "-S --needed --noconfirm" "$t/log" && fail "sem atualização não instala"
# 7. o pacman em uso (db.lck) e a sincronização que falha
touch "$FAKE/db/db.lck"; zera
bash "$PKG" upgrade-pacotes vlc > "$t/saida" 2>&1; rc=$?
{ [ "$rc" = 1 ] && grep -q "em uso" "$t/saida"; } || fail "db.lck: recusa (rc=$rc)"
rm -f "$FAKE/db/db.lck"; zera; : > "$FAKE/sync-falha"; printf 'vlc 1 -> 2\n' > "$FAKE/qu"
bash "$PKG" upgrade-pacotes vlc > "$t/saida" 2>&1; rc=$?
{ [ "$rc" = 1 ] && ! grep -q -- "-S --needed --noconfirm" "$t/log"; } || fail "sync falhando: código 1 sem instalar (rc=$rc)"
[ "$falhas" -eq 0 ] && echo "lumna-pkg (escolher pacotes): ok" || { echo "$falhas falha(s)"; exit 1; }
