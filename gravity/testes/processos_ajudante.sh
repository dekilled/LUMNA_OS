#!/bin/sh
# O ajudante com privilégio do gerenciador de processos: só aceita números e as palavras do protocolo, protege o PID 1, e faz o que se pede.
# Roda como quem está (em teste, root, sem o pkexec).
AQUI=$(cd "$(dirname "$0")/.." && pwd)
cd "$AQUI"; H="$AQUI/processos-ajudante"
[ -x "$H" ] || { echo "ajudante: faltou o make processos-ajudante"; exit 1; }
ok=1
falha() { echo "FALHOU: $1" >&2; ok=0; }
sh -c 'while :; do :; done' & p=$!
sleep 0.3
ini=$(awk '{ s=$0; sub(/.*\) /, "", s); split(s, c, " "); print c[20] }' /proc/$p/stat)
"$H" prioridade $p $ini 0; [ $? -eq 0 ] && [ "$(awk '{ s=$0; sub(/.*\) /, "", s); split(s, c, " "); print c[17] }' /proc/$p/stat)" = "10" ] || falha "prioridade Baixa devia pôr nice 10"
"$H" prioridade $p $((ini + 1)) 1 2>/dev/null; [ $? -eq 4 ] || falha "um instante de início errado devia sair com 4 (o processo mudou)"
"$H" prioridade 1 0 0 2>/dev/null; [ $? -eq 3 ] || falha "o PID 1 devia sair com 3 (protegido)"
"$H" prioridade abc 0 0 2>/dev/null; [ $? -eq 1 ] || falha "um PID que não é número devia ser recusado"
"$H" prioridade "$p; echo x" $ini 0 2>/dev/null; [ $? -eq 1 ] || falha "um argumento com ; devia ser recusado"
"$H" encerrar $p $ini um espetar 2>/dev/null; [ $? -eq 1 ] || falha "uma palavra fora do protocolo devia ser recusada"
"$H" encerrar -5 0 um forcar 2>/dev/null; [ $? -eq 1 ] || falha "um PID negativo devia ser recusado"
"$H" encerrar $p $ini um forcar >/dev/null; r=$?
wait $p 2>/dev/null; s=$?
[ $r -eq 0 ] && [ $s -eq 137 ] || falha "encerrar forcar devia tirar o processo com SIGKILL (rc $r, estado $s)"
# os serviços: um ajudante compilado com um systemctl de mentira (que anota o que recebeu) e entradas que não podem passar
T=$(mktemp -d)
printf '#!/bin/sh\necho "$@" >> "%s/chamadas"\n[ -e "%s/falha" ] && { echo "Failed to stop: Unit not loaded." >&2; exit 5; }\nexit 0\n' "$T" "$T" > "$T/systemctl"; chmod +x "$T/systemctl"
cc -std=gnu11 -Wall -Wextra -Iservicos -DSYSTEMCTL_CAMINHO="\"$T/systemctl\"" -o "$T/ajudante" exemplos/processos-ajudante.c servicos/processos.c || falha "o ajudante de teste devia compilar"
A="$T/ajudante"
: > "$T/chamadas"
for acao in start stop restart freeze thaw; do
	"$A" servico $acao backup.service; [ $? -eq 0 ] || falha "servico $acao backup.service devia dar certo"
done
[ "$(cat "$T/chamadas")" = "$(printf -- '--no-ask-password start -- backup.service\n--no-ask-password stop -- backup.service\n--no-ask-password restart -- backup.service\n--no-ask-password freeze -- backup.service\n--no-ask-password thaw -- backup.service')" ] || falha "o systemctl devia receber exatamente a ação e a unidade, depois de '--' ($(cat "$T/chamadas" | tr '\n' '|'))"
"$A" servico stop fstrim.timer >/dev/null; [ $? -eq 0 ] || falha "um timer também pode"
: > "$T/chamadas"
for ruim in "backup" "backup.socket" "../backup.service" "/etc/x.service" "-backup.service" ".oculto.service" "a b.service" "a;b.service" 'a$(id).service' "a|b.service" "a&b.service" ".service" "x.service.d" "$(printf 'a%.0s' $(seq 1 120)).service"; do
	"$A" servico stop "$ruim" 2>/dev/null; [ $? -eq 1 ] || falha "a unidade '$ruim' devia ser recusada (código 1)"
done
for acao in enable disable mask kill poweroff "start --now" ""; do
	"$A" servico "$acao" backup.service 2>/dev/null; [ $? -eq 1 ] || falha "a ação '$acao' devia ser recusada"
done
"$A" servico stop 2>/dev/null; [ $? -eq 1 ] || falha "sem unidade devia ser recusado"
"$A" servico stop backup.service extra 2>/dev/null; [ $? -eq 1 ] || falha "argumento a mais devia ser recusado"
[ ! -s "$T/chamadas" ] || falha "nada recusado pode chegar ao systemctl ($(cat "$T/chamadas" | tr '\n' '|'))"
for u in dbus.service systemd-logind.service greetd.service user@1000.service getty@tty1.service systemd-journald.service; do
	for acao in stop restart freeze; do
		"$A" servico $acao $u 2>/dev/null; [ $? -eq 3 ] || falha "$acao $u devia ser protegido (código 3)"
	done
done
[ ! -s "$T/chamadas" ] || falha "os essenciais não podem chegar ao systemctl"
"$A" servico start dbus.service; [ $? -eq 0 ] || falha "iniciar um essencial pode (é o contrário de derrubar)"
"$A" servico thaw systemd-logind.service; [ $? -eq 0 ] || falha "retomar um essencial pode"
touch "$T/falha"; "$A" servico stop backup.service 2>"$T/erro"; [ $? -eq 1 ] || falha "uma falha do systemctl devia sair com 1"
grep -q "Unit not loaded" "$T/erro" || falha "o motivo do systemctl devia ir para a saída de erro"
rm -rf "$T"
[ $ok = 1 ] && echo "ajudante de processos: ok" || exit 1
