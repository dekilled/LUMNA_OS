#!/bin/sh
# lumna-pkg: "atualizar só o LUMNA" e o horário marcado, com pacman, pacman-conf e systemctl de mentira (o curl, o bsdtar e o btrfs de snapshot são os de verdade/de mentira
# da pasta bin). sh lumna.sh   (a partir da pasta gravity)
AQUI=$(cd "$(dirname "$0")" && pwd)
PKG=$AQUI/../../../session/lumna-pkg
t=$(mktemp -d)
export PATH=$AQUI/bin-lumna:$AQUI/bin:$PATH FALSO_LOG=$t/log FAKE=$t/fake LUMNA_SNAPDIR=$t/snaps LUMNA_ETC=$t/etc LUMNA_VARLIB=$t/varlib LUMNA_LOG_ATUALIZAR=$t/atualizar.log
chmod +x "$AQUI"/bin/*
mkdir -p "$FAKE/db/sync" "$FAKE/repo" "$t/snaps"
( cd "$FAKE/repo" && mkdir -p pkg && echo x > pkg/desc && tar czf lumna.db pkg )   # um "banco" de verdade (tar comprimido)
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
zera() { : > "$t/log"; rm -f "$FAKE/sim-falha"; rm -rf "$t/snaps"; mkdir -p "$t/snaps"; }
printf 'lumna-orbit\nlumna-gravity\nlumna-shell\n' > "$FAKE/lumna-pkgs"

# 1. há atualização do LUMNA e dá para instalar só ela: instala só os do [lumna], com snapshot, SEM sincronizar o Arch
printf 'lumna-orbit 1.0 -> 1.1\nfirefox 1 -> 2\nglibc 2.1 -> 2.2\nlumna-gravity 3 -> 4\n' > "$FAKE/qu"
printf 'LUMNA-SIM lumna lumna-orbit 1.1\nLUMNA-SIM lumna lumna-gravity 4\n' > "$FAKE/sim"
zera
bash "$PKG" upgrade-lumna > "$t/saida" 2>&1 || fail "upgrade-lumna devia terminar bem: $(cat "$t/saida")"
grep -q "^pacman -S --needed --noconfirm --noprogressbar -- lumna-gravity lumna-orbit$" "$t/log" || fail "devia instalar SÓ os pacotes do LUMNA com atualização: $(cat "$t/log")"
grep -q "firefox\|glibc" "$t/log" && fail "nada do Arch pode entrar: $(cat "$t/log")"
grep -q "^pacman -Sy\|^pacman -Syu" "$t/log" && fail "não pode sincronizar nem atualizar o Arch: $(cat "$t/log")"
grep -q "^btrfs subvolume snapshot -r / $t/snaps/antes-" "$t/log" || fail "devia tirar o snapshot antes de instalar"
snap=$(grep -n "snapshot -r" "$t/log" | head -1 | cut -d: -f1); ins=$(grep -n "^pacman -S --needed --noconfirm" "$t/log" | cut -d: -f1)
[ -n "$snap" ] && [ -n "$ins" ] && [ "$snap" -lt "$ins" ] || fail "o snapshot vem ANTES da instalação"
[ -s "$FAKE/db/sync/lumna.db" ] || fail "o banco do [lumna] devia ser baixado para o sync"
cmp -s "$FAKE/db/sync/lumna.db" "$FAKE/repo/lumna.db" || fail "o banco baixado devia ser o do repositório"

# 2. a atualização do LUMNA exigiria pacotes do Arch: para, não instala nada e não tira snapshot
printf 'LUMNA-SIM lumna lumna-orbit 1.1\nLUMNA-SIM extra wlroots 0.99-1\n' > "$FAKE/sim"
zera
bash "$PKG" upgrade-lumna > "$t/saida" 2>&1; rc=$?
[ "$rc" = 3 ] || fail "precisando do Arch o código de saída é 3 (foi $rc)"
grep -q "^pacman -S --needed --noconfirm" "$t/log" && fail "não pode instalar nada quando exige o Arch"
grep -q "wlroots" "$t/saida" || fail "devia dizer QUAL pacote do Arch seria preciso: $(cat "$t/saida")"
grep -q "subvolume snapshot" "$t/log" && fail "sem instalar, sem snapshot"

# 3. o pacman recusa a conta (dependência não satisfeita): também para com o código 3
zera; : > "$FAKE/sim-falha"
bash "$PKG" upgrade-lumna > "$t/saida" 2>&1; rc=$?
[ "$rc" = 3 ] || fail "o pacman recusando a conta: código 3 (foi $rc)"
grep -q "^pacman -S --needed --noconfirm" "$t/log" && fail "não instala se a conta falhou"
grep -q "atualize tudo" "$t/saida" || fail "devia mandar atualizar tudo: $(cat "$t/saida")"

# 4. nada do LUMNA a atualizar (só o Arch): em dia, sem snapshot e sem instalar
printf 'firefox 1 -> 2\n' > "$FAKE/qu"; rm -f "$FAKE/sim-falha"
zera
bash "$PKG" upgrade-lumna > "$t/saida" 2>&1 || fail "em dia termina bem"
grep -q "em dia" "$t/saida" || fail "devia dizer que o LUMNA está em dia"
grep -q "^pacman -S \|subvolume" "$t/log" && fail "em dia não instala nem tira snapshot: $(cat "$t/log")"

# 5. o repositório não responde: falha sem instalar
printf 'lumna-orbit 1.0 -> 1.1\n' > "$FAKE/qu"
zera; rm -f "$FAKE/db/sync/lumna.db"; mv "$FAKE/repo/lumna.db" "$FAKE/repo/lumna.db.off"
bash "$PKG" upgrade-lumna > "$t/saida" 2>&1; rc=$?
[ "$rc" = 1 ] || fail "sem repositório o código é 1 (foi $rc)"
grep -q "^pacman -S " "$t/log" && fail "sem repositório não instala"
[ -e "$FAKE/db/sync/lumna.db" ] && fail "um download que falhou não deixa banco pela metade"
mv "$FAKE/repo/lumna.db.off" "$FAKE/repo/lumna.db"
# 5b. o pacman em uso (db.lck): não mexe
touch "$FAKE/db/db.lck"; zera
bash "$PKG" upgrade-lumna > "$t/saida" 2>&1; rc=$?
[ "$rc" = 1 ] && grep -q "em uso" "$t/saida" || fail "com o pacman em uso devia recusar (rc=$rc): $(cat "$t/saida")"
rm -f "$FAKE/db/db.lck"

# 6. o agendamento: arquivos e o systemctl
zera
bash "$PKG" agendar ligado lumna 03:30 seg,qua,sex > "$t/saida" 2>&1 || fail "agendar devia terminar bem: $(cat "$t/saida")"
conf=$t/etc/lumna/atualizar.conf
[ "$(cat "$conf")" = "modo=ligado
escopo=lumna
hora=03:30
dias=seg,qua,sex" ] || fail "a configuração gravada: $(cat "$conf" 2>&1)"
drop=$t/etc/systemd/system/lumna-atualizar.timer.d/horario.conf
grep -q "^OnCalendar=Mon,Wed,Fri \*-\*-\* 03:30:00$" "$drop" || fail "o horário do timer (dias em inglês para o systemd): $(cat "$drop" 2>&1)"
grep -q "^Persistent=true" "$drop" || fail "Persistent=true: roda ao ligar se o PC estava desligado"
grep -q "^OnCalendar=$" "$drop" || fail "o OnCalendar vazio zera o padrão do arquivo da unidade"
grep -q "^systemctl enable --now lumna-atualizar.timer" "$t/log" || fail "ligar o timer: $(cat "$t/log")"
bash "$PKG" agendar ligado tudo 22:00 todos >/dev/null || fail "agendar todos os dias"
grep -q "^OnCalendar=\*-\*-\* 22:00:00$" "$drop" || fail "todos os dias: $(cat "$drop")"
: > "$t/log"; bash "$PKG" agendar desligado tudo 22:00 todos >/dev/null
grep -q "^systemctl disable --now lumna-atualizar.timer" "$t/log" || fail "desligar o timer: $(cat "$t/log")"
grep -q "^modo=desligado" "$conf" || fail "a configuração registra desligado"
# entradas inválidas (o script roda como root): recusa e não escreve
rm -rf "$t/etc"
for ruim in "ligado lumna 25:00 todos" "ligado lumna 3:00 todos" "ligado lumna 03:00 seg;reboot" "ligado lumna 03:00 xyz" "ligado tudo 03:00 \$(id)" "ligado nada 03:00 todos" "talvez lumna 03:00 todos" "ligado lumna 03:00 seg,"; do
  bash "$PKG" agendar $ruim >/dev/null 2>&1; rc=$?
  [ "$rc" = 2 ] || fail "entrada inválida devia dar código 2 (agendar $ruim → $rc)"
done
[ ! -e "$t/etc" ] || fail "entradas inválidas não escrevem nada"

# 7. o que o timer chama: só LUMNA → anota o resultado
mkdir -p "$t/etc/lumna"; printf 'modo=ligado\nescopo=lumna\nhora=03:00\ndias=todos\n' > "$t/etc/lumna/atualizar.conf"
printf 'lumna-orbit 1.0 -> 1.1\n' > "$FAKE/qu"; printf 'LUMNA-SIM lumna lumna-orbit 1.1\n' > "$FAKE/sim"
zera
bash "$PKG" agendado >/dev/null 2>&1 || fail "o agendado devia terminar bem"
est=$t/varlib/atualizar.estado
grep -q "^resultado=ok" "$est" && grep -q "^escopo=lumna" "$est" && grep -q "^pacotes=lumna-orbit" "$est" || fail "o estado do agendado: $(cat "$est" 2>&1)"
grep -q "^quando=[0-9]\{9,\}" "$est" || fail "o estado tem a hora (epoch)"
grep -q "atualização agendada (escopo: lumna)" "$t/atualizar.log" || fail "o registro do agendado: $(cat "$t/atualizar.log" 2>&1)"
# precisa do Arch → "precisa-completa", e o agendado não fracassa (não é erro: é uma decisão)
printf 'LUMNA-SIM extra wlroots 0.99-1\n' > "$FAKE/sim"; zera
bash "$PKG" agendado >/dev/null 2>&1 || fail "precisar da atualização completa não é falha do agendado"
grep -q "^resultado=precisa-completa" "$est" || fail "devia anotar precisa-completa: $(cat "$est")"
# em dia → nada
printf 'firefox 1 -> 2\n' > "$FAKE/qu"; zera
bash "$PKG" agendado >/dev/null 2>&1; grep -q "^resultado=nada" "$est" || fail "em dia: $(cat "$est")"
# tudo: pacman -Syu com snapshot
printf 'modo=ligado\nescopo=tudo\nhora=03:00\ndias=todos\n' > "$t/etc/lumna/atualizar.conf"; zera
printf 'resolving dependencies...\n(1/3) upgrading foo\n(2/3) upgrading bar\n(3/3) upgrading baz\n' > "$FAKE/syu-out"; echo 0 > "$FAKE/syu-rc"
bash "$PKG" agendado >/dev/null 2>&1 || fail "agendado (tudo) devia terminar bem"
grep -q "^pacman -Syu --noconfirm --noprogressbar" "$t/log" || fail "escopo tudo: pacman -Syu: $(cat "$t/log")"
grep -q "subvolume snapshot" "$t/log" || fail "escopo tudo: snapshot antes"
grep -q "^resultado=ok" "$est" || fail "tudo ok: $(cat "$est")"
grep -q "^quantos=3$" "$est" || fail "o estado do 'tudo' conta os pacotes atualizados (3): $(cat "$est")"
printf ' there is nothing to do\n' > "$FAKE/syu-out"; zera
bash "$PKG" agendado >/dev/null 2>&1; grep -q "^resultado=nada" "$est" || fail "tudo sem nada a fazer: $(cat "$est")"
echo 1 > "$FAKE/syu-rc"; zera
bash "$PKG" agendado >/dev/null 2>&1 && fail "pacman falhando é falha do agendado (o systemd registra)"
grep -q "^resultado=falhou" "$est" || fail "tudo falhou: $(cat "$est")"
[ "$falhas" -eq 0 ] && echo "lumna-pkg (só LUMNA e agendamento): ok" || { echo "$falhas falha(s)"; exit 1; }
