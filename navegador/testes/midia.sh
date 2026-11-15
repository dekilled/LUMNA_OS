#!/bin/sh
# Teste da Mídia do navegador do LUMNA: abre no Orbit sem tela (XWayland) com dois "serviços" locais e guia só pelo teclado:
#   Enter abre um serviço -> F2 abre o menu (o site encolhe, tira print e lê o progresso) -> Enter troca de serviço (guarda endereço, progresso e
#   print) -> F2/Esc volta ao vídeo -> Alt+Home vai ao início, que passa a mostrar o "Continuar assistindo".
# Uso: sh navegador/testes/midia.sh <pasta do orbit> <pasta stage do navegador (navegador/build/stage)>
# Precisa de Xwayland e python3. Com grim, também guarda screenshots em <log>/*.png.
orbit=${1:-orbit}; stage=${2:-navegador/build/stage}; aqui=$(cd "$(dirname "$0")" && pwd)
orbit=$(cd "$orbit" && pwd); stage=$(cd "$stage" && pwd)
command -v Xwayland >/dev/null || { echo "midia: dispensado (falta Xwayland)"; exit 0; }
[ -x "$stage/lumna-navegador" ] || { echo "midia: dispensado (compile antes: CEF_ROOT=... sh navegador/dev.sh compilar)"; exit 0; }
run=$(mktemp -d); chmod 700 "$run"; mkdir -p "$run/cfg" "$run/home" "$run/dados"
export HOME="$run/home" XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR="$run"
porta=$(python3 -c "import socket;s=socket.socket();s.bind(('127.0.0.1',0));print(s.getsockname()[1])")
python3 "$aqui/servidor.py" "$porta" "$aqui/paginas" >/dev/null 2>&1 & srv=$!
# cada serviço tem o seu domínio (como os de verdade); o Beta é em "localhost" e LENTO (1,5 s), para simular uma página que demora a carregar
printf '[{"id":"alfa","nome":"Serviço Alfa","url":"http://127.0.0.1:%s/a.html","dominios":["127.0.0.1"]},{"id":"beta","nome":"Serviço Beta","url":"http://localhost:%s/lento/b.html","dominios":["localhost"]},{"id":"gama","nome":"Serviço Gama","url":"http://127.0.0.1:%s/a.html","dominios":["127.0.0.1"]}]' "$porta" "$porta" "$porta" > "$run/servicos.json"
# um endereço errado gravado por uma versão antiga (a página de outro serviço com o nome do Gama): tem de ser descartado ao abrir
printf '{"salvos":{"gama":{"url":"http://localhost:%s/lento/b.html","titulo":"errado","thumb":"","pos":0,"dur":0,"quando":1}}}' "$porta" > "$run/dados/midia.json"
limpa() { kill $nav $orb $srv 2>/dev/null; sleep 1; kill -9 $nav $orb 2>/dev/null; pkill -f "$run" 2>/dev/null; }
cd "$orbit" || exit 1
LUMNA_TESTE_TECLADOS=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 ./orbit >"$run/orbit.log" 2>&1 & orb=$!
sleep 2
D=$(grep -o "DISPLAY=:[0-9]*" "$run/orbit.log" | head -1 | cut -d= -f2)
[ -n "$D" ] || { echo "midia: dispensado (esta wlroots não tem XWayland)"; kill $orb $srv; rm -rf "$run"; exit 0; }
sock=$(ls "$run"/orbit.*.sock | head -1); W=$(ls "$run" | grep -m1 '^wayland-[0-9]*$')
msg() { ./orbit-msg -s "$sock" "$@"; }
tecla() { msg tecla_falsa 0 "$@" >/dev/null; }
foto() { command -v grim >/dev/null && WAYLAND_DISPLAY=$W grim "$run/$1.png" 2>/dev/null; }
# aberto de OUTRA pasta (não a do executável): garante que a libcef.so e o icudtl.dat sejam achados ao lado do executável ($ORIGIN)
cd "$run" && DISPLAY=$D LUMNA_NAV_DEBUG=1 LUMNA_NAV_SEM_GPU=1 LUMNA_NAV_DADOS="$run/dados" LUMNA_NAV_SERVICOS="$run/servicos.json" "$stage/lumna-navegador" >"$run/nav.log" 2>&1 & nav=$!
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
conta() { grep -a -c "$1" "$run/nav.log"; }
espera() { i=0; while [ "$(conta "$1")" -lt "${3:-1}" ] && [ $i -lt $((${2:-10} * 5)) ]; do sleep 0.2; i=$((i + 1)); done; [ "$(conta "$1")" -ge "${3:-1}" ]; }

# 0. o endereço errado do Gama (de outro serviço) é descartado ao carregar o banco, em vez de ser aberto depois em "Retomar"
espera "descartado gama" 25 || falha "o endereço do Gama que pertence a outro serviço devia ser descartado ao abrir"
# 1. a janela abre SEM moldura e a interface (HTML) responde pela ponte
espera "shell: desenhou salvos=0 servicos=3" 25 || falha "a interface não desenhou os 3 serviços ($(grep -a '^NAV' "$run/nav.log" | tail -3 | tr '\n' '|'))"
msg janelas | tr '{' '\n' | grep -q '"titulo":"LUMNA Mídia".*"barra":false' || falha "a janela da Mídia devia abrir sem moldura nem barra do Orbit"
foto 1-inicio
# 2. Enter no primeiro serviço
tecla 28
espera "estado servico alfa" 10 || falha "Enter devia abrir o Serviço Alfa"
espera "site carregou http://127.0.0.1:$porta/a.html" 20 || falha "o site do Alfa devia carregar"
sleep 2.2; foto 2-servico   # deixa o vídeo andar um pouco, para o progresso guardado ser > 0
# 3. F2: o site encolhe, a captura chega
tecla 60
espera "estado menu alfa" 10 || falha "F2 devia abrir o menu"
espera "captura [0-9]* bytes" 10 || falha "ao abrir o menu devia chegar o print do site"
sleep 1; foto 3-menu
larg=$(grep -a "fim da animação" "$run/nav.log" | tail -1 | sed 's/.* \([0-9]*\)x[0-9]*$/\1/')
[ "${larg:-9999}" -lt 1000 ] || falha "o site devia encolher com o menu aberto (largura final $larg)"
# 4. Enter: troca para o próximo serviço (o foco começa nele)
tecla 28
espera "estado servico beta" 10 || falha "Enter no menu devia trocar para o Serviço Beta"
espera "guardado alfa" 5 || falha "ao trocar devia guardar o Alfa"
espera "site carregou http://localhost:$porta/lento/b.html" 20 || falha "o site do Beta devia carregar"
foto 4-beta
grep -q '"alfa":{"url":"http://127.0.0.1:[0-9]*/a.html"' "$run/dados/midia.json" || falha "o último endereço do Alfa devia estar no banco"
grep -q '"alfa".*"pos":[1-9]' "$run/dados/midia.json" || falha "o progresso do vídeo do Alfa (pos) devia ter sido guardado: $(head -c 300 "$run/dados/midia.json")"
[ "$(wc -c < "$run/dados/thumbs/alfa.jpg" 2>/dev/null || echo 0)" -gt 1000 ] || falha "o print do Alfa devia estar em thumbs/alfa.jpg"
# 5. F2 e Esc: volta ao vídeo, no mesmo serviço
sleep 1; tecla 60; espera "estado menu beta" 10 || falha "F2 devia abrir o menu de novo"
sleep 1.5; tecla 1; espera "estado servico beta" 10 2 || falha "Esc devia voltar ao vídeo"
# 6. Alt+Home: início; o print do Beta chega ANTES de guardar e o início mostra os 2 em "Continuar assistindo"
sleep 1; msg tecla_falsa 0 56 baixo >/dev/null; sleep 0.1; tecla 102; sleep 0.1; msg tecla_falsa 0 56 cima >/dev/null
espera "estado inicio" 10 || falha "Alt+Home devia ir ao início"
espera "shell: desenhou salvos=2 servicos=3" 10 || falha "o início devia mostrar os dois serviços já assistidos (o Beta guardado ao sair do vídeo)"
[ "$(wc -c < "$run/dados/thumbs/beta.jpg" 2>/dev/null || echo 0)" -gt 1000 ] || falha "o print do Beta devia ter sido guardado ao ir para o início"
sleep 0.5; foto 5-inicio
# 7. do início, de novo para um serviço
tecla 28; espera "estado servico alfa" 10 2 || falha "Enter no início devia abrir o serviço de novo"
grep -a -q "estilo\|Cannot add multiple" "$run/nav.log" && grep -a -q "Cannot add multiple" "$run/nav.log" && falha "o CEF recusou as duas visões na mesma janela"
# 8. a CORRIDA: troca para o Beta (lento) e, ANTES de ele carregar, troca de novo. O endereço que ainda está na tela é o do Alfa: não pode ser
#    gravado com o nome do Beta (era o que fazia o "Retomar" de um serviço abrir a página de outro).
sleep 1; tecla 60; espera "estado menu alfa" 10 2 || falha "F2 devia abrir o menu (corrida)"
sleep 1; tecla 28; espera "estado servico beta" 10 3 || falha "devia trocar para o Beta (corrida)"
sleep 0.3; tecla 60; espera "estado menu beta" 10 2 || falha "F2 logo depois da troca devia abrir o menu"
tecla 106; sleep 0.2; tecla 28
espera "não guardei beta" 8 || falha "trocar ANTES de a página nova carregar devia recusar gravar o endereço da página antiga com o nome do Beta ($(grep -a 'guardado beta' "$run/nav.log" | tail -2 | tr '\n' '|'))"
grep -a -q '"beta":{"url":"http://localhost:[0-9]*/lento/b.html"' "$run/dados/midia.json" || falha "o endereço do Beta no banco devia continuar sendo o do próprio Beta: $(head -c 400 "$run/dados/midia.json")"
if [ $ok = 1 ]; then echo "midia: ok"; limpa; rm -rf "$run"; exit 0; fi
echo "(log em $run: nav.log e os screenshots *.png)" >&2; limpa; exit 1
