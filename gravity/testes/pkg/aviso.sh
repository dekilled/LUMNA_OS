#!/bin/sh
# lumna-atualizar-aviso: o aviso na tela quando a atualização agendada termina, com um gdbus de mentira que anota a chamada. sh aviso.sh   (a partir da pasta gravity)
AQUI=$(cd "$(dirname "$0")" && pwd)
AV=$AQUI/../../../session/lumna-atualizar-aviso
t=$(mktemp -d); mkdir -p "$t/bin" "$t/varlib" "$t/estado"
printf '#!/bin/sh\n[ -e "%s/gdbus-falha" ] && exit 1\necho "$*" >> %s/gdbus.log\n' "$t" "$t" > "$t/bin/gdbus"; chmod +x "$t/bin/gdbus"
export PATH="$t/bin:$PATH" LUMNA_VARLIB="$t/varlib" XDG_STATE_HOME="$t/estado" LUMNA_AVISO_UMA_VEZ=1
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
estado() { printf 'quando=%s\nescopo=%s\nresultado=%s\npacotes=%s\nquantos=%s\n' "$1" "$2" "$3" "$4" "$5" > "$t/varlib/atualizar.estado"; }
conta() { [ -f "$t/gdbus.log" ] && wc -l < "$t/gdbus.log" || echo 0; }
visto() { cat "$t/estado/lumna/atualizar-visto" 2>/dev/null; }

# 1. sem arquivo de estado: nada
sh "$AV"; [ "$(conta)" = 0 ] || fail "sem estado não avisa"
# 2. a primeira vez nesta conta: um resultado antigo só é guardado, sem aviso
estado 1000 lumna ok "lumna-orbit lumna-shell" 0
sh "$AV"; [ "$(conta)" = 0 ] || fail "um resultado de antes de a pessoa ter o vigia não vira aviso"
[ "$(visto)" = 1000 ] || fail "devia guardar o 'visto' (foi: $(visto))"
# 3. o agendado rodou de novo (só o LUMNA, atualizou): avisa uma vez, com os pacotes
estado 2000 lumna ok "lumna-orbit lumna-shell" 0
sh "$AV"; [ "$(conta)" = 1 ] || fail "devia avisar uma vez (avisos: $(conta))"
grep -q "Atualização agendada concluída" "$t/gdbus.log" && grep -q "lumna-orbit lumna-shell" "$t/gdbus.log" && grep -q "só o LUMNA" "$t/gdbus.log" || fail "o aviso diz o que atualizou: $(tail -1 "$t/gdbus.log")"
grep -q "org.freedesktop.Notifications.Notify" "$t/gdbus.log" || fail "usa o org.freedesktop.Notifications da sessão"
grep -q "byte 1" "$t/gdbus.log" || fail "urgência normal"
[ "$(visto)" = 2000 ] || fail "marca como visto"
# 4. o mesmo resultado não avisa de novo (o vigia olha a cada minuto)
sh "$AV"; sh "$AV"; [ "$(conta)" = 1 ] || fail "o mesmo resultado não avisa duas vezes (avisos: $(conta))"
# 5. escopo "tudo" com a contagem de pacotes
estado 3000 tudo ok "" 14
sh "$AV"; [ "$(conta)" = 2 ] || fail "novo resultado, novo aviso"
tail -1 "$t/gdbus.log" | grep -q "14 pacotes" && tail -1 "$t/gdbus.log" | grep -q "o sistema" || fail "o aviso do 'tudo' diz quantos pacotes: $(tail -1 "$t/gdbus.log")"
# 6. nada a atualizar: não incomoda, mas marca como visto
estado 4000 tudo nada "" 0
sh "$AV"; [ "$(conta)" = 2 ] || fail "'nada a atualizar' não avisa"
[ "$(visto)" = 4000 ] || fail "...mas fica marcado como visto"
# 7. precisa da atualização completa
estado 5000 lumna precisa-completa "" 0
sh "$AV"; [ "$(conta)" = 3 ] || fail "precisa-completa avisa"
tail -1 "$t/gdbus.log" | grep -q "adiada" && tail -1 "$t/gdbus.log" | grep -q "Atualizar tudo" || fail "diz para usar Atualizar tudo: $(tail -1 "$t/gdbus.log")"
# 8. falhou: urgência crítica e o caminho do log
estado 6000 tudo falhou "" 0
sh "$AV"; [ "$(conta)" = 4 ] || fail "falhou avisa"
tail -1 "$t/gdbus.log" | grep -q "byte 2" && tail -1 "$t/gdbus.log" | grep -q "/var/log/lumna-atualizar.log" || fail "falha: urgência crítica e o log: $(tail -1 "$t/gdbus.log")"
# 9. o servidor de avisos ainda não subiu (gdbus falha): NÃO marca como visto, e avisa quando ele subir
estado 7000 lumna ok "lumna-orbit" 0
: > "$t/gdbus-falha"
sh "$AV"; [ "$(visto)" = 6000 ] || fail "sem como avisar o 'visto' não anda (foi $(visto))"
rm -f "$t/gdbus-falha"
sh "$AV"; [ "$(conta)" = 5 ] && [ "$(visto)" = 7000 ] || fail "quando o servidor sobe o aviso sai (avisos: $(conta), visto: $(visto))"
# 10. aspas no texto não quebram a chamada (o gdbus lê o texto como GVariant)
estado 8000 lumna ok "it's-um-pacote" 0
sh "$AV"; tail -1 "$t/gdbus.log" | grep -q "it\\\\'s-um-pacote" || fail "aspa simples escapada: $(tail -1 "$t/gdbus.log")"
[ "$falhas" -eq 0 ] && echo "lumna-atualizar-aviso: ok" || { echo "$falhas falha(s)"; exit 1; }
