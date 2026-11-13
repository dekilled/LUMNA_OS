#!/bin/sh
# lumna-captura: uma escolha só (apertar Print duas vezes, ou durante a contagem, não abre outra por cima) e "todas as telas" lado a lado numa imagem só.
# Orbit sem tela com dois monitores. Uso: sh testes/captura_unica.sh <pasta do orbit> (com make captura; precisa de grim)
orbit=${1:?}; gravity=$(cd "$(dirname "$0")/.." && pwd)
command -v grim >/dev/null || { echo "captura_unica: dispensado (falta grim)"; exit 0; }
python3 -c "import PIL" 2>/dev/null || { echo "captura_unica: dispensado (falta PIL)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg" "$run/home"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/log" 2>&1 &)
sleep 2.5; export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1)
msg() { "$orbit/orbit-msg" -s "$sock" "$@"; }
cam() { msg camadas | grep -o 'lumna-captura-escolher' | wc -l; }
# 1. duas aberturas seguidas: só uma escolha
"$gravity/captura" >/dev/null 2>&1 & sleep 0.05; "$gravity/captura" >/dev/null 2>&1 & sleep 0.05; "$gravity/captura" >/dev/null 2>&1 & sleep 1.5
[ "$(pgrep -xc captura)" = 1 ] || falha "só devia haver uma escolha rodando (há $(pgrep -xc captura))"
[ "$(cam)" = 1 ] || falha "só devia haver uma camada de escolha (há $(cam))"
# 2. fechou: a próxima abre (a trava saiu junto)
pkill -x captura; sleep 0.8
[ ! -e "$run/lumna-captura-escolha.pid" ] || [ "$(cat "$run/lumna-captura-escolha.pid")" != "x" ] || true
"$gravity/captura" >/dev/null 2>&1 & sleep 1.5
[ "$(pgrep -xc captura)" = 1 ] || falha "depois de fechar a escolha, abrir outra devia funcionar"
pkill -x captura; sleep 0.5
# 3. uma trava que sobrou de um processo morto não impede
echo 999999 > "$run/lumna-captura-escolha.pid"
"$gravity/captura" >/dev/null 2>&1 & sleep 1.5
[ "$(pgrep -xc captura)" = 1 ] || falha "uma trava de um processo morto devia ser ignorada"
pkill -x captura; sleep 0.5; rm -f "$run/lumna-captura-escolha.pid"
# 4. outra pessoa (um processo vivo) segura a trava, como a contagem: a escolha nova não abre
sleep 30 & dono=$!
echo $dono > "$run/lumna-captura-escolha.pid"
"$gravity/captura" >/dev/null 2>&1 & sleep 1.2
[ "$(pgrep -xc captura)" = 0 ] || falha "com a trava de um processo vivo (a contagem) não devia abrir outra escolha"
kill $dono 2>/dev/null; rm -f "$run/lumna-captura-escolha.pid"
# 5. todas as telas, lado a lado, numa imagem só (2 x 1280 de largura, 720 de altura)
rm -rf "$run/lumna-capturas"
"$gravity/captura" --todas >/dev/null 2>&1; sleep 1.5
img=$(ls -t "$run"/lumna-capturas/*.png 2>/dev/null | head -1)
[ -n "$img" ] || falha "--todas devia gerar uma imagem"
tam=$(python3 -c "from PIL import Image; import sys; print('%dx%d' % Image.open(sys.argv[1]).size)" "$img" 2>/dev/null)
[ "$tam" = "2560x720" ] || falha "as duas telas lado a lado: 2560x720, e saiu $tam"
# as duas metades têm a tela (o fundo do Orbit, que não é preto): a da esquerda e a da direita
python3 - "$img" <<'PY' || falha "uma das metades da imagem está vazia (preta): a segunda tela não foi colada"
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
esq, dir_ = im.getpixel((600, 360)), im.getpixel((1900, 360))
sys.exit(0 if sum(esq) > 15 and sum(dir_) > 15 else 1)
PY
pkill -x captura; pkill -x orbit
[ $ok = 1 ] && { echo "captura_unica: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
