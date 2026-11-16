#!/bin/sh
# Painel > Sistema > Sobre > Atualizações do sistema > Escolher: a lista com caixinhas. Num Orbit sem tela, com o checkupdates/pacman, o analisador, o pkexec e o terminal de mentira:
# confere que só os pacotes liberados se marcam (os bloqueados não), que "Marcar os liberados" e "Atualizar N" funcionam, e o comando que vai ao lumna-pkg.
# Uso: sh testes/painel_escolher.sh <pasta do orbit>   (com make painel; precisa de grim e python3 com PIL)
orbit=${1:?}; g=$(cd "$(dirname "$0")/.." && pwd)
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/home" "$run/bin"
python3 "$g/testes/painel/monta_sistema_falso.py" "$run/raiz"
cat > "$run/raiz/bin/analisador" <<'FIM'
#!/bin/sh
sleep 1
printf 'vlc\t3.0.20-1\t3.0.21-1\textra\tok\tInstala também (novos): libnovo\n'
printf 'htop\t3.3-1\t3.4-1\textra\tok\t\n'
printf 'firefox\t130-1\t131-1\textra\tarrasta\tPuxaria também: glibc\n'
printf 'libfoo\t1-1\t2-1\t-\tquebra\tinstalling libfoo breaks dependency libfoo.so=1 required by bar\n'
printf 'linux\t6.10.1-1\t6.10.2-1\t-\tbase\tSistema base: só se atualiza junto com tudo.\n'
FIM
chmod +x "$run/raiz/bin/analisador"
printf '#!/bin/sh\necho "$*" >> %s/pkexec.log\n' "$run" > "$run/bin/pkexec"; printf '#!/bin/sh\nexec "$@" </dev/null\n' > "$run/bin/foot"; chmod +x "$run/bin"/*
(cd "$orbit" && XDG_RUNTIME_DIR="$run" XDG_CONFIG_HOME="$run/cfg" WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 &)
sleep 2.5
W=$(ls "$run" | grep -m1 '^wayland-[0-9]*$'); sock=$(ls "$run"/orbit.*.sock | head -1)
export XDG_RUNTIME_DIR="$run" WAYLAND_DISPLAY="$W" HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" LUMNA_RAIZ_SISTEMA="$run/raiz" LUMNA_ETC="$run/raiz/etc" LUMNA_VARLIB="$run/raiz/var/lib/lumna" LUMNA_PKG_ANALISAR="$run/raiz/bin/analisador" LUMNA_TERMINAL=foot PATH="$run/bin:$run/raiz/bin:$PATH"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
m() { "$orbit/orbit-msg" -s "$sock" "$@" >/dev/null; }
clica() { m mouse mover "$1" "$2"; sleep 0.3; m mouse botao baixo; sleep 0.1; m mouse botao cima; sleep 1.5; }
roxo() { # o ponto está no acento (roxo)?
	timeout 5 grim "$run/t.png" && python3 -W ignore -c "
from PIL import Image
r, gr, b = Image.open('$run/t.png').convert('RGB').getpixel(($1, $2))
print('sim' if r > 140 and b > 200 else 'nao')"
}
"$g/painel" --secao sobre/atualizacoes >"$run/p.log" 2>&1 & pid=$!
sleep 6
clica 468 331; sleep 4          # Procurar (3 atualizações)
clica 606 331; sleep 4          # Escolher: o analisador leva 1 s
# nada marcado: a caixinha do vlc (414,486) não é roxa; "Atualizar" (985,420) está desativado (não é roxo)
[ "$(roxo 414 486)" = nao ] || falha "no começo nenhuma caixinha vem marcada"
# o pacote bloqueado (firefox, cadeado em 414,609) não se marca ao clicar na linha
clica 700 616
[ "$(roxo 414 609)" = nao ] || falha "um pacote bloqueado (puxa outros) NÃO pode ser marcado"
clica 700 688
# marcar o vlc: a caixinha fica roxa e o botão vira "Atualizar 1"
clica 700 493
[ "$(roxo 414 486)" = sim ] || falha "tocar na linha do vlc devia marcá-lo"
[ "$(roxo 985 420)" = sim ] || falha "com um marcado o botão Atualizar fica ativo (roxo)"
# marcar o htop também e desmarcar o vlc: só o htop
clica 700 555
clica 700 493
[ "$(roxo 414 486)" = nao ] || falha "tocar de novo desmarca"
clica 700 493
# "Marcar os liberados": marca todos os livres (vlc e htop) e nenhum bloqueado
clica 858 412
[ "$(roxo 414 486)" = sim ] && [ "$(roxo 414 548)" = sim ] || falha "Marcar os liberados devia marcar vlc e htop"
[ "$(roxo 414 609)" = nao ] || falha "...e nenhum bloqueado"
# Atualizar: o terminal roda o lumna-pkg com SÓ os pacotes marcados
clica 1037 412; sleep 3
grep -qx "/usr/lib/lumna/lumna-pkg upgrade-pacotes vlc htop" "$run/pkexec.log" || falha "o comando devia ser: pkexec /usr/lib/lumna/lumna-pkg upgrade-pacotes vlc htop (foi: $(cat "$run/pkexec.log" 2>&1))"
grep -q "firefox\|glibc\|libfoo\|linux" "$run/pkexec.log" 2>/dev/null && falha "nenhum pacote bloqueado pode ir no comando"
kill $pid 2>/dev/null; pkill -x orbit
[ $ok = 1 ] && { rm -rf "$run"; echo "painel_escolher: ok"; } || { echo "(em $run)"; exit 1; }
