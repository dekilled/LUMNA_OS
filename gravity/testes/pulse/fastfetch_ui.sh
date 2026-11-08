#!/bin/sh
# O fastfetch roda sozinho na primeira aba do Pulse Terminal (pela integração com o bash, sem arquivo nenhum no /etc/skel), e não nas abas seguintes; o prompt de fábrica do Arch
# ganha o lilás. Usa um fastfetch de mentira. sh fastfetch_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY
orbit_dir=${1:?}; g=${2:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/casa" "$run/bin"
cat > "$run/casa/.bashrc" <<'RC'   # o ~/.bashrc de fábrica do Arch (só o prompt)
PS1='[\u@\h \W]\$ '
RC
printf '[[ -f ~/.bashrc ]] && . ~/.bashrc\n' > "$run/casa/.bash_profile"
printf '#!/bin/sh\ntouch "$FX_MARCA"\n' > "$run/bin/fastfetch"; chmod +x "$run/bin/fastfetch"
export XDG_RUNTIME_DIR=$run HOME=$run/casa LANG=C.UTF-8 SHELL=/bin/bash PATH=$run/bin:$PATH FX_MARCA=$run/marca LUMNA_PULSE_INTEGRACAO=$g/pulse-shell/integracao.bash
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 3
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
"$g/pulse" >"$run/c.log" 2>&1 &
sleep 3
falhas=0
[ -e "$run/marca" ] || { echo "FALHOU: o fastfetch devia rodar sozinho na primeira aba"; falhas=1; }
rm -f "$run/marca"
wtype 'printf "%s" "$PS1" > '$run/ps1; wtype -k Return; sleep 1
grep -q '38;2;167;139;250' "$run/ps1" || { echo "FALHOU: o prompt de fábrica do Arch devia ganhar o lilás: [$(cat "$run/ps1")]"; falhas=1; }
wtype -M ctrl -M shift t -m shift -m ctrl; sleep 2
[ ! -e "$run/marca" ] || { echo "FALHOU: o fastfetch não devia rodar numa aba nova"; falhas=1; }
pkill -x pulse; kill $orbit 2>/dev/null
[ "$falhas" -eq 0 ] && echo "fastfetch automático (primeira aba): ok" || exit 1
