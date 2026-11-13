#!/bin/sh
# Painel > Aparência > Animações com cliques de verdade num Orbit sem tela: os interruptores e o Aplicar gravam animacoes.ini e o modos.ini (troca).
# Coordenadas da janela do Painel recém-aberta (1280x720). Uso: sh testes/painel_animacoes.sh <pasta do orbit> (com make painel)
orbit=${1:?}; g=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home"
export XDG_RUNTIME_DIR="$run" XDG_CONFIG_HOME="$run/cfg" HOME="$run/home"
printf '[geral]\ntroca = carrossel\n' > "$run/cfg/lumna/modos.ini"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 &)
sleep 2.5
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1)
m() { "$orbit/orbit-msg" -s "$sock" "$@" >/dev/null; }
clica() { m mouse mover "$1" "$2"; sleep 0.3; m mouse botao baixo; sleep 0.1; m mouse botao cima; sleep 0.7; }
"$g/painel" --secao aparencia/animacoes >"$run/p.log" 2>&1 & pid=$!
sleep 5
m mouse mover 800 500; sleep 0.3; m mouse rolar 300; sleep 0.8   # a página é maior que a janela: rola até o fim
clica 1166 414; clica 1149 674   # "Deslizar" desliga, Aplicar
grep -q 'deslize = nao' "$run/cfg/lumna/animacoes.ini" 2>/dev/null || falha "o Aplicar devia gravar deslize = nao ($(cat "$run/cfg/lumna/animacoes.ini" 2>/dev/null))"
grep -q 'troca = carrossel' "$run/cfg/lumna/modos.ini" || falha "mexer só no deslize não podia mudar o carrossel ($(grep troca "$run/cfg/lumna/modos.ini"))"
grep -q 'janelas = sim' "$run/cfg/lumna/animacoes.ini" || falha "as janelas animadas não deviam mudar ao mexer no deslize ($(cat "$run/cfg/lumna/animacoes.ini"))"
clica 1166 486; clica 1149 674   # "Abrir e fechar janelas" desliga, Aplicar
grep -q 'janelas = nao' "$run/cfg/lumna/animacoes.ini" || falha "o Aplicar devia gravar janelas = nao ($(cat "$run/cfg/lumna/animacoes.ini"))"
clica 1166 567; clica 1149 674   # "Carrossel" desliga, Aplicar
grep -q 'troca = rapida' "$run/cfg/lumna/modos.ini" || falha "o Aplicar devia gravar troca = rapida ($(grep troca "$run/cfg/lumna/modos.ini"))"
grep -q 'animacoes = sim' "$run/cfg/gravity/tema.ini" 2>/dev/null || [ ! -e "$run/cfg/gravity/tema.ini" ] || falha "as animações dos apps não deviam mudar ($(grep -A1 movimento "$run/cfg/gravity/tema.ini"))"
kill $pid 2>/dev/null; pkill -x orbit
[ $ok = 1 ] && { echo "painel_animacoes: ok"; rm -rf "$run"; } || { echo "(log em $run)"; exit 1; }
