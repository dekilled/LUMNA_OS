#!/usr/bin/env bash
# Teste do CEF para o navegador do LUMNA: o motor toca H.264+AAC? Tem Widevine? Quanta memória?
# Baixa o CEF com codecs (build de terceiros, ver docs/NAVEGADOR.md), confere o hash, compila um app de teste de ~100 linhas e abre UMA janela.
# Nada é instalado no sistema: tudo fica em ~/.cache/lumna-cef-teste (apague a pasta para limpar).
#
# Uso:   bash scripts/teste-cef-codecs.sh [--wayland] [--gpu] [--manter] [--oficial]
#   --wayland  usa o Wayland nativo (Ozone) em vez do XWayland/X11 (o padrão)
#   --gpu      liga a GPU (o padrão do teste é sem GPU, que é o mais compatível)
#   --manter   deixa a janela aberta no fim, para você olhar; feche com Ctrl+C
#   --oficial  usa o CEF oficial da Spotify (sem codecs), para comparar: o H.264 DEVE falhar nele
# Precisa de: bash, curl, tar+bzip2, cmake, ninja (ou make), g++, e os headers do X11 e do GTK3 (Arch: sudo pacman -S --needed base-devel cmake ninja libx11 gtk3).
# Rode dentro de uma sessão gráfica (no LUMNA, no Orbit, com o XWayland ligado).
set -u
aqui=$(cd "$(dirname "$0")" && pwd); fonte="$aqui/../docs/navegador/teste-cef"
tmp=${LUMNA_CEF_TESTE:-$HOME/.cache/lumna-cef-teste}
ozone=x11; gpu=0; manter=0; oficial=0
for a in "$@"; do case $a in --wayland) ozone=wayland;; --gpu) gpu=1;; --manter) manter=1;; --oficial) oficial=1;; -h|--help) sed -n 2,13p "$0"; exit 0;; *) echo "opção desconhecida: $a"; exit 2;; esac; done

die() { echo "ERRO: $*" >&2; exit 1; }
for c in curl tar cmake g++; do command -v $c >/dev/null || die "falta o comando '$c'"; done
if command -v ninja >/dev/null; then gerador=Ninja; else command -v make >/dev/null || die "falta ninja ou make"; gerador="Unix Makefiles"; fi
pkg-config --exists x11 2>/dev/null || die "faltam os headers do X11 (Arch: sudo pacman -S libx11)"
pkg-config --exists gtk+-3.0 2>/dev/null || die "faltam os headers do GTK3 (Arch: sudo pacman -S gtk3)"
{ [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; } || die "rode numa sessão gráfica (sem DISPLAY nem WAYLAND_DISPLAY)"
[ "$ozone" = x11 ] && [ -z "${DISPLAY:-}" ] && die "sem DISPLAY: o XWayland do Orbit não está ligado? Tente --wayland"

# --- 1. baixar e conferir -------------------------------------------------------------------------------------------------------------------------------
mkdir -p "$tmp/dl"
if [ -n "${CEF_DIR:-}" ]; then
  cef=$CEF_DIR; origem="pasta local $CEF_DIR"                          # uso interno: dist já extraída
elif [ $oficial = 1 ]; then
  nome=oficial
  if [ ! -f "$tmp/dl/oficial.tar.bz2" ]; then
    echo "→ achando o CEF oficial estável (linux64) no índice da Spotify…"
    arq=$(curl -fsSL https://cef-builds.spotifycdn.com/index.json | python3 -c "
import json,sys
v=[x for x in json.load(sys.stdin)['linux64']['versions'] if x['channel']=='stable']
f=[f for f in v[0]['files'] if f['type']=='minimal'][0]; print(f['name'])") || die "não consegui o índice oficial"
    echo "→ baixando $arq (~330 MB)…"; curl -fL --progress-bar -o "$tmp/dl/oficial.tar.bz2" "https://cef-builds.spotifycdn.com/$arq" || die "falha no download"
  fi
  tar="$tmp/dl/oficial.tar.bz2"; origem="CEF oficial da Spotify"
else
  nome=godwinmix
  base=https://github.com/psmux/godwinmix-cef/releases/download/cef-154.0.34
  arq='cef_binary_154.0.34%2Bg14c5a08%2Bchromium-154.0.8037.98_linux64_minimal.tar.bz2'
  esperado=9d7c391a45048e49a7c1282820bfe324331072cd57996b43ba9036f005354127   # hash que o mantenedor publicou, e o que eu conferi
  tar="$tmp/dl/$nome.tar.bz2"
  [ -f "$tar" ] || { echo "→ baixando o CEF 154 com H.264/AAC (155 MB)…"; curl -fL --progress-bar -o "$tar" "$base/$arq" || die "falha no download"; }
  [ "$(sha256sum "$tar" | cut -d' ' -f1)" = "$esperado" ] || die "o hash não bate com o esperado: apague $tar e tente de novo; se persistir, NÃO rode"
  origem="godwinmix-cef (build de terceiros, sem licença de patentes; hash conferido)"
fi
if [ -z "${CEF_DIR:-}" ]; then
  cef="$tmp/$nome"
  [ -d "$cef" ] || { echo "→ extraindo…"; mkdir -p "$cef.tmp" && tar -xjf "$tar" -C "$cef.tmp" && mv "$cef.tmp"/cef_binary* "$cef" && rmdir "$cef.tmp" || die "falha ao extrair"; }
fi
ver=$(grep -m1 -o 'Chromium [0-9.]*\|chromium-[0-9.]*' "$cef/README.txt" 2>/dev/null | head -1)
echo "→ usando: $origem ${ver:+($ver)}"

# --- 2. compilar o app de teste (contra os headers DESTA versão do CEF) ----------------------------------------------------------------------------------
b="$tmp/build-${nome:-local}"; rm -rf "$b"; mkdir -p "$b/src"
cp "$fonte/main.cc" "$fonte/pagina.html" "$fonte/teste.mp4" "$b/src/"
sed "s#^set(CEF_ROOT .*#set(CEF_ROOT \"$cef\")#" "$fonte/CMakeLists.txt" > "$b/src/CMakeLists.txt"
echo "→ compilando o app de teste (1 a 2 minutos)…"
( cd "$b" && cmake -S src -B obj -G "$gerador" -DCMAKE_BUILD_TYPE=Release >cmake.log 2>&1 && cmake --build obj -j"$(nproc)" >build.log 2>&1 ) || die "a compilação falhou; veja $b/cmake.log e $b/build.log"

# --- 3. pasta de execução: libcef + recursos + app + página ----------------------------------------------------------------------------------------------
run="$b/run"; mkdir -p "$run"; cp -r "$cef"/Release/* "$cef"/Resources/* "$run/"; cp "$b/obj/teste" "$b/src/pagina.html" "$b/src/teste.mp4" "$run/"
cdm=""; for d in /opt/google/chrome/WidevineCdm "$HOME"/.config/google-chrome/WidevineCdm/* /usr/lib/chromium/WidevineCdm /opt/brave.com/brave/WidevineCdm; do
  [ -f "$d/_platform_specific/linux_x64/libwidevinecdm.so" ] && { cdm=$d; break; }; done
# o Widevine (EME) não funciona em file://, então a página é servida por http://127.0.0.1 (só nesta máquina, enquanto o teste roda)
porta=$(python3 -c "import socket;s=socket.socket();s.bind(('127.0.0.1',0));print(s.getsockname()[1])" 2>/dev/null) || die "falta o python3 (para servir a página de teste)"
( cd "$run" && exec python3 -m http.server "$porta" --bind 127.0.0.1 >/dev/null 2>&1 ) & servidor=$!
trap 'kill $servidor 2>/dev/null' EXIT; sleep 1
export TESTE_OZONE=$ozone TESTE_URL="http://127.0.0.1:$porta/pagina.html" TESTE_CACHE="$b/cache"
[ $gpu = 1 ] && export TESTE_GPU=1; [ $manter = 1 ] && export TESTE_MANTER=1
if [ -n "$cdm" ]; then
  export TESTE_WIDEVINE="$cdm/_platform_specific/linux_x64/libwidevinecdm.so"
  export TESTE_WIDEVINE_VER=$(grep -o '"version"[^,]*' "$cdm/manifest.json" | grep -o '[0-9][0-9.]*' | head -1)
  echo "→ Widevine achado no seu Chrome/Chromium: $cdm (versão ${TESTE_WIDEVINE_VER:-?}); o teste vai tentar usá-lo"
else
  echo "→ nenhum Widevine encontrado no sistema (sem o Chrome instalado); o teste de DRM deve dar NAO, e isso é esperado"
fi

# --- 4. rodar --------------------------------------------------------------------------------------------------------------------------------------------
echo "→ abrindo a janela de teste (ozone=$ozone, gpu=$gpu)…"; log="$b/saida.txt"
( cd "$run" && timeout 90 ./teste ) 2>"$b/erros.txt" | tee "$log" &
testepid=$!
sleep 14
echo "→ memória real (PSS) do CEF com 1 janela:"
tot=0; n=0
for p in $(pgrep -f "$run/teste"); do kb=$(awk '/^Pss:/{s+=$2} END{print s+0}' /proc/$p/smaps_rollup 2>/dev/null); tot=$((tot+kb)); n=$((n+1)); done
echo "   $((tot/1024)) MB em $n processos"
wait $testepid

echo; echo "================ RESUMO ================"
grep '^RESULTADO' "$log" | sed 's/^RESULTADO //' || echo "(a página não respondeu; veja $b/erros.txt)"
echo "========================================"
echo "Pasta do teste: $b   (saída completa em saida.txt, erros do CEF em erros.txt)"
echo "Cole o RESUMO acima na conversa. Para limpar: rm -rf $tmp"
