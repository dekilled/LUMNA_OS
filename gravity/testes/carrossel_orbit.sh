#!/bin/sh
# O carrossel de modos no Orbit (sem tela, teclados falsos): Super+Tab abre sem trocar, Tab gira, soltar o Super confirma, Esc cancela,
# e no modo "rapida" (o padrão) tudo continua como antes. Uso: sh testes/carrossel_orbit.sh <pasta do orbit>
orbit=${1:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
printf '#!/bin/sh\necho "$LUMNA_SAIDA $LUMNA_MODO" >> %s/carrossel.chamadas\n' "$run" > "$run/bin/lumna-carrossel"; chmod +x "$run/bin/lumna-carrossel"
printf '#!/bin/sh\nexit 0\n' > "$run/bin/lumna-modos"; chmod +x "$run/bin/lumna-modos"
printf 'lancador = touch %s/lancou\n' "$run" > "$run/orbit.conf"
export PATH="$run/bin:$PATH" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run" ORBIT_CONFIG="$run/orbit.conf"
ok=1
falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && LUMNA_TESTE_TECLADOS=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5; sock=$(ls "$run"/orbit.*.sock | head -1)
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
tecla() { msg tecla_falsa 0 "$@" >/dev/null; sleep 0.15; }
modo() { msg modo | grep -o '"modo":"[a-z]*"' | head -1 | cut -d'"' -f4; }
aberto() { msg carrossel | grep -o '"aberto":[a-z]*' | cut -d: -f2; }
alvo() { msg carrossel | grep -o '"alvo":"[a-z]*"' | cut -d'"' -f4; }
SUPER=125; TAB=15; ESC=1; ENTER=28; DIREITA=106; ESQ=105; SHIFT=42
# 1. o padrão ("rapida"): Super+Tab não abre carrossel, e o segundo Super+Tab troca como sempre
tecla $SUPER baixo; tecla $TAB; tecla $TAB; tecla $SUPER cima
[ "$(aberto)" = false ] || falha "no modo rapida o carrossel não devia abrir"
[ "$(modo)" = gaming ] || falha "no modo rapida o segundo Super+Tab devia trocar para o Gaming (está $(modo))"
msg modo desktop >/dev/null; rm -f "$run/lancou"
# 2. carrossel: abre sem trocar, gira, confirma ao soltar o Super
printf '[geral]\ntroca = carrossel\n' > "$run/cfg/lumna/modos.ini"
tecla $SUPER baixo; tecla $TAB
[ "$(aberto)" = true ] || falha "Super+Tab devia abrir o carrossel"
[ "$(alvo)" = desktop ] || falha "ao abrir, o alvo é o modo de agora (é $(alvo))"
tecla $TAB; [ "$(alvo)" = gaming ] || falha "Tab devia ir para o Gaming (é $(alvo))"
tecla $TAB; [ "$(alvo)" = midia ] || falha "Tab devia ir para a Mídia (é $(alvo))"
tecla $TAB; [ "$(alvo)" = desktop ] || falha "Tab devia dar a volta para o Desktop (é $(alvo))"
tecla $SHIFT baixo; tecla $TAB; tecla $SHIFT cima
[ "$(alvo)" = midia ] || falha "Shift+Tab devia voltar para a Mídia (é $(alvo))"
tecla $DIREITA; [ "$(alvo)" = desktop ] || falha "seta direita devia girar para o Desktop (é $(alvo))"
tecla $DIREITA; [ "$(alvo)" = gaming ] || falha "seta direita devia girar para o Gaming (é $(alvo))"
[ "$(modo)" = desktop ] || falha "enquanto o Super está apertado nada troca (está $(modo))"
tecla $TAB cima  # soltar o Tab, o Super continua
[ "$(aberto)" = true ] || falha "soltar o Tab não devia fechar o carrossel"
[ "$(modo)" = desktop ] || falha "soltar só o Tab não devia trocar o modo (está $(modo))"
tecla $SUPER cima
[ "$(aberto)" = false ] || falha "soltar o Super devia fechar o carrossel"
[ "$(modo)" = gaming ] || falha "soltar o Super devia trazer o modo do centro (Gaming; está $(modo))"
[ -e "$run/lancou" ] && falha "soltar o Super depois do Super+Tab abriu o Início"
# 3. Esc cancela e não troca nada
tecla $SUPER baixo; tecla $TAB; tecla $TAB; tecla $ESC; tecla $SUPER cima
[ "$(aberto)" = false ] || falha "Esc devia fechar o carrossel"
[ "$(modo)" = gaming ] || falha "Esc devia manter o modo (está $(modo))"
[ -e "$run/lancou" ] && falha "Esc e soltar o Super abriu o Início"
# 4. Enter confirma na hora, Super ainda apertado
tecla $SUPER baixo; tecla $TAB; tecla $TAB; tecla $ENTER
[ "$(aberto)" = false ] || falha "Enter devia fechar o carrossel"
tecla $SUPER cima
# 5. quem clica num cartão (o lumna-carrossel manda isto) confirma aquele modo
tecla $SUPER baixo; tecla $TAB; msg carrossel confirmar desktop >/dev/null; tecla $SUPER cima
[ "$(modo)" = desktop ] || falha "confirmar desktop pelo orbit-msg devia trazer o Desktop (está $(modo))"
# 6. o carrossel só abriu o programa: um por abertura, na saída certa
n=$(wc -l < "$run/carrossel.chamadas" 2>/dev/null || echo 0)
[ "$n" -ge 3 ] || falha "o lumna-carrossel devia ter sido chamado a cada abertura (chamadas: $n)"
# 7. a configuração volta para rapida: tudo como antes
printf '[geral]\ntroca = rapida\n' > "$run/cfg/lumna/modos.ini"
tecla $SUPER baixo; tecla $TAB; tecla $SUPER cima
[ "$(aberto)" = false ] || falha "com troca=rapida o carrossel não devia abrir"
pkill -x orbit
[ $ok = 1 ] && { echo "carrossel_orbit: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
