#!/bin/sh
# lumna-pkg: o snapshot antes de atualizar (e a poda dos antigos) e o voltar, com btrfs, findmnt, mount e pacman de mentira.
# sh snapshot.sh   (a partir da pasta gravity)
AQUI=$(cd "$(dirname "$0")" && pwd)
PKG=$AQUI/../../../session/lumna-pkg
t=$(mktemp -d)
export PATH=$AQUI/bin:$PATH FALSO_LOG=$t/log LUMNA_SNAPDIR=$t/snaps
chmod +x "$AQUI"/bin/*
mkdir -p "$t/snaps"
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
for i in 1 2 3 4 5 6 7; do mkdir "$t/snaps/antes-20260101-00000$i"; done   # já havia 7
bash "$PKG" upgrade > "$t/saida" 2>&1
n=$(ls -1d "$t/snaps"/antes-* | wc -l)
[ "$n" = 5 ] || fail "devia guardar só os 5 últimos (ficaram $n): $(ls "$t/snaps")"
[ -d "$t/snaps/antes-20260101-000001" ] && fail "o mais antigo devia ter sido apagado"
[ -d "$t/snaps/antes-20260101-000007" ] || fail "o mais novo antigo devia ficar"
grep -q "^btrfs subvolume snapshot -r / $t/snaps/antes-" "$t/log" || fail "devia tirar o snapshot só-leitura de /: $(cat "$t/log")"
# o snapshot vem ANTES do pacman
snap=$(grep -n "snapshot -r" "$t/log" | head -1 | cut -d: -f1); pac=$(grep -n "^pacman -Syu" "$t/log" | cut -d: -f1)
[ -n "$snap" ] && [ -n "$pac" ] && [ "$snap" -lt "$pac" ] || fail "o snapshot devia vir antes do pacman -Syu: $(cat "$t/log")"
grep -q "Snapshot antes-" "$t/saida" || fail "devia avisar do snapshot: $(cat "$t/saida")"
[ "$(bash "$PKG" snapshots | wc -l)" = 5 ] || fail "snapshots devia listar 5"
# voltar: sem dizer qual, o último
: > "$t/log"
bash "$PKG" voltar > "$t/saida" 2>&1 || fail "voltar falhou: $(cat "$t/saida")"
ultimo=$(ls -1d "$t/snaps"/antes-* | sort | tail -1)
grep -q "^mount -o subvolid=5 /dev/vda2 " "$t/log" || fail "devia montar o topo do Btrfs (sem o [/@] do findmnt): $(cat "$t/log")"
grep -q "^btrfs subvolume snapshot $ultimo .*/@\$" "$t/log" || fail "o último snapshot devia virar o novo @: $(cat "$t/log")"
grep -q "^umount " "$t/log" || fail "devia desmontar o topo"
grep -q "Reinicie" "$t/saida" || fail "devia mandar reiniciar: $(cat "$t/saida")"
# voltar a um que não existe
bash "$PKG" voltar nao-existe > "$t/saida" 2>&1 && fail "voltar a um snapshot inexistente devia falhar"
# sem a pasta de snapshots (sem Btrfs): atualiza do mesmo jeito, sem snapshot
rm -rf "$t/snaps"; : > "$t/log"
bash "$PKG" upgrade > "$t/saida" 2>&1
grep -q "^pacman -Syu" "$t/log" || fail "sem snapshots a atualização devia seguir"
grep -q "subvolume" "$t/log" && fail "sem a pasta não devia tentar snapshot"
[ "$falhas" -eq 0 ] && echo "lumna-pkg (snapshot e voltar): ok" || { echo "$falhas falha(s)"; exit 1; }
