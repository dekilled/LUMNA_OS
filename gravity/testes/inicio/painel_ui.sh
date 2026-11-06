#!/bin/sh
# O Painel (Inicialização e Privacidade) com cliques de verdade num Orbit headless e o mouse virtual: os interruptores e o Aplicar gravam o que dizem.
# sh painel_ui.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY ARRASTO2
orbit_dir=${1:?}; g=${2:?}; M=${3:?}
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg/lumna" "$run/cfg/autostart" "$run/etc/greetd" "$run/etc/xdg"
export PATH=$g/testes/painel/bin:$PATH XDG_RUNTIME_DIR=$run XDG_CONFIG_HOME=$run/cfg XDG_CONFIG_DIRS=$run/etc/xdg LUMNA_ETC=$run/etc FUNDO_ESTADO=$run/fundo.txt
printf '[Desktop Entry]\nType=Application\nName=Syncthing\nExec=syncthing --no-browser\n' > "$run/cfg/autostart/syncthing.desktop"
printf 'syncthing.service sim sim Syncthing\nbackup.service nao nao Copia\n' > "$run/fundo.txt"
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman "$orbit_dir/orbit" >"$run/orbit.log" 2>&1 &
orbit=$!
sleep 2
export WAYLAND_DISPLAY=$(ls "$run"/orbit.*.sock | head -1 | xargs basename | sed 's/^orbit\.//; s/\.sock$//')
clicar_em() { "$M" casa; sleep 0.2; "$M" mover "$1" "$2"; sleep 0.3; "$M" clicar; sleep 0.8; }
# clica e confere; se a condição não valeu (o Painel recém-aberto às vezes perde o clique), tenta de novo, até 3 vezes
ate_que() { cond=$1; shift; for _ in 1 2 3; do clicar_em "$@"; sleep 0.5; eval "$cond" && return 0; done; return 1; }
abrir() { "$g/painel" --secao "$1" >"$run/p.log" 2>&1 & pid=$!; sleep 5; }
fechar() { kill $pid 2>/dev/null; wait $pid 2>/dev/null; }

# Apps ao iniciar: desligar o Syncthing (o interruptor da primeira linha)
abrir inicializacao/apps
ate_que 'grep -q "Hidden=true" "$run/cfg/autostart/syncthing.desktop"' 1049 329
grep -q "Hidden=true" "$run/cfg/autostart/syncthing.desktop" || fail "o interruptor devia desligar o Syncthing (Hidden=true): $(cat "$run/cfg/autostart/syncthing.desktop")"
fechar

# Ao entrar: ligar "Reabrir as janelas" e Aplicar
abrir inicializacao/entrar
clicar_em 1165 446
ate_que 'grep -q "reabrir = sim" "$run/cfg/lumna/inicio.ini" 2>/dev/null' 1149 674
grep -q "reabrir = sim" "$run/cfg/lumna/inicio.ini" 2>/dev/null || fail "o Aplicar devia gravar reabrir = sim ($(cat "$run/cfg/lumna/inicio.ini" 2>/dev/null))"
fechar

# Em segundo plano: ligar o backup (o interruptor da primeira linha é o do backup, em ordem de nome)
abrir inicializacao/fundo
ate_que 'grep -q "^backup.service sim" "$run/fundo.txt"' 1165 344
grep -q "^backup.service sim" "$run/fundo.txt" || fail "o interruptor devia ligar o backup: $(cat "$run/fundo.txt")"
fechar

# Entrada: desligar o login automático (sem pkexec: o ajudante roda direto, no /etc falso)
printf '[default_session]\ncommand = "x"\nuser = "greeter"\n\n[initial_session]\ncommand = "lumna-orbit-session"\nuser = "maria"\n' > "$run/etc/greetd/config.toml"
export LUMNA_ENTRADA_AJUSTAR="$g/../session/lumna-entrada-ajustar" LUMNA_ENTRADA_SEM_PKEXEC=1
abrir inicializacao/entrada
ate_que '! grep -q initial_session "$run/etc/greetd/config.toml"' 1165 327
sleep 1
grep -q "initial_session" "$run/etc/greetd/config.toml" && fail "o interruptor devia desligar o login automático: $(cat "$run/etc/greetd/config.toml")"
fechar

# Privacidade → Histórico: desligar "lembrar os recentes" grava o ini, avisa o GTK e esvazia a lista
mkdir -p "$run/share"
export XDG_DATA_HOME=$run/share
printf '<xbel><bookmark href="file:///a"/></xbel>' > "$run/share/recently-used.xbel"
abrir privacidade/historico
ate_que 'grep -q "lembrar_recentes = nao" "$run/cfg/lumna/privacidade.ini" 2>/dev/null' 1165 327
grep -q "lembrar_recentes = nao" "$run/cfg/lumna/privacidade.ini" 2>/dev/null || fail "o interruptor devia gravar lembrar_recentes = nao"
grep -q "gtk-recent-files-enabled=false" "$run/cfg/gtk-3.0/settings.ini" 2>/dev/null || fail "o GTK devia parar de lembrar"
grep -q "<bookmark " "$run/share/recently-used.xbel" && fail "a lista de recentes devia ter sido esvaziada"
fechar

# Energia → Perfil: "Usar" a Economia vale na hora; o interruptor automático e o Aplicar gravam o energia.ini
echo balanced > "$run/perfil.txt"
export PERFIL_ESTADO=$run/perfil.txt
abrir energia/perfil
ate_que '[ "$(cat "$run/perfil.txt")" = "power-saver" ]' 1157 461
[ "$(cat "$run/perfil.txt")" = "power-saver" ] || fail "Usar devia pôr o perfil em power-saver: $(cat "$run/perfil.txt")"
clicar_em 1165 568
ate_que 'grep -q "perfil_auto = sim" "$run/cfg/lumna/energia.ini" 2>/dev/null' 1149 674
grep -q "perfil_auto = sim" "$run/cfg/lumna/energia.ini" 2>/dev/null || fail "o Aplicar devia gravar perfil_auto = sim ($(cat "$run/cfg/lumna/energia.ini" 2>/dev/null))"
fechar

# Usuários → Outros: o interruptor de administrador chama o ajudante com os argumentos certos (um ajudante de mentira anota)
printf 'root:x:0:0::/root:/bin/bash\nmaria:x:1000:1000:Maria:/home/maria:/bin/bash\njoao:x:1001:1001:Joao Silva:/home/joao:/bin/bash\n' > "$run/etc/passwd"
printf 'wheel:x:998:maria\n' > "$run/etc/group"
printf '#!/bin/sh\necho "$@" >> %s/ajudante.log\n' "$run" > "$run/ajudante"; chmod +x "$run/ajudante"
export LUMNA_USUARIO=maria LUMNA_USUARIOS_AJUDANTE="$run/ajudante" LUMNA_SEM_PKEXEC=1
abrir usuarios/outros
ate_que 'grep -qx "admin joao sim" "$run/ajudante.log" 2>/dev/null' 1049 287
sleep 1
grep -qx "admin joao sim" "$run/ajudante.log" 2>/dev/null || fail "o interruptor devia pedir 'admin joao sim' ao ajudante: $(cat "$run/ajudante.log" 2>/dev/null)"
fechar

kill $orbit 2>/dev/null; wait 2>/dev/null
[ $falhas -eq 0 ] && { echo "Painel (Inicialização) com cliques: ok"; rm -rf "$run"; exit 0; }
tail -3 "$run/p.log" >&2; exit 1
