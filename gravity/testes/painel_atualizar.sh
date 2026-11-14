#!/bin/sh
# Painel > Aplicativos > Atualizações: o "Atualizar tudo" abre um terminal; quando ele termina o Painel procura de novo sozinho, e os botões de atualização somem
# ("Tudo em dia"). Num Orbit sem tela, com um checkupdates, um sudo, um pacman e um terminal falsos. Uso: sh testes/painel_atualizar.sh <pasta do orbit> (com make painel)
orbit=${1:?}; g=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin" "$run/estado"
cat > "$run/bin/checkupdates" <<FIM
#!/bin/sh
[ -f "$run/estado/pendente" ] && { printf 'firefox 130.0-1 -> 131.0-1\nlinux 6.10.1-1 -> 6.10.2-1\n'; exit 0; }
exit 2
FIM
printf '#!/bin/sh\nexec "$@"\n' > "$run/bin/sudo"
cat > "$run/bin/pacman" <<FIM
#!/bin/sh
case "\$*" in *-Syu*) echo "\$*" >> "$run/estado/chamadas"; sleep 2; [ -f "$run/estado/nao-resolve" ] || rm -f "$run/estado/pendente" ;; esac
exit 0
FIM
printf '#!/bin/sh\nexec "$@" </dev/null\n' > "$run/bin/foot"; chmod +x "$run"/bin/*
export PATH="$run/bin:$PATH" XDG_RUNTIME_DIR="$run" XDG_CONFIG_HOME="$run/cfg" HOME="$run/home" LUMNA_TERMINAL=foot
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
(cd "$orbit" && WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 &)
sleep 2.5
export WAYLAND_DISPLAY=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1)
m() { "$orbit/orbit-msg" -s "$sock" "$@" >/dev/null; }
clica() { m mouse mover "$1" "$2"; sleep 0.3; m mouse botao baixo; sleep 0.1; m mouse botao cima; sleep 0.7; }
roxo() { # o botão cheio (roxo) está neste ponto da tela?
	timeout 5 grim "$run/t.png" && python3 -W ignore -c "
from PIL import Image
r, gr, b = Image.open('$run/t.png').convert('RGB').getpixel(($1, $2))
print('sim' if r > 140 and b > 200 else 'nao')"
}
verde() { # o texto "Tudo em dia" (verde) aparece na faixa de cima?
	python3 -W ignore -c "
from PIL import Image
im = Image.open('$run/t.png').convert('RGB').crop((400, 262, 700, 296))
print('sim' if any(g > 150 and g > r + 30 and g > b + 20 for r, g, b in im.getdata()) else 'nao')"
}
touch "$run/estado/pendente"
"$g/painel" --secao aplicativos/atualizacoes >"$run/p.log" 2>&1 & pid=$!
sleep 5
clica 1125 278; sleep 4   # Procurar
[ "$(roxo 1034 268)" = sim ] || falha "depois de procurar devia haver o botão Atualizar tudo"
clica 1107 278            # Atualizar tudo: o terminal falso roda o pacman (2 s) e termina
sleep 8
grep -q -- '-Syu' "$run/estado/chamadas" 2>/dev/null || falha "o Atualizar tudo devia rodar o pacman -Syu no terminal"
[ "$(roxo 1034 268)" = nao ] || falha "terminada a atualização, o botão Atualizar tudo devia sumir sozinho"
[ "$(verde)" = sim ] || falha "terminada a atualização devia dizer Tudo em dia"
# controle negativo: se a atualização não resolve (o pacman falha), os botões continuam
touch "$run/estado/pendente" "$run/estado/nao-resolve"
clica 1125 278; sleep 4   # Procurar de novo
[ "$(roxo 1034 268)" = sim ] || falha "com atualização pendente o botão devia voltar"
clica 1107 278; sleep 8
[ "$(roxo 1034 268)" = sim ] || falha "se a atualização não resolveu, o botão Atualizar tudo devia continuar (o teste distingue os dois casos)"
kill $pid 2>/dev/null; pkill -x orbit
[ $ok = 1 ] && { echo "painel_atualizar: ok"; rm -rf "$run"; } || { echo "(em $run)"; exit 1; }
