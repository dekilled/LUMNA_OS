#!/bin/sh
# O NumLock: liga em todos os teclados (um PC de verdade tem vários dispositivos de teclado) e não apaga quando a configuração é relida ou
# aparece outro teclado. Usa teclados falsos do Orbit (LUMNA_TESTE_TECLADOS). Uso: sh testes/numlock.sh <pasta do orbit>
orbit=${1:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
ok=1
falha() { echo "FALHOU: $1" >&2; ok=0; }
sobe() { (cd "$orbit" && LUMNA_TESTE_TECLADOS=${1:-3} WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &); sleep 2.5; sock=$(ls "$run"/orbit.*.sock | head -1); }
numlock() { "$orbit/orbit-msg" -s "$sock" entrada | grep -o '"numlock":\[[a-z,]*\]' | cut -d: -f2; }
para() { pkill -x orbit; sleep 1; rm -f "$run"/orbit.*.sock "$run"/wayland-*; }
# 1. o padrão: ligado, nos três teclados (antes só o último ficava ligado: cada teclado novo refazia o mapa de todos)
sobe 3
[ "$(numlock)" = "[true,true,true]" ] || falha "o NumLock devia estar ligado nos três teclados ($(numlock))"
# 2. a configuração do teclado muda (outro layout): o mapa é refeito e o NumLock continua ligado
printf '[teclado]\nlayout = us\n' > "$run/cfg/lumna/entrada.ini"; sleep 2
[ "$(numlock)" = "[true,true,true]" ] || falha "depois de mudar o layout o NumLock devia continuar ligado ($(numlock))"
printf '[teclado]\nlayout = br\n' > "$run/cfg/lumna/entrada.ini"; sleep 2
[ "$(numlock)" = "[true,true,true]" ] || falha "depois de mudar o layout de novo o NumLock devia continuar ligado ($(numlock))"
para
# 3. a opção numlock=0 desliga o padrão
printf '[teclado]\nnumlock = 0\n' > "$run/cfg/lumna/entrada.ini"
sobe 2
[ "$(numlock)" = "[false,false]" ] || falha "com numlock=0 o NumLock devia começar apagado ($(numlock))"
para
[ $ok = 1 ] && { echo "numlock: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
