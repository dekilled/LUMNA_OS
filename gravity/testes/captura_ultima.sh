#!/bin/sh
# lumna-captura: "última área usada". Quem capturou uma área ganha a mesma área já escolhida da próxima vez (Enter captura, ou se arrasta outra); outra tela, ou sem
# nada guardado, abre sem área. Orbit sem tela, 1280x720. Uso: sh testes/captura_ultima.sh <pasta do orbit> (com make captura; precisa de grim e PIL)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd)
command -v grim >/dev/null || { echo "captura_ultima: dispensado (falta grim)"; exit 0; }
python3 -c "import PIL" 2>/dev/null || { echo "captura_ultima: dispensado (falta PIL)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && LUMNA_TESTE_TECLADOS=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5; export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1)
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
saida=$(msg saidas | grep -o '"nome":"[^"]*"' | head -1 | cut -d'"' -f4)
[ -n "$saida" ] || { echo "FALHOU: não achei o nome da saída"; exit 1; }
ini="$run/cfg/lumna/captura.ini"
luz() { # a luz do pixel x,y da tela de agora
	timeout 5 grim "$run/f.png" 2>/dev/null && python3 -W ignore -c "
from PIL import Image
im = Image.open('$run/f.png').convert('L'); print(im.getpixel(($1, $2)))"
}
abre() { "$gravity/captura" >/dev/null 2>&1 & sleep 2; }
fecha() { pkill -x captura; sleep 0.8; }
# 1. com uma área guardada para esta tela, abre já escolhida: o interior fica mais claro que o fundo escurecido
printf 'audio=nenhum\nultima=%s;100.0;80.0;300.0;200.0\n' "$saida" > "$ini"
abre
dentro=$(luz 200 150); fora=$(luz 900 400)
echo "info: com a última área guardada: dentro $dentro, fora $fora"
[ "$dentro" -gt "$fora" ] || falha "devia abrir já com a última área escolhida (dentro $dentro, fora $fora)"
# Enter captura: a mesma área segue guardada
msg tecla_falsa 0 28 >/dev/null; sleep 0.1; msg tecla_falsa 0 28 >/dev/null; sleep 1.5
grep -q "ultima=$saida;100.0;80.0;300.0;200.0" "$ini" || falha "capturar a mesma área devia manter a última área ($(cat "$ini"))"
fecha
# 2. a área é de outra tela: não vale aqui (controle negativo: sem área escolhida, dentro = fora)
printf 'audio=nenhum\nultima=OUTRA-TELA;100.0;80.0;300.0;200.0\n' > "$ini"
abre
dentro=$(luz 200 150); fora=$(luz 900 400)
echo "info: com a área de outra tela: dentro $dentro, fora $fora"
[ "$dentro" = "$fora" ] || falha "a área de outra tela não devia ser usada (dentro $dentro, fora $fora)"
fecha
# 3. nada guardado: captura uma área arrastando; a próxima vez abre com ela
rm -f "$ini"
abre
msg mouse mover 400 200 >/dev/null; sleep 0.3; msg mouse botao baixo >/dev/null; sleep 0.2
msg mouse mover 550 320 >/dev/null; sleep 0.2; msg mouse mover 700 450 >/dev/null; sleep 0.3; msg mouse botao cima >/dev/null; sleep 0.5
msg tecla_falsa 0 28 >/dev/null; sleep 0.1; msg tecla_falsa 0 28 >/dev/null; sleep 3
grep -q "^ultima=$saida;400" "$ini" || falha "arrastar e capturar devia guardar a área ($(cat "$ini" 2>/dev/null))"
fecha
abre
dentro=$(luz 500 300); fora=$(luz 200 550)
echo "info: depois de capturar arrastando: dentro $dentro, fora $fora ($(grep ultima "$ini"))"
[ "$dentro" -gt "$fora" ] || falha "a área arrastada devia voltar escolhida (dentro $dentro, fora $fora)"
pkill -x orbit
[ $ok = 1 ] && { echo "captura_ultima: ok"; rm -rf "$run"; } || { echo "(em $run)"; exit 1; }
