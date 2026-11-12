#!/bin/sh
# O desktop novo do LUMNA (Orbit + Gravity) numa janela, dentro do desktop que já está aberto
# (Hyprland, GNOME, KDE...), para testar no PC sem trocar de sessão:
#
#   ./testar-desktop.sh            compila o que faltar e abre
#   ./testar-desktop.sh --tela-cheia  abre a janela do Orbit já em tela cheia (Super+F11 alterna; vale junto com --refazer)
#   ./testar-desktop.sh --refazer  recompila o Gravity e o Orbit antes de abrir (use depois de dar git pull aqui:
#                                  sem isso ele abre os binários de antes e você não vê as mudanças)
#
# O Orbit é o orbit/ deste mesmo repositório (compila ali; o binário fica fora do git).
# Lá dentro sobem o papel de parede, a topbar, a dock e os avisos, num D-Bus só deles (os avisos
# não tiram as notificações do seu desktop). Alt+Enter abre um terminal; fechar a janela sai.
# Para testar os avisos lá dentro: abra o terminal (Alt+Enter) e corra notify-send "Olá" "LUMNA".
REFAZER=0
for a in "$@"; do
	case "$a" in
	--refazer) REFAZER=1 ;;
	--tela-cheia) export ORBIT_TELA_CHEIA=1 ;; # a janela do Orbit abre em tela cheia (depois, Super+F11 alterna)
	esac
done
set -e

AQUI=$(cd "$(dirname "$0")" && pwd)
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/lumna
BIN=$CACHE/bin

falta() {
	echo "Falta: $1"
	echo "No Arch: sudo pacman -S --needed base-devel git wayland wayland-protocols libxkbcommon libinput cairo pango \\"
	echo "         glib2 gdk-pixbuf2 libpulse wlroots0.20 dbus foot libnotify \\"
	echo "         networkmanager bluez bluez-utils polkit"
	exit 1
}

if [ -z "$WAYLAND_DISPLAY" ]; then
	echo "Abra isto de dentro de um desktop Wayland (o Orbit abre como uma janela dele)."
	exit 1
fi
for p in wayland-client wayland-protocols wayland-scanner xkbcommon cairo pangocairo glib-2.0 gio-2.0 gdk-pixbuf-2.0 libpulse libinput; do
	pkg-config --exists "$p" || falta "$p"
done
command -v dbus-run-session >/dev/null || falta "dbus-run-session (pacote dbus)"

# o Gravity: a biblioteca e as peças do desktop
if [ "$REFAZER" = 1 ] || [ ! -x "$AQUI/avisos" ]; then
	make -C "$AQUI" -j"$(nproc)"
fi

# o Orbit: compila no wlroots 0.18, 0.19 e 0.20 (o Arch de hoje só tem o 0.20; o 0.18 do AUR já
# não compila com a libinput nova). O módulo muda de nome conforme a distro e a versão.
WLR=""
for m in wlroots-0.20 wlroots-0.19 wlroots-0.18 wlroots; do
	if pkg-config --exists "$m"; then
		WLR=$m
		break
	fi
done
[ -n "$WLR" ] || falta "wlroots (no Arch: wlroots0.20)"
O="$AQUI/../orbit"
if [ "$REFAZER" = 1 ] || [ ! -x "$O/orbit" ]; then
	rm -f "$O/orbit"
	make -C "$O" clean >/dev/null 2>&1 || true
	cp "$O/makefile.txt" "$O/Makefile"
	make -C "$O" PKGS="$WLR wayland-server xkbcommon libinput"
fi

# os nomes que as peças chamam umas às outras (a dock abre o lumna-lancador, o sino o lumna-avisos...)
mkdir -p "$BIN"
for p in papel barra dock lancador avisos bloqueio entrada ocioso modos connect midia emma emma-chat emma-app perfil captura painel apps rede transferencia mixer wifi galeria widgets loja setup jogos gamepad; do
	[ -x "$AQUI/$p" ] && ln -sf "$AQUI/$p" "$BIN/lumna-$p"
done
[ -x "$AQUI/musica" ] && ln -sf "$AQUI/musica" "$BIN/lumna-music"   # o app musica instala como lumna-music
ln -sf "$O/orbit-msg" "$BIN/orbit-msg"
ln -sf "$AQUI/../session/lumna-modo" "$BIN/lumna-modo"

# o que não derruba o desktop, mas deixa uma função muda: avisa antes de abrir
opcional() { command -v "$1" >/dev/null 2>&1 || echo "  falta $1: $2"; }
echo "Opcionais que faltam (o desktop abre sem eles):"
opcional wtype "o direcional, o texto e o menu do controle do celular não funcionam (pacman -S wtype)"
opcional wl-copy "a área de transferência do Connect (pacman -S wl-clipboard)"
opcional grim "a captura de tela (pacman -S grim)"
opcional wf-recorder "a gravação de tela (pacman -S wf-recorder)"
opcional firefox "a Mídia (pacman -S firefox; o comum só liga a extensão se for assinada: veja extensao-firefox/LEIAME.md)"
echo "Para ver o que está faltando: sh ../scripts/diag-connect.sh (dentro do desktop, no Alt+Enter)"

echo "Abrindo o desktop novo numa janela (feche-a para sair)..."
export PATH="$BIN:$PATH"
export ORBIT_BORDA=0 # as janelas desenham a própria moldura, de cantos redondos
exec dbus-run-session -- "$O/orbit" -s 'lumna-papel & lumna-desktop & lumna-barra & lumna-dock & lumna-avisos & lumna-widgets & lumna-ocioso & lumna-connect & { mkdir -p ~/.local/state/lumna; lumna-emma 2>>~/.local/state/lumna/emma.log; } & lumna-midia servico &
	sleep 1; notify-send -a LUMNA "Bem-vindo ao desktop novo" "Os avisos chegam aqui. O sino na topbar abre a central." 2>/dev/null'
