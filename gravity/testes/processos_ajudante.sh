#!/bin/sh
# O ajudante com privilégio do gerenciador de processos: só aceita números e as palavras do protocolo, protege o PID 1, e faz o que se pede.
# Roda como quem está (em teste, root, sem o pkexec).
AQUI=$(cd "$(dirname "$0")/.." && pwd)
H="$AQUI/processos-ajudante"
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
[ $ok = 1 ] && echo "ajudante de processos: ok" || exit 1
