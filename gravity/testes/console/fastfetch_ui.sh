#!/bin/sh
# O fastfetch roda sozinho na primeira aba do Terminal do LUMNA (via ~/.bashrc do skel), e não nas abas seguintes. Usa um fastfetch de mentira.
# sh fastfetch_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY PASTA_DA_SESSAO
orbit_dir=${1:?}; g=${2:?}; sessao=${3:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/casa" "$run/bin"
cp "$sessao/bashrc" "$run/casa/.bashrc"
printf '[[ -f ~/.bashrc ]] && . ~/.bashrc\n' > "$run/casa/.bash_profile"   # o skel do Arch faz isto
printf '#!/bin/sh\ntouch "$FX_MARCA"\n' > "$run/bin/fastfetch"; chmod +x "$run/bin/fastfetch"
export XDG_RUNTIME_DIR=$run HOME=$run/casa LANG=C.UTF-8 SHELL=/bin/bash PATH=$run/bin:$PATH FX_MARCA=$run/marca
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 3
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
"$g/console" >"$run/c.log" 2>&1 &
sleep 3
falhas=0
[ -e "$run/marca" ] || { echo "FALHOU: o fastfetch devia rodar sozinho na primeira aba"; falhas=1; }
rm -f "$run/marca"
wtype -M ctrl -M shift t -m shift -m ctrl; sleep 2
[ ! -e "$run/marca" ] || { echo "FALHOU: o fastfetch não devia rodar numa aba nova"; falhas=1; }
pkill -x console; kill $orbit 2>/dev/null
[ "$falhas" -eq 0 ] && echo "fastfetch automático (primeira aba): ok" || exit 1
